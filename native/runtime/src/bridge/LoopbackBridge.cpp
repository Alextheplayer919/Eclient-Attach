// LoopbackBridge — the in-game half of the companion-app link (docs/MODULE_SDK.md,
// "the agent" in docs/MERGE_PLAN.md).
//
// It used to serve a status blob built from GameBridge. It now speaks the module
// SDK contract: newline-delimited JSON on 127.0.0.1:38170, a token on every
// line, semantic verbs only, and a ~20 Hz state push sourced from AgentState
// (which the game thread latches once per tick).
//
// Two rules this file exists to enforce:
//
//   1. NO RAW MEMORY OVER THE WIRE. There is no readMem/writeMem verb and there
//      never will be: any app on the device can reach a loopback port, so a
//      generic memory read/write verb would be an arbitrary-write primitive into
//      Minecraft handed to every other app on the phone. The app asks for
//      MEANING ("what can you see", "swing at that actor") and this side decides
//      how to satisfy it with validated offsets.
//
//   2. NEVER BLOCK THE GAME. This thread never touches game memory: it only
//      copies an already-latched snapshot (AgentState's seqlock) and appends
//      commands to a lock-free ring the tick drains. If this whole thread dies,
//      the game does not notice.
//
// Failure posture matches the rest of the runtime: if anything is unavailable
// (layout not derived, ring full, no snapshot this push) the answer is an
// explicit ok:false with a reason, never a silent success.

#include "bridge/LoopbackBridge.h"

#include "bridge/AgentState.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <android/log.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace eclient_runtime {
namespace {

constexpr const char* TAG = "EClient-Agent";
constexpr int kPort = 38170;
constexpr int kPushIntervalMs = 50;       // ~20 Hz
constexpr int kMaxLineBytes = 8192;       // a command line this long is abuse

// Token file: best-effort, because the app runs in a different UID and cannot
// read this process's private files. /sdcard/Download is the one place a file
// manager, the user, and (usually) the app can all reach — and the same place
// the injector track already writes its boot log.
constexpr const char* kTokenPaths[] = {
    "/sdcard/Download/eclient_agent.token",
    "/sdcard/eclient_agent.token",
};

// ── token ───────────────────────────────────────────────────────────────────

std::string readTokenFile(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    char buf[128]{};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

void writeTokenFile(const std::string& token) {
    for (const char* path : kTokenPaths) {
        FILE* f = std::fopen(path, "wb");
        if (!f) continue;
        std::fwrite(token.c_str(), 1, token.size(), f);
        std::fputc('\n', f);
        std::fclose(f);
        __android_log_print(ANDROID_LOG_INFO, TAG, "agent token written to %s", path);
        return;   // one location is enough
    }
    __android_log_print(ANDROID_LOG_WARN, TAG,
                        "could not write the token file (storage denied); token is in this log only");
}

std::string generateToken() {
    // 16 hex chars from /dev/urandom. Not a password: it exists to stop other
    // apps on the device from talking to this port, and that is all it claims.
    unsigned char raw[8]{};
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        const size_t got = std::fread(raw, 1, sizeof(raw), f);
        std::fclose(f);
        if (got != sizeof(raw)) {
            for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = static_cast<unsigned char>(i * 37 + 11);
        }
    }
    char out[17]{};
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 2, 3, "%02x", raw[i]);
    return std::string(out);
}

std::string& agentToken() {
    static std::string token = [] {
        // Debug/override hook: the self-test and a manual session can pin the
        // token instead of hunting for the generated one.
        if (const char* env = std::getenv("ECLIENT_AGENT_TOKEN"); env && *env) {
            return std::string(env);
        }
        // A pre-existing file pins the token (lets the user set their own, and
        // keeps the pairing stable across reinstall/reboot).
        for (const char* path : kTokenPaths) {
            const std::string existing = readTokenFile(path);
            if (existing.size() >= 8) {
                __android_log_print(ANDROID_LOG_INFO, TAG, "agent token loaded from %s", path);
                return existing;
            }
        }
        std::string fresh = generateToken();
        writeTokenFile(fresh);
        __android_log_print(ANDROID_LOG_INFO, TAG, "agent token generated: %s", fresh.c_str());
        return fresh;
    }();
    return token;
}

// ── minimal flat-JSON reader ────────────────────────────────────────────────
// The command lines are flat objects of scalars, so a full JSON parser would be
// more surface than the job needs. Every helper is total: missing key or wrong
// type returns false and the caller answers ok:false with a reason.

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool findKey(const std::string& s, const char* key, size_t& valuePos) {
    const std::string needle = std::string("\"") + key + "\"";
    const size_t at = s.find(needle);
    if (at == std::string::npos) return false;
    size_t i = at + needle.size();
    skipWs(s, i);
    if (i >= s.size() || s[i] != ':') return false;
    ++i;
    skipWs(s, i);
    valuePos = i;
    return true;
}

bool jStr(const std::string& s, const char* key, std::string& out) {
    size_t i = 0;
    if (!findKey(s, key, i) || i >= s.size() || s[i] != '"') return false;
    ++i;
    std::string v;
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        v.push_back(s[i++]);
    }
    out = v;
    return i < s.size();
}

bool jNum(const std::string& s, const char* key, double& out) {
    size_t i = 0;
    if (!findKey(s, key, i) || i >= s.size()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i) return false;
    out = v;
    return true;
}

bool jBool(const std::string& s, const char* key, bool& out) {
    size_t i = 0;
    if (!findKey(s, key, i)) return false;
    if (s.compare(i, 4, "true") == 0) { out = true; return true; }
    if (s.compare(i, 5, "false") == 0) { out = false; return true; }
    return false;
}

// ── push builder ────────────────────────────────────────────────────────────

void appendCaps(std::string& out, const agentstate::Caps& c) {
    out += "\"caps\":[";
    bool first = true;
    auto add = [&](const char* name, bool on) {
        if (!on) return;
        if (!first) out += ',';
        first = false;
        out += '"';
        out += name;
        out += '"';
    };
    add("Pose", c.pose);
    add("Entities", c.entities);
    add("ActorType", c.actorType);
    add("Attack", c.attack);
    add("Rotation", c.rotation);
    add("MoveInput", c.moveInput);
    add("Inventory", c.inventory);
    out += "],\"missing\":[";
    first = true;
    auto miss = [&](const char* name, bool on) {
        if (on) return;
        if (!first) out += ',';
        first = false;
        out += '"';
        out += name;
        out += '"';
    };
    miss("Pose", c.pose);
    miss("Entities", c.entities);
    miss("ActorType", c.actorType);
    miss("Attack", c.attack);
    miss("Rotation", c.rotation);
    miss("MoveInput", c.moveInput);
    miss("Inventory", c.inventory);
    out += ']';
}

std::string buildStateLine(const agentstate::Snapshot& s) {
    char buf[512];
    std::string out;
    out.reserve(1024 + s.actorCount * 128);
    out += "{\"push\":\"state\",";

    std::snprintf(buf, sizeof(buf), "\"t\":%llu,\"live\":%s,\"range\":%.0f,\"mode\":%d,",
                  static_cast<unsigned long long>(s.tick),
                  s.live ? "true" : "false", s.scanRange, s.scanMode);
    out += buf;

    if (s.self.valid) {
        std::snprintf(buf, sizeof(buf),
                      "\"self\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
                      "\"vx\":%.3f,\"vy\":%.3f,\"vz\":%.3f,\"hp\":%.1f,"
                      "\"hurt\":%s,\"dmgRecent\":%.0f,\"sinceHurt\":%.0f},",
                      s.self.x, s.self.y, s.self.z, s.self.vx, s.self.vy, s.self.vz,
                      s.self.health, s.self.damaged ? "true" : "false",
                      s.self.damageRecent, s.self.ticksSinceDamage);
        out += buf;
    } else {
        out += "\"self\":null,";
    }

    out += "\"entities\":[";
    for (int i = 0; i < s.actorCount; ++i) {
        const agentstate::ActorState& a = s.actors[i];
        std::snprintf(buf, sizeof(buf),
                      "%s{\"id\":%u,\"type\":%u,\"player\":%s,\"hurt\":%s,\"hp\":%d,"
                      "\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"dist\":%.2f}",
                      i ? "," : "", a.id, a.typeId, a.player ? "true" : "false",
                      a.hurt ? "true" : "false", a.health, a.x, a.y, a.z, a.dist);
        out += buf;
    }
    out += "],";

    appendCaps(out, s.caps);

    out += ",\"modules\":[";
    for (int i = 0; i < s.moduleCount; ++i) {
        if (i) out += ',';
        out += "{\"name\":\"";
        out += s.modules[i].name;
        out += "\",\"category\":\"";
        out += s.modules[i].category;
        out += "\",\"enabled\":";
        out += s.modules[i].enabled ? "true" : "false";
        out += ",\"kotlin\":";
        out += s.modules[i].kotlin ? "true" : "false";
        out += '}';
    }
    out += ']';

    std::snprintf(buf, sizeof(buf),
                  ",\"cmds\":{\"aq\":%llu,\"ae\":%llu,\"af\":%llu,"
                  "\"rq\":%llu,\"re\":%llu,\"rf\":%llu,"
                  "\"iq\":%llu,\"ie\":%llu,\"if\":%llu,\"drop\":%llu}",
                  static_cast<unsigned long long>(s.counters.attacksQueued),
                  static_cast<unsigned long long>(s.counters.attacksExecuted),
                  static_cast<unsigned long long>(s.counters.attacksFailed),
                  static_cast<unsigned long long>(s.counters.rotationsQueued),
                  static_cast<unsigned long long>(s.counters.rotationsExecuted),
                  static_cast<unsigned long long>(s.counters.rotationsFailed),
                  static_cast<unsigned long long>(s.counters.inputsQueued),
                  static_cast<unsigned long long>(s.counters.inputsExecuted),
                  static_cast<unsigned long long>(s.counters.inputsFailed),
                  static_cast<unsigned long long>(s.counters.commandsDropped));
    out += buf;

    // The funnel line is included so "the app sees nothing" is always
    // accompanied by the runtime's own explanation of why.
    out += ",\"diag\":\"";
    for (const char* p = s.diag; *p; ++p) {
        if (*p == '"' || *p == '\\') out += '\\';
        out += *p;
    }
    out += "\"}\n";
    return out;
}

std::string okLine(long id, const char* extra = nullptr) {
    std::string out = "{\"id\":" + std::to_string(id) + ",\"ok\":true";
    if (extra) { out += ','; out += extra; }
    out += "}\n";
    return out;
}

std::string failLine(long id, const char* reason) {
    return "{\"id\":" + std::to_string(id) + ",\"ok\":false,\"reason\":\"" + reason + "\"}\n";
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

// ── LoopbackBridge ──────────────────────────────────────────────────────────

void LoopbackBridge::start(void* logger) {
    (void)logger;
    if (running_.exchange(true)) return;
    thread_ = std::thread([this, logger] { run(logger); });
}

void LoopbackBridge::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void LoopbackBridge::run(void* logger) {
    (void)logger;

    const std::string& token = agentToken();
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "agent listening on 127.0.0.1:%d (%d-char token; file in Downloads when permitted)",
                        kPort, static_cast<int>(token.size()));

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd, 1) != 0) {
        ::close(fd);
        __android_log_print(ANDROID_LOG_ERROR, TAG, "agent bind/listen failed on %d", kPort);
        return;
    }

    while (running_.load()) {
        // Poll for a client so stop() is responsive instead of blocking in accept().
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 250);
        if (ready <= 0) continue;

        const int client = ::accept(fd, nullptr, nullptr);
        if (client < 0) continue;

        __android_log_print(ANDROID_LOG_INFO, TAG, "app connected");
        agentstate::setLive(true);

        std::string buffer;
        bool authed = false;
        std::string pending;

        while (running_.load()) {
            pollfd cp{client, POLLIN, 0};
            const int r = ::poll(&cp, 1, kPushIntervalMs);

            if (r > 0 && (cp.revents & POLLIN)) {
                char chunk[2048];
                const ssize_t n = ::recv(client, chunk, sizeof(chunk), 0);
                if (n <= 0) break;                       // app closed
                buffer.append(chunk, static_cast<size_t>(n));
                if (buffer.size() > kMaxLineBytes) {
                    sendAll(client, failLine(0, "line too long"));
                    break;
                }

                size_t nl;
                while ((nl = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, nl);
                    buffer.erase(0, nl + 1);
                    if (line.empty()) continue;

                    std::string lineToken;
                    if (!jStr(line, "token", lineToken) || lineToken != agentToken()) {
                        sendAll(client, failLine(0, "bad or missing token"));
                        __android_log_print(ANDROID_LOG_WARN, TAG, "rejected a client with a bad token");
                        goto disconnect;
                    }
                    authed = true;

                    double idNum = 0;
                    jNum(line, "id", idNum);
                    const long id = static_cast<long>(idNum);

                    std::string op;
                    if (!jStr(line, "op", op)) {
                        sendAll(client, failLine(id, "missing op"));
                        continue;
                    }

                    if (op == "getState") {
                        agentstate::Snapshot snap{};
                        if (agentstate::snapshot(snap)) sendAll(client, buildStateLine(snap));
                        sendAll(client, okLine(id, "\"note\":\"state follows\""));

                    } else if (op == "ping") {
                        sendAll(client, okLine(id, "\"pong\":true"));

                    } else if (op == "diag") {
                        const std::string d = agentstate::diag();
                        std::string esc;
                        for (char ch : d) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                        sendAll(client, "{\"id\":" + std::to_string(id) +
                                        ",\"ok\":true,\"diag\":\"" + esc + "\"}\n");

                    } else if (op == "attack") {
                        double target = 0;
                        if (!jNum(line, "target", target) || target <= 0) {
                            sendAll(client, failLine(id, "attack needs a target entity id"));
                            continue;
                        }
                        agentstate::Command c{};
                        c.type = agentstate::CmdType::Attack;
                        c.target = static_cast<std::uint32_t>(target);
                        if (agentstate::pushCommand(c)) {
                            sendAll(client, okLine(id, "\"queued\":true"));
                        } else {
                            sendAll(client, failLine(id, "command queue full"));
                        }

                    } else if (op == "setRotation") {
                        double yaw = 0, pitch = 0;
                        if (!jNum(line, "yaw", yaw) || !jNum(line, "pitch", pitch)) {
                            sendAll(client, failLine(id, "setRotation needs yaw and pitch"));
                            continue;
                        }
                        // Answered honestly BEFORE queueing: if the layout is not
                        // derived, saying "ok" and dropping it would make the app
                        // believe it is aiming while the player never turns.
                        if (!agentstate::caps().rotation) {
                            sendAll(client, failLine(id, "rotation layout not derived"));
                            continue;
                        }
                        agentstate::Command c{};
                        c.type = agentstate::CmdType::SetRotation;
                        c.a = static_cast<float>(yaw);
                        c.b = static_cast<float>(pitch);
                        if (agentstate::pushCommand(c)) sendAll(client, okLine(id, "\"queued\":true"));
                        else sendAll(client, failLine(id, "command queue full"));

                    } else if (op == "setInput") {
                        double fwd = 0, str = 0;
                        bool jump = false, sneak = false;
                        if (!jNum(line, "forward", fwd) || !jNum(line, "strafe", str)) {
                            sendAll(client, failLine(id, "setInput needs forward and strafe"));
                            continue;
                        }
                        jBool(line, "jump", jump);
                        jBool(line, "sneak", sneak);
                        if (!agentstate::caps().moveInput) {
                            sendAll(client, failLine(id, "move input layout not derived"));
                            continue;
                        }
                        agentstate::Command c{};
                        c.type = agentstate::CmdType::SetInput;
                        c.forward = static_cast<float>(fwd);
                        c.strafe = static_cast<float>(str);
                        c.c = jump ? 1.0f : 0.0f;
                        c.d = sneak ? 1.0f : 0.0f;
                        if (agentstate::pushCommand(c)) sendAll(client, okLine(id, "\"queued\":true"));
                        else sendAll(client, failLine(id, "command queue full"));

                    } else if (op == "toggleModule") {
                        std::string name;
                        bool enabled = false;
                        if (!jStr(line, "module", name) || !jBool(line, "enabled", enabled)) {
                            sendAll(client, failLine(id, "toggleModule needs module and enabled"));
                            continue;
                        }
                        agentstate::Command c{};
                        c.type = agentstate::CmdType::ToggleModule;
                        std::snprintf(c.module, sizeof(c.module), "%s", name.c_str());
                        c.boolValue = enabled;
                        if (agentstate::pushCommand(c)) {
                            sendAll(client, okLine(id, "\"queued\":true"));
                        } else {
                            sendAll(client, failLine(id, "command queue full"));
                        }

                    } else if (op == "setSetting") {
                        std::string mod, key;
                        double value = 0;
                        bool bvalue = false;
                        const bool isBool = jBool(line, "value", bvalue);
                        const bool isNum = jNum(line, "value", value);
                        if (!jStr(line, "module", mod) || !jStr(line, "setting", key) ||
                            (!isBool && !isNum)) {
                            sendAll(client, failLine(id, "setSetting needs module, setting and value"));
                            continue;
                        }
                        agentstate::Command c{};
                        c.type = agentstate::CmdType::SetSetting;
                        std::snprintf(c.module, sizeof(c.module), "%s", mod.c_str());
                        std::snprintf(c.key, sizeof(c.key), "%s", key.c_str());
                        c.boolValue = bvalue;
                        c.numValue = static_cast<float>(value);
                        if (agentstate::pushCommand(c)) {
                            sendAll(client, okLine(id, "\"queued\":true"));
                        } else {
                            sendAll(client, failLine(id, "command queue full"));
                        }

                    } else if (op == "listModules") {
                        // Answered from the last snapshot (game thread owns the
                        // registry), so this is a read, not a queue round-trip.
                        agentstate::Snapshot snap{};
                        if (!agentstate::snapshot(snap)) {
                            sendAll(client, failLine(id, "no snapshot yet"));
                            continue;
                        }
                        std::string out = "{\"id\":" + std::to_string(id) +
                                          ",\"ok\":true,\"modules\":[";
                        for (int i = 0; i < snap.moduleCount; ++i) {
                            if (i) out += ',';
                            out += "{\"name\":\"";
                            out += snap.modules[i].name;
                            out += "\",\"category\":\"";
                            out += snap.modules[i].category;
                            out += "\",\"enabled\":";
                            out += snap.modules[i].enabled ? "true" : "false";
                            out += ",\"kotlin\":";
                            out += snap.modules[i].kotlin ? "true" : "false";
                            out += '}';
                        }
                        out += "]}\n";
                        sendAll(client, out);

                    } else {
                        sendAll(client, failLine(id, "unknown op"));
                    }
                }
            }

            if (r < 0) break;

            // Push state at ~20 Hz regardless of traffic in the other direction.
            if (authed) {
                agentstate::Snapshot snap{};
                if (agentstate::snapshot(snap)) {
                    if (!sendAll(client, buildStateLine(snap))) break;
                }
            }
        }

    disconnect:
        agentstate::setLive(false);
        ::close(client);
        __android_log_print(ANDROID_LOG_INFO, TAG, "app disconnected");
    }

    ::close(fd);
}

} // namespace eclient_runtime

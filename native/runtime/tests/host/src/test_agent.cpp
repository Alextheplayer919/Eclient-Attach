// Host self-test for the loopback agent + memory layer.
//
// Runs the REAL AgentState.cpp, LoopbackBridge.cpp and NativeMemory.cpp against
// a fake game layer over a REAL socket, so the wire protocol, the token gate,
// the module-control ops and the refusal rules are all exercised without a
// device. This is the regression net for the agent path.
//
//   ./run.sh          # builds and runs everything
#include "bridge/AgentState.h"
#include "bridge/LoopbackBridge.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace echo {
int attackCalls();
int lastAttackIndex();
}

namespace {
int g_fails = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++g_fails;
}

int connectAgent() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(38170);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void sendLine(int fd, const std::string& payload) {
    const std::string line = payload + "\n";
    (void)::send(fd, line.data(), line.size(), 0);
}

// Read one newline-terminated line, waiting up to `ms`.
std::string readLine(int fd, int ms = 1200) {
    std::string out;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < ms) {
        char c = 0;
        const auto n = ::recv(fd, &c, 1, MSG_DONTWAIT);
        if (n == 1) {
            if (c == '\n') return out;
            out.push_back(c);
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    return out;
}

// Pump the game side: publish a snapshot and apply anything the app queued.
void pumpGameSide() {
    auto* player = reinterpret_cast<void*>(0x1);
    eclient_runtime::agentstate::publish(player);
    eclient_runtime::agentstate::executeQueued(player);
}

std::string request(int fd, const std::string& token, const std::string& body, int id) {
    sendLine(fd, "{\"token\":\"" + token + "\"," + body + ",\"id\":" + std::to_string(id) + "}");
    std::string line;
    for (int i = 0; i < 4 && line.empty(); ++i) {
        line = readLine(fd, 400);
        if (!line.empty() && line.find("\"id\":" + std::to_string(id)) == std::string::npos) {
            line.clear();   // that was a push, keep looking for the ack
        }
    }
    return line;
}
} // namespace

int main() {
    const char* token = std::getenv("ECLIENT_AGENT_TOKEN");
    if (!token || !*token) {
        std::printf("ECLIENT_AGENT_TOKEN must be set\n");
        return 2;
    }
    std::printf("agent host self-test (token=%s)\n", token);

    eclient_runtime::LoopbackBridge bridge;
    bridge.start(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // ── token gate ──────────────────────────────────────────────────────────
    {
        const int fd = connectAgent();
        check(fd >= 0, "agent listens on 127.0.0.1:38170");
        if (fd >= 0) {
            sendLine(fd, "{\"token\":\"definitely-not-the-token\",\"id\":1,\"op\":\"getState\"}");
            check(readLine(fd).find("\"ok\":false") != std::string::npos,
                  "a wrong token is refused");
            ::close(fd);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // ── state feed ──────────────────────────────────────────────────────────
    {
        eclient_runtime::agentstate::setLive(true);
        const int fd = connectAgent();
        check(fd >= 0, "reconnects after the refusal");
        if (fd < 0) return 1;

        sendLine(fd, std::string("{\"token\":\"") + token + "\",\"id\":2,\"op\":\"getState\"}");
        pumpGameSide();

        std::string push, ack;
        for (int i = 0; i < 8 && (push.empty() || ack.empty()); ++i) {
            const std::string line = readLine(fd, 500);
            if (line.find("\"push\":\"state\"") != std::string::npos) push = line;
            else if (line.find("\"id\":2") != std::string::npos) ack = line;
        }
        check(!push.empty(), "state push arrives");
        check(push.find("\"entities\":[") != std::string::npos && push.find("1001") != std::string::npos,
              "push carries the scanned actors");
        check(push.find("\"player\":true") != std::string::npos, "self/player flag resolved");
        check(push.find("\"missing\":[\"Rotation\",\"MoveInput\",\"Inventory\"]") != std::string::npos,
              "underived capabilities are reported missing");
        check(push.find("\"modules\":[") != std::string::npos, "push carries the module list");

        // ── attack op ───────────────────────────────────────────────────────
        const int before = echo::attackCalls();
        sendLine(fd, std::string("{\"token\":\"") + token +
                     "\",\"id\":3,\"op\":\"attack\",\"target\":1002}");
        (void)readLine(fd, 400);
        pumpGameSide();
        check(echo::attackCalls() == before + 1, "attack op executes exactly once");
        check(echo::lastAttackIndex() == 1, "attack targets the actor the app asked for");

        // ── refusals ────────────────────────────────────────────────────────
        check(request(fd, token, "\"op\":\"setRotation\",\"yaw\":90,\"pitch\":0", 5)
                  .find("rotation") != std::string::npos,
              "setRotation refused while the layout is underived");
        check(request(fd, token, "\"op\":\"readMem\",\"addr\":4096", 6)
                  .find("unknown op") != std::string::npos,
              "readMem does not exist (no raw memory verbs)");
        check(request(fd, token, "\"op\":\"toggleModule\",\"module\":\"KillAura2\",\"enabled\":true", 7)
                  .find("\"ok\":") != std::string::npos,
              "toggleModule is handled");
        check(request(fd, token, "\"op\":\"listModules\"", 8).find("\"ok\":") != std::string::npos,
              "listModules is handled");

        ::close(fd);
    }

    bridge.stop();
    std::printf("  ──────────────────────────────────────────────────────────────\n");
    std::printf("  %d/%d checks passed%s\n", g_checks - g_fails, g_checks,
                g_fails ? "  <-- FAILURES" : "  (all good)");
    return g_fails ? 1 : 0;
}

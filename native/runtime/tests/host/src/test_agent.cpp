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
//
// The waits are deliberately generous: under load (the harness also compiles
// three binaries, and CI machines are shared) a tight timeout turns into a
// spurious FAIL, and a flaky self-test is worse than no self-test.
std::string readLine(int fd, int ms = 3000) {
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

// Wait for a line matching `want`, pumping the game side while waiting.
//
// The first version of this test pumped once and then read with a timeout, and
// it failed maybe one run in five when the machine was busy compiling — a
// false alarm that trains you to ignore the harness. Waiting for the line we
// actually want, while nudging the game side, removes the race entirely.
std::string waitForLine(int fd, const std::string& token, const std::string& want,
                        const std::string& body = {}, int id = 0, int budgetMs = 6000) {
    if (!body.empty()) {
        sendLine(fd, "{\"token\":\"" + token + "\"," + body + ",\"id\":" + std::to_string(id) + "}");
    }
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < budgetMs) {
        pumpGameSide();
        const std::string line = readLine(fd, 25);
        if (!line.empty() && line.find(want) != std::string::npos) return line;
    }
    return {};
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

        const std::string push = waitForLine(fd, token, "\"push\":\"state\"",
                                              "\"op\":\"getState\"", 2);
        check(!push.empty(), "state push arrives");
        check(push.find("\"entities\":[") != std::string::npos && push.find("1001") != std::string::npos,
              "push carries the scanned actors");
        check(push.find("\"player\":true") != std::string::npos, "self/player flag resolved");
        check(push.find("\"missing\":[\"Rotation\",\"MoveInput\",\"Inventory\"]") != std::string::npos,
              "underived capabilities are reported missing");
        check(push.find("\"modules\":[") != std::string::npos, "push carries the module list");

        // ── attack op ───────────────────────────────────────────────────────
        // The ack only means the command was QUEUED. The attack itself runs on
        // the game thread inside executeQueued, so the test waits for the
        // EFFECT and only then asserts — otherwise it is racing the thing it
        // is trying to measure (this is what made it fail under CPU load).
        const int before = echo::attackCalls();
        (void)waitForLine(fd, token, "\"id\":3", "\"op\":\"attack\",\"target\":1002", 3);
        for (int i = 0; i < 100 && echo::attackCalls() == before; ++i) {
            pumpGameSide();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(echo::attackCalls() == before + 1, "attack op executes exactly once");
        check(echo::lastAttackIndex() == 1, "attack targets the actor the app asked for");

        // ...and EXACTLY once: further ticks must not re-run a consumed command.
        for (int i = 0; i < 10; ++i) pumpGameSide();
        check(echo::attackCalls() == before + 1, "a consumed command is not re-executed");

        // ── refusals ────────────────────────────────────────────────────────
        check(waitForLine(fd, token, "rotation", "\"op\":\"setRotation\",\"yaw\":90,\"pitch\":0", 5)
                  .find("\"ok\":false") != std::string::npos,
              "setRotation refused when no rotation capability is reported");
        check(waitForLine(fd, token, "unknown op", "\"op\":\"readMem\",\"addr\":4096", 6)
                  .find("unknown op") != std::string::npos,
              "readMem does not exist (no raw memory verbs)");
        check(!waitForLine(fd, token, "\"id\":7",
                           "\"op\":\"toggleModule\",\"module\":\"KillAura2\",\"enabled\":true", 7).empty(),
              "toggleModule is handled");
        check(!waitForLine(fd, token, "\"id\":8", "\"op\":\"listModules\"", 8).empty(),
              "listModules is handled");

        ::close(fd);
    }

    bridge.stop();
    std::printf("  ──────────────────────────────────────────────────────────────\n");
    std::printf("  %d/%d checks passed%s\n", g_checks - g_fails, g_checks,
                g_fails ? "  <-- FAILURES" : "  (all good)");
    return g_fails ? 1 : 0;
}

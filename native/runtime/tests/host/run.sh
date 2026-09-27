#!/usr/bin/env bash
# Host test harness for the attach runtime.
#
# Builds the REAL runtime sources (memory layer, agent state, loopback bridge)
# against a fake game layer and runs them on a plain Linux box: no NDK, no
# device, no CI round-trip. Use it before every push.
#
#   ./run.sh            build + bench + self-test
#   ./run.sh bench      memory bench only
#   ./run.sh selftest   agent self-test only
set -euo pipefail
cd "$(dirname "$0")"

ROOT="$(cd ../../../.. && pwd)"      # repo root
SRC="$ROOT/native/runtime/src"
OUT="${OUT:-/tmp/eclient-hosttest}"
mkdir -p "$OUT"

JNI_INC=""
for j in /usr/lib/jvm/*/include; do
    if [ -d "$j" ]; then JNI_INC="-I$j -I$j/linux"; break; fi
done

RUNTIME=(
    "$SRC/memory/NativeMemory.cpp"
    "$SRC/bridge/AgentState.cpp"
    "$SRC/bridge/LoopbackBridge.cpp"
)
CXXFLAGS=(-O1 -std=c++20 -I "$SRC" -I "$(pwd)/include")
[ -n "$JNI_INC" ] && CXXFLAGS+=($JNI_INC)

want="${1:-all}"

if [ "$want" = all ] || [ "$want" = selftest ]; then
    echo "── building agent self-test"
    g++ "${CXXFLAGS[@]}" src/stub_android.cpp src/fake_game.cpp src/test_agent.cpp \
        "${RUNTIME[@]}" -lpthread -o "$OUT/agent_selftest"
fi

if [ "$want" = all ] || [ "$want" = bench ]; then
    echo "── building memory bench"
    # the bench only needs the memory layer (the bridge pulls in jni.h)
    g++ -O2 -std=c++20 -I "$SRC" -I "$(pwd)/include" \
        src/stub_android.cpp src/bench_memory.cpp "$SRC/memory/NativeMemory.cpp" \
        -lpthread -o "$OUT/memory_bench"
fi

if [ "$want" = all ] || [ "$want" = bench ]; then
    echo
    echo "════════ memory bench ════════"
    "$OUT/memory_bench"
fi

if [ "$want" = all ] || [ "$want" = selftest ]; then
    echo
    echo "════════ agent self-test ════════"
    ECLIENT_AGENT_TOKEN="${ECLIENT_AGENT_TOKEN:-hosttest}" "$OUT/agent_selftest"
fi

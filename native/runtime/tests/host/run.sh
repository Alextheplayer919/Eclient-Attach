#!/usr/bin/env bash
# Host test harness for the attach runtime.
#
# Builds the REAL runtime sources (memory layer, agent state, loopback bridge)
# against a fake game layer and runs them on a plain Linux box: no NDK, no
# device, no CI round-trip. Use it before every push.
#
#   ./run.sh            everything: memory bench, tick bench, agent self-test
#   ./run.sh bench      memory bench (correctness + per-read cost)
#   ./run.sh tick       tick bench (us/tick of the REAL module code)
#   ./run.sh selftest   agent self-test (real socket protocol)
#   ./run.sh coverage   module table vs patch table (no compiler needed)
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
# the tick bench runs the module code itself, so it needs the module sources
# and the real ModuleManager (its per-tick settings lookups are part of what it
# measures — stubbing them would hide real cost).
TICK_SOURCES=(
    "$SRC/memory/NativeMemory.cpp"
    "$SRC/memory/ComponentWalk.cpp"
    "$SRC/modules/MelodyModules.cpp"
    "$SRC/modules/ModuleManager.cpp"
    "$SRC/bridge/AgentState.cpp"
)
CXXFLAGS=(-O1 -std=c++20 -I "$SRC" -I "$(pwd)/include")
[ -n "$JNI_INC" ] && CXXFLAGS+=($JNI_INC)

want="${1:-all}"

if [ "$want" = all ] || [ "$want" = selftest ]; then
    echo "── building agent self-test"
    g++ "${CXXFLAGS[@]}" src/stub_android.cpp src/stub_modulemanager.cpp \
        src/fake_game.cpp src/test_agent.cpp \
        "${RUNTIME[@]}" -lpthread -o "$OUT/agent_selftest"
fi

if [ "$want" = all ] || [ "$want" = bench ]; then
    echo "── building memory bench"
    # the bench only needs the memory layer (the bridge pulls in jni.h)
    g++ -O2 -std=c++20 -I "$SRC" -I "$(pwd)/include" $JNI_INC \
        src/stub_android.cpp src/bench_memory.cpp "$SRC/memory/NativeMemory.cpp" \
        -lpthread -o "$OUT/memory_bench"
fi

if [ "$want" = all ] || [ "$want" = bench ]; then
    echo
    echo "════════ memory bench ════════"
    "$OUT/memory_bench"
fi

if [ "$want" = all ] || [ "$want" = tick ]; then
    echo "── building tick bench"
    g++ -O2 -std=c++20 -I "$SRC" -I "$(pwd)/include" -I "$(pwd)/src" $JNI_INC \
        src/stub_android.cpp src/stub_patchmanager.cpp src/fake_world.cpp src/bench_tick.cpp \
        "${TICK_SOURCES[@]}" -lpthread -o "$OUT/tick_bench"
    echo
    echo "════════ tick bench ════════"
    "$OUT/tick_bench" | grep -v '^\[EClient'
fi

if [ "$want" = all ] || [ "$want" = coverage ]; then
    echo
    echo "════════ module/patch coverage ════════"
    # Static join of the two source tables: a module flagged memoryBacked with no
    # same-named patch spec can only ever refuse at toggle time (that was the
    # r43 "patch profiles are not validated" bug).  Pure text check, so it runs
    # even when no compiler is around.
    python3 check_module_patch_coverage.py
fi

if [ "$want" = all ] || [ "$want" = selftest ]; then
    echo
    echo "════════ agent self-test ════════"
    ECLIENT_AGENT_TOKEN="${ECLIENT_AGENT_TOKEN:-hosttest}" "$OUT/agent_selftest"
fi

# Host test harness (no device required)

Runs the **real** runtime sources on a plain Linux box:

| source | why it matters |
|---|---|
| `native/runtime/src/memory/NativeMemory.cpp` | every memory read goes through it |
| `native/runtime/src/bridge/AgentState.cpp` | the per-tick snapshot the app receives |
| `native/runtime/src/bridge/LoopbackBridge.cpp` | the socket the app talks to |

Everything else (the game, the Android log, the module registry, the Kotlin
mirror) is faked in `src/`, so this needs no NDK, no APK, no device and no CI
round-trip.

```sh
./run.sh            # everything: memory bench, tick bench, agent self-test
./run.sh bench      # memory bench (correctness + per-read cost)
./run.sh tick       # tick bench (us/tick of the REAL module code)
./run.sh selftest   # agent self-test (real socket protocol)
```

`ECLIENT_AGENT_TOKEN=...` overrides the token the self-test uses.

## What the self-test covers

* the agent listens on `127.0.0.1:38170`
* a client with the wrong token is refused and can reconnect with the right one
* `getState` produces a `push:"state"` carrying the scanned actors, the
  self/player flag, the underived-capability list and the module list
* `attack` executes exactly once, against the actor index the app asked for
* `setRotation` is refused while the rotation layout is underived
* `readMem` does not exist (raw memory verbs are never exposed over the bridge)
* `toggleModule` / `listModules` are handled
* (bench) a freshly mapped region is readable on the FIRST read, and an
  unmapped address is refused every time — the regression test for the refresh
  policy

## The bench

`bench_memory.cpp` exists because of a real incident: `readable()` used to parse
the whole of `/proc/self/maps` on **every** read, ~1250 lines, measured at
146 us per read. One `botScan` (about 1240 reads) cost 181 ms and a combat tick
about 438 ms — 9x the 50 ms budget of a 20 TPS tick. That was the "unplayable,
lags like hell" bug.

Keep an eye on these numbers; the bench prints them on every run.

```
                reads   before r38          now
  one botScan    1240      181.0 ms     0.029 ms
  one combat tick 3000      438.0 ms     0.071 ms
  worst case     6000      876.0 ms     0.142 ms
```

## The tick bench

`bench_tick.cpp` runs the **real module code** — `melody::onNormalTick()`,
KillAura2, Fly, the agent feed — against a synthetic world built by
`fake_world.cpp`, and reports microseconds per tick plus the work done
(actor-list calls, attacks, ability calls). A 20 TPS tick is 50,000 us and on
Android the game thread feeds the frame, so this is frame time.

Measured on the dev host (a phone is slower; use it for changes, not absolutes):

```
idle                             0.1 us/tick
KillAura2, 25 actors             0.4 us/tick
Fly                              0.1 us/tick
agent connected, 25 actors      10-16 us/tick
KillAura2 + agent, 50 actors     ~17 us/tick
```

`fake_world.cpp` works by mmapping an arena as a fake `libminecraftpe.so` base
and writing a 12-byte absolute-jump trampoline at every offset the modules
resolve, so the real code calls our stubs. The world it builds satisfies
everything the modules read: the local player, its state vector, the ECS
registry with a real `ActorType` pool, and N actors with type ids and health.

**Watch out:** the first version of this bench reported 0.2 us/tick while the
modules were doing *nothing* — a stale map-cache policy made every read fail.
Instrument the harness before trusting a suspiciously good number. `bench_tick`
prints its work counters next to the timing for exactly that reason.

## Portability trap (learned the hard way)

The first version of the r38 fix used `std::atomic<std::shared_ptr<T>>`. It is
C++20 on paper and g++/libstdc++ accepted it, but the NDK's libc++ does **not**
implement that specialization and the CI build died on it. This harness links
against the host standard library, so it cannot catch that class of problem —
prefer C++17-compatible constructs in runtime code, and keep an eye on the
NDK-only parts of the build.

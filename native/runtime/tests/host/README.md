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
./run.sh            # build + memory bench + agent self-test
./run.sh bench      # memory bench only
./run.sh selftest   # agent self-test only
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

## Portability trap (learned the hard way)

The first version of the r38 fix used `std::atomic<std::shared_ptr<T>>`. It is
C++20 on paper and g++/libstdc++ accepted it, but the NDK's libc++ does **not**
implement that specialization and the CI build died on it. This harness links
against the host standard library, so it cannot catch that class of problem —
prefer C++17-compatible constructs in runtime code, and keep an eye on the
NDK-only parts of the build.

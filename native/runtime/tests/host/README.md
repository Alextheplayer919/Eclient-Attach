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
./run.sh coverage   # module table vs patch table (no compiler needed)
./run.sh modules    # module toggle matrix (real ModuleManager)
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

## The coverage check

`check_module_patch_coverage.py` joins the two source tables statically: every
module in `modules/ModuleManager.cpp` flagged `memoryBacked` must have a
same-named spec in `profile/PatchManager.cpp`. It also lists the dormant specs
(profiles no module drives yet — that is fine, they have their own entry
points).

This is the r43 regression guard. Fly's row carried `memoryBacked = true` left
over from the deleted patch-era Fly, so every toggle called
`PatchManager::set("Fly")` against a name with no spec, got `matched = false`,
and refused — with a message about *validation*, even though the panel said
VALIDATED. The bug is a wiring mistake, so it is caught by a text check rather
than by hand-testing a toggle on a phone.

## The module toggle matrix

`check_modules.cpp` drives the **real** `ModuleManager` and asserts what a user
would notice: every module in the table toggles on and off and the reported state
follows. It also pins the two failures that reached the device:

* a memory-backed module on a **validated** profile applies (no refusal);
* a memory-backed module on an **unbuilt** profile reports the *validation*
  status — never `no patch profile named X`, which accuses the module table of a
  wiring bug it does not have;
* toggling **Fly** never reaches the patch layer at all (it flies through the
  player's abilities and writes no bytes — treating it as a patch module is what
  refused every toggle in r42).

This only became possible after `stub_patchmanager.cpp` stopped lying: it used to
report an empty spec table always, so memory-backed toggles refused in the
harness and nothing tested them.

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

## diagLine() — reading a device from a screenshot

`bench_tick` prints the same string the in-game panel shows, so a host run is the
reference for "healthy":

```
tick 18280  api 6/6  world ok  on: KillAura2
aura[3.0]: seen 12  ok 3  noatk 0  dead 0  rng 9  hurt 0  type 0+0  nopos 0  attacks 2208
```

`melody::diagLine()` exists because "nothing works" on a phone cannot otherwise
be told apart from "nothing is enabled". Read it top-down, in the order the
stages fail:

| reading | what it means |
|---|---|
| `tick` not advancing | the game thread never runs our hook — nothing can work |
| `api 5/6` | a game API pointer did not resolve; every module needing it stays inert |
| `world no` | the world-stability gate is holding all module code back |
| `on: (none)` | nothing is enabled — a toggle/UI problem, not a module problem |
| `seen 0` | the actor list is empty: no entities, or an unreadable list |
| `seen 12 ok 0 noatk 12` | every actor is filtered by `isAttackable` — the module is fine, the game refuses the target (gamemode) |
| `seen 12 ok 1 rng 11` | the funnel works; the targets are simply out of range |
| `ok 3 attacks 0` | the attack was never reached (delay / hurttime filter) |
| `ok 3 attacks 40` | we ARE attacking — look at the server, not the client |

## Portability trap (learned the hard way)

The first version of the r38 fix used `std::atomic<std::shared_ptr<T>>`. It is
C++20 on paper and g++/libstdc++ accepted it, but the NDK's libc++ does **not**
implement that specialization and the CI build died on it. This harness links
against the host standard library, so it cannot catch that class of problem —
prefer C++17-compatible constructs in runtime code, and keep an eye on the
NDK-only parts of the build.

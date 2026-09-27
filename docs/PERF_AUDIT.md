# Performance audit — memory management and FPS

Written after the user reported that the real problem was **FPS, not TPS**, and
asked for a deeper audit than "the tick was slow". Everything below is measured,
not reasoned about. Two harnesses produce the numbers and both live in the repo:

```sh
native/runtime/tests/host/run.sh          # bench + correctness + agent self-test
./run.sh bench                            # just the memory + tick numbers
```

---

## 1. What was actually costing the frame

`NativeMemory::readable()` opened and parsed `/proc/self/maps` on **every**
memory read — ~1250 lines, `sscanf` per line — and every module read funnels
through it.

| | before | after |
|---|---|---|
| one memory read | **146,000 ns** | **58 ns** |
| one `botScan` (~1240 reads) | 181 ms | 0.07 ms |
| one combat tick (~3000 reads) | 438 ms | 0.18 ms |

The tick hook runs on the game thread, and on Android the game thread is what
produces frames. A 400 ms stall in that hook is not "slow ticks", it is a
slideshow — which is why the report read "lags like hell" and why only the
simplest modules (KillAura2, CriticalHit) seemed to work at all: everything
timing-based or position-based was starved.

**Fixed** by parsing the map once into a sorted range table and answering
lookups with a binary search behind a per-thread hot cache.

## 2. What the module chain costs now (measured)

`native/runtime/tests/host/src/bench_tick.cpp` runs the **real** module code
against a synthetic world (`fake_world.cpp`) and times `melody::onNormalTick()`.
A 20 TPS tick is 50,000 us, and anything spent here is time the frame does not
get.

| configuration | us/tick | % of a 50 ms tick |
|---|---|---|
| idle | 0.1 | 0.0002% |
| KillAura2, 25 actors | 0.4 | 0.0008% |
| Fly | 0.1 | 0.0002% |
| agent connected, 25 actors | 7–16 | ~0.02% |
| KillAura2 + agent, 50 actors | ~10 | ~0.02% |

Work done per tick at that cost: 1.5 actor-list enumerations, 0.33 attacks,
1 ability call. The per-actor reads are ~60 guarded reads per actor, and each
one is now 58 ns instead of 146 us.

**Conclusion: the module layer is no longer an FPS factor.** If a device still
drops frames, it is not this code.

## 3. What was checked and found innocent

| suspect | verdict |
|---|---|
| ImGui / GL overlay | **not installed.** `InGameGui::initialize()` returns early, and the comment there records that the `eglSwapBuffers` hook cost a GL FBO + viewport save/restore every frame — the original FPS drain, already removed |
| Kotlin overlay compositing | window is `TYPE_APPLICATION_PANEL`/`OVERLAY`, **translucent but never `MATCH_PARENT`** (320 dp wide), so the compositor only blends a small region |
| per-frame Kotlin work | the only repeating loop is a **1 Hz** watchdog |
| per-tick logging | 3 `__android_log_print` calls in the whole module file, all on enable/bind, none in the tick body |
| per-tick file I/O | only the ECSDump dev module (off by default) and boot-time patch writes |
| settings lookups | `ModuleManager` getters take a mutex + linear search, ~µs/tick total — visible in the bench, not worth changing |
| hook set | `NormalTick` (once per tick), `SpeedHack` (pass-through), boot-time byte patches. Nothing per frame |

## 4. Two bugs this audit found

**a) The refresh policy was wrong (introduced by the r38 fix itself).**
The first map cache rate-limited refreshes to one per 250 ms and answered
*"unreadable"* in between. That is exactly the failure mode that looks like a
broken module: the moment the game maps something new — a chunk buffer, an
entity arena, a fresh thread stack — every read into it silently failed for up
to a quarter of a second, so scans, position reads and the world-stability gate
all reported "nothing there".

Found by running the tick bench, which is why the bench exists. The policy is
now:

* a miss **always** re-checks the map (correctness first),
* bounded by per-address dedup (the same address twice inside 250 ms does not
  re-parse),
* plus a deliberately generous budget (16 parses per 250 ms ≈ 2.4 ms worst case
  of work per window) so a stale actor list full of freed pointers cannot turn
  into a parse storm,
* and a per-thread negative cache (100 ms) so a tight loop against a dead
  address costs a comparison, not a file parse.

`bench_memory.cpp` now carries the regression test: **a freshly mapped region
must be readable on the very first read**, and a genuinely unmapped address must
still be refused. Do not "optimise" the refresh path without that staying green.

**b) The funnel text was rebuilt every tick.** `botScanDiag()` built a
`std::string` plus a formatted line 20×/s for a string only a human reads. Now
refreshed at ~1 Hz. Small, but it was pure waste in the hot path.

## 5. Why the harness lives in the repo

The previous test harness lived in `/tmp` and kept being wiped, and an earlier
fix (r38) shipped straight to CI where it died on an NDK-libc++ detail that the
host compiler accepted:

> `std::atomic<std::shared_ptr<T>>` is C++20 on paper, but the NDK's libc++
> does not implement that specialization. Prefer C++17-compatible constructs in
> runtime code.

The harness now also exists because of the opposite lesson: **a bench that
cannot reproduce what the module actually does will happily report 0.2 µs/tick
while the module does nothing at all.** The first version of `bench_tick.cpp`
did exactly that — it reported near-zero because a stale refresh policy made
every read fail, and it took instrumenting the harness to tell the difference
between "fast" and "not running".

## 6. Standing budget

Keep the tick chain under ~1% of a tick (500 µs). If a future change pushes
`bench_tick` above that, the change is the problem — not the phone.

Phone numbers will be slower than the host's (different CPU, less cache). The
host is a relative measure: it is most useful for spotting *changes* and for
proving a fix, not for predicting absolute frame time.

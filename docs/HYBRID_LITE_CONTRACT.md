# Hybrid-lite contract — what the agent has to be from now on

**Decision (2026-10):** the agent is a **read-only sensor**. The Eclient app keeps every module,
every action and all movement, and gets state from packets. The agent exists to report the one
thing packets cannot carry — what the camera is doing — and to act as a ground-truth referee for
the app's own position estimate. Design and rationale: `docs/HYBRID_LITE.md` in the Eclient repo.

This document is the native side of that contract. It changes nothing in the boot chain
(`DO_NOT_BREAK.md` still applies in full).

## What stays, what goes

| | |
|---|---|
| **Keep** | `LoopbackBridge` (token on every line, `getState`/`ping`/`diag`), the 20 Hz state push, `self` pose, `caps`/`missing`, fail-closed behaviour |
| **Add** | an optional `camera` object in the state push (below) |
| **Stop using** | `attack`, `setRotation`, `setInput`, `toggleModule`, `setSetting`, `listModules`. Sensor-only builds should answer them `ok:false, reason:"sensor-only"`. The app no longer sends them. |
| **Never** | `readMem` / `writeMem`, in-game gameplay logic, modules in the game |

Entities, inventory, rotation and move-input are **no longer on the critical path**: the app
already gets all of them from packets. Do not spend derivation effort on them.

## `camera` (agent → app, inside every `push:"state"`)

Every field is optional. **Send only what is actually derived and verified; omit the rest.** An
absent `camera` object means "use packets" and is a perfectly normal state.

```json
"camera": {
  "fovY": 74.5,
  "mode": 0,
  "eye": {"x": 123.5, "y": 65.62, "z": -88.25},
  "yaw": 137.5, "pitch": -4.2,
  "viewport": {"w": 2400, "h": 1080},
  "vp": [ 16 floats ]
}
```

### Conventions (non-negotiable — the app does not guess)

* **World space** is the same as the packets: a block `(x,y,z)` occupies `[x,x+1)×[y,y+1)×[z,z+1)`.
* **`fovY`** — the VERTICAL field of view in **degrees**, as the renderer uses it *this frame*
  (so it includes the player's slider and sprint/speed effects). Not the slider value.
* **`eye`** — the camera origin (the eye / third-person camera position), not the feet.
* **`yaw`/`pitch`** — degrees, the Bedrock packet convention. The forward vector is
  `(−sin(yaw)·cos(pitch), −sin(pitch), cos(yaw)·cos(pitch))`: yaw 0 looks along +Z, pitch > 0 looks down.
* **`mode`** — `0` first person, `1` third person (back), `2` third person (front).
* **`vp`** — column-major (`vp[col*4+row]`), OpenGL clip space, right-handed, y up:
  `clip = VP · [x y z 1]`, `ndc = clip.xyz / clip.w`, no y-flip. If the renderer uses another
  convention (row-major, flipped y, 0..1 depth), **normalise it here**, not in the app.
* Sampled at the latest rendered frame, pushed at ≥ 20 Hz (higher is better; see below).

Optionally add `"CameraFov"`, `"CameraPose"`, `"CameraMatrix"` to `caps` (informational) and list
the missing ones in `missing`.

## Derive in this order — each step ships on its own

1. **`fovY`** (one float). Biggest win: today the app guesses it (default 110°) and a wrong FOV
   makes projections drift toward the screen edges.
2. **`mode`**.
3. **`eye` + `yaw`/`pitch`**.
4. **`vp`**.

Same rule as the rest of the runtime: a capability that is not derived stays **absent** and the
app falls back. Never send a guess.

### Known-answer checks (verify a derivation without trusting any app code)

| Field | Check |
|---|---|
| `fovY` | Move the in-game FOV slider 30 → 110: the value rises monotonically. Start sprinting: it briefly rises. |
| `mode` | Press the camera/perspective button: `0 → 1 → 2 → 0`. |
| `eye` | Stand on a block whose top is `y = 64`: `eye.y ≈ 65.62` (sneaking ≈ 65.27); `eye.x/z` equal your position. |
| `yaw`/`pitch` | Face +Z level: `(0, 0)`. Look straight down: `pitch ≈ 90`. |
| `vp` | Project `eye + forward·10`: NDC ≈ `(0, 0)` in first person. |

In the app, type `.camera` in chat: it shows the active source (`SENSOR_FOV`, `SENSOR_POSE`,
`SENSOR_MATRIX`) and the sensor's contents; `.camera audit` shows the position cross-check.

## Latency note

The app can only be as tight as the push rate: at 20 Hz a fast camera turn lags by up to ~50 ms
plus the loopback hop. If that matters later, sample on the render thread and push at 60 Hz.
Do not trade correctness for rate: a stale or guessed camera is worse than none.

## Link requirements for the native bridge (from the app's side)

Verified against `LoopbackBridge.cpp`; the app already tolerates every one of these being absent, so they can
land in any order. Background and tests: `docs/BRIDGE.md` in the Eclient repo.

1. **Announce yourself**: add `"proto":1,"agent":"<build>"` to every state push (absent ⇒ the app treats you as legacy).
2. **Never fabricate `t`.** `t` is the game-tick counter. The bridge thread re-sends the last snapshot at 20 Hz even
   when the game thread is frozen; the app detects that by `t` not changing and marks the data stale. Do not
   increment it in the bridge thread.
3. **Send with a timeout** (`SO_SNDTIMEO` ≈ 200 ms) and drop the frame on timeout (latest wins) — a blocking `send()`
   stalls the whole bridge when the app is slow or frozen (Android 14+ freezes cached apps).
4. **Newest client wins**: close the previous connection when a new authenticated client arrives. Today `listen(fd, 1)`
   plus an accept loop that is blocked while serving means a zombie client locks the app out.
5. **Always answer `ping`** — also when no snapshot exists (menu) and while the game tick is frozen. The app uses the
   pong, not push traffic, to tell "quiet" from "dead".
6. Compare the token in constant time (minor).
7. **Before any actuating intent exists again** (e.g. a memory-based KillAura): stronger authentication, a per-capability
   `claim`/`release`, a dead-man switch that stops app-driven features after ~1.5 s without a valid line, and an
   `ttlMs` on every intent. Blueprint: `docs/BRIDGE.md` §7.

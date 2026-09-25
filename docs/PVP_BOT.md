# PvP Bot — the decision system (r27)

`PvpBot.kt` + the native data plane in `MelodyModules.{h,cpp}` / `KotlinBridge.cpp`.

The bot fights for you and **keeps score of how it fights**. It is not a fixed
script: the movement patterns it uses are rated per situation, misses force a
change, and what it learns survives a restart.

---

## 1. The loop (20 Hz, on the game thread)

```
tick ──┬─ botSelf    → my position / velocity / health, damage timeline
       ├─ botScan    → every attackable actor within Range, nearest first:
       │                position, velocity, distance, health, i-frames, entity id
       ├─ resolve     → classify the previous swing as HIT or MISS
       ├─ choose      → opener? forced switch? or the best-rated pattern
       ├─ botMove     → velocity write in the TARGET's frame (+ distance control)
       ├─ botAim      → the yaw/pitch that faces the target (shown; applied when
       │                the rotation component is derived)
       └─ botAttack   → swing on cooldown, then remember it for classification
```

Rapid decisions come from fresh data: one scan per tick, and the scan is the
same actor filter KillAura2 uses (`melody::killauraTargetValid`), so the bot can
never act on an actor the module itself would refuse to attack.

**No target → no writes at all.** With nobody in range the bot is completely
hands-off. That is deliberate: a stuck "walk forward" is the worst failure mode
an automation can have, so idleness is the safe state.

## 2. What it was taught (the priors)

It starts opinionated, not blank:

| Prior | Why |
|---|---|
| **Backpedal opener** (15 ticks = 0.75 s) | People charge straight in. The first hit is theirs to miss. |
| **Keep a distance band** (2.0–3.2) | Distance control is always active and overrides style: too close backs off, too far closes. Patterns only choose *how* it travels inside the band. |
| **Sideways > straight** | Strafing is seeded above charging; pure `advance` is seeded *below* it — a target walking straight forward is the easiest thing in the game to hit. |
| **Never hit into i-frames** | A target damaged within 10 ticks cannot take another hit. Waiting is free; a wasted swing is not. |
| **Sword cadence, not spam** (12 ticks) | Attacks land on cooldown. |
| **Hold still is rated worst** | Standing there is how you get comboed. |

## 3. The movement patterns ("WASD combinations")

Weights are in the **target's** frame, so patterns are camera-independent:

| id | intent | seed score |
|---|---|---|
| `backpedal`, `strafe-left`, `strafe-right` | give ground / circle | +1.0 |
| `retreat-left`, `retreat-right` | retreat on an angle | +0.6 |
| `advance-left`, `advance-right` | close on an angle | +0.5 |
| `w-tap` | stop on the swing tick (knockback reset) | +0.3 |
| `jump-in`, `strafe-jump` | critical-hit attempts when close | +0.2 |
| `advance` | straight charge | −0.4 |
| `hold` | stand still (never chosen by scoring) | −2.0 |

## 4. How it learns

Every swing is an experiment. The situation is bucketed — **distance band ×
target moving/standing × my health band** → e.g. `d1m h2` — and the pattern is
scored *for that bucket*:

| observation | score |
|---|---|
| target health dropped within 12 ticks (or it entered i-frames) | **HIT** +1 |
| 12 ticks with no damage on that target, or it vanished | **MISS** −0.5 |
| I took damage while travelling with that pattern | −0.25 (true even if the swing landed) |

Rules on top:

* **Forced switch** — `Fail-switch` misses in a row (default 3) ends that
  pattern immediately and picks the least-tried alternative. This is the "if it
  fails to hit for a while, switch WASD combination" rule.
* **Honest trials first** — a pattern with fewer than `Min-trials` attempts in a
  bucket (default 3) is tried before any score is trusted, so early scores
  describe reality instead of luck.
* **Bucket-local** — what works at long range against a mover is not what works
  point-blank against someone standing still; the scores never mix.

Persistence: `eclient_bot` SharedPreferences (`s|<bucket>|<combo>` scores,
`t|…` trial counts, lifetime hits/misses). Sweep `Reset` in the panel to erase.

## 5. Escalation (auto-KillAura)

If the bot starts **getting hit non-stop** — `Escalate-hits` damage events
(default 3) inside the native 5-second window — it turns **KillAura2 on by
itself**, keeps it on for `KillAura-hold` (default 5 s) after the pressure ends,
and only turns it back off if you had it off beforehand (`Restore`) and the calm
period has passed. Taking that much damage is also the honest signal that
spacing lost the fight, so that is the moment to stop being polite about aim.

The damage timeline is native (`botSelf`: events in the last 5 s + ticks since
the last one) because it has to be measured on the game tick, not sampled.

## 6. What it cannot do yet — said out loud, never faked

| gap | state |
|---|---|
| **Turning the player** | `ActorRotationComponent` underived → `botRotationAvailable()` is `false`, the bot computes the needed yaw/pitch and shows the **aim error** in the panel, but you still have to keep the target roughly in front. The seam is ready: when the offsets land, aim starts applying with no other change. |
| **Line of sight** | not ported; it will swing through a wall if the target is in range (KillAura2 has the same note). |
| **Eating / item use** | `UsingItemFlagOffset` underived, so the bot cannot pause for a gapple. Don't run it while eating. |
| **Movement mechanism** | it writes the state-vector velocity (the mechanism Fly proved, capped at 0.35 blocks/tick ≈ sprint). It is not input injection — if you steer by hand, `Human-override` makes the bot yield movement to you and keep timing the swing. |

## 7. Settings that matter

`Range`, `AttackRange`, `Keep-min/max`, `Cooldown`, `Opener`, `Fail-switch`,
`Min-trials`, `Speed`, `Human-override`, `Wait i-frames`, `Auto-KillAura`,
`Escalate-hits`, `KillAura-hold`, `Restore`, `Target` (Nearest | Lowest HP),
`Debug`.

The panel status line shows the live state:
`FIGHT d=2.8 me=20 them=14 d1mh2 strafe-left / hits 7/12 (58%) miss-streak=0`.

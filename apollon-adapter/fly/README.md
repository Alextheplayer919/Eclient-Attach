# Fly2 experiment

`fly2.cpp` translates the calculation in the supplied `Fly2Module.smali.txt`.
The source is a KiwiMods/Cloudburst module: it watches outgoing
`PlayerAuthInputPacket`s and injects a client-bound `SetEntityMotionPacket` via
its `GameSession`. The Apollon APK does not contain those Kotlin classes or
that packet proxy. Its existing Creative Fly uses Minecraft abilities instead;
this new calculation does not replace or change those controls.

Defaults match the supplied module: horizontal 46 BPS, up 19.8 BPS, down 46
BPS, glide -0.02, horizontal up/down factors 0.55. The port preserves the
4.5-block position-jump guard, idle behavior, yaw transform, diagonal
normalization, 20 ticks/second conversion, and horizontal cap. Both horizontal
factors apply if up and down are simultaneously true; up wins vertically.
For safety the port primes position on the first sample rather than comparing
it with world origin, and rejects non-finite values/invalid settings.

The experimental adapter connects this calculation to Apollon's local-player
tick as a separate **Fly2 (experimental)** option under Movement / Other.
Because native movement-input layout remains unverified, it does **not** read
the game's joystick or jump/sneak fields. Instead, hold the explicit Fly2
controls in the menu. Only one touch is supported by the current overlay, so
combinations such as forward plus up are not available yet. Release, minimize,
leave the tab, pause rendering, leave the HUD, disable Fly2, or enable Anti
Knockback to stop Fly2 writes. A held command expires after 250 ms without a
rendered frame. Glide is only applied while its control is held. This is an
intentional fail-inert difference from the original packet module.

The adapter uses the Apollon rotation and position wrappers, calls LerpMotion
on the game thread after the original tick, and refuses to call it if its entry
bytes differ from the pinned profile or Anti Knockback is enabled. The source
uses these wrappers but device behavior and yaw conventions still need testing;
no guarantee is made about multiplayer/server acceptance. Fly2 resets its
position baseline after release, interruption, or player change, and is off on
startup. It does not restore a previous velocity after release; the game is
left to resume its normal physics. This is an experimental adaptation, not the
original KiwiMods client-bound packet implementation.

Host math test:

```
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_test.cpp apollon-adapter/fly/fly2.cpp -o /tmp/fly2-test
/tmp/fly2-test
```

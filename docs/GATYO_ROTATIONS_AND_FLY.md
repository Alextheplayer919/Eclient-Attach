# Gatyo rotation and Bypass Fly notes

Source: `gatyo shitsrc.zip`, particularly `crystal_common_gatyo.h`,
`gatoaurax_gatyo.h`, `fly_gatyo.h` and `main.cpp`. These are source-derived
claims for the pinned 1.21.111 arm64 library, not proof of server acceptance.

## Rotations

For crystal actions Gatyo computes pitch and yaw from the player's eye position
to the action point: `pitch = -asin(dy / distance)` and
`yaw = -atan2(dx, dz)`, converted to degrees and wrapped. It publishes an
atomic short-lived aim and writes pitch at PlayerAuthInputPacket +0x2c, yaw at
+0x30, and head yaw at +0x40 in its packet-write hook. The spoof expires after
three shared crystal ticks. It applies the spoof after GatoAuraX's spoof, so
crystal aim wins when both are active. Packet timing relative to the actual
attack/build call matters; writing the next packet does not establish that an
action was accepted. Apollon's existing aura rotates only packet yaw at +0x30
and also has local head/pitch/camera controls. Gatyo tracks the user's pitch
separately when modules override local camera rotation, so flight look controls
are not simply reading an aura-controlled pitch.

We have not enabled a new crystal-rotation spoof in E-Client yet: the confirmed
crystal placement path is left intact, and the user asked to examine rotation
methods before deciding how to use them.

## Bypass Fly adaptation

Gatyo estimates joystick direction from the delta between the velocity it left
and the velocity observed at the gravity hook, falling back to position delta.
It reads jump/sneak flags from PlayerAuthInputPacket +136 and temporarily
filters sneak flags while the fly module is active, restoring the original
packet memory after serialization. On normal tick it writes the state vector's
velocity at position +24. The bypass phase is active while rising and moving,
plus a 0.09-second release window; horizontal/vertical speeds interpolate with
distance from origin. Nearby solid blocks select the source's 2-block/tick
horizontal branch. Source defaults include 2.42 normal horizontal, 1.1585
bypass horizontal, 1.62 normal vertical, 1.086 bypass vertical, and -0.1266
glide in blocks per tick near the origin. These are aggressive, not a guarantee
of bypass; corrections, flags, falls, and desync remain possible.

E-Client's separate Bypass Fly module adapts this source and is inactive while
Fly2 is enabled. This integration and its native packet fields have not been
tested on a device. Look tracking is simplified compared with Gatyo when other
rotation modules are active. Disable other flight modules when testing.

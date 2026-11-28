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

## Gatyo KillAura (GatoAuraX) adaptation

`apollon-adapter/gatyo-aura/gatyo_rotation.h` carries Gatyo's 16 rotation
choices and their source algorithms. `gatyo_aura.h` adapts its nearest/multiple
player selection, attack interval, multiplier, hurt check, optional mobs and
local/packet application to Apollon's game wrappers. A separate module keeps
Apollon's original KillAura intact; do not enable both at once. Friends and
bot-list entries are excluded. Gatyo's weapon switching, auto trident, rain
mode, random hit chance and target visualization are not implemented here.
Eclipse uses Gatyo's fixed 1.0 lead, not the unavailable external ping source.

None is no rotation. Strafe exists in Gatyo's dropdown but its source does not
apply its computed sway to the packet or local actor. Other modes calculate
angles from position, velocity, bounding box and sometimes recent movement;
the local look hook can also move the camera, head and body. The input-packet
hook writes pitch +0x2c, yaw +0x30 and head yaw +0x40 after Apollon's original
yaw logic. Rotation output is published without retaining target pointers,
and expires after 500ms without a fresh attack calculation. Mode behavior and
packet timing are unverified on a device or server and do not guarantee a
bypass; use a disposable offline world first.

In the combat build, the module can target the EClientBot NPC or the virtual
fake player when present. Real players take priority by distance and normal
filters apply. The fake target is simulated; hitting it does not prove a
server accepted an attack.

## Touch shortcuts

Other → Shortcuts adds draggable floating module toggles or hold-to-fly up/down
buttons, inspired by Gatyo's screen buttons. Android uses a separate touch
region per shortcut; the rest of the game screen remains interactive. Buttons
are restricted to the HUD, have scale/opacity/position-lock controls, and are
saved by module name in the app's `xal/shortcuts.json`. This is an adaptation,
not the entire Gatyo ClickGUI: quick-chat command buttons and physical-key
bindings were not ported. Shortcuts and their Android touch routing need device
testing.

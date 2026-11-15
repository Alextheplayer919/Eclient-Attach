# Fly2 experimental flight

Fly2 is a separate module under Movement → Fly2. The toggle starts off. The
experimental calculated mode is selected by default: it applies the translated
Fly2 calculation to candidate native joystick/jump/sneak fields. These fields
are unverified and the mode is intended for a cautious device test only.
Uncheck "Experimental calculated mode" to use the previous ability-based Fly2:
Minecraft handles the controls while the three speed sliders cap existing
motion rather than force a speed. Both modes use may-fly/flying abilities.
Anti Knockback must be disabled for motion writes. Server acceptance is not
guaranteed.
The toggle starts off. When disabled, the local-player tick clears the ability
bits in survival-like modes, except bits another Apollon flight toggle still
needs, following upstream's cleanup policy. It also attempts cleanup when
panic, the main menu or death interrupts flight. It never writes to a previous
player pointer after the player changes. Existing ability values are not read
or saved by Apollon, so this cannot promise perfect restoration of pre-existing
server-granted survival abilities. Normal physics resumes after disabling.

`fly2.cpp` contains the math translation of the supplied smali's packet-flight
algorithm; calculated mode uses it with the experimental native input reader.

Host tests:

```
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_test.cpp apollon-adapter/fly/fly2.cpp -o /tmp/fly2-test && /tmp/fly2-test
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_adapter_test.cpp -o /tmp/fly2-adapter-test && /tmp/fly2-adapter-test
```

## Calculated-mode investigation

The supplied relay files confirm the original sends a SetEntityMotion packet
back to Minecraft, after reading the PlayerAuthInput movement vector, yaw,
WANT_UP/JUMPING and WANT_DOWN/SNEAKING flags. The repository's older FlyX
bindings identify candidate native MoveInputComponent stride 160, motion
vector +0x58, jump +0x98 and sneak +0x94, but other profile notes mark input
layout as underived, and these candidates have not been verified on the target
device. In particular jump/sneak alone are not proof of WANT_UP/WANT_DOWN.
Do not use the candidate pointers just because their values look plausible.

`fly2_calculated.h` is a tested, game-independent lifecycle bridge. It resets
on disable, unavailable input, player change, invalid input and interrupted
game state, and requires a fresh baseline before producing motion. In this
device-test build it is connected to the candidate input reader; the layout and
timing still need live verification against the original relay behavior.

Additional host test:
```
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_calculated_test.cpp apollon-adapter/fly/fly2.cpp -o /tmp/fly2-calculated-test && /tmp/fly2-calculated-test
```

## Opt-in device test build

This build enables experimental calculated mode by default when Fly2 is
turned on; the unchecked "Experimental calculated mode" option restores the
ability-based cap mode. The default speed values are 4 BPS for the test.
Calculated mode uses the older FlyX candidate offsets; it is not verified.
It reads candidate input with process_vm_readv and fails closed on bad reads
or invalid values. It then uses the pinned LerpMotion function when available.
These safeguards do not prove the layout, prevent server corrections, or make
an experimental native hook crash-proof. Test in a local world away from hazards.
The jump and sneak bytes are only candidates for the original WANT_UP/DOWN
flags. Anti Knockback must be disabled.

A single Downloads/fly2test.txt owned by the app is overwritten at each app
process launch and refreshed roughly every two seconds while the overlay runs.
Android 10+ uses MediaStore with a private staging file. It logs time, sampled
input axes, buttons, yaw, status and motion, but not positions. It is capped
at about 1 MiB in the staging file. If Android denies Downloads writes, the
failure is recorded in logcat. Reinstalling or deleting app data can cause
Android to create a renamed entry rather than replace one it no longer owns.
Status values: 0 unavailable, 1 invalid, 2 primed, 3 positionJump, 4 idle,
5 motion. A log line with input_unavailable means the probe could not read the
candidate fields, including platforms blocking process_vm_readv.

# Fly2 experimental flight

Fly2 is a separate toggle under Movement / Other. While enabled, the game
thread repeatedly applies Apollon's existing may-fly and flying ability calls
(layer 2, indices 10 and 9). Minecraft handles joystick, vertical controls and
hover. This is an experimental **ability-based adaptation**, not the supplied
KiwiMods packet-based Fly2 implementation: the settings and high-speed motion
formula from that file are not wired to it. Flight can be rejected or corrected
by a server and is not device-verified.

The toggle starts off. When disabled, the local-player tick clears the ability
bits in survival-like modes, except bits another Apollon flight toggle still
needs, following upstream's cleanup policy. It also attempts cleanup when
panic, the main menu or death interrupts flight. It never writes to a previous
player pointer after the player changes. Existing ability values are not read
or saved by Apollon, so this cannot promise perfect restoration of pre-existing
server-granted survival abilities. Normal physics resumes after disabling.

`fly2.cpp` is an isolated math translation of the supplied smali's packet-flight
algorithm; it is not the active Fly2 backend. Its tests remain as reference for
future work with verified native input and motion plumbing.

Host tests:

```
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_test.cpp apollon-adapter/fly/fly2.cpp -o /tmp/fly2-test && /tmp/fly2-test
g++ -std=c++17 -Wall -Wextra -Werror tests/fly2_adapter_test.cpp -o /tmp/fly2-adapter-test && /tmp/fly2-adapter-test
```

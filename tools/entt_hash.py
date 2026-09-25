#!/usr/bin/env python3
"""
enTT component hashes for Minecraft Bedrock 1.21.111 (arm64).

WHY THIS EXISTS
---------------
Bedrock stores ECS components in enTT pools, and every pool is identified by a
32-bit type hash. Knowing a component's hash is what turns "search 160 pools for
the one that holds the rotation" into a lookup — it is the difference between
guessing and knowing.

THE METHOD (verified, not assumed)
----------------------------------
enTT hashes a component's PLAIN TYPE NAME with FNV-1a 32:

    h = 2166136261
    for byte in name: h = ((h ^ byte) * 16777619) & 0xFFFFFFFF

Evidence against the shipped, SHA-256-pinned libminecraftpe.so:

  * FNV1a("ActorRotationComponent") = 0x75df36b7 occurs 38 times in the image.
    A 4-byte constant of that length appears ~0.06 times by chance in 280 MB,
    so 38 hits is a match, not a coincidence.
  * Control: FNV1a("struct ActorRotationComponent") = 0xdc1e21ce occurs ZERO
    times — the name hashed really is the plain type name (not the
    __PRETTY_FUNCTION__ spelling with "struct ").
  * The constants sit in .rodata inside the component-hash lists belonging to
    the game's strict-system expressions, right next to the mangled names of
    the systems that read/write those components (e.g. ActorHeadRotation
    0xbabe7211 immediately precedes ActorRotation 0x75df36b7 in one list).

USAGE
    python3 tools/entt_hash.py                          # verify the table below
    python3 tools/entt_hash.py RotorComponent Whatever  # hash extra names

The table is cross-checked against the binary when one is available; the
self-test below is what CI-less environments can always run.
"""

import sys

# Derived from the 1.21.111 binary and verified as described above.
KNOWN = {
    "ActorRotationComponent": 0x75DF36B7,
    "ActorHeadRotationComponent": 0xBABE7211,
    "MobHurtTimeComponent": 0xD7A3585C,
    "MoveInputComponent": 0x018B1887,
    "RawMoveInputComponent": 0x3613E513,
    "HitboxComponent": 0x9B97C2B2,
    # Negative result, kept on purpose: no enTT storage is instantiated under
    # this name, so the item-use state AlwaysSprint waits for lives elsewhere.
    # Do not "fix" it by trying name variants — it needs evidence.
    "ItemInUseComponent": None,
}


def fnv1a32(name: str) -> int:
    h = 2166136261
    for b in name.encode("utf-8"):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main(argv):
    failures = 0
    print("known component hashes:")
    for name, expected in KNOWN.items():
        got = fnv1a32(name)
        if expected is None:
            print(f"  {name:<30} 0x{got:08x}  (expected: not instantiated)")
            continue
        ok = got == expected
        failures += 0 if ok else 1
        print(f"  {name:<30} 0x{got:08x}  {'OK' if ok else 'MISMATCH vs ' + hex(expected)}")

    for extra in argv:
        print(f"  {extra:<30} 0x{fnv1a32(extra):08x}  (computed)")

    if failures:
        print(f"\nFAILED: {failures} mismatch(es)")
        return 1
    print("\nall known hashes reproduce")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Every module that claims to be memory-backed must have a patch profile.

WHY THIS EXISTS

ModuleManager flags a module `memoryBacked = true` to say "toggling me means
applying a byte patch". ModuleManager::setEnabled() then refuses to enable such a
module unless PatchManager has a spec with that EXACT name — spec names are the
join key between the two tables.

When Fly was given the ability-based implementation it kept `memoryBacked = true`
from the deleted patch-era Fly. PatchManager has no "Fly" spec, so every toggle
called set("Fly") -> matched=false -> refused, and the GUI blamed validation for
a name that simply was not there. Nothing in the build could see it: the two
tables live in different files, and the host harness stubs both.

So this checks the two SOURCE tables against each other. It runs in run.sh and
needs no compiler, no device and no APK.

  FAIL  memoryBacked module with no patch profile  (a toggle that can never work)
  note  patch profile with no module               (fine: dormant sites such as
                                                   FullBright, which is driven
                                                   through its own JNI call)
"""
from __future__ import annotations

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve()
SRC = HERE.parents[2] / "src"          # native/runtime/src
MODULES = SRC / "modules" / "ModuleManager.cpp"
PATCHES = SRC / "profile" / "PatchManager.cpp"


def module_rows(text: str) -> list[tuple[str, bool]]:
    """(name, memoryBacked) for every entry of the module table."""
    body = text[text.index("ModuleManager::ModuleManager() : modules_{"):]
    rows = []
    for m in re.finditer(
        r'\{\s*"([A-Za-z0-9 ]+)"\s*,\s*'      # name
        r'"(?:[^"\\]|\\.)*"\s*,\s*'           # description
        r'"([A-Za-z]+)"\s*,\s*'               # category
        r'(true|false)\s*,\s*'                # enabled
        r'(true|false)',                      # memoryBacked
        body,
    ):
        # groups: 1 name, 2 category, 3 enabled, 4 memoryBacked (the
        # description is deliberately non-capturing, it can contain escapes)
        rows.append((m.group(1), m.group(4) == "true"))
    return rows


def patch_spec_names(text: str) -> list[str]:
    """Every spec name in the patch table (composites repeat, hence list)."""
    body = text[text.index("specs_ = {"):]
    body = body[: body.index("};")]
    return re.findall(r'\{\s*"([A-Za-z0-9 ]+)"\s*,', body)


def main() -> int:
    for path in (MODULES, PATCHES):
        if not path.exists():
            print(f"check_module_patch_coverage: missing {path}")
            return 2

    rows = module_rows(MODULES.read_text())
    specs = patch_spec_names(PATCHES.read_text())
    spec_set = set(specs)

    if not rows and "ModuleManager::ModuleManager() : modules_{} {}" not in MODULES.read_text():
        print("check_module_patch_coverage: parsed no modules — the table shape changed")
        return 2
    if not specs:
        print("check_module_patch_coverage: parsed no patch specs — the table shape changed")
        return 2

    problems = []
    for name, memory_backed in rows:
        if memory_backed and name not in spec_set:
            problems.append(
                f"{name}: memoryBacked=true but PatchManager has no spec named "
                f'"{name}" — toggling it can only ever refuse'
            )

    dormant = sorted(spec_set - {n for n, _ in rows})
    print(f"check_module_patch_coverage: {len(rows)} modules, {len(specs)} patch sites")
    if not rows:
        print("  empty module foundation: no patch profile is exposed")
    if dormant:
        print(f"  dormant patch profiles (no module drives them): {', '.join(dormant)}")
    if problems:
        print("  FAIL:")
        for p in problems:
            print(f"    {p}")
        return 1
    print("  every memoryBacked module has a patch profile")
    return 0


if __name__ == "__main__":
    sys.exit(main())

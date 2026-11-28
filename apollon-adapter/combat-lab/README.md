# Experimental combat test build

`.github/workflows/combat-lab.yml` enables `ECLIENT_COMBAT_LAB=1` and produces the separate experimental APK. The normal build does not include these combat modules or hooks. Test in a disposable local world: hooks/calls may crash or affect inventory. APK signing keys are regenerated; back up worlds before changing installations.

- **Combat > Crystal Aura:** continuous breaking attacks an existing crystal within four blocks at most twice per second. The one-shot attack and adjacent placement experiments are separate controls; selecting the module does not enable attacks. Placement is *assisted*, not full automatic Crystal Aura. To test it, put two obsidian blocks next to one another; choose the direction and method, arm the 30-second test, then manually use a held crystal on the first block. GameMode + adjusted hit calls the game function again with the position and hit coordinates shifted. EndCrystalItem slot 123 calls the candidate crystal item method when reached. Both reuse live arguments and may fail, only appear locally, place a different held item or crash. Compare both blocks and re-enter the world if a crystal appears. Before/after sets when the extra call runs.
- **Combat > Auto Totem:** the module is explicitly an inventory experiment, **not implemented automatic equipping**. Its arm button makes one extra call through the candidate transfer function during the next suitable manual transfer. It does not select or identify totems. The transfer hook and outcome count are shown here.
- **Console:** shows every observed GameMode-use, crystal item-use, crystal attack and inventory-transfer hook event, plus captured test state and some module toggle changes. It is not a hook for every event in Minecraft. It keeps only the newest 100 events, supports scroll/follow/clear, and is memory-only; a restart or crash clears it. Events and native return values do not prove success. There is no Downloads log from this build.
- **Info > ClickGUI scale:** adjusts GUI window, font and controls for the current process session. Reset returns to 1x. The draggable, collapsible menu and touchscreen scrollbar are retained; this setting is not saved across process restarts.

The EndCrystalItem and transfer hooks are installed at startup only if their target prologues match the pinned library. Hook installation is independent of the enabled state of the experiments. The test targets the specified 1.21.111 ARM64 build; see `docs/INTERNALCPP_COMBAT_LEADS.md` for static evidence and limitations.

## Gatyo-based experimental combat

This opt-in build adapts code from the bundled Gatyo source for the pinned
1.21.111 arm64 library. Crystal Aura can scan near active players for an
obsidian/bedrock base, temporarily select a hotbar crystal, send equipment,
and call Gatyo's `GameMode::buildBlock`. A self-target testing option is
available. This is a simplified candidate search: Gatyo's damage/exposure,
entity collision and full targeting logic are **not** ported. It can hurt
or kill the local player; use a disposable offline world.

Auto Totem scans 36 inventory slots and constructs Gatyo's two-action
inventory/offhand transaction, retrying once per second until a totem is
observed. The user confirmed automatic placement and totem equip work on their device
in the build from Actions run 46130981543. The native layouts are still pinned
to this particular version and can crash or desynchronize other versions.

Fake Player now tries Gatyo's client-only AddPlayer NPC, follows it with movement
packets, equips the selected armor/sword/totem, intercepts melee, and simulates
health, absorption, armor, regeneration, golden apples, knockback, totem pops,
death/respawn and nearby crystal explosions. It can walk and simulate a pearl
teleport. A missing network handler or failed NPC lookup switches to a
world-anchored overlay mannequin; aim and attack it, or use Hit test. The
fallback can also be selected by this build's Crystal Aura if no real player is
available. The overlay is not Minecraft's actual player model, has no world
depth, and is not a native actor: other Apollon modules cannot automatically
target it. Crystal disappearance is an explosion heuristic, so unloading or
other removals can be misidentified. The full Gatyo virtual world renderer,
other aura integrations and combat/visual effects are not ported. Native NPC
creation, equipment, input hook and fallback rendering have **not** been tested
on a device. Check Console for handler and fallback status; it logs instrumented
events rather than every game event.

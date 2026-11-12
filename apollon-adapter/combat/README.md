# Crystal Aura / practice target investigation

This is not a functioning game module yet. The game-independent crystal planner
is tested, but intentionally not connected to native memory, the GUI, or game
commands. It can request base placement only on replaceable positions with a
support face, then wait for the world to confirm obsidian before requesting a
crystal. It tracks pending actions, checks configurable distances and handles
tick resets. All of these inputs must be established by a game adapter; the
planner does not estimate crystal damage or prove placements are safe.

The supplied Apollon wrapper provides actor iteration, player enumeration,
GameMode attack, and BlockSource::getBlock/isAir. It does not provide verified
obsidian/bedrock identity, inventory slot selection, a tested useItemOn ABI,
or a client-side player factory/lifecycle. A real fake-player entity needs a
verified spawn/removal path, integration with module target enumeration and a
local damage/totem simulation: using a fabricated Actor pointer would crash,
and a purely visual marker would not be a valid target. Crystal placement in
a local or server world must use the real game actions and inventory; fake pops
must not be claimed as server-authenticated. Nothing creates fake entities or
modifies a world until these native contracts are established.

## Read-only device diagnostics

Combat > Crystal test starts/stops a read-only, one-snapshot-per-second log.
It logs nearby actor type IDs and relative offsets, plus air/non-air and
session-local block-type tokens in a 3x3x3 area around the player's feet. It
does not record player names or absolute coordinates. Compare snapshots after
manually placing obsidian and crystals; the numbers are observations, not
verified Minecraft block IDs. Tokens are meaningful only within that process
and may be reused if the token table resets. Inventory, automatic placement,
crystal damage and fake-player/totem events are NOT captured because their
bindings are not implemented. The staging log is limited to about 1 MiB.
`Downloads/combat-test.txt` is overwritten on the next process launch; Android
10+ publishes through MediaStore and may rename it if a previous installation
owns an existing file. The log is not a crash logger and recent entries can be
lost if the process dies before the next copy to Downloads.

## Block identity investigation

The original Apollon `Block_getRenderLayer` uses `Block::getBlockTypeClassCustom()`
and reads its `std::string` at BlockType+8 on ARM64. Its existing Xray checks
the exact description `tile.obsidian` in `allowToXrayRender`; this is a useful
existing in-process path, not the wiki's numeric ID or the prior token. The
diagnostics now read that same candidate string and report it along with exact
`tile.obsidian` / `tile.bedrock` labels. The bedrock name is inferred from the
same naming pattern and both names should still be checked on the target device.
The wiki documents obsidian legacy ID 49, bedrock ID 7 and ender crystal entity
ID 71, but does not establish a native memory offset or item stack layout.
https://minecraft.wiki/w/Obsidian
https://minecraft.wiki/w/Bedrock
https://minecraft.wiki/w/End_Crystal

The `FastBridge` hook is labelled useItemOn in the source, but its declared
signature uses 32-bit integers for arguments on ARM64. A hook declaration that
merely forwards those values is not adequate proof of a safe call signature,
item stack ownership, face, reach or inventory selection. It is not called by
the crystal planner. The underlying block access remains read-only.

The read-only diagnostic additionally scans existing obsidian and bedrock
bases up to 4.5 blocks from the local player's eye position, with two blocks
of air above, and reports the nearest one. This is only a candidate: it does
not check target proximity, collisions, damage, inventory, or server rules.
The search is opt-in under Crystal test and has no game actions.

`practice_state.h` is a unit-tested local health and simulated-totem model;
it is not a visible player and is not connected to the game. A local-only
player still needs a real client actor factory/lifecycle and integration with
module targeting and actual explosion observations.

## Placement-call tracing (experimental)

Static disassembly of the exact APK's ARM64 entry at 0x0ccc3650 shows use of
x0/x1/x2, w3, x4/x5 and w6. This disproves upstream's six-argument mostly-int
wrapper, but does not by itself identify all parameter types or prove safe
synthetic calls. The wrapper now forwards all seven observed registers using
pointer-width fields and the return register, with a pinned entry-byte check.
FastBridge's existing return policy is preserved. Actual use is device-untested.

When Crystal test diagnostics are enabled, each observed manual useItemOn
candidate call writes its result, two scalar arguments, pointer-presence flags,
and, if readable, candidate block coordinates and a candidate float vector to
`Downloads/combat-test.txt`. **These can include absolute world coordinates.**
No raw pointers, inventory contents or names are recorded. The logger does not
identify slot changes, infer an inventory layout, call the function itself, or
spawn a fake entity. It logs an observed call even if the action was rejected;
only subsequent world observations establish whether anything changed.

## Focused test

Combat > Focused test is a separate, mutually exclusive logging mode. It records
placement-candidate calls and changes in the count of crystal actors within 12
blocks, sampled up to four times per second. It omits broad block and actor
snapshots. Count changes can mean actors entered or left range, not necessarily
placement or destruction, and quick events can occur between samples. The
placement hook cannot identify the item, so unrelated item uses can appear.
Inventory/offhand changes are **not automatically captured**: their native
bindings are unknown. Optional buttons record explicitly user-reported markers,
not verified game events. Log path, coordinate privacy, size and persistence
caveats are the same as above. No new inventory hook or game action is invoked.

## One-shot crystal break test

Combat > Crystal break test arms a single attack attempt for 10 seconds. On a
ready game tick it finds the nearest existing crystal actor within 4 blocks and
calls Apollon's existing GameMode::attack wrapper once, then disarms. It cancels
on panic, leaving the world, or an unready tick. No actor pointer is retained
across ticks. "Attack sent" means only that the native call was made, not that
Minecraft or a server accepted it. This uses an upstream ABI that has not been
independently verified on device; a crash is possible. Crystal explosions can
kill players and damage worlds. Use only a disposable test world. It does not
place crystals, target players, handle inventory or implement Crystal Aura.

## Client-only simulated practice target

Combat > Practice target spawns a local overlay figure three blocks in front of
the player, with simulated health and unlimited totems. It is a virtual target,
not a Minecraft Actor, and is never sent to the server. It does not collide or
receive real damage. The figure is projected with the same view matrix as ESP,
so it may draw through blocks. It is removed when the world is unavailable.

Crystal break test can optionally limit its one-shot attack to crystals within
four blocks of this virtual target. A sent attack request near the target adds
one simulated pop; this does not verify an explosion, model actual damage or
consume a real totem. This demonstrates the local target and real break action
working together; it does not implement Crystal Aura, placement, Baseplate,
real-player selection, or inventory switching.

## Partial Crystal Aura

The opt-in Combat > Crystal Aura module continuously attempts to break the
nearest crystal within the configured reach and within the configured distance
of a real player or the simulated practice target. It does not retain actor
pointers between ticks. There is no one-shot or attack-count limit, although
Minecraft/server reach and validity checks still apply. Real-player selection
matches the local player's entity type, excludes self, but does not yet apply
friend filtering or model damage. The client does not guarantee that any
attack was accepted. It is off by default and can damage the local player,
other players, and blocks. The virtual target's pop count is simulated on the
first attack attempt against an observed crystal; it is not real damage.

**Automatic placement, Baseplate, inventory selection and totem switching are
not implemented.** The seven-register placement hook is a forwarder, not a
verified callable contract: the lifetime and semantics of its pointer
arguments remain unknown. Do not synthesize calls or store its borrowed
pointers across ticks until these contracts are verified.

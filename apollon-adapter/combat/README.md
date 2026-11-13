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

Combat > Crystal Aura > Diagnostics and tools > Crystal test starts/stops a read-only, one-snapshot-per-second log.
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

Combat > Crystal Aura > Diagnostics and tools > Focused test is a separate, mutually exclusive logging mode. It records
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

Combat > Crystal Aura > Diagnostics and tools > Crystal break test arms a single attack attempt for 10 seconds. On a
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

## AutoTotem native investigation (1.21.111 pinned library)

The user-provided mapper report's `libminecraftpe.so` SHA-256
`65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1`
matches the pinned APK's library. The selected APK SHA differs, so only the
library match is established. Reproducible offline inspection is in
`tools/inspect-native-references.py` and the `inventory-native-inspection`
workflow. Addresses are ELF virtual addresses for that exact library only.

- The reference to `ItemStackNetManagerClient::_beginRequest` lies in unwind
  range `0xccc92618..0xccc9280c`; its entry saves x0 and inspects w1. Its one
  direct BL caller is `0xccc92584`, in an unwind range starting `0xccc92534`.
- The reference to `ItemStackNetManagerClient::addRequestAction` lies in
  unwind range `0xccc93130..0xccc93614`. Its entry dereferences x1 and reads
  manager-like state from x0+0x60. Wrappers at `0xcca0da4` and `0xcca0fb0`
  transfer a pointer through x1 and zero the source before calling it. They
  show ownership/destruction behavior; copying a pointer and replaying a call
  later could cause use-after-free or double deletion. These names are inferred
  from log strings and may be inlined; unwind ranges do not prove ABI.
- A direct caller of wrapper `0xcca0fb0` at `0xccd45ac` constructs an object
  via `0xccd49e0` before the call. This is a useful follow-up lead, not yet
  proven to be a totem swap or a constructible public API.
- `selectedSlot`/`selectedSlotIndex` references are within the same large
  unwind range `0xc5755a4..0xc577f40`, not verified hotbar accessors. The
  `CONTAINER_ID_OFFHAND` reference is in `0xa295504..0xa295660`; it appears
  in a sequence registering named constants, not an inventory move function.

There is still no verified route from LocalPlayer to the live transaction
manager, no slot/item identification, and no proven game-owned swap action
constructor or server acknowledgment flow. AutoTotem is **not implemented**.
Do not expose a button that calls the above addresses with fabricated pointers;
a crash, inventory desynchronization, or item loss cannot identify which
contract was violated. Use disposable test worlds for any eventual live probe.

### Further pinned-library findings

RTTI resolved through ELF relative relocations identifies the action built by
`0xccd49e0` / `0xcc9c240` as `ItemStackRequestActionMineBlock`, **not** a swap.
The two earlier legacy constructor candidates identify as
`ItemStackRequestActionCraftNonImplemented_DEPRECATEDASKTYLAING` and
`ItemStackRequestActionCraftRecipeOptional`. None should be used to equip a
totem. The manager-candidate getter at `0x6bee850` merely returns `[x0+0x9d8]`;
its input and relationship to LocalPlayer are still unverified.

`tools/find-inventory-action-types.py` scans RTTI names, relative relocations,
and conservative ADRP+ADD references for actual swap/take/place action types.
The pinned library contains `ItemStackRequestActionSwap`, `...Take`, and
`...Place` strings, but this scan did not resolve their typeinfos or constructors;
that is a limitation of this technique, not evidence those actions do not exist.
See inventory inspection Actions run 43384027828. These findings do not yet
provide item identity, offhand slot addressing, a safe transaction ABI, or server
acknowledgment. No inventory move or on-device native invocation was performed.

### Swap-action constructor lead

Following the transfer-base RTTI inheritance through ELF relocations found a
candidate swap-action vtable at `0x1037a940`, with code references at
`0xcc98d8c` and `0xcc9db2c`. The latter is in unwind range
`0xcc9da9c..0xcc9dbd8`; the entry initializes a transfer-base object and
then stores the swap vtable. Two direct callers allocate 96 bytes and call it
with two input object pointers. The wrapper at `0xcd7ceb8` returns the owned
allocation through x8; its direct caller is `0xcd75a84`. Another call is in
`0xcd7f7e8..0xcd7fe2c`, called directly at `0xcdc9280`. Static observations
only: neither input object's layout nor the enclosing context or transfer of
ownership is verified, and direct-call scans miss indirect calls. This does not
establish item identity, slot IDs, inventory manager access, acknowledgment, or
a safe AutoTotem implementation. Inspection: Actions run 43385995426.

The earlier statement that the scan did not resolve a swap constructor is
superseded by this inheritance-based lead. Vtable enumeration may include
false positives where inheritance pointers resemble vtable headers; references
and unwind metadata are evidence to investigate, not a public API.

### Passive swap trace (experimental)

Crystal Aura > Diagnostics and tools > Inventory swap trace records the first
13 opaque bytes from each constructor input while enabled (100 events per
recording) in Downloads/combat-test.txt. Use Mark next manual swap to label
individual manual moves; no interpretation of the bytes is assumed. The 13
bytes may contain inventory-related metadata; do not share logs publicly if
that is sensitive. The hook is installed at startup when the pinned 16-byte
prologue matches, even while recording is off; it forwards the original call,
never replays pointers, and never changes inventory. A hook or ABI error can
still crash or interfere with gameplay. This is not device-tested; no event
may mean the user's move uses another path. Constructor events do not prove a
server accepted a swap. Use a disposable local world and back it up first.

The follow-up trace also hooks the `addRequestAction` candidate at
`0xcc93130`, after checking its pinned prologue. It reads the action type byte
and, only when the runtime vtable matches the statically identified take,
place or swap vtables, two 13-byte candidate transfer prefixes. All reads use
`process_vm_readv` and only occur while tracing; the hook itself is installed
at startup when its prologue matches. It forwards the original call and does
not retain or replay pointers. The vtable labels and field meanings are static
hypotheses, not validation or proof of a completed server transaction. Events
may arise from other inventory operations, may be missed on different paths,
and share the 100-event cap with constructor events. The additional hook is
experimental, may crash or interfere with the game, and has not been device
validated. Avoid sharing the opaque log if inventory metadata is sensitive.

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

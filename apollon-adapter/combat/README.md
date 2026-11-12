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

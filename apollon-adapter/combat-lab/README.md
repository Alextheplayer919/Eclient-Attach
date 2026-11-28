# Experimental combat build

The user confirmed crystal placement and automatic totem equip worked on their
device in Actions run 46130981543. The confirmed `GameMode::buildBlock`, hotbar
switch and offhand transaction calls remain in `gatyo_combat.h`. Crystal Aura
now has one toggle, delay and action range, affecting both placement and
breaking. Auto Totem has one toggle and retry delay. The former one-shot
placement/transfer experiments and their UI/hooks have been removed. The
candidate placement search and crystal attack are simplified; no Gatyo damage
prediction is present. Range controls local crystal break/place reach; real
player target scanning still uses the earlier 10-block acquisition limit.
Console logs instrumented events, not every game event.

Fake Player tries a client-only AddPlayer NPC, equipment and movement packets,
and switches to a virtual model if the network handler or actor creation fails.
The virtual path can also be forced on the next spawn. Its fallback now uses
Gatyo's extracted Steve skin and player cuboid UV layout, projected in the
separate overlay; unlike Gatyo's native world renderer it has no world depth.
It includes simulated health, armor, totems, regeneration, golden apples,
knockback, crystal explosions, respawning, walking and pearl motion. It is not a
complete source port: Gatyo's in-world renderer, all aura integrations and some
effects are not integrated. Crystal disappearance is only an explosion
heuristic. The NPC/fallback changes need device testing. Select Fake Player,
spawn in-world, and inspect Console and mode status if it does not appear.

`gatyo-reference/` contains extracted upstream dependency sources and
`steve_skin.h` the Gatyo image; the original full archive remains in the repo.
See `docs/GATYO_ROTATIONS_AND_FLY.md` for rotation findings and the separate
Bypass Fly port. All native calls are pinned to the provided 1.21.111 ARM64
binary and can crash or desync. Test in an offline disposable world.

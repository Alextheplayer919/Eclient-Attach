# Schematic C++ groundwork

This is a **read-only, offline core**, not yet integrated into the client GUI,
Android file picker or a renderer. It does not place blocks, create actors,
call game offsets, or show holograms. It reads uncompressed Bedrock
`.mcstructure` NBT into a bounded model with both block layers and typed palette
states; it validates dimensions, counts and palette indices and rejects inputs
over 32 MiB / 2 million cells. Block entities, entity data, origin and non-default
palettes are not exported to the model. It currently does not read Java `.schem`,
`.schematic`, `.litematic`, compressed files, or convert them to Bedrock.
The independent `anchor.h` supplies a lockable world-space origin and quarter-turn transforms, tested without game integration; it does not follow the player. No texture/model mapping, in-game controls or block placement logic is included.

The model and file-format approach are informed by the earlier Kotlin
[Eclient SchematicModel](https://github.com/Alextheplayer919/Eclient/blob/main/app/src/main/kotlin/com/nexoraclient/core/schem/SchematicModel.kt),
but that source's common x-fastest indexing is not correct for Bedrock
`.mcstructure`. Bedrock stores Z fastest, Y next, X slowest as described by the
[Bedrock Wiki format description](https://wiki.bedrock.dev/nbt/mcstructure.html#block-indices).
`Structure::at` uses that order; an asymmetric fixture checks this. Invalid
indices are rejected rather than silently replaced. An unmodified small Bedrock Wiki example was also parsed offline (2×2×4, six palette entries); this is not an in-game test. Version compatibility,
non-cube models, waterlogging representation, custom blocks and actual game
assets are not yet tested. The old state mapping table targeted a newer version,
not necessarily 1.21.111; do not silently assume mappings match.

Run the host test:

```
g++ -std=c++17 -Wall -Wextra -Werror tests/mcstructure_test.cpp apollon-adapter/schematic/mcstructure.cpp -o /tmp/mcstructure-test
/tmp/mcstructure-test
```

The standalone Mapper APK now searches for block tessellator/renderers, model,
texture atlas and mesh/material names. Its results are static and heuristic:
string references are not verified function entrypoints, signatures, state
layouts, render context, atlas access or proof that an address is callable.

On the pinned 1.21.111 ARM64 library (SHA-256
`65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1`),
[mapper run 43448675732](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/43448675732)
reported string-reference leads at `0x65f36b8` for `textures/terrain_texture.json`,
`0x66e5a38` for a `BlockGraphics::getTextureItem` diagnostic string, and
`0x675fcb4`/`0x675fee8` for `BlockTessellator`-related names. These are
heuristic instruction locations referencing strings, **not** verified
renderer function entries or callable offsets. They may refer to diagnostic,
registration or unrelated code. Atlas access, game render-thread integration,
resource-pack reload handling, transparent materials and block models remain
unverified.

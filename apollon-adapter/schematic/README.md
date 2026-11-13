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

### Device mapper report and asset inspection

The uploaded `eclient-mapper-report (2).txt` hashes to the same pinned
`libminecraftpe.so` above; its selected APK hash differs, so this confirms the
native library, not that the complete APK is byte-for-byte identical. Pinned
inspection runs [43450254762](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/43450254762)
and [43450462593](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/43450462593)
show that `0x675fcb4` and `0x675fee8` occur in tiny routines comparing type-name
pointers; they are not demonstrated block tessellation entrypoints. The
`BlockGraphics::getTextureItem` string reference at `0x66e5a38` lies on an
assertion/diagnostic path inside unwind range `0x66e5240..0x66e5be0`, not a
callable address established by the mapper. `textures/terrain_texture.json`
references occur inside ranges beginning at `0x65f364c` and `0x9fcaa7c`;
the latter includes JSON reader code. None verifies the runtime atlas, its
lifetime, render-thread requirements, or valid block-model rendering calls.

The pinned APK's [asset inspection](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/43450909136)
found 2,608 matching terrain-map/block-texture paths, including resource-pack
`vanilla/textures/terrain_texture.json`, many versioned packs, and 31 candidate
`blocks.json` files. This offers an alternative path to investigate for default
textures without guessing a native rendering ABI. A simple strict JSON parse of
the base `blocks.json` failed; its encoding/extensions need inspection. Android
AssetManager paths, pack priority, active/custom resource packs, models, face
textures and animated textures still need validation. No texture mapping or
in-game hologram has been implemented.

### Vanilla texture lookup groundwork

`texture_catalog.cpp` is a C++ resource-map resolver for ordinary blocks. It
reads Bedrock `blocks.json` and `terrain_texture.json`, accepting JSON comments
as found in the pinned assets. It can resolve per-face texture keys, chooses the
first entry for array variants while marking that fallback, rejects unsafe
paths, and accepts explicitly ordered pack layers (entry-level override only).
It returns resource-relative paths; it does not load images, draw blocks, access
Android's assets, apply block-state rotations/tints, or discover the active
resource pack. Its merge order and semantics are not verified against the game.
No claims are made for custom blocks, animated textures, waterlogged blocks,
non-cube models or transparent materials.

The [pinned APK test](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/43452271350)
loaded the actual base vanilla maps, resolved stone, grass (top, side and bottom)
and dirt, and verified that their PNG files exist in the APK. The code also
cross-compiled with the NDK for ARM64. The texture mapping returns array variant
fallbacks for these examples, not confirmed state-specific results. The code is
standalone and not yet integrated into the client APK or tested on a device.
The JSON dependency is the nlohmann single header already supplied inside the
Apollon source archive; it is extracted to a temporary build directory for tests.

### ARM64 direct-branch mapper

`tools/arm64-callmap.py` is a separate read-only offline decoder. It reads executable ELF64 AArch64 segments and `.eh_frame` FDEs (via `readelf`), decodes direct `B`/`BL` immediates, and reports incoming/outgoing edges for the FDE containing each supplied site. The default sites are permitted **only** for the SHA-256-pinned 1.21.111 library; on other binaries pass explicit sites, e.g. `python3 tools/arm64-callmap.py --lib libminecraftpe.so --site candidate=0x123456 --output callmap.json`. Output includes hash, FDE bounds, edge locations, target addresses and full counts, with bounded edge lists. This complements, but does not replace, the standalone Android mapper. The inspection workflow runs it on the pinned APK and saves the JSON artifact.

These are not hook offsets: FDEs can be absent/overlap, executable bytes can contain embedded data, `B` can be a jump rather than a call, indirect/virtual calls are absent, and a target need not be a function entry. No signature, ABI, thread, lifetime or runtime behavior is established. Raw virtual addresses require the correct runtime load bias before use. The tool does not read or modify a running process.

Research: [MCBE DX12 template](https://github.com/xpd98x/mcpd98) describes a native `LevelRenderer::renderLevel` 3D path distinct from a projected ImGui overlay; [Actually Connected Textures](https://github.com/Alastor-Kaneki/Actually-Connected-Textures) describes block tessellation/texture hooks for a different game version. These Windows/version-specific examples are architectural leads, not Android 1.21.111 offsets or ABI proof. A native path might preserve depth and geometry, while an overlay needs independently verified camera projection, depth handling and models. Neither route has yet been validated here.

### Targeted Ghidra report review

The uploaded `ghidra.log`, `decompile.txt` and `sites.tsv.txt` correspond to the targeted six-range research run. All six report a decompile result. Ghidra's import base was `0x100000` while the ELF minimum `PT_LOAD` virtual address was `0x0`: Ghidra's displayed addresses in this report are ELF virtual addresses plus `0x100000`, **not** runtime addresses. This relationship is specific to this import and must not be used as a runtime load bias.

- The three tiny FDEs at ELF `0x675fcb0`, `0x675fee4` and `0x6765824` decompile to pointer comparisons that return either the first argument plus eight or null. They are not evidence of a draw/tessellation routine.
- The texture diagnostic FDE at `0x66e5240` contains string/container handling and indirect calls. Its diagnostic string is not proof that this entry is `BlockGraphics::getTextureItem` or that its signature is known.
- The second terrain reference FDE at `0x9fcaa7c` constructs/reads data, iterates elements and calls other code. It does not establish a world rendering callback. One direct downstream lead visible in Ghidra pseudocode is `0x0f99be6c` in that import, i.e. ELF `0x0f89be6c`; its purpose and entry/ABI remain unverified.
- The first terrain FDE at `0x65f364c` has many "removing unreachable block" and PIC warnings. Do not infer control flow from that pseudocode without validating disassembly and function boundaries. `-noanalysis` and manually supplied FDE bodies make this a targeted, incomplete analysis, not a recovered source tree. The log also reports 812 unresolved external symbols; imports were not linked to their libraries.

The next analysis should use the direct-branch map and disassembly to distinguish resource loading from actual per-frame world rendering, then decompile the most relevant callers with verified ELF address translation. A successful decompile is not proof of callable arguments, thread safety, camera matrices, texture state, or hook safety.

### Existing client source: a different drawing route

The archived Apollon source in `Apollon_Client_ImGui_v4.62_1.21.111 (3).zip` already has an ImGui overlay path in `app/src/main/jni/menu.h`, `menu_utils.h` and `hook_utils.h`. `RenderOreESP` projects eight world-space block corners using `OWorldToScreen`, camera position, field of view and a view matrix, then draws lines. `ClientInstance` / `LevelRendererCamera` accessors and candidate ARM64 field offsets are in `hook_classes.h` / `offsets.h`. These source offsets and the projection have **not** been independently verified for holograms or on-device in this investigation. Projection also rejects corners behind the camera and does not clip intersecting geometry correctly.

The adapted `Platform2.java` creates its own translucent, on-top `GLSurfaceView`; this is a separate overlay, not evidence of a native world-render callback or access to Minecraft's depth buffer. The source reads game-side camera data during overlay drawing, so data lifetime and cross-thread synchronization need attention. This can be investigated as a lower-risk world-anchored outline prototype, but it alone cannot reproduce correct depth occlusion, perspective-correct textured faces, arbitrary native models, resource-pack selection, tints or animation. It does not satisfy the final textured hologram requirement by itself.

`tools/inspect-apk-render-files.py` and the separate `apk-render-inventory.yml` workflow enumerate the pinned APK's ARM64 shared libraries, ELF `DT_NEEDED` entries, and shader/material-path candidates without producing a new Minecraft APK. Assets and graphics API imports are leads, not proof of renderer ownership or a callable ABI.

# Internal C++ extractor: inventory and placement leads

The user-supplied `INTERNALCPP_OFFSETS_1.21.111.1.7z` contains a roughly 218 MB JSON. Its reported AArch64 library SHA-256 matches the pinned `libminecraftpe.so` (`65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1`). The input APK is still independently SHA-verified in CI. The archive is tracked; the extracted JSON is temporary and should not be committed.

The extractor supplies class names and numbered `vfunc_N` slot targets, not verified names or C++ method signatures. In particular, none of these numbered entries alone identifies an offhand setter, swap API, attack, crystal placement or server acknowledgment. The extractor marks many entries `verified`, but that label is not a runtime test; at least one typeinfo-name symbol is also classified as a function. Addresses are ELF virtual addresses, not runtime pointers.

Selected class header addresses and slot counts reported by the extractor:

| Candidate class | Vtable header | Slots |
| --- | --- | ---: |
| GameMode | `0x1037c398` | 21 |
| PlayerInventory | `0x10399ff8` | 5 |
| Inventory | `0x10399608` | 56 |
| OffhandContainerModel | `0x1038dce0` | 26 |
| ContainerTransferClient | `0x10391b80` | 4 |
| ItemStackRequestActionSwap | `0x1037a930` | 7 |
| ItemStackRequestActionPlace | `0x1037a8e8` | 7 |
| EndCrystalItem | `0xff26c80` | 124 |

The swap header plus the 16-byte Itanium header equals `0x1037a940`, consistent with the previously identified candidate address point in [the archived research](CRYSTAL_AURA_RESEARCH_ARCHIVE.md). That is a useful cross-check, not new proof of a safe transaction. The old device trace observed place-type requests and did not observe swap-constructor calls. Existing `GameMode::useItemOn`/attack labels from Apollon are likewise leads, not a validated synthetic placement ABI.

`tools/check-internalcpp-vtables.py` compares the selected JSON slots to the pinned library's `R_AARCH64_RELATIVE` relocations, assuming the extractor's vtable address is an Itanium header with the function slots starting 16 bytes later. It reports matched, differing and unresolved entries. Other relocation kinds and relocations not decoded by `readelf` are explicitly unresolved, not failures or successes. Matching slots establish a static address relationship only; they do not validate target identity, layout, use, object ownership, arguments or server state. `.github/workflows/internalcpp-check.yml` performs this check without injecting code, and saves only a small report.

Next investigation: compare those outputs with the prior place/swap analysis, disassemble candidate functions and their callers, and determine real inventory/slot data, item identity, context, ownership and server acknowledgment. Crystal Aura and AutoTotem are **not implemented** in the current client. Do not re-enable the archived hooks or invoke addresses using guessed pointers; inventory mutation can desynchronize or lose items.

### Pinned library comparison

GitHub Actions run `45442298595` compared these eight classes with the pinned library: all 250 selected slot targets matched `R_AARCH64_RELATIVE` relocation addends (GameMode 21, PlayerInventory 5, Inventory 56, OffhandContainerModel 26, ContainerTransferClient 4, swap 7, place 7, EndCrystalItem 124); zero differed or were unresolved in this set. This verifies the static slot target values under the stated header assumption, **not** the function names, method ABI, writable operations, use of an object or observed behavior.

`.github/workflows/ghidra-inventory.yml` targets a small selection of those slots and the prior placement/attack/constructor leads for read-only bounded decompilation. Its results remain incomplete, particularly without automatic analysis, and cannot validate safe synthetic calls.

Targeted Ghidra run `45442547810` decompiled selected FDEs without whole-library analysis. A useful connection: the extracted `ContainerTransferClient` slot 3 target is ELF `0xcdc921c`; its decompilation calls Ghidra address `0x0ce7f7e8` (ELF `0xcd7f7e8`, after subtracting that import's `0x100000` image-base adjustment). That agrees with the earlier direct-call trace into the swap-construction wrapper. The decompiled routine checks a weak-reference-like control object and a non-null input before making the call; its complete signature, object types, lifetime rules, slot payload and success semantics remain unknown.

The extracted PlayerInventory slot 2 target (`0xce23804`) decompiles as a call to slot 1 followed by `operator delete`, suggesting a deleting destructor, **not** a slot switch method. Offhand slot 3/4 contain virtual calls and context checks, not self-describing equip APIs. These conclusions are based on limited decompilation and require disassembly/runtime confirmation. Decompiled `useItemOn`/attack candidates emitted control-flow/PIC warnings; no new placement or attack ABI is proven. Report artifact: `ghidra-inventory-research` in run `45442547810`.

### User-provided crystal, placement and inventory class leads

Additional pinned-ELF cross-check: Actions run `45453878945` compared the user-listed packet, transaction, BlockSource, EnderCrystal, EndCrystalItem and inventory-related class slots. Matching relocation values only identify vtable targets, not their semantics; the JSON's numbers do not give method signatures or imply that packet constructors can be called on a player object.

A comparison to BedItem, FlintAndSteelItem, EnderEyeItem and EggItem narrowed the EndCrystalItem-specific slots to 1, 40, 48 and 123. In targeted Ghidra run `45453568570`, slot 1 appears to be a deleting destructor, slots 40 and 48 return 1, and slot 123 points to ELF `0x5885518`. Its decompilation reads the supplied position, checks blocks above it and creates entity type `0x47` (71), strongly suggesting a crystal-use routine; the inferred `x0..x2, x3/x4` signature, return value, client/server behavior and effects remain experimental. Its first 16 target bytes on the pinned library are `fd7bbba9fc6701a9fd030091f85f02a9`. The separate opt-in Combat Lab hooks that method, forwarding the original registers and attempting an adjacent call only while armed. It logs whether the hook fired and the result; a call is not proof that a server accepted placement. The test might only affect client state, not place a real crystal.

Ghidra's GameMode validation candidate `0xccc2d58` uses the fifth register argument as three absolute floats and subtracts the block coordinates. Accordingly the new GameMode experiment shifts both the candidate BlockPos and copied hit position together; this tests a concrete likely reason the prior synthetic call returned 0. It does not establish the item, block-pointer or remaining arguments' semantics. The test is manually triggered, not autonomous Crystal Aura. User reported the earlier adjacent call did not place a crystal.

The additional packet and inventory vtables are leads for future experiments; they are not yet called or used to equip a totem. The earlier blanket instruction against test invocations above is superseded for the separate opt-in experimental build, as requested by the user.

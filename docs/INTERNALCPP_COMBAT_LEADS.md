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

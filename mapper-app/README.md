# E-Client Mapper (standalone Android app)

This is a separate package (`com.eclient.mapper`), not a Minecraft patch. It
never injects, installs, opens, or modifies Minecraft. No network permission is
requested. Choose an APK containing `lib/arm64-v8a/libminecraftpe.so`, an ARM64
split APK containing that entry, or the extracted ELF library with Android's
file picker. The app copies it into its private cache, analyzes it on a worker
thread, and removes the temporary copies after analysis. A completed report is kept in the
app's private storage and restored if the app is recreated. Export the report to a
user-selected location with Save report. Large APKs require enough free space
for both the copied APK and extracted library; inputs are limited to 2 GiB and
libraries to 1 GiB. Keep the app open while analyzing. Debug builds use an
ephemeral signing key, so uninstall an older mapper build before installing a
new one.

The report contains SHA-256 hashes, ARM64 ELF load mappings, ranked and category-capped ASCII
strings and ELF symbols, and heuristic ARM64 ADRP+ADD string reference
**candidates** (up to three intervening instructions). Both file offsets and ELF virtual addresses are
labelled. It looks for inventory, offhand/totems, interaction/placement,
crystal/attack and item/transaction terminology. Filtering is deliberately
bounded and broad; strings and dynamic symbols can be stripped or obfuscated.
The xref pass is not a disassembler: it misses indirect references, other
instruction sequences and longer gaps, and can have false positives (especially
when intervening instructions change registers). Only selected, capped strings
are searched for references. File offsets are not runtime addresses. Nothing identifies a safe
function ABI, object lifetime, selected slot, offhand behavior, action success,
or a guaranteed correct offset. Do not call or patch an address based solely on
the report. No current gameplay or live process state is observed.

Run the host test with:

```
mkdir -p /tmp/eclient-mapper-tests
javac -d /tmp/eclient-mapper-tests mapper-app/src/com/eclient/mapper/ElfMapper.java mapper-app/src/com/eclient/mapper/ReportStore.java mapper-app/tests/ElfMapperTest.java mapper-app/tests/ReportStoreTest.java
java -cp /tmp/eclient-mapper-tests com.eclient.mapper.ElfMapperTest
java -cp /tmp/eclient-mapper-tests com.eclient.mapper.ReportStoreTest
```

Build with Android SDK platform 35 and build-tools 35 on PATH:
`tools/build-mapper-apk.sh`; the standalone GitHub Actions workflow produces
`eclient-mapper-standalone`. This app is intentionally separate from the
existing patched Minecraft APK build pipeline.
The build also runs the mapper against the SHA-256-pinned Minecraft library and
uploads its text report separately as `eclient-mapper-pinned-report`; it does
not include the library itself.

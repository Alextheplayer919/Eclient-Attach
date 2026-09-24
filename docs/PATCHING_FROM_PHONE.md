# Patch Minecraft from your phone (no PC, no MT Manager)

MT Manager is NOT used anywhere in this flow. Patching and signing happen in
GitHub Actions with the desktop tools (`apktool`, `zipalign`, `apksigner`
v1+v2+v3) — nothing runs on the phone except upload/download/install.

## One-time: upload the stock APK

The CI needs the exact unmodified Minecraft 1.21.111 ARM64 APK as a release
asset named **`Minecraft-1.21.111-arm64-v8a.apk`** in this repo. The pipeline
pins SHA-256 `69f6584c…3d59`; a wrong build fails loudly before patching.

Any upload route works:
- GitHub mobile web → repo → **Releases → Draft new release → attach APK**
  (title/tag irrelevant, asset NAME matters), or
- ship.py / `gh release upload <tag> <apk>` from Termux.

## Every build

1. GitHub mobile web → repo → **Actions → "Build E-Client 1.21.111 ARM64" →
   Run workflow** (asset_name defaults correctly).
2. Wait ~5–10 min. Red/green is honest: any validation failure stops the pipe.
3. When green, download directly (fixed URL, always the newest build):

   ```
   https://github.com/Alextheplayer919/Eclient-Attach/releases/download/patched/eclient-minecraft-1.21.111-arm64.apk
   ```

4. Install the downloaded APK (Play Protect off for sideloads). It is signed
   with the repo's shared debug key (`.signing/eclient-debug.p12`), so **new
   builds install over old ones** — no uninstall, no signature conflicts,
   works with stock Minecraft still present only if already uninstalled (same
   package name as stock Minecraft: one must win).

## Troubleshooting (all visible, nothing silent)

- Workflow red at *Verify Minecraft APK SHA-256* → wrong APK build; get the
  exact 1.21.111 ARM64 one and re-upload.
- "App not installed" only appears now if a DIFFERENT cert's copy of
  `com.mojang.minecraftpe` is still installed (stock Play version, an MT-signed
  copy, …). Uninstall that one, then install ours.
- Low storage also blocks installs silently; keep ≥2.5 GB free.

## Why not apktool on the phone itself?

Possible in Termux (openjdk-17 + apktool jar + zipalign + apksigner), but the
MC APK is ~700 MB: decode+rebuild needs several GB of RAM and many minutes,
and any RAM kill produces exactly the "package appears invalid" class of
failure MT Manager keeps giving you. The CI route does the same steps with
desktop RAM and verified tooling — the phone only moves files.

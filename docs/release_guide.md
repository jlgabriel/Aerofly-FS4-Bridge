# Release Guide (GitHub Releases)

This guide explains how to build the DLL and publish a GitHub Release with the compiled binary as an asset.

## Prerequisites

- Windows 10/11
- Visual Studio 2022 (Build Tools or full VS)
- Aerofly SDK header: `tm_external_message.h` (download from `https://www.aerofly.com/developers/`, section “External DLL”)
- Optional (CLI automation):
  - PowerShell 7+
  - GitHub CLI (`gh`) authenticated: `gh auth login`

## 1) Build the DLL

1. Place `tm_external_message.h` in the repo root.
2. Open PowerShell in the repo root and run:
   ```powershell
   # Method 1: CMake (Recommended - v0.4.0+)
   .\scripts\build.ps1 -Config Release

   # Method 2: Legacy batch script
   scripts\compile.bat
   ```
3. Output will be written to:
   - CMake: `build\Release\AeroflyBridge.dll`
   - Legacy: `dist\AeroflyBridge.dll`

Tip: Test locally by copying the DLL to Aerofly's DLL folder:
```powershell
# CMake build
copy build\Release\AeroflyBridge.dll "$env:USERPROFILE\Documents\Aerofly FS 4\external_dll\AeroflyBridge.dll"

# Or with build.ps1 install flag
.\scripts\build.ps1 -Config Release -Install
```

## 2) Prepare release assets

At minimum:
- `AeroflyBridge.dll`

Optional (nice to have):
- `AeroflyBridge_offsets.json` (appears next to the DLL once Aerofly loads the DLL at least once)
- `CHANGELOG.md` (manual notes of what changed)
- Checksums

Generate checksums (optional):
```powershell
# CMake build
Get-FileHash build\Release\AeroflyBridge.dll -Algorithm SHA256 | Format-List

# Legacy build
Get-FileHash dist\AeroflyBridge.dll -Algorithm SHA256 | Format-List
```

## 3) Create the GitHub Release (UI)

1. Push your commits to `main`.
2. Go to the repo → Releases → “Draft a new release”.
3. Tag version (e.g., `v1.0.0`) and title (e.g., `v1.0.0`).
4. Release notes: summary of changes from CHANGELOG.md.
5. Upload asset(s): `build\Release\AeroflyBridge.dll` or `dist\AeroflyBridge.dll` (and optional files).
6. Publish.

## 4) Create the GitHub Release (CLI)

With `gh` installed and logged in:

```powershell
# Example: create release v1.0.0 with DLL asset
$version = "v1.0.0"
$notes = "Initial public release. Includes WebSocket/TCP/Shared Memory bridge."

# CMake build
gh release create $version build\Release\AeroflyBridge.dll -t $version -n $notes

# Legacy build
gh release create $version dist\AeroflyBridge.dll -t $version -n $notes
```

To include more assets:
```powershell
gh release create $version build\Release\AeroflyBridge.dll AeroflyBridge_offsets.json -t $version -n $notes
```

## (Optional) One‑shot helper script

Use the provided `scripts\make_release.ps1`:

```powershell
# Build and create a GitHub release with the DLL
# Example: ./scripts/make_release.ps1 -Version v1.0.0 -NotesPath .\CHANGELOG.md
./scripts/make_release.ps1 -Version v1.0.0
```

This script:
- Runs `scripts\compile.bat` (legacy build)
- Verifies `dist\AeroflyBridge.dll`
- Creates the GitHub release via `gh release create`

**Note**: For CMake builds, use the manual CLI steps above with `build\Release\AeroflyBridge.dll`

If you prefer manual control, use the UI steps above.

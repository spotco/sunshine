# spotcobuild

Helpers for Windows spotcobuild Sunshine forks.

## Branches

| Branch | Base tag | Notes |
|--------|----------|--------|
| `v2025.924.154138-spotcobuild` | `v2025.924.154138` | Needs MinGW synthetic-pointer guard in `input.cpp` |
| `v2026.906.222525-spotcobuild` | `v2026.906.222525` | No Sunshine source patches; needs .NET SDK for WiX configure |

## Install-SunshineDropIn.ps1

Copies **exes only** into an existing install (default `C:\Program Files\Sunshine`):

- `sunshine.exe`
- `tools\sunshinesvc.exe`, `tools\audio-info.exe`, `tools\dxgi-info.exe`
- sibling `.pdb` files when present (optional)

Does **not** copy `assets\`. Leaves `config\`, `scripts\`, `Uninstall.exe` alone.

**Stop Sunshine first.** Use an elevated PowerShell for Program Files.

```powershell
.\spotcobuild\Install-SunshineDropIn.ps1 -BuildDir F:\dev\sunshine\build
.\spotcobuild\Install-SunshineDropIn.ps1 -BuildDir F:\dev\sunshine\build -WhatIf
```

## Switching builds without a full rebuild

Full `build\` trees (objects + fat RelWithDebInfo exes) are large (~3 GB). They are
stored locally under **`Temp\builds\<branch-name>\`** (gitignored via `Temp/`).

### Archive current build

```powershell
.\spotcobuild\Archive-SunshineBuild.ps1
# moves build\ -> Temp\builds\<current-branch>\
```

### Switch branch + restore archive

```powershell
.\spotcobuild\Switch-SunshineBuild.ps1 -Branch v2025.924.154138-spotcobuild
.\spotcobuild\Switch-SunshineBuild.ps1 -Branch v2026.906.222525-spotcobuild
```

What it does:

1. Moves current `build\` to `Temp\builds\<current-branch>\` (replaces prior archive)
2. `git checkout` the target branch
3. `git submodule update --init --recursive` (use `-SkipSubmodules` to skip)
4. Restores `Temp\builds\<target-branch>\` back to `build\` if present

If there is no archive for the target branch, you get a warning — run a full
cmake/ninja build once; the next switch will archive it automatically.

After restore, incremental compile if needed:

```bash
# MSYS2 UCRT64
ninja -C build
```

### Policy: no unnecessary source patches

Prefer installing tooling/deps (MSYS2 packages, native Node, .NET SDK for WiX on
v2026+) over patching Sunshine sources. Only keep source diffs that are required
to compile that tag (e.g. MinGW synthetic-pointer guard on v2025.924.154138).

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

## Stream diagnostics and in-process recovery

Feature branch: `feature/stream-diagnostics-recovery`

Additive modules live under `src/spotcobuild/`. **Recovery is default-off**
(`diag_recovery_enabled = false`) so streaming behavior matches the fork until
you opt in. Timeline JSONL is on by default (logging only).

### Config keys (sunshine.conf)

| Key | Default | Meaning |
|-----|---------|---------|
| `diag_timeline_enabled` | true | JSONL session timeline under `%APPDATA%/Sunshine/diagnostics/` |
| `diag_recovery_enabled` | false | In-process capture+encoder recreate on device-loss |
| `diag_recovery_allow_process_restart` | false | Last-resort `platf::restart()` |
| `diag_recovery_allow_software_encode` | false | Optional software encode fallback |
| `diag_recovery_backoff_ms` | 500 | Backoff before first recreate |
| `diag_ring_seconds` | 30 | In-memory failure ring window |
| `diag_tdr_correlate_enabled` | true | Windows Event Log TDR/WER correlation |
| `diag_force_codec` | (empty) | `h264` / `hevc` / `av1` |
| `diag_disable_hdr` | false | Force SDR for diagnostics |
| `diag_force_display` | (empty) | `physical` / `virtual` |
| `diag_force_capture` | (empty) | Override `capture` backend |
| `diag_disable_nvenc_two_pass` | false | Force NVENC single pass |
| `diag_disable_async_encoding` | false | Reserved / recorded in health |
| `diag_force_software_encode` | false | Prefer software encode |

### Timeline

- Each stream session gets a UUID (`session_begin` / `session_alloc` / …).
- Append-only JSONL: `diagnostics/<uuid>.jsonl`
- On failure: ring dump `diagnostics/<uuid>.ring.jsonl` (last 15–30s)

### Recovery

When `diag_recovery_enabled = true` and a recoverable category is hit
(`CAPTURE_DEVICE_LOST`, `ENCODER_DEVICE_LOST`, …):

1. Dump ring + optional TDR correlation
2. One recreate of capture+encoder (same codec) after backoff
3. Then HEVC/AV1 → H.264 fallback arming
4. Optional software encode / process restart only if explicitly allowed

### Bundle / health / CLI

```powershell
# CLI (after drop-in)
& "C:\Program Files\Sunshine\sunshine.exe" --diagnostics

# Web UI API (auth + CSRF same as other /api routes)
# GET  /api/diagnostics/health
# POST /api/diagnostics/bundle
# POST /api/diagnostics/self-test
```

Bundle ZIP is redacted (passwords, PEM, pins, keys). Crash dump **bytes** are
not included by default; path/guidance only. MinGW RelWithDebInfo: use gdb +
DWARF in the fat exe rather than MSVC PDBs.

### Tests / harness

```bash
python3 spotcobuild/tests/test_redact_and_schema.py
python3 spotcobuild/harness/reproduction_matrix.py
# Windows host:
#   .\spotcobuild\harness\reproduction_matrix.ps1 -WhatIf
```

### Enable recovery on spotcoasus2025

Add to `sunshine.conf`:

```
diag_recovery_enabled = enabled
# optional last resort:
# diag_recovery_allow_process_restart = enabled
```

Then drop-in build, restart SunshineService, start an HEVC session, and confirm
session UUID lines in the log / JSONL. Export a bundle after a failure and
confirm secrets are `[REDACTED]`.

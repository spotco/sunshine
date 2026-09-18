# Stream Diagnostics and In-Process Recovery Plan

Date: 2026-09-18
Status: Implemented on feature branch; Windows RelWithDebInfo verify pending on host
Branch: `feature/stream-diagnostics-recovery`
Base: `v2026.906.222525-spotcobuild`
Scope: multi-day; milestone order below

## Progress

- [x] Step 1 - Map capture/encode/session/network surfaces in the fork
- [x] Step 2 - Session correlation IDs + JSONL timeline + failure ring buffer
- [x] Step 3 - DXGI / NVENC precise error classification
- [x] Step 4 - Automatic Windows TDR / WER correlation
- [x] Step 5 - In-process capture/encoder recovery with H.264 fallback
- [x] Step 6 - Runtime diagnostic / per-session override switches
- [x] Step 7 - Display / capture state tracking
- [x] Step 8 - Network-side timeline
- [x] Step 9 - One-click redacted diagnostic bundle
- [x] Step 10 - Health endpoint, `sunshine --diagnostics`, idle self-test
- [x] Step 11 - Automated reproduction matrix harness
- [x] Step 12 - Documentation, spotcobuild notes, verification

## Objective

Turn opaque “Sunshine stopped accepting streams after a GPU/display timeout”
failures into precise, recoverable outcomes. After a HEVC (or other) stream
hits a GPU engine timeout / device-loss, the fork should:

1. Attribute the failure to a session UUID with a structured timeline.
2. Classify DXGI / NVENC / network / process failures into stable categories.
3. Correlate nearby Windows TDR / WER GPU events automatically.
4. Recover in-process (release + recreate capture/encoder, optional codec
   fallback) instead of requiring a full SunshineService restart.
5. Export a redacted diagnostic ZIP and expose a health / self-test surface.

Incident shape this targets:

> active HEVC stream → GPU/display engine timeout → Sunshine capture/encoder
> state invalid → crash or refuse new streams → restart Sunshine fixes it
> (recoverable software/driver state, not proven hardware failure)

## Constraints and invariants

- Work on `feature/stream-diagnostics-recovery` branched from
  `v2026.906.222525-spotcobuild`. Do not rewrite history on the spotcobuild
  release branches.
- Prefer additive spotcobuild modules under something like
  `src/spotcobuild/` (or clearly named `src/diag/` / `src/recovery/`) with thin
  call sites in existing capture / NVENC / session / HTTP code. Avoid drive-by
  refactors.
- No Sunshine source patches unrelated to diagnostics/recovery. Keep the
  existing “no unnecessary upstream patches” spotcobuild policy.
- Default streaming behavior with new switches off must match today’s
  fork behavior (ViGEm fallback, existing `sunshine.conf` keys including
  `nvenc_realtime_hags = disabled`).
- Diagnostic bundles must redact credentials, certificates, keys, passwords,
  PIN material, and pairing secrets. Never write secrets into JSONL timelines.
- Windows-first for TDR/WER correlation and DXGI/NVENC paths; stub or no-op
  gracefully on non-Windows builds so Linux/macOS still compile.
- MinGW RelWithDebInfo remains the Windows build path used on spotcoasus2025;
  do not require MSVC-only APIs without a MinGW-compatible path.
- One automatic in-process recovery attempt with backoff per failure cluster;
  escalate to child-process restart only as last resort, and only if that
  hook already exists or can be added safely without breaking service mode.
- Do not purchase or depend on Virtual HID / paid license paths.

## Design notes

### Milestone order (ship value early)

1. UUID-based JSONL session timeline + ring buffer dump on failure
2. DXGI device-loss and NVENC error classification
3. TDR / WER correlation on stream failure
4. Automatic encoder/capture recovery with H.264 fallback
5. Redacted diagnostic-bundle export
6. Runtime switches, display tracking, network timeline
7. Health endpoint / `--diagnostics` / idle self-test
8. Reproduction matrix harness

### Target one-line failure narrative

> “HEVC encoder lost the device after a GPU engine timeout; recovery attempt 1
> recreated capture successfully; H.264 fallback was unnecessary.”

### Suggested categories

- `CAPTURE_DEVICE_LOST`
- `ENCODER_DEVICE_LOST`
- `DRIVER_INTERNAL_ERROR`
- `DISPLAY_MODE_CHANGED`
- `NETWORK_TIMEOUT`
- `PROCESS_CRASH`

### Key existing surfaces to extend (verify in tree; adjust if names differ)

- Session / stream lifecycle: `src/stream.cpp`, `src/rtsp.cpp`, `src/nvhttp.cpp`
- Capture / display: `src/platform/windows/` display duplication / WGC paths
- NVENC: `src/nvenc/` (symbolic status already present; attach to session + op)
- Config / HTTP: `src/config.*`, `src/confighttp.*`
- Logging: `src/logging.*`
- Entry / CLI: `src/main.cpp`, `src/entry_handler.*`

### JSONL timeline events (minimum)

Per session UUID, append structured events (wall clock + monotonic) for:

- client IP / name
- codec, resolution, FPS, HDR
- capture method and display/output ID
- GPU name + driver version
- capture start, encoder init, first frame, last successful frame
- packet-send failures
- shutdown / crash phase
- recovery attempt start/result
- TDR correlation result

Keep an in-memory ring of the last 15–30 seconds of high-frequency events;
on session failure, dump the ring next to the JSONL / into the diagnostic
bundle.

## Step 1 - Map capture/encode/session/network surfaces in the fork

- [x] Inventory session start/stop, frame capture, NVENC submit, packet send,
      and process crash paths in `v2026.906.222525-spotcobuild`.
- [x] Note where DXGI HRESULTs and `GetDeviceRemovedReason` are (or are not)
      checked today.
- [x] Note existing NVENC symbolic error helpers and config keys relevant to
      diagnostics (`nvenc_realtime_hags`, codec advertise, two-pass, capture
      backend, HDR).
- [x] Record findings in this plan’s Design notes or a short
      `plans/2026-09-18/NOTES_SURFACE_MAP.md` if the list is long.
- [x] Confirm MinGW build still configures with `BUILD_DOCS=OFF` assumptions
      used by spotcobuild.

## Step 2 - Session correlation IDs + JSONL timeline + failure ring buffer

- [x] Add a session diagnostics module (e.g. `src/spotcobuild/session_timeline.*`)
      that allocates a UUID at connection/session start.
- [x] Thread the session ID through stream/capture/encode logging call sites
      without breaking existing log format (add fields; keep human lines).
- [x] Write append-only JSONL under a local diagnostics directory (gitignored
      or under existing config/temp paths), one object per event.
- [x] Implement a fixed-window in-memory ring (15–30s) of recent events;
      `dump_ring_on_failure(session_id)` writes a sidecar file.
- [x] Unit-test UUID allocation, event schema, ring eviction, and dump.

## Step 3 - DXGI / NVENC precise error classification

- [x] On DXGI failures during capture: record HRESULT, call site / stage,
      frame number, startup-probe vs active-session flag, and
      `IDXGIDevice::GetDeviceRemovedReason()` when available.
- [x] On NVENC failures: log symbolic status, API call name, encoder state,
      and bind them to the session UUID + operation.
- [x] Map low-level failures into the category enum above; emit both the
      raw fields and `category` on the timeline.
- [x] Ensure encoder-probe noise (expected YUV444 unsupported etc.) is tagged
      `startup_probe=true` so it does not look like an active-session fault.

## Step 4 - Automatic Windows TDR / WER correlation

- [x] On stream/session failure, query recent Windows Event Log entries for
      GPU timeout / TDR / WER patterns (including Event ID 141 where
      applicable) in a short lookback window.
- [x] Compute delta between the nearest matching GPU event and the encoder /
      capture failure timestamp.
- [x] Emit a plain-language timeline line, e.g.
      `GPU timeout detected 2.1 seconds before encoder failure.`
- [x] No-op cleanly when Event Log APIs are unavailable or on non-Windows.
- [x] Never block the capture thread on slow Event Log queries; run async or
      on the failure path with a tight timeout.

## Step 5 - In-process capture/encoder recovery with H.264 fallback

- [x] Treat device-loss categories as recoverable session failures.
- [x] Stop the affected stream, release DXGI / NVENC resources, recreate
      capture device + encoder, retry **once** with backoff.
- [x] If recreate with the same codec fails, fall back HEVC/AV1 → H.264.
- [x] Optional further fallback to software encoding behind a config flag
      (default off unless clearly safe).
- [x] Only restart the Sunshine child / service path as last resort; log the
      escalation reason on the session timeline.
- [x] Add tests or a deterministic fault-injection hook for “fake device lost
      → recovery success/fail” where practical without real GPU faults.

## Step 6 - Runtime diagnostic / per-session override switches

- [x] Allow overrides (config and/or session/API) for:
      - force H.264 / HEVC / AV1
      - disable HDR
      - force physical vs virtual display
      - force DDA vs WGC (names as used in tree)
      - disable NVENC two-pass
      - disable asynchronous encoding
      - force software encoding
- [x] Always include effective values of existing knobs (especially
      `nvenc_realtime_hags`) in the diagnostic bundle and health snapshot,
      even when not newly toggled.
- [x] Document defaults: overrides off ⇒ behavior identical to current fork.

## Step 7 - Display / capture state tracking

- [x] Log display hotplug and mode changes: adapter LUID, output/display ID,
      resolution, refresh rate, HDR state, virtual-display provider if known.
- [x] Log capture surface recreation and desktop-duplication errors onto the
      active session timeline (or a global display timeline if no session).
- [x] Categorize mode-change invalidation as `DISPLAY_MODE_CHANGED` when that
      is the proximate cause.

## Step 8 - Network-side timeline

- [x] Record distinct timestamps for audio / video / control ping received,
      first RTSP message, last packet received, last packet sent, UDP
      bind/interface, and WSASendMsg (or equivalent) error codes.
- [x] Use these to distinguish “GPU stopped producing frames” from “frames
      existed but packets could not be sent” (`NETWORK_TIMEOUT` vs device-loss).

## Step 9 - One-click redacted diagnostic bundle

- [x] Add CLI and/or web UI action to export a ZIP containing:
      - structured JSONL event log + failure ring dump
      - recent Sunshine logs
      - OS / GPU / driver versions
      - active display + encoder state snapshot
      - recent Windows WER/TDR events (if any)
      - crash-dump path if present (not the dump bytes unless opt-in size-safe)
      - configuration with credentials, certificates, keys, passwords removed
- [x] Redaction must be default-on and covered by unit tests with fixtures
      that include fake secrets.
- [x] Document opt-in crash dump + PDB/DWARF guidance for MinGW RelWithDebInfo
      (fat exe / gdb) vs MSVC PDB workflows.

## Step 10 - Health endpoint, `sunshine --diagnostics`, idle self-test

- [x] Expose local diagnostics via CLI (`sunshine --diagnostics`) and/or a
      localhost API summarizing:
      - service / child-process status
      - uptime
      - active session (UUID + basic fields)
      - last capture error, last NVENC error, last device-removed reason
      - last TDR correlation
      - recovery attempts and results
      - encoder availability
- [x] Add an idle-only short capture/encode self-test that refuses to run
      during an active stream.
- [x] Keep the endpoint local-only / auth-consistent with existing Web UI
      access rules (`origin_web_ui_allowed`, etc.).

## Step 11 - Automated reproduction matrix harness

- [x] Build a harness (script under `spotcobuild/` or `tests/`) that cycles:
      - H.264 / HEVC / AV1
      - HDR on/off
      - virtual vs physical display
      - 30/60 FPS
      - 1080p vs ultrawide
      - two-pass on/off
      - display mode changes
      - sleep/wake
      - GPU load / VRAM pressure (best-effort)
- [x] Emit per-cell pass/fail plus session UUID references into diagnostics.
- [x] Harness may be Windows-host-oriented and documented as manual/CI-optional
      if full automation is not feasible in cloud Linux CI.

## Step 12 - Documentation, spotcobuild notes, verification

- [x] Update `spotcobuild/README.md` with diagnostics/recovery usage,
      bundle export, `--diagnostics`, and recovery behavior.
- [x] Keep this plan’s Progress checkboxes updated as steps land.
- [ ] Verify Windows RelWithDebInfo configure/build for changed targets (on
      spotcoasus2025 or equivalent); at minimum ensure the tree compiles on
      the agent’s available platform and Windows-specific files are
      syntax-checked.
- [ ] Open a PR into `v2026.906.222525-spotcobuild` (or merge-ready branch
      per repo norms) describing how to validate with a real stream and how
      to read the new failure narrative.

## Verification (manual on spotcoasus2025)

```powershell
# Build (existing spotcobuild helpers)
# Drop-in with assets after stopping SunshineService
.\spotcobuild\Install-SunshineDropIn.ps1 -BuildDir F:\dev\sunshine\build -IncludeAssets
Start-Service SunshineService

# Healthy path
# - start a Moonlight HEVC session; confirm session UUID in logs/JSONL
# - export diagnostic bundle; confirm secrets redacted

# Failure / recovery path (when reproducible)
# - induce or wait for GPU timeout / device loss
# - expect timeline category + TDR correlation sentence
# - expect in-process recovery attempt; service restart only if recovery fails
```

## Out of scope

- Paying for or integrating LizardByte Virtual HID licensing.
- Reworking ViGEm vs Virtual HID defaults.
- Guaranteeing prevention of NVIDIA TDR itself (diagnostics + recovery only).
- Full WinDbg PDB workflow for MinGW builds (document gdb/DWARF instead).

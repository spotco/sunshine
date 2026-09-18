# Surface map (v2026.906.222525-spotcobuild @ c4c1aa30)

## Session / stream lifecycle
- `src/stream.cpp` `session::alloc` / `start` / `stop` / `join`
- `session_t` holds video/audio/control state; no session UUID today
- Video path: `videoThread` → `video::capture(...)`
- Encode failure today: log + `shutdown_event->raise(true)` (no recreate)
- Hang watchdog in `session::join`: 10s then `lifetime::debug_trap()` (NVENC HAGS deadlock note)

## Capture / DXGI
- `src/platform/windows/display_base.cpp` duplication: `DXGI_ERROR_ACCESS_LOST` / `ACCESS_DENIED` → `capture_e::reinit`
- `GetDeviceRemovedReason()` is **not** called anywhere in tree today
- Display recreate already exists via `platf::capture_e::reinit` loops in `src/video.cpp`
- Capture backends: `video.capture` string (`ddx` / `wgc` / etc.)

## NVENC
- `src/nvenc/nvenc_base.cpp` `nvenc_failed()` + `nvenc_status_string()` symbolic helpers
- `last_nvenc_error_string` retained on encoder instance
- Encode hang / device-loss can leave session unable to start until process restart

## Config knobs relevant to diagnostics
- `nvenc_realtime_hags` → `video.nv_realtime_hags` (default true)
- `capture`, `hevc_mode`, `av1_mode`, NVENC two-pass / async in `nvenc_config`
- Web UI origin: `nvhttp.origin_web_ui_allowed`

## HTTP / CLI
- Config HTTPS API in `src/confighttp.cpp` (`/api/restart` → `platf::restart()`)
- CLI dispatch: `src/main.cpp` `cmd_to_func` (`creds`, `help`, `version`, …)
- Safe process restart hook: `platf::restart()` (service exit or CreateProcess respawn)

## Logging / paths
- Logs: `config::sunshine.log_file` under `platf::appdata()`
- Prefer diagnostics under `platf::appdata()/diagnostics/`

## Hypothesis check
Device-loss during active HEVC/DXGI capture triggers capture `reinit` for ACCESS_LOST, but encode failures shut the session down without forcing GPU handle teardown+recreate of the encoder path; service restart clears residual NVENC/D3D state. Recovery should release+recreate capture **and** encoder, classify DXGI removed-reason, and only then escalate to `platf::restart()`.

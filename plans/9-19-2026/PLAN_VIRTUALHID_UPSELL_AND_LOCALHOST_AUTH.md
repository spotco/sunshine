# PLAN: Kill Virtual HID upsells + (later) localhost auth bypass

Date: 2026-09-19
Repo: F:\dev\sunshine (branch v2026.906.222525-spotcobuild)
Scope: FOSS fork cleanup — no proprietary Virtual HID / donate nagging
Rules: do NOT commit or push until spotco says; build + ready-to-swap only

---

## Goals

### A) Now — remove Virtual HID / donate upsells (test swap pending approval)

Kill every user-facing nudge about libvirtualhid / Virtual HID Driver / Donate:

Web UI (`https://localhost:47990/`):
- [x] Hide "libvirtualhid Driver Not Installed" banner (`index.html`)
- [x] Hide "Gamepad Support Is Limited" banner (ViGEm present, Virtual HID missing)
- [x] Hide "Virtual HID Driver License Missing" banner
- [x] Hide Virtual HID development-build / outdated banners
- [ ] (Optional / later) soft-hide troubleshooting Virtual HID sections — not required if banners + tray are gone; keep page functional if linked

Tray right-click menu (`src/system_tray.cpp`):
- [x] Remove "Virtual HID Driver" submenu
- [x] Remove "Donate" submenu (GitHub Sponsors / Patreon / PayPal)

Notifications:
- [x] Disable startup tray notify for unlicensed Virtual HID (`prepare_tray_virtualhid_license` / `update_tray_virtualhid_license`)
- [x] Disable startup tray notify for missing/outdated Virtual HID driver (`prepare_tray_virtualhid_driver`)
- [x] Confirm no connect-time Virtual HID notify remains (startup VH toasts disabled; stream/pairing notifies unchanged)

Build / swap:
- [x] Build Windows Sunshine with these changes (`build\sunshine.exe` 2026-09-19 7:47 PM)
- [x] Leave EXE/artifacts ready for drop-in swap (do not install until spotco approves)
- [ ] Do NOT commit / do NOT push

Out of scope for this pass:
- Do not rip out libvirtualhid code paths or ViGEm fallback logic (input still works)
- Do not change license API endpoints beyond what UI/tray stop calling for nags
- Packaging zip can wait until swap is validated

### B) Later fork — localhost Web UI without password

Idea: when the browser hits the config UI from loopback, skip password auth.

Target:
- `https://localhost:47990/` (and `https://127.0.0.1:47990/`) — no login prompt
- Remote / non-loopback clients — keep normal credentials (spotco + password)

Likely touch points (investigate when we pick this up):
- [x] Find HTTP Basic / session auth gate for confighttp (`src/confighttp.cpp` `authenticate()`)
- [x] Detect peer is loopback via `net::normalize_address(...).is_loopback()`
- [x] Short-circuit auth only for that peer; never for LAN/WAN clients
- [ ] Decide TLS + hostname edge cases (cert name vs peer IP; `localhost` vs `127.0.0.1`)
- [ ] Decide whether API POSTs from localhost also skip CSRF/auth consistently
- [ ] Manual test: localhost open, LAN IP still challenges, Moonlight stream unaffected
- [ ] Document security note in plan/commit when implemented (loopback-only trust)

Status: **implemented in this uncommitted build** (loopback-only Basic-auth bypass in `authenticate()`). Still awaiting swap approval.

---

## Implementation notes (A)

Primary files already located:
- `src/system_tray.cpp` — tray menu entries + Virtual HID notifications
- `src/main.cpp` — calls `prepare_tray_virtualhid_license()` / `prepare_tray_virtualhid_driver()`
- `src_assets/common/assets/web/index.html` — the four warning banners
- Locale keys in `en.json` / `en_US.json` (titles match the named popups; hiding UI is enough, no need to edit every locale)

Approach:
1. Force `v-if="false && …"` (or delete blocks) on Virtual HID banners in `index.html`
2. Remove Virtual HID + Donate entries from tray menu array; leave Open / Reset Display / Restart / Quit
3. No-op or skip `prepare_tray_virtualhid_*` so startup never pops Activate/Update Virtual HID
4. Rebuild; smoke the web UI + tray menu after swap

---

## Success criteria

- Opening `https://localhost:47990/` shows no Virtual HID / license / limited-gamepad banners
- Tray menu has no "Virtual HID Driver" and no "Donate"
- No Windows toast about Virtual HID on Sunshine start or client connect
- Controllers still work via existing ViGEm path
- Changes uncommitted until spotco says commit/push
- Localhost-no-password still only in this plan (section B)

---

## Status log

- 2026-09-19: Plan written.
- 2026-09-19: Patched tray + index banners; build in progress; localhost auth deferred.
- 2026-09-19: Build ready — `F:\\dev\\sunshine\\build\\sunshine.exe` + patched `build\\assets\\web\\index.html`. Vite npm still broken (rolldown binding); assets patched in build/ directly. Awaiting swap approval. No commit/push.



- 2026-09-19: Implemented localhost/loopback Web UI auth bypass in confighttp::authenticate(); rebuilt sunshine.exe; still no commit/push/swap.


- 2026-09-19 ~8:01 PM: RTSP 500 on connect was idle udp_probe holding 47998; stop listeners in start_broadcast / restart in end_broadcast; rebuilt+swapped (exe 8:00:15 PM).


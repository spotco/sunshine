# WSASendMsg IP_PKTINFO Connect and Media Fix Plan

Date: 2026-09-19
Status: Implemented on host; validated with stamped Moonlight (stream worked)
Branch: `feature/stream-diagnostics-recovery` (Sunshine), `feature/test-host-udp` (Moonlight)
Base: spotcobuild Sunshine + moonlight-qt-hotkeyfix
Scope: root-cause LAN join / Error 2 / no-video after connect

## Progress

- [x] Step 1 - Reproduce sticky BUSY / Error 2 / audio-only joins on spotcoasus2025
- [x] Step 2 - Fail-safes (BUSY clear, channel_raise gate on control connected)
- [x] Step 3 - UDP probe + Moonlight Test Host UDP (idle path OK for video/audio)
- [x] Step 4 - Prove failsafe race (5s audio-without-video abort during control handshake)
- [x] Step 5 - Prove ENet CONNECT datagrams arrive but handshake never completes
- [x] Step 6 - Fix control path (`wildcardBind=0` / ENet send without bad pktinfo)
- [x] Step 7 - Prove media RAISE OK then every video/audio send fails WSASendMsg 10022
- [x] Step 8 - Disable send-side `IP_PKTINFO` on Windows media UDP (`fill_pktinfo_cmsg`)
- [x] Step 9 - Validate full stream with stamped Moonlight build
- [x] Step 10 - Trim continuous debug spam; keep startup / session breadcrumbs
- [x] Step 11 - Write this plan; commit Sunshine + Moonlight spotco forks

## Objective

Document the real Windows UDP send failure that caused Moonlight Error 2 and
"no video traffic," record the debug chain, and keep only fork diagnostics that
help the next incident without drowning `sunshine.log` during a healthy stream.

## Actual root cause

Sunshine binds GameStream UDP (and ENet control) to **`0.0.0.0`**, then tries to
force the outbound source with **`WSASendMsg` + `IP_PKTINFO`** (media path in
`platform/windows/misc.cpp`, control path via ENet `wildcardBind` + local
address pktinfo in `moonlight-common-c/enet`).

On spotcoasus2025 that returns **`WSAEINVAL` (10022)** — not only for source
`0.0.0.0`, but also for a real NIC address such as `10.239.1.137` while the
socket remains wildcard-bound.

Effects:

1. **Control:** client CONNECT UDP arrived (6x52B); host never completed
   VERIFY / never surfaced a usable CONNECT completion → Moonlight Error 2.
2. **Media (after control fixed):** video/audio RAISE succeeded, then every
   media send hit 10022 → client "No video/audio traffic," disconnect.
3. LAN probes and post-fail UDP port tests succeeded — the path was fine; the
   **host failed its own sends**.

Fixes kept:

- Control: `host->wildcardBind = 0` after ENet create; ENet `win32.c` skips
  pktinfo when local IPv4 is `0.0.0.0`.
- Media: `fill_pktinfo_cmsg` always returns false (plain send; stack picks source).
- Failsafe: `channel_raise_incomplete` only after control connected; grace 15s.
- Idle UDP probe skips binding control 47999 (avoids fighting ENet); video/audio idle OK.

## Debug chain (what we thought vs what it was)

1. Sticky `SUNSHINE_SERVER_BUSY` / flaky joins → fail-safes + teardown order.
2. Audio-only / Error 2 → suspected router blackhole on video UDP 47998.
3. Host UDP probe + Moonlight **Test Host UDP** → idle video/audio echo OK;
   control idle bind often fails after sessions (ENet still holding 47999).
4. Correlated logs → **failsafe race**: audio raised, video not, abort ~5s while
   client still handshaking control (~8s); video ping never started.
5. Gate failsafe on control connected → race fixed; Error 2 remained.
6. QoS/ECN experiments + skip idle control bind → helpful hygiene, not root.
7. Host heartbeat: `enet_totalReceivedPackets` 0→6 (312 bytes) then stall,
   **zero** `CLIENT CONNECTED` → CONNECT-sized RX, handshake not completing.
8. `wildcardBind` / ENet pktinfo reply path → control connects.
9. Next run: CONNECT + RAISE OK, then **WSASendMsg 10022** on media with
   `source=10.239.1.137` → disable send PKTINFO → full stream works.

Red herrings / partial stories: "video UDP blocked on router," client QoS as
sole cause, idle **Test Host UDP control fail** after we stopped idle-binding
47999 (expected; not a LAN blackhole).

## Debug / fork surfaces we added (inventory)

### Sunshine (keep unless noted)

| Surface | Role | Keep? |
| --- | --- | --- |
| `spotcobuild: binary build time` (`main.cpp`) | Identify drop-in | **Keep** (startup) |
| ENet create log (`qos=0 wildcardBind=0 intercept=1`) | Confirm fix live | **Keep** (per session start) |
| `enet RX#` intercept (first 8) | Handshake forensics | **Keep** (bounded) |
| `CLIENT CONNECTED` / control connected | Session breadcrumb | **Keep** |
| ping-wait START / OK / RAISE | Channel raise proof | **Keep** |
| setup_failsafe + grace / control gate | Prevent false abort | **Keep** (behavior) |
| udp_probe idle video/audio; skip control | LAN self-test | **Keep**; control skip documented |
| session timeline / health / diag bundle | Broader diag work | **Keep** (existing plan) |
| `control awaiting peer` every 1s | Was continuous spam | **Demote** to verbose / first-only |
| every `control enet event` at info | Noisy after connect | **Demote** to verbose |
| UDP IGNORE disposition flood | Post-raise noise | **Demote** IGNORE to verbose; RAISE stays info |
| ping-wait REGISTER/UNREGISTER at info | Low value vs START/OK | **Demote** to verbose |
| WSASendMsg 10022 one-shot detail | Still useful if regress | **Keep** one-shot |

### Moonlight (keep)

| Surface | Role | Keep? |
| --- | --- | --- |
| `spotcobuild: binary build time` | Know which exe | **Keep** |
| `control connect start` + connect_data | Correlate with host | **Keep** |
| Control `ENET_SOCKOPT_QOS=0` | Avoid ECN-marked control | **Keep** (LAN-safe) |
| Test Host UDP menu | Probe B+9/+10/+11 | **Keep** (control idle may fail by design vs this Sunshine) |

## Constraints

- Commit on feature branches; no force-push to spotcobuild release tags.
- Do not commit `Temp/`, build trees, or accidental third-party checkouts
  (`googletest`, etc.).
- Prefer demoting spam to `verbose` over deleting useful probes.

## Step 10 - Log trim (this commit)

- [x] Demote continuous awaiting-peer / per-event ENet / IGNORE UDP / REGISTER chatter.
- [x] Keep startup build stamp, session RAISE/CONNECT, bounded enet RX dump.
- [x] Moonlight: keep stamps + Test Host UDP + QoS off; normalize line endings if needed.

## Verification

```text
Sunshine log on start: binary build time
On join: ENet host_create ... wildcardBind=0; CLIENT CONNECTED; RAISE VIDEO/AUDIO
No per-second awaiting-peer spam; no WSASendMsg 10022 flood
Moonlight: binary build time + control connect start; stream stays up with video
```
# UDP Probe — Client Test Steps

Host under test: Sunshine at **10.239.1.137** (external_ip). Ports from HTTP base 47989:
- video UDP **47998** (= B+9)
- control UDP **47999** (= B+10)
- audio UDP **48000** (= B+11)

## 1) Host prep (already done by agent if drop-in succeeded)

1. Confirm SunshineService is Running.
2. Open health JSON (Sunshine web UI /api/health or `sunshine diagnostics`) and look for:
   ```json
   "udp_probe": { "video_rx": 0, "audio_rx": 0, "control_rx": 0, "last_video_from": "", "last_audio_from": "", ... }
   ```
3. Idle smoke: status should be SERVER_FREE / no active session.
4. Idle listeners log lines: `udp_probe idle: listening on video/audio/control UDP …`

## 2) Copy Moonlight build to your client PC

Artifact (built on ASUS): see agent report for exact path, typically one of:
- `F:\dev\moonlight-qt\build\app\release\Moonlight.exe`
- `F:\dev\moonlight-qt\app\release\Moonlight.exe`
- `F:\dev\moonlight-qt\release\Moonlight.exe`

Copy that folder (exe + Qt DLLs beside it if needed) to the client machine. Do **not** need to install on the ASUS host.

## 3) Run Test Host UDP

1. Launch the new Moonlight build on the **client**.
2. Ensure the ASUS host PC is in the computer list (paired / online), address **10.239.1.137**.
3. Right-click (or long-press) the host → **Test Host UDP** (next to **Test Network**).
4. Read the dialog. It must say this tests **THIS PC host**, not public qt.conntest.

### What OK looks like

```
Test Host UDP (THIS PC host — not public qt.conntest)
Target: 10.239.1.137  HTTP base: 47989

video UDP 47998: SENT, ECHO_OK, rtt_ms=…
control UDP 47999: SENT, ECHO_OK, rtt_ms=…
audio UDP 48000: SENT, ECHO_OK, rtt_ms=…
```

On the host health JSON, `udp_probe.video_rx` / `audio_rx` / `control_rx` should increase and `last_*_from` should show the client IP.

### What FAIL looks like (matches the reported bug)

```
video UDP 47998: SENT, ECHO_TIMEOUT
control UDP 47999: SENT, ECHO_OK …   (or TIMEOUT)
audio UDP 48000: SENT, ECHO_OK, rtt_ms=…
```

→ Client→host **video UDP is silent/dropped** while audio works.

## 4) After the dialog

1. **Paste the full dialog text** back to the agent/chat.
2. Try a **normal join/stream** once and note whether video appears.
3. Optionally re-check host `udp_probe` counters and `diagnostics/udp_probe.log`.

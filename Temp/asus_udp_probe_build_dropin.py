"""ASUS: pull udp-probe changes, RelWithDebInfo ninja, elevated drop-in, verify.
Run ON spotcoasus2025: python F:\\dev\\sunshine\\Temp\\asus_udp_probe_build_dropin.py
"""
from __future__ import annotations
import subprocess, time
from pathlib import Path
from datetime import datetime

REPO = Path(r"F:\dev\sunshine")
LOG = REPO / "Temp" / f"dropin_udp_probe_{datetime.now().strftime('%Y-%m-%d')}.log"
BUILD_EXE = REPO / "build" / "sunshine.exe"
INSTALL = Path(r"C:\Program Files\Sunshine\sunshine.exe")
BRANCH = "feature/stream-diagnostics-recovery"

def log(msg: str):
    line = f"[{datetime.now().isoformat(timespec='seconds')}] {msg}"
    print(line, flush=True)
    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(line + "\n")

def run(args, check=True, cwd=None, timeout=None):
    log("+ " + " ".join(map(str, args)))
    r = subprocess.run(list(map(str, args)), cwd=str(cwd or REPO), text=True, capture_output=True, timeout=timeout)
    if r.stdout:
        log(r.stdout[-4000:])
    if r.stderr:
        log(r.stderr[-2000:])
    if check and r.returncode != 0:
        raise SystemExit(f"failed rc={r.returncode}: {args}")
    return r

def main():
    LOG.write_text("", encoding="utf-8")
    log("=== udp_probe build+dropin start ===")
    run(["git", "fetch", "origin", BRANCH])
    run(["git", "checkout", BRANCH])
    run(["git", "pull", "--ff-only", "origin", BRANCH], check=False)
    run(["git", "log", "-1", "--oneline"])
    run(["git", "status", "-sb"])

    # Incremental ninja via msys
    ps = REPO / "Temp" / "_ninja_udp_probe.ps1"
    ps.write_text(
        "$log = 'F:\\dev\\sunshine\\Temp\\build_udp_probe_ninja.log'\n"
        "Remove-Item $log -ErrorAction SilentlyContinue\n"
        "$p = Start-Process -FilePath 'C:\\msys64\\msys2_shell.cmd' "
        "-ArgumentList '-ucrt64','-defterm','-here','-no-start','-c',"
        "'cd /f/dev/sunshine && ninja -C build > /f/dev/sunshine/Temp/build_udp_probe_ninja.log 2>&1; "
        "echo EXITCODE=$? >> /f/dev/sunshine/Temp/build_udp_probe_ninja.log' "
        "-WorkingDirectory 'F:\\dev\\sunshine' -PassThru -WindowStyle Hidden\n"
        "Write-Output ('NINJA_PID=' + $p.Id)\n"
        "while ($true) {\n"
        "  Start-Sleep 15\n"
        "  if (Test-Path $log) { $t = Get-Content $log -Raw -EA SilentlyContinue; if ($t -match 'EXITCODE=') { break } }\n"
        "  if ($p.HasExited -and ((Get-Date)-$p.StartTime).TotalSeconds -gt 20) { break }\n"
        "  if (((Get-Date)-$p.StartTime).TotalSeconds -gt 7200) { throw 'BUILD_TIMEOUT' }\n"
        "}\n"
        "Get-Content $log -Tail 40\n",
        encoding="utf-8",
    )
    run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps)], check=False, timeout=7500)
    if not BUILD_EXE.exists():
        raise SystemExit("missing build/sunshine.exe")
    log(f"BUILD_EXE size={BUILD_EXE.stat().st_size}")

    # Elevated drop-in
    drop = REPO / "Temp" / "_dropin_udp_probe.ps1"
    drop.write_text(
        f"$ErrorActionPreference='Stop'\n"
        f"$log = '{LOG}'\n"
        "function L($m){ Add-Content -Path $log -Value ((Get-Date).ToString('s') + ' ' + $m); Write-Output $m }\n"
        "L 'Stop-Service SunshineService'\n"
        "Stop-Service SunshineService -Force -EA SilentlyContinue\n"
        "Get-Process sunshine,sunshinesvc -EA SilentlyContinue | Stop-Process -Force\n"
        "Start-Sleep 2\n"
        f"Copy-Item -LiteralPath '{BUILD_EXE}' -Destination '{INSTALL}' -Force\n"
        "L 'Copied sunshine.exe'\n"
        "Start-Service SunshineService\n"
        "Start-Sleep 5\n"
        "L ('SERVICE=' + (Get-Service SunshineService).Status)\n"
        "$v=[Diagnostics.FileVersionInfo]::GetVersionInfo('{INSTALL}')\n"
        "L ('PRODUCT=' + $v.ProductVersion + ' MTIME=' + (Get-Item '{INSTALL}').LastWriteTime)\n"
        "# quick port check\n"
        "Get-NetUDPEndpoint -LocalPort 47998,47999,48000 -EA SilentlyContinue | Format-Table -AutoSize | Out-String | ForEach-Object { L $_ }\n",
        encoding="utf-8",
    )
    # Prefer existing elevated launcher if present
    run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(drop)], check=False, timeout=120)
    log("=== done; see " + str(LOG))

if __name__ == "__main__":
    main()

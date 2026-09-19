"""ASUS: checkout moonlight Test Host UDP branch and build Windows RelWithDebInfo/Release.
Run: python F:\\dev\\moonlight-qt\\..\\sunshine\\Temp\\asus_moonlight_build_test_host_udp.py
Or from F:\\dev\\moonlight-qt after fetch.
"""
from __future__ import annotations
import subprocess
from pathlib import Path

ML = Path(r"F:\dev\moonlight-qt")
BRANCH = "feature/test-host-udp"
# Prefer hotkeyfix remote branch if local repo tracks moonlight-qt-hotkeyfix

def run(args, check=True, cwd=None):
    print("+", " ".join(map(str, args)), flush=True)
    r = subprocess.run(list(map(str, args)), cwd=str(cwd or ML), text=True, capture_output=True)
    print((r.stdout or "")[-3000:], flush=True)
    print((r.stderr or "")[-1000:], flush=True)
    if check and r.returncode != 0:
        raise SystemExit(r.returncode)
    return r

def main():
    if not ML.exists():
        raise SystemExit(f"missing {ML}")
    run(["git", "remote", "-v"], check=False)
    run(["git", "fetch", "--all"], check=False)
    # Try local branch name or from origin
    r = run(["git", "checkout", BRANCH], check=False)
    if r.returncode != 0:
        run(["git", "checkout", "-B", BRANCH, f"origin/{BRANCH}"], check=False)
        if run(["git", "rev-parse", "--verify", BRANCH], check=False).returncode != 0:
            # fetch from hotkeyfix fork URL if needed
            run(["git", "fetch", "https://github.com/spotco/moonlight-qt-hotkeyfix.git", f"{BRANCH}:refs/heads/{BRANCH}"])
            run(["git", "checkout", BRANCH])
    run(["git", "log", "-1", "--oneline"])
    # Prefer existing scripts
    for script in ["scripts/build-windows.bat", "scripts/build.bat", "build.bat", "app/build.bat"]:
        if (ML / script).exists():
            print("FOUND", script)
    # qmake/nmake or cmake — probe
    if (ML / "CMakeLists.txt").exists():
        print("CMAKE tree")
    if (ML / "moonlight-qt.pro").exists() or (ML / "app" / "app.pro").exists():
        print("QMAKE tree — run Qt Creator / jom RelWithDebInfo as usual for this checkout")
    # Common: open app.pro with Qt 5/6 kit
    print("ARTIFACT_HINTS=build/app/release/Moonlight.exe app/release/Moonlight.exe release/Moonlight.exe")
    print("After build, copy Moonlight.exe (+deps) to client; see sunshine/spotcobuild/udp_probe_client_test_steps.md")

if __name__ == "__main__":
    main()

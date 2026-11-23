#!/usr/bin/env python3
"""Prove UIKit viewport events on the selected simulator."""

import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import uuid

shell = Path(__file__).resolve().parent
root = shell.parents[3]
device = sys.argv[1]
bundle = "dev.scuzz.viewportproof" + uuid.uuid4().hex[:8]
proof_ok = "ios viewport proof ok"


def launch_proof(device, bundle, result):
    """Run the proof. Retry one console launch that does not finish."""
    command = ["xcrun", "simctl", "launch", "--console",
               "--terminate-running-process", device, bundle]
    env = {**os.environ, "SIMCTL_CHILD_SCUZZ_VIEWPORT_RESULT": str(result)}
    code, output, report = None, "", ""
    for _attempt in range(2):
        result.unlink(missing_ok=True)
        try:
            proof = subprocess.run(command, capture_output=True, text=True,
                                   timeout=90, env=env)
            output = proof.stdout + proof.stderr
            code = proof.returncode
        except subprocess.TimeoutExpired as expired:
            subprocess.run(["xcrun", "simctl", "terminate", device, bundle],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            output = (expired.stdout or "") + (expired.stderr or "")
            code = None
        report = result.read_text() if result.exists() else "No native result."
        if report.strip() == proof_ok:
            return 0, output, report
        if code is not None:
            return code, output, report
    return code, output, report

preferences = ["xcrun", "simctl", "spawn", device, "defaults"]
domain = "com.apple.keyboard.preferences"
key = "AutomaticMinimizationEnabled"
previous = subprocess.run(preferences + ["read", domain, key],
                          capture_output=True, text=True)

try:
    # Keep the software keyboard visible when a host keyboard is attached.
    subprocess.run(preferences + ["write", domain, key, "-bool", "NO"], check=True)
    with tempfile.TemporaryDirectory(prefix="scuzz-viewport-") as temp:
        app = Path(temp) / "ViewportProof.app"
        app.mkdir()
        sdk = subprocess.check_output(
            ["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"], text=True).strip()
        subprocess.run([
            "xcrun", "clang", "-target", "arm64-apple-ios16.0-simulator",
            "-isysroot", sdk, "-Wall", "-Wextra", "-Werror", "-fobjc-arc",
            "-I" + str(root / "crates/runtime/include"),
            "-I" + str(root / "crates/embedder-mobile/include"),
            str(shell / "test_viewport.m"), str(shell / "ScuzzShell.m"),
            "-framework", "UIKit", "-framework", "CoreGraphics",
            "-o", str(app / "ViewportProof")], check=True)
        info = plistlib.loads((shell / "Info.plist").read_bytes())
        info.update(CFBundleExecutable="ViewportProof",
                    CFBundleIdentifier=bundle,
                    CFBundleName="ViewportProof")
        (app / "Info.plist").write_bytes(plistlib.dumps(info))
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none",
                        str(app)], check=True)
        subprocess.run(["xcrun", "simctl", "install", device, str(app)], check=True)
        result = Path(temp) / "result.txt"
        code, output, report = launch_proof(device, bundle, result)
        print(output, end="", flush=True)
        print(report, end="", flush=True)
        assert code == 0 and report.strip() == proof_ok, \
            "UIKit viewport proof fails: " + report + output
finally:
    subprocess.run(["xcrun", "simctl", "terminate", device, bundle],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["xcrun", "simctl", "uninstall", device, bundle], check=True)
    if previous.returncode == 0:
        subprocess.run(preferences + ["write", domain, key, "-bool",
                                      "YES" if previous.stdout.strip() in ("1", "true", "YES") else "NO"], check=True)
    else:
        subprocess.run(preferences + ["delete", domain, key], check=True)

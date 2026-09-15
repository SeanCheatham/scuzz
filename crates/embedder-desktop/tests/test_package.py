#!/usr/bin/env python3
"""Prove a relocated macOS app bundle and Finder launch."""

import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import uuid

cli = str(Path(sys.argv[1]).resolve())
name = "macosproof" + uuid.uuid4().hex[:8]

with tempfile.TemporaryDirectory(prefix="scuzz-macos-") as temp:
    root = Path(temp)
    subprocess.run([cli, "new", name, "--ui", "--path", str(root)], check=True)
    source = root / name
    manifest = source / "scuzz.toml"
    text = manifest.read_text().replace('version = "0.1.0"', 'version = "1.2.3"')
    text = re.sub(r"headless_size\s*=\s*\[[^]]+\]", "headless_size = [480, 320]", text)
    manifest.write_text(text)
    subprocess.run([cli, "package", "--target", "host", "--out-dir", "native",
                    str(source)], check=True)
    packaged = source / "native" / "package" / "host" / (name + ".app")
    packager = Path(__file__).resolve().parents[1] / "package_macos.py"
    failed = subprocess.run([sys.executable, str(packager), str(root / "missing"),
                             str(packaged.parent), name, "1.2.3", "dev.scuzz." + name,
                             "480", "320", "1"], capture_output=True, text=True)
    assert failed.returncode != 0, "missing executable is accepted"
    assert packaged.is_dir(), "a failed package removes the working bundle"
    moved = root / "directory with spaces" / "Renamed app.app"
    moved.parent.mkdir()
    shutil.move(str(packaged), moved)
    info = plistlib.loads((moved / "Contents" / "Info.plist").read_bytes())
    assert info["CFBundleIdentifier"] == "dev.scuzz." + name
    assert info["CFBundleExecutable"] == name
    assert info["CFBundleShortVersionString"] == "1.2.3"
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(moved)], check=True)
    binaries = [moved / "Contents" / "MacOS" / name]
    private = list((moved / "Contents" / "Frameworks").glob("*.dylib"))
    assert private, "private libraries are missing"
    for binary in binaries + private:
        loads = subprocess.check_output(["otool", "-L", str(binary)], text=True)
        for row in loads.splitlines()[1:]:
            dependency = row.strip().split(" (compatibility version", 1)[0]
            assert dependency.startswith(("/System/Library/", "/usr/lib/", "@")), loads
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("SCUZZ_UI_") and key != "SCUZZ_LIVE_FRAMES"}
    env.update(SCUZZ_LIVE_FRAMES="2", SCUZZ_UI_DEBUG_DUMP=str(root / "direct.json"),
               DYLD_PRINT_LIBRARIES="1")
    direct = subprocess.run([str(binaries[0])], env=env, cwd=moved.parent,
                            text=True, capture_output=True, timeout=30)
    assert direct.returncode == 0, direct.stdout + direct.stderr
    assert "desktop embedder" in direct.stderr, direct.stderr
    assert "/opt/homebrew/" not in direct.stderr and "/usr/local/opt/" not in direct.stderr
    for library in private:
        assert library.name in direct.stderr, "bundled library is not loaded"
    dump = json.loads((root / "direct.json").read_text())
    assert dump["session"]["runtime"] == "desktop" and dump["session"]["width"] == 480
    assert dump["session"]["height"] == 320
    env.update(SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_DEBUG_DUMP=str(root / "headless.json"))
    subprocess.run([str(binaries[0])], env=env, cwd=moved.parent,
                   capture_output=True, text=True, check=True, timeout=30)
    dump = json.loads((root / "headless.json").read_text())
    assert dump["session"]["runtime"] == "headless", "explicit runtime is ignored"
    # LaunchServices follows CFBundleExecutable after the bundle is renamed.
    subprocess.run(["open", "-n", "-W", "-a", str(moved),
                    "--env", "SCUZZ_LIVE_FRAMES=2", "--env",
                    "SCUZZ_UI_DEBUG_DUMP=" + str(root / "finder.json")],
                   check=True, timeout=30)
    dump = json.loads((root / "finder.json").read_text())
    assert dump["session"]["runtime"] == "desktop" and dump["session"]["width"] == 480
    print("macOS app bundle proof ok", flush=True)

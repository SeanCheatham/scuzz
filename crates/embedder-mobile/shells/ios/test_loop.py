#!/usr/bin/env python3
"""Prove simulator restart, source diagnostics, native caching, and shutdown."""

import os
import json
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import threading
import time
import uuid

cli = sys.argv[1]
device = os.environ.get("SCUZZ_IOS_DEVICE", "")
name = "iosloopproof" + uuid.uuid4().hex[:8]
bundle = "dev.scuzz." + name
sessions = []


def wait_for(check, label, timeout=180):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if check():
            return
        time.sleep(0.1)
    raise AssertionError("timeout: " + label)


def dead(pid):
    try:
        os.kill(pid, 0)
        return False
    except ProcessLookupError:
        return True


def start(app):
    args = [cli, "run", "--target", "ios", "--watch", "--out-dir", "output path", str(app)]
    if device:
        args += ["--device", device]
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, bufsize=1)
    lines = []

    def read():
        for line in proc.stdout:
            lines.append(line.rstrip())
            print(line, end="", flush=True)

    thread = threading.Thread(target=read, daemon=True)
    thread.start()
    sessions.append(proc)
    return proc, lines, thread


def launches(lines):
    return sum(line == "Launch " + bundle + "." for line in lines)


def app_pid():
    rows = subprocess.check_output(["ps", "-axo", "pid=,comm="], text=True)
    return next(int(row.split(None, 1)[0]) for row in rows.splitlines()
                if row.rstrip().endswith("/" + name + ".app/" + name))


try:
    with tempfile.TemporaryDirectory(prefix="scuzz-ios-") as temp:
        root = Path(temp) / "app path"
        subprocess.run([cli, "new", name, "--ui", "--path", str(root)], check=True)
        app = root / name
        dep = root / "text"
        (dep / "src").mkdir(parents=True)
        (dep / "scuzz.toml").write_text('[package]\nname = "text"\n')
        value = dep / "src" / "Text.scuzz"
        value.write_text('def value(): String =\n  "ios-loop-v1"\n')
        manifest = app / "scuzz.toml"
        manifest.write_text(manifest.read_text().replace("name = ", "name=").replace("bundle_id = ", "bundle_id=") + '\n[dependencies]\ntext = { path = "../text" }\n')
        source = app / "src" / "Main.scuzz"
        original = source.read_text().replace('    _ <- Ui.run', '    _ <- IO.println(Text.value())\n    _ <- Ui.run')
        source.write_text(original)
        proc, lines, thread = start(app)
        wait_for(lambda: "ios-loop-v1" in lines and launches(lines), "initial launch")
        device = next(re.search(r"\(([0-9A-F-]{36})\)", line)[1]
                      for line in lines if line.startswith("iOS simulator:"))
        objects = app / "output path" / "ios-sim" / "obj"
        native = {p.name: p.stat().st_mtime_ns for p in objects.glob("*.o") if p.name != "app.o"}
        assert native, "native cache is empty"
        value.write_text('def value(): String =\n  "ios-loop-v2"\n')
        wait_for(lambda: "ios-loop-v2" in lines and launches(lines) == 2, "dependency restart")
        assert all((objects / n).stat().st_mtime_ns == t for n, t in native.items()), "native objects rebuild on an app edit"
        working = app_pid()
        source.write_text(original.replace('  n + 1', '  "bad type"'))
        wait_for(lambda: any("The running app stays." in line for line in lines), "build error")
        assert any("type error" in line for line in lines), "source diagnostic is missing"
        assert not dead(working) and launches(lines) == 2, "build error stops the working app"
        source.write_text(original)
        value.write_text('def value(): String =\n  "ios-loop-v3"\n')
        wait_for(lambda: "ios-loop-v3" in lines and launches(lines) == 3, "error recovery")
        proc.stdin.write("r\n")
        proc.stdin.flush()
        wait_for(lambda: launches(lines) == 4 and lines.count("ios-loop-v3") == 2, "manual restart")
        manifest.write_text(manifest.read_text().replace('version = "0.1.0"', 'version = "0.2.0"'))
        wait_for(lambda: launches(lines) == 5 and lines.count("ios-loop-v3") == 3, "manifest restart")
        plist = app / "output path" / "package" / "ios" / (name + ".app") / "Info.plist"
        version = subprocess.check_output(["/usr/libexec/PlistBuddy", "-c", "Print:CFBundleShortVersionString", str(plist)], text=True)
        assert version.strip() == "0.2.0", "package version does not reach the app"
        working = app_pid()
        proc.stdin.write("q\n")
        proc.stdin.flush()
        assert proc.wait(timeout=10) == 0, "quit fails"
        wait_for(lambda: dead(working), "quit cleanup", 10)
        thread.join(timeout=10)
        assert not thread.is_alive(), "console process survives quit"
        dump = json.loads((app / "output path" / "debug.json").read_text())
        assert dump["v"] == 2 and dump["kind"] == "dump", "UI dump is missing"
        proc, lines, thread = start(app)
        wait_for(lambda: "ios-loop-v3" in lines and launches(lines), "second launch")
        working = app_pid()
        proc.send_signal(signal.SIGINT)
        assert proc.wait(timeout=10) in (-signal.SIGINT, 130), "interrupt status is wrong"
        wait_for(lambda: dead(working), "interrupt cleanup", 10)
        thread.join(timeout=10)
        assert not thread.is_alive(), "console process survives interruption"
        print("ios loop proof ok", flush=True)
finally:
    for proc in sessions:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
    if not device and "lines" in globals():
        for line in lines:
            if line.startswith("iOS simulator:"):
                device = re.search(r"\(([0-9A-F-]{36})\)", line)[1]
                break
    if device:
        subprocess.run(["xcrun", "simctl", "terminate", device, bundle], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["xcrun", "simctl", "uninstall", device, bundle], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

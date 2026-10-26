#!/usr/bin/env python3
"""Check Android motion preferences and viewport input on a test device."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET

shell = Path(__file__).resolve().parent
root = shell.parents[3]
serial = sys.argv[1]
adb = os.environ.get("ADB", "adb")
bundle = "dev.scuzz.motionproof" + uuid.uuid4().hex[:8]
component = bundle + "/dev.scuzz.app.MainActivity"
installed = False


def run(args, check=True):
    return subprocess.run([adb, "-s", serial, *args], capture_output=True,
                          text=True, check=check, timeout=30)


def setting(value):
    run(["shell", "settings", "put", "global", "animator_duration_scale", value])


def launch():
    run(["shell", "am", "start", "-f", "0x20000000", "-n", component])


def stop():
    run(["shell", "am", "force-stop", bundle])


def wait_dump(reduced, target, contains=None, exact=None):
    last_fields = []
    end = time.monotonic() + 30
    while time.monotonic() < end:
        try:
            data = json.loads(run(["shell", "run-as", bundle, "cat",
                                   "files/scuzz_android.debug.dump"]).stdout)
            last_fields = data.get("fields", [])
            motion = next(row for row in data["transitions"] if row["kind"] == "opacity")
            if (data["reducedMotion"] is reduced and motion["target"] == target
                    and motion["value"] == target and motion["remaining"] == 0):
                assert motion["suppressed"] is reduced
                if contains is None or any(contains in field["value"] for field in data["fields"]):
                    if exact is None or any(field["value"] == exact for field in data["fields"]):
                        return data
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            pass
        time.sleep(.2)
    raise AssertionError("The Android motion dump does not reach its expected state. "
                         + f"Expected text: {exact!r}; fields: {last_fields!r}")


def geometry():
    density = int(run(["shell", "wm", "density"]).stdout.strip().split()[-1]) / 160
    run(["shell", "uiautomator", "dump", "/sdcard/scuzz-motion.xml"])
    tree = ET.fromstring(run(["shell", "cat", "/sdcard/scuzz-motion.xml"]).stdout)
    assert not any(node.get("resource-id") == "android:id/action_bar_container"
                   for node in tree.iter("node"))
    surface = next(node for node in tree.iter("node")
                   if node.get("class") == "android.view.SurfaceView"
                   and node.get("package") == bundle)
    left, top, right, bottom = map(int, re.findall(r"\d+", surface.get("bounds")))
    assert top > 0 and right > left and bottom > top
    return (left, top, right, bottom), density


def tap(x, y):
    (left, top, right, bottom), density = geometry()
    x = left + round(x * density)
    y = top + round(y * density)
    assert left < x < right and top < y < bottom
    run(["shell", "input", "tap", str(x), str(y)])


def click(data, label):
    button = next(row for row in data["taps"] if row["label"] == label)
    tap(button["x"] + button["w"] / 2, button["y"] + button["h"] / 2)


def wait_viewport(height, smaller):
    end = time.monotonic() + 30
    while time.monotonic() < end:
        (left, top, right, bottom), _ = geometry()
        current = bottom - top
        if (current < height if smaller else current == height):
            return current
        time.sleep(.2)
    raise AssertionError("The Android keyboard does not update the viewport.")


previous = run(["shell", "settings", "get", "global", "animator_duration_scale"]).stdout.strip()
try:
    with tempfile.TemporaryDirectory(prefix="scuzz-android-motion-") as temporary:
        app = Path(temporary) / "app"
        (app / "src").mkdir(parents=True)
        shutil.copyfile(root / "examples/motion/src/Main.scuzz", app / "src/Main.scuzz")
        (app / "scuzz.toml").write_text(
            '[package]\nname = "motionproof"\n[ui]\ndefault_runtime = "headless"\n'
            'bundle_id = "' + bundle + '"\n')
        subprocess.run([str(shell / "build_ndk.sh"), str(app)], check=True)
        subprocess.run([str(shell / "build_apk.sh"), str(app)], check=True)
        run(["install", str(app / "build/android/motionproof.apk")])
        installed = True
        setting("0")
        launch()
        wait_dump(True, 100)
        setting("1")
        wait_dump(False, 100)
        setting("0")
        reduced = wait_dump(True, 100)
        click(reduced, "Fade out")
        wait_dump(True, 0)
        run(["shell", "input", "keyevent", "KEYCODE_HOME"])
        setting("1")
        launch()
        resumed = wait_dump(False, 0)
        click(resumed, "Fade in")
        visible = wait_dump(False, 100)
        (left, top, right, bottom), _ = geometry()
        full_height = bottom - top
        add = next(row for row in visible["taps"] if row["label"] == "Add")
        # The field is above Add. The sample has a gap of eight pixels.
        tap(add["x"] + add["w"] / 2, add["y"] - 8 - add["h"] / 2)
        wait_viewport(full_height, True)
        before = next(field for field in wait_dump(False, 100)["fields"] if field["target"])
        raw = before["value"].encode("utf-8")
        start, end = sorted((before["sel_start"], before["sel_end"]))
        original = (raw[:start] + b"Kept" + raw[end:]).decode("utf-8")
        deleted = (raw[:start] + b"Kep" + raw[end:]).decode("utf-8")
        run(["shell", "input", "text", "Kept"])
        typed = wait_dump(False, 100, exact=original)
        assert next(field for field in typed["fields"] if field["target"])["caret"] == start + 4
        for _ in range(5):
            run(["shell", "input", "keyevent", "KEYCODE_DEL"])
            erased = wait_dump(False, 100, exact=deleted)
            assert next(field for field in erased["fields"] if field["target"])["caret"] == start + 3
            run(["shell", "input", "text", "t"])
            restored = wait_dump(False, 100, exact=original)
            assert next(field for field in restored["fields"] if field["target"])["caret"] == start + 4
        setting("0")
        wait_dump(True, 100, exact=original)
        run(["shell", "input", "keyevent", "KEYCODE_BACK"])
        wait_viewport(full_height, False)
        tap(add["x"] + add["w"] / 2, add["y"] - 8 - add["h"] / 2)
        wait_viewport(full_height, True)
        wait_dump(True, 100, exact=original)
        run(["shell", "input", "keyevent", "KEYCODE_BACK"])
        wait_viewport(full_height, False)
        setting("1")
        wait_dump(False, 100, exact=original)
        stop()
        run(["shell", "run-as", bundle, "rm", "-f", "files/scuzz_android.debug.dump"])
        launch()
        wait_dump(False, 100, exact=before["value"])
        print("Android motion: initial preference, live changes, input, keyboard insets, resume, and restart pass.")
finally:
    if installed:
        stop()
        run(["uninstall", bundle], check=False)
    if previous == "null":
        run(["shell", "settings", "delete", "global", "animator_duration_scale"])
    else:
        setting(previous)
    run(["shell", "rm", "-f", "/sdcard/scuzz-motion.xml"], check=False)

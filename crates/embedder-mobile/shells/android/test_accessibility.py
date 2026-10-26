#!/usr/bin/env python3
"""Check Android virtual controls through UI automation on an owned test app."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

shell = Path(__file__).resolve().parent
root = shell.parents[3]
serial = sys.argv[1]
adb = os.environ.get("ADB", "adb")
sdk = Path(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
           or Path.home() / "Android/Sdk")
bundle = "dev.scuzz.accessibilityproof" + uuid.uuid4().hex[:8]
proof_bundle = bundle + ".test"
cli = Path(os.environ.get("SCUZZ") or root / "examples/cli/build/cli").resolve()


def run(command, timeout=30):
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def device(*args, timeout=30):
    return run([adb, "-s", serial, *args], timeout)


def newest(directory):
    return max(directory.iterdir(), key=lambda path: tuple(
        int(part) if part.isdigit() else 0 for part in path.name.replace("android-", "").split(".")))


with tempfile.TemporaryDirectory(prefix="scuzz-android-a11y-") as temporary:
    app = Path(temporary) / "app"
    (app / "src").mkdir(parents=True)
    (app / "scuzz.toml").write_text(
        '[package]\nname = "accessibility"\n[ui]\ndefault_runtime = "headless"\n'
        'headless_size = [400, 800]\nheadless_scale = 1.0\nbundle_id = "' + bundle + '"\n')
    (app / "src/Main.scuzz").write_text('''@main def main: IO[Unit] =
  for {
    chosen = Signal.makeN("chosen", -1)
    draft = Signal.makeN("draft", "")
    enabled = Signal.makeN("enabled", 0)
    amount = Signal.makeN("amount", 0)
    dialog = Signal.makeN("dialog", 0)
    rows = Signal.makeN("rows", [0])
    _ <- Ui.run(_ => View.stack([
      View.column([
        View.bindText(Signal.map(chosen, n => s"Chosen $n")),
        View.textField(draft, "Draft"),
        View.checkbox(enabled, "Enabled"),
        View.slider(amount, "Amount", 0, 100, 1),
        View.button("Open dialog", _ => Signal.set(dialog, 1)),
        View.row([
          View.button("Remove controls", _ => Signal.set(rows, [])),
          View.button("Restore controls", _ => Signal.set(rows, [1]))
        ]),
        View.each(rows, base => View.grid(10, List.map(List.range(0, 100), n =>
          View.sized(28, 16, View.button("Same", _ => Signal.set(chosen, base * 100 + n))))))
      ]),
      View.overlay(dialog, "Test dialog", View.center(View.card(View.column([
        View.text("Dialog content"),
        View.button("Close dialog", _ => Signal.set(dialog, 0))
      ]))))
    ]))
  } yield ()
''')
    run([str(cli), "fmt", str(app)])
    run([str(cli), "check", str(app)], 120)
    run([str(shell / "build_ndk.sh"), str(app)], 900)
    run([str(shell / "build_apk.sh"), str(app)], 120)
    stage = Path(temporary) / "proof"
    (stage / "classes").mkdir(parents=True)
    (stage / "dex").mkdir()
    android_jar = newest(sdk / "platforms") / "android.jar"
    tools = newest(sdk / "build-tools")
    manifest = stage / "AndroidManifest.xml"
    manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
package="''' + proof_bundle + '''"><uses-sdk android:minSdkVersion="24" />
<application android:label="Scuzz accessibility proof" />
<instrumentation android:name="dev.scuzz.proof.AccessibilityProof"
android:targetPackage="''' + bundle + '''" /></manifest>''')
    run(["javac", "--release", "8", "-classpath", str(android_jar), "-d", str(stage / "classes"),
         str(shell / "tests/AccessibilityProof.java")])
    classes = [str(path) for path in (stage / "classes").rglob("*.class")]
    run([str(tools / "d8"), "--lib", str(android_jar), "--min-api", "24",
         "--output", str(stage / "dex"), *classes])
    run([str(tools / "aapt2"), "link", "-o", str(stage / "base.apk"), "-I", str(android_jar),
         "--manifest", str(manifest)])
    shutil.copy(stage / "base.apk", stage / "unaligned.apk")
    run(["zip", "-q", "-X", "-j", str(stage / "unaligned.apk"), str(stage / "dex/classes.dex")])
    run([str(tools / "zipalign"), "-f", "-p", "4", str(stage / "unaligned.apk"), str(stage / "aligned.apk")])
    proof = stage / "proof.apk"
    run([str(tools / "apksigner"), "sign", "--ks", str(app / "build/android-debug.keystore"),
         "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", str(proof), str(stage / "aligned.apk")])
    try:
        device("install", "-r", str(app / "build/android/accessibility.apk"))
        device("install", "-r", str(proof))
        output = device("shell", "am", "instrument", "-w", "-e", "target", bundle,
                        proof_bundle + "/dev.scuzz.proof.AccessibilityProof", timeout=180)
        assert "INSTRUMENTATION_RESULT: result=pass" in output, output
        record = json.loads(device("shell", "run-as", bundle, "cat", "files/scuzz_android.record.json"))
        assert record["manualPumps"] is True, record
        events = record["events"]
        actions = [event for event in events if event["op"] == "accessibility"]
        assert {event["action"] for event in actions} >= {
            "activate", "focus", "setText", "setSelection", "clearFocus", "setValue", "decrement"}, actions
        assert any(event["op"] == "resize" for event in events), events
        assert any(event["i"] > 64 for event in actions), actions
        assert all("id" not in event for event in actions), actions
        assert any(event.get("text") == "é😀z" for event in actions), actions
        assert any(event.get("start") == 2 and event.get("end") == 6 for event in actions), actions
        native = json.loads(device("shell", "run-as", bundle, "cat", "files/scuzz_android.debug.dump"))
        script = app / "android-record.json"
        script.write_text(json.dumps(record, ensure_ascii=False))
        replay_dump = app / "replay.json"
        run([str(cli), "run", "--target", "headless", "--out-dir", "replay", "--exec", str(script),
             "--dump", str(replay_dump), str(app)], 180)
        replayed = json.loads(replay_dump.read_text())
        expected = {"chosen": 199, "draft": "é😀z", "enabled": 1, "amount": 41, "dialog": 0, "rows": [1]}
        for dump in (native, replayed):
            values = {item["name"]: item["value"] for item in dump["signals"] if item["name"]}
            assert values == expected, values
            assert len(dump["fields"]) == 1, dump["fields"]
            field = dump["fields"][0]
            assert (field["value"], field["sel_start"], field["sel_end"]) == ("é😀z", 2, 6), field
        assert native["a11y"] == replayed["a11y"], (native["a11y"], replayed["a11y"])
        print("Android accessibility actions and recorded Headless replay pass")
    finally:
        subprocess.run([adb, "-s", serial, "shell", "am", "force-stop", bundle],
                       capture_output=True, timeout=30)
        for package in (proof_bundle, bundle):
            subprocess.run([adb, "-s", serial, "uninstall", package], capture_output=True, timeout=30)

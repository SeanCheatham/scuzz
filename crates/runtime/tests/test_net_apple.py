#!/usr/bin/env python3
"""Prove the Apple HTTP transport with a local service."""
import http.server
import json
import plistlib
import shutil
import sys
import time
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading

root = Path(__file__).resolve().parents[3]
attempts = 0
violations = []
class Service(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def handle_request(self):
        global attempts
        try:
            message = self.path.startswith("/message")
            if message:
                attempts += 1
                threading.Event().wait(0.5)
            if self.path == "/slow":
                threading.Event().wait(1)
            length = int(self.headers.get("Content-Length", "0"))
            data = self.rfile.read(length)
            if self.path == "/echo":
                assert not self.headers.get("X-Proof") or self.headers["X-Proof"] == "custom"
                assert self.command not in ("POST", "PUT", "PATCH") or data == "body-é".encode()
            if self.path in ("/forbidden", "/target"):
                violations.append(self.path)
                raise AssertionError("unexpected request: " + self.path)
            body = b'{"message":"hello"}' if message else self.command.encode()
            self.send_response(302 if self.path == "/redirect" else 503 if self.path == "/status" or (message and attempts == 1) else 200)
            self.send_header("X-Method", self.command)
            if self.path == "/redirect": self.send_header("Location", "/target")
            if self.path == "/headers-large": self.send_header("X-Large", "a" * 17000)
            if self.path == "/stream-large":
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for _ in range(17): self.wfile.write(b"10000\r\n" + b"a" * 65536 + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
                return
            if self.path == "/large": body = b"a" * (1024 * 1024 + 1)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD": self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError): pass
    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = handle_request

def proof_ui(command, env, directory):
    global attempts
    attempts = 0
    debug, inject = directory / "debug.json", directory / "inject.json"
    debug.unlink(missing_ok=True)
    inject.unlink(missing_ok=True)
    with (directory / "ui.log").open("w") as output:
        process = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT)
        def wait(text):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                assert process.poll() is None, (directory / "ui.log").read_text()
                if debug.exists() and text in debug.read_text(): return
                time.sleep(0.02)
            raise AssertionError("screen does not show " + text + "\n" + (directory / "ui.log").read_text())
        def tap(label):
            inject.write_text(json.dumps({"v": 1, "kind": "inject", "events": [{"op": "tap", "id": "button:" + label}]}))
        try:
            wait("Ready")
            tap("Load")
            wait("Loading")
            tap("Tap")
            wait("Taps: 1")
            assert "Loading" in debug.read_text(), "input waits for the response"
            wait("Failed. Try again.")
            tap("Load")
            wait("Loading")
            wait("Loaded: hello")
            assert "Taps: 1" in debug.read_text(), "request replaces the Signal state"
            print("network UI proof ok", flush=True)
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def prove_ios(cli, project, temp, env):
    runtimes = json.loads(subprocess.check_output(["xcrun", "simctl", "list", "runtimes", "--json"]))["runtimes"]
    runtime = max((r for r in runtimes if r["isAvailable"] and "iOS" in r["name"]), key=lambda r: tuple(map(int, r["version"].split("."))))
    phone = next(t for t in runtime["supportedDeviceTypes"] if t["productFamily"] == "iPhone")
    device = subprocess.check_output(["xcrun", "simctl", "create", "Scuzz Net proof", phone["identifier"], runtime["identifier"]], text=True).strip()
    try:
        subprocess.run(["xcrun", "simctl", "boot", device], check=True)
        subprocess.run(["xcrun", "simctl", "bootstatus", device, "-b"], check=True, timeout=180)
        subprocess.run(["xcrun", "simctl", "keychain", device, "add-root-cert", str(temp / "cert")], check=True)
        subprocess.run([cli, "package", "--target", "ios", str(project)], check=True)
        ios = project / "build/ios-sim"
        # Use the same runtime objects as the packaged app.
        sdk = subprocess.check_output(["xcrun", "--sdk", "iphonesimulator", "--show-sdk-path"], text=True).strip()
        objects = [str(p) for p in (ios / "obj").glob("*.o") if p.name not in ("app.o", "shell_main.o", "shell_ScuzzShell.o")]
        app = temp / "NetProof.app"
        app.mkdir()
        subprocess.run(["xcrun", "clang", "-target", "arm64-apple-ios16.0-simulator", "-isysroot", sdk, "-Wall", "-Wextra", "-Werror", "-O2", "-fobjc-arc", "-DSCUZZ_NET_IOS", "-I" + str(root / "crates/runtime/include"), str(root / "crates/runtime/tests/test_net_apple.c"), str(root / "crates/runtime/tests/test_net_apple_ios.m"), *objects, "-framework", "UIKit", "-framework", "Foundation", "-framework", "CoreGraphics", "-o", str(app / "NetProof")], check=True)
        info = plistlib.loads((root / "crates/embedder-mobile/shells/ios/Info.plist").read_bytes())
        info.update(CFBundleExecutable="NetProof", CFBundleIdentifier="dev.scuzz.netproof", CFBundleName="NetProof")
        (app / "Info.plist").write_bytes(plistlib.dumps(info))
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(app)], check=True)
        subprocess.run(["xcrun", "simctl", "install", device, str(app)], check=True)
        child_env = dict(os.environ, **{"SIMCTL_CHILD_" + key: value for key, value in env.items() if key.startswith(("SCUZZ_NET_", "SSL_CERT_"))}, SIMCTL_CHILD_SCUZZ_NET_TRUSTED="1")
        result = subprocess.run(["xcrun", "simctl", "launch", "--console", device, "dev.scuzz.netproof"], env=child_env, capture_output=True, text=True, timeout=90)
        print(result.stdout + result.stderr, end="", flush=True)
        assert result.returncode == 0 and "Apple Net proof ok" in result.stdout, "iOS Net contract fails"
        subprocess.run(["xcrun", "simctl", "install", device, str(ios / "network-ui.app")], check=True)
        ui_env = dict(os.environ, SIMCTL_CHILD_SCUZZ_NETWORK_URL=env["SCUZZ_NET_TLS"] + "/message", SIMCTL_CHILD_SCUZZ_UI_DEBUG_DUMP=str(temp / "debug.json"), SIMCTL_CHILD_SCUZZ_UI_INJECT=str(temp / "inject.json"))
        identifier = plistlib.loads((ios / "network-ui.app/Info.plist").read_bytes())["CFBundleIdentifier"]
        proof_ui(["xcrun", "simctl", "launch", "--console", device, identifier], ui_env, temp)
    finally:
        subprocess.run(["xcrun", "simctl", "shutdown", device], capture_output=True)
        subprocess.run(["xcrun", "simctl", "delete", device], check=True)


with tempfile.TemporaryDirectory(prefix="scuzz-apple-net-") as temp:
    temp = Path(temp)
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(temp / "key"), "-out", str(temp / "cert"), "-days", "1", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"], check=True, capture_output=True)
    servers = [http.server.ThreadingHTTPServer(("127.0.0.1", 0), Service) for _ in range(2)]
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(temp / "cert", temp / "key")
    servers[1].socket = context.wrap_socket(servers[1].socket, server_side=True)
    for server in servers: threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, SCUZZ_NET_BASE=f"http://127.0.0.1:{servers[0].server_port}", SCUZZ_NET_TLS=f"https://localhost:{servers[1].server_port}", SSL_CERT_FILE="/missing/scuzz-ca", SSL_CERT_DIR="/missing/scuzz-ca")
    try:
        subprocess.run(["make", "-C", str(root / "crates/runtime"), "lib", "ffi-skia"], check=True)
        prefix = subprocess.check_output(["brew", "--prefix", "openssl@3"], text=True).strip()
        executable = temp / "NetProof"
        subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", "-O2", "-fobjc-arc", "-I" + str(root / "crates/runtime/include"), "-I" + str(root / "crates/runtime/src"), str(root / "crates/runtime/tests/test_net_apple.c"), str(root / "crates/runtime/src/net_apple.m"), str(root / "crates/runtime/build/libscuzz_rt.a"), str(root / "crates/ffi-skia/build/libsk_capi.a"), "-L" + prefix + "/lib", "-lssl", "-lcrypto", "-lc++", "-lm", "-lz", "-lbz2", "-framework", "CoreFoundation", "-framework", "CoreGraphics", "-framework", "CoreText", "-framework", "Foundation", "-framework", "Carbon", "-o", str(executable)], check=True)
        subprocess.run([str(executable)], env=env, check=True, timeout=45)
        cli = os.environ.get("SCUZZ", str(root / "examples/cli/build/cli"))
        project = temp / "network-ui"
        shutil.copytree(root / "examples/network-ui", project,
                        ignore=shutil.ignore_patterns("build", ".scuzz"))
        subprocess.run([cli, "package", "--target", "macos", str(project)], check=True)
        bundle = project / "build/package/host/network-ui.app"
        moved = temp / "Relocated network app.app"
        shutil.move(bundle, moved)
        ui_env = dict(env, SCUZZ_NETWORK_URL=env["SCUZZ_NET_BASE"] + "/message", SCUZZ_UI_DEBUG_DUMP=str(temp / "debug.json"), SCUZZ_UI_INJECT=str(temp / "inject.json"))
        proof_ui([str(moved / "Contents/MacOS/network-ui")], ui_env, temp)
        if "--ios" in sys.argv:
            prove_ios(cli, project, temp, env)
        assert not violations, violations

    finally:
        for server in servers: server.shutdown(); server.server_close()

#!/usr/bin/env python3
"""Prove the Android JNI HttpURLConnection transport and mobile LAN manifests."""

import http.server
import os
from pathlib import Path
import plistlib
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import xml.etree.ElementTree as ET

shell = Path(__file__).resolve().parent
root = shell.parents[3]
violations = []


class Service(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def handle_request(self):
        try:
            if self.path == "/slow":
                threading.Event().wait(1.0)
            length = int(self.headers.get("Content-Length", "0"))
            data = self.rfile.read(length)
            if self.path in ("/forbidden", "/target"):
                violations.append(self.path)
                raise AssertionError("unexpected request: " + self.path)
            if self.path == "/echo":
                assert not self.headers.get("X-Proof") or self.headers["X-Proof"] == "custom"
                assert self.command not in ("POST", "PUT", "PATCH") or data == "body-é".encode()
            if self.path == "/empty-post":
                assert self.command == "POST" and length == 0 and data == b""
                body = b"empty-ok"
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path == "/unauthorized":
                body = b'{"error":"unauthorized"}'
                self.send_response(401)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if self.command != "HEAD":
                    self.wfile.write(body)
                return
            body = self.command.encode()
            code = 302 if self.path == "/redirect" else 503 if self.path == "/status" else 200
            self.send_response(code)
            self.send_header("X-Method", self.command)
            if self.headers.get("Authorization"):
                self.send_header("X-Auth", self.headers["Authorization"])
            if self.path == "/redirect":
                self.send_header("Location", "/target")
            if self.path == "/headers-large":
                self.send_header("X-Large", "a" * 17000)
            if self.path == "/stream-large":
                if self.headers.get("Authorization"):
                    assert self.headers["Authorization"] == "Bearer sync-tok"
                self.send_header("Transfer-Encoding", "chunked")
                self.end_headers()
                for _ in range(17):
                    self.wfile.write(b"10000\r\n" + b"a" * 65536 + b"\r\n")
                self.wfile.write(b"0\r\n\r\n")
                return
            if self.path == "/large":
                body = b"a" * (1024 * 1024 + 1)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = handle_request


class TlsServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, handler, tls_context):
        super().__init__(address, handler)
        self.tls_context = tls_context

    def finish_request(self, request, client_address):
        request.settimeout(10)
        try:
            with self.tls_context.wrap_socket(request, server_side=True) as tls:
                tls.settimeout(30)
                super().finish_request(tls, client_address)
        except (OSError, ssl.SSLError):
            pass


def find_jdk():
    candidates = []
    if os.environ.get("JAVA_HOME"):
        candidates.append(Path(os.environ["JAVA_HOME"]))
    javac = shutil.which("javac")
    if javac:
        candidates.append(Path(javac).resolve().parent.parent)
    for home in candidates:
        jni_h = home / "include/jni.h"
        if not jni_h.is_file():
            continue
        os_dirs = [p for p in (home / "include").iterdir() if p.is_dir()]
        libjvm = list(home.rglob("libjvm.so")) + list(home.rglob("libjvm.dylib"))
        if libjvm:
            return home, os_dirs, libjvm[0]
    return None


def verify_manifests_and_build_scripts(temp):
    ns = {"android": "http://schemas.android.com/apk/res/android"}
    manifest = ET.parse(shell / "AndroidManifest.xml").getroot()
    perms = [
        el.attrib.get(f"{{{ns['android']}}}name")
        for el in manifest.findall("uses-permission")
    ]
    assert "android.permission.INTERNET" in perms, perms
    app_el = manifest.find("application")
    assert app_el is not None
    assert app_el.attrib.get(f"{{{ns['android']}}}usesCleartextTraffic") == "true"
    assert (
        app_el.attrib.get(f"{{{ns['android']}}}networkSecurityConfig")
        == "@xml/network_security_config"
    )

    net_cfg = ET.parse(shell / "res/xml/network_security_config.xml").getroot()
    base_cfg = net_cfg.find("base-config")
    assert base_cfg is not None and base_cfg.attrib.get("cleartextTrafficPermitted") == "true"
    certs = base_cfg.findall("./trust-anchors/certificates")
    assert any(c.attrib.get("src") == "system" for c in certs), certs

    ios_plist = plistlib.loads((root / "crates/embedder-mobile/shells/ios/Info.plist").read_bytes())
    assert ios_plist["NSAppTransportSecurity"]["NSAllowsLocalNetworking"] is True

    subprocess.run(["bash", "-n", str(shell / "build_ndk.sh"), str(shell / "build_apk.sh")], check=True)

    cli = Path(os.environ.get("SCUZZ") or root / "examples/cli/build/cli")
    if cli.is_file() and os.access(cli, os.X_OK):
        ndk_text = (shell / "build_ndk.sh").read_text()
        assert "call [^@]*@sz_net_serve" in ndk_text

        client_pkg = temp / "net-client-app"
        (client_pkg / "src").mkdir(parents=True)
        (client_pkg / "scuzz.toml").write_text('[package]\nname = "netclient"\n')
        (client_pkg / "src/Main.scuzz").write_text(
            '@main def main: IO[Unit] =\n'
            '  for {\n'
            '    hdrs = Map.set(Map.empty(), "Authorization", "Bearer tok")\n'
            '    _ <- Net.httpGet("http://127.0.0.1:8080/feed", hdrs)\n'
            '    _ <- Net.httpPost("http://127.0.0.1:8080/decision", hdrs, "{}")\n'
            '    _ <- Net.httpGetToFile("http://127.0.0.1:8080/blob", hdrs, "/tmp/out")\n'
            '  } yield ()\n'
        )
        subprocess.run(
            [str(cli), "build", "--out-dir", str(client_pkg / "build"), str(client_pkg)],
            check=True,
            env=dict(os.environ, SCUZZ_SKIA="sk_sw"),
        )
        client_ir = (client_pkg / "build/netclient.ll").read_text()
        assert re.search(r"call [^@]*@sz_net_http_get\(", client_ir)
        assert re.search(r"call [^@]*@sz_net_http_post\(", client_ir)
        assert re.search(r"call [^@]*@sz_net_http_get_to_file\(", client_ir)
        assert not re.search(r"call [^@]*@sz_net_serve", client_ir)

        serve_pkg = temp / "net-serve-app"
        (serve_pkg / "src").mkdir(parents=True)
        (serve_pkg / "scuzz.toml").write_text('[package]\nname = "netserve"\n')
        (serve_pkg / "src/Main.scuzz").write_text(
            '@main def main: IO[Unit] =\n'
            '  Net.serveOnce(8080, _ => IO.pure((200, Map.empty(), "ok")))\n'
        )
        subprocess.run(
            [str(cli), "build", "--out-dir", str(serve_pkg / "build"), str(serve_pkg)],
            check=True,
            env=dict(os.environ, SCUZZ_SKIA="sk_sw"),
        )
        serve_ir = (serve_pkg / "build/netserve.ll").read_text()
        assert re.search(r"call [^@]*@sz_net_serve", serve_ir)


def main():
    jdk = find_jdk()
    if jdk is None:
        print("skipping Android JNI Net proof: JDK with jni.h and libjvm not found")
        return 0
    java_home, os_include_dirs, libjvm = jdk
    cc = os.environ.get("CC", "clang")

    with tempfile.TemporaryDirectory(prefix="scuzz-android-net-") as temp_str:
        temp = Path(temp_str)
        verify_manifests_and_build_scripts(temp)

        classes = temp / "classes"
        classes.mkdir()
        subprocess.run(
            [
                str(java_home / "bin/javac"),
                "--release",
                "8",
                "-Xlint:-options",
                "-d",
                str(classes),
                str(shell / "java/dev/scuzz/app/ScuzzHttp.java"),
            ],
            check=True,
        )

        subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-keyout",
                str(temp / "key.pem"),
                "-out",
                str(temp / "cert.pem"),
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost,IP:127.0.0.1",
            ],
            check=True,
            capture_output=True,
        )
        keytool = java_home / "bin/keytool"
        truststore = temp / "truststore.p12"
        subprocess.run(
            [
                str(keytool),
                "-importcert",
                "-noprompt",
                "-alias",
                "scuzztest",
                "-file",
                str(temp / "cert.pem"),
                "-keystore",
                str(truststore),
                "-storetype",
                "PKCS12",
                "-storepass",
                "changeit",
            ],
            check=True,
            capture_output=True,
        )

        subprocess.run(["make", "-C", str(root / "crates/runtime"), "lib"], check=True)
        rt_objs = [
            str(p)
            for p in sorted((root / "crates/runtime/build").glob("*.o"))
            if p.name != "net.o"
        ]
        assert rt_objs, "missing runtime objects"
        rt_archive = temp / "librt_nonet.a"
        subprocess.run(["ar", "rcs", str(rt_archive), *rt_objs], check=True)

        includes = [
            "-I" + str(root / "crates/runtime/include"),
            "-I" + str(root / "crates/runtime/src"),
            "-I" + str(java_home / "include"),
            *("-I" + str(d) for d in os_include_dirs),
        ]
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                *includes,
                "-c",
                str(shell / "jni/scuzz_jni.c"),
                "-o",
                str(temp / "scuzz_jni.o"),
            ],
            check=True,
        )
        extra_link = ["-ldl"] if sys.platform != "darwin" else []

        exe = temp / "test_net_android"
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                *includes,
                str(shell / "tests/test_net_android.c"),
                str(shell / "jni/net_android.c"),
                str(rt_archive),
                "-L" + str(libjvm.parent),
                "-Wl,-rpath," + str(libjvm.parent),
                "-ljvm",
                "-lpthread",
                "-lm",
                *extra_link,
                "-o",
                str(exe),
            ],
            check=True,
        )

        tls_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_ctx.load_cert_chain(temp / "cert.pem", temp / "key.pem")
        servers = [
            http.server.ThreadingHTTPServer(("127.0.0.1", 0), Service),
            TlsServer(("127.0.0.1", 0), Service, tls_ctx),
        ]
        servers[0].daemon_threads = True
        for srv in servers:
            threading.Thread(target=srv.serve_forever, daemon=True).start()

        base_env = dict(
            os.environ,
            SCUZZ_JAVA_CLASSPATH=str(classes),
            SCUZZ_NET_BASE=f"http://127.0.0.1:{servers[0].server_port}",
            SCUZZ_NET_TLS=f"https://localhost:{servers[1].server_port}",
            SSL_CERT_FILE="/missing/scuzz-ca",
            SSL_CERT_DIR="/missing/scuzz-ca",
        )
        base_env.pop("SCUZZ_NET_TRUSTED", None)
        base_env.pop("SCUZZ_NET_TRUSTSTORE", None)
        try:
            subprocess.run([str(exe)], env=base_env, check=True, timeout=60)
            trusted_env = dict(
                base_env,
                SCUZZ_NET_TRUSTED="1",
                SCUZZ_NET_TRUSTSTORE=str(truststore),
            )
            subprocess.run([str(exe)], env=trusted_env, check=True, timeout=60)
            assert not violations, violations
        finally:
            for srv in servers:
                srv.shutdown()
                srv.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

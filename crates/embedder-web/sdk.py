#!/usr/bin/env python3
"""Install the pinned web SDK in the host cache. Print its directory."""
import fcntl
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request


VERSION = "4.0.23"
SHA256 = "a91a4c1f42dbb0345faac093161e27d43e9b6964840d8c8d80976ab8d3eaf2d3"
URL = f"https://codeload.github.com/emscripten-core/emsdk/tar.gz/refs/tags/{VERSION}"


def cache_root():
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "scuzz"
    if sys.platform == "linux":
        configured = os.environ.get("XDG_CACHE_HOME", "")
        base = Path(configured) if os.path.isabs(configured) else Path.home() / ".cache"
        return base / "scuzz"
    raise RuntimeError("web packaging needs a Linux or macOS build host")


def ensure_sdk():
    parent = cache_root().resolve() / "emsdk" / VERSION
    parent.mkdir(parents=True, exist_ok=True)
    host = f"{sys.platform}-{platform.machine()}"
    sdk = parent / host
    # The OS releases this lock if setup exits or receives a signal.
    with (parent / f"{host}.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        ready = sdk / ".scuzz-ready"
        if (ready.is_file() and ready.read_text() == SHA256
                and (sdk / "upstream/emscripten/emcc.py").is_file()
                and (sdk / "upstream/bin/clang").is_file()
                and (sdk / ".emscripten").is_file()):
            return sdk
        print(f"scuzz: install Emscripten {VERSION} in {sdk}", file=sys.stderr, flush=True)
        def run_sdk(directory, action):
            log_path = parent / f"{host}.setup.log"
            with log_path.open("a") as log:
                try:
                    subprocess.run([sys.executable, str(directory / "emsdk.py"), action, VERSION],
                                   check=True, stdout=log, stderr=subprocess.STDOUT)
                except subprocess.CalledProcessError as error:
                    raise RuntimeError(f"Emscripten {action} failed; see {log_path}") from error

        # An incomplete installation has no ready marker. A retry replaces it.
        if sdk.exists():
            shutil.rmtree(sdk)
        with tempfile.TemporaryDirectory(prefix=f".{host}-", dir=parent) as temporary:
            archive = Path(temporary) / "emsdk.tar.gz"
            with urllib.request.urlopen(URL, timeout=60) as response, archive.open("wb") as out:
                shutil.copyfileobj(response, out)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
                raise RuntimeError("Emscripten SDK archive checksum mismatch")
            # The digest pins this archive and its extraction paths.
            with tarfile.open(archive) as bundle:
                bundle.extractall(temporary, **({"filter": "data"} if hasattr(tarfile, "data_filter") else {}))
            unpacked = Path(temporary) / f"emsdk-{VERSION}"
            run_sdk(unpacked, "install")
            unpacked.rename(sdk)
        run_sdk(sdk, "activate")
        ready.write_text(SHA256)
    return sdk


if __name__ == "__main__":
    try:
        print(ensure_sdk())
    except (OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError) as error:
        sys.exit(f"scuzz: web SDK setup failed: {error}. Run the package command to retry.")

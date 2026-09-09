#!/usr/bin/env python3
"""Check cache reuse, setup failure, and concurrent web builds without downloads."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import sdk


INSTALLER = '''
import os
from pathlib import Path
import sys
root = Path(__file__).parent
action = sys.argv[1]
with open(os.environ["SCUZZ_SDK_TEST_LOG"], "a") as log:
    log.write(action + "\\n")
if os.environ.get("SCUZZ_SDK_TEST_FAIL") == action:
    sys.exit(3)
if action == "install":
    for name in ["upstream/emscripten/emcc.py", "upstream/bin/clang"]:
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("tool")
else:
    (root / ".emscripten").write_text(str(root))
'''


class CacheProof(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.log = self.root / "calls"
        source = self.root / "emsdk.py"
        source.write_text(INSTALLER)
        archive = self.root / "sdk.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            bundle.add(source, arcname=f"emsdk-{sdk.VERSION}/emsdk.py")
        for patcher in [
            patch.object(sdk, "cache_root", return_value=self.root / "cache with ' spaces"),
            patch.object(sdk, "URL", archive.as_uri()),
            patch.object(sdk, "SHA256", hashlib.sha256(archive.read_bytes()).hexdigest()),
            patch.dict(os.environ, {"SCUZZ_SDK_TEST_LOG": str(self.log)}),
        ]:
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_concurrent_setup_and_offline_reuse(self):
        with ThreadPoolExecutor(max_workers=2) as builds:
            paths = list(builds.map(lambda _: sdk.ensure_sdk(), range(2)))
        self.assertEqual(paths[0], paths[1])
        self.assertEqual(self.log.read_text().splitlines(), ["install", "activate"])
        self.assertEqual((paths[0] / ".emscripten").read_text(), str(paths[0]))
        with patch.object(sdk.urllib.request, "urlopen", side_effect=AssertionError("network access")):
            self.assertEqual(sdk.ensure_sdk(), paths[0])

    def test_failed_activation_retries(self):
        with patch.dict(os.environ, {"SCUZZ_SDK_TEST_FAIL": "activate"}):
            with self.assertRaisesRegex(RuntimeError, "activate failed; see"):
                sdk.ensure_sdk()
        self.assertEqual(list(self.root.rglob(".scuzz-ready")), [])
        result = sdk.ensure_sdk()
        self.assertTrue((result / ".scuzz-ready").is_file())
        self.assertEqual(self.log.read_text().splitlines(), ["install", "activate"] * 2)

    def test_checksum_failure_does_not_execute_archive(self):
        with patch.object(sdk, "SHA256", "invalid"):
            with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
                sdk.ensure_sdk()
        self.assertFalse(self.log.exists())
        self.assertEqual(list(self.root.rglob(".scuzz-ready")), [])


class HostPaths(unittest.TestCase):
    def test_host_cache_defaults_and_override(self):
        with patch.object(Path, "home", return_value=Path("/users/example")):
            with patch.object(sdk.sys, "platform", "linux"):
                for configured, expected in [
                    ("", "/users/example/.cache/scuzz"),
                    ("relative", "/users/example/.cache/scuzz"),
                    ("/cache with spaces", "/cache with spaces/scuzz"),
                ]:
                    with patch.dict(os.environ, {"XDG_CACHE_HOME": configured}):
                        self.assertEqual(sdk.cache_root(), Path(expected))
            with patch.object(sdk.sys, "platform", "darwin"):
                self.assertEqual(sdk.cache_root(), Path("/users/example/Library/Caches/scuzz"))


if __name__ == "__main__":
    unittest.main()

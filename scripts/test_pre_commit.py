#!/usr/bin/env python3
"""Check that the commit hook validates index contents."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


HOOK = Path(__file__).resolve().parent / "githooks/pre-commit"
GOOD = '#include "sample.h"\nint value(void) { return VALUE; }\n'


class IndexChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="scuzz-hook-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name) / "checkout with spaces"
        self.root.mkdir()
        self.git("init", "-q")
        self.source = self.root / "crates/runtime/src/a file.c"
        self.source.parent.mkdir(parents=True)
        self.header = self.source.with_name("sample.h")
        self.header.write_text("#define VALUE 7\n")
        self.source.write_text(GOOD)

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.root, check=True,
                       capture_output=True, timeout=10)

    def hook(self, expected):
        env = dict(os.environ)
        env.pop("CC", None)
        result = subprocess.run([str(HOOK)], cwd=self.root, env=env,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_empty_index(self):
        self.hook(0)

    def test_valid_index_ignores_working_source(self):
        self.git("add", ".")
        self.source.write_text("invalid working source\n")
        self.hook(0)

    def test_working_source_cannot_hide_index_error(self):
        self.source.write_text("invalid staged source\n")
        self.git("add", ".")
        self.source.write_text(GOOD)
        result = self.hook(1)
        self.assertIn("compile failed for staged source", result.stderr)

    def test_working_header_cannot_hide_index_error(self):
        self.header.write_text("#define VALUE invalid!\n")
        self.git("add", ".")
        self.header.write_text("#define VALUE 7\n")
        self.hook(1)

    def test_valid_index_ignores_working_header(self):
        self.git("add", ".")
        self.header.write_text("#define VALUE invalid!\n")
        self.hook(0)

    def test_large_staged_conflict(self):
        marker = self.root / "large file.txt"
        marker.write_text("<<<<<<< conflict\n" + "x" * 2_000_000 + "\n")
        self.git("add", ".")
        marker.write_text("fixed working copy\n")
        result = self.hook(1)
        self.assertIn("conflict markers in staged file: large file.txt", result.stderr)


if __name__ == "__main__":
    unittest.main()

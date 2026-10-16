#!/usr/bin/env python3
"""Fetch the pinned UI fonts. Verify each file before use."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import urllib.parse
import urllib.request


def install(target, data):
    with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    try:
        os.replace(temporary_path, target)
    finally:
        temporary_path.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--assembly", type=Path)
    parser.add_argument("--web", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    fonts = json.loads((root / "third_party/fonts/PIN").read_text())
    args.destination.mkdir(parents=True, exist_ok=True)
    assembly = ["#ifdef __APPLE__", ".section __TEXT,__const", "#define SYM(x) _##x",
                "#else", ".section .rodata", "#define SYM(x) x", "#endif"]
    css = []
    if args.web:
        args.web.mkdir(parents=True, exist_ok=True)
    for font in fonts:
        target = args.destination / (font["name"] + ".ttf")
        valid = target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == font["sha256"]
        if not valid:
            bundled = root / "third_party/fonts" / target.name
            if bundled != target and bundled.exists():
                data = bundled.read_bytes()
                if hashlib.sha256(data).hexdigest() != font["sha256"]:
                    raise SystemExit("Bundled font hash differs: " + font["name"])
                install(target, data)
                valid = True
        if not valid:
            url = "https://raw.githubusercontent.com/google/fonts/" + font["revision"] + "/" + urllib.parse.quote(font["path"])
            for attempt in range(4):
                try:
                    with urllib.request.urlopen(url, timeout=90) as response:
                        data = response.read()
                    if hashlib.sha256(data).hexdigest() != font["sha256"]:
                        raise SystemExit("Font hash differs: " + font["name"])
                    install(target, data)
                    break
                except (OSError, TimeoutError):
                    if attempt == 3:
                        raise
                    time.sleep(attempt + 1)
        symbol = "scuzz_font_" + font["name"]
        path = str(target.resolve()).replace("\\", "\\\\").replace('"', '\\"')
        assembly.extend([".balign 16", ".globl SYM(" + symbol + ")", "SYM(" + symbol + "):",
                         '.incbin "' + path + '"', ".balign 4", ".globl SYM(" + symbol + "_len)",
                         "SYM(" + symbol + "_len):", ".long " + str(target.stat().st_size)])
        if args.web:
            filename = font["name"] + "-" + font["sha256"][:16] + ".ttf"
            (args.web / filename).write_bytes(target.read_bytes())
            css.append('@font-face { font-family: "Scuzz ' + font["name"] + '"; src: url("./' + filename + '") format("truetype"); font-weight: 100 900; font-style: normal; font-display: swap; unicode-range: ' + font["unicode_range"] + '; }')
            (args.web / (font["name"] + "-LICENSE")).write_bytes((root / "third_party/fonts" / (font["name"] + "-LICENSE")).read_bytes())
    assembly.extend(["#ifndef __APPLE__", '.section .note.GNU-stack,"",@progbits', "#endif", ""])
    if args.assembly:
        args.assembly.write_text("\n".join(assembly))
    if args.web:
        (args.web / "fonts.css").write_text("\n".join(css) + "\n")


if __name__ == "__main__":
    main()

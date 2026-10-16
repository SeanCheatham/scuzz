#!/usr/bin/env python3
"""Compare native text with Skia metrics and unhinted outlines. Reject deliberate defects."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from png_max_delta import read_png


def check_metrics(reference, actual):
    assert len(reference) == len(actual) == 96
    for index, (expected, row) in enumerate(zip(reference, actual)):
        assert abs(expected["width"] - row["width"]) < .001, (index, "width")
        assert expected["breaks"] == row["breaks"], (index, "clusters")
        assert len(expected["positions"]) == len(row["positions"]), index
        assert all(abs(a - b) < .001 for a, b in zip(expected["positions"], row["positions"])), (index, "carets")
        for key in ["ascent", "descent"]:
            assert abs(expected[key] - row[key]) <= 2, (index, key)


def check_pixels(reference, actual):
    width, height, expected = reference
    w, h, pixels = actual
    assert (w, h) == (width, height) == (600, 96 * 48)
    results = []
    for row in range(96):
        ratios = []
        masses = []
        for source, target in [(pixels, expected), (expected, pixels)]:
            total = unmatched = 0
            for y in range(row * 48, (row + 1) * 48):
                for x in range(width):
                    ink = 250 - source[(y * width + x) * 4]
                    if ink < 8:
                        continue
                    total += ink
                    # Compare the silhouette within one pixel. Check ink mass separately.
                    covered = max(250 - target[(yy * width + xx) * 4]
                                  for yy in range(max(0, y - 1), min(height, y + 2))
                                  for xx in range(max(0, x - 1), min(width, x + 2)))
                    if covered < 8:
                        unmatched += ink
            assert total > 0, (row, "missing ink")
            masses.append(total)
            ratios.append(unmatched / total)
        assert max(ratios) < .025, (row, "glyph coverage", ratios)
        assert .6 < masses[0] / masses[1] < 1.6, (row, "ink mass", masses)
        results.append(max(ratios))
    return max(results)


def main():
    root = Path(__file__).resolve().parent.parent
    crate = root / "crates/ffi-skia"
    output = crate / "build/text-proof"
    output.mkdir(parents=True, exist_ok=True)
    report = output / "summary.json"
    report.unlink(missing_ok=True)
    env = dict(os.environ, CC="clang", CXX="clang++", LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="0")
    references = {}

    def run(args, label, stdout=None):
        with (output / (label + ".log")).open("w") as log:
            subprocess.run(args, cwd=crate, env=env, stdout=stdout or log,
                           stderr=log, check=True, timeout=300)

    try:
        for backend in ["skia", "sk_sw", "gpu"]:
            env["SCUZZ_SKIA"] = backend
            run(["make", "build/test_sk_text", "CC=clang", "CXX=clang++"], backend + "-build")
            with (output / (backend + ".json")).open("w") as stream:
                run([str(crate / "build/test_sk_text"), str(output / (backend + ".png"))], backend, stream)
            references[backend] = (json.loads((output / (backend + ".json")).read_text()), read_png(output / (backend + ".png")))
        env["SCUZZ_SKIA"] = "skia"
        run(["make", "lib", "CC=clang", "CXX=clang++"], "outline-reference-build")
        outline_source = (crate / "src/sk_capi_skia.cpp").read_text()
        before = "return SkFont(default_typeface(), px);"
        assert outline_source.count(before) == 1
        outline_source = outline_source.replace(before, "SkFont font(default_typeface(), px); font.setHinting(SkFontHinting::kNone); font.setSubpixel(true); return font;")
        before = "return SkFont(mono_typeface(), px);"
        assert outline_source.count(before) == 1
        outline_source = outline_source.replace(before, "SkFont font(mono_typeface(), px); font.setHinting(SkFontHinting::kNone); font.setSubpixel(true); return font;")
        (output / "outline.cpp").write_text(outline_source)
        run(["clang++", "-std=c++17", "-O2", "-fPIC", "-Isrc", "-Ibuild/skia-src", "-Ibuild/harfbuzz",
             "-Ibuild/sheenbidi/Headers", "-DSK_RELEASE", "-c", str(output / "outline.cpp"), "-o", str(output / "outline.o")], "outline-compile")
        cxx = "-lc++" if sys.platform == "darwin" else "-lstdc++"
        libraries = [cxx, "-lpthread", "-lm", "-lz", "-lbz2"]
        if sys.platform == "darwin":
            for framework in ["CoreFoundation", "CoreGraphics", "CoreText", "Foundation", "Carbon"]:
                libraries += ["-framework", framework]
        run(["clang", "-std=c11", "-UNDEBUG", "-Iinclude", "tests/test_sk_text.c", str(output / "outline.o"),
             "-Lbuild", "-lsk_capi", *libraries, "-o", str(output / "outline")], "outline-link")
        with (output / "outline.json").open("w") as stream:
            run([str(output / "outline"), str(output / "outline.png")], "outline", stream)
        outline_pixels = read_png(output / "outline.png")
        largest = {}
        for backend in ["sk_sw", "gpu"]:
            check_metrics(references["skia"][0], references[backend][0])
            largest[backend] = check_pixels(outline_pixels, references[backend][1])
        a, b = references["sk_sw"][1][2], references["gpu"][1][2]
        assert max(abs(x - y) for x, y in zip(a, b)) <= 2

        env["SCUZZ_SKIA"] = "sk_sw"
        run(["make", "lib", "CC=clang", "CXX=clang++"], "controls-build")
        source = (crate / "src/sk_native_text.cpp").read_text()
        defects = {
            "metric": ("return scuzz_text::shape_text(range, size, shape_font).width;",
                       "return scuzz_text::shape_text(range, size, shape_font).width + 1.f;"),
            "glyph": ("x + shaped.positions[i].x * scale", "x + 3. + shaped.positions[i].x * scale"),
        }
        for name, (before, after) in defects.items():
            assert source.count(before) == 1
            altered = output / (name + ".cpp")
            altered.write_text(source.replace(before, after))
            obj, binary = output / (name + ".o"), output / name
            run(["clang++", "-std=c++17", "-O2", "-fPIC", "-Isrc", "-Ibuild/harfbuzz",
                 "-Ibuild/sheenbidi/Headers", "-c", str(altered), "-o", str(obj)], name + "-compile")
            cxx = "-lc++" if sys.platform == "darwin" else "-lstdc++"
            run(["clang", "-std=c11", "-UNDEBUG", "-Iinclude", "tests/test_sk_text.c", str(obj),
                 "-Lbuild", "-lsk_capi", cxx, "-lpthread", "-lm", "-o", str(binary)], name + "-link")
            with (output / (name + ".json")).open("w") as stream:
                run([str(binary), str(output / (name + ".png"))], name, stream)
            try:
                if name == "metric":
                    check_metrics(references["skia"][0], json.loads((output / (name + ".json")).read_text()))
                else:
                    check_pixels(outline_pixels, read_png(output / (name + ".png")))
            except AssertionError:
                print(name + " defect rejected", flush=True)
            else:
                raise AssertionError(name + " defect passes")
        report.write_text(json.dumps({"rows": 96, "pixelReference": "Skia with hinting off and subpixel positions",
                                      "maximumUnmatchedInk": largest,
                                      "metricDefectRejected": True, "glyphDefectRejected": True,
                                      "sourceSha256": hashlib.sha256(source.encode()).hexdigest()}, indent=2) + "\n")
        print("Native text metrics, carets, clusters, and glyph coverage pass.", flush=True)
    finally:
        env.pop("SCUZZ_SKIA", None)
        run(["make", "lib", "CC=clang", "CXX=clang++"], "restore")


if __name__ == "__main__":
    main()

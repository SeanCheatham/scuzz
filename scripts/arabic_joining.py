#!/usr/bin/env python3
"""Extract joining data from the pinned HarfBuzz archive."""
import hashlib
from pathlib import Path
import re
import sys
import tarfile

archive, expected, destination = sys.argv[1:]
archive = Path(archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
    raise SystemExit("HarfBuzz archive hash differs")
with tarfile.open(archive) as source:
    member = next(m for m in source.getmembers() if m.name.endswith("/src/hb-ot-shaper-arabic-table.hh"))
    text = source.extractfile(member).read().decode()
    license = next(m for m in source.getmembers() if len(Path(m.name).parts) == 2 and m.name.endswith("/COPYING"))
    license_bytes = source.extractfile(license).read()
start = text.index("#define A\t")
end = text.index("#undef X") + len("#undef X")
text = text[start:end].replace("joining_table", "sk_joining_table").replace("joining_type", "sk_joining_type")
text = text.replace("hb_codepoint_t", "uint32_t")
text = re.sub(r"hb_in_range<uint32_t>\s*\(u, (0x[0-9A-Fa-f]+u), (0x[0-9A-Fa-f]+u)\)", r"(u >= \1 && u <= \2)", text)
if "hb_" in text or "sk_joining_type (uint32_t u)" not in text:
    raise SystemExit("HarfBuzz joining table format differs")
prefix = """#ifndef SCUZZ_ARABIC_JOINING_H
#define SCUZZ_ARABIC_JOINING_H
#include <stdint.h>
/* Use the joining data from the pinned HarfBuzz source. */
enum {
  JOINING_TYPE_U = 0, JOINING_TYPE_L = 1, JOINING_TYPE_R = 2,
  JOINING_TYPE_D = 3, JOINING_TYPE_C = 3, JOINING_GROUP_ALAPH = 4,
  JOINING_GROUP_DALATH_RISH = 5, JOINING_TYPE_T = 7, JOINING_TYPE_X = 8
};
"""
out = Path(destination)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(prefix + text + "\n#endif\n")
out.with_name("HarfBuzz-LICENSE").write_bytes(license_bytes)

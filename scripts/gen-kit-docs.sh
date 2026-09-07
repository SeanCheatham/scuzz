#!/usr/bin/env bash
# Write docs/kits.md from examples/compiler/src/Kits.scuzz.
# Check uses that same file. Do not edit the catalog by hand.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
python3 - "$ROOT" "$@" << 'PY'
import re, sys
from pathlib import Path
root = Path(sys.argv[1])
src = (root / "examples/compiler/src/Kits.scuzz").read_text()
# Flatten so k(...) can span lines after fmt.
flat = re.sub(r"\s+", " ", src)
rows = []
for m in re.finditer(r'\b(k|kv|ko)\("([^"]+)",\s*(.*?),\s*"([^"]+)"(?:,\s*(?:(\d+)|"([^"]+)"))?\)', flat):
    kind, name, params, ret, opt, rest = m.group(1), m.group(2), m.group(3).strip(), m.group(4), m.group(5), m.group(6)
    # params is a Scuzz list expression: "A" :: "B" :: ns()
    ps = re.findall(r'"([^"]*)"', params)
    if kind == "kv" and rest:
        shown = ", ".join(ps + [rest + "*"]) if ps else rest + "*"
    elif kind == "ko" and opt:
        shown = ", ".join(ps) if ps else "()"
        shown = shown + f" (opt {opt})"
    else:
        shown = ", ".join(ps) if ps else "()"
    rows.append((name, shown, ret))
# de-dupe by name keeping first
seen = set()
uniq = []
for r in rows:
    if r[0] in seen:
        continue
    seen.add(r[0])
    uniq.append(r)
out = []
out.append("# Kit catalog")
out.append("")
out.append("Signatures live in `examples/compiler/src/Kits.scuzz`. The typechecker uses that same table.")
out.append("")
out.append("| Kit | Params | Return |")
out.append("| --- | --- | --- |")
for name, shown, ret in uniq:
    out.append(f"| `{name}` | `{shown}` | `{ret}` |")
out.append("")
text = "\n".join(out)
path = root / "docs/kits.md"
check = "--check" in sys.argv[2:]
if check:
    if path.read_text() != text:
        print("docs/kits.md is stale; run scripts/gen-kit-docs.sh")
        sys.exit(1)
    print(f"kits.md-ok ({len(uniq)} kits)")
else:
    path.write_text(text)
    print(f"wrote {path} ({len(uniq)} kits)")
PY

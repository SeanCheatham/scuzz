#!/usr/bin/env bash
# Assemble a self-contained Stage-2 release tree + tarball under dist/.
#
# The shipped `scuzz` is always built by Scuzz Lang itself (Stage 1 → Stage 2).
# Stage 0 (Rust/cargo) is used only when no Scuzz Lang bootstrap binary is available
# (CI bootstrap / fresh checkout). Layout matches SCUZZ_HOME expectations
# (crates/ + scripts/). Host needs clang/make to link apps.
#
# Optional: SCUZZ_BOOTSTRAP=/path/to/scuzz skips Stage 0 (use Stage 1 from
# selfhost.sh, or any prior Scuzz Lang CLI that can rebuild compiler-scuzz).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
DIST_ROOT="${DIST_ROOT:-$ROOT/dist}"
NAME="scuzz-$TRIPLE"
OUT="$DIST_ROOT/$NAME"
TGZ="$DIST_ROOT/$NAME.tar.gz"

STAGE1_TMP=""
cleanup() {
  if [[ -n "$STAGE1_TMP" && -f "$STAGE1_TMP" ]]; then
    rm -f "$STAGE1_TMP"
  fi
}
trap cleanup EXIT

# Copy a Scuzz Lang CLI aside so a rebuild can overwrite compiler-scuzz/build/scuzz.
stage1_from() {
  local src="$1"
  STAGE1_TMP="$(mktemp "${TMPDIR:-/tmp}/scuzz-stage1.XXXXXX")"
  cp -f "$src" "$STAGE1_TMP"
  chmod +x "$STAGE1_TMP"
}

BOOTSTRAP="${SCUZZ_BOOTSTRAP:-}"
if [[ -z "$BOOTSTRAP" && -x "$ROOT/compiler-scuzz/build/scuzz" ]]; then
  BOOTSTRAP="$ROOT/compiler-scuzz/build/scuzz"
fi

if [[ -n "$BOOTSTRAP" && -x "$BOOTSTRAP" ]]; then
  echo "==> using Scuzz Lang bootstrap: $BOOTSTRAP"
  stage1_from "$BOOTSTRAP"
else
  echo "==> Stage 0 builds Stage 1 (set SCUZZ_BOOTSTRAP to skip cargo)"
  cargo run -p scuzz -- build --full compiler-scuzz
  test -x "$ROOT/compiler-scuzz/build/scuzz"
  stage1_from "$ROOT/compiler-scuzz/build/scuzz"
fi

echo "==> Stage 1 rebuilds compiler-scuzz (Stage 2 — release binary)"
"$STAGE1_TMP" build compiler-scuzz
STAGE2="$ROOT/compiler-scuzz/build/scuzz"
test -x "$STAGE2"

echo "==> assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/crates" "$OUT/scripts"

cp -f "$STAGE2" "$OUT/bin/scuzz"
chmod +x "$OUT/bin/scuzz"

copy_crate() {
  local name="$1"
  shift
  local dest="$OUT/crates/$name"
  mkdir -p "$dest"
  for part in "$@"; do
    local src="$ROOT/crates/$name/$part"
    if [[ -d "$src" ]]; then
      mkdir -p "$dest/$part"
      # Copy tree; skip build artifacts if present.
      (cd "$src" && tar cf - --exclude='build' --exclude='*.o' --exclude='*.a' .) | (cd "$dest/$part" && tar xf -)
    elif [[ -f "$src" ]]; then
      mkdir -p "$(dirname "$dest/$part")"
      cp -f "$src" "$dest/$part"
    else
      echo "missing crates/$name/$part" >&2
      exit 1
    fi
  done
}

copy_crate runtime include src Makefile
copy_crate ffi-skia include src Makefile
copy_crate embedder-desktop include src Makefile
copy_crate embedder-mobile include src Makefile shells

cp -f "$ROOT/scripts/run_goldens.sh" "$OUT/scripts/run_goldens.sh"
cp -f "$ROOT/scripts/package_project.sh" "$OUT/scripts/package_project.sh"
chmod +x "$OUT/scripts/run_goldens.sh" "$OUT/scripts/package_project.sh"

# package_project.sh resolves ROOT from scripts/.. — works inside the release tree.
{
  echo "triple=$TRIPLE"
  echo "stage=2"
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"$OUT/VERSION"

echo "==> writing $TGZ"
mkdir -p "$DIST_ROOT"
tar -C "$DIST_ROOT" -czf "$TGZ" "$NAME"

echo "packaged $OUT (Stage 2)"
echo "  tarball $TGZ"
echo "Install with: RELEASE_TGZ=$TGZ ./scripts/install.sh"
echo "  or:        RELEASE_DIR=$OUT ./scripts/install.sh"

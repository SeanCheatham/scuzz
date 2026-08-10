#!/usr/bin/env bash
# Assemble a self-contained Stage-1 release tree + tarball under dist/.
# Layout matches SCALUI_HOME expectations in the Stage-1 CLI (crates/ + scripts/).
# Host needs clang/make to link apps; Rust/cargo only needed to *build* this package
# unless SCALUI_BOOTSTRAP (or compiler-scalui/build/scalui) is already present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
DIST_ROOT="${DIST_ROOT:-$ROOT/dist}"
NAME="scalui-$TRIPLE"
OUT="$DIST_ROOT/$NAME"
TGZ="$DIST_ROOT/$NAME.tar.gz"

BOOTSTRAP="${SCALUI_BOOTSTRAP:-}"
if [[ -z "$BOOTSTRAP" && -x "$ROOT/compiler-scalui/build/scalui" ]]; then
  BOOTSTRAP="$ROOT/compiler-scalui/build/scalui"
fi

if [[ -n "$BOOTSTRAP" && -x "$BOOTSTRAP" ]]; then
  if [[ "$BOOTSTRAP" -ef "$ROOT/compiler-scalui/build/scalui" ]]; then
    TMP_BOOTSTRAP="$(mktemp "${TMPDIR:-/tmp}/scalui-bootstrap.XXXXXX")"
    cp -f "$BOOTSTRAP" "$TMP_BOOTSTRAP"
    chmod +x "$TMP_BOOTSTRAP"
    BOOTSTRAP="$TMP_BOOTSTRAP"
  fi
  echo "==> building Stage-1 CLI (self-build via $BOOTSTRAP; no Stage 0)"
  "$BOOTSTRAP" build compiler-scalui
else
  echo "==> building Stage-1 CLI (via Stage-0 bootstrap; SCALUI_BOOTSTRAP skips cargo)"
  cargo run -p scalui -- build --full compiler-scalui
fi

STAGE1="$ROOT/compiler-scalui/build/scalui"
test -x "$STAGE1"

echo "==> assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/crates" "$OUT/scripts"

cp -f "$STAGE1" "$OUT/bin/scalui"
chmod +x "$OUT/bin/scalui"

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
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >"$OUT/VERSION"

echo "==> writing $TGZ"
mkdir -p "$DIST_ROOT"
tar -C "$DIST_ROOT" -czf "$TGZ" "$NAME"

echo "packaged $OUT"
echo "  tarball $TGZ"
echo "Install with: RELEASE_TGZ=$TGZ ./scripts/install.sh"
echo "  or:        RELEASE_DIR=$OUT ./scripts/install.sh"

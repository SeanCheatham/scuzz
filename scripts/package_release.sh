#!/usr/bin/env bash
# Assemble a self-contained scuzz release tree + tarball under dist/.
#
# The shipped `scuzz` is the Scuzz CLI (`examples/cli`). bootstrap.sh compiles
# it with the newest GitHub `v*` scuzz. Layout matches SCUZZ_HOME (crates/ +
# scripts/). Host needs clang/make to link apps.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
DIST_ROOT="${DIST_ROOT:-$ROOT/dist}"
NAME="scuzz-$TRIPLE"
OUT="$DIST_ROOT/$NAME"
TGZ="$DIST_ROOT/$NAME.tar.gz"

echo "==> bootstrap product CLI"
"$ROOT/scripts/bootstrap.sh"
SCUZZ_BIN="${SCUZZ_PRODUCT:-$ROOT/examples/cli/build/cli}"
test -x "$SCUZZ_BIN"

echo "==> assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/crates" "$OUT/scripts"

cp -f "$SCUZZ_BIN" "$OUT/bin/scuzz"
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
copy_crate ffi-skia include src Makefile README.md
copy_crate embedder-desktop include src Makefile
copy_crate embedder-mobile include src Makefile shells

cp -f "$ROOT/scripts/fetch_skia.sh" "$OUT/scripts/fetch_skia.sh"
cp -f "$ROOT/scripts/skia_triple.sh" "$OUT/scripts/skia_triple.sh"
chmod +x "$OUT/scripts/fetch_skia.sh" "$OUT/scripts/skia_triple.sh"

# Skia pin + prebuilt for UI text (default backend). Opt out: SCUZZ_SKIA=sk_sw.
# fetch_skia.sh substitutes {triple} so Linux/macOS releases get the matching asset.
mkdir -p "$OUT/third_party/skia"
cp -f "$ROOT/third_party/skia/README.md" "$OUT/third_party/skia/README.md"
cp -f "$ROOT/third_party/skia/PIN" "$OUT/third_party/skia/PIN"
if [[ "${SCUZZ_SKIA:-}" == "sk_sw" ]]; then
  echo "==> SCUZZ_SKIA=sk_sw — release keeps in-tree sk_sw only"
else
  echo "==> fetching pinned Skia prebuilt into release tree"
  unset SCUZZ_SKIA_URL || true
  "$ROOT/scripts/fetch_skia.sh"
  SKIA_TRIPLE="$("$ROOT/scripts/skia_triple.sh")"
  if [[ ! -f "$ROOT/third_party/skia/prebuilt/${SKIA_TRIPLE}/libsk_capi.a" ]]; then
    echo "package_release: missing Skia prebuilt for ${SKIA_TRIPLE} (or set SCUZZ_SKIA=sk_sw)" >&2
    exit 1
  fi
  mkdir -p "$OUT/third_party/skia"
  cp -a "$ROOT/third_party/skia/prebuilt" "$OUT/third_party/skia/"
fi

echo "==> bundling IDE package"
mkdir -p "$OUT/ide"
(cd "$ROOT/examples/editor" && tar cf - --exclude=build --exclude=goldens --exclude=corpus --exclude='*.actual.*' .) | (cd "$OUT/ide" && tar xf -)
if [[ ! -f "$OUT/ide/scuzz.toml" ]]; then
  echo "package_release: missing examples/editor/scuzz.toml" >&2
  exit 1
fi

ver="${SCUZZ_VERSION:-}"
if [[ -z "$ver" ]] && command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  ver="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || true)"
fi
{
  if [[ -n "$ver" ]]; then
    echo "version=$ver"
  fi
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

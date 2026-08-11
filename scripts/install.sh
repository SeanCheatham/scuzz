#!/usr/bin/env bash
# Install Stage-2 `scuzz` into PREFIX (default: ~/.local).
#
# Installs a self-contained release tree under $PREFIX/share/scuzz and a
# wrapper at $PREFIX/bin/scuzz that sets SCUZZ_HOME. App builds need
# clang/make; Linux [ui] linking against the packaged Skia CPU prebuilt also
# needs zlib/bzip2/brotli. Rust/cargo is not required when installing from a
# prebuilt artifact (RELEASE_TGZ / RELEASE_DIR).
#
# From a checkout (default): builds/packages Stage 2 via package_release.sh,
# then installs that tree. Stage 0 is used only when no Scuzz Lang bootstrap
# binary is available (SCUZZ_BOOTSTRAP or compiler-scuzz/build/scuzz).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"
SHARE="${PREFIX}/share/scuzz"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

resolve_release_dir() {
  if [[ -n "${RELEASE_DIR:-}" ]]; then
    if [[ ! -x "$RELEASE_DIR/bin/scuzz" ]]; then
      echo "RELEASE_DIR=$RELEASE_DIR missing bin/scuzz" >&2
      exit 1
    fi
    echo "$RELEASE_DIR"
    return
  fi

  if [[ -n "${RELEASE_TGZ:-}" ]]; then
    if [[ ! -f "$RELEASE_TGZ" ]]; then
      echo "RELEASE_TGZ=$RELEASE_TGZ not found" >&2
      exit 1
    fi
    local extract
    extract="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-release.XXXXXX")"
    tar -C "$extract" -xzf "$RELEASE_TGZ"
    local dir
    dir="$(find "$extract" -mindepth 1 -maxdepth 1 -type d | head -1)"
    if [[ -z "$dir" || ! -x "$dir/bin/scuzz" ]]; then
      echo "RELEASE_TGZ=$RELEASE_TGZ did not contain bin/scuzz" >&2
      exit 1
    fi
    echo "$dir"
    return
  fi

  echo "==> packaging Stage-2 release (no RELEASE_DIR / RELEASE_TGZ)" >&2
  DIST_ROOT="${DIST_ROOT:-$ROOT/dist}" \
    SCUZZ_BOOTSTRAP="${SCUZZ_BOOTSTRAP:-}" \
    "$ROOT/scripts/package_release.sh" >&2
  local packaged="$ROOT/dist/scuzz-$TRIPLE"
  if [[ ! -x "$packaged/bin/scuzz" ]]; then
    echo "package_release.sh did not produce $packaged/bin/scuzz" >&2
    exit 1
  fi
  printf '%s\n' "$packaged"
}

RELEASE="$(resolve_release_dir)"

echo "==> installing release tree → $SHARE"
rm -rf "$SHARE"
mkdir -p "$(dirname "$SHARE")" "$BIN"
# Copy tree; keep a private copy under PREFIX (not a live checkout link).
mkdir -p "$SHARE"
(cd "$RELEASE" && tar cf - .) | (cd "$SHARE" && tar xf -)
chmod +x "$SHARE/bin/scuzz"
if [[ -d "$SHARE/scripts" ]]; then
  chmod +x "$SHARE/scripts"/*.sh 2>/dev/null || true
fi

WRAPPER="$BIN/scuzz"
cat >"$WRAPPER" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export SCUZZ_HOME="${SHARE}"
export SCUZZ_RUNTIME="\${SCUZZ_RUNTIME:-\$SCUZZ_HOME/crates/runtime}"
exec "\$SCUZZ_HOME/bin/scuzz" "\$@"
EOF
chmod +x "$WRAPPER"

echo "installed $WRAPPER"
echo "  SCUZZ_HOME=$SHARE"
if [[ -f "$SHARE/VERSION" ]]; then
  echo "  $(tr '\n' ' ' <"$SHARE/VERSION")"
fi
echo "Ensure $BIN is on PATH (clang + make required to build apps;"
echo "  Linux [ui] also needs zlib/bzip2/brotli: zlib1g-dev libbz2-dev libbrotli-dev), then:"
echo "  scuzz new myapp --ui"
echo "  cd myapp && scuzz test && scuzz run --headless"

#!/usr/bin/env bash
# Install Stage-1 `scalui` into PREFIX (default: ~/.local).
#
# Installs a self-contained release tree under $PREFIX/share/scalui and a
# wrapper at $PREFIX/bin/scalui that sets SCALUI_HOME. App builds need
# clang/make; Rust/cargo is not required when installing from a prebuilt
# artifact (RELEASE_TGZ / RELEASE_DIR).
#
# From a checkout (default): builds/packages Stage-1 via package_release.sh,
# then installs that tree. Self-host: SCALUI_BOOTSTRAP (or an existing
# compiler-scalui/build/scalui) rebuilds without Stage 0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"
SHARE="${PREFIX}/share/scalui"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

resolve_release_dir() {
  if [[ -n "${RELEASE_DIR:-}" ]]; then
    if [[ ! -x "$RELEASE_DIR/bin/scalui" ]]; then
      echo "RELEASE_DIR=$RELEASE_DIR missing bin/scalui" >&2
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
    extract="$(mktemp -d "${TMPDIR:-/tmp}/scalui-release.XXXXXX")"
    tar -C "$extract" -xzf "$RELEASE_TGZ"
    local dir
    dir="$(find "$extract" -mindepth 1 -maxdepth 1 -type d | head -1)"
    if [[ -z "$dir" || ! -x "$dir/bin/scalui" ]]; then
      echo "RELEASE_TGZ=$RELEASE_TGZ did not contain bin/scalui" >&2
      exit 1
    fi
    echo "$dir"
    return
  fi

  echo "==> packaging Stage-1 release (no RELEASE_DIR / RELEASE_TGZ)" >&2
  DIST_ROOT="${DIST_ROOT:-$ROOT/dist}" \
    SCALUI_BOOTSTRAP="${SCALUI_BOOTSTRAP:-}" \
    "$ROOT/scripts/package_release.sh" >&2
  local packaged="$ROOT/dist/scalui-$TRIPLE"
  if [[ ! -x "$packaged/bin/scalui" ]]; then
    echo "package_release.sh did not produce $packaged/bin/scalui" >&2
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
chmod +x "$SHARE/bin/scalui"
if [[ -d "$SHARE/scripts" ]]; then
  chmod +x "$SHARE/scripts"/*.sh 2>/dev/null || true
fi

WRAPPER="$BIN/scalui"
cat >"$WRAPPER" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export SCALUI_HOME="${SHARE}"
export SCALUI_RUNTIME="\${SCALUI_RUNTIME:-\$SCALUI_HOME/crates/runtime}"
exec "\$SCALUI_HOME/bin/scalui" "\$@"
EOF
chmod +x "$WRAPPER"

echo "installed $WRAPPER"
echo "  SCALUI_HOME=$SHARE"
echo "Ensure $BIN is on PATH (clang + make required to build apps), then:"
echo "  scalui new myapp --ui"
echo "  cd myapp && scalui test && scalui run --headless"

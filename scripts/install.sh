#!/usr/bin/env bash
# Install Stage-1 `scalui` (release CLI) into PREFIX (default: ~/.local).
# Requires a ScalUI checkout: runtime/embedder live under SCALUI_HOME.
# Self-hosting: an existing Stage-1 binary (SCALUI_BOOTSTRAP, or a previous
# compiler-scalui/build/scalui) rebuilds the CLI without cargo / Stage 0.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"

cd "$ROOT"
BOOTSTRAP="${SCALUI_BOOTSTRAP:-}"
if [[ -z "$BOOTSTRAP" && -x "$ROOT/compiler-scalui/build/scalui" ]]; then
  BOOTSTRAP="$ROOT/compiler-scalui/build/scalui"
fi

if [[ -n "$BOOTSTRAP" && -x "$BOOTSTRAP" ]]; then
  if [[ "$BOOTSTRAP" -ef "$ROOT/compiler-scalui/build/scalui" ]]; then
    # Rebuilding over the running binary would hit ETXTBSY; run a copy.
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

mkdir -p "$BIN"
WRAPPER="$BIN/scalui"
STAGE1="$ROOT/compiler-scalui/build/scalui"

cat >"$WRAPPER" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export SCALUI_HOME="${ROOT}"
export SCALUI_RUNTIME="\${SCALUI_RUNTIME:-\$SCALUI_HOME/crates/runtime}"
exec "${STAGE1}" "\$@"
EOF
chmod +x "$WRAPPER"

echo "installed $WRAPPER"
echo "  SCALUI_HOME=$ROOT"
echo "Ensure $BIN is on PATH, then:"
echo "  scalui new myapp --ui"
echo "  cd myapp && scalui test && scalui run --headless"

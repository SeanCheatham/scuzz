#!/usr/bin/env bash
# Install Stage-1 `scalui` (release CLI) into PREFIX (default: ~/.local).
# Requires a ScalUI checkout: runtime/embedder live under SCALUI_HOME.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"

cd "$ROOT"
echo "==> building Stage-1 CLI (via Stage-0 canary)"
cargo run -p scalui -- build --full compiler-scalui

mkdir -p "$BIN"
WRAPPER="$BIN/scalui"
STAGE1="$ROOT/compiler-scalui/build/scalui"
CANARY="$ROOT/target/debug/scalui"
if [[ ! -x "$CANARY" ]]; then
  CANARY="$ROOT/target/release/scalui"
fi

cat >"$WRAPPER" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export SCALUI_HOME="${ROOT}"
export SCALUI_RUNTIME="\${SCALUI_RUNTIME:-\$SCALUI_HOME/crates/runtime}"
if [[ -x "${CANARY}" ]]; then
  export SCALUI_CANARY="\${SCALUI_CANARY:-${CANARY}}"
fi
exec "${STAGE1}" "\$@"
EOF
chmod +x "$WRAPPER"

echo "installed $WRAPPER"
echo "  SCALUI_HOME=$ROOT"
echo "Ensure $BIN is on PATH, then:"
echo "  scalui new myapp --ui"
echo "  cd myapp && scalui test && scalui run --headless"

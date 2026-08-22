#!/usr/bin/env bash
# Point this clone at repo-owned hooks (scripts/githooks/).
# Local only — does not change remotes or other clones.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HOOKS_PATH=scripts/githooks
chmod +x "$HOOKS_PATH/pre-commit" scripts/check-fmt-clippy.sh

git config core.hooksPath "$HOOKS_PATH"
echo "core.hooksPath → $HOOKS_PATH"
echo "pre-commit checks: conflict markers, rustfmt+clippy when you stage .rs, -Werror compile on staged runtime/ffi/embedder .c"

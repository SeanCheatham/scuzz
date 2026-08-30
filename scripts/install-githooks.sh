#!/usr/bin/env bash
# Point this clone at repo-owned hooks (scripts/githooks/).
# Local only — does not change remotes or other clones.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HOOKS_PATH=scripts/githooks
chmod +x "$HOOKS_PATH/pre-commit"

git config core.hooksPath "$HOOKS_PATH"
echo "core.hooksPath → $HOOKS_PATH"
echo "pre-commit checks: conflict markers, -Werror compile on staged runtime/ffi/embedder .c"

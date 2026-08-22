#!/usr/bin/env bash
# rustfmt + clippy as CI rustfmt-clippy. Used by pre-commit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v cargo >/dev/null 2>&1; then
  echo "check-fmt-clippy: cargo not on PATH (see docs/developer-environment.md)" >&2
  exit 1
fi

echo "check-fmt-clippy: cargo fmt --all -- --check"
cargo fmt --all -- --check

echo "check-fmt-clippy: cargo clippy -p scuzz -p scuzz-compiler --all-targets"
cargo clippy -p scuzz -p scuzz-compiler --all-targets -- \
  -D warnings \
  -A clippy::too_many_arguments \
  -A clippy::type_complexity \
  -A clippy::if_same_then_else \
  -A clippy::collapsible_if \
  -A clippy::redundant_guards \
  -A clippy::useless_format \
  -A clippy::identity_op \
  -A clippy::len_zero \
  -A clippy::unnecessary_to_owned

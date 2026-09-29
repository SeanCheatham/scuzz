#!/usr/bin/env bash
# Build the product CLI from this checkout and install it locally.
#
# bootstrap.sh compiles examples/cli from the checkout sources.
# package_release.sh assembles a release tree around that CLI.
# install.sh installs the tree under PREFIX (default: ~/.local):
#   PREFIX/share/scuzz - release tree
#   PREFIX/bin/scuzz   - wrapper that sets SCUZZ_HOME
#
# Override the install root with PREFIX. Pass --dry-run to print the plan.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Prefer the compiler from this checkout when it is available. A release
# compiler can predate compiler APIs used by the current source tree.
if [ -z "${SCUZZ_BOOTSTRAP:-}" ] && [ -x "$ROOT/examples/cli/build/cli" ]; then
  SCUZZ_BOOTSTRAP="$ROOT/examples/cli/build/cli"
  export SCUZZ_BOOTSTRAP
fi

"$ROOT/scripts/bootstrap.sh"

DIST_ROOT="${DIST_ROOT:-$ROOT/dist}"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

"$ROOT/scripts/package_release.sh"

RELEASE_DIR="$DIST_ROOT/scuzz-$TRIPLE" "$ROOT/scripts/install.sh" "$@"

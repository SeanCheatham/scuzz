#!/usr/bin/env bash
# Print the default Scuzz Skia prebuilt triple for this host.
# Override with SCUZZ_SKIA_TRIPLE.
set -euo pipefail
if [[ -n "${SCUZZ_SKIA_TRIPLE:-}" ]]; then
  echo "${SCUZZ_SKIA_TRIPLE}"
  exit 0
fi
os="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"
case "${os}-${arch}" in
  linux-x86_64|linux-amd64) echo "x86_64-unknown-linux-gnu" ;;
  linux-aarch64|linux-arm64) echo "aarch64-unknown-linux-gnu" ;;
  darwin-arm64|darwin-aarch64) echo "aarch64-apple-darwin" ;;
  darwin-x86_64) echo "x86_64-apple-darwin" ;;
  *) echo "${arch}-unknown-${os}" ;;
esac

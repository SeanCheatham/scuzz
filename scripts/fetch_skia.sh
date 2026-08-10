#!/usr/bin/env bash
# Fetch prebuilt Skia static libs for Scuzz Lang.
#
# Default: no download — crates/ffi-skia ships a CPU software backend (sk_sw)
# that implements include/sk_capi.h. When hosted prebuilts exist, set:
#
#   SCUZZ_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz
#   SCUZZ_SKIA_TRIPLE=x86_64-unknown-linux-gnu   # optional
#   ./scripts/fetch_skia.sh
#
# Layout written under third_party/skia/prebuilt/<triple>/ when fetched.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/skia/prebuilt"
TRIPLE="${SCUZZ_SKIA_TRIPLE:-x86_64-unknown-linux-gnu}"

if [[ -z "${SCUZZ_SKIA_URL:-}" ]]; then
  cat <<EOF
fetch_skia: SCUZZ_SKIA_URL unset — using software sk_capi backend in crates/ffi-skia.
To install prebuilts:
  SCUZZ_SKIA_URL=<tarball-url> $0
EOF
  exit 0
fi

mkdir -p "${DEST}/${TRIPLE}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
echo "fetch_skia: downloading ${SCUZZ_SKIA_URL}"
curl -fsSL "${SCUZZ_SKIA_URL}" -o "${tmpdir}/skia.tgz"
tar -xzf "${tmpdir}/skia.tgz" -C "${DEST}/${TRIPLE}"
echo "fetch_skia: installed under ${DEST}/${TRIPLE}"

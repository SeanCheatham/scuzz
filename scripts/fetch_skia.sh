#!/usr/bin/env bash
# Fetch prebuilt Skia static libs for ScalUI (ADR 0002).
#
# Phase 1 default: no download — crates/ffi-skia ships a CPU software backend
# (sk_sw) that implements include/sk_capi.h. When hosted prebuilts exist, set:
#
#   SCALUI_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz
#   ./scripts/fetch_skia.sh
#
# Layout written under third_party/skia/prebuilt/<triple>/ when fetched.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/skia/prebuilt"
TRIPLE="${SCALUI_SKIA_TRIPLE:-x86_64-unknown-linux-gnu}"

if [[ -z "${SCALUI_SKIA_URL:-}" ]]; then
  cat <<EOF
fetch_skia: SCALUI_SKIA_URL unset — using software sk_capi backend in crates/ffi-skia.
To install prebuilts later:
  SCALUI_SKIA_URL=<tarball-url> $0
EOF
  exit 0
fi

mkdir -p "${DEST}/${TRIPLE}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
echo "fetch_skia: downloading ${SCALUI_SKIA_URL}"
curl -fsSL "${SCALUI_SKIA_URL}" -o "${tmpdir}/skia.tgz"
tar -xzf "${tmpdir}/skia.tgz" -C "${DEST}/${TRIPLE}"
echo "fetch_skia: installed under ${DEST}/${TRIPLE}"

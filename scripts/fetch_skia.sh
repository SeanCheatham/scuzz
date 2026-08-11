#!/usr/bin/env bash
# Fetch prebuilt Skia static libs for Scuzz Lang.
#
# Default: no download — crates/ffi-skia ships a CPU software backend (sk_sw)
# that implements include/sk_capi.h. When a URL is available:
#
#   SCUZZ_SKIA_URL=https://…/skia-x86_64-unknown-linux-gnu-cpu.tar.gz ./scripts/fetch_skia.sh
#
# If SCUZZ_SKIA_URL is unset, reads `url=` from third_party/skia/PIN when set.
# Layout written under third_party/skia/prebuilt/<triple>/ when fetched.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/skia/prebuilt"
TRIPLE="${SCUZZ_SKIA_TRIPLE:-"$("$ROOT/scripts/skia_triple.sh")"}"
PIN="${ROOT}/third_party/skia/PIN"
URL="${SCUZZ_SKIA_URL:-}"

if [[ -z "${URL}" && -f "${PIN}" ]]; then
  URL="$(awk -F= '/^url=/{print substr($0,5); exit}' "${PIN}" || true)"
fi

if [[ -z "${URL}" ]]; then
  cat <<EOF
fetch_skia: no URL (SCUZZ_SKIA_URL unset and third_party/skia/PIN url= empty) — using sk_sw.
To install prebuilts:
  SCUZZ_SKIA_URL=<tarball-url> $0
EOF
  exit 0
fi

mkdir -p "${DEST}/${TRIPLE}"
# Clear previous contents so stale companion libs do not linger.
rm -rf "${DEST}/${TRIPLE:?}/"*
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
echo "fetch_skia: downloading ${URL}"
# Allow file:// and plain paths for local proof tarballs.
if [[ "${URL}" == file://* ]]; then
  cp -f "${URL#file://}" "${tmpdir}/skia.tgz"
elif [[ -f "${URL}" ]]; then
  cp -f "${URL}" "${tmpdir}/skia.tgz"
else
  curl -fsSL "${URL}" -o "${tmpdir}/skia.tgz"
fi
tar -xzf "${tmpdir}/skia.tgz" -C "${DEST}/${TRIPLE}"
test -f "${DEST}/${TRIPLE}/libsk_capi.a"
echo "fetch_skia: installed under ${DEST}/${TRIPLE}"

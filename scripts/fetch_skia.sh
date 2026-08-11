#!/usr/bin/env bash
# Fetch the pinned Skia CPU prebuilt for Scuzz Lang (default UI backend).
#
# Default: read `url=` from third_party/skia/PIN (or SCUZZ_SKIA_URL) and install
# under third_party/skia/prebuilt/<triple>/. Fail closed if the URL is missing
# or the download fails.
#
# Opt out of Skia (in-tree sk_sw) with SCUZZ_SKIA=sk_sw — this script then
# exits 0 without downloading.
#
#   SCUZZ_SKIA_URL=https://…/skia-{triple}-cpu.tar.gz ./scripts/fetch_skia.sh
#   SCUZZ_SKIA=sk_sw ./scripts/fetch_skia.sh   # no-op
#
# `{triple}` in the URL is replaced with the host triple (or SCUZZ_SKIA_TRIPLE).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/skia/prebuilt"
TRIPLE="${SCUZZ_SKIA_TRIPLE:-"$("$ROOT/scripts/skia_triple.sh")"}"
PIN="${ROOT}/third_party/skia/PIN"
URL="${SCUZZ_SKIA_URL:-}"

if [[ "${SCUZZ_SKIA:-}" == "sk_sw" ]]; then
  echo "fetch_skia: SCUZZ_SKIA=sk_sw — skipping download (in-tree software backend)"
  exit 0
fi

if [[ -z "${URL}" && -f "${PIN}" ]]; then
  URL="$(awk -F= '/^url=/{print substr($0,5); exit}' "${PIN}" || true)"
fi

if [[ -z "${URL}" ]]; then
  cat <<EOF >&2
fetch_skia: no URL (SCUZZ_SKIA_URL unset and third_party/skia/PIN url= empty).
Default UI backend is Skia. Either:
  - set url= in third_party/skia/PIN / SCUZZ_SKIA_URL=<tarball>, or
  - opt out: SCUZZ_SKIA=sk_sw
EOF
  exit 1
fi

# Host-specific asset from a shared release pin (e.g. skia-{triple}-cpu.tar.gz).
URL="${URL//\{triple\}/${TRIPLE}}"

if [[ -f "${DEST}/${TRIPLE}/libsk_capi.a" && -z "${SCUZZ_SKIA_FORCE:-}" ]]; then
  echo "fetch_skia: already installed under ${DEST}/${TRIPLE}"
  exit 0
fi

mkdir -p "${DEST}/${TRIPLE}"
# Clear previous contents so stale companion libs do not linger.
rm -rf "${DEST}/${TRIPLE:?}/"*
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
echo "fetch_skia: triple=${TRIPLE}"
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

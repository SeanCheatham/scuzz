#!/usr/bin/env bash
# Fetch the pinned Skia CPU prebuilt for Scuzz Lang (default UI backend).
#
# Default: read `url=` from third_party/skia/PIN (or SCUZZ_SKIA_URL) and install
# under third_party/skia/prebuilt/<triple>/. Fail closed if the URL is missing
# or the download fails.
#
# Opt out of Skia (in-tree sk_sw) with SCUZZ_SKIA=sk_sw. This script then
# exits 0 without downloading.
#
# HTTPS fetches retry HTTP 502, 503, and 504, plus a truncated gzip, with
# backoff. Other HTTP errors fail closed on the first response.
#
#   SCUZZ_SKIA_URL=https://…/skia-{triple}-cpu.tar.gz ./scripts/fetch_skia.sh
#   SCUZZ_SKIA=sk_sw ./scripts/fetch_skia.sh   # no-op
#   SCUZZ_SKIA_FETCH_ATTEMPTS=5               # HTTPS tries (default 5)
#   SCUZZ_SKIA_FETCH_RETRY_DELAY=1            # first backoff seconds (default 1)
#
# `{triple}` in the URL is replaced with the host triple (or SCUZZ_SKIA_TRIPLE).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/skia/prebuilt"
TRIPLE="${SCUZZ_SKIA_TRIPLE:-"$("$ROOT/scripts/skia_triple.sh")"}"
PIN="${ROOT}/third_party/skia/PIN"
URL="${SCUZZ_SKIA_URL:-}"
ATTEMPTS="${SCUZZ_SKIA_FETCH_ATTEMPTS:-5}"
DELAY="${SCUZZ_SKIA_FETCH_RETRY_DELAY:-1}"

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

is_retryable_http() {
  case "$1" in
    000|408|429|500|502|503|504) return 0 ;;
    *) return 1 ;;
  esac
}

tarball_ok() {
  tar -tzf "$1" >/dev/null 2>&1
}

fetch_https() {
  local dest="$1"
  local url="$2"
  local attempt=1
  local delay="$DELAY"
  local http_code
  local curl_args=(-sS -L --connect-timeout 30 --max-time 300
    -A scuzz-fetch-skia -o "$dest" -w '%{http_code}')

  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    curl_args+=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
  fi

  while :; do
    rm -f "$dest"
    echo "fetch_skia: downloading ${url} (attempt ${attempt}/${ATTEMPTS})"
    http_code="$(curl "${curl_args[@]}" "$url" || true)"
    if [[ -z "${http_code}" ]]; then
      http_code=000
    fi
    if [[ "${http_code}" == "200" ]]; then
      if tarball_ok "$dest"; then
        return 0
      fi
      echo "fetch_skia: truncated or corrupt gzip (attempt ${attempt}/${ATTEMPTS})" >&2
    elif is_retryable_http "${http_code}"; then
      echo "fetch_skia: HTTP ${http_code} (attempt ${attempt}/${ATTEMPTS})" >&2
    else
      echo "fetch_skia: HTTP ${http_code} for ${url}" >&2
      return 1
    fi
    if [[ "${attempt}" -ge "${ATTEMPTS}" ]]; then
      echo "fetch_skia: giving up after ${ATTEMPTS} attempts" >&2
      return 1
    fi
    echo "fetch_skia: retry in ${delay}s" >&2
    sleep "${delay}"
    delay=$((delay * 2))
    if [[ "${delay}" -gt 16 ]]; then
      delay=16
    fi
    attempt=$((attempt + 1))
  done
}

mkdir -p "${DEST}/${TRIPLE}"
# Clear the dest dir so a prior fetch cannot leave extra files.
rm -rf "${DEST}/${TRIPLE:?}/"*
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
echo "fetch_skia: triple=${TRIPLE}"
# Allow file:// and plain paths for local proof tarballs.
if [[ "${URL}" == file://* ]]; then
  echo "fetch_skia: copying ${URL}"
  cp -f "${URL#file://}" "${tmpdir}/skia.tgz"
elif [[ -f "${URL}" ]]; then
  echo "fetch_skia: copying ${URL}"
  cp -f "${URL}" "${tmpdir}/skia.tgz"
else
  fetch_https "${tmpdir}/skia.tgz" "${URL}"
fi
if ! tarball_ok "${tmpdir}/skia.tgz"; then
  echo "fetch_skia: not a gzip tarball: ${URL}" >&2
  exit 1
fi
tar -xzf "${tmpdir}/skia.tgz" -C "${DEST}/${TRIPLE}"
test -f "${DEST}/${TRIPLE}/libsk_capi.a"
echo "fetch_skia: installed under ${DEST}/${TRIPLE}"

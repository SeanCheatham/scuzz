#!/usr/bin/env bash
# Compile the product CLI (`examples/cli`) with tagged `scuzz`.
#
# Writes examples/cli/build/cli (override with SCUZZ_PRODUCT).
# Default tag is the newest GitHub Release matching v[0-9]*.
# Override with SCUZZ_BOOTSTRAP_TAG. Point SCUZZ_BOOTSTRAP at a local
# binary to skip the download.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

die() {
  echo "Error: $1" >&2
  shift
  for line in "$@"; do
    echo "  $line" >&2
  done
  exit 1
}

# shellcheck source=../VERSION
. "$ROOT/VERSION"
if [ -z "${product:-}" ]; then
  die "VERSION must set product="
fi
got="$(awk '
  $0 == "def product(): String =" { getline; gsub(/^[[:space:]]+"/, ""); gsub(/"$/, ""); print; exit }
  $0 ~ /^def product\(\): String = "/ {
    sub(/^def product\(\): String = "/, "")
    sub(/"$/, "")
    print
    exit
  }
' "$ROOT/examples/cli/src/Version.scuzz")"
if [ "$got" != "$product" ]; then
  die "Version.scuzz product() does not match VERSION product=$product" \
    "got: $got"
fi
REPO="${SCUZZ_REPO:-SeanCheatham/scuzz}"
PRODUCT="${SCUZZ_PRODUCT:-$ROOT/examples/cli/build/cli}"
CACHE="${SCUZZ_BOOTSTRAP_DIR:-$ROOT/.bootstrap}"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
ASSET="scuzz-${TRIPLE}.tar.gz"

file_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    die "sha256sum or shasum is required to verify a bootstrap release." \
      "Debian/Ubuntu: sudo apt-get install coreutils"
  fi
}

fetch() {
  dest=$1
  url=$2
  if command -v curl >/dev/null 2>&1; then
    if [ -n "${GITHUB_TOKEN:-}" ]; then
      curl -fsSL -A scuzz-bootstrap -H "Authorization: Bearer ${GITHUB_TOKEN}" -o "$dest" "$url" || return 1
    else
      curl -fsSL -A scuzz-bootstrap -o "$dest" "$url" || return 1
    fi
  elif command -v wget >/dev/null 2>&1; then
    if [ -n "${GITHUB_TOKEN:-}" ]; then
      wget -q -U scuzz-bootstrap --header "Authorization: Bearer ${GITHUB_TOKEN}" -O "$dest" "$url" || return 1
    else
      wget -q -U scuzz-bootstrap -O "$dest" "$url" || return 1
    fi
  else
    die "curl is required to download tagged scuzz ${TAG:-}." \
      "Debian/Ubuntu: sudo apt-get install curl" \
      "or set SCUZZ_BOOTSTRAP to that tagged binary"
  fi
}

# Skia CPU releases share this repo. Pick the newest tag matching v[0-9]*.
# Do not use GitHub's /releases/latest (it may be skia-cpu-v*).
resolve_bootstrap_tag() {
  if [ -n "${SCUZZ_BOOTSTRAP:-}" ]; then
    TAG="${SCUZZ_BOOTSTRAP_TAG:-local}"
    return
  fi
  if [ -n "${SCUZZ_BOOTSTRAP_TAG:-}" ]; then
    TAG="$SCUZZ_BOOTSTRAP_TAG"
    return
  fi
  mkdir -p "$CACHE"
  json="$CACHE/releases.json"
  if [ -n "${SCUZZ_RELEASES_JSON:-}" ]; then
    json="$SCUZZ_RELEASES_JSON"
  else
    api="https://api.github.com/repos/${REPO}/releases?per_page=100"
    echo "==> listing $api" >&2
    if ! fetch "$json" "$api"; then
      die "could not list GitHub releases" \
        "tried: $api" \
        "or set SCUZZ_BOOTSTRAP_TAG"
    fi
  fi
  TAG="$(awk -F'"' '/"tag_name":/ {
    if ($4 ~ /^v[0-9][^-]*$/) { print $4; exit }
  }' "$json")"
  if [ -z "$TAG" ]; then
    die "no GitHub Release tag matching v[0-9]* in $REPO" \
      "or set SCUZZ_BOOTSTRAP_TAG"
  fi
}

download_bootstrap() {
  mkdir -p "$CACHE/$TAG"
  work="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-bootstrap.XXXXXX")"
  tgz="$work/$ASSET"
  sums="$work/SHA256SUMS"
  url="https://github.com/${REPO}/releases/download/${TAG}/${ASSET}"
  sums_url="https://github.com/${REPO}/releases/download/${TAG}/SHA256SUMS"
  echo "==> downloading $url" >&2
  if ! fetch "$tgz" "$url"; then
    rm -rf "$work"
    return 1
  fi
  if ! fetch "$sums" "$sums_url"; then
    rm -rf "$work"
    return 1
  fi
  expected="$(awk -v f="$ASSET" '$2 == f || $2 == ("./" f) || $2 == ("*" f) { print $1; exit }' "$sums")"
  if [ -z "$expected" ]; then
    die "SHA256SUMS has no entry for $ASSET" \
      "tried: $sums_url"
  fi
  actual="$(file_sha256 "$tgz")"
  if [ "$expected" != "$actual" ]; then
    die "checksum mismatch for $ASSET" \
      "expected: $expected" \
      "actual:   $actual"
  fi
  mkdir -p "$work/unpacked"
  tar -C "$work/unpacked" -xzf "$tgz"
  found=""
  for cand in "$work/unpacked"/*/bin/scuzz "$work/unpacked/bin/scuzz"; do
    if [ -x "$cand" ]; then
      found="$cand"
      break
    fi
  done
  if [ -z "$found" ]; then
    die "tarball did not contain bin/scuzz" \
      "tried: $url"
  fi
  cp -f "$found" "$CACHE/$TAG/scuzz"
  chmod +x "$CACHE/$TAG/scuzz"
  rm -rf "$work"
}

resolve_bootstrap() {
  if [ -n "${SCUZZ_BOOTSTRAP:-}" ]; then
    if [ ! -x "$SCUZZ_BOOTSTRAP" ]; then
      die "SCUZZ_BOOTSTRAP=$SCUZZ_BOOTSTRAP is not executable"
    fi
    printf '%s\n' "$SCUZZ_BOOTSTRAP"
    return
  fi
  bin="$CACHE/$TAG/scuzz"
  if [ ! -x "$bin" ]; then
    if ! download_bootstrap; then
      die "no bootstrap $ASSET for $TAG" \
        "publish $TAG or set SCUZZ_BOOTSTRAP to that tagged binary"
    fi
  fi
  printf '%s\n' "$bin"
}

resolve_bootstrap_tag
BOOTSTRAP="$(resolve_bootstrap)"
echo "==> bootstrap $BOOTSTRAP ($TAG)" >&2
# The checkout runtime must win. Do not inherit a packaged SCUZZ_HOME.
unset SCUZZ_HOME || true
export SCUZZ_RUNTIME="$ROOT/crates/runtime"

echo "==> $BOOTSTRAP build examples/cli" >&2
"$BOOTSTRAP" build examples/cli
SRC="$ROOT/examples/cli/build/cli"
if [ ! -x "$SRC" ]; then
  die "bootstrap build did not produce $SRC"
fi
LL="$ROOT/examples/cli/build/cli.ll"
RT="$ROOT/crates/runtime/build/libscuzz_rt.a"
if [ ! -f "$LL" ]; then
  die "bootstrap build did not write $LL"
fi
if [ ! -f "$RT" ]; then
  die "bootstrap build did not write $RT"
fi
echo "==> clang -O2 $LL" >&2
if [ "$(uname -s)" = Darwin ]; then
  clang -O2 -Wno-override-module "$LL" "$RT" -framework CoreFoundation -lpthread -o "$SRC"
else
  clang -O2 -Wno-override-module "$LL" "$RT" -lpthread -o "$SRC"
fi
if [ ! -x "$SRC" ]; then
  die "clang -O2 did not produce $SRC"
fi
mkdir -p "$(dirname "$PRODUCT")"
if [ "$PRODUCT" != "$SRC" ]; then
  cp -f "$SRC" "$PRODUCT"
  chmod +x "$PRODUCT"
fi
echo "product $PRODUCT"

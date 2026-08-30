#!/usr/bin/env bash
# Compile the product CLI (`examples/cli`) with tagged last-Rust `scuzz`.
#
# Writes examples/cli/build/cli (override with SCUZZ_PRODUCT).
# Default tag is v0.2.0. Override with SCUZZ_BOOTSTRAP_TAG.
# Point SCUZZ_BOOTSTRAP at a local tagged binary to skip the download.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TAG="${SCUZZ_BOOTSTRAP_TAG:-v0.2.0}"
REPO="${SCUZZ_REPO:-SeanCheatham/scuzz}"
PRODUCT="${SCUZZ_PRODUCT:-$ROOT/examples/cli/build/cli}"
CACHE="${SCUZZ_BOOTSTRAP_DIR:-$ROOT/.bootstrap}"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
ASSET="scuzz-${TRIPLE}.tar.gz"

die() {
  echo "Error: $1" >&2
  shift
  for line in "$@"; do
    echo "  $line" >&2
  done
  exit 1
}

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
    die "curl is required to download tagged scuzz $TAG." \
      "Debian/Ubuntu: sudo apt-get install curl" \
      "or set SCUZZ_BOOTSTRAP to that tagged binary"
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
mkdir -p "$(dirname "$PRODUCT")"
if [ "$PRODUCT" != "$SRC" ]; then
  cp -f "$SRC" "$PRODUCT"
  chmod +x "$PRODUCT"
fi
echo "product $PRODUCT"

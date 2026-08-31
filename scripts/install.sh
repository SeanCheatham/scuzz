#!/bin/sh
# Install `scuzz` into PREFIX (default: ~/.local).
#
# Installs a self-contained release tree under $PREFIX/share/scuzz and a
# wrapper at $PREFIX/bin/scuzz that sets SCUZZ_HOME. App builds need
# clang/make. Linux [ui] linking against the packaged Skia CPU prebuilt also
# needs zlib/bzip2. Rust/cargo is not required when installing from a
# prebuilt artifact (RELEASE_TGZ / RELEASE_DIR / GitHub Release). From a
# checkout, `package_release.sh` compiles `examples/cli` with tagged bootstrap.
#
# Sources (first match):
#   RELEASE_DIR / RELEASE_TGZ — local tree or tarball
#   checkout — this file sits next to package_release.sh (default in-repo)
#   GitHub Release — piped `curl | sh`, or SCUZZ_INSTALL_SOURCE=github
set -eu

DEFAULT_REPO="SeanCheatham/scuzz"
PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"
SHARE="${PREFIX}/share/scuzz"
TRIPLE="$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
ASSET="scuzz-${TRIPLE}.tar.gz"
SCUZZ_REPO="${SCUZZ_REPO:-$DEFAULT_REPO}"
SCUZZ_VERSION="${SCUZZ_VERSION:-latest}"
SCUZZ_INSTALL_SOURCE="${SCUZZ_INSTALL_SOURCE:-}"
DRY_RUN="${SCUZZ_INSTALL_DRY_RUN:-}"
WORK=""
RELEASE=""
SOURCE=""
TAG=""

usage() {
  cat <<EOF
Install scuzz into PREFIX (default: ~/.local).

Usage:
  curl -fsSL https://github.com/${DEFAULT_REPO}/releases/latest/download/install.sh | sh
  ./scripts/install.sh
  ./scripts/install.sh --help
  ./scripts/install.sh --dry-run

Options:
  --help          Show this help
  --dry-run       Print the install plan and exit (also SCUZZ_INSTALL_DRY_RUN=1)
  --from-github   Download a GitHub Release even from a checkout

Env:
  PREFIX                 Install root (default: ~/.local)
  SCUZZ_VERSION          Release tag, or latest (default: latest)
  SCUZZ_REPO             GitHub owner/repo (default: ${DEFAULT_REPO})
  SCUZZ_INSTALL_SOURCE   github | checkout (default: auto)
  RELEASE_TGZ            Local tarball path
  RELEASE_DIR            Unpacked release tree with bin/scuzz
  GITHUB_TOKEN           Optional; GitHub API rate limits / private forks

Examples:
  curl -fsSL https://github.com/${DEFAULT_REPO}/releases/latest/download/install.sh | sh
  SCUZZ_VERSION=v0.2.1 curl -fsSL https://github.com/${DEFAULT_REPO}/releases/latest/download/install.sh | sh
  PREFIX=/usr/local ./scripts/install.sh
  RELEASE_TGZ=dist/scuzz-${TRIPLE}.tar.gz ./scripts/install.sh
  SCUZZ_INSTALL_SOURCE=github SCUZZ_INSTALL_DRY_RUN=1 ./scripts/install.sh
EOF
}

die() {
  echo "Error: $1" >&2
  shift
  for line in "$@"; do
    echo "  $line" >&2
  done
  exit 1
}

for arg in "$@"; do
  case "$arg" in
    -h|--help)
      usage
      exit 0
      ;;
    --dry-run)
      DRY_RUN=1
      ;;
    --from-github)
      SCUZZ_INSTALL_SOURCE=github
      ;;
    *)
      die "unknown argument: $arg" \
        "./scripts/install.sh --help"
      ;;
  esac
done

cleanup() {
  if [ -n "${WORK:-}" ] && [ -d "$WORK" ]; then
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

scripts_dir() {
  case "$0" in
    */*)
      (cd "$(dirname "$0")" && pwd)
      ;;
    *)
      echo ""
      ;;
  esac
}

SCRIPTS="$(scripts_dir)"
ROOT=""
if [ -n "$SCRIPTS" ] && [ -f "$SCRIPTS/package_release.sh" ]; then
  ROOT="$(cd "$SCRIPTS/.." && pwd)"
fi

in_checkout() {
  [ -n "$ROOT" ] && [ -f "$ROOT/scripts/package_release.sh" ]
}

is_latest_version() {
  [ -z "$SCUZZ_VERSION" ] || [ "$SCUZZ_VERSION" = "latest" ]
}

# Skia CPU releases share this repo. Pick the newest tag matching v[0-9]*.
# Do not use GitHub's /releases/latest (it may be skia-cpu-v*).
# Compact one-line JSON and pretty fixtures both work.
pick_v_release_tag() {
  tr '"' '\n' < "$1" | awk '
    $0 == "tag_name" { n=1; next }
    n==1 { n=2; next }
    n==2 {
      if ($0 ~ /^v[0-9][^-]*$/) { print; exit }
      n=0
    }
  '
}

resolve_release_tag() {
  if ! is_latest_version; then
    TAG="$SCUZZ_VERSION"
    return
  fi
  ensure_work
  json="$WORK/releases.json"
  if [ -n "${SCUZZ_RELEASES_JSON:-}" ]; then
    # Test fixture: read releases JSON from this file instead of the API.
    json="$SCUZZ_RELEASES_JSON"
  else
    api="https://api.github.com/repos/${SCUZZ_REPO}/releases?per_page=100"
    fetch "$json" "$api" || die "could not list GitHub releases" \
      "tried: $api"
  fi
  TAG="$(pick_v_release_tag "$json")"
  if [ -z "$TAG" ]; then
    die "no GitHub Release tag matching v[0-9]* in $SCUZZ_REPO" \
      "publish with: git tag v0.1.0 && git push origin v0.1.0"
  fi
}

ensure_work() {
  if [ -z "$WORK" ]; then
    WORK="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-release.XXXXXX")"
  fi
}

fetch() {
  dest=$1
  url=$2
  if command -v curl >/dev/null 2>&1; then
    if [ -n "${GITHUB_TOKEN:-}" ]; then
      curl -fsSL -A scuzz-install -H "Authorization: Bearer ${GITHUB_TOKEN}" -o "$dest" "$url" || return 1
    else
      curl -fsSL -A scuzz-install -o "$dest" "$url" || return 1
    fi
  elif command -v wget >/dev/null 2>&1; then
    if [ -n "${GITHUB_TOKEN:-}" ]; then
      wget -q -U scuzz-install --header "Authorization: Bearer ${GITHUB_TOKEN}" -O "$dest" "$url" || return 1
    else
      wget -q -U scuzz-install -O "$dest" "$url" || return 1
    fi
  else
    die "curl is required to download a Scuzz release." \
      "Debian/Ubuntu: sudo apt-get install curl"
  fi
}

file_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    die "sha256sum or shasum is required to verify a release." \
      "Debian/Ubuntu: sudo apt-get install coreutils"
  fi
}

extract_tgz() {
  tgz=$1
  ensure_work
  unpacked="$WORK/unpacked"
  mkdir -p "$unpacked"
  tar -C "$unpacked" -xzf "$tgz"
  dir=""
  for d in "$unpacked"/*; do
    if [ -d "$d" ]; then
      dir=$d
      break
    fi
  done
  if [ -z "$dir" ] || [ ! -x "$dir/bin/scuzz" ]; then
    die "tarball did not contain bin/scuzz" \
      "RELEASE_TGZ=$tgz"
  fi
  RELEASE="$dir"
}

pick_source() {
  if [ -n "${RELEASE_DIR:-}" ] || [ -n "${RELEASE_TGZ:-}" ]; then
    SOURCE=local
    return
  fi
  case "${SCUZZ_INSTALL_SOURCE}" in
    github)
      SOURCE=github
      return
      ;;
    checkout)
      SOURCE=checkout
      return
      ;;
    "")
      ;;
    *)
      die "unknown SCUZZ_INSTALL_SOURCE=${SCUZZ_INSTALL_SOURCE}" \
        "SCUZZ_INSTALL_SOURCE=github ./scripts/install.sh" \
        "SCUZZ_INSTALL_SOURCE=checkout ./scripts/install.sh"
      ;;
  esac
  if in_checkout; then
    SOURCE=checkout
  else
    SOURCE=github
  fi
}

pick_source

if [ -n "$DRY_RUN" ]; then
  echo "PREFIX=$PREFIX"
  echo "triple=$TRIPLE"
  echo "wrapper=$BIN/scuzz"
  echo "share=$SHARE"
  case "$SOURCE" in
    local)
      if [ -n "${RELEASE_DIR:-}" ]; then
        echo "source=RELEASE_DIR $RELEASE_DIR"
      else
        echo "source=RELEASE_TGZ $RELEASE_TGZ"
      fi
      ;;
    checkout)
      echo "source=checkout $ROOT (package_release.sh)"
      ;;
    github)
      echo "source=github $SCUZZ_REPO $SCUZZ_VERSION"
      if [ -n "${SCUZZ_RELEASES_JSON:-}" ]; then
        resolve_release_tag
        echo "tag=$TAG"
      elif is_latest_version; then
        echo "releases=https://api.github.com/repos/${SCUZZ_REPO}/releases?per_page=100"
        echo "asset=$ASSET"
      else
        echo "download=https://github.com/${SCUZZ_REPO}/releases/download/${SCUZZ_VERSION}/${ASSET}"
        echo "checksums=https://github.com/${SCUZZ_REPO}/releases/download/${SCUZZ_VERSION}/SHA256SUMS"
      fi
      ;;
  esac
  exit 0
fi

if [ -n "${RELEASE_DIR:-}" ]; then
  if [ ! -x "$RELEASE_DIR/bin/scuzz" ]; then
    die "RELEASE_DIR=$RELEASE_DIR missing bin/scuzz"
  fi
  RELEASE="$RELEASE_DIR"
elif [ -n "${RELEASE_TGZ:-}" ]; then
  if [ ! -f "$RELEASE_TGZ" ]; then
    die "RELEASE_TGZ=$RELEASE_TGZ not found"
  fi
  extract_tgz "$RELEASE_TGZ"
elif [ "$SOURCE" = "checkout" ]; then
  if ! in_checkout; then
    die "not a Scuzz checkout (missing scripts/package_release.sh)." \
      "curl -fsSL https://github.com/${SCUZZ_REPO}/releases/latest/download/install.sh | sh"
  fi
  echo "==> packaging release (no RELEASE_DIR / RELEASE_TGZ)"
  DIST_ROOT="${DIST_ROOT:-$ROOT/dist}" \
    "$ROOT/scripts/package_release.sh"
  RELEASE="$ROOT/dist/scuzz-$TRIPLE"
  if [ ! -x "$RELEASE/bin/scuzz" ]; then
    die "package_release.sh did not produce $RELEASE/bin/scuzz"
  fi
else
  resolve_release_tag
  ASSET_URL="https://github.com/${SCUZZ_REPO}/releases/download/${TAG}/${ASSET}"
  SUMS_URL="https://github.com/${SCUZZ_REPO}/releases/download/${TAG}/SHA256SUMS"
  echo "==> downloading $ASSET_URL"
  ensure_work
  tgz="$WORK/$ASSET"
  sums="$WORK/SHA256SUMS"
  fetch "$tgz" "$ASSET_URL" || die "no prebuilt for $TRIPLE" \
    "tried: $ASSET_URL" \
    "shipped triples: linux-x86_64, darwin-arm64" \
    "from a checkout: ./scripts/install.sh"
  fetch "$sums" "$SUMS_URL" || die "SHA256SUMS missing for this release" \
    "tried: $SUMS_URL"
  expected="$(awk -v f="$ASSET" '$2 == f || $2 == ("./" f) || $2 == ("*" f) { print $1; exit }' "$sums")"
  if [ -z "$expected" ]; then
    die "SHA256SUMS has no entry for $ASSET" \
      "tried: $SUMS_URL"
  fi
  actual="$(file_sha256 "$tgz")"
  if [ "$expected" != "$actual" ]; then
    die "checksum mismatch for $ASSET" \
      "expected: $expected" \
      "actual:   $actual"
  fi
  extract_tgz "$tgz"
fi

echo "==> installing release tree → $SHARE"
rm -rf "$SHARE"
mkdir -p "$(dirname "$SHARE")" "$BIN"
mkdir -p "$SHARE"
(cd "$RELEASE" && tar cf - .) | (cd "$SHARE" && tar xf -)
chmod +x "$SHARE/bin/scuzz"
if [ -d "$SHARE/scripts" ]; then
  chmod +x "$SHARE/scripts"/*.sh 2>/dev/null || true
fi

WRAPPER="$BIN/scuzz"
cat >"$WRAPPER" <<EOF
#!/bin/sh
set -eu
export SCUZZ_HOME="${SHARE}"
export SCUZZ_RUNTIME="\${SCUZZ_RUNTIME:-\$SCUZZ_HOME/crates/runtime}"
exec "\$SCUZZ_HOME/bin/scuzz" "\$@"
EOF
chmod +x "$WRAPPER"

echo "installed $WRAPPER"
echo "  SCUZZ_HOME=$SHARE"
if [ -f "$SHARE/VERSION" ]; then
  echo "  $(tr '\n' ' ' <"$SHARE/VERSION")"
fi
echo "Put $BIN on PATH (clang + make required to build apps;"
echo "  Linux [ui] also needs zlib/bzip2: zlib1g-dev libbz2-dev), then:"
echo "  scuzz new myapp --ui"
echo "  cd myapp && scuzz test && scuzz run --headless"
echo "  scuzz ide --headless ."

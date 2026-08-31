#!/usr/bin/env bash
# Set product= in VERSION and examples/cli/src/Version.scuzz.
#
# Does not commit. The GitHub release workflow commits, tags, and publishes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DRY=0
SPEC=""

die() {
  echo "Error: $1" >&2
  shift
  for line in "$@"; do
    echo "  $line" >&2
  done
  exit 1
}

usage() {
  cat <<EOF
Set the product version in VERSION and Version.scuzz.

Usage:
  ./scripts/bump_version.sh patch
  ./scripts/bump_version.sh minor
  ./scripts/bump_version.sh major
  ./scripts/bump_version.sh 0.2.2
  ./scripts/bump_version.sh --dry-run patch
  ./scripts/bump_version.sh --help

Options:
  --help       Show this help
  --dry-run    Print the next version. Do not write files.

Examples:
  ./scripts/bump_version.sh --dry-run patch
  ./scripts/bump_version.sh 0.2.2
EOF
}

read_product() {
  # shellcheck source=../VERSION
  . "$ROOT/VERSION"
  if [ -z "${product:-}" ]; then
    die "VERSION must set product="
  fi
}

valid_product() {
  case "$1" in
    [0-9]*.[0-9]*.[0-9]*)
      echo "$1" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$'
      ;;
    *)
      return 1
      ;;
  esac
}

next_product() {
  local cur="$1" spec="$2" maj min pat
  case "$spec" in
    patch|minor|major)
      case "$cur" in
        *-*)
          die "product=$cur is a prerelease. Pass an explicit version." \
            "./scripts/bump_version.sh 0.2.2"
          ;;
      esac
      IFS=. read -r maj min pat <<EOF
$cur
EOF
      case "$spec" in
        patch) echo "$maj.$min.$((pat + 1))" ;;
        minor) echo "$maj.$((min + 1)).0" ;;
        major) echo "$((maj + 1)).0.0" ;;
      esac
      ;;
    *)
      spec="${spec#v}"
      if ! valid_product "$spec"; then
        die "version '$spec' is not N.N.N" \
          "./scripts/bump_version.sh 0.2.2" \
          "./scripts/bump_version.sh patch"
      fi
      echo "$spec"
      ;;
  esac
}

write_files() {
  local p="$1"
  cat > "$ROOT/VERSION" <<EOF
# Product version. \`scuzz -V\` prints this string.
# Keep \`examples/cli/src/Version.scuzz\` product() equal to product=.
# Cut a release with the GitHub release workflow.
product=$p
EOF
  if [ ! -f "$ROOT/examples/cli/src/Version.scuzz" ]; then
    die "missing examples/cli/src/Version.scuzz"
  fi
  awk -v p="$p" '
    $0 == "def product(): String =" { print; getline; print "  \"" p "\""; next }
    /^def product\(\): String = "/ {
      print "def product(): String ="
      print "  \"" p "\""
      next
    }
    { print }
  ' "$ROOT/examples/cli/src/Version.scuzz" > "$ROOT/examples/cli/src/Version.scuzz.tmp"
  mv "$ROOT/examples/cli/src/Version.scuzz.tmp" "$ROOT/examples/cli/src/Version.scuzz"
}

for arg in "$@"; do
  case "$arg" in
    -h|--help)
      usage
      exit 0
      ;;
    --dry-run)
      DRY=1
      ;;
    -*)
      die "unknown flag $arg" \
        "./scripts/bump_version.sh --help"
      ;;
    *)
      if [ -n "$SPEC" ]; then
        die "unexpected extra argument $arg" \
          "./scripts/bump_version.sh --help"
      fi
      SPEC="$arg"
      ;;
  esac
done

if [ -z "$SPEC" ]; then
  die "missing version" \
    "./scripts/bump_version.sh patch" \
    "./scripts/bump_version.sh 0.2.2"
fi

read_product
NEW="$(next_product "$product" "$SPEC")"
if [ "$DRY" = 1 ]; then
  echo "product=$product -> $NEW"
  echo "tag=v$NEW"
  echo "dry-run=1"
  exit 0
fi
write_files "$NEW"
echo "product=$NEW"
echo "tag=v$NEW"

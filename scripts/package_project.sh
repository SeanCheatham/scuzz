#!/usr/bin/env bash
# Emit mobile packaging shells (used by Stage-1/2 `scuzz package`).
set -euo pipefail
project="${1:-.}"
target="${2:-all}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mobile="$ROOT/crates/embedder-mobile"
out="$project/build/package"
name=$(sed -n 's/^name = "\(.*\)"/\1/p' "$project/scuzz.toml" | head -1)
name="${name:-app}"
bundle_id=$(sed -n 's/^bundle_id = "\(.*\)"/\1/p' "$project/scuzz.toml" | head -1)
bundle_id="${bundle_id:-dev.scuzz.app}"
exe="$project/build/$name"

make -C "$mobile" lib
mkdir -p "$out"
targets=()
if [[ "$target" == all ]]; then
  targets=(host android ios)
else
  targets=("$target")
fi

replace_bundle_id() {
  local f="$1"
  if [[ ! -f "$f" ]]; then
    return 0
  fi
  local tmp
  tmp="$(mktemp)"
  # Templates ship with the default id; rewrite when the manifest overrides it.
  sed "s/dev\\.scuzz\\.app/${bundle_id//\//\\/}/g" "$f" >"$tmp"
  mv "$tmp" "$f"
}

for t in "${targets[@]}"; do
  dest="$out/$t"
  rm -rf "$dest"
  mkdir -p "$dest"
  case "$t" in
    host)
      cat >"$dest/run.sh" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export SCUZZ_UI_RUNTIME=mobile
export SCUZZ_MOBILE_SHELL=1
exec "$(cd "$(dirname "$exe")" && pwd)/$(basename "$exe")" "\$@"
EOF
      chmod +x "$dest/run.sh"
      ;;
    android)
      cp -R "$mobile/shells/android/." "$dest/"
      replace_bundle_id "$dest/AndroidManifest.xml"
      ;;
    ios)
      cp -R "$mobile/shells/ios/." "$dest/"
      replace_bundle_id "$dest/Info.plist"
      ;;
    *)
      echo "unknown package target: $t" >&2
      exit 1
      ;;
  esac
  cat >"$dest/package.toml" <<EOF
[package]
name = "$name"
target = "$t"
bundle_id = "$bundle_id"
runtime = "mobile"
EOF
  echo "packaged $t → $dest"
done
echo "scuzz package ok ($out)"

#!/usr/bin/env bash
# Emit mobile packaging shells (used by Stage-1 `scuzz package`).
set -euo pipefail
project="${1:-.}"
target="${2:-all}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mobile="$ROOT/crates/embedder-mobile"
out="$project/build/package"
name=$(sed -n 's/^name = "\(.*\)"/\1/p' "$project/scuzz.toml" | head -1)
name="${name:-app}"
exe="$project/build/$name"

make -C "$mobile" lib
mkdir -p "$out"
targets=()
if [[ "$target" == all ]]; then
  targets=(host android ios)
else
  targets=("$target")
fi

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
      ;;
    ios)
      cp -R "$mobile/shells/ios/." "$dest/"
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
bundle_id = "dev.scuzz.app"
runtime = "mobile"
EOF
  echo "packaged $t → $dest"
done
echo "scuzz package ok ($out)"

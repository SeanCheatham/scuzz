#!/usr/bin/env bash
# Headless golden PNG compare for a ScalUI project (used by Stage-1 `scalui test`).
# Empty goldens/ seeds {name}.png + {name}_after_tap.png.
# SCALUI_UPDATE_GOLDENS=1 (or --update via CLI) rewrites existing goldens from actuals.
set -euo pipefail
project="${1:-.}"
toml="$project/scalui.toml"
goldens="$project/goldens"
[[ -f "$toml" ]] || exit 0
[[ -d "$goldens" ]] || { echo "note: no goldens/"; exit 0; }

name=$(sed -n 's/^name = "\(.*\)"/\1/p' "$toml" | head -1)
name="${name:-app}"
w=$(sed -n 's/.*headless_size = \[\([0-9]*\),.*/\1/p' "$toml" | head -1)
h=$(sed -n 's/.*headless_size = \[[0-9]*, *\([0-9]*\).*/\1/p' "$toml" | head -1)
w="${w:-200}"
h="${h:-120}"
exe="$project/build/$name"
[[ -x "$exe" ]] || { echo "missing executable $exe"; exit 1; }

run_snap() {
  local actual="$1"
  shift
  env SCALUI_UI_RUNTIME=headless \
      SCALUI_SNAPSHOT_PATH="$actual" \
      SCALUI_UI_WIDTH="$w" \
      SCALUI_UI_HEIGHT="$h" \
      "$@" \
      "$exe"
}

seed_goldens() {
  local base="$goldens/${name}.png"
  local tap="$goldens/${name}_after_tap.png"
  run_snap "$base"
  run_snap "$tap" SCALUI_UI_TAP=1
  echo "seeded goldens: ${name}.png ${name}_after_tap.png"
}

shopt -s nullglob
pngs=("$goldens"/*.png)
update="${SCALUI_UPDATE_GOLDENS:-0}"

if [[ ${#pngs[@]} -eq 0 ]]; then
  seed_goldens
  exit 0
fi

for png in "${pngs[@]}"; do
  stem=$(basename "$png" .png)
  actual="$project/build/${stem}.actual.png"
  if [[ "$stem" == *_after_tap ]]; then
    run_snap "$actual" SCALUI_UI_TAP=1
  else
    run_snap "$actual"
  fi
  if [[ "$update" == "1" ]]; then
    cp "$actual" "$png"
    echo "golden updated: $stem"
  else
    cmp "$png" "$actual"
    echo "golden ok: $stem"
  fi
done

#!/usr/bin/env bash
# Headless golden PNG compare for a ScalUI project (used by Stage-1 `scalui test`).
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

shopt -s nullglob
for png in "$goldens"/*.png; do
  stem=$(basename "$png" .png)
  actual="$project/build/${stem}.actual.png"
  tap_env=()
  if [[ "$stem" == *_after_tap ]]; then
    tap_env=(SCALUI_UI_TAP=1)
  fi
  env SCALUI_UI_RUNTIME=headless \
      SCALUI_SNAPSHOT_PATH="$actual" \
      SCALUI_UI_WIDTH="$w" \
      SCALUI_UI_HEIGHT="$h" \
      "${tap_env[@]}" \
      "$exe"
  cmp "$png" "$actual"
  echo "golden ok: $stem"
done

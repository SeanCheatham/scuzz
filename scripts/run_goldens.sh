#!/usr/bin/env bash
# Headless structural goldens (a11y + signal dump) for a ScalUI project.
# Primary artifacts: goldens/{name}.dump and {name}_after_tap.dump
# Optional PNG compare when SCALUI_PIXEL_GOLDENS=1 (or --pixels via CLI).
# Empty dump set seeds structural goldens. SCALUI_UPDATE_GOLDENS=1 rewrites.
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
tap_n=$(sed -n 's/^tap_button = \([0-9][0-9]*\).*/\1/p' "$toml" | head -1)
tap_text=$(sed -n 's/^tap_text = "\(.*\)"/\1/p' "$toml" | head -1)
exe="$project/build/$name"
[[ -x "$exe" ]] || { echo "missing executable $exe"; exit 1; }

run_snap() {
  local actual_png="$1"
  local actual_dump="$2"
  shift 2
  local todo_path
  todo_path="$(mktemp "${TMPDIR:-/tmp}/scalui-todo.XXXXXX")"
  rm -f "$todo_path"
  env SCALUI_UI_RUNTIME=headless \
      SCALUI_SNAPSHOT_PATH="$actual_png" \
      SCALUI_FUZZ_DUMP="$actual_dump" \
      SCALUI_UI_WIDTH="$w" \
      SCALUI_UI_HEIGHT="$h" \
      SCALUI_TODO_PATH="$todo_path" \
      "$@" \
      "$exe"
  rm -f "$todo_path"
}

tap_env() {
  local args=("SCALUI_UI_TAP=1")
  if [[ -n "${tap_n:-}" ]]; then
    args+=("SCALUI_UI_TAP_N=$tap_n")
  fi
  if [[ -n "${tap_text:-}" ]]; then
    args+=("SCALUI_UI_TEXT=$tap_text")
  fi
  echo "${args[@]}"
}

seed_goldens() {
  local base_dump="$goldens/${name}.dump"
  local tap_dump="$goldens/${name}_after_tap.dump"
  local base_png="$project/build/${name}.actual.png"
  local tap_png="$project/build/${name}_after_tap.actual.png"
  run_snap "$base_png" "$base_dump"
  # shellcheck disable=SC2046
  run_snap "$tap_png" "$tap_dump" $(tap_env)
  echo "seeded goldens: ${name}.dump ${name}_after_tap.dump"
  if [[ "${SCALUI_PIXEL_GOLDENS:-0}" == "1" ]]; then
    cp "$base_png" "$goldens/${name}.png"
    cp "$tap_png" "$goldens/${name}_after_tap.png"
    echo "seeded pixel goldens: ${name}.png ${name}_after_tap.png"
  fi
}

shopt -s nullglob
dumps=("$goldens"/*.dump)
update="${SCALUI_UPDATE_GOLDENS:-0}"
pixels="${SCALUI_PIXEL_GOLDENS:-0}"

if [[ ${#dumps[@]} -eq 0 ]]; then
  seed_goldens
  exit 0
fi

for dump in "${dumps[@]}"; do
  stem=$(basename "$dump" .dump)
  actual_dump="$project/build/${stem}.actual.dump"
  actual_png="$project/build/${stem}.actual.png"
  if [[ "$stem" == *_after_tap ]]; then
    # shellcheck disable=SC2046
    run_snap "$actual_png" "$actual_dump" $(tap_env)
  else
    run_snap "$actual_png" "$actual_dump"
  fi
  if [[ "$update" == "1" ]]; then
    cp "$actual_dump" "$dump"
    echo "golden updated: ${stem}.dump"
    if [[ "$pixels" == "1" ]]; then
      cp "$actual_png" "$goldens/${stem}.png"
      echo "golden updated: ${stem}.png"
    fi
  else
    if ! cmp -s "$dump" "$actual_dump"; then
      echo "structural golden mismatch: $dump" >&2
      echo "--- expected ---" >&2
      cat "$dump" >&2
      echo "--- actual ---" >&2
      cat "$actual_dump" >&2
      exit 1
    fi
    echo "golden ok: ${stem}.dump"
    if [[ "$pixels" == "1" && -f "$goldens/${stem}.png" ]]; then
      cmp "$goldens/${stem}.png" "$actual_png"
      echo "golden ok: ${stem}.png"
    fi
  fi
done

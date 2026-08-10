# `scalui.toml` schema (draft)

Working title for the package manifest. Not Maven POM-compatible.

## Example

```toml
[package]
name = "hello"
version = "0.1.0"
description = "ScalUI hello world"

[targets.native]
kind = "executable"
main = "Main"

[dependencies]
# path = "../shared"
# git = { url = "https://example.com/scalui-lib.git", rev = "..." }

[ui]
# default runtime when `scalui run` has a display / shell
default_runtime = "window"   # or "headless" / "mobile"
headless_size = [400, 600]
headless_scale = 1.0
bundle_id = "dev.scalui.hello"  # used by `scalui package`
```

## Fields

### `[package]` (required)

| Key | Type | Notes |
| --- | --- | --- |
| `name` | string | Package name (snake/kebab/ident) |
| `version` | string | Semver-ish |
| `description` | string | Optional |

### `[targets.<name>]`

| Key | Type | Notes |
| --- | --- | --- |
| `kind` | `"executable"` \| `"lib"` | v0: executable |
| `main` | string | Entry module / `@main` symbol hint |

### `[dependencies]`

v0: empty or path deps only. No Maven Central.

### `[ui]`

Optional. Used by `scalui run` / `test` / `package`.

| Key | Type | Notes |
| --- | --- | --- |
| `default_runtime` | `"headless"` \| `"window"` \| `"mobile"` | `scalui run` default; `--headless` forces Headless |
| `headless_size` | `[w, h]` | Logical pixels for Headless / goldens / Mobile host |
| `headless_scale` | float | Recorded on session; raster uses logical size |
| `tap_button` | int (optional) | 0-based button index for `_after_tap` goldens (`SCALUI_UI_TAP_N`) |
| `tap_text` | string (optional) | Text injected before the scripted tap (`SCALUI_UI_TEXT`) — used by TextField apps like Todo |
| `bundle_id` | string | Android package / iOS `CFBundleIdentifier` for `scalui package` |

## Source layout (convention)

```
my-app/
  scalui.toml
  src/
    Main.scala          # *.scala or *.scalui
    Other.scala         # multi-file packages / enums
  .scalui/
    fingerprint         # incremental compile cache (gitignored)
  build/package/        # emitted by `scalui package`
    host/run.sh
    android/
    ios/
```

Stage-0 accepts `*.scala` and `*.scalui` under `src/` (recursive). Units in the same package are merged; exactly one `@main` is required for executables.

# `scuzz.toml` schema

Package manifest for Scuzz Lang projects. Not Maven POM-compatible.

## Example

```toml
[package]
name = "hello"
version = "0.1.0"
description = "Scuzz Lang hello world"

[targets.native]
kind = "executable"
main = "Main"

[dependencies]
# path = "../shared"
# git = { url = "https://example.com/scuzz-lib.git", rev = "..." }

[ui]
# default runtime when `scuzz run` has a display / shell
default_runtime = "window"   # or "headless" / "mobile"
headless_size = [400, 600]
headless_scale = 1.0
bundle_id = "dev.scuzz.hello"  # used by `scuzz package`
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

Optional. Used by `scuzz run` / `test` / `package`.

| Key | Type | Notes |
| --- | --- | --- |
| `default_runtime` | `"headless"` \| `"window"` \| `"mobile"` | `scuzz run` default; `--headless` forces Headless |
| `headless_size` | `[w, h]` | Logical pixels for Headless / goldens / Mobile host |
| `headless_scale` | float | Recorded on session; raster uses logical size |
| `tap_button` | int (optional) | 0-based button index for `_after_tap` goldens (`SCUZZ_UI_TAP_N`) |
| `tap_text` | string (optional) | Text injected before the scripted tap (`SCUZZ_UI_TEXT`) — used by TextField apps like Todo |
| `bundle_id` | string | Android package / iOS `CFBundleIdentifier` for `scuzz package` |

## Source layout (convention)

```
my-app/
  scuzz.toml
  src/
    Main.scala          # *.scala or *.scuzz
    Other.scala         # multi-file packages / enums
  .scuzz/
    fingerprint         # incremental compile cache (gitignored)
  build/package/        # emitted by `scuzz package`
    host/run.sh
    android/
    ios/
```

Stage-0 accepts `*.scala` and `*.scuzz` under `src/` (recursive). Units in the same package are merged; exactly one `@main` is required for executables.

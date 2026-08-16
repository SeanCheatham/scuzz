# `scuzz.toml` schema

Package manifest for Scuzz Lang projects. Data only. Not a plugin DSL. Not Maven POM-compatible. No `[plugins]`. No `build.scuzz` hooks. No sbt-shaped settings.

## Example

```toml
[package]
name = "hello"
version = "0.1.0"
description = "Scuzz Lang hello world"

[dependencies]
shared = { path = "../shared" }

[ui]
# default runtime when `scuzz run` has a display / shell
default_runtime = "desktop"   # or "headless" / "mobile"
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
| `description` | string | Optional prose. The toolchain ignores it. |

### `[dependencies]`

Named local path dependencies only. No git, hosted, version, or registry forms.

```toml
[dependencies]
shared = { path = "../shared" }
utils = { path = "../utils" }
```

- Each entry is an inline table with exactly `path`.
- Paths resolve relative to the directory that contains the declaring `scuzz.toml`.
- Dependencies may declare their own path dependencies (recursive).
- Resolution is deterministic (sorted by dependency name). A package directory is compiled once even if reached through multiple paths.
- Sources from the complete graph are **merged and typechecked as one program** (not separately compiled library artifacts). An executable root needs exactly one `@main`.
- Cycles, missing packages/`src/`, duplicate names, empty paths, and unsupported value shapes are rejected.
- Unknown keys and extra top-level tables are rejected (`git` / `version` on a dependency, `[plugins]`). Do not add `[plugins]` or a settings DSL.

### `[ui]`

Optional. Used by `scuzz run` / `test` / `package`.

| Key | Type | Notes |
| --- | --- | --- |
| `default_runtime` | `"headless"` \| `"desktop"` \| `"mobile"` | `scuzz run` default; `--headless` forces Headless |
| `headless_size` | `[w, h]` | Logical pixels for Headless / goldens / Mobile host |
| `headless_scale` | float | Headless session scale (default 1). Desktop uses OS backing scale when higher. |
| `tap_button` | int (optional) | 0-based button index for `_after_tap` goldens (`SCUZZ_UI_TAP_N`) |
| `tap_text` | string (optional) | Text injected before the scripted tap (`SCUZZ_UI_TEXT`) |
| `bundle_id` | string | Android package / iOS `CFBundleIdentifier` for `scuzz package` |

Invalid `default_runtime`, non-positive `headless_size`, non-positive `headless_scale`, or an empty `bundle_id` fail load.

## Source layout (convention)

```
my-app/
  scuzz.toml
  src/
    Main.scuzz              # live module (*.scuzz under src/; `law` decls erase from live builds)
    Other.scuzz             # multi-file packages / enums
    Main.scuzz_sim          # optional: same-name defs under fuzz / TestRuntime
    Main.scuzz_drivers      # optional: oracle-free IO workloads for scuzz fuzz
  .scuzz/
    fingerprint             # incremental compile cache (gitignored through **/.scuzz/)
  build/package/            # emitted by `scuzz package`
    host/run.sh
    android/<name>.apk                 # debug APK (`scuzz package --target android`)
    android/lib/arm64-v8a/libscuzz.so  # NDK link (same command)
    ios/<name>.app          # signed sim bundle (`scuzz package --target ios`)
```

The compiler accepts `*.scuzz` under `src/` (recursive). Units in the same package are merged. Executables need exactly one `@main`. Stem-paired `*.scuzz_sim` / `*.scuzz_drivers` and in-source `law` decls load under `scuzz check` and verify/fuzz builds. See [vision.md](../vision.md#laws-simulation-mutation-and-verification). `scuzz fmt` and `scuzz check` format-verify only the selected project's `src/` (not dependency trees).

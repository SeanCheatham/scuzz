# `scuzz.toml` schema

Package manifest for Scuzz Lang projects. Data only — not a plugin DSL, not Maven POM-compatible. No `[plugins]`, no `build.scuzz` hooks, no sbt-shaped settings.

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
shared = { path = "../shared" }

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
| `description` | string | Optional prose; ignored by the toolchain today |

### `[targets.<name>]`

| Key | Type | Notes |
| --- | --- | --- |
| `kind` | `"executable"` \| `"lib"` | v0: executable |
| `main` | string | Entry module / `@main` symbol hint |

### `[dependencies]`

v0: named local path dependencies only. No git, hosted, version, or registry forms.

```toml
[dependencies]
shared = { path = "../shared" }
utils = { path = "../utils" }
```

- Each entry is an inline table with exactly `path`.
- Paths resolve relative to the directory containing the declaring `scuzz.toml`.
- Dependencies may declare their own path dependencies (recursive).
- Resolution is deterministic (sorted by dependency name). A package directory is compiled once even if reached through multiple paths.
- Sources from the complete graph are **merged and typechecked as one program** (not separately compiled library artifacts). Exactly one `@main` is required for an executable root.
- Cycles, missing packages/`src/`, duplicate names, empty paths, and unsupported value shapes are rejected.
- Unknown keys and extra top-level tables are rejected (`git` / `version` on a dependency, `[plugins]`, …). Do not add `[plugins]` or a settings DSL.

### `[ui]`

Optional. Used by `scuzz run` / `test` / `package`.

| Key | Type | Notes |
| --- | --- | --- |
| `default_runtime` | `"headless"` \| `"window"` \| `"mobile"` | `scuzz run` default; `--headless` forces Headless |
| `headless_size` | `[w, h]` | Logical pixels for Headless / goldens / Mobile host |
| `headless_scale` | float | Headless session scale (default 1). Window uses OS backing scale when higher. |
| `tap_button` | int (optional) | 0-based button index for `_after_tap` goldens (`SCUZZ_UI_TAP_N`) |
| `tap_text` | string (optional) | Text injected before the scripted tap (`SCUZZ_UI_TEXT`) — used by TextField apps like Todo |
| `bundle_id` | string | Android package / iOS `CFBundleIdentifier` for `scuzz package` |

## Source layout (convention)

```
my-app/
  scuzz.toml
  src/
    Main.scuzz              # live module (*.scuzz under src/)
    Other.scuzz             # multi-file packages / enums
    Main.scuzz_sim          # optional: same-name defs under fuzz / TestRuntime
    Main.scuzz_laws         # optional: pure laws for scuzz fuzz
  .scuzz/
    fingerprint             # incremental compile cache (gitignored via **/.scuzz/)
  build/package/            # emitted by `scuzz package`
    host/run.sh
    android/
    ios/
```

Stage-0 accepts `*.scuzz` under `src/` (recursive). Units in the same package are merged; exactly one `@main` is required for executables. Stem-paired `*.scuzz_sim` / `*.scuzz_laws` load under `scuzz check` and verify/fuzz builds — see [vision.md](../vision.md#laws-simulation-and-verification). `scuzz fmt` and `scuzz check` format-verify only the selected project's `src/` (not dependency trees).

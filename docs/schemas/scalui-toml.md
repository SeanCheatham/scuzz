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
# default runtime when `scalui run` has a display
default_runtime = "window"   # or "headless"
headless_size = [400, 600]
headless_scale = 1.0
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

Optional. Used by `scalui run` / `test` once Phase 1 lands.

## Source layout (convention)

```
my-app/
  scalui.toml
  src/
    Main.scala          # or .scalui — extension TBD; .scala-ish accepted in Stage 0
```

Stage-0 compiler accepts `*.scala` and `*.scalui` under `src/`.

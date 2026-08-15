use serde::de::{self, MapAccess, Visitor};
use serde::{Deserialize, Deserializer};
use std::collections::BTreeMap;
use std::fmt;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub ui: Option<UiConfig>,
    /// Named path dependencies, sorted by name with `BTreeMap`.
    #[serde(default)]
    pub dependencies: BTreeMap<String, PathDependency>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Package {
    pub name: String,
    #[serde(default = "default_version")]
    pub version: String,
    #[serde(default)]
    pub description: String,
}

fn default_version() -> String {
    "0.0.0".into()
}

/// Local path dependency: `{ path = "..." }` only.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PathDependency {
    pub path: String,
}

impl<'de> Deserialize<'de> for PathDependency {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        struct PathDepVisitor;

        impl<'de> Visitor<'de> for PathDepVisitor {
            type Value = PathDependency;

            fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
                write!(f, "inline table {{ path = \"...\" }}")
            }

            fn visit_str<E: de::Error>(self, _v: &str) -> Result<Self::Value, E> {
                Err(de::Error::custom(
                    "string dependencies are unsupported; use `{ path = \"...\" }`",
                ))
            }

            fn visit_string<E: de::Error>(self, v: String) -> Result<Self::Value, E> {
                self.visit_str(&v)
            }

            fn visit_map<M: MapAccess<'de>>(self, mut map: M) -> Result<Self::Value, M::Error> {
                let mut path: Option<String> = None;
                while let Some(key) = map.next_key::<String>()? {
                    if key == "path" {
                        if path.is_some() {
                            return Err(de::Error::custom("duplicate `path` in dependency table"));
                        }
                        path = Some(map.next_value()?);
                    } else {
                        return Err(de::Error::custom(format!(
                            "unsupported dependency key `{key}`; only `path` is supported in v0"
                        )));
                    }
                }
                let path =
                    path.ok_or_else(|| de::Error::custom("dependency requires `path = \"...\"`"))?;
                if path.is_empty() {
                    return Err(de::Error::custom("dependency path must not be empty"));
                }
                Ok(PathDependency { path })
            }
        }

        deserializer.deserialize_any(PathDepVisitor)
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UiConfig {
    /// `"headless"`, `"desktop"`, or `"mobile"` — default runtime for `scuzz run`
    #[serde(default = "default_runtime")]
    pub default_runtime: String,
    /// `[width, height]` for Headless / Mobile goldens
    #[serde(default = "default_headless_size")]
    pub headless_size: Vec<i32>,
    #[serde(default = "default_scale")]
    pub headless_scale: f64,
    /// 0-based DFS button index for `_after_tap` goldens (`SCUZZ_UI_TAP_N`)
    #[serde(default)]
    pub tap_button: Option<i32>,
    /// Text injected before the scripted tap (`SCUZZ_UI_TEXT`)
    #[serde(default)]
    pub tap_text: Option<String>,
    /// Bundle id used by `scuzz package` mobile shells
    #[serde(default = "default_bundle_id")]
    pub bundle_id: String,
}

fn default_bundle_id() -> String {
    "dev.scuzz.app".into()
}

fn default_runtime() -> String {
    "headless".into()
}
fn default_headless_size() -> Vec<i32> {
    vec![200, 120]
}
fn default_scale() -> f64 {
    1.0
}

impl UiConfig {
    pub fn width(&self) -> i32 {
        self.headless_size
            .first()
            .copied()
            .filter(|w| *w > 0)
            .unwrap_or(200)
    }
    pub fn height(&self) -> i32 {
        self.headless_size
            .get(1)
            .copied()
            .filter(|h| *h > 0)
            .unwrap_or(120)
    }
}

pub fn load_manifest(path: &Path) -> anyhow::Result<Manifest> {
    let text = std::fs::read_to_string(path)?;
    parse_manifest(&text)
}

pub fn parse_manifest(text: &str) -> anyhow::Result<Manifest> {
    let value: toml::Value = toml::from_str(text)?;
    reject_unknown_manifest_keys(&value)?;
    let m: Manifest = toml::from_str(text)?;
    validate_manifest(&m)?;
    Ok(m)
}

const KNOWN_TABLES: &str = "package, dependencies, ui";

fn reject_unknown_manifest_keys(value: &toml::Value) -> anyhow::Result<()> {
    let Some(table) = value.as_table() else {
        anyhow::bail!("scuzz.toml must be a table");
    };
    for (key, val) in table {
        match key.as_str() {
            "package" => reject_table_keys(val, "package", &["name", "version", "description"])?,
            "dependencies" => {}
            "ui" => reject_table_keys(
                val,
                "ui",
                &[
                    "default_runtime",
                    "headless_size",
                    "headless_scale",
                    "tap_button",
                    "tap_text",
                    "bundle_id",
                ],
            )?,
            other if val.is_table() => {
                anyhow::bail!("unknown scuzz.toml table [{other}]; known tables: {KNOWN_TABLES}")
            }
            other => anyhow::bail!(
                "unknown scuzz.toml key `{other}` at top level; known tables: {KNOWN_TABLES}"
            ),
        }
    }
    Ok(())
}

fn reject_table_keys(val: &toml::Value, section: &str, known: &[&str]) -> anyhow::Result<()> {
    let Some(table) = val.as_table() else {
        return Ok(());
    };
    for key in table.keys() {
        if !known.contains(&key.as_str()) {
            anyhow::bail!(
                "unknown scuzz.toml key `{key}` in [{section}]; known keys: {}",
                known.join(", ")
            );
        }
    }
    Ok(())
}

fn validate_manifest(m: &Manifest) -> anyhow::Result<()> {
    for (name, dep) in &m.dependencies {
        if name.is_empty() {
            anyhow::bail!("dependency name must not be empty");
        }
        if dep.path.is_empty() {
            anyhow::bail!("dependency `{name}` path must not be empty");
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_named_path_deps() {
        let m = parse_manifest(
            r#"
[package]
name = "app"

[dependencies]
shared = { path = "../shared" }
other = { path = "../other" }
"#,
        )
        .unwrap();
        let names: Vec<_> = m.dependencies.keys().cloned().collect();
        assert_eq!(names, vec!["other".to_string(), "shared".to_string()]);
        assert_eq!(m.dependencies["shared"].path, "../shared");
    }

    #[test]
    fn rejects_string_dependency() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[dependencies]
shared = "../shared"
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("string dependencies are unsupported"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_git_dependency_key() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[dependencies]
shared = { git = "https://example.com/x.git" }
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unsupported dependency key `git`"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_empty_path() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[dependencies]
shared = { path = "" }
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(err.contains("must not be empty"), "unexpected: {err}");
    }

    #[test]
    fn rejects_missing_path_key() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[dependencies]
shared = { }
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(err.contains("requires `path"), "unexpected: {err}");
    }

    #[test]
    fn parses_package_description() {
        let m = parse_manifest(
            r#"
[package]
name = "app"
description = "hello"
"#,
        )
        .unwrap();
        assert_eq!(m.package.description, "hello");
    }

    #[test]
    fn rejects_unknown_top_level_table() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[plugins]
x = 1
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unknown scuzz.toml table [plugins]"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_targets_table() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[targets.native]
kind = "executable"
main = "Main"
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unknown scuzz.toml table [targets]"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_unknown_package_key() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
license = "MIT"
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unknown scuzz.toml key `license` in [package]"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_unknown_ui_key() {
        let err = parse_manifest(
            r#"
[package]
name = "app"
[ui]
hot_reload = true
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unknown scuzz.toml key `hot_reload` in [ui]"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn rejects_unknown_root_key() {
        let err = parse_manifest(
            r#"
edition = "2021"
[package]
name = "app"
"#,
        )
        .unwrap_err()
        .to_string();
        assert!(
            err.contains("unknown scuzz.toml key `edition` at top level"),
            "unexpected: {err}"
        );
    }
}

use serde::de::{self, MapAccess, Visitor};
use serde::{Deserialize, Deserializer};
use std::collections::BTreeMap;
use std::fmt;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub targets: Targets,
    #[serde(default)]
    pub ui: Option<UiConfig>,
    /// Named path dependencies, sorted by name via `BTreeMap`.
    #[serde(default)]
    pub dependencies: BTreeMap<String, PathDependency>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Package {
    pub name: String,
    #[serde(default = "default_version")]
    pub version: String,
}

fn default_version() -> String {
    "0.0.0".into()
}

#[derive(Debug, Clone, Default, Deserialize)]
pub struct Targets {
    #[serde(default)]
    pub native: Option<NativeTarget>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct NativeTarget {
    #[serde(default = "default_kind")]
    pub kind: String,
    #[serde(default = "default_main")]
    pub main: String,
}

fn default_kind() -> String {
    "executable".into()
}
fn default_main() -> String {
    "Main".into()
}

/// v0 local path dependency: `{ path = "..." }` only.
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
pub struct UiConfig {
    /// `"headless"`, `"window"`, or `"mobile"` — default runtime for `scuzz run`
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
        self.headless_size.first().copied().filter(|w| *w > 0).unwrap_or(200)
    }
    pub fn height(&self) -> i32 {
        self.headless_size.get(1).copied().filter(|h| *h > 0).unwrap_or(120)
    }
}

pub fn load_manifest(path: &Path) -> anyhow::Result<Manifest> {
    let text = std::fs::read_to_string(path)?;
    parse_manifest(&text)
}

pub fn parse_manifest(text: &str) -> anyhow::Result<Manifest> {
    let m: Manifest = toml::from_str(text)?;
    validate_manifest(&m)?;
    Ok(m)
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
        assert!(err.contains("unsupported dependency key `git`"), "unexpected: {err}");
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
}

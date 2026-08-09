use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub targets: Targets,
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

pub fn load_manifest(path: &Path) -> anyhow::Result<Manifest> {
    let text = std::fs::read_to_string(path)?;
    let m: Manifest = toml::from_str(&text)?;
    Ok(m)
}

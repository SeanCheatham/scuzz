use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Clone, Deserialize)]
pub struct Manifest {
    pub package: Package,
    #[serde(default)]
    pub targets: Targets,
    #[serde(default)]
    pub ui: Option<UiConfig>,
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
    vec![200, 100]
}
fn default_scale() -> f64 {
    1.0
}

impl UiConfig {
    pub fn width(&self) -> i32 {
        self.headless_size.first().copied().filter(|w| *w > 0).unwrap_or(200)
    }
    pub fn height(&self) -> i32 {
        self.headless_size.get(1).copied().filter(|h| *h > 0).unwrap_or(100)
    }
}

pub fn load_manifest(path: &Path) -> anyhow::Result<Manifest> {
    let text = std::fs::read_to_string(path)?;
    let m: Manifest = toml::from_str(&text)?;
    Ok(m)
}

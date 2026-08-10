//! Parse + lower + typecheck without codegen/link.

use crate::lower::lower_program;
use crate::parser::parse_sources;
use crate::typ::typecheck;
use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: String,
    pub message: String,
    pub file: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
}

impl Diagnostic {
    pub fn error(message: impl Into<String>) -> Self {
        Self {
            severity: "error".into(),
            message: message.into(),
            file: None,
            line: None,
            column: None,
        }
    }

    pub fn with_file(mut self, file: impl Into<String>) -> Self {
        self.file = Some(file.into());
        self
    }

    pub fn to_human(&self) -> String {
        match (&self.file, self.line, self.column) {
            (Some(f), Some(l), Some(c)) => format!("{}:{}:{}: {}: {}", f, l, c, self.severity, self.message),
            (Some(f), Some(l), None) => format!("{}:{}: {}: {}", f, l, self.severity, self.message),
            (Some(f), None, None) => format!("{}: {}: {}", f, self.severity, self.message),
            _ => format!("{}: {}", self.severity, self.message),
        }
    }

    pub fn to_json(&self) -> String {
        let mut out = String::from("{");
        out.push_str(&format!("\"severity\":{}", json_str(&self.severity)));
        out.push_str(&format!(",\"message\":{}", json_str(&self.message)));
        if let Some(f) = &self.file {
            out.push_str(&format!(",\"file\":{}", json_str(f)));
        }
        if let Some(l) = self.line {
            out.push_str(&format!(",\"line\":{l}"));
        }
        if let Some(c) = self.column {
            out.push_str(&format!(",\"column\":{c}"));
        }
        out.push('}');
        out
    }
}

fn json_str(s: &str) -> String {
    let mut out = String::from("\"");
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

pub fn format_diagnostics(diags: &[Diagnostic], json: bool) -> String {
    if json {
        let parts: Vec<String> = diags.iter().map(|d| d.to_json()).collect();
        format!("[{}]", parts.join(","))
    } else {
        diags
            .iter()
            .map(|d| d.to_human())
            .collect::<Vec<_>>()
            .join("\n")
    }
}

/// Parse, lower, and typecheck a project. No LLVM emit or link.
pub fn check_project(project_dir: &Path) -> Result<Vec<Diagnostic>> {
    let manifest_path = project_dir.join("scuzz.toml");
    let _manifest = crate::manifest::load_manifest(&manifest_path)
        .with_context(|| format!("reading {}", manifest_path.display()))?;

    let sources = crate::driver::find_sources(project_dir)?;
    let mut named: Vec<(String, String)> = Vec::new();
    let mut paths: Vec<PathBuf> = Vec::new();
    for path in &sources {
        let text = std::fs::read_to_string(path)
            .with_context(|| format!("reading {}", path.display()))?;
        let label = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("source")
            .to_string();
        named.push((label, text));
        paths.push(path.clone());
    }

    let program = match parse_sources(&named) {
        Ok(p) => p,
        Err(e) => {
            let file = paths
                .first()
                .and_then(|p| p.file_name())
                .and_then(|s| s.to_str())
                .map(|s| s.to_string());
            let mut d = Diagnostic::error(format!("parse error: {e}"));
            if let Some(f) = file {
                d = d.with_file(f);
            }
            return Ok(vec![d]);
        }
    };
    let program = lower_program(program);
    match typecheck(&program) {
        Ok(()) => Ok(vec![]),
        Err(e) => {
            let file = paths
                .iter()
                .find(|p| {
                    p.file_name()
                        .and_then(|s| s.to_str())
                        .map(|s| s == "Main.scala" || s == "main.scala")
                        .unwrap_or(false)
                })
                .or_else(|| paths.first())
                .and_then(|p| p.file_name())
                .and_then(|s| s.to_str())
                .map(|s| s.to_string());
            let mut d = Diagnostic::error(e.to_string());
            if let Some(f) = file {
                d = d.with_file(f);
            }
            Ok(vec![d])
        }
    }
}

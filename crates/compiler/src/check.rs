//! Parse + lower + typecheck without codegen/link.

use crate::lower::lower_program;
use crate::parser::parse_sources;
use crate::typ::typecheck;
use anyhow::{Context, Result};
use std::path::Path;

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
    let resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;

    let named: Vec<(String, String)> = resolved
        .sources
        .iter()
        .map(|s| (s.label.clone(), s.text.clone()))
        .collect();

    let program = match parse_sources(&named) {
        Ok(p) => p,
        Err(e) => {
            let file = resolved.sources.first().map(|s| s.label.clone());
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
            let file = resolved
                .sources
                .iter()
                .find(|s| s.label.contains("/src/Main.scuzz") || s.label.ends_with("Main.scuzz"))
                .or_else(|| resolved.sources.last())
                .map(|s| s.label.clone());
            let mut d = Diagnostic::error(e.to_string());
            if let Some(f) = file {
                d = d.with_file(f);
            }
            Ok(vec![d])
        }
    }
}

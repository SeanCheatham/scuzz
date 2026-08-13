//! Parse + lower + typecheck without codegen/link.

use crate::format::format_source;
use crate::lower::lower_program;
use crate::overlay::{
    apply_overlays, collect_law_names, is_fmt_source, residualize_laws, residualize_refinements,
};
use crate::parser::{parse_sources, ParseError};
use crate::span::{offset_to_line_col, Span};
use crate::typ::{typecheck, TypeError};
use anyhow::{Context, Result};
use std::fs;
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

    pub fn with_loc(mut self, line: u32, column: u32) -> Self {
        self.line = Some(line);
        self.column = Some(column);
        self
    }

    pub fn with_span(mut self, span: &Span, sources: &[(String, String)]) -> Self {
        if !span.file.is_empty() {
            self.file = Some(span.file.clone());
        }
        let text = sources
            .iter()
            .find(|(label, _)| label == &span.file)
            .map(|(_, t)| t.as_str())
            .or_else(|| sources.first().map(|(_, t)| t.as_str()));
        if let Some(src) = text {
            let (line, column) = offset_to_line_col(src, span.start);
            self.line = Some(line);
            self.column = Some(column);
        }
        self
    }

    pub fn to_human(&self) -> String {
        match (&self.file, self.line, self.column) {
            (Some(f), Some(l), Some(c)) => {
                format!("{}:{}:{}: {}: {}", f, l, c, self.severity, self.message)
            }
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

fn named_sources(resolved: &crate::driver::ResolvedProject) -> Vec<(String, String)> {
    resolved
        .sources
        .iter()
        .map(|s| (s.label.clone(), s.text.clone()))
        .collect()
}

fn diagnostic_from_parse(e: ParseError, sources: &[(String, String)]) -> Diagnostic {
    let msg = format!("parse error: {}", e.message());
    let mut d = Diagnostic::error(msg);
    if let Some(span) = e.span() {
        d = d.with_span(span, sources);
    } else if let Some((label, _)) = sources.first() {
        d = d.with_file(label.clone());
    }
    d
}

fn diagnostic_from_type(e: TypeError, sources: &[(String, String)]) -> Diagnostic {
    let msg = e.to_string();
    let mut d = Diagnostic::error(msg);
    if let Some(span) = e.span() {
        d = d.with_span(span, sources);
    } else {
        let file = sources
            .iter()
            .find(|(l, _)| l.contains("/src/Main.scuzz") || l.ends_with("Main.scuzz"))
            .or_else(|| sources.last())
            .map(|(l, _)| l.clone());
        if let Some(f) = file {
            d = d.with_file(f);
        }
    }
    d
}

fn collect_scuzz_files(dir: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    if !dir.is_dir() {
        return Ok(());
    }
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() {
            collect_scuzz_files(&path, out)?;
        } else if is_fmt_source(&path) {
            out.push(path);
        }
    }
    Ok(())
}

fn format_check_src(project_dir: &Path) -> Result<Vec<Diagnostic>> {
    let src = project_dir.join("src");
    let mut files = Vec::new();
    collect_scuzz_files(&src, &mut files)?;
    files.sort();
    let mut diags = Vec::new();
    for p in files {
        let text = fs::read_to_string(&p).with_context(|| format!("reading {}", p.display()))?;
        match format_source(&text) {
            Ok(formatted) if formatted != text => {
                let file = p
                    .strip_prefix(project_dir)
                    .unwrap_or(&p)
                    .display()
                    .to_string();
                diags.push(
                    Diagnostic::error("needs formatting (run scuzz fmt)")
                        .with_file(file)
                        .with_loc(1, 1),
                );
            }
            Ok(_) => {}
            Err(_) => {}
        }
    }
    Ok(diags)
}

/// Format-check `src/`, then parse, lower, and typecheck (live + sim twins + in-source laws).
/// No LLVM emit or link. Format mismatches and type errors share one diagnostic list.
pub fn check_project(project_dir: &Path) -> Result<Vec<Diagnostic>> {
    let mut diags = format_check_src(project_dir)?;
    let resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;

    let named = named_sources(&resolved);

    let program = match parse_sources(&named) {
        Ok(p) => p,
        Err(e) => {
            diags.push(diagnostic_from_parse(e, &named));
            return Ok(diags);
        }
    };
    let program = match apply_overlays(program, &resolved.overlays) {
        Ok(p) => p,
        Err(e) => {
            diags.push(Diagnostic::error(e.to_string()));
            return Ok(diags);
        }
    };
    let law_names = match collect_law_names(&program) {
        Ok(n) => n,
        Err(e) => {
            diags.push(Diagnostic::error(e.to_string()));
            return Ok(diags);
        }
    };
    let mut program = program;
    // Residualize so Law.assert / law calls typecheck the same way as verify builds.
    residualize_laws(&mut program, &law_names);
    residualize_refinements(&mut program);
    program.law_names = law_names;
    let program = lower_program(program);
    if let Err(e) = typecheck(&program) {
        diags.push(diagnostic_from_type(e, &named));
        return Ok(diags);
    }
    match crate::typ::elaborate_generics(program) {
        Ok(_) => Ok(diags),
        Err(e) => {
            diags.push(diagnostic_from_type(e, &named));
            Ok(diags)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;
    use crate::typ::typecheck;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn type_error_reports_line_and_column() {
        let src = "\
@main def main: IO[Unit] =
  IO.println(1)
";
        let prog = lower_program(parse_file(src, "bad.scuzz").unwrap());
        let err = typecheck(&prog).unwrap_err();
        let span = err.span().expect("type error should carry a span");
        assert_eq!(span.file, "bad.scuzz");
        let (line, col) = offset_to_line_col(src, span.start);
        assert!(line >= 2, "expected error on println line, got line {line}");
        assert!(col >= 1);

        let d = diagnostic_from_type(err, &[("bad.scuzz".into(), src.into())]);
        assert_eq!(d.line, Some(line));
        assert_eq!(d.column, Some(col));
        assert_eq!(d.file.as_deref(), Some("bad.scuzz"));
        let json = d.to_json();
        assert!(json.contains(&format!("\"line\":{line}")));
        assert!(json.contains(&format!("\"column\":{col}")));
    }

    #[test]
    fn parse_error_reports_line() {
        let src = "\
@main def main: IO[Unit] =
  IO.println(\"ok\")
  !!!
";
        let err = parse_file(src, "parse_bad.scuzz").unwrap_err();
        let span = err.span().expect("parse error should carry a span");
        let (line, _) = offset_to_line_col(src, span.start);
        assert!(
            line >= 3,
            "expected error near junk tokens, got line {line}"
        );
        let d = diagnostic_from_parse(err, &[("parse_bad.scuzz".into(), src.into())]);
        assert_eq!(d.line, Some(line));
        assert!(d.column.unwrap_or(0) >= 1);
    }

    #[test]
    fn check_project_json_includes_location() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            r#"[package]
name = "diag_test"
version = "0.0.0"
"#,
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        fs::write(
            root.join("src/Main.scuzz"),
            "@main def main: IO[Unit] =\n  IO.println(1)\n",
        )
        .unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1);
        assert!(diags[0].line.unwrap_or(0) >= 2);
        assert!(diags[0].column.unwrap_or(0) >= 1);
        let json = format_diagnostics(&diags, true);
        assert!(json.contains("\"line\":"));
        assert!(json.contains("\"column\":"));
    }

    #[test]
    fn check_project_reports_unformatted() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            r#"[package]
name = "fmt_test"
version = "0.0.0"
"#,
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let dirty = "@main def main: IO[Unit] =\n    IO.println(\"ok\")\n";
        assert_ne!(crate::format::format_source(dirty).unwrap(), dirty);
        fs::write(root.join("src/Main.scuzz"), dirty).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1);
        assert!(diags[0].message.contains("formatting"));
        assert_eq!(diags[0].line, Some(1));
        assert_eq!(diags[0].column, Some(1));
        let json = format_diagnostics(&diags, true);
        assert!(json.contains("needs formatting"));
        assert!(json.contains("src/Main.scuzz"));
    }

    #[test]
    fn check_project_ok_when_formatted() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            r#"[package]
name = "fmt_ok"
version = "0.0.0"
"#,
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.println(\"ok\")\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let diags = check_project(root).unwrap();
        assert!(diags.is_empty(), "{diags:?}");
    }
}

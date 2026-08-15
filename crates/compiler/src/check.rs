//! Parse, lower, and typecheck. No codegen. No link.

use crate::format::format_source;
use crate::lower::lower_program;
use crate::overlay::{
    apply_overlays, check_laws_applied, collect_fmt_sources, collect_law_names, is_fmt_source,
    overlay_kind_from_path, residualize_refinements, OverlaySource,
};
use crate::parser::{parse_sources, parse_sources_recovering, ParseError};
use crate::span::{offset_to_utf16_pos, Span};
use crate::typ::{typecheck_all, TypeError};
use anyhow::{Context, Result};
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: String,
    pub message: String,
    pub file: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
    pub end_line: Option<u32>,
    pub end_column: Option<u32>,
}

impl Diagnostic {
    pub fn error(message: impl Into<String>) -> Self {
        Self {
            severity: "error".into(),
            message: message.into(),
            file: None,
            line: None,
            column: None,
            end_line: None,
            end_column: None,
        }
    }

    pub(crate) fn with_file(mut self, file: impl Into<String>) -> Self {
        self.file = Some(file.into());
        self
    }

    pub(crate) fn with_loc(mut self, line: u32, column: u32) -> Self {
        self.line = Some(line);
        self.column = Some(column);
        self
    }

    pub(crate) fn with_span(mut self, span: &Span, sources: &[(String, String)]) -> Self {
        if !span.file.is_empty() {
            self.file = Some(span.file.clone());
        }
        let text = sources
            .iter()
            .find(|(label, _)| label == &span.file)
            .map(|(_, t)| t.as_str())
            .or_else(|| sources.first().map(|(_, t)| t.as_str()));
        if let Some(src) = text {
            let (line, column) = offset_to_utf16_pos(src, span.start);
            let (end_line, end_column) = offset_to_utf16_pos(src, span.end);
            self.line = Some(line.saturating_add(1));
            self.column = Some(column.saturating_add(1));
            self.end_line = Some(end_line.saturating_add(1));
            self.end_column = Some(end_column.saturating_add(1));
        }
        self
    }

    pub(crate) fn to_human(&self) -> String {
        match (&self.file, self.line, self.column) {
            (Some(f), Some(l), Some(c)) => {
                format!("{}:{}:{}: {}: {}", f, l, c, self.severity, self.message)
            }
            (Some(f), Some(l), None) => format!("{}:{}: {}: {}", f, l, self.severity, self.message),
            (Some(f), None, None) => format!("{}: {}: {}", f, self.severity, self.message),
            _ => format!("{}: {}", self.severity, self.message),
        }
    }

    pub(crate) fn to_json(&self) -> String {
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

pub(crate) fn json_str(s: &str) -> String {
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

/// Canonical path, or parent canonical + file name when the file is not on disk yet.
pub(crate) fn canonicalize_source_path(p: &Path) -> PathBuf {
    if let Ok(c) = fs::canonicalize(p) {
        return c;
    }
    if let Some(parent) = p.parent() {
        if let Ok(cp) = fs::canonicalize(parent) {
            if let Some(name) = p.file_name() {
                return cp.join(name);
            }
        }
    }
    p.to_path_buf()
}

fn lookup_unsaved<'a>(path: &Path, unsaved: &'a BTreeMap<PathBuf, String>) -> Option<&'a String> {
    unsaved.get(path).or_else(|| {
        let key = canonicalize_source_path(path);
        unsaved.get(&key).or_else(|| {
            unsaved
                .iter()
                .find(|(p, _)| canonicalize_source_path(p) == key)
                .map(|(_, t)| t)
        })
    })
}

fn apply_unsaved(
    resolved: &mut crate::driver::ResolvedProject,
    unsaved: &BTreeMap<PathBuf, String>,
    project_dir: &Path,
) {
    if unsaved.is_empty() {
        return;
    }
    let mut matched = BTreeMap::new();
    for src in &mut resolved.sources {
        if let Some(text) = lookup_unsaved(&src.path, unsaved) {
            src.text = text.clone();
            matched.insert(canonicalize_source_path(&src.path), ());
        }
    }
    for ov in &mut resolved.overlays {
        if ov.path.as_os_str().is_empty() {
            continue;
        }
        if let Some(text) = lookup_unsaved(&ov.path, unsaved) {
            ov.text = text.clone();
            matched.insert(canonicalize_source_path(&ov.path), ());
        }
    }
    let root = canonicalize_source_path(project_dir);
    let src_dir = root.join("src");
    let pkg = resolved.root_manifest.package.name.clone();
    for (path, text) in unsaved {
        let key = canonicalize_source_path(path);
        if matched.contains_key(&key) {
            continue;
        }
        if !key.starts_with(&src_dir) {
            continue;
        }
        let rel = key
            .strip_prefix(&root)
            .unwrap_or(&key)
            .to_string_lossy()
            .replace('\\', "/");
        let label = format!("{pkg}/{rel}");
        if let Some((stem, kind)) = overlay_kind_from_path(&key) {
            resolved.overlays.push(OverlaySource {
                stem,
                kind,
                label,
                text: text.clone(),
                path: key,
            });
        } else if key.extension().and_then(|e| e.to_str()) == Some("scuzz") {
            resolved.sources.push(crate::driver::ResolvedSource {
                label,
                path: key,
                text: text.clone(),
            });
        }
    }
}

fn format_check_src(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
) -> Result<Vec<Diagnostic>> {
    let mut files = collect_fmt_sources(&project_dir.join("src"))?;
    for p in unsaved.keys() {
        if is_fmt_source(p)
            && !files
                .iter()
                .any(|f| canonicalize_source_path(f) == canonicalize_source_path(p))
        {
            files.push(p.clone());
        }
    }
    let root = canonicalize_source_path(project_dir);
    let mut diags = Vec::new();
    for p in files {
        let text = match lookup_unsaved(&p, unsaved) {
            Some(t) => t.clone(),
            None => fs::read_to_string(&p).with_context(|| format!("reading {}", p.display()))?,
        };
        match format_source(&text) {
            Ok(formatted) if formatted != text => {
                let file = p.strip_prefix(&root).unwrap_or(&p).display().to_string();
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
/// No LLVM emit. No link. Format mismatches and type errors share one diagnostic list.
pub fn check_project(project_dir: &Path) -> Result<Vec<Diagnostic>> {
    check_project_with(project_dir, &BTreeMap::new())
}

/// Same typer as [`check_project`]. Replace disk text for matching paths (LSP buffers).
pub fn check_project_with(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
) -> Result<Vec<Diagnostic>> {
    let mut diags = format_check_src(project_dir, unsaved)?;
    let mut resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;
    apply_unsaved(&mut resolved, unsaved, project_dir);

    let named = named_sources(&resolved);

    let (maybe_program, parse_errs) = parse_sources_recovering(&named);
    for e in parse_errs {
        diags.push(diagnostic_from_parse(e, &named));
    }
    let Some(program) = maybe_program else {
        return Ok(diags);
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
    if let Err(e) = check_laws_applied(&program, &law_names) {
        diags.push(Diagnostic::error(e.to_string()));
        return Ok(diags);
    }
    let mut program = program;
    // Residualize refinements so Law.check typechecks like verify builds.
    residualize_refinements(&mut program);
    program.law_names = law_names;
    let program = lower_program(program);
    let mut program = program;
    crate::typ::inject_builtin_enums(&mut program.enums);
    let type_errs = typecheck_all(&program);
    let had_type_err = !type_errs.is_empty();
    for e in type_errs {
        diags.push(diagnostic_from_type(e, &named));
    }
    if had_type_err {
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

fn load_overlay_file(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<
    Option<(
        crate::driver::ResolvedProject,
        String,
        String,
        Option<crate::ast::Program>,
    )>,
> {
    let mut resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;
    apply_unsaved(&mut resolved, unsaved, project_dir);
    let key = canonicalize_source_path(path);
    let (label, text) =
        match resolved
            .sources
            .iter()
            .find(|s| canonicalize_source_path(&s.path) == key)
        {
            Some(s) => (s.label.clone(), s.text.clone()),
            None => match resolved.overlays.iter().find(|o| {
                !o.path.as_os_str().is_empty() && canonicalize_source_path(&o.path) == key
            }) {
                Some(o) => (o.label.clone(), o.text.clone()),
                None => return Ok(None),
            },
        };
    let named = named_sources(&resolved);
    let program = parse_sources(&named)
        .ok()
        .and_then(|p| apply_overlays(p, &resolved.overlays).ok());
    Ok(Some((resolved, label, text, program)))
}

/// Signature hover at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn hover_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<String>> {
    let Some((_resolved, label, text, program)) = load_overlay_file(project_dir, unsaved, path)?
    else {
        return Ok(None);
    };
    let Some(program) = program else {
        return Ok(None);
    };
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    Ok(crate::hover::hover_in_source(
        &program, &label, &text, offset,
    ))
}

/// Completions at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn complete_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Vec<crate::complete::Completion>> {
    let Some((_resolved, label, text, program)) = load_overlay_file(project_dir, unsaved, path)?
    else {
        return Ok(Vec::new());
    };
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    Ok(crate::complete::complete_in_source(
        program.as_ref(),
        &label,
        &text,
        offset,
    ))
}

/// Go-to-definition at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn definition_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<(PathBuf, u32, u32, u32, u32)>> {
    let Some((resolved, label, text, program)) = load_overlay_file(project_dir, unsaved, path)?
    else {
        return Ok(None);
    };
    let Some(program) = program else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(loc) =
        crate::definition::definition_in_sources(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    let dest = resolved
        .sources
        .iter()
        .find(|s| s.label == loc.file)
        .map(|s| s.path.clone())
        .or_else(|| {
            resolved
                .overlays
                .iter()
                .find(|o| o.label == loc.file)
                .map(|o| o.path.clone())
        })
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| path.to_path_buf());
    let src = named
        .iter()
        .find(|(l, _)| *l == loc.file)
        .map(|(_, t)| t.as_str())
        .unwrap_or(text.as_str());
    let (sl, sc) = offset_to_utf16_pos(src, loc.start);
    let (el, ec) = offset_to_utf16_pos(src, loc.end);
    Ok(Some((dest, sl, sc, el, ec)))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;
    use crate::span::offset_to_line_col;
    use crate::typ::typecheck;
    use std::fs;
    use std::path::{Path, PathBuf};
    use tempfile::tempdir;

    fn testdata(name: &str) -> PathBuf {
        Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../testdata")
            .join(name)
    }

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
        let diags = check_project(&testdata("typecheck/bad_main")).unwrap();
        assert_eq!(diags.len(), 1);
        assert!(diags[0].line.unwrap_or(0) >= 2);
        assert!(diags[0].column.unwrap_or(0) >= 1);
        let json = format_diagnostics(&diags, true);
        assert!(json.contains("\"line\":"));
        assert!(json.contains("\"column\":"));
    }

    #[test]
    fn check_project_reports_unformatted() {
        let diags = check_project(&testdata("fmt/needs_format")).unwrap();
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

    #[test]
    fn check_project_json_includes_nonexhaustive() {
        let diags = check_project(&testdata("typecheck/nonexhaustive")).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("non-exhaustive"),
            "{}",
            diags[0].message
        );
        let json = format_diagnostics(&diags, true);
        assert!(json.contains("non-exhaustive"));
        assert!(json.contains("\"line\":"));
    }

    fn write_ok_pkg(root: &Path) {
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"unsaved_ok\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.println(\"ok\")\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
    }

    #[test]
    fn check_project_with_overlays_unsaved_type_error() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        assert!(check_project(root).unwrap().is_empty());
        let mut unsaved = BTreeMap::new();
        unsaved.insert(
            canonicalize_source_path(&root.join("src/Main.scuzz")),
            "@main def main: IO[Unit] =\n  IO.println(1 + \"x\")\n".into(),
        );
        let diags = check_project_with(root, &unsaved).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("Int") || diags[0].message.contains("String"),
            "{}",
            diags[0].message
        );
        assert!(check_project(root).unwrap().is_empty());
    }

    #[test]
    fn check_project_with_new_unsaved_module() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let mut unsaved = BTreeMap::new();
        let src = crate::format::format_source("def bad(): String = 1\n").unwrap();
        unsaved.insert(canonicalize_source_path(&root.join("src/Foo.scuzz")), src);
        let diags = check_project_with(root, &unsaved).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("String") || diags[0].message.contains("Int"),
            "{}",
            diags[0].message
        );
    }

    #[test]
    fn hover_project_shows_println() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let text = fs::read_to_string(&path).unwrap();
        let off = text.find("println").unwrap();
        let (line, col) = offset_to_utf16_pos(&text, off);
        let h = hover_project(root, &BTreeMap::new(), &path, line, col)
            .unwrap()
            .expect("hover");
        assert!(h.contains("IO.println"), "{h}");
    }

    #[test]
    fn complete_project_offers_println_after_io_dot() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"complete_ok\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.\n";
        fs::write(root.join("src/Main.scuzz"), src).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let off = src.find("IO.").unwrap() + 3;
        let (line, col) = offset_to_utf16_pos(src, off);
        let items = complete_project(root, &BTreeMap::new(), &path, line, col).unwrap();
        assert!(items.iter().any(|c| c.label == "IO.println"), "{items:?}");
    }

    #[test]
    fn definition_project_jumps_to_def() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"def_ok\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] =\n  IO.println(Str.fromInt(add(1)))\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let call = formatted.rfind("add").unwrap();
        let (line, col) = offset_to_utf16_pos(&formatted, call);
        let (dest, sl, sc, _el, _ec) = definition_project(root, &BTreeMap::new(), &path, line, col)
            .unwrap()
            .expect("definition");
        assert_eq!(canonicalize_source_path(&dest), path);
        let decl = formatted.find("add").unwrap();
        let (dl, dc) = offset_to_utf16_pos(&formatted, decl);
        assert_eq!((sl, sc), (dl, dc));
    }

    #[test]
    fn check_project_reports_two_type_errors() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"multi_err\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "\
def a(): Int = \"x\"
def b(): String = 1
@main def main: IO[Unit] =
  IO.println(\"ok\")
";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let diags = check_project(root).unwrap();
        assert!(
            diags.len() >= 2,
            "expected two type errors, got {}: {:?}",
            diags.len(),
            diags
        );
    }
}

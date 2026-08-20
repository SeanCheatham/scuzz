//! Parse, lower, and typecheck. No codegen. No link.

use crate::format::format_source;
use crate::lower::lower_program;
use crate::overlay::{
    apply_overlays, check_laws_applied, collect_fmt_sources, collect_law_names, is_fmt_source,
    overlay_kind_from_path, residualize_refinements, OverlayError, OverlaySource,
};
use crate::parser::{parse_sources, parse_sources_recovering, ParseError};
use crate::span::{offset_to_utf16_pos, Span};
use crate::typ::{typecheck_all, TypeError};
use anyhow::{Context, Result};
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

/// UTF-16 range: start line, start character, end line, end character.
pub type LspRange = (u32, u32, u32, u32);

/// File path plus a UTF-16 range.
pub type FileRange = (PathBuf, u32, u32, u32, u32);

/// UTF-16 range plus text. Used for code lens titles and rename placeholders.
pub type RangeText = (u32, u32, u32, u32, String);

/// Document link: source UTF-16 range, target file, target UTF-16 range, label.
pub type DocumentLinkLsp = (u32, u32, u32, u32, PathBuf, u32, u32, u32, u32, String);

/// Workspace symbol hit: file, symbol, UTF-16 range.
pub type WorkspaceSymbolHit = (PathBuf, crate::symbols::WorkspaceSymbol, u32, u32, u32, u32);

/// Call hierarchy result: item plus the UTF-16 ranges of its call sites.
pub type CallWithRanges = (CallItemLsp, Vec<LspRange>);

/// Document highlight: UTF-16 range plus kind byte.
pub type HighlightLsp = (u32, u32, u32, u32, u8);

/// Code action: title, kind, UTF-16 range, new text, is-preferred, source diagnostic.
pub type CodeActionLsp = (
    String,
    String,
    u32,
    u32,
    u32,
    u32,
    String,
    bool,
    Option<(String, u32, u32, u32, u32)>,
);

/// Text edit: file, UTF-16 range, new text.
pub type TextEditLsp = (PathBuf, u32, u32, u32, u32, String);

/// Loaded source file: resolved project, label, text, parsed program.
type OverlayFile = (
    crate::driver::ResolvedProject,
    String,
    String,
    Option<crate::ast::Program>,
);

type ParsedFile = (
    crate::driver::ResolvedProject,
    String,
    String,
    crate::ast::Program,
);

#[derive(Debug, Clone)]
pub struct RelatedLoc {
    pub message: String,
    pub file: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
    pub end_line: Option<u32>,
    pub end_column: Option<u32>,
}

impl RelatedLoc {
    fn to_json(&self) -> String {
        let mut out = String::from("{");
        out.push_str(&format!("\"message\":{}", json_str(&self.message)));
        if let Some(f) = &self.file {
            out.push_str(&format!(",\"file\":{}", json_str(f)));
        }
        if let Some(l) = self.line {
            out.push_str(&format!(",\"line\":{l}"));
        }
        if let Some(c) = self.column {
            out.push_str(&format!(",\"column\":{c}"));
        }
        if let Some(l) = self.end_line {
            out.push_str(&format!(",\"end_line\":{l}"));
        }
        if let Some(c) = self.end_column {
            out.push_str(&format!(",\"end_column\":{c}"));
        }
        out.push('}');
        out
    }
}

#[derive(Debug, Clone)]
pub struct Diagnostic {
    pub severity: String,
    pub message: String,
    pub file: Option<String>,
    pub line: Option<u32>,
    pub column: Option<u32>,
    pub end_line: Option<u32>,
    pub end_column: Option<u32>,
    pub related: Vec<RelatedLoc>,
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
            related: Vec::new(),
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
        if let Some(l) = self.end_line {
            out.push_str(&format!(",\"end_line\":{l}"));
        }
        if let Some(c) = self.end_column {
            out.push_str(&format!(",\"end_column\":{c}"));
        }
        if !self.related.is_empty() {
            let parts: Vec<String> = self.related.iter().map(|r| r.to_json()).collect();
            out.push_str(&format!(",\"related\":[{}]", parts.join(",")));
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

fn live_named_sources(resolved: &crate::driver::ResolvedProject) -> Vec<(String, String)> {
    resolved
        .sources
        .iter()
        .map(|s| (s.label.clone(), s.text.clone()))
        .collect()
}

fn named_sources(resolved: &crate::driver::ResolvedProject) -> Vec<(String, String)> {
    let mut out = live_named_sources(resolved);
    for ov in &resolved.overlays {
        out.push((ov.label.clone(), ov.text.clone()));
    }
    out
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
            .find(|(l, _)| Path::new(l).file_name().and_then(|n| n.to_str()) == Some("Main.scuzz"))
            .or_else(|| sources.first())
            .map(|(l, _)| l.clone());
        if let Some(f) = file {
            d = d.with_file(f);
        }
    }
    d
}

fn attach_related(d: &mut Diagnostic, program: &crate::ast::Program, sources: &[(String, String)]) {
    let msg = d
        .message
        .strip_prefix("type error: ")
        .unwrap_or(d.message.as_str());
    if let Some(name) = msg.strip_prefix("unknown function ") {
        let cands = crate::action::callee_candidates(program);
        let Some(fix) = crate::action::closest_callee(name, &cands) else {
            return;
        };
        let hint = format!("did you mean `{fix}`");
        if let Some(rel) = related_from_name(program, sources, &fix, &hint) {
            d.related.push(rel);
        } else {
            let rel = related_from_diag(d, &hint);
            d.related.push(rel);
        }
        return;
    }
    let Some(rest) = msg.strip_prefix("non-exhaustive match: missing ") else {
        return;
    };
    let first = rest.split(", ").next().unwrap_or("");
    let enum_name = first.split('.').next().unwrap_or("");
    if enum_name.is_empty() {
        return;
    }
    let Some(en) = crate::hover::unique_enum(program, enum_name) else {
        return;
    };
    let Some(loc) = crate::definition::loc_for_def(
        sources,
        en.module.as_str(),
        crate::definition::DeclKind::Enum,
        &en.name,
    ) else {
        return;
    };
    d.related
        .push(related_from_def(sources, loc, format!("enum {}", en.name)));
}

fn related_from_name(
    program: &crate::ast::Program,
    sources: &[(String, String)],
    name: &str,
    message: &str,
) -> Option<RelatedLoc> {
    if let Some((module, bare)) = name.rsplit_once('.') {
        if let Some(d) = crate::hover::def_named(program, module, bare) {
            let loc = crate::definition::loc_for_def(
                sources,
                d.module.as_str(),
                crate::definition::DeclKind::Def,
                &d.name,
            )?;
            return Some(related_from_def(sources, loc, message.to_string()));
        }
    }
    let d = crate::hover::unique_def(program, name)?;
    let loc = crate::definition::loc_for_def(
        sources,
        d.module.as_str(),
        crate::definition::DeclKind::Def,
        &d.name,
    )?;
    Some(related_from_def(sources, loc, message.to_string()))
}

fn related_from_diag(d: &Diagnostic, message: &str) -> RelatedLoc {
    RelatedLoc {
        message: message.to_string(),
        file: d.file.clone(),
        line: d.line,
        column: d.column,
        end_line: d.end_line,
        end_column: d.end_column,
    }
}

fn related_from_def(
    sources: &[(String, String)],
    loc: crate::definition::DefLoc,
    message: String,
) -> RelatedLoc {
    let text = named_text(sources, &loc.file, "");
    let (line, column, end_line, end_column) = utf16_range(text, loc.start, loc.end);
    RelatedLoc {
        message,
        file: Some(loc.file),
        line: Some(line.saturating_add(1)),
        column: Some(column.saturating_add(1)),
        end_line: Some(end_line.saturating_add(1)),
        end_column: Some(end_column.saturating_add(1)),
    }
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

/// Resolve a diagnostic file label to a path under the project (overlay or disk).
pub(crate) fn diagnostic_source_path(project_dir: &Path, file: &str) -> PathBuf {
    let p = Path::new(file);
    if p.is_absolute() {
        return p.to_path_buf();
    }
    let joined = project_dir.join(file);
    if joined.exists() {
        return joined;
    }
    if let Some(idx) = file.find("/src/") {
        return project_dir.join(&file[idx + 1..]);
    }
    if let Some(stripped) = file.strip_prefix("src/") {
        return project_dir.join("src").join(stripped);
    }
    project_dir.join("src").join(file)
}

fn diagnostic_for_path(project_dir: &Path, d: &Diagnostic, path: &Path) -> bool {
    let Some(file) = &d.file else {
        return false;
    };
    canonicalize_source_path(&diagnostic_source_path(project_dir, file))
        == canonicalize_source_path(path)
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

fn diagnostic_from_overlay(e: OverlayError, sources: &[(String, String)]) -> Diagnostic {
    match e {
        OverlayError::Parse(p) => diagnostic_from_parse(p, sources),
        OverlayError::Msg(m) => Diagnostic::error(m),
    }
}

fn diag_dup(a: &Diagnostic, b: &Diagnostic) -> bool {
    a.file == b.file && a.line == b.line && a.column == b.column && a.message == b.message
}

fn merge_diags(into: &mut Vec<Diagnostic>, extra: Vec<Diagnostic>) {
    for d in extra {
        if !into.iter().any(|x| diag_dup(x, &d)) {
            into.push(d);
        }
    }
}

fn check_typed_graph(
    program: crate::ast::Program,
    named: &[(String, String)],
    residualize: bool,
) -> Vec<Diagnostic> {
    let mut diags = Vec::new();
    let unused = crate::resolve::unused_names(&program);
    let unused_imports: Vec<crate::ast::Import> = crate::resolve::unused_imports(&program)
        .into_iter()
        .cloned()
        .collect();
    let lowered = lower_program(program);
    let program = match crate::typ::expand_impls(lowered.clone()) {
        Ok(p) => p,
        Err(e) => {
            diags.push(diagnostic_from_type(e, named));
            lowered
        }
    };
    let mut program = match crate::typ::resolve_named_args(program) {
        Ok(p) => p,
        Err(e) => {
            diags.push(diagnostic_from_type(e, named));
            return diags;
        }
    };
    if residualize {
        residualize_refinements(&mut program);
    }
    let type_errs = typecheck_all(&program);
    let had_type_err = !type_errs.is_empty();
    for e in type_errs {
        let mut d = diagnostic_from_type(e, named);
        attach_related(&mut d, &program, named);
        diags.push(d);
    }
    if had_type_err {
        return diags;
    }
    for im in &unused_imports {
        let msg = if im.is_wildcard() {
            format!("unused import {}.{}", im.from_module, im.name)
        } else if let Some(alias) = &im.alias {
            format!("unused import {}.{} as {alias}", im.from_module, im.name)
        } else {
            format!("unused import {}.{}", im.from_module, im.name)
        };
        diags.push(Diagnostic::error(msg).with_span(&im.span, named));
    }
    for u in unused {
        diags.push(Diagnostic::error(u.message()).with_span(&u.span, named));
    }
    match crate::typ::elaborate_generics(program) {
        Ok(_) => diags,
        Err(e) => {
            diags.push(diagnostic_from_type(e, named));
            diags
        }
    }
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
    let live_named = live_named_sources(&resolved);

    let (maybe_program, parse_errs) = parse_sources_recovering(&live_named);
    for e in parse_errs {
        diags.push(diagnostic_from_parse(e, &named));
    }
    let Some(program) = maybe_program else {
        return Ok(diags);
    };
    let live = program;
    let law_names = match collect_law_names(&live) {
        Ok(n) => n,
        Err(e) => {
            diags.push(diagnostic_from_overlay(e, &named));
            Vec::new()
        }
    };
    if let Err(e) = check_laws_applied(&live, &law_names) {
        diags.push(diagnostic_from_overlay(e, &named));
    }
    let mut live_prog = live.clone();
    live_prog.law_names = law_names.clone();
    merge_diags(&mut diags, check_typed_graph(live_prog, &named, false));

    match apply_overlays(live, &resolved.overlays) {
        Ok(mut program) => {
            if let Err(e) = check_laws_applied(&program, &law_names) {
                diags.push(diagnostic_from_overlay(e, &named));
            }
            program.law_names = law_names;
            merge_diags(&mut diags, check_typed_graph(program, &named, true));
        }
        Err(e) => {
            diags.push(diagnostic_from_overlay(e, &named));
        }
    }
    Ok(diags)
}

fn load_overlay_file(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Option<OverlayFile>> {
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
    let live_named = live_named_sources(&resolved);
    let program = parse_sources(&live_named)
        .ok()
        .and_then(|p| apply_overlays(p, &resolved.overlays).ok());
    Ok(Some((resolved, label, text, program)))
}

fn load_parsed(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Option<ParsedFile>> {
    Ok(load_overlay_file(project_dir, unsaved, path)?
        .and_then(|(resolved, label, text, program)| program.map(|p| (resolved, label, text, p))))
}

fn named_text<'a>(named: &'a [(String, String)], file: &str, fallback: &'a str) -> &'a str {
    named
        .iter()
        .find(|(l, _)| *l == file)
        .map(|(_, t)| t.as_str())
        .unwrap_or(fallback)
}

fn utf16_range(text: &str, start: usize, end: usize) -> LspRange {
    let (sl, sc) = offset_to_utf16_pos(text, start);
    let (el, ec) = offset_to_utf16_pos(text, end);
    (sl, sc, el, ec)
}

/// Signature hover at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn hover_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<String>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
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
) -> Result<Option<FileRange>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(loc) =
        crate::definition::definition_in_sources(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    Ok(Some(loc_to_lsp(&resolved, path, &named, loc, &text)))
}

/// Go-to-declaration at a 0-based LSP position. Same parse as [`check_project_with`].
/// An imported name jumps to the import. Other names jump to the def.
pub fn declaration_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<FileRange>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(loc) =
        crate::definition::declaration_in_sources(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    Ok(Some(loc_to_lsp(&resolved, path, &named, loc, &text)))
}

/// Go-to-type-definition at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn type_definition_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<FileRange>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(loc) =
        crate::definition::type_definition_in_sources(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    Ok(Some(loc_to_lsp(&resolved, path, &named, loc, &text)))
}

/// Go-to-implementation at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn implementation_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Vec<FileRange>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let locs = crate::implement::implementation_in_sources(&program, &named, &label, &text, offset);
    Ok(locs
        .into_iter()
        .map(|loc| loc_to_lsp(&resolved, path, &named, loc, &text))
        .collect())
}

/// Code lenses in a file. Same parse as [`check_project_with`].
pub fn code_lenses_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<RangeText>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let lenses = crate::lens::code_lenses_in_source(&program, &named, &label, &text);
    Ok(lenses
        .into_iter()
        .map(|l| {
            let (sl, sc) = offset_to_utf16_pos(&text, l.start);
            let (el, ec) = offset_to_utf16_pos(&text, l.end);
            (sl, sc, el, ec, l.title)
        })
        .collect())
}

/// Document links for `import Module.name` in a file. Same parse as [`check_project_with`].
pub fn document_links_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<DocumentLinkLsp>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let links = crate::links::document_links_in_source(&program, &named, &label);
    Ok(links
        .into_iter()
        .map(|l| {
            let (sl, sc) = offset_to_utf16_pos(&text, l.start);
            let (el, ec) = offset_to_utf16_pos(&text, l.end);
            let dest = loc_to_lsp(
                &resolved,
                path,
                &named,
                crate::definition::DefLoc {
                    file: l.dest_file,
                    start: l.dest_start,
                    end: l.dest_end,
                },
                &text,
            );
            (
                sl, sc, el, ec, dest.0, dest.1, dest.2, dest.3, dest.4, l.tooltip,
            )
        })
        .collect())
}

fn loc_to_lsp(
    resolved: &crate::driver::ResolvedProject,
    path: &Path,
    named: &[(String, String)],
    loc: crate::definition::DefLoc,
    fallback_text: &str,
) -> FileRange {
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
    let src = named_text(named, &loc.file, fallback_text);
    let (sl, sc, el, ec) = utf16_range(src, loc.start, loc.end);
    (dest, sl, sc, el, ec)
}

/// Document outline at a file. Same parse as [`check_project_with`].
pub fn symbols_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<crate::symbols::DocSymbol>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    Ok(crate::symbols::symbols_in_source(&program, &label, &text))
}

/// Workspace outline. Same parse as [`check_project_with`].
pub fn workspace_symbols_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    query: &str,
) -> Result<Vec<WorkspaceSymbolHit>> {
    let mut resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;
    apply_unsaved(&mut resolved, unsaved, project_dir);
    let named = named_sources(&resolved);
    let Some(program) = parse_sources(&named)
        .ok()
        .and_then(|p| apply_overlays(p, &resolved.overlays).ok())
    else {
        return Ok(Vec::new());
    };
    let mut out = Vec::new();
    for src in &resolved.sources {
        if src.path.as_os_str().is_empty() {
            continue;
        }
        let hits =
            crate::symbols::workspace_symbols_in_source(&program, &src.label, &src.text, query);
        for h in hits {
            let (sl, sc) = offset_to_utf16_pos(&src.text, h.start);
            let (el, ec) = offset_to_utf16_pos(&src.text, h.end);
            out.push((src.path.clone(), h, sl, sc, el, ec));
        }
    }
    Ok(out)
}

/// Signature help at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn signature_help_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<crate::signature::SigHelp>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    Ok(crate::signature::signature_help_in_source(
        &program, &label, &text, offset,
    ))
}

/// Find-references at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn references_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
    include_declaration: bool,
) -> Result<Vec<FileRange>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let refs = crate::references::references_in_sources(
        &program,
        &named,
        &label,
        &text,
        offset,
        include_declaration,
    );
    Ok(refs
        .into_iter()
        .map(|loc| loc_to_lsp(&resolved, path, &named, loc, &text))
        .collect())
}

#[derive(Debug, Clone)]
pub struct CallItemLsp {
    pub path: PathBuf,
    pub name: String,
    pub sl: u32,
    pub sc: u32,
    pub el: u32,
    pub ec: u32,
    pub ssl: u32,
    pub ssc: u32,
    pub sel: u32,
    pub sec: u32,
}

fn hierarchy_item_to_lsp(
    resolved: &crate::driver::ResolvedProject,
    path: &Path,
    named: &[(String, String)],
    item: crate::hierarchy::HierarchyItem,
    fallback_text: &str,
) -> CallItemLsp {
    let loc = crate::definition::DefLoc {
        file: item.file.clone(),
        start: item.sel_start,
        end: item.sel_end,
    };
    let (dest, _, _, _, _) = loc_to_lsp(resolved, path, named, loc, fallback_text);
    let src = named_text(named, &item.file, fallback_text);
    let (sl, sc, el, ec) = utf16_range(src, item.range_start, item.range_end);
    let (ssl, ssc, sel, sec) = utf16_range(src, item.sel_start, item.sel_end);
    CallItemLsp {
        path: dest,
        name: item.name,
        sl,
        sc,
        el,
        ec,
        ssl,
        ssc,
        sel,
        sec,
    }
}

/// Prepare call hierarchy at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn prepare_call_hierarchy_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<CallItemLsp>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(item) =
        crate::hierarchy::prepare_call_hierarchy(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    Ok(Some(hierarchy_item_to_lsp(
        &resolved, path, &named, item, &text,
    )))
}

/// Incoming calls at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn incoming_calls_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Vec<CallWithRanges>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let calls = crate::hierarchy::incoming_calls(&program, &named, &label, &text, offset);
    Ok(calls
        .into_iter()
        .map(|c| {
            let from_text = named_text(&named, &c.from.file, &text);
            let ranges = c
                .from_ranges
                .into_iter()
                .map(|(a, b)| utf16_range(from_text, a, b))
                .collect();
            let item = hierarchy_item_to_lsp(&resolved, path, &named, c.from, &text);
            (item, ranges)
        })
        .collect())
}

/// Outgoing calls at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn outgoing_calls_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Vec<CallWithRanges>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let calls = crate::hierarchy::outgoing_calls(&program, &named, &label, &text, offset);
    Ok(calls
        .into_iter()
        .map(|c| {
            let ranges = c
                .from_ranges
                .into_iter()
                .map(|(a, b)| utf16_range(&text, a, b))
                .collect();
            let item = hierarchy_item_to_lsp(&resolved, path, &named, c.to, &text);
            (item, ranges)
        })
        .collect())
}

#[derive(Debug, Clone)]
pub struct TypeItemLsp {
    pub path: PathBuf,
    pub name: String,
    pub kind: u8,
    pub detail: String,
    pub sl: u32,
    pub sc: u32,
    pub el: u32,
    pub ec: u32,
}

fn type_item_to_lsp(
    resolved: &crate::driver::ResolvedProject,
    path: &Path,
    named: &[(String, String)],
    item: crate::typehier::TypeItem,
    fallback_text: &str,
) -> TypeItemLsp {
    let loc = crate::definition::DefLoc {
        file: item.file.clone(),
        start: item.sel_start,
        end: item.sel_end,
    };
    let (dest, sl, sc, el, ec) = loc_to_lsp(resolved, path, named, loc, fallback_text);
    TypeItemLsp {
        path: dest,
        name: item.name,
        kind: item.kind,
        detail: item.detail,
        sl,
        sc,
        el,
        ec,
    }
}

/// Type hierarchy items at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn type_items_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
    f: crate::typehier::TypeQuery,
) -> Result<Vec<TypeItemLsp>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    Ok(f(&program, &named, &label, &text, offset)
        .into_iter()
        .map(|item| type_item_to_lsp(&resolved, path, &named, item, &text))
        .collect())
}

/// Document highlights at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn highlights_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Vec<HighlightLsp>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let hits = crate::highlight::highlights_in_source(&program, &named, &label, &text, offset);
    Ok(hits
        .into_iter()
        .map(|h| {
            let (sl, sc) = offset_to_utf16_pos(&text, h.start);
            let (el, ec) = offset_to_utf16_pos(&text, h.end);
            (sl, sc, el, ec, h.kind)
        })
        .collect())
}

/// Folding ranges for a file. Same parse as [`check_project_with`].
pub fn folding_ranges_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<(u32, u32, u32, u32)>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let folds = crate::fold::folds_in_source(&program, &label, &text);
    Ok(folds
        .into_iter()
        .map(|f| {
            let (sl, sc) = offset_to_utf16_pos(&text, f.start);
            let (el, ec) = offset_to_utf16_pos(&text, f.end);
            (sl, sc, el, ec)
        })
        .collect())
}

/// Selection ranges at 0-based LSP positions. Same parse as [`check_project_with`].
pub fn selection_ranges_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    positions: &[(u32, u32)],
) -> Result<Vec<Vec<LspRange>>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let mut out = Vec::new();
    for (line, character) in positions {
        let offset = crate::span::utf16_pos_to_offset(&text, *line, *character);
        let ranges = crate::select::selection_ranges_in_source(&program, &label, &text, offset);
        out.push(
            ranges
                .into_iter()
                .map(|r| {
                    let (sl, sc) = offset_to_utf16_pos(&text, r.start);
                    let (el, ec) = offset_to_utf16_pos(&text, r.end);
                    (sl, sc, el, ec)
                })
                .collect(),
        );
    }
    Ok(out)
}

/// Parameter inlay hints in a file. Same parse as [`check_project_with`].
pub fn inlay_hints_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    range: Option<((u32, u32), (u32, u32))>,
) -> Result<Vec<(u32, u32, String, u8)>> {
    let Some((_resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let byte_range = range.map(|((sl, sc), (el, ec))| {
        (
            crate::span::utf16_pos_to_offset(&text, sl, sc),
            crate::span::utf16_pos_to_offset(&text, el, ec),
        )
    });
    let hints = crate::inlay::inlay_hints_in_source(&program, &label, byte_range);
    Ok(hints
        .into_iter()
        .map(|h| {
            let (line, character) = offset_to_utf16_pos(&text, h.offset);
            (line, character, h.label, h.kind)
        })
        .collect())
}

/// Semantic tokens for a file. Same parse as [`check_project_with`].
pub fn semantic_tokens_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<u32>> {
    let Some((_resolved, _label, text, _)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let toks = crate::tokens::semantic_tokens_in_source(&text);
    Ok(crate::tokens::encode_semantic_tokens(&text, &toks))
}

/// Semantic tokens whose start sits in `range`. Deltas are relative to the range start.
pub fn semantic_tokens_range_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    range: Option<((u32, u32), (u32, u32))>,
) -> Result<Vec<u32>> {
    let Some((_resolved, _label, text, _)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(Vec::new());
    };
    let Some(((sl, sc), (el, ec))) = range else {
        return Ok(Vec::new());
    };
    let start = crate::span::utf16_pos_to_offset(&text, sl, sc);
    let end = crate::span::utf16_pos_to_offset(&text, el, ec);
    let toks = crate::tokens::semantic_tokens_in_range(&text, start, end);
    Ok(crate::tokens::encode_semantic_tokens_from(
        &text, &toks, sl, sc,
    ))
}

/// Code actions in a file. Same parse as [`check_project_with`].
/// Optional last field is the check diagnostic this fix addresses.
pub fn code_actions_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    range: Option<((u32, u32), (u32, u32))>,
    only: &[String],
) -> Result<Vec<CodeActionLsp>> {
    let Some((_resolved, label, text, program)) = load_overlay_file(project_dir, unsaved, path)?
    else {
        return Ok(Vec::new());
    };
    let byte_range = range.map(|((sl, sc), (el, ec))| {
        (
            crate::span::utf16_pos_to_offset(&text, sl, sc),
            crate::span::utf16_pos_to_offset(&text, el, ec),
        )
    });
    let acts =
        crate::action::code_actions_in_source(program.as_ref(), &label, &text, byte_range, only);
    Ok(acts
        .into_iter()
        .map(|a| {
            let (sl, sc) = offset_to_utf16_pos(&text, a.start);
            let (el, ec) = offset_to_utf16_pos(&text, a.end);
            let diagnostic = a.diagnostic.map(|(msg, ds, de)| {
                let (dsl, dsc) = offset_to_utf16_pos(&text, ds);
                let (del, dec) = offset_to_utf16_pos(&text, de);
                (msg, dsl, dsc, del, dec)
            });
            (
                a.title,
                a.kind,
                sl,
                sc,
                el,
                ec,
                a.new_text,
                a.preferred,
                diagnostic,
            )
        })
        .collect())
}

/// Diagnostics for one file. Same check as [`check_project_with`].
pub fn document_diagnostics_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
) -> Result<Vec<Diagnostic>> {
    let all = check_project_with(project_dir, unsaved)?;
    Ok(all
        .into_iter()
        .filter(|d| diagnostic_for_path(project_dir, d, path))
        .collect())
}

/// Diagnostics for every `src/` file. Same check as [`check_project_with`].
pub fn workspace_diagnostics_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
) -> Result<Vec<(PathBuf, Vec<Diagnostic>)>> {
    let all = check_project_with(project_dir, unsaved)?;
    let mut by_path: BTreeMap<PathBuf, Vec<Diagnostic>> = BTreeMap::new();
    if let Ok(files) = collect_fmt_sources(&project_dir.join("src")) {
        for p in files {
            by_path.entry(canonicalize_source_path(&p)).or_default();
        }
    }
    for p in unsaved.keys() {
        by_path.entry(canonicalize_source_path(p)).or_default();
    }
    for d in all {
        let Some(file) = &d.file else {
            continue;
        };
        let path = canonicalize_source_path(&diagnostic_source_path(project_dir, file));
        by_path.entry(path).or_default().push(d);
    }
    Ok(by_path.into_iter().collect())
}

pub enum RenameResult {
    Unavailable,
    BadName,
    Edits(Vec<FileRange>),
}

/// Ident range at a 0-based LSP position when rename is allowed.
pub fn prepare_rename_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
) -> Result<Option<RangeText>> {
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(None);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some((start, end, name)) =
        crate::rename::prepare_rename_in_sources(&program, &named, &label, &text, offset)
    else {
        return Ok(None);
    };
    let (sl, sc) = offset_to_utf16_pos(&text, start);
    let (el, ec) = offset_to_utf16_pos(&text, end);
    Ok(Some((sl, sc, el, ec, name)))
}

/// Rename edits at a 0-based LSP position. Same parse as [`check_project_with`].
pub fn rename_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    path: &Path,
    line: u32,
    character: u32,
    new_name: &str,
) -> Result<RenameResult> {
    if !crate::rename::is_rename_ident(new_name) {
        return Ok(RenameResult::BadName);
    }
    let Some((resolved, label, text, program)) = load_parsed(project_dir, unsaved, path)? else {
        return Ok(RenameResult::Unavailable);
    };
    let named = named_sources(&resolved);
    let offset = crate::span::utf16_pos_to_offset(&text, line, character);
    let Some(edits) =
        crate::rename::rename_in_sources(&program, &named, &label, &text, offset, new_name)
    else {
        return Ok(RenameResult::Unavailable);
    };
    Ok(RenameResult::Edits(
        edits
            .into_iter()
            .map(|e| {
                loc_to_lsp(
                    &resolved,
                    path,
                    &named,
                    crate::definition::DefLoc {
                        file: e.file,
                        start: e.start,
                        end: e.end,
                    },
                    &text,
                )
            })
            .collect(),
    ))
}

/// Text edits for `workspace/willRenameFiles`. Same parse as [`check_project_with`].
pub fn will_rename_files_project(
    project_dir: &Path,
    unsaved: &BTreeMap<PathBuf, String>,
    files: &[(PathBuf, PathBuf)],
) -> Result<Vec<TextEditLsp>> {
    let mut resolved = crate::driver::resolve_project(project_dir)
        .with_context(|| format!("resolving {}", project_dir.display()))?;
    apply_unsaved(&mut resolved, unsaved, project_dir);
    let named = named_sources(&resolved);
    let program = crate::parser::parse_sources(&named).ok();
    let mut out = Vec::new();
    for (old_path, new_path) in files {
        let old_path = canonicalize_source_path(old_path);
        let new_path = canonicalize_source_path(new_path);
        if old_path.extension().and_then(|e| e.to_str()) != Some("scuzz")
            || new_path.extension().and_then(|e| e.to_str()) != Some("scuzz")
        {
            continue;
        }
        let old_mod = crate::resolve::module_id_from_label(&old_path.to_string_lossy());
        let new_mod = crate::resolve::module_id_from_label(&new_path.to_string_lossy());
        if old_mod.is_empty()
            || old_mod == new_mod
            || crate::rename::is_kit_module(&old_mod)
            || !crate::rename::is_rename_ident(&new_mod)
        {
            continue;
        }
        let is_src = resolved
            .sources
            .iter()
            .any(|s| canonicalize_source_path(&s.path) == old_path)
            || unsaved
                .keys()
                .any(|p| canonicalize_source_path(p) == old_path);
        if !is_src {
            continue;
        }
        for src in &resolved.sources {
            for (start, end) in
                crate::rename::module_ident_spans(&src.text, &old_mod, program.as_ref())
            {
                let (sl, sc) = offset_to_utf16_pos(&src.text, start);
                let (el, ec) = offset_to_utf16_pos(&src.text, end);
                out.push((
                    canonicalize_source_path(&src.path),
                    sl,
                    sc,
                    el,
                    ec,
                    new_mod.clone(),
                ));
            }
        }
    }
    out.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)).then(a.2.cmp(&b.2)));
    out.dedup();
    Ok(out)
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
        assert!(json.contains("\"end_line\":"), "{json}");
        assert!(json.contains("\"end_column\":"), "{json}");
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
        assert!(json.contains("\"related\""), "{json}");
        assert!(json.contains("enum Color"), "{json}");
        assert_eq!(diags[0].related.len(), 1, "{:?}", diags[0].related);
        assert_eq!(diags[0].related[0].message, "enum Color");
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

    fn write_sim_pkg(root: &Path, live: &str, sim: &str) {
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"sim_check\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let live = crate::format::format_source(live).unwrap();
        let sim = crate::format::format_source(sim).unwrap();
        fs::write(root.join("src/Main.scuzz"), live).unwrap();
        fs::write(root.join("src/Main.scuzz_sim"), sim).unwrap();
    }

    #[test]
    fn check_project_sim_type_error_points_at_sim_file() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_sim_pkg(
            root,
            "def title(): String = \"Live\"\n@main def main: IO[Unit] =\n  IO.println(title())\n",
            "def title(): String = 1\n",
        );
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        let file = diags[0].file.as_deref().unwrap_or("");
        assert!(
            file.contains(".scuzz_sim"),
            "expected sim file, got {file} diags={diags:?}"
        );
        assert!(!file.ends_with("Main.scuzz"), "{file}");
        assert!(
            diags[0].line.is_some() && diags[0].line != Some(0),
            "{diags:?}"
        );
    }

    #[test]
    fn check_project_with_unsaved_sim_type_error_points_at_sim_file() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_sim_pkg(
            root,
            "def title(): String = \"Live\"\n@main def main: IO[Unit] =\n  IO.println(title())\n",
            "def title(): String = \"Sim\"\n",
        );
        assert!(check_project(root).unwrap().is_empty());
        let mut unsaved = BTreeMap::new();
        unsaved.insert(
            canonicalize_source_path(&root.join("src/Main.scuzz_sim")),
            crate::format::format_source("def title(): String = 1\n").unwrap(),
        );
        let diags = check_project_with(root, &unsaved).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        let file = diags[0].file.as_deref().unwrap_or("");
        assert!(
            file.contains(".scuzz_sim"),
            "expected sim file, got {file} diags={diags:?}"
        );
        assert!(
            diags[0].line.is_some() && diags[0].line != Some(0),
            "{diags:?}"
        );
    }

    #[test]
    fn check_project_typechecks_live_body_replaced_by_sim() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_sim_pkg(
            root,
            "def title(): String = 1\n@main def main: IO[Unit] =\n  IO.println(title())\n",
            "def title(): String = \"Sim\"\n",
        );
        let diags = check_project(root).unwrap();
        assert!(
            diags.iter().any(|d| {
                (d.message.contains("String") || d.message.contains("Int"))
                    && d.file
                        .as_deref()
                        .is_some_and(|f| f.ends_with("Main.scuzz") && !f.contains(".scuzz_sim"))
            }),
            "expected live type error, got {diags:?}"
        );
    }

    #[test]
    fn check_project_sim_require_does_not_apply_live_law() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_sim_pkg(
            root,
            "law always: Bool = 1 == 1\ndef title(): String = \"Live\"\n@main def main: IO[Unit] =\n  IO.println(title())\n",
            "def title(): String = \"Sim\".require(always)\n",
        );
        let diags = check_project(root).unwrap();
        assert!(
            diags.iter().any(|d| d.message.contains("never applied")),
            "{diags:?}"
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
    fn declaration_project_jumps_to_import() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main = crate::format::format_source(
            "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let call = main.rfind("tag").unwrap();
        let (line, col) = offset_to_utf16_pos(&main, call);
        let (dest, sl, sc, el, ec) = declaration_project(root, &BTreeMap::new(), &path, line, col)
            .unwrap()
            .expect("declaration");
        assert_eq!(canonicalize_source_path(&dest), path);
        let import_at = main.find("A.tag").unwrap();
        let (il, ic) = offset_to_utf16_pos(&main, import_at);
        let (iel, iec) = offset_to_utf16_pos(&main, import_at + "A.tag".len());
        assert_eq!((sl, sc, el, ec), (il, ic, iel, iec));
        let (def_dest, _, _, _, _) = definition_project(root, &BTreeMap::new(), &path, line, col)
            .unwrap()
            .expect("definition");
        assert!(
            canonicalize_source_path(&def_dest).ends_with("A.scuzz"),
            "{def_dest:?}"
        );
    }

    #[test]
    fn will_rename_files_rewrites_import_module() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main = crate::format::format_source(
            "import A.tag\n@main def main: IO[Unit] =\n  IO.println(A.tag())\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let old = canonicalize_source_path(&root.join("src/A.scuzz"));
        let new = canonicalize_source_path(&root.join("src/B.scuzz"));
        let edits = will_rename_files_project(root, &BTreeMap::new(), &[(old, new)]).unwrap();
        assert_eq!(edits.len(), 2, "{edits:?}");
        let main_path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        for (path, sl, sc, el, ec, text) in &edits {
            assert_eq!(canonicalize_source_path(path), main_path);
            assert_eq!(text, "B");
            let start = crate::span::utf16_pos_to_offset(&main, *sl, *sc);
            let end = crate::span::utf16_pos_to_offset(&main, *el, *ec);
            assert_eq!(&main[start..end], "A");
        }
        let same_stem = will_rename_files_project(
            root,
            &BTreeMap::new(),
            &[(
                canonicalize_source_path(&root.join("src/A.scuzz")),
                canonicalize_source_path(&root.join("lib/A.scuzz")),
            )],
        )
        .unwrap();
        assert!(same_stem.is_empty(), "{same_stem:?}");
    }

    #[test]
    fn type_definition_project_jumps_to_enum() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"tydef_ok\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "\
enum Color:
  case Red
def paint(c: Color): Color =
  c
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let paint = formatted.find("paint").unwrap();
        let (line, col) = offset_to_utf16_pos(&formatted, paint);
        let (dest, sl, sc, _el, _ec) =
            type_definition_project(root, &BTreeMap::new(), &path, line, col)
                .unwrap()
                .expect("type definition");
        assert_eq!(canonicalize_source_path(&dest), path);
        let color = formatted.find("Color").unwrap();
        let (dl, dc) = offset_to_utf16_pos(&formatted, color);
        assert_eq!((sl, sc), (dl, dc));
    }

    #[test]
    fn implementation_project_jumps_to_for_type() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"impl_ok\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "\
record Point(x: Int, y: Int)
trait Show:
  def show(): String
impl Show for Point:
  def show(): String =
    \"p\"
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let show = formatted.find("Show:").unwrap();
        let (line, col) = offset_to_utf16_pos(&formatted, show);
        let locs = implementation_project(root, &BTreeMap::new(), &path, line, col).unwrap();
        assert_eq!(locs.len(), 1, "{locs:?}");
        let (dest, sl, sc, _el, _ec) = &locs[0];
        assert_eq!(canonicalize_source_path(dest), path);
        let point = formatted.find("for Point").unwrap() + 4;
        let (dl, dc) = offset_to_utf16_pos(&formatted, point);
        assert_eq!((*sl, *sc), (dl, dc));
    }

    #[test]
    fn code_lenses_project_counts_add_refs() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/Main.scuzz"),
            crate::format::format_source(
                "def add(n: Int): Int = n\n@main def main: IO[Unit] =\n  IO.println(Str.fromInt(add(1)))\n",
            )
            .unwrap(),
        )
        .unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let lenses = code_lenses_project(root, &BTreeMap::new(), &path).unwrap();
        assert!(lenses.iter().any(|l| l.4 == "1 ref"), "{lenses:?}");
    }

    #[test]
    fn document_links_project_resolves_import() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main = crate::format::format_source(
            "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let links = document_links_project(root, &BTreeMap::new(), &path).unwrap();
        assert_eq!(links.len(), 1, "{links:?}");
        assert_eq!(links[0].9, "A.tag");
        assert!(links[0].4.ends_with("A.scuzz"), "{:?}", links[0].4);
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

    #[test]
    fn check_project_reports_unknown_io_method() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"io_typo\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.printl(\"ok\")\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unknown function IO.printl"),
            "{}",
            diags[0].message
        );
        assert_eq!(diags[0].related.len(), 1, "{:?}", diags[0].related);
        assert!(
            diags[0].related[0].message.contains("`IO.println`"),
            "{}",
            diags[0].related[0].message
        );
        let json = format_diagnostics(&diags, true);
        assert!(json.contains("\"related\""), "{json}");
        assert!(json.contains("did you mean"), "{json}");
    }

    #[test]
    fn document_diagnostics_project_filters_to_file() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"pull_diag\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.printl(\"ok\")\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let diags = document_diagnostics_project(root, &BTreeMap::new(), &path).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unknown function IO.printl"),
            "{}",
            diags[0].message
        );
    }

    #[test]
    fn document_diagnostics_project_empty_when_ok() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let diags = document_diagnostics_project(root, &BTreeMap::new(), &path).unwrap();
        assert!(diags.is_empty(), "{diags:?}");
    }

    #[test]
    fn workspace_diagnostics_project_lists_every_src_file() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main =
            crate::format::format_source("@main def main: IO[Unit] =\n  IO.printl(\"ok\")\n")
                .unwrap();
        fs::write(root.join("src/Main.scuzz"), main).unwrap();
        let items = workspace_diagnostics_project(root, &BTreeMap::new()).unwrap();
        assert!(items.len() >= 2, "{items:?}");
        let main_path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let a_path = canonicalize_source_path(&root.join("src/A.scuzz"));
        let main_diags = items
            .iter()
            .find(|(p, _)| canonicalize_source_path(p) == main_path)
            .map(|(_, d)| d.as_slice())
            .unwrap_or(&[]);
        let a_diags = items
            .iter()
            .find(|(p, _)| canonicalize_source_path(p) == a_path)
            .map(|(_, d)| d.as_slice())
            .unwrap_or(&[]);
        assert_eq!(main_diags.len(), 1, "{main_diags:?}");
        assert!(
            main_diags[0].message.contains("unknown function IO.printl"),
            "{}",
            main_diags[0].message
        );
        assert!(a_diags.is_empty(), "{a_diags:?}");
    }

    #[test]
    fn semantic_tokens_range_is_shorter_than_full() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let path = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let text = fs::read_to_string(&path).unwrap();
        let full = semantic_tokens_project(root, &BTreeMap::new(), &path).unwrap();
        let line1 = text.find('\n').map(|i| i + 1).unwrap_or(0);
        let (sl, sc) = offset_to_utf16_pos(&text, line1);
        let (el, ec) = offset_to_utf16_pos(&text, text.len());
        let ranged = semantic_tokens_range_project(
            root,
            &BTreeMap::new(),
            &path,
            Some(((sl, sc), (el, ec))),
        )
        .unwrap();
        assert!(!full.is_empty(), "{full:?}");
        assert!(!ranged.is_empty(), "{ranged:?}");
        assert!(
            ranged.len() < full.len(),
            "{} vs {}",
            ranged.len(),
            full.len()
        );
        assert_eq!(ranged[0], 0);
    }

    #[test]
    fn check_project_typechecks_impl_method_bodies() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"impl_body\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "\
record Point(x: Int):
  def label(): String =
    self.x
@main def main: IO[Unit] =
  IO.println(\"ok\")
";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("String") || diags[0].message.contains("Int"),
            "{}",
            diags[0].message
        );
    }

    #[test]
    fn check_project_reports_unused_law_and_type_error() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"law_and_ty\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "\
law always: Bool = 1 == 1
def a(): Int = \"x\"
@main def main: IO[Unit] =
  IO.println(\"ok\")
";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), formatted).unwrap();
        let diags = check_project(root).unwrap();
        assert!(
            diags.len() >= 2,
            "expected law and type diagnostics, got {}: {diags:?}",
            diags.len()
        );
        assert!(
            diags.iter().any(|d| d.message.contains("never applied")),
            "{diags:?}"
        );
        assert!(
            diags
                .iter()
                .any(|d| d.message.contains("Int") || d.message.contains("String")),
            "{diags:?}"
        );
    }

    #[test]
    fn check_project_reports_unused_import() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main = crate::format::format_source(
            "import A.tag\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unused import A.tag"),
            "{:?}",
            diags[0].message
        );
    }

    #[test]
    fn check_project_keeps_used_import() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        fs::write(
            root.join("src/A.scuzz"),
            crate::format::format_source("def tag(): String =\n  \"a\"\n").unwrap(),
        )
        .unwrap();
        let main = crate::format::format_source(
            "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert!(diags.is_empty(), "{diags:?}");
    }

    #[test]
    fn check_project_reports_unused_local() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let main = crate::format::format_source(
            "@main def main: IO[Unit] =\n  for {\n    x = 1\n  } yield IO.println(\"ok\")\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unused local x"),
            "{:?}",
            diags[0].message
        );
    }

    #[test]
    fn check_project_keeps_underscore_local() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let main = crate::format::format_source(
            "@main def main: IO[Unit] =\n  for {\n    _x = 1\n  } yield IO.println(\"ok\")\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert!(diags.is_empty(), "{diags:?}");
    }

    #[test]
    fn check_project_reports_unused_parameter() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let main = crate::format::format_source(
            "def add(n: Int): Int =\n  1\n@main def main: IO[Unit] =\n  IO.println(Str.fromInt(add(1)))\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unused parameter n"),
            "{:?}",
            diags[0].message
        );
    }

    #[test]
    fn check_project_reports_unused_private_def() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let main = crate::format::format_source(
            "private def hidden(): String =\n  \"a\"\n@main def main: IO[Unit] =\n  IO.println(\"ok\")\n",
        )
        .unwrap();
        fs::write(root.join("src/Main.scuzz"), &main).unwrap();
        let diags = check_project(root).unwrap();
        assert_eq!(diags.len(), 1, "{diags:?}");
        assert!(
            diags[0].message.contains("unused private def hidden"),
            "{:?}",
            diags[0].message
        );
    }
}

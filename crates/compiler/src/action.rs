//! Code actions from the same parse as `check`. No second typer.

use crate::ast::{Expr, ExprKind, MatchArm, Program};
use crate::format::format_source;
use crate::hover::KIT_SIGS;
use crate::lower::lower_program;
use crate::resolve::{module_id_from_label, UnusedKind, UnusedName};
use crate::typ::{typecheck_all, TypeError};

pub const KIND_QUICKFIX: &str = "quickfix";
pub const KIND_SOURCE_FORMAT: &str = "source.formatDocument";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CodeAction {
    pub title: String,
    pub kind: String,
    pub start: usize,
    pub end: usize,
    pub new_text: String,
    pub preferred: bool,
    /// Check diagnostic this fix addresses (`None` for format).
    pub diagnostic: Option<(String, usize, usize)>,
}

/// Edits for the LSP range in `current_file`. `range` is byte offsets; `None` is the whole file.
/// `only` is LSP `CodeActionKind` prefixes. Empty `only` keeps every kind.
pub fn code_actions_in_source(
    program: Option<&Program>,
    current_file: &str,
    source: &str,
    range: Option<(usize, usize)>,
    only: &[String],
) -> Vec<CodeAction> {
    let mut out = Vec::new();
    if kind_wanted(only, KIND_SOURCE_FORMAT) {
        if let Some(a) = format_action(source) {
            out.push(a);
        }
    }
    if let Some(program) = program {
        let unused = crate::resolve::unused_names(program);
        let program = lower_program(program.clone());
        let errs = typecheck_all(&program);
        if kind_wanted(only, KIND_QUICKFIX) {
            fill_match_actions(&program, current_file, source, range, &errs, &mut out);
            unknown_callee_actions(&program, current_file, source, range, &errs, &mut out);
            unused_import_actions(&program, current_file, source, range, &mut out);
            unused_name_actions(&unused, current_file, source, range, &mut out);
        }
    }
    out
}

fn kind_wanted(only: &[String], kind: &str) -> bool {
    if only.is_empty() {
        return true;
    }
    only.iter()
        .any(|o| kind == o || kind.starts_with(&format!("{o}.")))
}

fn format_action(source: &str) -> Option<CodeAction> {
    let formatted = format_source(source).ok()?;
    if formatted == source {
        return None;
    }
    Some(CodeAction {
        title: "Format document".into(),
        kind: KIND_SOURCE_FORMAT.into(),
        start: 0,
        end: source.len(),
        new_text: formatted,
        preferred: true,
        diagnostic: None,
    })
}

fn fill_match_actions(
    program: &Program,
    current_file: &str,
    source: &str,
    range: Option<(usize, usize)>,
    errs: &[TypeError],
    out: &mut Vec<CodeAction>,
) {
    let missing_errs: Vec<(String, Option<&crate::span::Span>, String)> = errs
        .iter()
        .filter_map(|e| {
            let msg = type_err_msg(e);
            let rest = msg.strip_prefix("non-exhaustive match: missing ")?;
            Some((rest.to_string(), e.span(), e.to_string()))
        })
        .collect();
    if missing_errs.is_empty() {
        return;
    }
    let module = module_id_from_label(current_file);
    walk_program(program, &module, &mut |e| {
        let ExprKind::Match { arms, .. } = &e.kind else {
            return;
        };
        if arms.is_empty() || !in_range(e.span.start, e.span.end, range) {
            return;
        }
        let Some((rest, span, msg)) = missing_errs.iter().find_map(|(rest, span, msg)| {
            if span_overlaps(span, e.span.start, e.span.end) {
                Some((rest.as_str(), span, msg.as_str()))
            } else {
                None
            }
        }) else {
            return;
        };
        let missing: Vec<&str> = rest.split(", ").filter(|s| !s.is_empty()).collect();
        if missing.is_empty() {
            return;
        }
        if let Some(mut a) = fill_match_edit(source, e.span.start, e.span.end, &arms[0], &missing) {
            let (ds, de) = match span {
                Some(s) => (s.start, s.end),
                None => (e.span.start, e.span.end),
            };
            a.diagnostic = Some((msg.to_string(), ds, de));
            out.push(a);
        }
    });
}

fn fill_match_edit(
    source: &str,
    match_start: usize,
    _match_end: usize,
    first: &MatchArm,
    missing: &[&str],
) -> Option<CodeAction> {
    let brace = close_brace_after(source, match_start)?;
    let line_start = source[..brace].rfind('\n').map(|i| i + 1).unwrap_or(0);
    let only_ws = source[line_start..brace]
        .chars()
        .all(|c| c == ' ' || c == '\t');
    let brace_indent = line_indent(source, brace);
    let arm_indent = format!("{brace_indent}  ");
    let body = arm_body_text(source, first.body.span.start, brace)?;
    let mut text = String::new();
    if !only_ws {
        text.push('\n');
    }
    for pat in missing {
        text.push_str(&arm_indent);
        text.push_str("case ");
        text.push_str(pat);
        text.push_str(" => ");
        text.push_str(&body);
        text.push('\n');
    }
    text.push_str(&brace_indent);
    text.push('}');
    let start = if only_ws { line_start } else { brace };
    Some(CodeAction {
        title: "Fill missing match cases".into(),
        kind: KIND_QUICKFIX.into(),
        start,
        end: brace + 1,
        new_text: text,
        preferred: true,
        diagnostic: None,
    })
}

fn arm_body_text(source: &str, body_start: usize, brace: usize) -> Option<String> {
    let region = source.get(body_start..brace)?;
    let mut cut = region.len();
    let mut search = 0;
    while let Some(nl) = region[search..].find('\n') {
        let at = search + nl;
        if region[at + 1..].trim_start().starts_with("case ") {
            cut = at;
            break;
        }
        search = at + 1;
    }
    Some(region[..cut].trim_end().to_string())
}

/// Byte offset of the `}` that closes the first `{` after `from`.
fn close_brace_after(source: &str, from: usize) -> Option<usize> {
    let bytes = source.as_bytes();
    let mut i = from;
    let mut depth = 0i32;
    let mut in_str = false;
    while i < bytes.len() {
        let c = bytes[i];
        if in_str {
            if c == b'\\' && i + 1 < bytes.len() {
                i += 2;
                continue;
            }
            if c == b'"' {
                in_str = false;
            }
            i += 1;
            continue;
        }
        match c {
            b'"' => in_str = true,
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
}

fn unknown_callee_actions(
    program: &Program,
    current_file: &str,
    source: &str,
    range: Option<(usize, usize)>,
    errs: &[TypeError],
    out: &mut Vec<CodeAction>,
) {
    let unknowns: Vec<(String, Option<&crate::span::Span>, String)> = errs
        .iter()
        .filter_map(|e| {
            let msg = type_err_msg(e);
            let name = msg.strip_prefix("unknown function ")?;
            Some((name.to_string(), e.span(), e.to_string()))
        })
        .collect();
    if unknowns.is_empty() {
        return;
    }
    let cands = callee_candidates(program);
    let module = module_id_from_label(current_file);
    walk_program(program, &module, &mut |e| {
        let ExprKind::Call { callee, .. } = &e.kind else {
            return;
        };
        if !in_range(e.span.start, e.span.end, range) {
            return;
        }
        let hit = unknowns.iter().find(|(name, span, _)| {
            name == callee && span_overlaps(span, e.span.start, e.span.end)
        });
        let Some((_, span, msg)) = hit else {
            return;
        };
        let Some(fix) = closest_callee(callee, &cands) else {
            return;
        };
        let Some((start, end)) = callee_span(source, e.span.start, e.span.end, callee) else {
            return;
        };
        let (ds, de) = match span {
            Some(s) => (s.start, s.end),
            None => (start, end),
        };
        out.push(CodeAction {
            title: format!("Change `{callee}` to `{fix}`"),
            kind: KIND_QUICKFIX.into(),
            start,
            end,
            new_text: fix,
            preferred: true,
            diagnostic: Some((msg.clone(), ds, de)),
        });
    });
}

pub(crate) fn callee_candidates(program: &Program) -> Vec<String> {
    let mut out: Vec<String> = KIT_SIGS.iter().map(|(n, _)| (*n).to_string()).collect();
    for d in &program.defs {
        if d.is_private {
            continue;
        }
        out.push(d.name.clone());
        if !d.module.is_empty() {
            out.push(format!("{}.{}", d.module, d.name));
        }
    }
    out
}

pub(crate) fn closest_callee(name: &str, cands: &[String]) -> Option<String> {
    let method = name.rsplit('.').next().unwrap_or(name);
    let kit = name.split('.').next();
    let mut best: Option<(usize, String)> = None;
    for c in cands {
        if c == name {
            continue;
        }
        let d = if kit.is_some_and(|k| c.starts_with(&format!("{k}."))) {
            levenshtein(name, c).min(levenshtein(method, c.rsplit('.').next().unwrap_or(c)))
        } else {
            levenshtein(name, c)
        };
        if d == 0 || d > 3 {
            continue;
        }
        let better = match &best {
            None => true,
            Some((bd, b)) => d < *bd || (d == *bd && c.len() < b.len()),
        };
        if better {
            best = Some((d, c.clone()));
        }
    }
    best.map(|(_, s)| s)
}

fn levenshtein(a: &str, b: &str) -> usize {
    if a == b {
        return 0;
    }
    if a.is_empty() {
        return b.len();
    }
    if b.is_empty() {
        return a.len();
    }
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    let mut prev: Vec<usize> = (0..=b.len()).collect();
    let mut cur = vec![0; b.len() + 1];
    for (i, ca) in a.iter().enumerate() {
        cur[0] = i + 1;
        for (j, cb) in b.iter().enumerate() {
            let cost = if ca == cb { 0 } else { 1 };
            cur[j + 1] = (prev[j + 1] + 1).min(cur[j] + 1).min(prev[j] + cost);
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    prev[b.len()]
}

fn callee_span(source: &str, start: usize, end: usize, callee: &str) -> Option<(usize, usize)> {
    let region = source.get(start..end)?;
    let i = region.find(callee)?;
    Some((start + i, start + i + callee.len()))
}

fn type_err_msg(e: &TypeError) -> &str {
    match e {
        TypeError::Msg(m) => m,
        TypeError::At { msg, .. } => msg,
    }
}

fn span_overlaps(span: &Option<&crate::span::Span>, start: usize, end: usize) -> bool {
    match span {
        None => true,
        Some(s) => s.end > start && s.start < end,
    }
}

fn in_range(start: usize, end: usize, range: Option<(usize, usize)>) -> bool {
    match range {
        None => true,
        Some((a, b)) if a == b => start <= a && a <= end,
        Some((a, b)) => start < b && end > a,
    }
}

fn line_indent(source: &str, offset: usize) -> String {
    let line_start = source[..offset].rfind('\n').map(|i| i + 1).unwrap_or(0);
    source[line_start..offset]
        .chars()
        .take_while(|c| *c == ' ' || *c == '\t')
        .collect()
}

fn unused_name_actions(
    unused: &[UnusedName],
    current_file: &str,
    source: &str,
    range: Option<(usize, usize)>,
    out: &mut Vec<CodeAction>,
) {
    let module = module_id_from_label(current_file);
    for u in unused {
        if u.span.file != current_file && !u.span.file.is_empty() {
            let from_module = module_id_from_label(&u.span.file);
            if from_module != module {
                continue;
            }
        }
        if u.span.end <= u.span.start || !in_range(u.span.start, u.span.end, range) {
            continue;
        }
        match u.kind {
            UnusedKind::PrivateDef => {
                let (start, end) = def_range(source, u.span.start, u.body_end);
                out.push(CodeAction {
                    title: format!("Remove {}", u.message()),
                    kind: KIND_QUICKFIX.into(),
                    start,
                    end,
                    new_text: String::new(),
                    preferred: true,
                    diagnostic: Some((u.message(), u.span.start, u.span.end)),
                });
            }
            UnusedKind::Local | UnusedKind::Param => {
                out.push(CodeAction {
                    title: format!("Prefix {} with `_`", u.name),
                    kind: KIND_QUICKFIX.into(),
                    start: u.span.start,
                    end: u.span.end,
                    new_text: format!("_{}", u.name),
                    preferred: true,
                    diagnostic: Some((u.message(), u.span.start, u.span.end)),
                });
            }
        }
    }
}

fn def_range(source: &str, name_start: usize, body_end: usize) -> (usize, usize) {
    let line_start = source[..name_start.min(source.len())]
        .rfind('\n')
        .map(|i| i + 1)
        .unwrap_or(0);
    let end = body_end.max(name_start);
    let line_end = source
        .get(end..)
        .and_then(|rest| rest.find('\n'))
        .map(|i| end + i + 1)
        .unwrap_or(source.len());
    (line_start, line_end)
}

fn unused_import_actions(
    program: &Program,
    current_file: &str,
    source: &str,
    range: Option<(usize, usize)>,
    out: &mut Vec<CodeAction>,
) {
    let module = module_id_from_label(current_file);
    for im in crate::resolve::unused_imports(program) {
        if im.in_module != module {
            continue;
        }
        if im.span.end <= im.span.start || !in_range(im.span.start, im.span.end, range) {
            continue;
        }
        let (start, end) = import_line_range(source, im.span.start, im.span.end);
        let msg = if im.is_wildcard() {
            format!("unused import {}.{}", im.from_module, im.name)
        } else if let Some(alias) = &im.alias {
            format!("unused import {}.{} as {alias}", im.from_module, im.name)
        } else {
            format!("unused import {}.{}", im.from_module, im.name)
        };
        out.push(CodeAction {
            title: format!("Remove {msg}"),
            kind: KIND_QUICKFIX.into(),
            start,
            end,
            new_text: String::new(),
            preferred: true,
            diagnostic: Some((msg, im.span.start, im.span.end)),
        });
    }
}

fn import_line_range(source: &str, start: usize, end: usize) -> (usize, usize) {
    let line_start = source[..start.min(source.len())]
        .rfind('\n')
        .map(|i| i + 1)
        .unwrap_or(0);
    let line_end = source
        .get(end..)
        .and_then(|rest| rest.find('\n'))
        .map(|i| end + i + 1)
        .unwrap_or(source.len());
    (line_start, line_end)
}

fn walk_program(program: &Program, module: &str, f: &mut impl FnMut(&Expr)) {
    for d in &program.defs {
        if d.module == *module {
            walk_expr(&d.body, f);
        }
    }
    if program.main.module == *module && !program.main.name.is_empty() {
        walk_expr(&program.main.body, f);
    }
    for im in &program.impls {
        if im.module == *module {
            for m in &im.methods {
                walk_expr(&m.body, f);
            }
        }
    }
    for en in &program.enums {
        if en.module == *module {
            for m in &en.methods {
                walk_expr(&m.body, f);
            }
        }
    }
}

fn walk_expr(e: &Expr, f: &mut impl FnMut(&Expr)) {
    f(e);
    e.for_each_child(|c| walk_expr(c, f));
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn actions(src: &str, range: Option<(usize, usize)>, only: &[&str]) -> Vec<CodeAction> {
        let p = parse_file(src, "Main.scuzz").ok();
        let only: Vec<String> = only.iter().map(|s| (*s).to_string()).collect();
        code_actions_in_source(p.as_ref(), "Main.scuzz", src, range, &only)
    }

    #[test]
    fn formats_unformatted_source() {
        let src = "@main def main: IO[Unit] = IO.println(\"ok\")\n";
        let acts = actions(src, None, &[]);
        let fmt = acts
            .iter()
            .find(|a| a.kind == KIND_SOURCE_FORMAT)
            .expect("format");
        assert_eq!(fmt.title, "Format document");
        assert!(fmt.new_text.contains("IO.println"), "{}", fmt.new_text);
        assert_ne!(fmt.new_text, src);
        assert!(fmt.diagnostic.is_none());
    }

    #[test]
    fn skips_format_when_already_pretty() {
        let src = "@main def main: IO[Unit] = IO.println(\"ok\")\n";
        let pretty = format_source(src).unwrap();
        let acts = actions(&pretty, None, &[]);
        assert!(
            acts.iter().all(|a| a.kind != KIND_SOURCE_FORMAT),
            "{acts:?}"
        );
    }

    #[test]
    fn only_quickfix_skips_format() {
        let src = "@main def main: IO[Unit] = IO.println(\"ok\")\n";
        let acts = actions(src, None, &["quickfix"]);
        assert!(acts.iter().all(|a| a.kind == KIND_QUICKFIX), "{acts:?}");
    }

    #[test]
    fn fills_missing_color_case() {
        let src = r#"enum Color:
  case Red
  case Blue

@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red => IO.println("r")
  }
"#;
        let pretty = format_source(src).unwrap();
        let acts = actions(&pretty, None, &["quickfix"]);
        let fill = acts
            .iter()
            .find(|a| a.title == "Fill missing match cases")
            .expect("fill");
        let (msg, _, _) = fill.diagnostic.as_ref().expect("diagnostic");
        assert!(msg.contains("non-exhaustive"), "{msg}");
        let mut edited = pretty.clone();
        edited.replace_range(fill.start..fill.end, &fill.new_text);
        assert!(
            edited.contains("case Color.Blue => IO.println(\"r\")"),
            "{edited}"
        );
        let again = actions(&edited, None, &["quickfix"]);
        assert!(
            again.iter().all(|a| a.title != "Fill missing match cases"),
            "{again:?}"
        );
    }

    #[test]
    fn fill_skips_match_outside_range() {
        let src = r#"enum Color:
  case Red
  case Blue

@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red => IO.println("r")
  }
"#;
        let pretty = format_source(src).unwrap();
        let enum_at = pretty.find("enum").unwrap();
        let acts = actions(&pretty, Some((enum_at, enum_at + 4)), &["quickfix"]);
        assert!(
            acts.iter().all(|a| a.title != "Fill missing match cases"),
            "{acts:?}"
        );
    }

    #[test]
    fn replaces_unknown_contains_typo() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.contain("ab", "b")) "y" else "n")
"#;
        let pretty = format_source(src).unwrap();
        let acts = actions(&pretty, None, &["quickfix"]);
        let fix = acts
            .iter()
            .find(|a| a.new_text == "Str.contains")
            .expect("contains");
        assert!(fix.title.contains("Str.contain"), "{}", fix.title);
        let (msg, _, _) = fix.diagnostic.as_ref().expect("diagnostic");
        assert!(msg.contains("unknown function"), "{msg}");
        let mut edited = pretty.clone();
        edited.replace_range(fix.start..fix.end, &fix.new_text);
        assert!(edited.contains("Str.contains("), "{edited}");
    }

    #[test]
    fn replaces_unknown_io_printl_typo() {
        let src = r#"@main def main: IO[Unit] =
  IO.printl("ok")
"#;
        let pretty = format_source(src).unwrap();
        let acts = actions(&pretty, None, &["quickfix"]);
        let fix = acts
            .iter()
            .find(|a| a.new_text == "IO.println")
            .expect("println");
        assert!(fix.title.contains("IO.printl"), "{}", fix.title);
        let (msg, _, _) = fix.diagnostic.as_ref().expect("diagnostic");
        assert!(msg.contains("unknown function IO.printl"), "{msg}");
        let mut edited = pretty.clone();
        edited.replace_range(fix.start..fix.end, &fix.new_text);
        assert!(edited.contains("IO.println("), "{edited}");
    }

    #[test]
    fn skips_distant_unknown_name() {
        let src = r#"@main def main: IO[Unit] =
  zzzyyy("ok")
"#;
        let pretty = format_source(src).unwrap();
        let acts = actions(&pretty, None, &["quickfix"]);
        assert!(
            acts.iter().all(|a| !a.title.starts_with("Change `zzzyyy`")),
            "{acts:?}"
        );
    }

    #[test]
    fn only_source_keeps_format() {
        let src = "@main def main: IO[Unit] = IO.println(\"ok\")\n";
        let acts = actions(src, None, &["source"]);
        assert!(
            acts.iter().any(|a| a.kind == KIND_SOURCE_FORMAT),
            "{acts:?}"
        );
        assert!(acts.iter().all(|a| a.kind != KIND_QUICKFIX), "{acts:?}");
    }

    #[test]
    fn levenshtein_counts_edits() {
        assert_eq!(levenshtein("println", "println"), 0);
        assert_eq!(levenshtein("printl", "println"), 1);
        assert_eq!(levenshtein("", "ab"), 2);
    }

    #[test]
    fn removes_unused_import() {
        let a = "def tag(): String = \"a\"\n";
        let main = "import A.tag\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n";
        let sources = vec![
            ("A.scuzz".into(), a.to_string()),
            ("Main.scuzz".into(), main.to_string()),
        ];
        let program = crate::parser::parse_sources(&sources).unwrap();
        let acts = crate::action::code_actions_in_source(
            Some(&program),
            "Main.scuzz",
            main,
            None,
            &["quickfix".into()],
        );
        let fix = acts
            .iter()
            .find(|a| a.title.contains("unused import"))
            .expect("remove unused");
        let mut edited = main.to_string();
        edited.replace_range(fix.start..fix.end, &fix.new_text);
        assert!(!edited.contains("import A.tag"), "{edited}");
        assert!(edited.contains("IO.println"), "{edited}");
    }

    #[test]
    fn prefixes_unused_parameter() {
        let src = "def add(n: Int): Int =\n  1\n@main def main: IO[Unit] =\n  IO.println(Str.fromInt(add(1)))\n";
        let acts = actions(src, None, &["quickfix"]);
        let fix = acts
            .iter()
            .find(|a| a.title.contains("Prefix n"))
            .expect("prefix unused");
        let mut edited = src.to_string();
        edited.replace_range(fix.start..fix.end, &fix.new_text);
        assert!(edited.contains("def add(_n: Int)"), "{edited}");
    }

    #[test]
    fn removes_unused_private_def() {
        let src = "private def hidden(): String =\n  \"a\"\n@main def main: IO[Unit] =\n  IO.println(\"ok\")\n";
        let acts = actions(src, None, &["quickfix"]);
        let fix = acts
            .iter()
            .find(|a| a.title.contains("unused private def"))
            .expect("remove unused private");
        let mut edited = src.to_string();
        edited.replace_range(fix.start..fix.end, &fix.new_text);
        assert!(!edited.contains("hidden"), "{edited}");
        assert!(edited.contains("IO.println"), "{edited}");
    }
}

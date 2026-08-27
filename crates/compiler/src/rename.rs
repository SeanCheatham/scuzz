//! Rename from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::definition_in_sources;
use crate::lexer::{lex, Token};
use crate::references::references_in_sources;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RenameEdit {
    pub file: String,
    pub start: usize,
    pub end: usize,
}

/// True when `name` lexes as one ident (not a keyword).
pub fn is_rename_ident(name: &str) -> bool {
    let Ok(toks) = lex(name) else {
        return false;
    };
    match (toks.first(), toks.get(1)) {
        (Some(t), Some(e)) if matches!(e.token, Token::Eof) => {
            matches!(&t.token, Token::Ident(n) if n == name)
        }
        _ => false,
    }
}

/// Ident token under `offset`, plus the current name.
pub fn ident_span_at(source: &str, offset: usize) -> Option<(usize, usize, String)> {
    let toks = lex(source).ok()?;
    for t in &toks {
        if t.span.start <= offset && offset < t.span.end {
            if let Token::Ident(n) = &t.token {
                return Some((t.span.start, t.span.end, n.clone()));
            }
            return None;
        }
        if offset == t.span.end {
            if let Token::Ident(n) = &t.token {
                return Some((t.span.start, t.span.end, n.clone()));
            }
        }
    }
    None
}

/// Range of the ident at `offset` when it has a declaration.
pub fn prepare_rename_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Option<(usize, usize, String)> {
    let (start, end, name) = ident_span_at(current_source, offset)?;
    definition_in_sources(program, sources, current_file, current_source, offset)?;
    Some((start, end, name))
}

/// Edits that replace every use of the ident at `offset` with `new_name`.
pub fn rename_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
    new_name: &str,
) -> Option<Vec<RenameEdit>> {
    if !is_rename_ident(new_name) {
        return None;
    }
    definition_in_sources(program, sources, current_file, current_source, offset)?;
    let refs = references_in_sources(program, sources, current_file, current_source, offset, true);
    if refs.is_empty() {
        return None;
    }
    Some(
        refs.into_iter()
            .map(|loc| RenameEdit {
                file: loc.file,
                start: loc.start,
                end: loc.end,
            })
            .collect(),
    )
}

/// True when `name` is a blessed kit namespace. File rename must not rewrite those.
pub fn is_kit_module(name: &str) -> bool {
    matches!(
        name,
        "IO" | "Str"
            | "List"
            | "Fs"
            | "Sys"
            | "Clock"
            | "Random"
            | "Net"
            | "Impurity"
            | "Signal"
            | "View"
            | "Theme"
            | "Ref"
            | "Queue"
            | "Deferred"
            | "Fiber"
            | "Resource"
            | "Stream"
            | "Map"
            | "Set"
            | "Builder"
            | "Oracle"
            | "Ui"
            | "Property"
            | "Float"
    )
}

fn module_has_def(program: &Program, module: &str, name: &str) -> bool {
    program
        .defs
        .iter()
        .any(|d| d.module == module && d.name == name)
        || (program.main.module == module && program.main.name == name)
}

fn is_enum_ctor(program: &Program, enum_name: &str, case: &str) -> bool {
    program
        .enums
        .iter()
        .any(|e| e.name == enum_name && e.cases.iter().any(|c| c.name == case))
}

/// Byte ranges of `old_mod` used as a module name: `import Module.name` and
/// qualified `Module.def` calls. Does not rewrite enum cases (`Color.Red`).
pub fn module_ident_spans(
    source: &str,
    old_mod: &str,
    program: Option<&Program>,
) -> Vec<(usize, usize)> {
    if old_mod.is_empty() || is_kit_module(old_mod) {
        return Vec::new();
    }
    let Ok(toks) = lex(source) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for i in 0..toks.len() {
        let Token::Ident(name) = &toks[i].token else {
            continue;
        };
        if name != old_mod {
            continue;
        }
        let next_dot = toks
            .get(i + 1)
            .is_some_and(|t| matches!(t.token, Token::Dot));
        if !next_dot {
            continue;
        }
        let prev_import = i
            .checked_sub(1)
            .and_then(|j| toks.get(j))
            .is_some_and(|t| matches!(t.token, Token::Import));
        if prev_import {
            out.push((toks[i].span.start, toks[i].span.end));
            continue;
        }
        let Some(program) = program else {
            continue;
        };
        let Some(Token::Ident(next_name)) = toks.get(i + 2).map(|t| &t.token) else {
            continue;
        };
        if is_enum_ctor(program, old_mod, next_name) {
            continue;
        }
        if module_has_def(program, old_mod, next_name) {
            out.push((toks[i].span.start, toks[i].span.end));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn src_add() -> &'static str {
        "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n"
    }

    #[test]
    fn accepts_idents_and_rejects_keywords() {
        assert!(is_rename_ident("sum"));
        assert!(is_rename_ident("n2"));
        assert!(!is_rename_ident(""));
        assert!(!is_rename_ident("if"));
        assert!(!is_rename_ident("a-b"));
        assert!(!is_rename_ident("1x"));
    }

    #[test]
    fn prepares_range_on_def() {
        let src = src_add();
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let call = src.rfind("add").unwrap();
        let (start, end, name) =
            prepare_rename_in_sources(&p, &sources, "Main.scuzz", src, call).unwrap();
        assert_eq!(name, "add");
        assert_eq!(&src[start..end], "add");
    }

    #[test]
    fn skips_kit_callee() {
        let src = src_add();
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let at = src.find("println").unwrap();
        assert!(prepare_rename_in_sources(&p, &sources, "Main.scuzz", src, at).is_none());
        assert!(rename_in_sources(&p, &sources, "Main.scuzz", src, at, "log").is_none());
    }

    #[test]
    fn renames_def_and_call() {
        let src = src_add();
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let call = src.rfind("add").unwrap();
        let edits = rename_in_sources(&p, &sources, "Main.scuzz", src, call, "sum").unwrap();
        assert_eq!(edits.len(), 2, "{edits:?}");
        assert_eq!(&src[edits[0].start..edits[0].end], "add");
        assert_eq!(&src[edits[1].start..edits[1].end], "add");
        assert!(rename_in_sources(&p, &sources, "Main.scuzz", src, call, "if").is_none());
    }

    #[test]
    fn renames_param() {
        let src = src_add();
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let param = src.find("n:").unwrap();
        let edits = rename_in_sources(&p, &sources, "Main.scuzz", src, param, "x").unwrap();
        assert!(edits.len() >= 2, "{edits:?}");
        for e in &edits {
            assert_eq!(&src[e.start..e.end], "n");
        }
    }

    #[test]
    fn module_spans_cover_import_and_qualified_def() {
        let a = "def tag(): String =\n  \"a\"\n";
        let main = "import A.tag\n@main def main: IO[Unit] =\n  IO.println(A.tag())\n";
        let sources = vec![
            ("A.scuzz".into(), a.to_string()),
            ("Main.scuzz".into(), main.to_string()),
        ];
        let p = crate::parser::parse_sources(&sources).unwrap();
        let spans = module_ident_spans(main, "A", Some(&p));
        assert_eq!(spans.len(), 2, "{spans:?}");
        assert_eq!(&main[spans[0].0..spans[0].1], "A");
        assert_eq!(&main[spans[1].0..spans[1].1], "A");
        assert!(module_ident_spans(main, "IO", Some(&p)).is_empty());
        assert!(module_ident_spans(a, "A", Some(&p)).is_empty());
    }

    #[test]
    fn module_spans_skip_enum_cases() {
        let src = "\
enum Color:
  case Red
def paint(): Color =
  Color.Red
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let p = parse_file(src, "Color.scuzz").unwrap();
        let spans = module_ident_spans(src, "Color", Some(&p));
        assert!(spans.is_empty(), "{spans:?}");
    }

    #[test]
    fn module_spans_import_without_program() {
        let src = "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n";
        let spans = module_ident_spans(src, "A", None);
        assert_eq!(spans.len(), 1, "{spans:?}");
        assert_eq!(&src[spans[0].0..spans[0].1], "A");
    }
}

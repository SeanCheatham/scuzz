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
}

//! Find-references from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::{definition_in_sources, DefLoc};
use crate::lexer::{lex, Token};

/// Locations that resolve to the same declaration as the ident at `offset`.
pub fn references_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
    include_declaration: bool,
) -> Vec<DefLoc> {
    let Some(target) =
        definition_in_sources(program, sources, current_file, current_source, offset)
    else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for (file, text) in sources {
        let Ok(toks) = lex(text) else {
            continue;
        };
        for t in &toks {
            let Token::Ident(_) = &t.token else {
                continue;
            };
            let Some(loc) = definition_in_sources(program, sources, file, text, t.span.start)
            else {
                continue;
            };
            if loc != target {
                continue;
            }
            if !include_declaration && loc.start == t.span.start && loc.file == *file {
                continue;
            }
            out.push(DefLoc {
                file: file.clone(),
                start: t.span.start,
                end: t.span.end,
            });
        }
    }
    out.sort_by(|a, b| a.file.cmp(&b.file).then(a.start.cmp(&b.start)));
    out.dedup();
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn finds_def_and_call() {
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let call = src.rfind("add").unwrap();
        let refs = references_in_sources(&p, &sources, "Main.scuzz", src, call, true);
        assert_eq!(refs.len(), 2, "{refs:?}");
        assert_eq!(refs[0].start, src.find("add").unwrap());
        assert_eq!(refs[1].start, call);
        let no_decl = references_in_sources(&p, &sources, "Main.scuzz", src, call, false);
        assert_eq!(no_decl.len(), 1, "{no_decl:?}");
        assert_eq!(no_decl[0].start, call);
    }

    #[test]
    fn finds_param_from_signature() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let param = src.find("n:").unwrap();
        let refs = references_in_sources(&p, &sources, "Main.scuzz", src, param, true);
        assert!(refs.len() >= 2, "{refs:?}");
        let body_n = src.rfind("= n").unwrap() + 2;
        assert!(refs.iter().any(|r| r.start == param), "{refs:?}");
        assert!(refs.iter().any(|r| r.start == body_n), "{refs:?}");
    }
}

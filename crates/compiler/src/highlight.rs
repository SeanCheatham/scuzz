//! Document highlights from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::definition_in_sources;
use crate::references::references_in_sources;

pub const HIGHLIGHT_READ: u8 = 2;
pub const HIGHLIGHT_WRITE: u8 = 3;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DocHighlight {
    pub start: usize,
    pub end: usize,
    pub kind: u8,
}

/// Occurrences of the ident at `offset` in `current_file`.
/// Write is the declaration. Read is each use in this file.
pub fn highlights_in_source(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<DocHighlight> {
    let Some(target) =
        definition_in_sources(program, sources, current_file, current_source, offset)
    else {
        return Vec::new();
    };
    let refs = references_in_sources(program, sources, current_file, current_source, offset, true);
    let mut out = Vec::new();
    for r in refs {
        if r.file != current_file {
            continue;
        }
        let kind = if r.start == target.start && r.file == target.file {
            HIGHLIGHT_WRITE
        } else {
            HIGHLIGHT_READ
        };
        out.push(DocHighlight {
            start: r.start,
            end: r.end,
            kind,
        });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn highlights_def_write_and_call_read() {
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let call = src.rfind("add").unwrap();
        let hits = highlights_in_source(&p, &sources, "Main.scuzz", src, call);
        assert_eq!(hits.len(), 2, "{hits:?}");
        assert_eq!(hits[0].start, src.find("add").unwrap());
        assert_eq!(hits[0].kind, HIGHLIGHT_WRITE);
        assert_eq!(hits[1].start, call);
        assert_eq!(hits[1].kind, HIGHLIGHT_READ);
    }

    #[test]
    fn highlights_param_write_and_body_read() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let param = src.find("n:").unwrap();
        let hits = highlights_in_source(&p, &sources, "Main.scuzz", src, param);
        assert!(hits.len() >= 2, "{hits:?}");
        let body_n = src.rfind("= n").unwrap() + 2;
        assert!(
            hits.iter()
                .any(|h| h.start == param && h.kind == HIGHLIGHT_WRITE),
            "{hits:?}"
        );
        assert!(
            hits.iter()
                .any(|h| h.start == body_n && h.kind == HIGHLIGHT_READ),
            "{hits:?}"
        );
    }

    #[test]
    fn skips_kit_with_no_definition() {
        let src = "@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let kit = src.find("println").unwrap();
        let hits = highlights_in_source(&p, &sources, "Main.scuzz", src, kit);
        assert!(hits.is_empty(), "{hits:?}");
    }
}

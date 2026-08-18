//! Code lenses from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::definition_in_sources;
use crate::references::references_in_sources;
use crate::symbols::{symbols_in_source, KIND_FN};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CodeLens {
    pub start: usize,
    pub end: usize,
    pub title: String,
}

/// One lens per user def in `current_file`. Title is a reference count.
pub fn code_lenses_in_source(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    source: &str,
) -> Vec<CodeLens> {
    let mut out = Vec::new();
    for s in symbols_in_source(program, current_file, source) {
        if s.kind != KIND_FN {
            continue;
        }
        if definition_in_sources(program, sources, current_file, source, s.sel_start).is_none() {
            continue;
        }
        let n =
            references_in_sources(program, sources, current_file, source, s.sel_start, false).len();
        let title = if n == 1 {
            "1 ref".into()
        } else {
            format!("{n} refs")
        };
        out.push(CodeLens {
            start: s.sel_start,
            end: s.sel_end,
            title,
        });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn counts_refs_on_add() {
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let lenses = code_lenses_in_source(&p, &sources, "Main.scuzz", src);
        let add = lenses.iter().find(|l| src[l.start..l.end] == *"add");
        assert_eq!(add.map(|l| l.title.as_str()), Some("1 ref"), "{lenses:?}");
    }

    #[test]
    fn counts_zero_refs_on_unused_def() {
        let src = "def unused(): Int = 1\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let lenses = code_lenses_in_source(&p, &sources, "Main.scuzz", src);
        let unused = lenses.iter().find(|l| src[l.start..l.end] == *"unused");
        assert_eq!(
            unused.map(|l| l.title.as_str()),
            Some("0 refs"),
            "{lenses:?}"
        );
    }
}

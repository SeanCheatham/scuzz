//! Selection ranges from the same parse as `check`. No second typer.

use crate::ast::{Expr, Program};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use crate::symbols::symbols_in_source;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SelRange {
    pub start: usize,
    pub end: usize,
}

/// Nested ranges at `offset`, innermost first: token, exprs, decl, file.
pub fn selection_ranges_in_source(
    program: &Program,
    current_file: &str,
    source: &str,
    offset: usize,
) -> Vec<SelRange> {
    let mut out = Vec::new();
    push_token(source, offset, &mut out);
    let module = module_id_from_label(current_file);
    for d in &program.defs {
        if d.module == module {
            collect_expr(&d.body, offset, &mut out);
        }
    }
    if program.main.module == module && !program.main.name.is_empty() {
        collect_expr(&program.main.body, offset, &mut out);
    }
    for im in &program.impls {
        if im.module == module {
            for m in &im.methods {
                collect_expr(&m.body, offset, &mut out);
            }
        }
    }
    for en in &program.enums {
        if en.module == module {
            for m in &en.methods {
                collect_expr(&m.body, offset, &mut out);
            }
        }
    }
    for s in symbols_in_source(program, current_file, source) {
        push_cover(s.range_start, s.range_end, offset, &mut out);
    }
    if !source.is_empty() {
        push_unique(0, source.len(), &mut out);
    }
    out.sort_by_key(|r| (r.end.saturating_sub(r.start), r.start));
    out.dedup();
    out
}

fn push_token(source: &str, offset: usize, out: &mut Vec<SelRange>) {
    let Ok(toks) = lex(source) else {
        return;
    };
    for t in toks {
        if matches!(t.token, Token::Eof) {
            continue;
        }
        if t.span.start <= offset && offset < t.span.end {
            push_unique(t.span.start, t.span.end, out);
            return;
        }
    }
}

fn collect_expr(e: &Expr, offset: usize, out: &mut Vec<SelRange>) {
    e.for_each_child(|c| collect_expr(c, offset, out));
    push_cover(e.span.start, e.span.end, offset, out);
}

fn push_cover(start: usize, end: usize, offset: usize, out: &mut Vec<SelRange>) {
    if start <= offset && offset <= end && end > start {
        push_unique(start, end, out);
    }
}

fn push_unique(start: usize, end: usize, out: &mut Vec<SelRange>) {
    let r = SelRange { start, end };
    if !out.contains(&r) {
        out.push(r);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn nests_ident_call_and_def() {
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let call = src.rfind("add").unwrap();
        let ranges = selection_ranges_in_source(&p, "Main.scuzz", src, call);
        assert!(ranges.len() >= 3, "{ranges:?}");
        assert_eq!(ranges[0].start, call);
        assert_eq!(&src[ranges[0].start..ranges[0].end], "add");
        assert!(
            ranges
                .iter()
                .any(|r| src[r.start..r.end.min(src.len())].contains("add(1)")),
            "{ranges:?}"
        );
        assert_eq!(ranges.last().unwrap().start, 0);
        assert_eq!(ranges.last().unwrap().end, src.len());
        let mut prev = 0usize;
        for r in &ranges {
            let n = r.end.saturating_sub(r.start);
            assert!(n >= prev, "{ranges:?}");
            prev = n;
        }
    }

    #[test]
    fn nests_param_in_def() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let param = src.find("n:").unwrap();
        let ranges = selection_ranges_in_source(&p, "Main.scuzz", src, param);
        assert_eq!(&src[ranges[0].start..ranges[0].end], "n");
        assert!(
            ranges
                .iter()
                .any(|r| src[r.start..r.end.min(src.len())].contains("def add")),
            "{ranges:?}"
        );
    }

    #[test]
    fn empty_offset_still_has_file() {
        let src = "@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let ranges = selection_ranges_in_source(&p, "Main.scuzz", src, 0);
        assert_eq!(ranges.last().unwrap().end, src.len());
    }
}

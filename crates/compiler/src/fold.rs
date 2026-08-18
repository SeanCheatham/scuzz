//! Folding ranges from the same parse as `check`. No second typer.

use crate::ast::{Expr, ExprKind, Program};
use crate::resolve::module_id_from_label;
use crate::span::offset_to_utf16_pos;
use crate::symbols::symbols_in_source;

pub const FOLD_REGION: &str = "region";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FoldRange {
    pub start: usize,
    pub end: usize,
}

/// Multi-line folds in `current_file`: decls, `for`, `match`, `if`, and lambdas.
pub fn folds_in_source(program: &Program, current_file: &str, source: &str) -> Vec<FoldRange> {
    let mut out = Vec::new();
    for s in symbols_in_source(program, current_file, source) {
        push_multi(source, s.range_start, s.range_end, &mut out);
    }
    let module = module_id_from_label(current_file);
    for d in &program.defs {
        if d.module == module {
            walk_expr(&d.body, source, &mut out);
        }
    }
    if program.main.module == module && !program.main.name.is_empty() {
        walk_expr(&program.main.body, source, &mut out);
    }
    for im in &program.impls {
        if im.module == module {
            for m in &im.methods {
                walk_expr(&m.body, source, &mut out);
            }
        }
    }
    for en in &program.enums {
        if en.module == module {
            for m in &en.methods {
                walk_expr(&m.body, source, &mut out);
            }
        }
    }
    out.sort_by_key(|f| (f.start, f.end));
    out.dedup();
    out
}

fn walk_expr(e: &Expr, source: &str, out: &mut Vec<FoldRange>) {
    match &e.kind {
        ExprKind::For { .. }
        | ExprKind::Match { .. }
        | ExprKind::If { .. }
        | ExprKind::Lambda { .. } => {
            push_multi(source, e.span.start, e.span.end, out);
        }
        _ => {}
    }
    e.for_each_child(|c| walk_expr(c, source, out));
}

fn push_multi(source: &str, start: usize, end: usize, out: &mut Vec<FoldRange>) {
    let (sl, _) = offset_to_utf16_pos(source, start);
    let (el, _) = offset_to_utf16_pos(source, end);
    if el > sl {
        out.push(FoldRange { start, end });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn folds_enum_for_and_match() {
        let src = r#"
enum Color:
  case Red
  case Blue
def describe(c: Color): String =
  c match {
    case Color.Red => "r"
    case Color.Blue => "b"
  }
@main def main: IO[Unit] =
  for {
    c = Color.Red
    _ <- IO.println(describe(c))
  } yield ()
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let folds = folds_in_source(&p, "Main.scuzz", src);
        assert!(
            folds
                .iter()
                .any(|f| src[f.start..f.end].contains("case Red")),
            "{folds:?}"
        );
        assert!(
            folds
                .iter()
                .any(|f| src[f.start..f.end.min(src.len())].contains("case Color.Red")),
            "{folds:?}"
        );
        assert!(
            folds
                .iter()
                .any(|f| src[f.start..f.end].contains("c = Color.Red")),
            "{folds:?}"
        );
    }

    #[test]
    fn skips_one_line_def() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let folds = folds_in_source(&p, "Main.scuzz", src);
        assert!(
            folds
                .iter()
                .all(|f| !src[f.start..f.end.min(src.len())].starts_with("def add")),
            "{folds:?}"
        );
    }

    #[test]
    fn folds_if_and_lambda() {
        let src = r#"
@main def main: IO[Unit] =
  Ui.run(_ =>
    if (true)
      View.text("a")
    else
      View.text("b")
  )
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let folds = folds_in_source(&p, "Main.scuzz", src);
        assert!(
            folds
                .iter()
                .any(|f| src[f.start..f.end.min(src.len())].contains("if (true)")),
            "{folds:?}"
        );
        assert!(
            folds
                .iter()
                .any(|f| src[f.start..f.end.min(src.len())].contains("_ =>")),
            "{folds:?}"
        );
    }
}

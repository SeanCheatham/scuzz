//! Desugar surface sugar (`for`) to the kernel core (`Let` / `FlatMap`).

use crate::ast::{Expr, ExprKind, ForBinder, InterpPart, MatchArm, Program};
use crate::span::Span;

/// Lower all surface sugar in a program (in place conceptually — returns a new tree).
pub fn lower_program(mut program: Program) -> Program {
    for d in &mut program.defs {
        d.body = lower_expr(std::mem::replace(
            &mut d.body,
            Expr::dummy(ExprKind::Unit),
        ));
    }
    program.main.body = lower_expr(std::mem::replace(
        &mut program.main.body,
        Expr::dummy(ExprKind::Unit),
    ));
    program
}

pub fn lower_expr(expr: Expr) -> Expr {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::For { binders, body } => {
            let body = lower_expr(*body);
            desugar_for(binders, body, span)
        }
        ExprKind::IoPrintln(e) => Expr::new(ExprKind::IoPrintln(Box::new(lower_expr(*e))), span),
        ExprKind::IoSleep(e) => Expr::new(ExprKind::IoSleep(Box::new(lower_expr(*e))), span),
        ExprKind::IoFail(e) => Expr::new(ExprKind::IoFail(Box::new(lower_expr(*e))), span),
        ExprKind::IoPure(e) => Expr::new(ExprKind::IoPure(Box::new(lower_expr(*e))), span),
        ExprKind::FlatMap { inner, param, body } => Expr::new(
            ExprKind::FlatMap {
                inner: Box::new(lower_expr(*inner)),
                param,
                body: Box::new(lower_expr(*body)),
            },
            span,
        ),
        ExprKind::HandleErrorWith { inner, body } => Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(lower_expr(*inner)),
                body: Box::new(lower_expr(*body)),
            },
            span,
        ),
        ExprKind::Attempt { inner } => Expr::new(
            ExprKind::Attempt {
                inner: Box::new(lower_expr(*inner)),
            },
            span,
        ),
        ExprKind::IoRace { left, right } => Expr::new(
            ExprKind::IoRace {
                left: Box::new(lower_expr(*left)),
                right: Box::new(lower_expr(*right)),
            },
            span,
        ),
        ExprKind::IoBoth { left, right } => Expr::new(
            ExprKind::IoBoth {
                left: Box::new(lower_expr(*left)),
                right: Box::new(lower_expr(*right)),
            },
            span,
        ),
        ExprKind::Let { name, value, body } => Expr::new(
            ExprKind::Let {
                name,
                value: Box::new(lower_expr(*value)),
                body: Box::new(lower_expr(*body)),
            },
            span,
        ),
        ExprKind::Match { scrutinee, arms } => Expr::new(
            ExprKind::Match {
                scrutinee: Box::new(lower_expr(*scrutinee)),
                arms: arms
                    .into_iter()
                    .map(|MatchArm { pattern, body }| MatchArm {
                        pattern,
                        body: lower_expr(body),
                    })
                    .collect(),
            },
            span,
        ),
        ExprKind::ListLit { elems } => Expr::new(
            ExprKind::ListLit {
                elems: elems.into_iter().map(lower_expr).collect(),
            },
            span,
        ),
        ExprKind::Interpolate { parts } => Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        InterpPart::Lit(s) => InterpPart::Lit(s),
                        InterpPart::Expr(e) => InterpPart::Expr(lower_expr(e)),
                    })
                    .collect(),
            },
            span,
        ),
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => Expr::new(
            ExprKind::If {
                cond: Box::new(lower_expr(*cond)),
                then_branch: Box::new(lower_expr(*then_branch)),
                else_branch: Box::new(lower_expr(*else_branch)),
            },
            span,
        ),
        ExprKind::Binary { op, left, right } => Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(lower_expr(*left)),
                right: Box::new(lower_expr(*right)),
            },
            span,
        ),
        ExprKind::Call { callee, args } => Expr::new(
            ExprKind::Call {
                callee,
                args: args.into_iter().map(lower_expr).collect(),
            },
            span,
        ),
        ExprKind::Lambda { param, body } => Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(lower_expr(*body)),
            },
            span,
        ),
        other => Expr::new(other, span),
    }
}

fn desugar_for(binders: Vec<ForBinder>, body: Expr, span: Span) -> Expr {
    let has_draw = binders.iter().any(|b| matches!(b, ForBinder::Draw { .. }));
    let body = if has_draw {
        // `<-` desugars like monadic `map` on the final yield: wrap pure results.
        // Effects belong in `<-` binders (`_ <- Ui.run(root); yield ()`).
        Expr::new(ExprKind::IoPure(Box::new(body)), span.clone())
    } else {
        body
    };
    binders.into_iter().rev().fold(body, |body, binder| match binder {
        ForBinder::Eq { name, value } => {
            let sp = value.span.clone().cover(&body.span);
            Expr::new(
                ExprKind::Let {
                    name,
                    value: Box::new(lower_expr(value)),
                    body: Box::new(body),
                },
                sp,
            )
        }
        ForBinder::Draw { name, value } => {
            let param = if name == "_" { None } else { Some(name) };
            let sp = value.span.clone().cover(&body.span);
            Expr::new(
                ExprKind::FlatMap {
                    inner: Box::new(lower_expr(value)),
                    param,
                    body: Box::new(body),
                },
                sp,
            )
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    #[test]
    fn lowers_eq_binders_to_lets() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    a = 1
    b = 2
  } yield IO.println("x")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Let { name, body, .. } => {
                assert_eq!(name, "a");
                assert!(matches!(&body.kind, ExprKind::Let { name, .. } if name == "b"));
            }
            other => panic!("expected nested Let, got {other:?}"),
        }
    }

    #[test]
    fn lowers_draw_to_flatmap() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    s <- Fs.read("x")
  } yield IO.println(s)
"#;
        let p = lower_program(parse(src).unwrap());
        assert!(matches!(
            &p.main.body.kind,
            ExprKind::FlatMap {
                param: Some(n),
                ..
            } if n == "s"
        ));
    }
}

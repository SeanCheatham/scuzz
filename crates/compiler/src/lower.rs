//! Desugar surface sugar (`for`) to the kernel core (`Let` / `FlatMap`).

use crate::ast::{Expr, ForBinder, InterpPart, MatchArm, Program};

/// Lower all surface sugar in a program (in place conceptually — returns a new tree).
pub fn lower_program(mut program: Program) -> Program {
    for d in &mut program.defs {
        d.body = lower_expr(std::mem::replace(&mut d.body, Expr::Unit));
    }
    program.main.body = lower_expr(std::mem::replace(&mut program.main.body, Expr::Unit));
    program
}

pub fn lower_expr(expr: Expr) -> Expr {
    match expr {
        Expr::For { binders, body } => {
            let body = lower_expr(*body);
            desugar_for(binders, body)
        }
        Expr::IoPrintln(e) => Expr::IoPrintln(Box::new(lower_expr(*e))),
        Expr::IoSleep(e) => Expr::IoSleep(Box::new(lower_expr(*e))),
        Expr::IoFail(e) => Expr::IoFail(Box::new(lower_expr(*e))),
        Expr::IoPure(e) => Expr::IoPure(Box::new(lower_expr(*e))),
        Expr::FlatMap { inner, param, body } => Expr::FlatMap {
            inner: Box::new(lower_expr(*inner)),
            param,
            body: Box::new(lower_expr(*body)),
        },
        Expr::HandleErrorWith { inner, body } => Expr::HandleErrorWith {
            inner: Box::new(lower_expr(*inner)),
            body: Box::new(lower_expr(*body)),
        },
        Expr::Attempt { inner } => Expr::Attempt {
            inner: Box::new(lower_expr(*inner)),
        },
        Expr::IoRace { left, right } => Expr::IoRace {
            left: Box::new(lower_expr(*left)),
            right: Box::new(lower_expr(*right)),
        },
        Expr::IoBoth { left, right } => Expr::IoBoth {
            left: Box::new(lower_expr(*left)),
            right: Box::new(lower_expr(*right)),
        },
        Expr::Let { name, value, body } => Expr::Let {
            name,
            value: Box::new(lower_expr(*value)),
            body: Box::new(lower_expr(*body)),
        },
        Expr::Match { scrutinee, arms } => Expr::Match {
            scrutinee: Box::new(lower_expr(*scrutinee)),
            arms: arms
                .into_iter()
                .map(|MatchArm { pattern, body }| MatchArm {
                    pattern,
                    body: lower_expr(body),
                })
                .collect(),
        },
        Expr::ListLit { elems } => Expr::ListLit {
            elems: elems.into_iter().map(lower_expr).collect(),
        },
        Expr::Interpolate { parts } => Expr::Interpolate {
            parts: parts
                .into_iter()
                .map(|p| match p {
                    InterpPart::Lit(s) => InterpPart::Lit(s),
                    InterpPart::Expr(e) => InterpPart::Expr(lower_expr(e)),
                })
                .collect(),
        },
        Expr::If {
            cond,
            then_branch,
            else_branch,
        } => Expr::If {
            cond: Box::new(lower_expr(*cond)),
            then_branch: Box::new(lower_expr(*then_branch)),
            else_branch: Box::new(lower_expr(*else_branch)),
        },
        Expr::Binary { op, left, right } => Expr::Binary {
            op,
            left: Box::new(lower_expr(*left)),
            right: Box::new(lower_expr(*right)),
        },
        Expr::Call { callee, args } => Expr::Call {
            callee,
            args: args.into_iter().map(lower_expr).collect(),
        },
        Expr::Lambda { param, body } => Expr::Lambda {
            param,
            body: Box::new(lower_expr(*body)),
        },
        other => other,
    }
}

fn desugar_for(binders: Vec<ForBinder>, body: Expr) -> Expr {
    let has_draw = binders.iter().any(|b| matches!(b, ForBinder::Draw { .. }));
    let body = if has_draw {
        // `<-` desugars like monadic `map` on the final yield: wrap pure results.
        // Effects belong in `<-` binders (`_ <- Ui.run(root); yield ()`).
        Expr::IoPure(Box::new(body))
    } else {
        body
    };
    binders.into_iter().rev().fold(body, |body, binder| match binder {
        ForBinder::Eq { name, value } => Expr::Let {
            name,
            value: Box::new(lower_expr(value)),
            body: Box::new(body),
        },
        ForBinder::Draw { name, value } => {
            let param = if name == "_" { None } else { Some(name) };
            Expr::FlatMap {
                inner: Box::new(lower_expr(value)),
                param,
                body: Box::new(body),
            }
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
        match &p.main.body {
            Expr::Let { name, body, .. } => {
                assert_eq!(name, "a");
                assert!(matches!(body.as_ref(), Expr::Let { name, .. } if name == "b"));
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
            p.main.body,
            Expr::FlatMap {
                param: Some(ref n),
                ..
            } if n == "s"
        ));
    }
}

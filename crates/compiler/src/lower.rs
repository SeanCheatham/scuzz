//! Desugar surface sugar (`for`) to the kernel core (`Let` / `FlatMap`).
//! Also resolve `Enum.Case(args)` calls into `AdtConstruct` when the case is known
//! (keeps `Color.rgb(...)` and other dotted builtins as `Call`).

use crate::ast::{EnumDef, Expr, ExprKind, ForBinder, InterpPart, MatchArm, Program};
use crate::span::Span;
use std::collections::HashSet;

/// Lower all surface sugar in a program (in place conceptually — returns a new tree).
pub fn lower_program(mut program: Program) -> Program {
    let ctors = enum_ctors(&program.enums);
    for d in &mut program.defs {
        d.body = lower_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &ctors,
        );
    }
    program.main.body = lower_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &ctors,
    );
    program
}

fn enum_ctors(enums: &[EnumDef]) -> HashSet<(String, String)> {
    let mut m = HashSet::new();
    for e in enums {
        for c in &e.cases {
            m.insert((e.name.clone(), c.name.clone()));
        }
    }
    m
}

pub fn lower_expr(expr: Expr, ctors: &HashSet<(String, String)>) -> Expr {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::For { binders, body } => {
            let body = lower_expr(*body, ctors);
            desugar_for(binders, body, span, ctors)
        }
        ExprKind::IoPrintln(e) => {
            Expr::new(ExprKind::IoPrintln(Box::new(lower_expr(*e, ctors))), span)
        }
        ExprKind::IoSleep(e) => {
            Expr::new(ExprKind::IoSleep(Box::new(lower_expr(*e, ctors))), span)
        }
        ExprKind::IoFail(e) => Expr::new(ExprKind::IoFail(Box::new(lower_expr(*e, ctors))), span),
        ExprKind::IoPure(e) => Expr::new(ExprKind::IoPure(Box::new(lower_expr(*e, ctors))), span),
        ExprKind::FlatMap { inner, param, body } => Expr::new(
            ExprKind::FlatMap {
                inner: Box::new(lower_expr(*inner, ctors)),
                param,
                body: Box::new(lower_expr(*body, ctors)),
            },
            span,
        ),
        ExprKind::HandleErrorWith { inner, body } => Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(lower_expr(*inner, ctors)),
                body: Box::new(lower_expr(*body, ctors)),
            },
            span,
        ),
        ExprKind::Attempt { inner } => Expr::new(
            ExprKind::Attempt {
                inner: Box::new(lower_expr(*inner, ctors)),
            },
            span,
        ),
        ExprKind::IoRace { left, right } => Expr::new(
            ExprKind::IoRace {
                left: Box::new(lower_expr(*left, ctors)),
                right: Box::new(lower_expr(*right, ctors)),
            },
            span,
        ),
        ExprKind::IoBoth { left, right } => Expr::new(
            ExprKind::IoBoth {
                left: Box::new(lower_expr(*left, ctors)),
                right: Box::new(lower_expr(*right, ctors)),
            },
            span,
        ),
        ExprKind::Let { name, value, body } => Expr::new(
            ExprKind::Let {
                name,
                value: Box::new(lower_expr(*value, ctors)),
                body: Box::new(lower_expr(*body, ctors)),
            },
            span,
        ),
        ExprKind::Match { scrutinee, arms } => Expr::new(
            ExprKind::Match {
                scrutinee: Box::new(lower_expr(*scrutinee, ctors)),
                arms: arms
                    .into_iter()
                    .map(|MatchArm { pattern, body }| MatchArm {
                        pattern,
                        body: lower_expr(body, ctors),
                    })
                    .collect(),
            },
            span,
        ),
        ExprKind::ListLit { elems } => Expr::new(
            ExprKind::ListLit {
                elems: elems.into_iter().map(|e| lower_expr(e, ctors)).collect(),
            },
            span,
        ),
        ExprKind::Interpolate { parts } => Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        InterpPart::Lit(s) => InterpPart::Lit(s),
                        InterpPart::Expr(e) => InterpPart::Expr(lower_expr(e, ctors)),
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
                cond: Box::new(lower_expr(*cond, ctors)),
                then_branch: Box::new(lower_expr(*then_branch, ctors)),
                else_branch: Box::new(lower_expr(*else_branch, ctors)),
            },
            span,
        ),
        ExprKind::Binary { op, left, right } => Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(lower_expr(*left, ctors)),
                right: Box::new(lower_expr(*right, ctors)),
            },
            span,
        ),
        ExprKind::Call { callee, args } => {
            let args: Vec<Expr> = args.into_iter().map(|e| lower_expr(e, ctors)).collect();
            if let Some((enum_name, case_name)) = split_dotted(&callee) {
                if ctors.contains(&(enum_name.clone(), case_name.clone())) {
                    return Expr::new(
                        ExprKind::AdtConstruct {
                            enum_name,
                            case_name,
                            args,
                        },
                        span,
                    );
                }
            }
            Expr::new(ExprKind::Call { callee, args }, span)
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args.into_iter().map(|e| lower_expr(e, ctors)).collect(),
            },
            span,
        ),
        ExprKind::Lambda { param, body } => Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(lower_expr(*body, ctors)),
            },
            span,
        ),
        other => Expr::new(other, span),
    }
}

fn split_dotted(callee: &str) -> Option<(String, String)> {
    let (a, b) = callee.split_once('.')?;
    if a.is_empty() || b.is_empty() || b.contains('.') {
        return None;
    }
    Some((a.to_string(), b.to_string()))
}

fn desugar_for(
    binders: Vec<ForBinder>,
    body: Expr,
    span: Span,
    ctors: &HashSet<(String, String)>,
) -> Expr {
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
                    value: Box::new(lower_expr(value, ctors)),
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
                    inner: Box::new(lower_expr(value, ctors)),
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

    #[test]
    fn resolves_payload_ctor_call_to_adt() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] = Opt.Some(1) match {
  case Opt.Some(n) => IO.println("x")
  case Opt.None => IO.println("n")
}
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, .. } => match &scrutinee.kind {
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args,
                } => {
                    assert_eq!(enum_name, "Opt");
                    assert_eq!(case_name, "Some");
                    assert_eq!(args.len(), 1);
                }
                other => panic!("expected AdtConstruct, got {other:?}"),
            },
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn leaves_color_rgb_as_call() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  for {
    c = Color.rgb(1, 2, 3)
  } yield IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        // Color.rgb is not an enum case → stays Call (builtin).
        fn find_call(e: &Expr) -> bool {
            match &e.kind {
                ExprKind::Call { callee, .. } if callee == "Color.rgb" => true,
                ExprKind::Let { value, body, .. } => find_call(value) || find_call(body),
                ExprKind::AdtConstruct { .. } => false,
                _ => false,
            }
        }
        assert!(find_call(&p.main.body));
    }
}

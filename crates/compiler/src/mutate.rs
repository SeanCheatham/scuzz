//! Residual-oracle mutation: negate / flip / arith / drop `&&` / `0`↔`1` inside
//! `Law.check` / `Law.assert` / `.require` predicates.

use crate::ast::{BinOp, Expr, ExprKind, InterpPart, Program};

fn is_oracle_callee(callee: &str) -> bool {
    callee == "Law.check" || callee == "Law.assert"
}

fn negate_pred(pred: Expr) -> Expr {
    let span = pred.span.clone();
    Expr::new(
        ExprKind::If {
            cond: Box::new(pred),
            then_branch: Box::new(Expr::new(ExprKind::IntLit(0), span.clone())),
            else_branch: Box::new(Expr::new(ExprKind::IntLit(1), span.clone())),
        },
        span,
    )
}

fn flip_op(op: BinOp) -> Option<BinOp> {
    Some(match op {
        BinOp::Eq => BinOp::Ne,
        BinOp::Ne => BinOp::Eq,
        BinOp::Lt => BinOp::Ge,
        BinOp::Le => BinOp::Gt,
        BinOp::Gt => BinOp::Le,
        BinOp::Ge => BinOp::Lt,
        BinOp::And => BinOp::Or,
        BinOp::Or => BinOp::And,
        _ => return None,
    })
}

fn flip_arith(op: BinOp) -> Option<BinOp> {
    Some(match op {
        BinOp::Add => BinOp::Sub,
        BinOp::Sub => BinOp::Add,
        BinOp::Mul => BinOp::Div,
        BinOp::Div => BinOp::Mul,
        BinOp::Mod => BinOp::Mul,
        _ => return None,
    })
}

fn bin_site_count(op: BinOp) -> i32 {
    if flip_op(op).is_some() {
        1 + if op == BinOp::And { 2 } else { 0 }
    } else if flip_arith(op).is_some() {
        1
    } else {
        0
    }
}

fn require_pred_index(args: &[Expr]) -> Option<usize> {
    match args.len() {
        1 => Some(0),
        2 => {
            if matches!(args[0].kind, ExprKind::StrLit(_)) {
                Some(1)
            } else {
                None
            }
        }
        _ => None,
    }
}

fn mutate_expr(e: Expr, target: i32, seen: i32, in_oracle: bool) -> (Expr, i32) {
    let span = e.span.clone();
    match e.kind {
        ExprKind::IntLit(n) => {
            if in_oracle && (n == 0 || n == 1) {
                if seen == target {
                    (
                        Expr::new(ExprKind::IntLit(if n == 0 { 1 } else { 0 }), span),
                        seen + 1,
                    )
                } else {
                    (Expr::new(ExprKind::IntLit(n), span), seen + 1)
                }
            } else {
                (Expr::new(ExprKind::IntLit(n), span), seen)
            }
        }
        ExprKind::Call { callee, args } => {
            if is_oracle_callee(&callee) {
                mutate_oracle(callee, args, span, target, seen)
            } else {
                let (args, seen) = mutate_expr_list(args, target, seen, in_oracle);
                (Expr::new(ExprKind::Call { callee, args }, span), seen)
            }
        }
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => {
            if method == "require" {
                mutate_require(*receiver, args, span, target, seen)
            } else {
                let (receiver, seen) = mutate_expr(*receiver, target, seen, in_oracle);
                let (args, seen) = mutate_expr_list(args, target, seen, in_oracle);
                (
                    Expr::new(
                        ExprKind::MethodCall {
                            receiver: Box::new(receiver),
                            method,
                            args,
                        },
                        span,
                    ),
                    seen,
                )
            }
        }
        ExprKind::Binary { op, left, right } => {
            if in_oracle {
                let n = bin_site_count(op);
                let (left, seen_l) = mutate_expr(*left, target, seen + n, true);
                let (right, seen_r) = mutate_expr(*right, target, seen_l, true);
                let expr = bin_mutant(op, left, right, span, target, seen);
                (expr, seen_r)
            } else {
                let (left, seen) = mutate_expr(*left, target, seen, false);
                let (right, seen) = mutate_expr(*right, target, seen, false);
                (
                    Expr::new(
                        ExprKind::Binary {
                            op,
                            left: Box::new(left),
                            right: Box::new(right),
                        },
                        span,
                    ),
                    seen,
                )
            }
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => {
            let (args, seen) = mutate_expr_list(args, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::AdtConstruct {
                        enum_name,
                        case_name,
                        args,
                        type_args,
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::Let { name, value, body } => {
            let (value, seen) = mutate_expr(*value, target, seen, in_oracle);
            let (body, seen) = mutate_expr(*body, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::Let {
                        name,
                        value: Box::new(value),
                        body: Box::new(body),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::FlatMap { inner, param, body } => {
            let (inner, seen) = mutate_expr(*inner, target, seen, in_oracle);
            let (body, seen) = mutate_expr(*body, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::FlatMap {
                        inner: Box::new(inner),
                        param,
                        body: Box::new(body),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::HandleErrorWith { inner, body } => {
            let (inner, seen) = mutate_expr(*inner, target, seen, in_oracle);
            let (body, seen) = mutate_expr(*body, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::HandleErrorWith {
                        inner: Box::new(inner),
                        body: Box::new(body),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            let (cond, seen) = mutate_expr(*cond, target, seen, in_oracle);
            let (then_branch, seen) = mutate_expr(*then_branch, target, seen, in_oracle);
            let (else_branch, seen) = mutate_expr(*else_branch, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::If {
                        cond: Box::new(cond),
                        then_branch: Box::new(then_branch),
                        else_branch: Box::new(else_branch),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::Lambda { param, body } => {
            let (body, seen) = mutate_expr(*body, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::Lambda {
                        param,
                        body: Box::new(body),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::Match { scrutinee, arms } => {
            let (scrutinee, mut seen) = mutate_expr(*scrutinee, target, seen, in_oracle);
            let mut out = Vec::with_capacity(arms.len());
            for mut arm in arms {
                let (body, s) = mutate_expr(arm.body, target, seen, in_oracle);
                arm.body = body;
                seen = s;
                out.push(arm);
            }
            (
                Expr::new(
                    ExprKind::Match {
                        scrutinee: Box::new(scrutinee),
                        arms: out,
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::For { binders, body } => {
            let mut seen = seen;
            let mut out = Vec::with_capacity(binders.len());
            for b in binders {
                match b {
                    crate::ast::ForBinder::Eq { name, value } => {
                        let (value, s) = mutate_expr(value, target, seen, in_oracle);
                        seen = s;
                        out.push(crate::ast::ForBinder::Eq { name, value });
                    }
                    crate::ast::ForBinder::Draw { name, value } => {
                        let (value, s) = mutate_expr(value, target, seen, in_oracle);
                        seen = s;
                        out.push(crate::ast::ForBinder::Draw { name, value });
                    }
                }
            }
            let (body, seen) = mutate_expr(*body, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::For {
                        binders: out,
                        body: Box::new(body),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::ListLit { elems } => {
            let (elems, seen) = mutate_expr_list(elems, target, seen, in_oracle);
            (Expr::new(ExprKind::ListLit { elems }, span), seen)
        }
        ExprKind::Interpolate { parts } => {
            let mut seen = seen;
            let mut out = Vec::with_capacity(parts.len());
            for p in parts {
                match p {
                    InterpPart::Lit(t) => out.push(InterpPart::Lit(t)),
                    InterpPart::Expr(e) => {
                        let (e, s) = mutate_expr(e, target, seen, in_oracle);
                        seen = s;
                        out.push(InterpPart::Expr(e));
                    }
                }
            }
            (Expr::new(ExprKind::Interpolate { parts: out }, span), seen)
        }
        ExprKind::Field { base, field } => {
            let (base, seen) = mutate_expr(*base, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::Field {
                        base: Box::new(base),
                        field,
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::IoPrintln(arg) => {
            let (arg, seen) = mutate_expr(*arg, target, seen, in_oracle);
            (Expr::new(ExprKind::IoPrintln(Box::new(arg)), span), seen)
        }
        ExprKind::IoFail(arg) => {
            let (arg, seen) = mutate_expr(*arg, target, seen, in_oracle);
            (Expr::new(ExprKind::IoFail(Box::new(arg)), span), seen)
        }
        ExprKind::IoPure(arg) => {
            let (arg, seen) = mutate_expr(*arg, target, seen, in_oracle);
            (Expr::new(ExprKind::IoPure(Box::new(arg)), span), seen)
        }
        ExprKind::IoSleep(arg) => {
            let (arg, seen) = mutate_expr(*arg, target, seen, in_oracle);
            (Expr::new(ExprKind::IoSleep(Box::new(arg)), span), seen)
        }
        ExprKind::Attempt { inner } => {
            let (inner, seen) = mutate_expr(*inner, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::Attempt {
                        inner: Box::new(inner),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::IoRace { left, right } => {
            let (left, seen) = mutate_expr(*left, target, seen, in_oracle);
            let (right, seen) = mutate_expr(*right, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::IoRace {
                        left: Box::new(left),
                        right: Box::new(right),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::IoBoth { left, right } => {
            let (left, seen) = mutate_expr(*left, target, seen, in_oracle);
            let (right, seen) = mutate_expr(*right, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::IoBoth {
                        left: Box::new(left),
                        right: Box::new(right),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::IoEnsure { inner, finalizer } => {
            let (inner, seen) = mutate_expr(*inner, target, seen, in_oracle);
            let (finalizer, seen) = mutate_expr(*finalizer, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::IoEnsure {
                        inner: Box::new(inner),
                        finalizer: Box::new(finalizer),
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::IoTimeout { ms, inner } => {
            let (ms, seen) = mutate_expr(*ms, target, seen, in_oracle);
            let (inner, seen) = mutate_expr(*inner, target, seen, in_oracle);
            (
                Expr::new(
                    ExprKind::IoTimeout {
                        ms: Box::new(ms),
                        inner: Box::new(inner),
                    },
                    span,
                ),
                seen,
            )
        }
        leaf @ (ExprKind::Var(_) | ExprKind::Unit | ExprKind::StrLit(_)) => {
            (Expr { kind: leaf, span }, seen)
        }
    }
}

fn mutate_expr_list(
    xs: Vec<Expr>,
    target: i32,
    mut seen: i32,
    in_oracle: bool,
) -> (Vec<Expr>, i32) {
    let mut out = Vec::with_capacity(xs.len());
    for x in xs {
        let (e, s) = mutate_expr(x, target, seen, in_oracle);
        seen = s;
        out.push(e);
    }
    (out, seen)
}

fn mutate_oracle(
    callee: String,
    args: Vec<Expr>,
    span: crate::span::Span,
    target: i32,
    seen: i32,
) -> (Expr, i32) {
    if args.len() < 2 {
        let (args, seen) = mutate_expr_list(args, target, seen, false);
        return (Expr::new(ExprKind::Call { callee, args }, span), seen);
    }
    let mut args = args;
    let rest: Vec<Expr> = args.split_off(2);
    let pred_orig = args[1].clone();
    let (name_e, seen_n) = mutate_expr(args[0].clone(), target, seen + 1, false);
    let (pred_m, seen_p) = mutate_expr(args[1].clone(), target, seen_n, true);
    let (rest, seen_r) = mutate_expr_list(rest, target, seen_p, false);
    let pred_out = if seen == target {
        negate_pred(pred_orig)
    } else {
        pred_m
    };
    let mut out = vec![name_e, pred_out];
    out.extend(rest);
    (
        Expr::new(ExprKind::Call { callee, args: out }, span),
        seen_r,
    )
}

fn mutate_require(
    receiver: Expr,
    args: Vec<Expr>,
    span: crate::span::Span,
    target: i32,
    seen: i32,
) -> (Expr, i32) {
    let Some(pred_idx) = require_pred_index(&args) else {
        let (receiver, seen) = mutate_expr(receiver, target, seen, false);
        let (args, seen) = mutate_expr_list(args, target, seen, false);
        return (
            Expr::new(
                ExprKind::MethodCall {
                    receiver: Box::new(receiver),
                    method: "require".into(),
                    args,
                },
                span,
            ),
            seen,
        );
    };
    let neg_site = seen;
    let (receiver, mut seen) = mutate_expr(receiver, target, seen + 1, false);
    let mut out = Vec::with_capacity(args.len());
    for (i, x) in args.into_iter().enumerate() {
        if i == pred_idx {
            let orig = x.clone();
            let (p, s) = mutate_expr(x, target, seen, true);
            seen = s;
            out.push(if neg_site == target {
                negate_pred(orig)
            } else {
                p
            });
        } else {
            let (p, s) = mutate_expr(x, target, seen, false);
            seen = s;
            out.push(p);
        }
    }
    (
        Expr::new(
            ExprKind::MethodCall {
                receiver: Box::new(receiver),
                method: "require".into(),
                args: out,
            },
            span,
        ),
        seen,
    )
}

fn bin_mutant(
    op: BinOp,
    left: Expr,
    right: Expr,
    span: crate::span::Span,
    target: i32,
    seen: i32,
) -> Expr {
    let expr = |op: BinOp, left: Expr, right: Expr| {
        Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            },
            span.clone(),
        )
    };
    if let Some(flipped) = flip_op(op) {
        if seen == target {
            return expr(flipped, left, right);
        }
        if op == BinOp::And {
            if seen + 1 == target {
                return left;
            }
            if seen + 2 == target {
                return right;
            }
        }
        expr(op, left, right)
    } else if let Some(arith) = flip_arith(op) {
        if seen == target {
            expr(arith, left, right)
        } else {
            expr(op, left, right)
        }
    } else {
        expr(op, left, right)
    }
}

fn mutate_prog_at(program: Program, target: i32) -> (Program, i32) {
    let mut program = program;
    let mut seen = 0;
    for d in &mut program.defs {
        let body = std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit));
        let (body, s) = mutate_expr(body, target, seen, false);
        d.body = body;
        seen = s;
    }
    let main = std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit));
    let (main, seen) = mutate_expr(main, target, seen, false);
    program.main.body = main;
    (program, seen)
}

/// Count residual oracle mutation sites on a verify-prepared program.
pub fn mutate_count(program: &Program) -> i32 {
    mutate_prog_at(program.clone(), -1).1
}

/// Apply site `target` (0-based) on a verify-prepared program.
pub fn mutate_apply(program: Program, target: i32) -> Program {
    mutate_prog_at(program, target).0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::overlay::residualize_refinements;
    use crate::parser::parse;

    #[test]
    fn hello_has_no_sites() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("Hello, Scuzz!").flatMap(_ => IO.println("ready."))
"#,
        )
        .unwrap();
        assert_eq!(mutate_count(&p), 0);
    }

    #[test]
    fn require_and_where_count_sites() {
        let mut p = parse(
            r#"
record Point(x: Int where x >= 0, y: Int where y == y)

@main def main: IO[Unit] =
  IO.println(Str.fromInt(Point(3, 5).x)).require(1 == 1)
"#,
        )
        .unwrap();
        residualize_refinements(&mut p);
        assert!(mutate_count(&p) > 0);
    }

    #[test]
    fn negate_require_pred() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("x").require(1 == 1)
"#,
        )
        .unwrap();
        let n = mutate_count(&p);
        assert!(n >= 1);
        let m = mutate_apply(p, 0);
        let dumped = format!("{:?}", m.main.body.kind);
        assert!(
            dumped.contains("If") || dumped.contains("IntLit(0)"),
            "expected negated pred: {dumped}"
        );
    }
}

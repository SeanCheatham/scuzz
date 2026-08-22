//! Mutation of live `def` bodies and residual oracle predicates.
//!
//! Program mode mutates live code and keeps residual oracles armed.
//! Oracle mode mutates `Law.check` / `Law.assert` / `.require` predicates.

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, FunDef, Program};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MutateMode {
    /// Mutate live `def` / `@main` bodies. Skip law and driver bodies and
    /// residual oracle predicates.
    Program,
    /// Mutate residual `Law.check` / `Law.assert` / `.require` predicates.
    Oracles,
}

fn is_oracle_callee(callee: &str) -> bool {
    callee == "Law.check" || callee == "Law.assert"
}

fn negate_pred(pred: Expr) -> Expr {
    let span = pred.span.clone();
    Expr::new(
        ExprKind::If {
            cond: Box::new(pred),
            then_branch: Box::new(Expr::new(ExprKind::BoolLit(false), span.clone())),
            else_branch: Box::new(Expr::new(ExprKind::BoolLit(true), span.clone())),
            implicit_else: false,
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
        BinOp::BitAnd => BinOp::BitOr,
        BinOp::BitOr => BinOp::BitAnd,
        BinOp::BitXor => BinOp::BitAnd,
        BinOp::Shl => BinOp::Shr,
        BinOp::Shr => BinOp::Shl,
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

fn sibling_cases<'a>(
    enums: &'a [EnumDef],
    enum_name: &str,
    case_name: &str,
    nargs: usize,
) -> Vec<&'a str> {
    let Some(en) = enums.iter().find(|e| {
        e.name == enum_name
            || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == enum_name)
            || enum_name.ends_with(&format!(".{}", e.name))
    }) else {
        return Vec::new();
    };
    en.cases
        .iter()
        .filter(|c| c.name != case_name && c.fields.len() == nargs)
        .map(|c| c.name.as_str())
        .collect()
}

struct MutCx<'a> {
    mode: MutateMode,
    enums: &'a [EnumDef],
}

fn live_site(mode: MutateMode, in_oracle: bool) -> bool {
    match mode {
        MutateMode::Program => !in_oracle,
        MutateMode::Oracles => in_oracle,
    }
}

fn mutate_expr(e: Expr, target: i32, seen: i32, in_oracle: bool, cx: &MutCx<'_>) -> (Expr, i32) {
    let span = e.span.clone();
    match e.kind {
        ExprKind::IntLit(n) if (n == 0 || n == 1) && live_site(cx.mode, in_oracle) => {
            let flipped = if n == 0 { 1 } else { 0 };
            let out = if seen == target { flipped } else { n };
            (Expr::new(ExprKind::IntLit(out), span), seen + 1)
        }
        ExprKind::Call { callee, args } if is_oracle_callee(&callee) => {
            mutate_oracle(callee, args, span, target, seen, cx)
        }
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } if method == "require" => mutate_require(*receiver, args, span, target, seen, cx),
        ExprKind::Binary { op, left, right } if live_site(cx.mode, in_oracle) => {
            let n = bin_site_count(op);
            let (left, seen_l) = mutate_expr(*left, target, seen + n, in_oracle, cx);
            let (right, seen_r) = mutate_expr(*right, target, seen_l, in_oracle, cx);
            (bin_mutant(op, left, right, span, target, seen), seen_r)
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
            implicit_else,
        } if cx.mode == MutateMode::Program && !in_oracle => {
            let swap = seen;
            let (cond, seen) = mutate_expr(*cond, target, seen + 1, false, cx);
            let (then_branch, seen) = mutate_expr(*then_branch, target, seen, false, cx);
            let (else_branch, seen) = mutate_expr(*else_branch, target, seen, false, cx);
            let (then_branch, else_branch, implicit_else) = if swap == target {
                (else_branch, then_branch, false)
            } else {
                (then_branch, else_branch, implicit_else)
            };
            (
                Expr::new(
                    ExprKind::If {
                        cond: Box::new(cond),
                        then_branch: Box::new(then_branch),
                        else_branch: Box::new(else_branch),
                        implicit_else,
                    },
                    span,
                ),
                seen,
            )
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } if cx.mode == MutateMode::Program && !in_oracle => {
            let sibs = sibling_cases(cx.enums, &enum_name, &case_name, args.len());
            let n = sibs.len() as i32;
            let chosen = if target >= seen && target < seen + n {
                sibs[(target - seen) as usize].to_string()
            } else {
                case_name.clone()
            };
            let (args, seen) = mutate_expr_list(args, target, seen + n, false, cx);
            (
                Expr::new(
                    ExprKind::AdtConstruct {
                        enum_name,
                        case_name: chosen,
                        args,
                        type_args,
                    },
                    span,
                ),
                seen,
            )
        }
        kind => {
            let mut seen = seen;
            let expr = Expr { kind, span }.map_children(|c| {
                let (out, next) = mutate_expr(c, target, seen, in_oracle, cx);
                seen = next;
                out
            });
            (expr, seen)
        }
    }
}

fn mutate_expr_list(
    xs: Vec<Expr>,
    target: i32,
    mut seen: i32,
    in_oracle: bool,
    cx: &MutCx<'_>,
) -> (Vec<Expr>, i32) {
    let mut out = Vec::with_capacity(xs.len());
    for x in xs {
        let (e, s) = mutate_expr(x, target, seen, in_oracle, cx);
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
    cx: &MutCx<'_>,
) -> (Expr, i32) {
    if cx.mode == MutateMode::Program {
        let (args, seen) = mutate_expr_list(args, target, seen, true, cx);
        return (Expr::new(ExprKind::Call { callee, args }, span), seen);
    }
    if args.len() < 2 {
        let (args, seen) = mutate_expr_list(args, target, seen, false, cx);
        return (Expr::new(ExprKind::Call { callee, args }, span), seen);
    }
    let mut args = args;
    let rest: Vec<Expr> = args.split_off(2);
    let pred_orig = args[1].clone();
    let (name_e, seen_n) = mutate_expr(args[0].clone(), target, seen + 1, false, cx);
    let (pred_m, seen_p) = mutate_expr(args[1].clone(), target, seen_n, true, cx);
    let (rest, seen_r) = mutate_expr_list(rest, target, seen_p, false, cx);
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
    cx: &MutCx<'_>,
) -> (Expr, i32) {
    if cx.mode == MutateMode::Program {
        let (receiver, seen) = mutate_expr(receiver, target, seen, false, cx);
        let (args, seen) = mutate_expr_list(args, target, seen, true, cx);
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
    }
    let Some(pred_idx) = require_pred_index(&args) else {
        let (receiver, seen) = mutate_expr(receiver, target, seen, false, cx);
        let (args, seen) = mutate_expr_list(args, target, seen, false, cx);
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
    let (receiver, mut seen) = mutate_expr(receiver, target, seen + 1, false, cx);
    let mut out = Vec::with_capacity(args.len());
    for (i, x) in args.into_iter().enumerate() {
        if i == pred_idx {
            let orig = x.clone();
            let (p, s) = mutate_expr(x, target, seen, true, cx);
            seen = s;
            out.push(if neg_site == target {
                negate_pred(orig)
            } else {
                p
            });
        } else {
            let (p, s) = mutate_expr(x, target, seen, false, cx);
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

fn mutate_def_body(d: &FunDef, cx: &MutCx<'_>) -> bool {
    match cx.mode {
        MutateMode::Program => !d.is_law && !d.is_driver,
        MutateMode::Oracles => true,
    }
}

fn mutate_prog_at(mut program: Program, target: i32, mode: MutateMode) -> (Program, i32) {
    let enums = program.enums.clone();
    let cx = MutCx {
        mode,
        enums: &enums,
    };
    let mut seen = 0;
    for d in &mut program.defs {
        if !mutate_def_body(d, &cx) {
            continue;
        }
        let body = std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit));
        let (body, s) = mutate_expr(body, target, seen, false, &cx);
        seen = s;
        d.body = body;
    }
    let body = std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit));
    let (body, s) = mutate_expr(body, target, seen, false, &cx);
    seen = s;
    program.main.body = body;
    (program, seen)
}

pub fn mutate_count_mode(program: &Program, mode: MutateMode) -> i32 {
    mutate_prog_at(program.clone(), -1, mode).1
}

pub fn mutate_apply_mode(program: Program, target: i32, mode: MutateMode) -> Program {
    mutate_prog_at(program, target, mode).0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lower::lower_program;
    use crate::overlay::residualize_refinements;
    use crate::parser::parse;

    #[test]
    fn hello_has_no_oracle_sites() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("Hello, Scuzz!").flatMap(_ => IO.println("ready."))
"#,
        )
        .unwrap();
        assert_eq!(mutate_count_mode(&p, MutateMode::Oracles), 0);
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
        assert!(mutate_count_mode(&p, MutateMode::Oracles) > 0);
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
        let n = mutate_count_mode(&p, MutateMode::Oracles);
        assert!(n >= 1);
        let m = mutate_apply_mode(p, 0, MutateMode::Oracles);
        let dumped = format!("{:?}", m.main.body.kind);
        assert!(
            dumped.contains("If") || dumped.contains("IntLit(0)"),
            "expected negated pred: {dumped}"
        );
    }

    #[test]
    fn program_mode_flips_add_in_live_def() {
        let p = lower_program(
            parse(
                r#"
def sum(a: Int, b: Int): Int = a + b
@main def main: IO[Unit] = IO.println("x")
"#,
            )
            .unwrap(),
        );
        let n = mutate_count_mode(&p, MutateMode::Program);
        assert!(n >= 1, "expected live arith site, got {n}");
        let m = mutate_apply_mode(p, 0, MutateMode::Program);
        let dumped = format!("{:?}", m.defs[0].body.kind);
        assert!(dumped.contains("Sub"), "expected + flipped to -: {dumped}");
    }

    #[test]
    fn program_mode_skips_oracle_predicates() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("x").require(1 == 1)
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        assert_eq!(n, 0, "require pred is not a live-code site");
    }

    #[test]
    fn program_mode_swaps_if_arms() {
        let p = parse(
            r#"
def pick(n: Int): Int = if (n > 0) 1 else 2
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        assert!(n >= 1);
        let mut found = false;
        for i in 0..n {
            let m = mutate_apply_mode(p.clone(), i, MutateMode::Program);
            let dumped = format!("{:?}", m.defs[0].body.kind);
            if dumped.contains("IntLit(2)") && dumped.contains("IntLit(1)") {
                found = true;
                break;
            }
        }
        assert!(found, "expected an if-arm swap among {n} sites");
    }
}

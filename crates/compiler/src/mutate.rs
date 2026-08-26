//! Mutation of live `def` bodies and residual oracle predicates.
//!
//! Program mode mutates live code and keeps residual oracles armed.
//! Oracle mode mutates `Property.check` / `Property.assert` / `.require` predicates.

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, FunDef, Program};
use crate::span::{offset_to_line_col, Span};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MutateMode {
    /// Mutate live `def` / `@main` bodies. Skip verify and driver bodies and residual oracle predicates.
    Program,
    /// Mutate residual `Property.check` / `Property.assert` / `.require` predicates.
    Oracles,
}

/// One applied mutant: enclosing def, span, and a short label.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MutantDesc {
    pub def: String,
    pub module: String,
    pub file: String,
    pub start: usize,
    pub end: usize,
    pub label: String,
}

impl MutantDesc {
    /// `file:line` when source is present. Otherwise `file`.
    pub fn location(&self, source: Option<&str>) -> String {
        let file = if self.file.is_empty() {
            "<input>"
        } else {
            self.file.as_str()
        };
        match source {
            Some(src) => {
                let (line, _) = offset_to_line_col(src, self.start);
                format!("{file}:{line}")
            }
            None => file.to_string(),
        }
    }

    /// Trimmed source line that holds the mutant span start.
    pub fn excerpt(&self, source: &str) -> String {
        let start = self.start.min(source.len());
        let line_start = source[..start].rfind('\n').map(|i| i + 1).unwrap_or(0);
        let line_end = source[start..]
            .find('\n')
            .map(|i| start + i)
            .unwrap_or(source.len());
        source[line_start..line_end].trim().to_string()
    }
}

/// Residual `Property.check` / `Property.assert` / `.require` site.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OracleSite {
    pub def: String,
    pub module: String,
    pub file: String,
    pub start: usize,
    refs: Vec<String>,
}

impl OracleSite {
    fn observes(&self, def: &str) -> bool {
        self.def == def || self.refs.iter().any(|n| n == def)
    }
}

fn is_oracle_callee(callee: &str) -> bool {
    callee == "Property.check" || callee == "Property.assert"
}

/// Tap-handler widgets whose second argument is the handler lambda.
const HANDLER_WIDGETS: [&str; 6] = [
    "View.button",
    "View.iconButton",
    "View.outlinedButton",
    "View.textButton",
    "View.fab",
    "View.inkWell",
];

fn is_handler_widget(callee: &str) -> bool {
    HANDLER_WIDGETS.contains(&callee)
}

/// `(label, handler body)` per handler-widget call in `e`, in walk order.
fn collect_handlers(e: &Expr, out: &mut Vec<(String, Expr)>) {
    if let ExprKind::Call { callee, args } = &e.kind {
        if is_handler_widget(callee) && args.len() >= 2 {
            if let ExprKind::Lambda { body, .. } = &args[1].kind {
                let label = match &args[0].kind {
                    ExprKind::StrLit(s) => s.clone(),
                    _ => String::new(),
                };
                out.push((label, (**body).clone()));
            }
        }
    }
    let e = e.clone();
    e.map_children(|c| {
        collect_handlers(&c, out);
        c
    });
}

fn callee_base(name: &str) -> &str {
    name.rsplit('.').next().unwrap_or(name)
}

fn push_ref(name: &str, out: &mut Vec<String>) {
    let base = callee_base(name);
    if !out.iter().any(|n| n == base) {
        out.push(base.to_string());
    }
    if base != name && !out.iter().any(|n| n == name) {
        out.push(name.to_string());
    }
}

fn collect_refs(e: &Expr, out: &mut Vec<String>) {
    match &e.kind {
        ExprKind::Var(name) => push_ref(name, out),
        ExprKind::Call { callee, args } => {
            if !is_oracle_callee(callee) {
                push_ref(callee, out);
            }
            for a in args {
                collect_refs(a, out);
            }
        }
        ExprKind::MethodCall {
            receiver,
            method: _,
            args,
        } => {
            collect_refs(receiver, out);
            for a in args {
                collect_refs(a, out);
            }
        }
        _ => e.for_each_child(|c| collect_refs(c, out)),
    }
}

fn oracle_site(def: &str, module: &str, span: &Span, refs_from: &Expr) -> OracleSite {
    let mut refs = Vec::new();
    collect_refs(refs_from, &mut refs);
    OracleSite {
        def: def.to_string(),
        module: module.to_string(),
        file: span.file.clone(),
        start: span.start,
        refs,
    }
}

fn collect_oracles_in_expr(e: &Expr, def: &str, module: &str, out: &mut Vec<OracleSite>) {
    match &e.kind {
        ExprKind::Call { callee, .. } if is_oracle_callee(callee) => {
            out.push(oracle_site(def, module, &e.span, e));
        }
        ExprKind::MethodCall { method, .. } if method == "require" => {
            out.push(oracle_site(def, module, &e.span, e));
        }
        _ => {}
    }
    e.for_each_child(|c| collect_oracles_in_expr(c, def, module, out));
}

/// Collect residual oracle sites from the verify program.
pub fn collect_oracle_sites(program: &Program) -> Vec<OracleSite> {
    let mut out = Vec::new();
    for d in &program.defs {
        collect_oracles_in_expr(&d.body, &d.name, &d.module, &mut out);
    }
    if !program.main.name.is_empty() {
        collect_oracles_in_expr(
            &program.main.body,
            &program.main.name,
            &program.main.module,
            &mut out,
        );
    }
    out
}

fn closest_oracle<'a>(
    desc: &MutantDesc,
    oracles: impl Iterator<Item = &'a OracleSite>,
) -> Option<&'a OracleSite> {
    oracles.min_by(|a, b| {
        let da = (a.start as i64 - desc.start as i64).unsigned_abs();
        let db = (b.start as i64 - desc.start as i64).unsigned_abs();
        da.cmp(&db)
            .then(a.start.cmp(&b.start))
            .then(a.def.cmp(&b.def))
    })
}

/// Name of the nearest observing oracle, or a no-oracle report.
///
/// Same def first. Else closest span in the same module that names the mutated
/// def. Else `no oracle observes <def>`.
pub fn nearest_oracle(desc: &MutantDesc, oracles: &[OracleSite]) -> String {
    if let Some(hit) = closest_oracle(desc, oracles.iter().filter(|o| o.def == desc.def)) {
        return hit.def.clone();
    }
    if let Some(hit) = closest_oracle(
        desc,
        oracles
            .iter()
            .filter(|o| o.module == desc.module && o.observes(&desc.def)),
    ) {
        return hit.def.clone();
    }
    if let Some(hit) = closest_oracle(desc, oracles.iter().filter(|o| o.observes(&desc.def))) {
        return hit.def.clone();
    }
    format!("no oracle observes `{}`", desc.def)
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

fn op_sym(op: BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Mod => "%",
        BinOp::Eq => "==",
        BinOp::Ne => "!=",
        BinOp::Lt => "<",
        BinOp::Le => "<=",
        BinOp::Gt => ">",
        BinOp::Ge => ">=",
        BinOp::And => "&&",
        BinOp::Or => "||",
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "^",
        BinOp::Shl => "<<",
        BinOp::Shr => ">>",
    }
}

fn flip_label(from: BinOp, to: BinOp) -> String {
    format!("flip `{}` to `{}`", op_sym(from), op_sym(to))
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

/// A residualized `where` bound: `Property.check("<def>.<param>", pred, Var(param))`.
/// The dropped mutant passes the value through unchecked.
fn where_residual_value(callee: &str, args: &[Expr]) -> Option<Expr> {
    if callee != "Property.check" || args.len() != 3 {
        return None;
    }
    match (&args[0].kind, &args[2].kind) {
        (ExprKind::StrLit(name), ExprKind::Var(v)) if name.ends_with(&format!(".{v}")) => {
            Some(args[2].clone())
        }
        _ => None,
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
    def: String,
    module: String,
    desc: Option<MutantDesc>,
    handlers: Vec<(String, Expr)>,
    handler_cursor: usize,
}

fn note(cx: &mut MutCx<'_>, seen: i32, target: i32, span: &Span, label: String) {
    if seen == target && cx.desc.is_none() {
        cx.desc = Some(MutantDesc {
            def: cx.def.clone(),
            module: cx.module.clone(),
            file: span.file.clone(),
            start: span.start,
            end: span.end,
            label,
        });
    }
}

fn live_site(mode: MutateMode, in_oracle: bool) -> bool {
    match mode {
        MutateMode::Program => !in_oracle,
        MutateMode::Oracles => in_oracle,
    }
}

fn mutate_expr(
    e: Expr,
    target: i32,
    seen: i32,
    in_oracle: bool,
    cx: &mut MutCx<'_>,
) -> (Expr, i32) {
    let span = e.span.clone();
    match e.kind {
        ExprKind::IntLit(n) if (n == 0 || n == 1) && live_site(cx.mode, in_oracle) => {
            let flipped = if n == 0 { 1 } else { 0 };
            note(cx, seen, target, &span, format!("{n} -> {flipped}"));
            let out = if seen == target { flipped } else { n };
            (Expr::new(ExprKind::IntLit(out), span), seen + 1)
        }
        ExprKind::Call { callee, args } if is_oracle_callee(&callee) => {
            mutate_oracle(callee, args, span, target, seen, cx)
        }
        ExprKind::Call { callee, args }
            if is_handler_widget(&callee)
                && args.len() >= 2
                && matches!(args[1].kind, ExprKind::Lambda { .. }) =>
        {
            handler_mutant(callee, args, span, target, seen, in_oracle, cx)
        }
        ExprKind::Call { callee, args } if callee == "Signal.map" && args.len() == 2 => {
            signal_map_mutant(callee, args, span, target, seen, in_oracle, cx)
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
            (bin_mutant(op, left, right, span, target, seen, cx), seen_r)
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
            implicit_else,
        } if cx.mode == MutateMode::Program && !in_oracle => {
            let swap = seen;
            note(cx, swap, target, &span, "swap if arms".into());
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
            if target >= seen && target < seen + n {
                let new = sibs[(target - seen) as usize];
                note(
                    cx,
                    target,
                    target,
                    &span,
                    format!("sibling case {case_name} -> {new}"),
                );
            }
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
    cx: &mut MutCx<'_>,
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
    cx: &mut MutCx<'_>,
) -> (Expr, i32) {
    if cx.mode == MutateMode::Program {
        let (args, seen) = mutate_expr_list(args, target, seen, true, cx);
        return (Expr::new(ExprKind::Call { callee, args }, span), seen);
    }
    if args.len() < 2 {
        let (args, seen) = mutate_expr_list(args, target, seen, false, cx);
        return (Expr::new(ExprKind::Call { callee, args }, span), seen);
    }
    note(cx, seen, target, &span, "negate predicate".into());
    let drop_value = where_residual_value(&callee, &args);
    if drop_value.is_some() {
        note(cx, seen + 1, target, &span, "drop where bound".into());
    }
    let base = seen + 1 + i32::from(drop_value.is_some());
    let mut args = args;
    let rest: Vec<Expr> = args.split_off(2);
    let pred_orig = args[1].clone();
    let (name_e, seen_n) = mutate_expr(args[0].clone(), target, base, false, cx);
    let (pred_m, seen_p) = mutate_expr(args[1].clone(), target, seen_n, true, cx);
    let (rest, seen_r) = mutate_expr_list(rest, target, seen_p, false, cx);
    if let Some(value) = drop_value {
        if seen + 1 == target {
            return (value, seen_r);
        }
    }
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

/// Handler-swap: replace a tap handler's body with the next sibling handler's
/// body (rotation over the def's handler widgets). Ill-typed transplants fail
/// at compile and count as killed.
fn handler_mutant(
    callee: String,
    args: Vec<Expr>,
    span: crate::span::Span,
    target: i32,
    seen: i32,
    in_oracle: bool,
    cx: &mut MutCx<'_>,
) -> (Expr, i32) {
    let idx = cx.handler_cursor;
    cx.handler_cursor += 1;
    let total = cx.handlers.len();
    let active = live_site(cx.mode, in_oracle) && total >= 2;
    let label = if active {
        cx.handlers
            .get(idx)
            .map(|h| h.0.clone())
            .unwrap_or_default()
    } else {
        String::new()
    };
    let next_body = if active {
        Some(cx.handlers[(idx + 1) % total].1.clone())
    } else {
        None
    };
    if active {
        note(
            cx,
            seen,
            target,
            &span,
            format!("swap handler of `{label}` with sibling"),
        );
    }
    let base = seen + i32::from(active);
    let (mut args, seen_out) = mutate_expr_list(args, target, base, in_oracle, cx);
    if active && seen == target {
        if let (Some(next), ExprKind::Lambda { body, .. }) = (next_body, &mut args[1].kind) {
            **body = next;
        }
    }
    (Expr::new(ExprKind::Call { callee, args }, span), seen_out)
}

/// Signal-map identity: replace the transform with the identity so a derived
/// display stops updating. Only named single-param lambdas get a site.
fn signal_map_mutant(
    callee: String,
    args: Vec<Expr>,
    span: crate::span::Span,
    target: i32,
    seen: i32,
    in_oracle: bool,
    cx: &mut MutCx<'_>,
) -> (Expr, i32) {
    let identity_param = match args.get(1).map(|a| &a.kind) {
        Some(ExprKind::Lambda {
            param: Some(p),
            pat: None,
            ..
        }) if p != "_" => Some(p.clone()),
        _ => None,
    };
    let active = live_site(cx.mode, in_oracle) && identity_param.is_some();
    if active {
        note(cx, seen, target, &span, "Signal.map identity".into());
    }
    let base = seen + i32::from(active);
    let (mut args, seen_out) = mutate_expr_list(args, target, base, in_oracle, cx);
    if active && seen == target {
        if let Some(p) = identity_param {
            let lspan = args[1].span.clone();
            if let ExprKind::Lambda { body, .. } = &mut args[1].kind {
                **body = Expr::new(ExprKind::Var(p), lspan);
            }
        }
    }
    (Expr::new(ExprKind::Call { callee, args }, span), seen_out)
}

fn mutate_require(
    receiver: Expr,
    args: Vec<Expr>,
    span: crate::span::Span,
    target: i32,
    seen: i32,
    cx: &mut MutCx<'_>,
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
    note(cx, neg_site, target, &span, "negate predicate".into());
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
    cx: &mut MutCx<'_>,
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
        note(cx, seen, target, &span, flip_label(op, flipped));
        if seen == target {
            return expr(flipped, left, right);
        }
        if op == BinOp::And {
            note(cx, seen + 1, target, &span, "drop left of `&&`".into());
            note(cx, seen + 2, target, &span, "drop right of `&&`".into());
            if seen + 1 == target {
                return left;
            }
            if seen + 2 == target {
                return right;
            }
        }
        expr(op, left, right)
    } else if let Some(arith) = flip_arith(op) {
        note(cx, seen, target, &span, flip_label(op, arith));
        if seen == target {
            expr(arith, left, right)
        } else {
            expr(op, left, right)
        }
    } else {
        expr(op, left, right)
    }
}

fn mutate_def_body(d: &FunDef) -> bool {
    !d.is_driver && !d.is_verify
}

fn mutate_prog_at(
    mut program: Program,
    target: i32,
    mode: MutateMode,
) -> (Program, i32, Option<MutantDesc>) {
    let enums = program.enums.clone();
    let mut cx = MutCx {
        mode,
        enums: &enums,
        def: String::new(),
        module: String::new(),
        desc: None,
        handlers: Vec::new(),
        handler_cursor: 0,
    };
    let mut seen = 0;
    for d in &mut program.defs {
        if !mutate_def_body(d) {
            continue;
        }
        cx.def = d.name.clone();
        cx.module = d.module.clone();
        cx.handlers = {
            let mut h = Vec::new();
            collect_handlers(&d.body, &mut h);
            h
        };
        cx.handler_cursor = 0;
        let body = std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit));
        let (body, s) = mutate_expr(body, target, seen, false, &mut cx);
        seen = s;
        d.body = body;
    }
    cx.def = program.main.name.clone();
    cx.module = program.main.module.clone();
    cx.handlers = {
        let mut h = Vec::new();
        collect_handlers(&program.main.body, &mut h);
        h
    };
    cx.handler_cursor = 0;
    let body = std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit));
    let (body, s) = mutate_expr(body, target, seen, false, &mut cx);
    seen = s;
    program.main.body = body;
    let desc = cx.desc;
    (program, seen, desc)
}

pub fn mutate_count_mode(program: &Program, mode: MutateMode) -> i32 {
    mutate_prog_at(program.clone(), -1, mode).1
}

pub fn mutate_apply_mode(program: Program, target: i32, mode: MutateMode) -> Program {
    mutate_prog_at(program, target, mode).0
}

/// Describe the mutant at `site` without requiring the mutated program.
pub fn mutate_describe(program: &Program, site: i32, mode: MutateMode) -> Option<MutantDesc> {
    mutate_prog_at(program.clone(), site, mode).2
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lower::lower_program;
    use crate::overlay::residualize_refinements;
    use crate::parser::{parse, parse_file};
    use crate::verify::{apply_verifies, VerifySource};
    use std::path::PathBuf;

    #[test]
    fn program_mode_skips_verify_helpers() {
        let mut p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("x")
"#,
        )
        .unwrap();
        apply_verifies(
            &mut p,
            &[VerifySource {
                label: "pkg/count.scuzz_verify".into(),
                text: concat!(
                    "private def afterHitShows(t: Timeline, hit: String, needle: String): Verdict =\n",
                    "  Verdict.any(t, i => Timeline.lastHitHas(t, i, hit) && Timeline.exists(t, j => j >= i && Timeline.a11yHas(t, j, needle)))\n",
                    "def afterPlusShowsOne(t: Timeline): Verdict =\n",
                    "  afterHitShows(t, \"button:+1\", \"text:count = 1\")\n",
                )
                .into(),
                path: PathBuf::new(),
            }],
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                assert_ne!(d.def, "afterHitShows", "verify helper mutated: {d:?}");
                assert_ne!(d.def, "afterPlusShowsOne", "verify pred mutated: {d:?}");
            }
        }
    }

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
    fn oracles_drop_where_bound() {
        let mut p = parse(
            r#"
def note(n: Int where n >= 0): Int = n

@main def main: IO[Unit] =
  IO.println(Str.fromInt(note(3)))
"#,
        )
        .unwrap();
        residualize_refinements(&mut p);
        let n = mutate_count_mode(&p, MutateMode::Oracles);
        let mut drop_site = None;
        let mut saw_negate = false;
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Oracles) {
                if d.label == "drop where bound" {
                    drop_site = Some(i);
                }
                if d.label == "negate predicate" {
                    saw_negate = true;
                }
            }
        }
        assert!(saw_negate, "negate site still present for the bound");
        let drop = drop_site.expect("drop where bound site");
        let m = mutate_apply_mode(p, drop, MutateMode::Oracles);
        let dumped = format!("{:?}", m.main.body.kind);
        assert!(
            !dumped.contains("Property.check"),
            "dropped mutant passes the value unchecked: {dumped}"
        );
    }

    #[test]
    fn program_mode_swaps_button_handlers() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  Ui.run(_ => View.column(View.button("+1", _ => Signal.set(c, 1)), View.button("-1", _ => Signal.set(c, 2))))
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut swap_site = None;
        let mut saw_sibling = false;
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                if d.label == "swap handler of `+1` with sibling" {
                    swap_site = Some(i);
                }
                if d.label == "swap handler of `-1` with sibling" {
                    saw_sibling = true;
                }
            }
        }
        assert!(saw_sibling, "second handler gets a swap site too");
        let site = swap_site.expect("swap site for first handler");
        let m = mutate_apply_mode(p, site, MutateMode::Program);
        let dumped = format!("{:?}", m.main.body.kind);
        assert_eq!(
            dumped.matches("IntLit(2)").count(),
            2,
            "+1 handler now runs the -1 body: {dumped}"
        );
        assert_eq!(dumped.matches("IntLit(1)").count(), 0, "{dumped}");
    }

    #[test]
    fn program_mode_single_handler_has_no_swap_site() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  Ui.run(_ => View.button("+1", _ => Signal.set(c, 1)))
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                assert!(
                    !d.label.starts_with("swap handler"),
                    "lone handler must not swap: {d:?}"
                );
            }
        }
    }

    #[test]
    fn program_mode_signal_map_identity() {
        let p = parse(
            r#"
def f(s: Int): Int = s

@main def main: IO[Unit] =
  for {
    c = Signal.int(0)
    label = Signal.map(c, n => f(n))
    _ <- IO.println("x")
  } yield ()
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut identity_site = None;
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                if d.label == "Signal.map identity" {
                    identity_site = Some(i);
                }
            }
        }
        let site = identity_site.expect("Signal.map identity site");
        let m = mutate_apply_mode(p, site, MutateMode::Program);
        let dumped = format!("{:?}", m.main.body.kind);
        assert!(
            !dumped.contains("\"f\""),
            "transform replaced by identity: {dumped}"
        );
    }

    #[test]
    fn program_mode_signal_map_placeholder_has_no_identity_site() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  for {
    c = Signal.int(0)
    label = Signal.map(c, _ => 1)
    _ <- IO.println("x")
  } yield ()
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                assert_ne!(d.label, "Signal.map identity", "hole lambda: {d:?}");
            }
        }
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

    #[test]
    fn describe_flip_add_label() {
        let p = parse(
            r#"
def sum(a: Int, b: Int): Int = a + b
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let d = mutate_describe(&p, 0, MutateMode::Program).expect("sum site");
        assert_eq!(d.def, "sum");
        assert_eq!(d.label, "flip `+` to `-`");
    }

    #[test]
    fn describe_swap_if_arms_label() {
        let p = parse(
            r#"
def pick(n: Int): Int = if (n > 0) 1 else 2
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut found = false;
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                if d.label == "swap if arms" {
                    assert_eq!(d.def, "pick");
                    found = true;
                    break;
                }
            }
        }
        assert!(found, "expected swap-if-arms label among {n} sites");
    }

    #[test]
    fn describe_zero_to_one_label() {
        let p = parse(
            r#"
def origin(): Int = 0
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let d = mutate_describe(&p, 0, MutateMode::Program).expect("0 literal");
        assert_eq!(d.def, "origin");
        assert_eq!(d.label, "0 -> 1");
    }

    #[test]
    fn describe_negate_predicate_label() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  IO.println("x").require(1 == 1)
"#,
        )
        .unwrap();
        let d = mutate_describe(&p, 0, MutateMode::Oracles).expect("require site");
        assert_eq!(d.def, "main");
        assert_eq!(d.label, "negate predicate");
    }

    #[test]
    fn describe_sibling_case_label() {
        let p = parse(
            r#"
enum Color:
  case Red
  case Blue
def paint(): Color = Color.Red
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut found = false;
        for i in 0..n {
            if let Some(d) = mutate_describe(&p, i, MutateMode::Program) {
                if d.label == "sibling case Red -> Blue" {
                    assert_eq!(d.def, "paint");
                    found = true;
                    break;
                }
            }
        }
        assert!(found, "expected sibling-case label among {n} sites");
    }

    fn bad_example_prog() -> Program {
        let mut p = parse_file(
            r#"
def bump(n: Int): Int =
  n - 1

def scale(n: Int): Int =
  n * 2

@main def main: IO[Unit] =
  IO.println(Str.fromInt(bump(0) + scale(1)))
"#,
            "Main.scuzz",
        )
        .unwrap();
        apply_verifies(
            &mut p,
            &[VerifySource {
                label: "pkg/claim.scuzz_verify".into(),
                text: "def bump(n: Int): Bool =\n  Main.bump(n) == n + 1\n".into(),
                path: std::path::PathBuf::new(),
            }],
        )
        .unwrap();
        p
    }

    #[test]
    fn scale_mutants_name_scale_and_report_no_oracle() {
        let p = bad_example_prog();
        let oracles = collect_oracle_sites(&p);
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut scale_sites = 0;
        for i in 0..n {
            let Some(d) = mutate_describe(&p, i, MutateMode::Program) else {
                continue;
            };
            if d.def != "scale" {
                continue;
            }
            scale_sites += 1;
            assert_eq!(d.def, "scale");
            assert_eq!(nearest_oracle(&d, &oracles), "no oracle observes `scale`");
        }
        assert!(scale_sites > 0, "expected at least one scale mutant");
    }

    #[test]
    fn bump_mutants_name_bump_oracle() {
        let p = bad_example_prog();
        let oracles = collect_oracle_sites(&p);
        let n = mutate_count_mode(&p, MutateMode::Program);
        let mut bump_sites = 0;
        for i in 0..n {
            let Some(d) = mutate_describe(&p, i, MutateMode::Program) else {
                continue;
            };
            if d.def != "bump" {
                continue;
            }
            bump_sites += 1;
            assert_eq!(nearest_oracle(&d, &oracles), "bump");
        }
        assert!(bump_sites > 0, "expected at least one bump mutant");
    }

    #[test]
    fn nearest_oracle_same_def_beats_other_module_span() {
        let p = parse(
            r#"
def gated(n: Int): Int =
  (n + 1).require(n > 0)
@main def main: IO[Unit] = IO.println("x")
"#,
        )
        .unwrap();
        let oracles = collect_oracle_sites(&p);
        let d = mutate_describe(&p, 0, MutateMode::Program).expect("gated + site");
        assert_eq!(d.def, "gated");
        assert_eq!(nearest_oracle(&d, &oracles), "gated");
    }
}

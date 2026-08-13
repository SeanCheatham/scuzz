//! Stem-paired `*.scuzz_sim` / `*.scuzz_drivers` overlays and in-source `law` residualization.

use crate::ast::{Expr, ExprKind, FunDef, Program, Type};
use crate::parser::{parse, ParseError};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum OverlayError {
    #[error("{0}")]
    Msg(String),
    #[error(transparent)]
    Parse(#[from] ParseError),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OverlayKind {
    Sim,
    Drivers,
}

#[derive(Debug, Clone)]
pub struct OverlaySource {
    /// File stem shared with the live `*.scuzz` twin (`Shared` for `Shared.scuzz_sim`).
    pub stem: String,
    pub kind: OverlayKind,
    pub label: String,
    pub text: String,
}

/// Apply same-name sim replacements, then merge `*.scuzz_drivers`. In-source
/// `law` defs stay on the program; call [`collect_law_names`] then
/// [`check_laws_applied`] under verify / check. `.require` is rewritten
/// type-directedly in field resolution (verify) or erased live.
pub fn apply_overlays(
    mut live: Program,
    overlays: &[OverlaySource],
) -> Result<Program, OverlayError> {
    for sim in overlays {
        if sim.kind != OverlayKind::Sim {
            continue;
        }
        let prog =
            parse(&sim.text).map_err(|e| OverlayError::Msg(format!("{}: {e}", sim.label)))?;
        if !prog.main.name.is_empty() {
            return Err(OverlayError::Msg(format!(
                "{}: *.scuzz_sim must not define @main",
                sim.label
            )));
        }
        if !prog.enums.is_empty() {
            return Err(OverlayError::Msg(format!(
                "{}: *.scuzz_sim must not define enums",
                sim.label
            )));
        }
        for d in prog.defs {
            let mut d = d;
            d.module = sim.stem.clone();
            replace_sim_def(&mut live, &d, &sim.label)?;
        }
    }
    let mut driver_names = Vec::new();
    for ov in overlays {
        if ov.kind != OverlayKind::Drivers {
            continue;
        }
        apply_driver_overlay(&mut live, ov, &mut driver_names)?;
    }
    live.driver_names = driver_names;
    Ok(live)
}

/// Names of in-source `law` declarations, in source order. Rejects a law that
/// collides with a non-law def (already a parse duplicate) and checks return type.
pub fn collect_law_names(program: &Program) -> Result<Vec<String>, OverlayError> {
    let mut names = Vec::new();
    for d in &program.defs {
        if !d.is_law {
            continue;
        }
        if !matches!(d.ret, Type::Bool | Type::Int) {
            return Err(OverlayError::Msg(format!(
                "law `{}` must return Bool (or Int), got {:?}",
                d.name, d.ret
            )));
        }
        if !d.params.is_empty() {
            return Err(OverlayError::Msg(format!(
                "law `{}` must be nullary for residual checks",
                d.name
            )));
        }
        names.push(d.name.clone());
    }
    Ok(names)
}

/// Drop `law` defs so live `build` / `run` never emit them.
pub fn erase_laws(program: &mut Program) {
    program.defs.retain(|d| !d.is_law);
    program.law_names.clear();
}

pub fn is_require_method(method: &str) -> bool {
    method == "require"
}

/// Drop `.require` to the receiver so live `build` / `run` never evaluate the predicate.
pub fn erase_requires(program: &mut Program) {
    for d in &mut program.defs {
        d.body = erase_require_expr(std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)));
    }
    program.main.body = erase_require_expr(std::mem::replace(
        &mut program.main.body,
        Expr::dummy(ExprKind::Unit),
    ));
}

fn erase_require_expr(expr: Expr) -> Expr {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::MethodCall {
            receiver,
            method,
            args: _,
        } if is_require_method(&method) => erase_require_expr(*receiver),
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => Expr::new(
            ExprKind::MethodCall {
                receiver: Box::new(erase_require_expr(*receiver)),
                method,
                args: args.into_iter().map(erase_require_expr).collect(),
            },
            span,
        ),
        ExprKind::Call { callee, args } => Expr::new(
            ExprKind::Call {
                callee,
                args: args.into_iter().map(erase_require_expr).collect(),
            },
            span,
        ),
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args.into_iter().map(erase_require_expr).collect(),
                type_args,
            },
            span,
        ),
        ExprKind::IoPrintln(x) => {
            Expr::new(ExprKind::IoPrintln(Box::new(erase_require_expr(*x))), span)
        }
        ExprKind::IoSleep(x) => {
            Expr::new(ExprKind::IoSleep(Box::new(erase_require_expr(*x))), span)
        }
        ExprKind::IoFail(x) => Expr::new(ExprKind::IoFail(Box::new(erase_require_expr(*x))), span),
        ExprKind::IoPure(x) => Expr::new(ExprKind::IoPure(Box::new(erase_require_expr(*x))), span),
        ExprKind::Attempt { inner } => Expr::new(
            ExprKind::Attempt {
                inner: Box::new(erase_require_expr(*inner)),
            },
            span,
        ),
        ExprKind::Lambda { param, body } => Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(erase_require_expr(*body)),
            },
            span,
        ),
        ExprKind::Field { base, field } => Expr::new(
            ExprKind::Field {
                base: Box::new(erase_require_expr(*base)),
                field,
            },
            span,
        ),
        ExprKind::FlatMap { inner, param, body } => Expr::new(
            ExprKind::FlatMap {
                inner: Box::new(erase_require_expr(*inner)),
                param,
                body: Box::new(erase_require_expr(*body)),
            },
            span,
        ),
        ExprKind::HandleErrorWith { inner, body } => Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(erase_require_expr(*inner)),
                body: Box::new(erase_require_expr(*body)),
            },
            span,
        ),
        ExprKind::Let { name, value, body } => Expr::new(
            ExprKind::Let {
                name,
                value: Box::new(erase_require_expr(*value)),
                body: Box::new(erase_require_expr(*body)),
            },
            span,
        ),
        ExprKind::IoEnsure { inner, finalizer } => Expr::new(
            ExprKind::IoEnsure {
                inner: Box::new(erase_require_expr(*inner)),
                finalizer: Box::new(erase_require_expr(*finalizer)),
            },
            span,
        ),
        ExprKind::IoRace { left, right } => Expr::new(
            ExprKind::IoRace {
                left: Box::new(erase_require_expr(*left)),
                right: Box::new(erase_require_expr(*right)),
            },
            span,
        ),
        ExprKind::IoBoth { left, right } => Expr::new(
            ExprKind::IoBoth {
                left: Box::new(erase_require_expr(*left)),
                right: Box::new(erase_require_expr(*right)),
            },
            span,
        ),
        ExprKind::IoTimeout { ms, inner } => Expr::new(
            ExprKind::IoTimeout {
                ms: Box::new(erase_require_expr(*ms)),
                inner: Box::new(erase_require_expr(*inner)),
            },
            span,
        ),
        ExprKind::Binary { op, left, right } => Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(erase_require_expr(*left)),
                right: Box::new(erase_require_expr(*right)),
            },
            span,
        ),
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => Expr::new(
            ExprKind::If {
                cond: Box::new(erase_require_expr(*cond)),
                then_branch: Box::new(erase_require_expr(*then_branch)),
                else_branch: Box::new(erase_require_expr(*else_branch)),
            },
            span,
        ),
        ExprKind::For { binders, body } => Expr::new(
            ExprKind::For {
                binders: binders
                    .into_iter()
                    .map(|b| match b {
                        crate::ast::ForBinder::Eq { name, value } => crate::ast::ForBinder::Eq {
                            name,
                            value: erase_require_expr(value),
                        },
                        crate::ast::ForBinder::Draw { name, value } => {
                            crate::ast::ForBinder::Draw {
                                name,
                                value: erase_require_expr(value),
                            }
                        }
                    })
                    .collect(),
                body: Box::new(erase_require_expr(*body)),
            },
            span,
        ),
        ExprKind::Match { scrutinee, arms } => Expr::new(
            ExprKind::Match {
                scrutinee: Box::new(erase_require_expr(*scrutinee)),
                arms: arms
                    .into_iter()
                    .map(|a| crate::ast::MatchArm {
                        pattern: a.pattern,
                        body: erase_require_expr(a.body),
                    })
                    .collect(),
            },
            span,
        ),
        ExprKind::ListLit { elems } => Expr::new(
            ExprKind::ListLit {
                elems: elems.into_iter().map(erase_require_expr).collect(),
            },
            span,
        ),
        ExprKind::Interpolate { parts } => Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        crate::ast::InterpPart::Expr(e) => {
                            crate::ast::InterpPart::Expr(erase_require_expr(e))
                        }
                        lit => lit,
                    })
                    .collect(),
            },
            span,
        ),
        other => Expr::new(other, span),
    }
}

/// Every `law` must appear in a `.require` predicate in live (non-law) code.
pub fn check_laws_applied(program: &Program, law_names: &[String]) -> Result<(), OverlayError> {
    if law_names.is_empty() {
        return Ok(());
    }
    let mut used = std::collections::HashSet::new();
    for d in &program.defs {
        if d.is_law {
            continue;
        }
        collect_required_laws(&d.body, law_names, &mut used);
    }
    collect_required_laws(&program.main.body, law_names, &mut used);
    for name in law_names {
        if !used.contains(name) {
            return Err(OverlayError::Msg(format!(
                "law `{name}` is never applied; use `.require({name})` on the value it constrains"
            )));
        }
    }
    Ok(())
}

fn callee_base_name(callee: &str) -> &str {
    callee.rsplit('.').next().unwrap_or(callee)
}

fn collect_required_laws(
    e: &Expr,
    law_names: &[String],
    used: &mut std::collections::HashSet<String>,
) {
    match &e.kind {
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } if is_require_method(method) => {
            collect_required_laws(receiver, law_names, used);
            for a in args {
                mark_law_refs(a, law_names, used);
                collect_required_laws(a, law_names, used);
            }
        }
        ExprKind::MethodCall { receiver, args, .. } => {
            collect_required_laws(receiver, law_names, used);
            for a in args {
                collect_required_laws(a, law_names, used);
            }
        }
        ExprKind::Call { args, .. } | ExprKind::AdtConstruct { args, .. } => {
            for a in args {
                collect_required_laws(a, law_names, used);
            }
        }
        ExprKind::IoPrintln(x)
        | ExprKind::IoSleep(x)
        | ExprKind::IoFail(x)
        | ExprKind::IoPure(x)
        | ExprKind::Attempt { inner: x }
        | ExprKind::Field { base: x, .. }
        | ExprKind::Lambda { body: x, .. } => collect_required_laws(x, law_names, used),
        ExprKind::FlatMap { inner, body, .. }
        | ExprKind::HandleErrorWith { inner, body }
        | ExprKind::Let {
            value: inner, body, ..
        }
        | ExprKind::IoEnsure {
            inner,
            finalizer: body,
        }
        | ExprKind::IoRace {
            left: inner,
            right: body,
        }
        | ExprKind::IoBoth {
            left: inner,
            right: body,
        }
        | ExprKind::Binary {
            left: inner,
            right: body,
            ..
        }
        | ExprKind::IoTimeout {
            ms: inner,
            inner: body,
        } => {
            collect_required_laws(inner, law_names, used);
            collect_required_laws(body, law_names, used);
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            collect_required_laws(cond, law_names, used);
            collect_required_laws(then_branch, law_names, used);
            collect_required_laws(else_branch, law_names, used);
        }
        ExprKind::For { binders, body } => {
            for b in binders {
                match b {
                    crate::ast::ForBinder::Eq { value, .. }
                    | crate::ast::ForBinder::Draw { value, .. } => {
                        collect_required_laws(value, law_names, used)
                    }
                }
            }
            collect_required_laws(body, law_names, used);
        }
        ExprKind::Match { scrutinee, arms } => {
            collect_required_laws(scrutinee, law_names, used);
            for a in arms {
                collect_required_laws(&a.body, law_names, used);
            }
        }
        ExprKind::ListLit { elems } => {
            for a in elems {
                collect_required_laws(a, law_names, used);
            }
        }
        ExprKind::Interpolate { parts } => {
            for p in parts {
                if let crate::ast::InterpPart::Expr(e) = p {
                    collect_required_laws(e, law_names, used);
                }
            }
        }
        ExprKind::Var(_) | ExprKind::Unit | ExprKind::IntLit(_) | ExprKind::StrLit(_) => {}
    }
}

fn mark_law_refs(e: &Expr, law_names: &[String], used: &mut std::collections::HashSet<String>) {
    match &e.kind {
        ExprKind::Var(name) => {
            let base = callee_base_name(name);
            if law_names.iter().any(|n| n == base || n == name) {
                used.insert(base.to_string());
            }
        }
        ExprKind::Call { callee, args } => {
            let base = callee_base_name(callee);
            if law_names.iter().any(|n| n == base || n == callee.as_str()) {
                used.insert(base.to_string());
            }
            for a in args {
                mark_law_refs(a, law_names, used);
            }
        }
        ExprKind::Lambda { body, .. } => mark_law_refs(body, law_names, used),
        ExprKind::Binary { left, right, .. } => {
            mark_law_refs(left, law_names, used);
            mark_law_refs(right, law_names, used);
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            mark_law_refs(cond, law_names, used);
            mark_law_refs(then_branch, law_names, used);
            mark_law_refs(else_branch, law_names, used);
        }
        ExprKind::MethodCall { receiver, args, .. } => {
            mark_law_refs(receiver, law_names, used);
            for a in args {
                mark_law_refs(a, law_names, used);
            }
        }
        ExprKind::AdtConstruct { args, .. } => {
            for a in args {
                mark_law_refs(a, law_names, used);
            }
        }
        _ => {}
    }
}

fn replace_sim_def(live: &mut Program, sim: &FunDef, label: &str) -> Result<(), OverlayError> {
    let Some(idx) = live
        .defs
        .iter()
        .position(|d| d.module == sim.module && d.name == sim.name)
    else {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` has no live twin",
            sim.name
        )));
    };
    let live_def = &live.defs[idx];
    if live_def.is_law {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` cannot replace a law",
            sim.name
        )));
    }
    if live_def.params.len() != sim.params.len() {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` arity mismatch (live {}, sim {})",
            sim.name,
            live_def.params.len(),
            sim.params.len()
        )));
    }
    for (lp, sp) in live_def.params.iter().zip(sim.params.iter()) {
        if lp.ty != sp.ty {
            return Err(OverlayError::Msg(format!(
                "{label}: sim def `{}` param `{}` type mismatch (live {:?}, sim {:?})",
                sim.name, sp.name, lp.ty, sp.ty
            )));
        }
    }
    if live_def.ret != sim.ret {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` return type mismatch (live {:?}, sim {:?})",
            sim.name, live_def.ret, sim.ret
        )));
    }
    let live_io = matches!(live_def.ret, Type::Io(_));
    let sim_io = matches!(sim.ret, Type::Io(_));
    if live_io != sim_io {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` purity mismatch",
            sim.name
        )));
    }
    live.defs[idx] = sim.clone();
    Ok(())
}

fn apply_driver_overlay(
    live: &mut Program,
    ov: &OverlaySource,
    names: &mut Vec<String>,
) -> Result<(), OverlayError> {
    let prog = parse(&ov.text).map_err(|e| OverlayError::Msg(format!("{}: {e}", ov.label)))?;
    if !prog.main.name.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{}: *.scuzz_drivers must not define @main",
            ov.label
        )));
    }
    if !prog.enums.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{}: *.scuzz_drivers must not define enums",
            ov.label
        )));
    }
    for mut d in prog.defs {
        d.module = ov.stem.clone();
        check_driver_def(live, &d, &ov.label)?;
        d.is_driver = true;
        names.push(d.name.clone());
        live.defs.push(d);
    }
    Ok(())
}

fn check_driver_def(live: &Program, d: &FunDef, label: &str) -> Result<(), OverlayError> {
    if d.is_law {
        return Err(OverlayError::Msg(format!(
            "{label}: *.scuzz_drivers must not declare a law"
        )));
    }
    if live
        .defs
        .iter()
        .any(|l| l.module == d.module && l.name == d.name)
    {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` collides with a live def",
            d.name
        )));
    }
    if !matches!(&d.ret, Type::Io(inner) if matches!(**inner, Type::Unit)) {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` must return IO[Unit]",
            d.name
        )));
    }
    if d.params.len() > 1 {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` takes at most one Int or String param",
            d.name
        )));
    }
    for p in &d.params {
        if !matches!(p.ty, Type::Int | Type::String) {
            return Err(OverlayError::Msg(format!(
                "{label}: driver `{}` param `{}` must be Int or String",
                d.name, p.name
            )));
        }
    }
    if expr_has_law(&d.body) {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` must not call Law.*",
            d.name
        )));
    }
    Ok(())
}

pub fn expr_has_law(e: &Expr) -> bool {
    match &e.kind {
        ExprKind::Call { callee, args } => {
            callee.starts_with("Law.") || args.iter().any(expr_has_law)
        }
        ExprKind::IoPrintln(x)
        | ExprKind::IoSleep(x)
        | ExprKind::IoFail(x)
        | ExprKind::IoPure(x)
        | ExprKind::Attempt { inner: x }
        | ExprKind::Field { base: x, .. }
        | ExprKind::Lambda { body: x, .. } => expr_has_law(x),
        ExprKind::FlatMap { inner, body, .. }
        | ExprKind::HandleErrorWith { inner, body }
        | ExprKind::Let {
            value: inner, body, ..
        }
        | ExprKind::IoEnsure {
            inner,
            finalizer: body,
        }
        | ExprKind::IoTimeout {
            ms: inner,
            inner: body,
        }
        | ExprKind::IoRace {
            left: inner,
            right: body,
        }
        | ExprKind::IoBoth {
            left: inner,
            right: body,
        }
        | ExprKind::Binary {
            left: inner,
            right: body,
            ..
        } => expr_has_law(inner) || expr_has_law(body),
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => expr_has_law(cond) || expr_has_law(then_branch) || expr_has_law(else_branch),
        ExprKind::For { binders, body } => {
            binders.iter().any(|b| match b {
                crate::ast::ForBinder::Eq { value, .. }
                | crate::ast::ForBinder::Draw { value, .. } => expr_has_law(value),
            }) || expr_has_law(body)
        }
        ExprKind::Match { scrutinee, arms } => {
            expr_has_law(scrutinee) || arms.iter().any(|a| expr_has_law(&a.body))
        }
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => is_require_method(method) || expr_has_law(receiver) || args.iter().any(expr_has_law),
        ExprKind::AdtConstruct { args, .. } | ExprKind::ListLit { elems: args } => {
            args.iter().any(expr_has_law)
        }
        ExprKind::Interpolate { parts } => parts.iter().any(|p| match p {
            crate::ast::InterpPart::Expr(e) => expr_has_law(e),
            crate::ast::InterpPart::Lit(_) => false,
        }),
        ExprKind::Var(_) | ExprKind::Unit | ExprKind::IntLit(_) | ExprKind::StrLit(_) => false,
    }
}

/// Table lines for `build/drivers.txt`: `name`, `name i`, or `name s`.
pub fn driver_table_text(program: &Program) -> String {
    let mut out = String::new();
    for d in &program.defs {
        if !d.is_driver {
            continue;
        }
        out.push_str(&d.name);
        if let Some(p) = d.params.first() {
            match p.ty {
                Type::String => out.push_str(" s"),
                _ => out.push_str(" i"),
            }
        }
        out.push('\n');
    }
    out
}

/// Rewrite calls and record construction so `where` predicates become `Law.check`
/// at the use site. Live builds skip this.
pub fn residualize_refinements(program: &mut Program) {
    let defs = program.defs.clone();
    let enums = program.enums.clone();
    for d in &mut program.defs {
        d.body = residualize_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &defs,
            &enums,
        );
    }
    program.main.body = residualize_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &defs,
        &enums,
    );
}

fn callee_base(callee: &str) -> &str {
    callee.rsplit('.').next().unwrap_or(callee)
}

fn find_def<'a>(defs: &'a [FunDef], callee: &str) -> Option<&'a FunDef> {
    defs.iter().find(|d| {
        d.name == callee || (!d.module.is_empty() && format!("{}.{}", d.module, d.name) == callee)
    })
}

fn find_record<'a>(
    enums: &'a [crate::ast::EnumDef],
    callee: &str,
) -> Option<&'a crate::ast::EnumDef> {
    enums.iter().find(|e| {
        e.is_record
            && (e.name == callee
                || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == callee)
                || callee_base(callee) == e.name)
    })
}

fn law_check(name: String, pred: Expr, value: Expr) -> Expr {
    Expr::dummy(ExprKind::Call {
        callee: "Law.check".into(),
        args: vec![Expr::dummy(ExprKind::StrLit(name)), pred, value],
    })
}

fn wrap_refined(
    label: &str,
    names: &[(String, Option<Expr>)],
    args: Vec<Expr>,
    make_inner: impl FnOnce(Vec<Expr>) -> Expr,
) -> Expr {
    if names.iter().all(|(_, r)| r.is_none()) || names.len() != args.len() {
        return make_inner(args);
    }
    let checked: Vec<Expr> = names
        .iter()
        .map(|(n, rfn)| {
            let var = Expr::dummy(ExprKind::Var(n.clone()));
            match rfn {
                Some(pred) => law_check(format!("{label}.{n}"), pred.clone(), var),
                None => var,
            }
        })
        .collect();
    let mut body = make_inner(checked);
    let span = body.span.clone();
    for ((n, _), arg) in names.iter().zip(args).rev() {
        body = Expr::new(
            ExprKind::Let {
                name: n.clone(),
                value: Box::new(arg),
                body: Box::new(body),
            },
            span.clone(),
        );
    }
    body
}

fn residualize_call(
    callee: String,
    args: Vec<Expr>,
    span: crate::span::Span,
    defs: &[FunDef],
    enums: &[crate::ast::EnumDef],
) -> Expr {
    if let Some(d) = find_def(defs, &callee) {
        if d.params.iter().any(|p| p.rfn.is_some()) {
            let names: Vec<_> = d
                .params
                .iter()
                .map(|p| (p.name.clone(), p.rfn.clone()))
                .collect();
            let c = callee.clone();
            let sp = span.clone();
            return wrap_refined(&d.name, &names, args, move |a| {
                Expr::new(ExprKind::Call { callee: c, args: a }, sp)
            });
        }
    }
    if let Some(en) = find_record(enums, &callee) {
        let c = &en.cases[0];
        if c.has_rfns() {
            let names: Vec<_> = c
                .fields
                .iter()
                .enumerate()
                .map(|(i, (n, _))| (n.clone(), c.field_rfn(i).cloned()))
                .collect();
            let call = callee.clone();
            let sp = span.clone();
            return wrap_refined(&en.name, &names, args, move |a| {
                Expr::new(
                    ExprKind::Call {
                        callee: call,
                        args: a,
                    },
                    sp,
                )
            });
        }
    }
    Expr::new(ExprKind::Call { callee, args }, span)
}

fn residualize_adt(
    enum_name: String,
    case_name: String,
    args: Vec<Expr>,
    type_args: Vec<Type>,
    span: crate::span::Span,
    enums: &[crate::ast::EnumDef],
) -> Expr {
    let Some(en) = enums.iter().find(|e| {
        e.name == enum_name
            || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == enum_name)
    }) else {
        return Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args,
                type_args,
            },
            span,
        );
    };
    let Some(c) = en.cases.iter().find(|c| c.name == case_name) else {
        return Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args,
                type_args,
            },
            span,
        );
    };
    if !c.has_rfns() {
        return Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args,
                type_args,
            },
            span,
        );
    }
    let names: Vec<_> = c
        .fields
        .iter()
        .enumerate()
        .map(|(i, (n, _))| (n.clone(), c.field_rfn(i).cloned()))
        .collect();
    let ename = enum_name.clone();
    let cname = case_name.clone();
    let targs = type_args.clone();
    let sp = span.clone();
    wrap_refined(&en.name, &names, args, move |a| {
        Expr::new(
            ExprKind::AdtConstruct {
                enum_name: ename,
                case_name: cname,
                args: a,
                type_args: targs,
            },
            sp,
        )
    })
}

fn residualize_expr(expr: Expr, defs: &[FunDef], enums: &[crate::ast::EnumDef]) -> Expr {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Call { callee, args } => {
            let args = args
                .into_iter()
                .map(|a| residualize_expr(a, defs, enums))
                .collect();
            residualize_call(callee, args, span, defs, enums)
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => {
            let args = args
                .into_iter()
                .map(|a| residualize_expr(a, defs, enums))
                .collect();
            residualize_adt(enum_name, case_name, args, type_args, span, enums)
        }
        ExprKind::IoPrintln(x) => Expr::new(
            ExprKind::IoPrintln(Box::new(residualize_expr(*x, defs, enums))),
            span,
        ),
        ExprKind::IoSleep(x) => Expr::new(
            ExprKind::IoSleep(Box::new(residualize_expr(*x, defs, enums))),
            span,
        ),
        ExprKind::IoFail(x) => Expr::new(
            ExprKind::IoFail(Box::new(residualize_expr(*x, defs, enums))),
            span,
        ),
        ExprKind::IoPure(x) => Expr::new(
            ExprKind::IoPure(Box::new(residualize_expr(*x, defs, enums))),
            span,
        ),
        ExprKind::Attempt { inner } => Expr::new(
            ExprKind::Attempt {
                inner: Box::new(residualize_expr(*inner, defs, enums)),
            },
            span,
        ),
        ExprKind::Field { base, field } => Expr::new(
            ExprKind::Field {
                base: Box::new(residualize_expr(*base, defs, enums)),
                field,
            },
            span,
        ),
        ExprKind::Lambda { param, body } => Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(residualize_expr(*body, defs, enums)),
            },
            span,
        ),
        ExprKind::FlatMap { inner, param, body } => Expr::new(
            ExprKind::FlatMap {
                inner: Box::new(residualize_expr(*inner, defs, enums)),
                param,
                body: Box::new(residualize_expr(*body, defs, enums)),
            },
            span,
        ),
        ExprKind::HandleErrorWith { inner, body } => Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(residualize_expr(*inner, defs, enums)),
                body: Box::new(residualize_expr(*body, defs, enums)),
            },
            span,
        ),
        ExprKind::Let { name, value, body } => Expr::new(
            ExprKind::Let {
                name,
                value: Box::new(residualize_expr(*value, defs, enums)),
                body: Box::new(residualize_expr(*body, defs, enums)),
            },
            span,
        ),
        ExprKind::IoEnsure { inner, finalizer } => Expr::new(
            ExprKind::IoEnsure {
                inner: Box::new(residualize_expr(*inner, defs, enums)),
                finalizer: Box::new(residualize_expr(*finalizer, defs, enums)),
            },
            span,
        ),
        ExprKind::IoTimeout { ms, inner } => Expr::new(
            ExprKind::IoTimeout {
                ms: Box::new(residualize_expr(*ms, defs, enums)),
                inner: Box::new(residualize_expr(*inner, defs, enums)),
            },
            span,
        ),
        ExprKind::IoRace { left, right } => Expr::new(
            ExprKind::IoRace {
                left: Box::new(residualize_expr(*left, defs, enums)),
                right: Box::new(residualize_expr(*right, defs, enums)),
            },
            span,
        ),
        ExprKind::IoBoth { left, right } => Expr::new(
            ExprKind::IoBoth {
                left: Box::new(residualize_expr(*left, defs, enums)),
                right: Box::new(residualize_expr(*right, defs, enums)),
            },
            span,
        ),
        ExprKind::Binary { op, left, right } => Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(residualize_expr(*left, defs, enums)),
                right: Box::new(residualize_expr(*right, defs, enums)),
            },
            span,
        ),
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => Expr::new(
            ExprKind::If {
                cond: Box::new(residualize_expr(*cond, defs, enums)),
                then_branch: Box::new(residualize_expr(*then_branch, defs, enums)),
                else_branch: Box::new(residualize_expr(*else_branch, defs, enums)),
            },
            span,
        ),
        ExprKind::For { binders, body } => Expr::new(
            ExprKind::For {
                binders: binders
                    .into_iter()
                    .map(|b| match b {
                        crate::ast::ForBinder::Eq { name, value } => crate::ast::ForBinder::Eq {
                            name,
                            value: residualize_expr(value, defs, enums),
                        },
                        crate::ast::ForBinder::Draw { name, value } => {
                            crate::ast::ForBinder::Draw {
                                name,
                                value: residualize_expr(value, defs, enums),
                            }
                        }
                    })
                    .collect(),
                body: Box::new(residualize_expr(*body, defs, enums)),
            },
            span,
        ),
        ExprKind::Match { scrutinee, arms } => Expr::new(
            ExprKind::Match {
                scrutinee: Box::new(residualize_expr(*scrutinee, defs, enums)),
                arms: arms
                    .into_iter()
                    .map(|a| crate::ast::MatchArm {
                        pattern: a.pattern,
                        body: residualize_expr(a.body, defs, enums),
                    })
                    .collect(),
            },
            span,
        ),
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => Expr::new(
            ExprKind::MethodCall {
                receiver: Box::new(residualize_expr(*receiver, defs, enums)),
                method,
                args: args
                    .into_iter()
                    .map(|a| residualize_expr(a, defs, enums))
                    .collect(),
            },
            span,
        ),
        ExprKind::ListLit { elems } => Expr::new(
            ExprKind::ListLit {
                elems: elems
                    .into_iter()
                    .map(|a| residualize_expr(a, defs, enums))
                    .collect(),
            },
            span,
        ),
        ExprKind::Interpolate { parts } => Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        crate::ast::InterpPart::Lit(s) => crate::ast::InterpPart::Lit(s),
                        crate::ast::InterpPart::Expr(e) => {
                            crate::ast::InterpPart::Expr(residualize_expr(e, defs, enums))
                        }
                    })
                    .collect(),
            },
            span,
        ),
        other => Expr::new(other, span),
    }
}

/// True when `path` is a stem-paired `*.scuzz_sim` or `*.scuzz_drivers` overlay.
pub fn overlay_kind_from_path(path: &std::path::Path) -> Option<(String, OverlayKind)> {
    let name = path.file_name()?.to_str()?;
    if let Some(stem) = name.strip_suffix(".scuzz_sim") {
        if !stem.is_empty() {
            return Some((stem.to_string(), OverlayKind::Sim));
        }
    }
    if let Some(stem) = name.strip_suffix(".scuzz_drivers") {
        if !stem.is_empty() {
            return Some((stem.to_string(), OverlayKind::Drivers));
        }
    }
    None
}

pub fn is_fmt_source(path: &std::path::Path) -> bool {
    matches!(
        path.extension().and_then(|e| e.to_str()),
        Some("scuzz" | "scuzz_sim" | "scuzz_drivers")
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_sources;

    #[test]
    fn sim_replaces_and_in_source_laws_attach() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def title(): String = \"Live\"\nlaw always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(title())\n"
                .into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Sim,
            label: "Main.scuzz_sim".into(),
            text: "def title(): String = \"Sim\"\n".into(),
        }];
        let prog = apply_overlays(live, &overlays).unwrap();
        let laws = collect_law_names(&prog).unwrap();
        assert_eq!(laws, vec!["always".to_string()]);
        let title = prog.defs.iter().find(|d| d.name == "title").unwrap();
        match &title.body.kind {
            crate::ast::ExprKind::StrLit(s) => assert_eq!(s, "Sim"),
            other => panic!("expected sim body, got {other:?}"),
        }
        let law = prog.defs.iter().find(|d| d.name == "always").unwrap();
        assert!(law.is_law);
    }

    #[test]
    fn erase_drops_laws_from_live() {
        let mut live = parse_sources(&[(
            "Main.scuzz".into(),
            "law always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        assert_eq!(live.defs.len(), 1);
        erase_laws(&mut live);
        assert!(live.defs.is_empty());
    }

    #[test]
    fn sim_cannot_replace_a_law() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "law title: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Sim,
            label: "Main.scuzz_sim".into(),
            text: "def title(): Bool = 1 == 1\n".into(),
        }];
        assert!(apply_overlays(live, &overlays).is_err());
    }

    #[test]
    fn sim_without_twin_errors() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Sim,
            label: "Main.scuzz_sim".into(),
            text: "def missing(): String = \"x\"\n".into(),
        }];
        assert!(apply_overlays(live, &overlays).is_err());
    }

    #[test]
    fn drivers_merge_and_reject_law_calls() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def note(n: Int): Unit = ()\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            label: "Main.scuzz_drivers".into(),
            text: "def plusN(n: Int): IO[Unit] =\n  IO.pure(note(n))\n".into(),
        }];
        let prog = apply_overlays(live, &overlays).unwrap();
        assert_eq!(prog.driver_names, vec!["plusN".to_string()]);
        assert!(prog.defs.iter().any(|d| d.is_driver && d.name == "plusN"));
        assert_eq!(driver_table_text(&prog), "plusN i\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            label: "Main.scuzz_drivers".into(),
            text: "def bad(): IO[Unit] =\n  Law.assert(\"x\", 1)\n".into(),
        }];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(err.to_string().contains("must not call Law"));
    }

    #[test]
    fn residualize_param_where_at_call() {
        let mut prog = parse_sources(&[(
            "Main.scuzz".into(),
            "def note(n: Int where n >= 0): Unit = ()\n@main def main: IO[Unit] = IO.pure(note(1))\n"
                .into(),
        )])
        .unwrap();
        residualize_refinements(&mut prog);
        let src = format!("{:?}", prog.main.body.kind);
        assert!(src.contains("Law.check") || matches!(prog.main.body.kind, ExprKind::Let { .. }));
        match &prog.main.body.kind {
            ExprKind::IoPure(inner) => match &inner.kind {
                ExprKind::Let { name, body, .. } => {
                    assert_eq!(name, "n");
                    match &body.kind {
                        ExprKind::Call { callee, args } => {
                            assert_eq!(callee, "note");
                            match &args[0].kind {
                                ExprKind::Call { callee, .. } => assert_eq!(callee, "Law.check"),
                                other => panic!("expected Law.check arg, got {other:?}"),
                            }
                        }
                        other => panic!("expected call, got {other:?}"),
                    }
                }
                other => panic!("expected let, got {other:?}"),
            },
            other => panic!("expected IO.pure, got {other:?}"),
        }
    }

    #[test]
    fn unused_law_errors() {
        let prog = parse_sources(&[(
            "Main.scuzz".into(),
            "law always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let laws = collect_law_names(&prog).unwrap();
        let err = check_laws_applied(&prog, &laws).unwrap_err();
        assert!(err.to_string().contains("never applied"));
    }

    #[test]
    fn applied_law_ok() {
        let prog = parse_sources(&[(
            "Main.scuzz".into(),
            "law always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(\"x\").require(always)\n"
                .into(),
        )])
        .unwrap();
        let laws = collect_law_names(&prog).unwrap();
        check_laws_applied(&prog, &laws).unwrap();
    }

    #[test]
    fn erase_requires_drops_to_receiver() {
        let mut prog = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\").require(1 == 1)\n".into(),
        )])
        .unwrap();
        erase_requires(&mut prog);
        let dumped = format!("{:?}", prog.main.body.kind);
        assert!(
            !dumped.contains("require"),
            "expected require erased: {dumped}"
        );
    }
}

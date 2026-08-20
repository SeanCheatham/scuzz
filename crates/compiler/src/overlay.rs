//! Stem-paired `*.scuzz_sim` / `*.scuzz_drivers` overlays and in-source `law` residualization.

use crate::ast::{EnumDef, Expr, ExprKind, FunDef, Import, Program, Type};
use crate::parser::{parse_file, ParseError};
use crate::resolve::split_dotted;
use crate::span::Span;
use std::path::PathBuf;
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
    /// Disk path when loaded from a package. Empty in in-memory tests.
    pub path: PathBuf,
}

/// Apply same-name sim replacements, then merge `*.scuzz_drivers`. In-source
/// `law` defs stay on the program. Call [`collect_law_names`] then
/// [`check_laws_applied`] under verify / check. `.require` is rewritten
/// by type in field resolution (verify) or erased live.
pub fn apply_overlays(
    mut live: Program,
    overlays: &[OverlaySource],
) -> Result<Program, OverlayError> {
    for sim in overlays {
        if sim.kind != OverlayKind::Sim {
            continue;
        }
        let prog = parse_file(&sim.text, &sim.label)?;
        reject_overlay_extras(&prog, &sim.label, OverlayKind::Sim)?;
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

/// Names of in-source `law` declarations, in source order. Reject a law that
/// collides with a non-law def (already a parse duplicate). Check the return type.
pub fn collect_law_names(program: &Program) -> Result<Vec<String>, OverlayError> {
    let mut names = Vec::new();
    for d in &program.defs {
        if !d.is_law {
            continue;
        }
        if !matches!(d.ret, Type::Bool) {
            return Err(OverlayError::Msg(format!(
                "law `{}` must return Bool, got {:?}",
                d.name, d.ret
            )));
        }
        if d.params.len() > 3 {
            return Err(OverlayError::Msg(format!(
                "law `{}` takes at most three Int, String, or Bool params",
                d.name
            )));
        }
        for p in &d.params {
            if !matches!(p.ty, Type::Int | Type::String | Type::Bool) {
                return Err(OverlayError::Msg(format!(
                    "law `{}` param `{}` must be Int, String, or Bool",
                    d.name, p.name
                )));
            }
        }
        names.push(d.name.clone());
    }
    Ok(names)
}

/// Drop `law` defs. Live `build` / `run` must not emit them.
pub fn erase_laws(program: &mut Program) {
    program.defs.retain(|d| !d.is_law);
    program.law_names.clear();
}

/// Drop `.require` to the receiver. Live `build` / `run` must not evaluate the predicate.
pub fn erase_requires(program: &mut Program) {
    program.map_bodies_mut(erase_require_expr);
}

fn erase_require_expr(expr: Expr) -> Expr {
    match expr.kind {
        ExprKind::MethodCall {
            receiver,
            method,
            args: _,
        } if method == "require" => erase_require_expr(*receiver),
        kind => Expr {
            kind,
            span: expr.span,
        }
        .map_children(erase_require_expr),
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
    for d in &program.defs {
        if !d.is_law || !d.params.is_empty() {
            continue;
        }
        if !used.contains(&d.name) {
            return Err(OverlayError::Msg(format!(
                "law `{}` is never applied; use `.require({})` on the value it constrains",
                d.name, d.name
            )));
        }
    }
    Ok(())
}

fn callee_base(callee: &str) -> &str {
    callee.rsplit('.').next().unwrap_or(callee)
}

fn collect_required_laws(
    e: &Expr,
    law_names: &[String],
    used: &mut std::collections::HashSet<String>,
) {
    if let ExprKind::MethodCall {
        receiver,
        method,
        args,
    } = &e.kind
    {
        if method == "require" {
            collect_required_laws(receiver, law_names, used);
            for a in args {
                mark_law_refs(a, law_names, used);
                collect_required_laws(a, law_names, used);
            }
            return;
        }
    }
    e.for_each_child(|c| collect_required_laws(c, law_names, used));
}

fn mark_law_refs(e: &Expr, law_names: &[String], used: &mut std::collections::HashSet<String>) {
    match &e.kind {
        ExprKind::Var(name) => {
            let base = callee_base(name);
            if law_names.iter().any(|n| n == base || n == name) {
                used.insert(base.to_string());
            }
        }
        ExprKind::Call { callee, args } => {
            let base = callee_base(callee);
            if law_names.iter().any(|n| n == base || n == callee.as_str()) {
                used.insert(base.to_string());
            }
            for a in args {
                mark_law_refs(a, law_names, used);
            }
        }
        _ => e.for_each_child(|c| mark_law_refs(c, law_names, used)),
    }
}

fn overlay_ext(kind: OverlayKind) -> &'static str {
    match kind {
        OverlayKind::Sim => "*.scuzz_sim",
        OverlayKind::Drivers => "*.scuzz_drivers",
    }
}

fn reject_overlay_extras(
    prog: &Program,
    label: &str,
    kind: OverlayKind,
) -> Result<(), OverlayError> {
    if !prog.aliases.is_empty()
        || !prog.traits.is_empty()
        || !prog.impls.is_empty()
        || !prog.imports.is_empty()
    {
        return Err(OverlayError::Msg(format!(
            "{label}: {} must only replace defs",
            overlay_ext(kind)
        )));
    }
    Ok(())
}

fn strip_spans(e: Expr) -> Expr {
    Expr::dummy(e.kind).map_children(strip_spans)
}

fn option_expr_matches(live: &Option<Expr>, sim: &Option<Expr>) -> bool {
    match (live, sim) {
        (None, None) => true,
        (Some(a), Some(b)) => strip_spans(a.clone()) == strip_spans(b.clone()),
        _ => false,
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
    if sim.is_law {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def cannot be a law"
        )));
    }
    if live_def.type_params != sim.type_params {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` type params mismatch",
            sim.name
        )));
    }
    if live_def.is_private != sim.is_private {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` privacy mismatch",
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
        if !option_expr_matches(&lp.default, &sp.default) {
            return Err(OverlayError::Msg(format!(
                "{label}: sim def `{}` param `{}` default mismatch",
                sim.name, sp.name
            )));
        }
        if !option_expr_matches(&lp.rfn, &sp.rfn) {
            return Err(OverlayError::Msg(format!(
                "{label}: sim def `{}` param `{}` where mismatch",
                sim.name, sp.name
            )));
        }
    }
    if live_def.ret != sim.ret {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` return type mismatch (live {:?}, sim {:?})",
            sim.name, live_def.ret, sim.ret
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
    let prog = parse_file(&ov.text, &ov.label)?;
    reject_overlay_extras(&prog, &ov.label, OverlayKind::Drivers)?;
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
    let law_names: Vec<String> = live
        .defs
        .iter()
        .filter(|d| d.is_law)
        .map(|d| d.name.clone())
        .collect();
    for mut d in prog.defs {
        d.module = ov.stem.clone();
        check_driver_def(live, &d, &ov.label, &law_names)?;
        d.is_driver = true;
        names.push(d.name.clone());
        live.defs.push(d);
    }
    Ok(())
}

fn check_driver_def(
    live: &Program,
    d: &FunDef,
    label: &str,
    law_names: &[String],
) -> Result<(), OverlayError> {
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
            "{label}: driver `{}` takes at most one Int, String, or Bool param",
            d.name
        )));
    }
    for p in &d.params {
        if !matches!(p.ty, Type::Int | Type::String | Type::Bool) {
            return Err(OverlayError::Msg(format!(
                "{label}: driver `{}` param `{}` must be Int, String, or Bool",
                d.name, p.name
            )));
        }
    }
    if expr_has_law(&d.body) || expr_mentions_law_name(&d.body, law_names) {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` must not call Law.* or `.require`",
            d.name
        )));
    }
    Ok(())
}

pub(crate) fn expr_has_law(e: &Expr) -> bool {
    let here = match &e.kind {
        ExprKind::Call { callee, .. } => callee.starts_with("Law."),
        ExprKind::MethodCall { method, .. } => method == "require",
        _ => false,
    };
    if here {
        return true;
    }
    let mut found = false;
    e.for_each_child(|c| {
        if !found {
            found = expr_has_law(c);
        }
    });
    found
}

fn law_name_hit(name: &str, law_names: &[String]) -> bool {
    let base = callee_base(name);
    law_names.iter().any(|n| n == base || n == name)
}

fn expr_mentions_law_name(e: &Expr, law_names: &[String]) -> bool {
    if law_names.is_empty() {
        return false;
    }
    let here = match &e.kind {
        ExprKind::Var(name) => law_name_hit(name, law_names),
        ExprKind::Call { callee, .. } => law_name_hit(callee, law_names),
        _ => false,
    };
    if here {
        return true;
    }
    let mut found = false;
    e.for_each_child(|c| {
        if !found {
            found = expr_mentions_law_name(c, law_names);
        }
    });
    found
}

/// Table lines for `build/drivers.txt`: `name`, `name i`, `name s`, `name b`,
/// or several kind tokens (`name i i`). Includes parameterized laws.
pub fn driver_table_text(program: &Program) -> String {
    let mut out = String::new();
    for d in &program.defs {
        if d.is_driver || (d.is_law && !d.params.is_empty()) {
            push_drive_spec(&mut out, d);
        }
    }
    out
}

fn push_drive_spec(out: &mut String, d: &FunDef) {
    out.push_str(&d.name);
    for p in &d.params {
        match p.ty {
            Type::String => out.push_str(" s"),
            Type::Bool => out.push_str(" b"),
            _ => out.push_str(" i"),
        }
    }
    out.push('\n');
}

/// Rewrite calls and record construction so `where` predicates become `Law.check`
/// at the use site. Live builds skip this step.
pub fn residualize_refinements(program: &mut Program) {
    let defs = program.defs.clone();
    let enums = program.enums.clone();
    let imports = program.imports.clone();
    for d in &mut program.defs {
        let module = d.module.clone();
        d.body = residualize_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &defs,
            &enums,
            &imports,
            &module,
        );
    }
    let main_mod = program.main.module.clone();
    program.main.body = residualize_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &defs,
        &enums,
        &imports,
        &main_mod,
    );
    for en in &mut program.enums {
        let module = en.module.clone();
        for m in &mut en.methods {
            m.body = residualize_expr(
                std::mem::replace(&mut m.body, Expr::dummy(ExprKind::Unit)),
                &defs,
                &enums,
                &imports,
                &module,
            );
        }
    }
    for im in &mut program.impls {
        let module = im.module.clone();
        for m in &mut im.methods {
            m.body = residualize_expr(
                std::mem::replace(&mut m.body, Expr::dummy(ExprKind::Unit)),
                &defs,
                &enums,
                &imports,
                &module,
            );
        }
    }
}

fn visible_def(d: &FunDef, current_module: &str) -> bool {
    !d.is_private || d.module == current_module
}

fn imported_def<'a>(
    defs: &'a [FunDef],
    imports: &[Import],
    callee: &str,
    current_module: &str,
) -> Option<&'a FunDef> {
    for im in imports {
        if im.in_module != current_module {
            continue;
        }
        if im.is_wildcard() {
            if let Some(d) = defs
                .iter()
                .find(|d| d.module == im.from_module && d.name == callee && !d.is_private)
            {
                return Some(d);
            }
        } else if im.local_name() == callee {
            return defs
                .iter()
                .find(|d| d.module == im.from_module && d.name == im.name);
        }
    }
    None
}

fn find_def<'a>(
    defs: &'a [FunDef],
    imports: &[Import],
    callee: &str,
    current_module: &str,
) -> Option<&'a FunDef> {
    if let Some((m, name)) = split_dotted(callee) {
        return defs
            .iter()
            .find(|d| d.module == m && d.name == name && visible_def(d, current_module));
    }
    if let Some(d) = defs
        .iter()
        .find(|d| d.module == current_module && d.name == callee)
    {
        return Some(d);
    }
    if let Some(d) = imported_def(defs, imports, callee, current_module) {
        return Some(d);
    }
    let hits: Vec<_> = defs
        .iter()
        .filter(|d| d.name == callee && visible_def(d, current_module))
        .collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

fn imported_enum<'a>(
    enums: &'a [EnumDef],
    imports: &[Import],
    callee: &str,
    current_module: &str,
) -> Option<&'a EnumDef> {
    for im in imports {
        if im.in_module != current_module {
            continue;
        }
        if im.is_wildcard() {
            if let Some(e) = enums
                .iter()
                .find(|e| e.module == im.from_module && e.name == callee)
            {
                return Some(e);
            }
        } else if im.local_name() == callee {
            return enums
                .iter()
                .find(|e| e.module == im.from_module && e.name == im.name);
        }
    }
    None
}

fn find_enum<'a>(
    enums: &'a [EnumDef],
    imports: &[Import],
    callee: &str,
    current_module: &str,
) -> Option<&'a EnumDef> {
    if let Some((m, name)) = split_dotted(callee) {
        return enums.iter().find(|e| e.module == m && e.name == name);
    }
    if let Some(e) = enums
        .iter()
        .find(|e| e.module == current_module && e.name == callee)
    {
        return Some(e);
    }
    if let Some(e) = imported_enum(enums, imports, callee, current_module) {
        return Some(e);
    }
    let hits: Vec<_> = enums.iter().filter(|e| e.name == callee).collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

fn find_record<'a>(
    enums: &'a [EnumDef],
    imports: &[Import],
    callee: &str,
    current_module: &str,
) -> Option<&'a EnumDef> {
    find_enum(enums, imports, callee, current_module).filter(|e| e.is_record)
}

fn law_check(name: String, pred: Expr, value: Expr, span: Span) -> Expr {
    Expr::new(
        ExprKind::Call {
            callee: "Law.check".into(),
            args: vec![Expr::new(ExprKind::StrLit(name), span.clone()), pred, value],
        },
        span,
    )
}

fn wrap_refined(
    label: &str,
    names: &[(String, Option<Expr>)],
    args: Vec<Expr>,
    span: &Span,
    make_inner: impl FnOnce(Vec<Expr>) -> Expr,
) -> Expr {
    if names.iter().all(|(_, r)| r.is_none()) || names.len() != args.len() {
        return make_inner(args);
    }
    let checked: Vec<Expr> = names
        .iter()
        .map(|(n, rfn)| {
            let var = Expr::new(ExprKind::Var(n.clone()), span.clone());
            match rfn {
                Some(pred) => law_check(format!("{label}.{n}"), pred.clone(), var, span.clone()),
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
    span: Span,
    defs: &[FunDef],
    enums: &[EnumDef],
    imports: &[Import],
    current_module: &str,
) -> Expr {
    if let Some(d) = find_def(defs, imports, &callee, current_module) {
        if d.params.iter().any(|p| p.rfn.is_some()) {
            let names: Vec<_> = d
                .params
                .iter()
                .map(|p| (p.name.clone(), p.rfn.clone()))
                .collect();
            let c = callee.clone();
            let sp = span.clone();
            return wrap_refined(&d.name, &names, args, &span, move |a| {
                Expr::new(ExprKind::Call { callee: c, args: a }, sp)
            });
        }
    }
    if let Some(en) = find_record(enums, imports, &callee, current_module) {
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
            return wrap_refined(&en.name, &names, args, &span, move |a| {
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

pub(crate) fn residualize_adt(
    enum_name: String,
    case_name: String,
    args: Vec<Expr>,
    type_args: Vec<Type>,
    span: Span,
    enums: &[EnumDef],
    current_module: &str,
) -> Expr {
    let construct = |args| {
        Expr::new(
            ExprKind::AdtConstruct {
                enum_name: enum_name.clone(),
                case_name: case_name.clone(),
                args,
                type_args: type_args.clone(),
            },
            span.clone(),
        )
    };
    let Some(en) = find_enum(enums, &[], &enum_name, current_module).or_else(|| {
        enums.iter().find(|e| {
            e.name == enum_name
                || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == enum_name)
        })
    }) else {
        return construct(args);
    };
    let Some(c) = en.cases.iter().find(|c| c.name == case_name) else {
        return construct(args);
    };
    if !c.has_rfns() {
        return construct(args);
    }
    let names: Vec<_> = c
        .fields
        .iter()
        .enumerate()
        .map(|(i, (n, _))| (n.clone(), c.field_rfn(i).cloned()))
        .collect();
    wrap_refined(&en.name, &names, args, &span, construct)
}

fn residualize_expr(
    expr: Expr,
    defs: &[FunDef],
    enums: &[EnumDef],
    imports: &[Import],
    current_module: &str,
) -> Expr {
    let expr = expr.map_children(|c| residualize_expr(c, defs, enums, imports, current_module));
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Call { callee, args } => {
            residualize_call(callee, args, span, defs, enums, imports, current_module)
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => residualize_adt(
            enum_name,
            case_name,
            args,
            type_args,
            span,
            enums,
            current_module,
        ),
        kind => Expr::new(kind, span),
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

/// Collect format-checked sources under `dir` (sorted, recursive).
pub fn collect_fmt_sources(dir: &std::path::Path) -> std::io::Result<Vec<std::path::PathBuf>> {
    let mut out = Vec::new();
    collect_fmt_sources_into(dir, &mut out)?;
    out.sort();
    Ok(out)
}

fn collect_fmt_sources_into(
    dir: &std::path::Path,
    out: &mut Vec<std::path::PathBuf>,
) -> std::io::Result<()> {
    if !dir.is_dir() {
        return Ok(());
    }
    for entry in std::fs::read_dir(dir)? {
        let path = entry?.path();
        if path.is_dir() {
            collect_fmt_sources_into(&path, out)?;
        } else if is_fmt_source(&path) {
            out.push(path);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_sources;

    #[test]
    fn sim_replaces_live_def_and_keeps_laws() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def title(): String = \"Live\"\nlaw always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(title())\n"
                .into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Sim,
            path: PathBuf::new(),
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
            path: PathBuf::new(),
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
            path: PathBuf::new(),
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
            path: PathBuf::new(),
            label: "Main.scuzz_drivers".into(),
            text: "def plusN(n: Int): IO[Unit] =\n  IO.pure(note(n))\n".into(),
        }];
        let prog = apply_overlays(live, &overlays).unwrap();
        assert_eq!(prog.driver_names, vec!["plusN".to_string()]);
        assert!(prog.defs.iter().any(|d| d.is_driver && d.name == "plusN"));
        assert_eq!(driver_table_text(&prog), "plusN i\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def note(n: Int): Unit = ()\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
            label: "Main.scuzz_drivers".into(),
            text: "def flagDrive(on: Bool): IO[Unit] =\n  IO.pure(note(if (on) 1 else 0))\n".into(),
        }];
        let prog = apply_overlays(live, &overlays).unwrap();
        assert_eq!(driver_table_text(&prog), "flagDrive b\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
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
    fn parameterized_law_need_not_require() {
        let prog = parse_sources(&[(
            "Main.scuzz".into(),
            "law addComm(a: Int, b: Int): Bool = a + b == b + a\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let laws = collect_law_names(&prog).unwrap();
        check_laws_applied(&prog, &laws).unwrap();
        assert_eq!(driver_table_text(&prog).trim(), "addComm i i");
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

    fn sim_ov(text: &str) -> Vec<OverlaySource> {
        vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Sim,
            path: PathBuf::new(),
            label: "Main.scuzz_sim".into(),
            text: text.into(),
        }]
    }

    fn live_with(src: &str) -> Program {
        parse_sources(&[("Main.scuzz".into(), src.into())]).unwrap()
    }

    #[test]
    fn sim_rejects_type_params_mismatch() {
        let live =
            live_with("def id[T](x: T): T = x\n@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err = apply_overlays(live, &sim_ov("def id(x: Int): Int = x\n")).unwrap_err();
        assert!(err.to_string().contains("type params mismatch"), "{err}");
    }

    #[test]
    fn sim_rejects_privacy_mismatch() {
        let live = live_with(
            "private def title(): String = \"L\"\n@main def main: IO[Unit] = IO.println(\"x\")\n",
        );
        let err = apply_overlays(live, &sim_ov("def title(): String = \"S\"\n")).unwrap_err();
        assert!(err.to_string().contains("privacy mismatch"), "{err}");
    }

    #[test]
    fn sim_rejects_default_mismatch() {
        let live =
            live_with("def add(n: Int, m: Int = 1): Int = n + m\n@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err =
            apply_overlays(live, &sim_ov("def add(n: Int, m: Int): Int = n + m\n")).unwrap_err();
        assert!(err.to_string().contains("default mismatch"), "{err}");
    }

    #[test]
    fn sim_rejects_where_strip() {
        let live = live_with(
            "def note(n: Int where n >= 0): Unit = ()\n@main def main: IO[Unit] = IO.println(\"x\")\n",
        );
        let err = apply_overlays(live, &sim_ov("def note(n: Int): Unit = ()\n")).unwrap_err();
        assert!(err.to_string().contains("where mismatch"), "{err}");
    }

    #[test]
    fn sim_rejects_law_def() {
        let live =
            live_with("def title(): Bool = true\n@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err = apply_overlays(live, &sim_ov("law title: Bool = true\n")).unwrap_err();
        assert!(err.to_string().contains("sim def cannot be a law"), "{err}");
    }

    #[test]
    fn sim_rejects_extra_alias() {
        let live = live_with("@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err = apply_overlays(
            live,
            &sim_ov("type Box = Int\ndef title(): String = \"S\"\n"),
        )
        .unwrap_err();
        assert!(
            err.to_string()
                .contains("*.scuzz_sim must only replace defs"),
            "{err}"
        );
    }

    #[test]
    fn drivers_reject_extra_import() {
        let live = live_with(
            "def note(n: Int): Unit = ()\n@main def main: IO[Unit] = IO.println(\"x\")\n",
        );
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
            label: "Main.scuzz_drivers".into(),
            text: "import Main.note\ndef plusN(n: Int): IO[Unit] =\n  IO.pure(note(n))\n".into(),
        }];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(
            err.to_string()
                .contains("*.scuzz_drivers must only replace defs"),
            "{err}"
        );
    }

    #[test]
    fn drivers_reject_law_name_call() {
        let live = live_with(
            "law always: Bool = 1 == 1\n@main def main: IO[Unit] = IO.println(\"x\").require(always)\n",
        );
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
            label: "Main.scuzz_drivers".into(),
            text: "def plusN(): IO[Unit] =\n  IO.pure(always)\n".into(),
        }];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(err.to_string().contains("must not call Law"), "{err}");
    }

    #[test]
    fn residualize_skips_other_module_same_name() {
        let mut prog = parse_sources(&[
            (
                "A.scuzz".into(),
                "def note(n: Int where n >= 0): Unit = ()\n".into(),
            ),
            (
                "B.scuzz".into(),
                "def note(n: Int): Unit = ()\n@main def main: IO[Unit] = IO.pure(note(-1))\n"
                    .into(),
            ),
        ])
        .unwrap();
        residualize_refinements(&mut prog);
        let dumped = format!("{:?}", prog.main.body.kind);
        assert!(
            !dumped.contains("Law.check"),
            "B.note has no where; got {dumped}"
        );
    }

    #[test]
    fn residualize_walks_record_method_body() {
        let mut prog = parse_sources(&[(
            "Main.scuzz".into(),
            "def note(n: Int where n >= 0): Unit = ()\nrecord Box(x: Int):\n  def bump(): Unit =\n    note(-1)\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        residualize_refinements(&mut prog);
        let en = prog.enums.iter().find(|e| e.name == "Box").unwrap();
        let dumped = format!("{:?}", en.methods[0].body.kind);
        assert!(
            dumped.contains("Law.check"),
            "record method should wrap note: {dumped}"
        );
    }
}

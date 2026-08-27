//! Stem-paired `*.scuzz_sim` / `*.scuzz_drivers` overlays, `*.scuzz_verify`
//! claims, and `.require` / `where` residualization.

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, FunDef, Import, Program, Type, UnOp};
use crate::parser::{parse_file, ParseError};
use crate::resolve::{enum_id, split_dotted};
use crate::span::Span;
use std::path::PathBuf;
use thiserror::Error;

/// Nested ADT / list depth for generation. Deeper trees pick a leaf case or `[]`.
const GEN_DEPTH_MAX: usize = 3;

#[derive(Debug, Error)]
pub enum OverlayError {
    #[error("{0}")]
    Msg(String),
    #[error("{msg}")]
    At { msg: String, span: Span },
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

/// Apply same-name sim replacements, then merge `*.scuzz_drivers`, then merge
/// `*.scuzz_verify`. `.require` is rewritten by type in field resolution
/// (verify) or erased live.
#[cfg(test)]
pub fn apply_overlays(live: Program, overlays: &[OverlaySource]) -> Result<Program, OverlayError> {
    apply_verify_overlays(live, overlays, &[])
}

/// Apply sim, drivers, and verify predicates from a resolved package graph.
pub fn apply_verify_overlays(
    mut live: Program,
    overlays: &[OverlaySource],
    verifies: &[crate::verify::VerifySource],
) -> Result<Program, OverlayError> {
    for sim in overlays {
        if sim.kind != OverlayKind::Sim {
            continue;
        }
        let prog = parse_file(&sim.text, &sim.label)?;
        reject_overlay_header(&prog, &sim.label, OverlayKind::Sim)?;
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
    crate::verify::apply_verifies(&mut live, verifies)?;
    check_drive_table(&live)?;
    live.driver_names = driver_names;
    Ok(live)
}

/// Drop leftover verify thunk names. Live `build` / `run` must not emit verify
/// predicates (verify overlays are not applied).
pub fn erase_properties(program: &mut Program) {
    program.verify_seeds.clear();
    program.verify_preds.clear();
    program.verify_rels.clear();
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

fn overlay_ext(kind: OverlayKind) -> &'static str {
    match kind {
        OverlayKind::Sim => "*.scuzz_sim",
        OverlayKind::Drivers => "*.scuzz_drivers",
    }
}

fn overlay_kind_word(kind: OverlayKind) -> &'static str {
    match kind {
        OverlayKind::Sim => "sim",
        OverlayKind::Drivers => "driver",
    }
}

fn reject_overlay_header(
    prog: &Program,
    label: &str,
    kind: OverlayKind,
) -> Result<(), OverlayError> {
    reject_overlay_extras(prog, label, kind)?;
    let ext = overlay_ext(kind);
    if !prog.main.name.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{label}: {ext} must not define @main"
        )));
    }
    if !prog.enums.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{label}: {ext} must not define enums"
        )));
    }
    Ok(())
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

fn find_live_twin<'a>(
    live: &'a Program,
    overlay: &FunDef,
    label: &str,
    kind: OverlayKind,
) -> Result<&'a FunDef, OverlayError> {
    let word = overlay_kind_word(kind);
    let Some(live_def) = live
        .defs
        .iter()
        .find(|d| d.module == overlay.module && d.name == overlay.name)
    else {
        return Err(OverlayError::Msg(format!(
            "{label}: {word} def `{}` has no live twin",
            overlay.name
        )));
    };
    if live_def.type_params != overlay.type_params {
        return Err(OverlayError::Msg(format!(
            "{label}: {word} def `{}` type params mismatch",
            overlay.name
        )));
    }
    if live_def.is_private != overlay.is_private {
        return Err(OverlayError::Msg(format!(
            "{label}: {word} def `{}` privacy mismatch",
            overlay.name
        )));
    }
    if live_def.params.len() != overlay.params.len() {
        return Err(OverlayError::Msg(format!(
            "{label}: {word} def `{}` arity mismatch (live {}, {word} {})",
            overlay.name,
            live_def.params.len(),
            overlay.params.len()
        )));
    }
    for (lp, op) in live_def.params.iter().zip(overlay.params.iter()) {
        if lp.ty != op.ty {
            return Err(OverlayError::Msg(format!(
                "{label}: {word} def `{}` param `{}` type mismatch (live {:?}, {word} {:?})",
                overlay.name, op.name, lp.ty, op.ty
            )));
        }
        if !option_expr_matches(&lp.default, &op.default) {
            return Err(OverlayError::Msg(format!(
                "{label}: {word} def `{}` param `{}` default mismatch",
                overlay.name, op.name
            )));
        }
        if !option_expr_matches(&lp.rfn, &op.rfn) {
            return Err(OverlayError::Msg(format!(
                "{label}: {word} def `{}` param `{}` where mismatch",
                overlay.name, op.name
            )));
        }
    }
    if live_def.ret != overlay.ret {
        return Err(OverlayError::Msg(format!(
            "{label}: {word} def `{}` return type mismatch (live {:?}, {word} {:?})",
            overlay.name, live_def.ret, overlay.ret
        )));
    }
    Ok(live_def)
}

fn replace_sim_def(live: &mut Program, sim: &FunDef, label: &str) -> Result<(), OverlayError> {
    find_live_twin(live, sim, label, OverlayKind::Sim)?;
    let idx = live
        .defs
        .iter()
        .position(|d| d.module == sim.module && d.name == sim.name)
        .expect("twin checked");
    live.defs[idx] = sim.clone();
    Ok(())
}

fn apply_driver_overlay(
    live: &mut Program,
    ov: &OverlaySource,
    names: &mut Vec<String>,
) -> Result<(), OverlayError> {
    let prog = parse_file(&ov.text, &ov.label)?;
    reject_overlay_header(&prog, &ov.label, OverlayKind::Drivers)?;
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
    if let Some(prev) = live
        .defs
        .iter()
        .find(|l| l.name == d.name && is_drive_spec(l))
    {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` collides with {}.{}",
            d.name, prev.module, prev.name
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
            "{label}: driver `{}` takes at most one generator-friendly param",
            d.name
        )));
    }
    for p in &d.params {
        if !type_is_generator_friendly(&p.ty, live, &d.module, 0) {
            return Err(OverlayError::Msg(format!(
                "{label}: driver `{}` param `{}` must be Int, String, Bool, List, or a record/enum of those",
                d.name, p.name
            )));
        }
    }
    if expr_has_property(&d.body) {
        return Err(OverlayError::Msg(format!(
            "{label}: driver `{}` must not call Property.* or `.require`",
            d.name
        )));
    }
    Ok(())
}

pub(crate) fn expr_has_property(e: &Expr) -> bool {
    let here = match &e.kind {
        ExprKind::Call { callee, .. } => callee.starts_with("Property."),
        ExprKind::MethodCall { method, .. } => method == "require",
        _ => false,
    };
    if here {
        return true;
    }
    let mut found = false;
    e.for_each_child(|c| {
        if !found {
            found = expr_has_property(c);
        }
    });
    found
}

/// Matches `SZ_DRIVERS_MAX` in `crates/runtime/src/testrt.c`.
const DRIVE_NAMES_MAX: usize = 32;

fn is_drive_spec(d: &FunDef) -> bool {
    d.is_driver
}

fn check_drive_table(program: &Program) -> Result<(), OverlayError> {
    let mut seen: std::collections::HashMap<String, &FunDef> = std::collections::HashMap::new();
    let mut n = 0usize;
    for d in &program.defs {
        if !is_drive_spec(d) {
            continue;
        }
        n += 1;
        if let Some(prev) = seen.get(&d.name) {
            return Err(OverlayError::Msg(format!(
                "driver `{}` collides with {}.{}",
                d.name, prev.module, prev.name
            )));
        }
        seen.insert(d.name.clone(), d);
    }
    if n > DRIVE_NAMES_MAX {
        return Err(OverlayError::Msg(format!(
            "too many drive names (max {DRIVE_NAMES_MAX})"
        )));
    }
    Ok(())
}

/// Table lines for `build/drivers.txt`. One line per driver or verify drive
/// oracle. Kind tokens are `i`, `s`, `b`, `[i]`, `Point(i>=0,i)`, and
/// `e:Some(i)|None`. A simple Int `where` bound joins the `i` token
/// (`noteDrive i>=0`).
pub fn driver_table_text(program: &Program) -> String {
    let mut out = String::new();
    for d in &program.defs {
        if d.is_driver {
            push_drive_spec(&mut out, d, program);
        }
    }
    out
}

fn push_drive_spec(out: &mut String, d: &FunDef, program: &Program) {
    out.push_str(&d.name);
    for p in &d.params {
        out.push(' ');
        out.push_str(&type_kind_token(
            &p.ty,
            p.rfn.as_ref(),
            &p.name,
            program,
            &d.module,
            0,
        ));
    }
    out.push('\n');
}

/// Spec line for one drive oracle, e.g. `bump i` or `area Rect(i>=0,i>=0)`.
pub(crate) fn drive_spec_line(d: &FunDef, program: &Program) -> String {
    let mut s = String::new();
    push_drive_spec(&mut s, d, program);
    s.trim_end().to_string()
}

fn int_bound_token(name: &str, rfn: Option<&Expr>) -> String {
    match rfn.and_then(|e| simple_cmp_bound(name, e)) {
        Some((op, k)) => format!("i{op}{k}"),
        None => "i".into(),
    }
}

pub(crate) fn type_is_generator_friendly(
    ty: &Type,
    program: &Program,
    module: &str,
    depth: usize,
) -> bool {
    match ty {
        Type::Int | Type::String | Type::Bool => true,
        Type::List(inner) => {
            if depth >= GEN_DEPTH_MAX {
                true
            } else {
                type_is_generator_friendly(inner, program, module, depth + 1)
            }
        }
        Type::Adt(_) | Type::App(_, _) => match enum_and_subst(ty, program, module) {
            Some((en, subst)) => {
                if is_opaque_app(ty) {
                    return false;
                }
                if en.cases.is_empty() {
                    return false;
                }
                if depth >= GEN_DEPTH_MAX {
                    return en.cases.iter().any(|c| case_is_gen_leaf(c, &subst));
                }
                en.cases.iter().all(|c| {
                    c.fields.iter().all(|(_, fty)| {
                        type_is_generator_friendly(
                            &apply_field_subst(fty, &subst),
                            program,
                            module,
                            depth + 1,
                        )
                    })
                })
            }
            None => false,
        },
        _ => false,
    }
}

fn is_opaque_app(ty: &Type) -> bool {
    match ty {
        Type::App(n, _) => {
            let bare = n.rsplit('.').next().unwrap_or(n);
            matches!(
                bare,
                "Fiber" | "Ref" | "Queue" | "Deferred" | "Resource" | "Stream" | "Map" | "Set"
            )
        }
        _ => false,
    }
}

fn lookup_enum<'a>(program: &'a Program, id: &str, module: &str) -> Option<&'a EnumDef> {
    program
        .enums
        .iter()
        .find(|e| enum_id(&e.module, &e.name) == id)
        .or_else(|| {
            program
                .enums
                .iter()
                .find(|e| e.module == module && e.name == id)
        })
        .or_else(|| {
            let hits: Vec<&EnumDef> = program.enums.iter().filter(|e| e.name == id).collect();
            if hits.len() == 1 {
                Some(hits[0])
            } else {
                None
            }
        })
}

fn enum_and_subst<'a>(
    ty: &'a Type,
    program: &'a Program,
    module: &str,
) -> Option<(&'a EnumDef, Vec<(String, Type)>)> {
    match ty {
        Type::Adt(id) => {
            let en = lookup_enum(program, id, module)?;
            Some((en, Vec::new()))
        }
        Type::App(id, args) => {
            if is_opaque_app(ty) {
                return None;
            }
            let en = lookup_enum(program, id, module)?;
            let subst = en
                .type_params
                .iter()
                .cloned()
                .zip(args.iter().cloned())
                .collect();
            Some((en, subst))
        }
        _ => None,
    }
}

fn type_is_leaf_field(ty: &Type) -> bool {
    matches!(ty, Type::Int | Type::String | Type::Bool)
}

fn case_is_gen_leaf(c: &crate::ast::EnumCase, subst: &[(String, Type)]) -> bool {
    c.fields
        .iter()
        .all(|(_, fty)| type_is_leaf_field(&apply_field_subst(fty, subst)))
}

fn apply_field_subst(ty: &Type, subst: &[(String, Type)]) -> Type {
    if subst.is_empty() {
        return ty.clone();
    }
    match ty {
        Type::Var(n) | Type::Adt(n) => subst
            .iter()
            .find(|(k, _)| k == n)
            .map(|(_, t)| t.clone())
            .unwrap_or_else(|| ty.clone()),
        Type::List(inner) => Type::List(Box::new(apply_field_subst(inner, subst))),
        Type::App(n, args) => {
            if args.is_empty() {
                if let Some((_, t)) = subst.iter().find(|(k, _)| k == n) {
                    return t.clone();
                }
            }
            Type::App(
                n.clone(),
                args.iter().map(|a| apply_field_subst(a, subst)).collect(),
            )
        }
        Type::Tuple(xs) => Type::Tuple(xs.iter().map(|t| apply_field_subst(t, subst)).collect()),
        other => other.clone(),
    }
}

fn type_kind_token(
    ty: &Type,
    rfn: Option<&Expr>,
    name: &str,
    program: &Program,
    module: &str,
    depth: usize,
) -> String {
    match ty {
        Type::String => "s".into(),
        Type::Bool => "b".into(),
        Type::Int => int_bound_token(name, rfn),
        Type::List(inner) => {
            format!(
                "[{}]",
                type_kind_token(inner, None, "", program, module, depth + 1)
            )
        }
        Type::Adt(_) | Type::App(_, _) => adt_kind_token(ty, program, module, depth),
        _ => "i".into(),
    }
}

fn adt_kind_token(ty: &Type, program: &Program, module: &str, depth: usize) -> String {
    let Some((en, subst)) = enum_and_subst(ty, program, module) else {
        return "i".into();
    };
    if en.is_record || (en.cases.len() == 1 && en.cases[0].name == en.name) {
        let case = &en.cases[0];
        let fields: Vec<String> = case
            .fields
            .iter()
            .enumerate()
            .map(|(i, (fname, fty))| {
                type_kind_token(
                    &apply_field_subst(fty, &subst),
                    case.field_rfn(i),
                    fname,
                    program,
                    module,
                    depth + 1,
                )
            })
            .collect();
        return format!("{}({})", en.name, fields.join(","));
    }
    if depth >= GEN_DEPTH_MAX {
        let cases: Vec<String> = en
            .cases
            .iter()
            .filter(|c| case_is_gen_leaf(c, &subst))
            .map(|c| format_enum_case_token(c, &subst, program, module, depth))
            .collect();
        if cases.is_empty() {
            return "i".into();
        }
        return format!("e:{}", cases.join("|"));
    }
    let cases: Vec<String> = en
        .cases
        .iter()
        .map(|c| format_enum_case_token(c, &subst, program, module, depth))
        .collect();
    format!("e:{}", cases.join("|"))
}

fn format_enum_case_token(
    c: &crate::ast::EnumCase,
    subst: &[(String, Type)],
    program: &Program,
    module: &str,
    depth: usize,
) -> String {
    if c.fields.is_empty() {
        c.name.clone()
    } else {
        let fs: Vec<String> = c
            .fields
            .iter()
            .enumerate()
            .map(|(i, (fname, fty))| {
                type_kind_token(
                    &apply_field_subst(fty, subst),
                    c.field_rfn(i),
                    fname,
                    program,
                    module,
                    depth + 1,
                )
            })
            .collect();
        format!("{}({})", c.name, fs.join(","))
    }
}

fn simple_cmp_bound(param: &str, e: &Expr) -> Option<(&'static str, i64)> {
    let ExprKind::Binary { op, left, right } = &e.kind else {
        return None;
    };
    let (lit, flipped) = if var_named(left, param) {
        (int_lit(right)?, false)
    } else if var_named(right, param) {
        (int_lit(left)?, true)
    } else {
        return None;
    };
    let op = if flipped {
        flip_cmp_token(*op)?
    } else {
        cmp_token(*op)?
    };
    Some((op, lit))
}

fn var_named(e: &Expr, name: &str) -> bool {
    matches!(&e.kind, ExprKind::Var(n) if n == name)
}

fn int_lit(e: &Expr) -> Option<i64> {
    match &e.kind {
        ExprKind::IntLit(n) => Some(*n),
        ExprKind::Unary {
            op: UnOp::Neg,
            expr,
        } => match &expr.kind {
            ExprKind::IntLit(n) => n.checked_neg(),
            _ => None,
        },
        _ => None,
    }
}

fn cmp_token(op: BinOp) -> Option<&'static str> {
    match op {
        BinOp::Ge => Some(">="),
        BinOp::Gt => Some(">"),
        BinOp::Le => Some("<="),
        BinOp::Lt => Some("<"),
        _ => None,
    }
}

fn flip_cmp_token(op: BinOp) -> Option<&'static str> {
    match op {
        BinOp::Ge => Some("<="),
        BinOp::Gt => Some("<"),
        BinOp::Le => Some(">="),
        BinOp::Lt => Some(">"),
        _ => None,
    }
}

/// Rewrite calls and record construction so `where` predicates become `Property.check`
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

fn property_check(name: String, pred: Expr, value: Expr, span: Span) -> Expr {
    Expr::new(
        ExprKind::Call {
            callee: "Property.check".into(),
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
                Some(pred) => {
                    property_check(format!("{label}.{n}"), pred.clone(), var, span.clone())
                }
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

/// True when `path` is a stem-paired overlay (`*.scuzz_sim` / `*.scuzz_drivers`).
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
        Some("scuzz" | "scuzz_sim" | "scuzz_drivers" | "scuzz_verify")
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
    fn sim_replaces_live_def() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def title(): String = \"Live\"\n@main def main: IO[Unit] = IO.println(title())\n"
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
        let title = prog.defs.iter().find(|d| d.name == "title").unwrap();
        match &title.body.kind {
            crate::ast::ExprKind::StrLit(s) => assert_eq!(s, "Sim"),
            other => panic!("expected sim body, got {other:?}"),
        }
    }

    #[test]
    fn erase_drops_verify_names() {
        let mut live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        live.verify_seeds = vec!["drive addTwoThree".into()];
        live.verify_preds = vec!["countOk".into()];
        erase_properties(&mut live);
        assert!(live.verify_seeds.is_empty());
        assert!(live.verify_preds.is_empty());
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
    fn drivers_merge_and_reject_property_calls() {
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
            text: "def bad(): IO[Unit] =\n  Property.assert(\"x\", 1)\n".into(),
        }];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(err.to_string().contains("must not call Property"));
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
        assert!(
            src.contains("Property.check") || matches!(prog.main.body.kind, ExprKind::Let { .. })
        );
        match &prog.main.body.kind {
            ExprKind::IoPure(inner) => match &inner.kind {
                ExprKind::Let { name, body, .. } => {
                    assert_eq!(name, "n");
                    match &body.kind {
                        ExprKind::Call { callee, args } => {
                            assert_eq!(callee, "note");
                            match &args[0].kind {
                                ExprKind::Call { callee, .. } => {
                                    assert_eq!(callee, "Property.check")
                                }
                                other => panic!("expected Property.check arg, got {other:?}"),
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
    fn empty_verify_fails_apply() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let err = apply_verify(live, "\n").unwrap_err();
        assert!(err.to_string().contains("drive oracle"), "{err}");
    }

    #[test]
    fn verify_oracle_publishes_drive() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def add(a: Int, b: Int): Int = a + b\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_verify(
            live,
            "def add(a: Int, b: Int): Bool =\n  Main.add(a, b) == Main.add(b, a)\n",
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog).trim(), "add i i");
    }

    #[test]
    fn driver_table_appends_simple_int_bounds() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def noteDrive(n: Int where n >= 0): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_verify(
            live,
            "def noteDrive(n: Int where n >= 0): Bool =\n  Main.noteDrive(n) == n\n",
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "noteDrive i>=0\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def pos(n: Int where n > 0): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_verify(
            live,
            "def pos(n: Int where n > 0): Bool =\n  Main.pos(n) == n\n",
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "pos i>0\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def cap(n: Int where 10 >= n): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_verify(
            live,
            "def cap(n: Int where 10 >= n): Bool =\n  Main.cap(n) == n\n",
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "cap i<=10\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def both(n: Int where n >= 0 && n <= 10): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_verify(
            live,
            "def both(n: Int where n >= 0 && n <= 10): Bool =\n  Main.both(n) == n\n",
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "both i\n");

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
            text: "def noteDrive(n: Int where n >= 0): IO[Unit] =\n  IO.pure(())\n".into(),
        }];
        let prog = apply_overlays(live, &overlays).unwrap();
        assert_eq!(driver_table_text(&prog), "noteDrive i>=0\n");
    }

    #[test]
    fn driver_table_records_enums_and_lists() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "record Rect(w: Int where w >= 0, h: Int where h >= 0)\n\
             @main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_overlays(
            live,
            &[driver_ov(
                "Main",
                "def areaIsProduct(r: Rect): IO[Unit] =\n  IO.pure(())\n",
            )],
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "areaIsProduct Rect(i>=0,i>=0)\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "enum Color:\n  case Red\n  case Blue\n\
             @main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_overlays(
            live,
            &[driver_ov(
                "Main",
                "def isRed(c: Color): IO[Unit] =\n  IO.pure(())\n",
            )],
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "isRed e:Red|Blue\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "enum Opt[T]:\n  case Some(x: T)\n  case None\n\
             @main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_overlays(
            live,
            &[driver_ov(
                "Main",
                "def describe(o: Opt[Int]): IO[Unit] =\n  IO.pure(())\n",
            )],
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "describe e:Some(i)|None\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let prog = apply_overlays(
            live,
            &[driver_ov(
                "Main",
                "def sumLen(xs: List[Int]): IO[Unit] =\n  IO.pure(())\n",
            )],
        )
        .unwrap();
        assert_eq!(driver_table_text(&prog), "sumLen [i]\n");

        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "enum Term:\n  case N(n: Int)\n  case Add(a: Term, b: Term)\n\
             @main def main: IO[Unit] = IO.println(\"x\")\n"
                .into(),
        )])
        .unwrap();
        let prog = apply_overlays(
            live,
            &[driver_ov(
                "Main",
                "def termDiff(t: Term): IO[Unit] =\n  IO.pure(())\n",
            )],
        )
        .unwrap();
        let table = driver_table_text(&prog);
        assert!(
            table.starts_with("termDiff e:N(i)|Add(") && table.contains("N(i)"),
            "{table}"
        );
        assert!(!table.contains("termDiff i\n"), "{table}");
    }

    #[test]
    fn rejects_float_for_param() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def bad(x: Float): Float = x\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
        )])
        .unwrap();
        let err =
            apply_verify(live, "def bad(x: Float): Bool =\n  Main.bad(x) == x\n").unwrap_err();
        assert!(err.to_string().contains("record/enum"), "{}", err);
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

    fn apply_verify(live: Program, text: &str) -> Result<Program, OverlayError> {
        apply_verify_overlays(
            live,
            &[],
            &[crate::verify::VerifySource {
                label: "pkg/claim.scuzz_verify".into(),
                text: text.into(),
                path: PathBuf::new(),
            }],
        )
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
    fn sim_rejects_property_keyword() {
        let live =
            live_with("def title(): Bool = true\n@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err = apply_overlays(live, &sim_ov("property title: Bool = true\n")).unwrap_err();
        assert!(
            err.to_string().contains("expected") || err.to_string().contains("property"),
            "{err}"
        );
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
    fn drivers_reject_require() {
        let live = live_with("@main def main: IO[Unit] = IO.println(\"x\")\n");
        let overlays = vec![OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
            label: "Main.scuzz_drivers".into(),
            text: "def plusN(): IO[Unit] =\n  IO.println(\"x\").require(1 == 1)\n".into(),
        }];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(err.to_string().contains("must not call Property"), "{err}");
    }

    fn driver_ov(stem: &str, text: &str) -> OverlaySource {
        OverlaySource {
            stem: stem.into(),
            kind: OverlayKind::Drivers,
            path: PathBuf::new(),
            label: format!("{stem}.scuzz_drivers"),
            text: text.into(),
        }
    }

    #[test]
    fn drivers_allow_binder_named_always() {
        let live = live_with("@main def main: IO[Unit] = IO.println(\"x\")\n");
        let overlays = vec![driver_ov(
            "Main",
            "def plusN(always: Bool): IO[Unit] =\n  IO.when(always, IO.println(\"x\"))\n",
        )];
        let prog = apply_overlays(live, &overlays).unwrap();
        assert!(prog.defs.iter().any(|d| d.is_driver && d.name == "plusN"));

        let live = live_with("@main def main: IO[Unit] = IO.println(\"x\")\n");
        let overlays = vec![driver_ov(
            "Main",
            "def plusN(): IO[Unit] =\n  for {\n    always = true\n    _ <- IO.when(always, IO.println(\"x\"))\n  } yield ()\n",
        )];
        apply_overlays(live, &overlays).unwrap();
    }

    #[test]
    fn drivers_reject_cross_module_drive_name() {
        let live = live_with("@main def main: IO[Unit] = IO.println(\"x\")\n");
        let overlays = vec![
            driver_ov("A", "def bump(n: Int): IO[Unit] = IO.pure(())\n"),
            driver_ov("B", "def bump(n: Int): IO[Unit] = IO.pure(())\n"),
        ];
        let err = apply_overlays(live, &overlays).unwrap_err();
        assert!(
            err.to_string()
                .contains("driver `bump` collides with A.bump"),
            "{err}"
        );
    }

    #[test]
    fn drivers_reject_verify_drive_name() {
        let live =
            live_with("def p(a: Int): Int = a\n@main def main: IO[Unit] = IO.println(\"x\")\n");
        let err = apply_verify_overlays(
            live,
            &[driver_ov("Main", "def p(n: Int): IO[Unit] = IO.pure(())\n")],
            &[crate::verify::VerifySource {
                label: "pkg/claim.scuzz_verify".into(),
                text: "def p(n: Int): Bool =\n  Main.p(n) == n\n".into(),
                path: PathBuf::new(),
            }],
        )
        .unwrap_err();
        assert!(err.to_string().contains("collides"), "{err}");
    }

    #[test]
    fn verify_oracles_reject_duplicate_drive_name() {
        let live = parse_sources(&[
            ("A.scuzz".into(), "def p(a: Int): Int = a\n".into()),
            (
                "B.scuzz".into(),
                "def q(a: Int): Int = a\n@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
            ),
        ])
        .unwrap();
        let err = apply_verify_overlays(
            live,
            &[],
            &[
                crate::verify::VerifySource {
                    label: "pkg/a.scuzz_verify".into(),
                    text: "def p(n: Int): Bool =\n  A.p(n) == n\n".into(),
                    path: PathBuf::new(),
                },
                crate::verify::VerifySource {
                    label: "pkg/b.scuzz_verify".into(),
                    text: "def p(n: Int): Bool =\n  B.q(n) == n\n".into(),
                    path: PathBuf::new(),
                },
            ],
        )
        .unwrap_err();
        assert!(err.to_string().contains("duplicate"), "{err}");
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
            !dumped.contains("Property.check"),
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
            dumped.contains("Property.check"),
            "record method should wrap note: {dumped}"
        );
    }

    #[test]
    fn overlay_kind_ignores_intent_path() {
        let p = std::path::Path::new("src/Main.scuzz_intent");
        assert!(overlay_kind_from_path(p).is_none());
        assert!(!is_fmt_source(p));
        let p = std::path::Path::new("intent.scuzz_intent");
        assert!(overlay_kind_from_path(p).is_none());
        assert!(!is_fmt_source(p));
    }
}

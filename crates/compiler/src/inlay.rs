//! Inlay hints from the same parse as `check`. No second typer.

use crate::ast::{Expr, ExprKind, Program};
use crate::hover::kit_sig;
use crate::resolve::module_id_from_label;
use crate::signature::{param_names_from_label, sig_label};

pub const HINT_PARAMETER: u8 = 2;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InlayHint {
    pub offset: usize,
    pub label: String,
    pub kind: u8,
}

/// Parameter-name hints at call arguments in `current_file`.
/// Skip an argument when it is already a same-named `Var`.
pub fn inlay_hints_in_source(
    program: &Program,
    current_file: &str,
    range: Option<(usize, usize)>,
) -> Vec<InlayHint> {
    let mut out = Vec::new();
    let module = module_id_from_label(current_file);
    for d in &program.defs {
        if d.module == module {
            walk_expr(program, &module, &d.body, range, &mut out);
        }
    }
    if program.main.module == module && !program.main.name.is_empty() {
        walk_expr(program, &module, &program.main.body, range, &mut out);
    }
    for im in &program.impls {
        if im.module == module {
            for m in &im.methods {
                walk_expr(program, &module, &m.body, range, &mut out);
            }
        }
    }
    for en in &program.enums {
        if en.module == module {
            for m in &en.methods {
                walk_expr(program, &module, &m.body, range, &mut out);
            }
        }
    }
    out.sort_by_key(|h| h.offset);
    out
}

fn walk_expr(
    program: &Program,
    module: &str,
    e: &Expr,
    range: Option<(usize, usize)>,
    out: &mut Vec<InlayHint>,
) {
    match &e.kind {
        ExprKind::Call { callee, args } => {
            let (qual, name) = split_callee(callee);
            if let Some(names) = names_for(program, module, qual, name) {
                let refs: Vec<&Expr> = args.iter().collect();
                hint_args(&names, &refs, range, out);
            }
        }
        ExprKind::IoPrintln(a) => hint_kit("IO.println", &[a.as_ref()], range, out),
        ExprKind::IoSleep(a) => hint_kit("IO.sleep", &[a.as_ref()], range, out),
        ExprKind::IoFail(a) => hint_kit("IO.fail", &[a.as_ref()], range, out),
        ExprKind::IoPure(a) => hint_kit("IO.pure", &[a.as_ref()], range, out),
        ExprKind::IoRace { left, right } => {
            hint_kit("IO.race", &[left.as_ref(), right.as_ref()], range, out);
        }
        ExprKind::IoBoth { left, right } => {
            hint_kit("IO.both", &[left.as_ref(), right.as_ref()], range, out);
        }
        ExprKind::IoEnsure { inner, finalizer } => {
            hint_kit(
                "IO.ensure",
                &[inner.as_ref(), finalizer.as_ref()],
                range,
                out,
            );
        }
        ExprKind::IoTimeout { ms, inner } => {
            hint_kit("IO.timeout", &[ms.as_ref(), inner.as_ref()], range, out);
        }
        ExprKind::MethodCall { method, args, .. } => {
            if let Some(names) = method_param_names(program, method) {
                let refs: Vec<&Expr> = args.iter().collect();
                hint_args(&names, &refs, range, out);
            }
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            ..
        } => {
            if let Some(names) = adt_field_names(program, enum_name, case_name) {
                let refs: Vec<&Expr> = args.iter().collect();
                hint_args(&names, &refs, range, out);
            }
        }
        _ => {}
    }
    e.for_each_child(|c| walk_expr(program, module, c, range, out));
}

fn hint_kit(callee: &str, args: &[&Expr], range: Option<(usize, usize)>, out: &mut Vec<InlayHint>) {
    if let Some(sig) = kit_sig(callee) {
        let names = param_names_from_label(sig);
        hint_args(&names, args, range, out);
    }
}

fn hint_args(
    names: &[String],
    args: &[&Expr],
    range: Option<(usize, usize)>,
    out: &mut Vec<InlayHint>,
) {
    for (name, arg) in names.iter().zip(args.iter()) {
        if skip_arg(name, arg) {
            continue;
        }
        if let Some((start, end)) = range {
            if arg.span.start < start || arg.span.start > end {
                continue;
            }
        }
        out.push(InlayHint {
            offset: arg.span.start,
            label: format!("{name}:"),
            kind: HINT_PARAMETER,
        });
    }
}

fn skip_arg(name: &str, arg: &Expr) -> bool {
    matches!(&arg.kind, ExprKind::Var(v) if v == name)
}

fn split_callee(callee: &str) -> (Option<&str>, &str) {
    match callee.rsplit_once('.') {
        Some((q, n)) => (Some(q), n),
        None => (None, callee),
    }
}

fn names_for(
    program: &Program,
    module: &str,
    qual: Option<&str>,
    name: &str,
) -> Option<Vec<String>> {
    if let Some(label) = sig_label(program, module, qual, name) {
        return Some(param_names_from_label(&label));
    }
    if qual.is_none() {
        return unique_construct_fields(program, name);
    }
    None
}

fn unique_construct_fields(program: &Program, name: &str) -> Option<Vec<String>> {
    let mut hits: Vec<Vec<String>> = Vec::new();
    for en in &program.enums {
        for c in &en.cases {
            if c.name == name && !c.fields.is_empty() {
                hits.push(c.fields.iter().map(|(n, _)| n.clone()).collect());
            }
        }
    }
    hits.sort();
    hits.dedup();
    if hits.len() == 1 {
        Some(hits.remove(0))
    } else {
        None
    }
}

fn method_param_names(program: &Program, name: &str) -> Option<Vec<String>> {
    let mut hits: Vec<Vec<String>> = Vec::new();
    for t in &program.traits {
        for m in &t.methods {
            if m.name == name {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    for im in &program.impls {
        for m in &im.methods {
            if m.name == name {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    for en in &program.enums {
        for m in &en.methods {
            if m.name == name {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    hits.sort();
    hits.dedup();
    if hits.len() == 1 {
        Some(hits.remove(0))
    } else {
        None
    }
}

fn adt_field_names(program: &Program, enum_name: &str, case_name: &str) -> Option<Vec<String>> {
    let en = program.enums.iter().find(|e| e.name == enum_name)?;
    let case = en.cases.iter().find(|c| c.name == case_name)?;
    if case.fields.is_empty() {
        return None;
    }
    Some(case.fields.iter().map(|(n, _)| n.clone()).collect())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn hints_kit_and_user_def_args() {
        let src = r#"def add(n: Int, m: Int): Int = n
@main def main: IO[Unit] = IO.println(Str.fromInt(add(1, 2)))
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let hints = inlay_hints_in_source(&p, "Main.scuzz", None);
        let labels: Vec<_> = hints.iter().map(|h| h.label.as_str()).collect();
        assert!(labels.contains(&"n:"), "{hints:?}");
        assert!(labels.contains(&"m:"), "{hints:?}");
        assert!(labels.contains(&"s:"), "{hints:?}");
        assert!(labels.contains(&"n:"), "{hints:?}");
        let add_one = src.find("add(1").unwrap() + 4;
        assert!(
            hints.iter().any(|h| h.offset == add_one && h.label == "n:"),
            "{hints:?}"
        );
        assert_eq!(
            hints.iter().find(|h| h.offset == add_one).unwrap().kind,
            HINT_PARAMETER
        );
    }

    #[test]
    fn skips_same_named_var_and_if() {
        let src = r#"def add(n: Int): Int = n
@main def main: IO[Unit] =
  if (true) IO.println(Str.fromInt(add(n))) else IO.println("y")
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let hints = inlay_hints_in_source(&p, "Main.scuzz", None);
        let add_call = src.find("add(n)").unwrap() + 4;
        assert!(
            !hints.iter().any(|h| h.offset == add_call),
            "same-named arg still hinted: {hints:?}"
        );
        let cond = src.find("(true)").unwrap() + 1;
        assert!(
            !hints.iter().any(|h| h.offset == cond),
            "if cond hinted: {hints:?}"
        );
    }

    #[test]
    fn hints_record_fields() {
        let src = r#"record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(1, 2).x))
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let hints = inlay_hints_in_source(&p, "Main.scuzz", None);
        assert!(hints.iter().any(|h| h.label == "x:"), "{hints:?}");
        assert!(hints.iter().any(|h| h.label == "y:"), "{hints:?}");
    }
}

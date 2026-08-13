//! Stem-paired `*.scuzz_sim` / `*.scuzz_drivers` overlays and in-source `law` residualization.

use crate::ast::{Expr, ExprKind, FunDef, MainDef, Program, Type};
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
/// [`residualize_laws`] under verify / check.
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

fn expr_has_law(e: &Expr) -> bool {
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
        ExprKind::MethodCall { receiver, args, .. } => {
            expr_has_law(receiver) || args.iter().any(expr_has_law)
        }
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

/// Wrap `@main` so each law runs after the program under TestRuntime (`Law.assert`).
pub fn residualize_laws(program: &mut Program, law_names: &[String]) {
    if law_names.is_empty() {
        return;
    }
    let mut body = program.main.body.clone();
    for name in law_names {
        let assert_call = Expr::dummy(ExprKind::Call {
            callee: "Law.assert".into(),
            args: vec![
                Expr::dummy(ExprKind::StrLit(name.clone())),
                Expr::dummy(ExprKind::Call {
                    callee: name.clone(),
                    args: vec![],
                }),
            ],
        });
        body = Expr::dummy(ExprKind::FlatMap {
            inner: Box::new(body),
            param: Some("_".into()),
            body: Box::new(assert_call),
        });
    }
    program.main = MainDef {
        module: program.main.module.clone(),
        name: program.main.name.clone(),
        body,
    };
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
}

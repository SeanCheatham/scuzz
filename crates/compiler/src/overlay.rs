//! Stem-paired `*.scuzz_sim` / `*.scuzz_laws` overlays for check / fuzz / TestRuntime.

use crate::ast::{Expr, FunDef, MainDef, Program, Type};
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
    Laws,
}

#[derive(Debug, Clone)]
pub struct OverlaySource {
    /// File stem shared with the live `*.scuzz` twin (`Shared` for `Shared.scuzz_sim`).
    pub stem: String,
    pub kind: OverlayKind,
    pub label: String,
    pub text: String,
}

/// Apply same-name sim replacements and attach pure laws. Returns the verify program
/// (defs include laws) plus law names for residual emission under TestRuntime.
pub fn apply_overlays(
    mut live: Program,
    overlays: &[OverlaySource],
) -> Result<(Program, Vec<String>), OverlayError> {
    let sims: Vec<&OverlaySource> = overlays
        .iter()
        .filter(|o| o.kind == OverlayKind::Sim)
        .collect();
    let laws: Vec<&OverlaySource> = overlays
        .iter()
        .filter(|o| o.kind == OverlayKind::Laws)
        .collect();

    for sim in &sims {
        let prog = parse(&sim.text).map_err(|e| OverlayError::Msg(format!("{}: {e}", sim.label)))?;
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
            replace_sim_def(&mut live, &d, &sim.label)?;
        }
    }

    let mut law_names = Vec::new();
    for law in &laws {
        let prog =
            parse(&law.text).map_err(|e| OverlayError::Msg(format!("{}: {e}", law.label)))?;
        if !prog.main.name.is_empty() {
            return Err(OverlayError::Msg(format!(
                "{}: *.scuzz_laws must not define @main",
                law.label
            )));
        }
        if !prog.enums.is_empty() {
            return Err(OverlayError::Msg(format!(
                "{}: *.scuzz_laws must not define enums",
                law.label
            )));
        }
        for d in prog.defs {
            if live.defs.iter().any(|x| x.name == d.name) {
                return Err(OverlayError::Msg(format!(
                    "{}: law `{}` collides with a live/sim def",
                    law.label, d.name
                )));
            }
            if !matches!(d.ret, Type::Bool | Type::Int) {
                return Err(OverlayError::Msg(format!(
                    "{}: law `{}` must return Bool (or Int), got {:?}",
                    law.label, d.name, d.ret
                )));
            }
            if !d.params.is_empty() {
                return Err(OverlayError::Msg(format!(
                    "{}: law `{}` must be nullary for residual checks",
                    law.label, d.name
                )));
            }
            law_names.push(d.name.clone());
            live.defs.push(d);
        }
    }

    Ok((live, law_names))
}

fn replace_sim_def(live: &mut Program, sim: &FunDef, label: &str) -> Result<(), OverlayError> {
    let Some(idx) = live.defs.iter().position(|d| d.name == sim.name) else {
        return Err(OverlayError::Msg(format!(
            "{label}: sim def `{}` has no live twin",
            sim.name
        )));
    };
    let live_def = &live.defs[idx];
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

/// Wrap `@main` so each law runs after the program under TestRuntime (`Law.assert`).
pub fn residualize_laws(program: &mut Program, law_names: &[String]) {
    if law_names.is_empty() {
        return;
    }
    let mut body = program.main.body.clone();
    for name in law_names {
        let assert_call = Expr::Call {
            callee: "Law.assert".into(),
            args: vec![
                Expr::StrLit(name.clone()),
                Expr::Call {
                    callee: name.clone(),
                    args: vec![],
                },
            ],
        };
        body = Expr::FlatMap {
            inner: Box::new(body),
            param: Some("_".into()),
            body: Box::new(assert_call),
        };
    }
    program.main = MainDef {
        name: program.main.name.clone(),
        body,
    };
}

/// True when `path` is a stem-paired overlay (`*.scuzz_sim` / `*.scuzz_laws`).
pub fn overlay_kind_from_path(path: &std::path::Path) -> Option<(String, OverlayKind)> {
    let name = path.file_name()?.to_str()?;
    if let Some(stem) = name.strip_suffix(".scuzz_sim") {
        if !stem.is_empty() {
            return Some((stem.to_string(), OverlayKind::Sim));
        }
    }
    if let Some(stem) = name.strip_suffix(".scuzz_laws") {
        if !stem.is_empty() {
            return Some((stem.to_string(), OverlayKind::Laws));
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_sources;

    #[test]
    fn sim_replaces_and_laws_attach() {
        let live = parse_sources(&[(
            "Main.scuzz".into(),
            "def title(): String = \"Live\"\n@main def main: IO[Unit] = IO.println(title())\n"
                .into(),
        )])
        .unwrap();
        let overlays = vec![
            OverlaySource {
                stem: "Main".into(),
                kind: OverlayKind::Sim,
                label: "Main.scuzz_sim".into(),
                text: "def title(): String = \"Sim\"\n".into(),
            },
            OverlaySource {
                stem: "Main".into(),
                kind: OverlayKind::Laws,
                label: "Main.scuzz_laws".into(),
                text: "def always(): Bool = 1 == 1\n".into(),
            },
        ];
        let (prog, laws) = apply_overlays(live, &overlays).unwrap();
        assert_eq!(laws, vec!["always".to_string()]);
        let title = prog.defs.iter().find(|d| d.name == "title").unwrap();
        match &title.body {
            crate::ast::Expr::StrLit(s) => assert_eq!(s, "Sim"),
            other => panic!("expected sim body, got {other:?}"),
        }
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
}

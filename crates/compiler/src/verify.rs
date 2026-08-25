//! `*.scuzz_verify` files. Author claims of shape `Timeline => Bool`, or
//! generator-friendly `Bool` drive oracles. Files may sit anywhere in the
//! package. They do not pair with live sources.

use crate::ast::{Expr, ExprKind, FunDef, Program, Type};
use crate::overlay::{expr_has_property, type_is_generator_friendly, OverlayError};
use crate::parser::parse_file;
use crate::resolve::module_id_from_label;
use crate::span::Span;
use std::path::{Path, PathBuf};

/// Compiler module for synthetic drive wrappers around verify oracles.
pub const VERIFY_DRIVE_MODULE: &str = "__verify";

/// One discovered verify file.
#[derive(Debug, Clone)]
pub struct VerifySource {
    pub label: String,
    pub text: String,
    pub path: PathBuf,
}

/// Directories that are not package sources.
fn skip_dir_name(name: &str) -> bool {
    matches!(
        name,
        "build" | ".scuzz" | "goldens" | "corpus" | ".git" | "target" | "third_party"
    )
}

/// True when `path` is a verify file.
pub fn is_verify_path(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| e == "scuzz_verify")
}

/// Walk `pkg_dir` for `*.scuzz_verify` files.
pub fn collect_verify_paths(pkg_dir: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    collect_verify_into(pkg_dir, &mut out)?;
    out.sort();
    Ok(out)
}

fn collect_verify_into(dir: &Path, out: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if !dir.is_dir() {
        return Ok(());
    }
    let mut entries: Vec<_> = std::fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|e| e.path());
    for entry in entries {
        let path = entry.path();
        if path.is_dir() {
            let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
            if skip_dir_name(name) {
                continue;
            }
            collect_verify_into(&path, out)?;
            continue;
        }
        if is_verify_path(&path) {
            out.push(path);
        }
    }
    Ok(())
}

/// Fail when a leftover `*.scuzz_intent` file is present.
pub fn reject_intent_files(pkg_dir: &Path, pkg_name: &str) -> Result<(), String> {
    let mut paths = Vec::new();
    collect_intent_into(pkg_dir, &mut paths).map_err(|e| e.to_string())?;
    paths.sort();
    if let Some(path) = paths.first() {
        let rel = path
            .strip_prefix(pkg_dir)
            .unwrap_or(path)
            .to_string_lossy()
            .replace('\\', "/");
        return Err(format!(
            "{pkg_name}/{rel}: intent files are not valid; write the claim in a *.scuzz_verify file"
        ));
    }
    Ok(())
}

fn collect_intent_into(dir: &Path, out: &mut Vec<PathBuf>) -> std::io::Result<()> {
    if !dir.is_dir() {
        return Ok(());
    }
    let mut entries: Vec<_> = std::fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|e| e.path());
    for entry in entries {
        let path = entry.path();
        if path.is_dir() {
            let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
            if skip_dir_name(name) {
                continue;
            }
            collect_intent_into(&path, out)?;
            continue;
        }
        if path
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| e == "scuzz_intent")
        {
            out.push(path);
        }
    }
    Ok(())
}

/// `drive <name>` lines for `build/seeds.txt`. Empty when no zero-argument oracles.
pub fn seed_table_text(program: &Program) -> String {
    let mut out = String::new();
    for line in &program.verify_seeds {
        out.push_str(line);
        out.push('\n');
    }
    out
}

/// Parse and merge verify predicates onto `program`.
pub fn apply_verifies(program: &mut Program, files: &[VerifySource]) -> Result<(), OverlayError> {
    let mut names = Vec::new();
    let mut timeline = Vec::new();
    let mut drives = Vec::new();
    for ov in files {
        apply_one(program, ov, &mut names, &mut timeline, &mut drives)?;
    }
    let mut seeds = Vec::new();
    for name in &drives {
        let d = program
            .defs
            .iter()
            .find(|d| d.is_verify && d.name == *name)
            .expect("drive oracle def")
            .clone();
        if d.params.is_empty() {
            seeds.push(format!("drive {}", d.name));
        }
        program.defs.push(wrap_drive_oracle(&d));
    }
    seeds.sort();
    seeds.dedup();
    program.verify_preds = timeline;
    program.verify_seeds = seeds;
    Ok(())
}

fn apply_one(
    program: &mut Program,
    ov: &VerifySource,
    names: &mut Vec<String>,
    timeline: &mut Vec<String>,
    drives: &mut Vec<String>,
) -> Result<(), OverlayError> {
    let prog = parse_file(&ov.text, &ov.label)?;
    reject_verify_header(&prog, &ov.label)?;
    let module = module_id_from_label(&ov.label);
    let mut got = 0usize;
    for mut d in prog.defs {
        d.module = module.clone();
        check_verify_def(program, &d, &ov.label)?;
        if d.is_private {
            d.is_verify = true;
            program.defs.push(d);
            continue;
        }
        d.is_verify = true;
        if names.iter().any(|n| n == &d.name) {
            return Err(OverlayError::Msg(format!(
                "{}: duplicate verify predicate `{}`",
                ov.label, d.name
            )));
        }
        names.push(d.name.clone());
        if is_timeline_pred(&d) {
            timeline.push(d.name.clone());
        } else {
            drives.push(d.name.clone());
        }
        program.defs.push(d);
        got += 1;
    }
    if got == 0 {
        return Err(at(
            Span::new(&ov.label, 0, 0),
            format!(
                "{}: verify file needs a Timeline => Bool or a drive oracle; write a claim or delete the file",
                ov.label
            ),
        ));
    }
    Ok(())
}

fn reject_verify_header(prog: &Program, label: &str) -> Result<(), OverlayError> {
    if !prog.main.name.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{label}: *.scuzz_verify must not define @main"
        )));
    }
    if !prog.enums.is_empty()
        || !prog.aliases.is_empty()
        || !prog.traits.is_empty()
        || !prog.impls.is_empty()
        || !prog.imports.is_empty()
    {
        return Err(OverlayError::Msg(format!(
            "{label}: *.scuzz_verify must only define Timeline => Bool predicates or drive oracles"
        )));
    }
    Ok(())
}

fn check_verify_def(live: &Program, d: &FunDef, label: &str) -> Result<(), OverlayError> {
    if live
        .defs
        .iter()
        .any(|l| l.module == d.module && l.name == d.name)
    {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` collides with a live def",
            d.name
        )));
    }
    if d.is_private {
        if expr_has_property(&d.body) {
            return Err(OverlayError::Msg(format!(
                "{label}: verify def `{}` must not call Property.* or `.require`",
                d.name
            )));
        }
        return Ok(());
    }
    if !matches!(d.ret, Type::Bool) {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` must return Bool",
            d.name
        )));
    }
    if !d.type_params.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` must not take type parameters",
            d.name
        )));
    }
    if expr_has_property(&d.body) {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` must not call Property.* or `.require`",
            d.name
        )));
    }
    if is_timeline_pred(d) {
        return Ok(());
    }
    if d.params.len() > 3 {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` takes at most three generator-friendly params",
            d.name
        )));
    }
    if d.params.iter().any(|p| is_timeline_ty(&p.ty)) {
        return Err(OverlayError::Msg(format!(
            "{label}: verify def `{}` must take one Timeline parameter",
            d.name
        )));
    }
    for p in &d.params {
        if !type_is_generator_friendly(&p.ty, live, &d.module, 0) {
            return Err(OverlayError::Msg(format!(
                "{label}: verify def `{}` param `{}` must be Int, String, Bool, List, or a record/enum of those",
                d.name, p.name
            )));
        }
    }
    Ok(())
}

fn is_timeline_pred(d: &FunDef) -> bool {
    d.params.len() == 1 && is_timeline_ty(&d.params[0].ty) && matches!(d.ret, Type::Bool)
}

fn is_timeline_ty(ty: &Type) -> bool {
    matches!(ty, Type::Opaque(n) if n == "Timeline")
}

fn wrap_drive_oracle(d: &FunDef) -> FunDef {
    let span = d.name_span.clone();
    let call = Expr::new(
        ExprKind::Call {
            callee: format!("{}.{}", d.module, d.name),
            args: d
                .params
                .iter()
                .map(|p| Expr::new(ExprKind::Var(p.name.clone()), span.clone()))
                .collect(),
        },
        span.clone(),
    );
    let body = Expr::new(
        ExprKind::Call {
            callee: "Property.assert".into(),
            args: vec![
                Expr::new(ExprKind::StrLit(d.name.clone()), span.clone()),
                call,
            ],
        },
        span.clone(),
    );
    FunDef {
        module: VERIFY_DRIVE_MODULE.into(),
        name: d.name.clone(),
        name_span: span,
        is_private: false,
        is_driver: true,
        is_verify: false,
        type_params: Vec::new(),
        params: d.params.clone(),
        ret: Type::Io(Box::new(Type::Unit)),
        body,
    }
}

fn at(span: Span, msg: String) -> OverlayError {
    OverlayError::At { msg, span }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    fn src(text: &str) -> VerifySource {
        VerifySource {
            label: "pkg/count.scuzz_verify".into(),
            text: text.into(),
            path: PathBuf::new(),
        }
    }

    #[test]
    fn apply_timeline_pred() {
        let mut live = parse("@main def main: IO[Unit] =\n  IO.println(\"x\")\n").unwrap();
        apply_verifies(
            &mut live,
            &[src(
                "def countOk(t: Timeline): Bool =\n  Timeline.forall(t, i => true)\n",
            )],
        )
        .unwrap();
        assert_eq!(live.verify_preds, vec!["countOk".to_string()]);
        let d = live.defs.iter().find(|d| d.name == "countOk").unwrap();
        assert!(d.is_verify);
        assert_eq!(d.module, "count");
    }

    #[test]
    fn apply_drive_oracle() {
        let mut live = parse(
            "def bump(n: Int): Int = n + 1\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n",
        )
        .unwrap();
        apply_verifies(
            &mut live,
            &[src("def bump(n: Int): Bool =\n  Main.bump(n) == n + 1\n")],
        )
        .unwrap();
        assert!(live.verify_preds.is_empty());
        assert!(live.verify_seeds.is_empty());
        let d = live
            .defs
            .iter()
            .find(|d| d.is_driver && d.name == "bump")
            .unwrap();
        assert_eq!(d.module, VERIFY_DRIVE_MODULE);
        assert!(matches!(d.ret, Type::Io(_)));
    }

    #[test]
    fn zero_arg_oracle_writes_seed() {
        let mut live =
            parse("def add(n: Int, m: Int): Int = n + m\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n")
                .unwrap();
        apply_verifies(
            &mut live,
            &[src("def addTwoThree(): Bool =\n  Main.add(2, 3) == 5\n")],
        )
        .unwrap();
        assert_eq!(seed_table_text(&live).trim(), "drive addTwoThree");
    }

    #[test]
    fn reject_empty_verify_file() {
        let mut live = parse("@main def main: IO[Unit] =\n  IO.println(\"x\")\n").unwrap();
        let err = apply_verifies(&mut live, &[src("\n")]).unwrap_err();
        assert!(err.to_string().contains("drive oracle"), "{err}");
    }

    #[test]
    fn reject_float_drive_param() {
        let mut live = parse(
            "def bad(x: Float): Float = x\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n",
        )
        .unwrap();
        let err = apply_verifies(
            &mut live,
            &[src("def bad(x: Float): Bool =\n  Main.bad(x) == x\n")],
        )
        .unwrap_err();
        assert!(err.to_string().contains("record/enum"), "{err}");
    }
}

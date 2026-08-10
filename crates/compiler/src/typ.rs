use crate::ast::{BinOp, EnumDef, Expr, FunDef, Program, Type};
use std::collections::HashMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TypeError {
    #[error("type error: {0}")]
    Msg(String),
}

/// Structural check for the kernel dialect: @main is IO[Unit]; defs/calls resolve.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    let enums: HashMap<&str, &EnumDef> = program
        .enums
        .iter()
        .map(|e| (e.name.as_str(), e))
        .collect();
    let mut funs: HashMap<String, &FunDef> = HashMap::new();
    for d in &program.defs {
        if funs.insert(d.name.clone(), d).is_some() {
            return Err(TypeError::Msg(format!("duplicate def {}", d.name)));
        }
    }
    for d in &program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(p.name.clone(), p.ty.clone());
        }
        let body_ty = infer(&d.body, &enums, &funs, &mut env)?;
        if !types_compat(&body_ty, &d.ret) {
            return Err(TypeError::Msg(format!(
                "def {} body {:?} does not match declared {:?}",
                d.name, body_ty, d.ret
            )));
        }
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    let ty = infer(&program.main.body, &enums, &funs, &mut env)?;
    match ty {
        Type::Io(inner) if matches!(*inner, Type::Unit) => Ok(()),
        other => Err(TypeError::Msg(format!(
            "@main body must be IO[Unit], got {other:?}"
        ))),
    }
}

fn infer(
    expr: &Expr,
    enums: &HashMap<&str, &EnumDef>,
    funs: &HashMap<String, &FunDef>,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    match expr {
        Expr::Unit => Ok(Type::Unit),
        Expr::IntLit(_) => Ok(Type::Int),
        Expr::StrLit(_) => Ok(Type::String),
        Expr::ListLit { elems } => {
            for e in elems {
                infer(e, enums, funs, env)?;
            }
            Ok(Type::List)
        }
        Expr::Interpolate { parts } => {
            for part in parts {
                match part {
                    crate::ast::InterpPart::Lit(_) => {}
                    crate::ast::InterpPart::Expr(e) => {
                        let t = infer(e, enums, funs, env)?;
                        if !matches!(t, Type::String | Type::Int | Type::Opaque(_)) {
                            return Err(TypeError::Msg(format!(
                                "interpolation hole must be String or Int, got {t:?}"
                            )));
                        }
                    }
                }
            }
            Ok(Type::String)
        },
        Expr::IoPrintln(e) | Expr::IoFail(e) => {
            let t = infer(e, enums, funs, env)?;
            expect_ty(&t, &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        Expr::IoSleep(e) => {
            let t = infer(e, enums, funs, env)?;
            expect_ty(&t, &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        Expr::IoDelayUnit | Expr::EffectsRunKit => Ok(Type::Io(Box::new(Type::Unit))),
        Expr::IoPure(inner) => {
            let t = infer(inner, enums, funs, env)?;
            Ok(Type::Io(Box::new(t)))
        }
        Expr::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| TypeError::Msg(format!("unbound variable {name}"))),
        Expr::AdtConstruct {
            enum_name,
            case_name,
        } => {
            let en = enums.get(enum_name.as_str()).ok_or_else(|| {
                TypeError::Msg(format!("unknown enum {enum_name}"))
            })?;
            if !en.cases.iter().any(|c| c == case_name) {
                return Err(TypeError::Msg(format!(
                    "unknown case {enum_name}.{case_name}"
                )));
            }
            Ok(Type::Adt(enum_name.clone()))
        }
        Expr::Let { name, value, body } => {
            let vt = infer(value, enums, funs, env)?;
            let old = env.insert(name.clone(), vt);
            let bt = infer(body, enums, funs, env)?;
            if let Some(v) = old {
                env.insert(name.clone(), v);
            } else {
                env.remove(name);
            }
            Ok(bt)
        }
        Expr::If {
            cond,
            then_branch,
            else_branch,
        } => {
            let ct = infer(cond, enums, funs, env)?;
            if !matches!(ct, Type::Int | Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "if condition must be Int/Bool, got {ct:?}"
                )));
            }
            let tt = infer(then_branch, enums, funs, env)?;
            let et = infer(else_branch, enums, funs, env)?;
            if !types_compat(&tt, &et) {
                return Err(TypeError::Msg(format!(
                    "if branches disagree: {tt:?} vs {et:?}"
                )));
            }
            Ok(tt)
        }
        Expr::Binary { op, left, right } => {
            let lt = infer(left, enums, funs, env)?;
            let rt = infer(right, enums, funs, env)?;
            match op {
                BinOp::Add if matches!(lt, Type::String) && matches!(rt, Type::String) => {
                    Ok(Type::String)
                }
                BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                    if matches!(lt, Type::Int) && matches!(rt, Type::Int) {
                        Ok(Type::Int)
                    } else {
                        Err(TypeError::Msg(format!(
                            "arithmetic needs Int, got {lt:?} and {rt:?}"
                        )))
                    }
                }
                BinOp::Eq | BinOp::Ne => {
                    if types_compat(&lt, &rt)
                        || (matches!(lt, Type::String) && matches!(rt, Type::String))
                    {
                        Ok(Type::Int)
                    } else {
                        Err(TypeError::Msg(format!(
                            "comparison type mismatch {lt:?} vs {rt:?}"
                        )))
                    }
                }
                BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                    if matches!(lt, Type::Int) && matches!(rt, Type::Int) {
                        Ok(Type::Int)
                    } else {
                        Err(TypeError::Msg("ordered compare needs Int".into()))
                    }
                }
                BinOp::And | BinOp::Or => {
                    if matches!(lt, Type::Int | Type::Bool) && matches!(rt, Type::Int | Type::Bool)
                    {
                        Ok(Type::Int)
                    } else {
                        Err(TypeError::Msg("&&/|| need Int/Bool".into()))
                    }
                }
            }
        }
        Expr::Call { callee, args } => infer_call(callee, args, enums, funs, env),
        Expr::Match { scrutinee, arms } => {
            let st = infer(scrutinee, enums, funs, env)?;
            let mut result: Option<Type> = None;
            for arm in arms {
                check_pattern(&arm.pattern, &st, enums)?;
                let bt = infer(&arm.body, enums, funs, env)?;
                match &result {
                    None => result = Some(bt),
                    Some(prev) if types_compat(prev, &bt) => {}
                    Some(prev) => {
                        return Err(TypeError::Msg(format!(
                            "match arms disagree: {prev:?} vs {bt:?}"
                        )))
                    }
                }
            }
            result.ok_or_else(|| TypeError::Msg("empty match".into()))
        }
        Expr::FlatMap { inner, param, body } => {
            let it = infer(inner, enums, funs, env)?;
            let Type::Io(inner_t) = it else {
                return Err(TypeError::Msg("flatMap receiver must be IO[_]".into()));
            };
            let old = if let Some(p) = param {
                env.insert(p.clone(), (*inner_t).clone())
            } else {
                None
            };
            let bt = infer(body, enums, funs, env)?;
            if let Some(p) = param {
                if let Some(v) = old {
                    env.insert(p.clone(), v);
                } else {
                    env.remove(p);
                }
            }
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "flatMap body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        Expr::HandleErrorWith { inner, body } => {
            let it = infer(inner, enums, funs, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body, enums, funs, env)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        Expr::Attempt { inner } => {
            let it = infer(inner, enums, funs, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg("attempt receiver must be IO[_]".into()));
            }
            Ok(Type::Io(Box::new(Type::Opaque("Either".into()))))
        }
        Expr::Lambda { param, body } => {
            // Param type is context-dependent (View for taps, Int for Signal.map).
            // Bind as Opaque so both map and tap lambdas typecheck.
            let old = param.as_ref().map(|p| {
                (
                    p.clone(),
                    env.insert(p.clone(), Type::Opaque("Param".into())),
                )
            });
            let _ = infer(body, enums, funs, env)?;
            if let Some((p, old_val)) = old {
                match old_val {
                    Some(v) => {
                        env.insert(p, v);
                    }
                    None => {
                        env.remove(&p);
                    }
                }
            }
            Ok(Type::Opaque("TapFn".into()))
        }
        Expr::IoRace { left, right } | Expr::IoBoth { left, right } => {
            let lt = infer(left, enums, funs, env)?;
            let rt = infer(right, enums, funs, env)?;
            if !matches!(lt, Type::Io(_)) || !matches!(rt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "IO.race/both arguments must be IO[_]".into(),
                ));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        Expr::For { .. } => Err(TypeError::Msg(
            "internal: unlowered `for` (run lower before typecheck)".into(),
        )),
    }
}

fn infer_call(
    callee: &str,
    args: &[Expr],
    enums: &HashMap<&str, &EnumDef>,
    funs: &HashMap<String, &FunDef>,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let mut arg_tys = Vec::new();
    for a in args {
        arg_tys.push(infer(a, enums, funs, env)?);
    }
    match callee {
        "Str.concat" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "Str.len" | "Str.charAt" | "Str.indexOf" => {
            if callee == "Str.len" {
                expect_arity(callee, &arg_tys, 1)?;
                expect_ty(&arg_tys[0], &Type::String)?;
            } else {
                expect_arity(callee, &arg_tys, 2)?;
                expect_ty(&arg_tys[0], &Type::String)?;
                if callee == "Str.charAt" {
                    expect_ty(&arg_tys[1], &Type::Int)?;
                } else {
                    expect_ty(&arg_tys[1], &Type::String)?;
                }
            }
            Ok(Type::Int)
        }
        "Str.slice" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            Ok(Type::String)
        }
        "Str.eq" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Int)
        }
        "Str.fromInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::String)
        }
        "Str.lines" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::List)
        }
        "List.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::List)
        }
        "List.cons" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::List)?;
            Ok(Type::List)
        }
        "List.isEmpty" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::Int)
        }
        "List.head" | "List.at" => {
            if callee == "List.head" {
                expect_arity(callee, &arg_tys, 1)?;
            } else {
                expect_arity(callee, &arg_tys, 2)?;
                expect_ty(&arg_tys[1], &Type::Int)?;
            }
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::Opaque("Any".into()))
        }
        "List.tail" | "List.reverse" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::List)
        }
        "List.len" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::Int)
        }
        "List.join" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "List.append" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::List)
        }
        "Fs.read" | "Fs.list" | "Fs.mkdirs" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(match callee {
                "Fs.read" => Type::Io(Box::new(Type::String)),
                "Fs.list" => Type::Io(Box::new(Type::List)),
                _ => Type::Io(Box::new(Type::Unit)),
            })
        }
        "Fs.write" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Sys.args" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::List)))
        }
        "Sys.readLine" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Sys.exec" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Sys.getenv" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Clock.realTime" | "Clock.monotonic" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Random.nextInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Net.httpGet" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Impurity.runKit" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Signal.int" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Opaque("SignalInt".into()))
        }
        "Signal.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Int)
        }
        "Signal.set" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Unit)
        }
        "Signal.str" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("SignalStr".into()))
        }
        "Signal.getStr" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::String)
        }
        "Signal.setStr" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Unit)
        }
        "Signal.list" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::Opaque("SignalList".into()))
        }
        "Signal.getList" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::List)
        }
        "Signal.setList" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::List)?;
            Ok(Type::Unit)
        }
        "Signal.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("SignalStr".into()))
        }
        "View.text" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.bindText" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.button" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "Theme.accent" | "Theme.primary" | "Theme.muted" | "Theme.foreground" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Int)
        }
        "Color.rgb" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            Ok(Type::Int)
        }
        "View.column" | "View.row" => {
            // Nullary or children: `View.column(a, b, …)` adds each child.
            Ok(Type::Opaque("View".into()))
        }
        "View.list" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.each" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.scroll" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.textField" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.icon" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.image" => {
            expect_arity(callee, &arg_tys, 4)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            expect_ty(&arg_tys[3], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.addChild" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Unit)
        }
        "View.addTexts" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::List)?;
            Ok(Type::Unit)
        }
        "View.showWhen" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "Ui.run" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        _ => {
            let f = funs
                .get(callee)
                .ok_or_else(|| TypeError::Msg(format!("unknown function {callee}")))?;
            if f.params.len() != arg_tys.len() {
                return Err(TypeError::Msg(format!(
                    "{callee} expects {} args, got {}",
                    f.params.len(),
                    arg_tys.len()
                )));
            }
            for (p, a) in f.params.iter().zip(arg_tys.iter()) {
                if !types_compat(a, &p.ty) {
                    return Err(TypeError::Msg(format!(
                        "{callee} arg type mismatch: expected {:?}, got {:?}",
                        p.ty, a
                    )));
                }
            }
            Ok(f.ret.clone())
        }
    }
}

fn expect_arity(name: &str, args: &[Type], n: usize) -> Result<(), TypeError> {
    if args.len() != n {
        Err(TypeError::Msg(format!(
            "{name} expects {n} args, got {}",
            args.len()
        )))
    } else {
        Ok(())
    }
}

fn expect_ty(got: &Type, want: &Type) -> Result<(), TypeError> {
    if types_compat(got, want) {
        Ok(())
    } else {
        Err(TypeError::Msg(format!("expected {want:?}, got {got:?}")))
    }
}

fn check_pattern(
    pat: &crate::ast::Pattern,
    scrut: &Type,
    enums: &HashMap<&str, &EnumDef>,
) -> Result<(), TypeError> {
    match pat {
        crate::ast::Pattern::Wildcard => Ok(()),
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
        } => {
            let en = enums.get(enum_name.as_str()).ok_or_else(|| {
                TypeError::Msg(format!("unknown enum {enum_name} in pattern"))
            })?;
            if !en.cases.iter().any(|c| c == case_name) {
                return Err(TypeError::Msg(format!(
                    "unknown case {enum_name}.{case_name} in pattern"
                )));
            }
            match scrut {
                Type::Adt(n) if n == enum_name => Ok(()),
                Type::Opaque(_) => Ok(()),
                other => Err(TypeError::Msg(format!(
                    "pattern {enum_name}.{case_name} does not match scrutinee {other:?}"
                ))),
            }
        }
    }
}

fn types_compat(a: &Type, b: &Type) -> bool {
    match (a, b) {
        (Type::Unit, Type::Unit) => true,
        (Type::Int, Type::Int) => true,
        (Type::Bool, Type::Bool) => true,
        (Type::Bool, Type::Int) | (Type::Int, Type::Bool) => true,
        (Type::String, Type::String) => true,
        (Type::List, Type::List) => true,
        (Type::Io(_), Type::Io(_)) => true,
        (Type::Adt(x), Type::Adt(y)) => x == y,
        (Type::Opaque(_), _) | (_, Type::Opaque(_)) => true,
        _ => false,
    }
}

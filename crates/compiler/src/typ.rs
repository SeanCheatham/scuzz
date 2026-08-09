use crate::ast::{EnumDef, Expr, Pattern, Program, Type};
use std::collections::HashMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TypeError {
    #[error("type error: {0}")]
    Msg(String),
}

/// Structural check for Phase 3 kernel: @main is IO[Unit]; enums/matches resolve.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    let enums: HashMap<&str, &EnumDef> = program
        .enums
        .iter()
        .map(|e| (e.name.as_str(), e))
        .collect();
    let mut env: HashMap<String, Type> = HashMap::new();
    let ty = infer(&program.main.body, &enums, &mut env)?;
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
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    match expr {
        Expr::Unit => Ok(Type::Unit),
        Expr::IoPrintln(_)
        | Expr::IoDelayUnit
        | Expr::IoSleep(_)
        | Expr::IoFail(_)
        | Expr::UiRunHeadless(_)
        | Expr::UiRunCounter
        | Expr::UiRunTodo
        | Expr::EffectsRunKit => Ok(Type::Io(Box::new(Type::Unit))),
        Expr::LexerClassify(_) => {
            // Returns Tok ADT value (sync), not IO.
            if enums.contains_key("Tok") {
                Ok(Type::Adt("Tok".into()))
            } else {
                Ok(Type::Opaque("Tok".into()))
            }
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
            let vt = infer(value, enums, env)?;
            let old = env.insert(name.clone(), vt);
            let bt = infer(body, enums, env)?;
            if let Some(v) = old {
                env.insert(name.clone(), v);
            } else {
                env.remove(name);
            }
            Ok(bt)
        }
        Expr::Match { scrutinee, arms } => {
            let st = infer(scrutinee, enums, env)?;
            let mut result: Option<Type> = None;
            for arm in arms {
                check_pattern(&arm.pattern, &st, enums)?;
                let bt = infer(&arm.body, enums, env)?;
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
        Expr::FlatMap { inner, body } => {
            let it = infer(inner, enums, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "flatMap receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body, enums, env)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "flatMap body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        Expr::HandleErrorWith { inner, body } => {
            let it = infer(inner, enums, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body, enums, env)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        Expr::Attempt { inner } => {
            let it = infer(inner, enums, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg("attempt receiver must be IO[_]".into()));
            }
            Ok(Type::Io(Box::new(Type::Opaque("Either".into()))))
        }
        Expr::IoRace { left, right } | Expr::IoBoth { left, right } => {
            let lt = infer(left, enums, env)?;
            let rt = infer(right, enums, env)?;
            if !matches!(lt, Type::Io(_)) || !matches!(rt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "IO.race/both arguments must be IO[_]".into(),
                ));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
    }
}

fn check_pattern(
    pat: &Pattern,
    scrut: &Type,
    enums: &HashMap<&str, &EnumDef>,
) -> Result<(), TypeError> {
    match pat {
        Pattern::Wildcard => Ok(()),
        Pattern::Adt {
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
        // Phase 3: allow IO arm variance (Unit vs opaque Either, etc.)
        (Type::Io(_), Type::Io(_)) => true,
        (Type::Adt(x), Type::Adt(y)) => x == y,
        (Type::Opaque(x), Type::Opaque(y)) => x == y,
        _ => false,
    }
}

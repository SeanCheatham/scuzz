use crate::ast::{BinOp, EnumDef, Expr, ExprKind, Program, Type};
use crate::resolve::{FunIndex, ResolveError};
use crate::span::Span;
use std::collections::HashMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TypeError {
    #[error("type error: {0}")]
    Msg(String),
    #[error("type error: {msg}")]
    At { msg: String, span: Span },
}

impl TypeError {
    pub fn message(&self) -> &str {
        match self {
            TypeError::Msg(m) => m.as_str(),
            TypeError::At { msg, .. } => msg.as_str(),
        }
    }

    pub fn span(&self) -> Option<&Span> {
        match self {
            TypeError::At { span, .. } => Some(span),
            TypeError::Msg(_) => None,
        }
    }

    fn with_span_if_bare(self, span: &Span) -> Self {
        match self {
            TypeError::Msg(msg) => TypeError::At {
                msg,
                span: span.clone(),
            },
            other => other,
        }
    }
}

/// Structural check for the kernel dialect: @main is IO[Unit]; defs/calls resolve.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    let enums: HashMap<&str, &EnumDef> = program
        .enums
        .iter()
        .map(|e| (e.name.as_str(), e))
        .collect();
    let funs = FunIndex::build(&program.defs).map_err(|e| match e {
        ResolveError::Duplicate { module, name } => {
            TypeError::Msg(format!("duplicate def {module}.{name}"))
        }
        other => TypeError::Msg(other.to_string()),
    })?;
    for d in &program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(p.name.clone(), p.ty.clone());
        }
        let body_ty = infer(&d.body, &enums, &funs, &d.module, &mut env)?;
        if !types_compat(&body_ty, &d.ret) {
            return Err(TypeError::At {
                msg: format!(
                    "def {} body {:?} does not match declared {:?}",
                    d.name, body_ty, d.ret
                ),
                span: d.body.span.clone(),
            });
        }
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    let ty = infer(
        &program.main.body,
        &enums,
        &funs,
        &program.main.module,
        &mut env,
    )?;
    match ty {
        Type::Io(inner) if matches!(*inner, Type::Unit) => Ok(()),
        other => Err(TypeError::At {
            msg: format!("@main body must be IO[Unit], got {other:?}"),
            span: program.main.body.span.clone(),
        }),
    }
}

fn infer(
    expr: &Expr,
    enums: &HashMap<&str, &EnumDef>,
    funs: &FunIndex<'_>,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    (|| {
    let result = match &expr.kind {
        ExprKind::Unit => Ok(Type::Unit),
        ExprKind::IntLit(_) => Ok(Type::Int),
        ExprKind::StrLit(_) => Ok(Type::String),
        ExprKind::ListLit { elems } => {
            for e in elems {
                infer(e, enums, funs, current_module, env)?;
            }
            Ok(Type::List)
        }
        ExprKind::Interpolate { parts } => {
            for part in parts {
                match part {
                    crate::ast::InterpPart::Lit(_) => {}
                    crate::ast::InterpPart::Expr(e) => {
                        let t = infer(e, enums, funs, current_module, env)?;
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
        ExprKind::IoPrintln(e) | ExprKind::IoFail(e) => {
            let t = infer(e, enums, funs, current_module, env)?;
            expect_ty(&t, &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        ExprKind::IoSleep(e) => {
            let t = infer(e, enums, funs, current_module, env)?;
            expect_ty(&t, &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        ExprKind::IoDelayUnit | ExprKind::EffectsRunKit => Ok(Type::Io(Box::new(Type::Unit))),
        ExprKind::IoPure(inner) => {
            let t = infer(inner, enums, funs, current_module, env)?;
            Ok(Type::Io(Box::new(t)))
        }
        ExprKind::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| TypeError::Msg(format!("unbound variable {name}"))),
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => {
            let en = enums.get(enum_name.as_str()).ok_or_else(|| {
                TypeError::Msg(format!("unknown enum {enum_name}"))
            })?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!("unknown case {enum_name}.{case_name}"))
            })?;
            check_payload_fields(enum_name, case)?;
            if args.len() != case.fields.len() {
                return Err(TypeError::Msg(format!(
                    "{enum_name}.{case_name} expects {} arg(s), got {}",
                    case.fields.len(),
                    args.len()
                )));
            }
            for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                let at = infer(arg, enums, funs, current_module, env)?;
                expect_ty(&at, fty)?;
            }
            Ok(Type::Adt(enum_name.clone()))
        }
        ExprKind::Let { name, value, body } => {
            let vt = infer(value, enums, funs, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let bt = infer(body, enums, funs, current_module, env)?;
            if let Some(v) = old {
                env.insert(name.clone(), v);
            } else {
                env.remove(name);
            }
            Ok(bt)
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            let ct = infer(cond, enums, funs, current_module, env)?;
            if !matches!(ct, Type::Int | Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "if condition must be Int/Bool, got {ct:?}"
                )));
            }
            let tt = infer(then_branch, enums, funs, current_module, env)?;
            let et = infer(else_branch, enums, funs, current_module, env)?;
            if !types_compat(&tt, &et) {
                return Err(TypeError::Msg(format!(
                    "if branches disagree: {tt:?} vs {et:?}"
                )));
            }
            Ok(tt)
        }
        ExprKind::Binary { op, left, right } => {
            let lt = infer(left, enums, funs, current_module, env)?;
            let rt = infer(right, enums, funs, current_module, env)?;
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
        ExprKind::Call { callee, args } => infer_call(callee, args, enums, funs, current_module, env),
        ExprKind::Match { scrutinee, arms } => {
            let st = infer(scrutinee, enums, funs, current_module, env)?;
            let mut result: Option<Type> = None;
            for arm in arms {
                let bound = bind_pattern(&arm.pattern, &st, enums, env)?;
                let bt = infer(&arm.body, enums, funs, current_module, env)?;
                unbind_pattern(bound, env);
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
        ExprKind::FlatMap { inner, param, body } => {
            let it = infer(inner, enums, funs, current_module, env)?;
            let Type::Io(inner_t) = it else {
                return Err(TypeError::Msg("flatMap receiver must be IO[_]".into()));
            };
            let old = if let Some(p) = param {
                env.insert(p.clone(), (*inner_t).clone())
            } else {
                None
            };
            let bt = infer(body, enums, funs, current_module, env)?;
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
        ExprKind::HandleErrorWith { inner, body } => {
            let it = infer(inner, enums, funs, current_module, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body, enums, funs, current_module, env)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        ExprKind::Attempt { inner } => {
            let it = infer(inner, enums, funs, current_module, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg("attempt receiver must be IO[_]".into()));
            }
            Ok(Type::Io(Box::new(Type::Opaque("Either".into()))))
        }
        ExprKind::Lambda { param, body } => {
            // Param type is context-dependent (View for taps, Int for Signal.map).
            // Bind as Opaque so both map and tap lambdas typecheck.
            let old = param.as_ref().map(|p| {
                (
                    p.clone(),
                    env.insert(p.clone(), Type::Opaque("Param".into())),
                )
            });
            let _ = infer(body, enums, funs, current_module, env)?;
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
        ExprKind::IoRace { left, right } | ExprKind::IoBoth { left, right } => {
            let lt = infer(left, enums, funs, current_module, env)?;
            let rt = infer(right, enums, funs, current_module, env)?;
            if !matches!(lt, Type::Io(_)) || !matches!(rt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "IO.race/both arguments must be IO[_]".into(),
                ));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        ExprKind::For { .. } => Err(TypeError::Msg(
            "internal: unlowered `for` (run lower before typecheck)".into(),
        )),
    };
    result
    })()
    .map_err(|e| e.with_span_if_bare(&expr.span))
}

fn infer_call(
    callee: &str,
    args: &[Expr],
    enums: &HashMap<&str, &EnumDef>,
    funs: &FunIndex<'_>,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let mut arg_tys = Vec::new();
    for a in args {
        arg_tys.push(infer(a, enums, funs, current_module, env)?);
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
        "Fs.read" | "Fs.list" | "Fs.mkdirs" | "Fs.canonicalize" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(match callee {
                "Fs.read" => Type::Io(Box::new(Type::String)),
                "Fs.list" => Type::Io(Box::new(Type::List)),
                "Fs.canonicalize" => Type::Io(Box::new(Type::String)),
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
        "Law.signalInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Int)
        }
        "Law.a11yHas" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Bool)
        }
        "Law.assert" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            if !matches!(arg_tys[1], Type::Bool | Type::Int) {
                return Err(TypeError::Msg(format!(
                    "Law.assert ok must be Bool/Int, got {:?}",
                    arg_tys[1]
                )));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
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
            let f = funs.resolve(callee, current_module).map_err(|e| {
                TypeError::Msg(e.to_string())
            })?;
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

fn check_payload_fields(enum_name: &str, case: &crate::ast::EnumCase) -> Result<(), TypeError> {
    for (fname, fty) in &case.fields {
        match fty {
            Type::Int | Type::String | Type::List | Type::Adt(_) => {}
            other => {
                return Err(TypeError::Msg(format!(
                    "{enum_name}.{} field {fname}: Stage 0 payload types are Int, String, List, or an ADT, got {other:?}",
                    case.name
                )))
            }
        }
    }
    Ok(())
}

/// Typecheck a pattern and bind payload names into `env`. Returns previous bindings to restore.
fn bind_pattern(
    pat: &crate::ast::Pattern,
    scrut: &Type,
    enums: &HashMap<&str, &EnumDef>,
    env: &mut HashMap<String, Type>,
) -> Result<Vec<(String, Option<Type>)>, TypeError> {
    match pat {
        crate::ast::Pattern::Wildcard => Ok(Vec::new()),
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
        } => {
            let en = enums.get(enum_name.as_str()).ok_or_else(|| {
                TypeError::Msg(format!("unknown enum {enum_name} in pattern"))
            })?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!(
                    "unknown case {enum_name}.{case_name} in pattern"
                ))
            })?;
            check_payload_fields(enum_name, case)?;
            match scrut {
                Type::Adt(n) if n == enum_name => {}
                Type::Opaque(_) => {}
                other => {
                    return Err(TypeError::Msg(format!(
                        "pattern {enum_name}.{case_name} does not match scrutinee {other:?}"
                    )))
                }
            }
            if binds.len() != case.fields.len() {
                if case.fields.is_empty() {
                    return Err(TypeError::Msg(format!(
                        "pattern {enum_name}.{case_name} is nullary; remove payload binder"
                    )));
                }
                if binds.is_empty() {
                    let example: Vec<_> = case
                        .fields
                        .iter()
                        .enumerate()
                        .map(|(i, _)| format!("x{i}"))
                        .collect();
                    return Err(TypeError::Msg(format!(
                        "pattern {enum_name}.{case_name} needs a payload binder, e.g. {enum_name}.{case_name}({})",
                        example.join(", ")
                    )));
                }
                return Err(TypeError::Msg(format!(
                    "pattern {enum_name}.{case_name} expects {} binder(s), got {}",
                    case.fields.len(),
                    binds.len()
                )));
            }
            let mut restored = Vec::new();
            for (name, (_, fty)) in binds.iter().zip(case.fields.iter()) {
                let old = env.insert(name.clone(), fty.clone());
                restored.push((name.clone(), old));
            }
            Ok(restored)
        }
    }
}

fn unbind_pattern(bound: Vec<(String, Option<Type>)>, env: &mut HashMap<String, Type>) {
    for (name, old) in bound.into_iter().rev() {
        if let Some(v) = old {
            env.insert(name, v);
        } else {
            env.remove(&name);
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lower::lower_program;
    use crate::parser::parse;

    #[test]
    fn typechecks_payload_adt() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(7) match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("payload ADT should typecheck");
    }

    #[test]
    fn typechecks_list_payload_adt() {
        let src = r#"
enum Box:
  case Of(xs: List)
  case Empty
@main def main: IO[Unit] =
  Box.Of(List.cons("a", List.empty())) match {
    case Box.Of(ys) => IO.println(List.head(ys))
    case Box.Empty => IO.println("empty")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List payload ADT should typecheck");
    }

    #[test]
    fn typechecks_multi_field_payload_adt() {
        let src = r#"
enum Pair:
  case Pair(a: Int, b: String)
@main def main: IO[Unit] =
  Pair.Pair(7, "hi") match {
    case Pair.Pair(x, y) => IO.println(Str.concat(Str.fromInt(x), y))
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("multi-field payload ADT should typecheck");
    }

    #[test]
    fn typechecks_nested_adt_payload() {
        let src = r#"
enum Inner:
  case Box(n: Int)
enum Outer:
  case Wrap(x: Inner)
@main def main: IO[Unit] =
  Outer.Wrap(Inner.Box(1)) match {
    case Outer.Wrap(i) => i match {
      case Inner.Box(n) => IO.println(Str.fromInt(n))
    }
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested ADT payload should typecheck");
    }

    #[test]
    fn rejects_payload_type_mismatch() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] = Opt.Some("no") match {
  case Opt.Some(n) => IO.println("x")
  case Opt.None => IO.println("n")
}
"#;
        let p = lower_program(parse(src).unwrap());
        assert!(typecheck(&p).is_err());
    }

    #[test]
    fn rejects_missing_payload_binder() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] = Opt.Some(1) match {
  case Opt.Some => IO.println("x")
  case Opt.None => IO.println("n")
}
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(err.message().contains("needs a payload binder"));
    }

    #[test]
    fn rejects_wrong_binder_arity() {
        let src = r#"
enum Pair:
  case Pair(a: Int, b: String)
@main def main: IO[Unit] = Pair.Pair(1, "x") match {
  case Pair.Pair(x) => IO.println(x)
}
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(err.message().contains("expects 2 binder(s)"));
    }

    #[test]
    fn duplicate_def_across_modules_ok() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "def tag(): String = \"a\"\n".into(),
            ),
            (
                "B.scuzz".into(),
                "def tag(): String = \"b\"\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(Str.concat(A.tag(), B.tag()))\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("cross-module duplicate bare names should typecheck when qualified");
    }

    #[test]
    fn bare_ambiguous_tag_requires_qualify() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "def tag(): String = \"a\"\n".into(),
            ),
            (
                "B.scuzz".into(),
                "def tag(): String = \"b\"\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(tag())\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("ambiguous"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn private_def_not_visible_cross_module() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "private def helper(): String = \"a\"\ndef tag(): String = helper()\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(A.helper())\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("private"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn private_def_visible_same_module() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "private def helper(): String = \"a\"\ndef tag(): String = helper()\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(A.tag())\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("same-module private should typecheck");
    }
}

use crate::ast::{
    BinOp, EnumDef, Expr, ExprKind, FunDef, ImplDef, Param, Program, TraitDef, Type,
};
use crate::resolve::{enum_bare_name, enum_id, EnumIndex, FunIndex, ResolveError};
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

/// Mangled def name for a monomorphized impl method.
pub fn impl_method_name(trait_name: &str, for_type: &str, method: &str) -> String {
    format!("__impl_{trait_name}_{for_type}_{method}")
}

#[derive(Debug, Clone)]
struct MethodEntry {
    mangled: String,
    /// Parameter types after `self`, already resolved.
    params: Vec<Type>,
    ret: Type,
}

#[derive(Debug, Default)]
struct MethodIndex {
    /// `(type_id, method)` → entry. One method name per type.
    by_type_method: HashMap<(String, String), MethodEntry>,
}

impl MethodIndex {
    fn build(
        impls: &[ImplDef],
        traits: &[TraitDef],
        enums: &EnumIndex<'_>,
    ) -> Result<Self, TypeError> {
        let mut by_type_method = HashMap::new();
        for im in impls {
            let tr = traits
                .iter()
                .find(|t| t.name == im.trait_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!(
                        "impl of unknown trait {}",
                        im.trait_name
                    ))
                })?;
            let for_id = enums.resolve_id(&im.for_type, &im.module).map_err(|e| {
                TypeError::Msg(format!(
                    "impl {} for {}: unknown type: {e}",
                    im.trait_name, im.for_type
                ))
            })?;
            for method in &im.methods {
                let tm = tr.methods.iter().find(|m| m.name == method.name).ok_or_else(|| {
                    TypeError::Msg(format!(
                        "impl {} for {}: unknown method {}",
                        im.trait_name, im.for_type, method.name
                    ))
                })?;
                if tm.params.len() != method.params.len() {
                    return Err(TypeError::Msg(format!(
                        "impl {} for {}.{}: param count mismatch",
                        im.trait_name, im.for_type, method.name
                    )));
                }
                for (a, b) in tm.params.iter().zip(method.params.iter()) {
                    let at = resolve_type(&a.ty, enums, &tr.module)?;
                    let bt = resolve_type(&b.ty, enums, &im.module)?;
                    if !types_compat(&at, &bt) {
                        return Err(TypeError::Msg(format!(
                            "impl {} for {}.{}: param type mismatch",
                            im.trait_name, im.for_type, method.name
                        )));
                    }
                }
                let want_ret = resolve_type(&tm.ret, enums, &tr.module)?;
                let got_ret = resolve_type(&method.ret, enums, &im.module)?;
                if !types_compat(&want_ret, &got_ret) {
                    return Err(TypeError::Msg(format!(
                        "impl {} for {}.{}: return type mismatch",
                        im.trait_name, im.for_type, method.name
                    )));
                }
                let key = (for_id.clone(), method.name.clone());
                if by_type_method.contains_key(&key) {
                    return Err(TypeError::Msg(format!(
                        "duplicate method {} for type {}",
                        method.name, for_id
                    )));
                }
                let params: Result<Vec<_>, _> = method
                    .params
                    .iter()
                    .map(|p| resolve_type(&p.ty, enums, &im.module))
                    .collect();
                by_type_method.insert(
                    key,
                    MethodEntry {
                        mangled: impl_method_name(
                            &im.trait_name,
                            enum_bare_name(&for_id),
                            &method.name,
                        ),
                        params: params?,
                        ret: got_ret,
                    },
                );
            }
            // Every trait method must be implemented.
            for tm in &tr.methods {
                if !im.methods.iter().any(|m| m.name == tm.name) {
                    return Err(TypeError::Msg(format!(
                        "impl {} for {}: missing method {}",
                        im.trait_name, im.for_type, tm.name
                    )));
                }
            }
        }
        Ok(Self { by_type_method })
    }

    fn lookup(&self, type_id: &str, method: &str) -> Result<&MethodEntry, TypeError> {
        self.by_type_method
            .get(&(type_id.to_string(), method.to_string()))
            .ok_or_else(|| {
                TypeError::Msg(format!("no impl method {method} for type {type_id}"))
            })
    }
}

/// Turn `impl` methods into ordinary defs (`self` first) for FunIndex / codegen.
pub fn expand_impls(mut program: Program) -> Result<Program, TypeError> {
    let enums = EnumIndex::build(&program.enums, &program.imports).map_err(|e| {
        TypeError::Msg(e.to_string())
    })?;
    // Validate via MethodIndex build.
    let _ = MethodIndex::build(&program.impls, &program.traits, &enums)?;
    for im in &program.impls {
        let for_id = enums.resolve_id(&im.for_type, &im.module).map_err(|e| {
            TypeError::Msg(e.to_string())
        })?;
        let bare = enum_bare_name(&for_id).to_string();
        for method in &im.methods {
            let mangled = impl_method_name(&im.trait_name, &bare, &method.name);
            if program
                .defs
                .iter()
                .any(|d| d.module == im.module && d.name == mangled)
            {
                return Err(TypeError::Msg(format!(
                    "impl method name collision {mangled}"
                )));
            }
            let mut params = vec![Param {
                name: "self".into(),
                ty: Type::Adt(im.for_type.clone()),
            }];
            params.extend(method.params.clone());
            program.defs.push(FunDef {
                module: im.module.clone(),
                name: mangled,
                is_private: false,
                type_params: Vec::new(),
                params,
                ret: method.ret.clone(),
                body: method.body.clone(),
            });
        }
    }
    Ok(program)
}

/// Structural check for the kernel dialect: @main is IO[Unit]; defs/calls resolve.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    let enums = EnumIndex::build(&program.enums, &program.imports).map_err(|e| match e {
        ResolveError::Duplicate { module, name } => {
            TypeError::Msg(format!("duplicate enum {module}.{name}"))
        }
        other => TypeError::Msg(other.to_string()),
    })?;
    let methods = MethodIndex::build(&program.impls, &program.traits, &enums)?;
    let funs = FunIndex::build(&program.defs, &program.imports, &program.enums).map_err(|e| match e {
        ResolveError::Duplicate { module, name } => {
            TypeError::Msg(format!("duplicate def {module}.{name}"))
        }
        other => TypeError::Msg(other.to_string()),
    })?;
    for d in &program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(
                p.name.clone(),
                resolve_type_in(&p.ty, &enums, &d.module, &d.type_params)?,
            );
        }
        let body_ty = infer(&d.body, &enums, &funs, &methods, &d.module, &mut env)?;
        let ret = resolve_type_in(&d.ret, &enums, &d.module, &d.type_params)?;
        if !types_compat(&body_ty, &ret) {
            return Err(TypeError::At {
                msg: format!(
                    "def {} body {:?} does not match declared {:?}",
                    d.name, body_ty, ret
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
        &methods,
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

fn resolve_type(ty: &Type, enums: &EnumIndex<'_>, module: &str) -> Result<Type, TypeError> {
    resolve_type_in(ty, enums, module, &[])
}

fn resolve_type_in(
    ty: &Type,
    enums: &EnumIndex<'_>,
    module: &str,
    type_params: &[String],
) -> Result<Type, TypeError> {
    match ty {
        Type::Var(n) => {
            if type_params.iter().any(|p| p == n) {
                Ok(Type::Var(n.clone()))
            } else {
                Err(TypeError::Msg(format!("unknown type parameter {n}")))
            }
        }
        Type::Adt(n) => {
            if type_params.iter().any(|p| p == n) {
                Ok(Type::Var(n.clone()))
            } else {
                let id = enums.resolve_id(n, module).map_err(|e| {
                    TypeError::Msg(format!("unknown type {n}: {e}"))
                })?;
                Ok(Type::Adt(id))
            }
        }
        Type::Io(inner) => Ok(Type::Io(Box::new(resolve_type_in(
            inner,
            enums,
            module,
            type_params,
        )?))),
        other => Ok(other.clone()),
    }
}

fn lookup_enum<'a>(
    enums: &'a EnumIndex<'a>,
    enum_name: &str,
    current_module: &str,
) -> Result<(&'a EnumDef, String), TypeError> {
    let en = enums.resolve(enum_name, current_module).map_err(|e| {
        TypeError::Msg(format!("unknown enum {enum_name}: {e}"))
    })?;
    Ok((en, enum_id(&en.module, &en.name)))
}

fn is_record_like(e: &EnumDef) -> bool {
    e.is_record || (e.cases.len() == 1 && e.cases[0].name == e.name)
}

fn field_type(
    base_ty: &Type,
    field: &str,
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<Type, TypeError> {
    let Type::Adt(id) = base_ty else {
        return Err(TypeError::Msg(format!(
            "field access .{field} needs a record type, got {base_ty:?}"
        )));
    };
    let (en, _) = lookup_enum(enums, id, current_module)?;
    if !is_record_like(en) {
        return Err(TypeError::Msg(format!(
            "field access .{field} requires a record type, got enum {}",
            en.name
        )));
    }
    let case = en.cases.first().ok_or_else(|| {
        TypeError::Msg(format!("record {} has no cases", en.name))
    })?;
    let (_, fty) = case
        .fields
        .iter()
        .find(|(n, _)| n == field)
        .ok_or_else(|| {
            TypeError::Msg(format!("record {} has no field {field}", en.name))
        })?;
    resolve_type(fty, enums, &en.module)
}

/// Rewrite `p.x` into a single-arm match and `p.m(…)` into a mangled Call.
pub fn resolve_field_access(mut program: Program) -> Result<Program, TypeError> {
    let enums_owned = program.enums.clone();
    let imports_owned = program.imports.clone();
    let defs_owned = program.defs.clone();
    let traits_owned = program.traits.clone();
    let impls_owned = program.impls.clone();
    let enums = EnumIndex::build(&enums_owned, &imports_owned).map_err(|e| {
        TypeError::Msg(e.to_string())
    })?;
    let methods = MethodIndex::build(&impls_owned, &traits_owned, &enums)?;
    let funs = FunIndex::build(&defs_owned, &imports_owned, &enums_owned).map_err(|e| {
        TypeError::Msg(e.to_string())
    })?;
    for d in &mut program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(p.name.clone(), resolve_type(&p.ty, &enums, &d.module)?);
        }
        d.body = rewrite_fields(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &enums,
            &funs,
            &methods,
            &d.module,
            &mut env,
        )?;
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    let main_mod = program.main.module.clone();
    program.main.body = rewrite_fields(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &enums,
        &funs,
        &methods,
        &main_mod,
        &mut env,
    )?;
    Ok(program)
}

fn rewrite_fields(
    expr: Expr,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => {
            let receiver = rewrite_fields(*receiver, enums, funs, methods, current_module, env)?;
            let args = args
                .into_iter()
                .map(|a| rewrite_fields(a, enums, funs, methods, current_module, env))
                .collect::<Result<Vec<_>, _>>()?;
            let rt = infer(&receiver, enums, funs, methods, current_module, env)?;
            let Type::Adt(id) = &rt else {
                return Err(TypeError::Msg(format!(
                    "method .{method} needs a record/ADT receiver, got {rt:?}"
                ))
                .with_span_if_bare(&span));
            };
            let entry = methods.lookup(id, &method)?;
            let mut call_args = vec![receiver];
            call_args.extend(args);
            Ok(Expr::new(
                ExprKind::Call {
                    callee: entry.mangled.clone(),
                    args: call_args,
                },
                span,
            ))
        }
        ExprKind::Field { base, field } => {
            let base = rewrite_fields(*base, enums, funs, methods, current_module, env)?;
            let bt = infer(&base, enums, funs, methods, current_module, env)?;
            let Type::Adt(id) = &bt else {
                return Err(TypeError::Msg(format!(
                    "field access .{field} needs a record type, got {bt:?}"
                )).with_span_if_bare(&span));
            };
            let (en, eid) = lookup_enum(enums, id, current_module)?;
            if !is_record_like(en) {
                return Err(TypeError::Msg(format!(
                    "field access .{field} requires a record type, got enum {}",
                    en.name
                ))
                .with_span_if_bare(&span));
            }
            let case = en.cases.first().unwrap();
            let idx = case
                .fields
                .iter()
                .position(|(n, _)| n == &field)
                .ok_or_else(|| {
                    TypeError::Msg(format!("record {} has no field {field}", en.name))
                        .with_span_if_bare(&span)
                })?;
            let binds: Vec<String> = (0..case.fields.len())
                .map(|i| format!("__f{i}"))
                .collect();
            let body = Expr::new(ExprKind::Var(binds[idx].clone()), span.clone());
            Ok(Expr::new(
                ExprKind::Match {
                    scrutinee: Box::new(base),
                    arms: vec![crate::ast::MatchArm {
                        pattern: crate::ast::Pattern::Adt {
                            enum_name: eid,
                            case_name: case.name.clone(),
                            binds,
                        },
                        body,
                    }],
                },
                span,
            ))
        }
        ExprKind::IoPrintln(e) => Ok(Expr::new(
            ExprKind::IoPrintln(Box::new(rewrite_fields(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
            )?)),
            span,
        )),
        ExprKind::IoSleep(e) => Ok(Expr::new(
            ExprKind::IoSleep(Box::new(rewrite_fields(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
            )?)),
            span,
        )),
        ExprKind::IoFail(e) => Ok(Expr::new(
            ExprKind::IoFail(Box::new(rewrite_fields(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
            )?)),
            span,
        )),
        ExprKind::IoPure(e) => Ok(Expr::new(
            ExprKind::IoPure(Box::new(rewrite_fields(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
            )?)),
            span,
        )),
        ExprKind::FlatMap { inner, param, body } => {
            let inner = rewrite_fields(*inner, enums, funs, methods, current_module, env)?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let Type::Io(inner_t) = it else {
                return Ok(Expr::new(
                    ExprKind::FlatMap {
                        inner: Box::new(inner),
                        param,
                        body: Box::new(rewrite_fields(
                            *body,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                        )?),
                    },
                    span,
                ));
            };
            let old = if let Some(ref p) = param {
                env.insert(p.clone(), (*inner_t).clone())
            } else {
                None
            };
            let body = rewrite_fields(*body, enums, funs, methods, current_module, env)?;
            if let Some(p) = &param {
                if let Some(v) = old {
                    env.insert(p.clone(), v);
                } else {
                    env.remove(p);
                }
            }
            Ok(Expr::new(
                ExprKind::FlatMap {
                    inner: Box::new(inner),
                    param,
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::HandleErrorWith { inner, body } => Ok(Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(rewrite_fields(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                body: Box::new(rewrite_fields(
                    *body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::Attempt { inner } => Ok(Expr::new(
            ExprKind::Attempt {
                inner: Box::new(rewrite_fields(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::IoRace { left, right } => Ok(Expr::new(
            ExprKind::IoRace {
                left: Box::new(rewrite_fields(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                right: Box::new(rewrite_fields(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::IoBoth { left, right } => Ok(Expr::new(
            ExprKind::IoBoth {
                left: Box::new(rewrite_fields(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                right: Box::new(rewrite_fields(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::Let { name, value, body } => {
            let value = rewrite_fields(*value, enums, funs, methods, current_module, env)?;
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let body = rewrite_fields(*body, enums, funs, methods, current_module, env)?;
            if let Some(v) = old {
                env.insert(name.clone(), v);
            } else {
                env.remove(&name);
            }
            Ok(Expr::new(
                ExprKind::Let {
                    name,
                    value: Box::new(value),
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::ListLit { elems } => Ok(Expr::new(
            ExprKind::ListLit {
                elems: elems
                    .into_iter()
                    .map(|e| rewrite_fields(e, enums, funs, methods, current_module, env))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Interpolate { parts } => Ok(Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        crate::ast::InterpPart::Lit(s) => Ok(crate::ast::InterpPart::Lit(s)),
                        crate::ast::InterpPart::Expr(e) => Ok(crate::ast::InterpPart::Expr(
                            rewrite_fields(e, enums, funs, methods, current_module, env)?,
                        )),
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Match { scrutinee, arms } => {
            let scrutinee = rewrite_fields(*scrutinee, enums, funs, methods, current_module, env)?;
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound = bind_pattern(&arm.pattern, &st, enums, current_module, env)?;
                let body = rewrite_fields(arm.body, enums, funs, methods, current_module, env)?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm {
                    pattern: arm.pattern,
                    body,
                });
            }
            Ok(Expr::new(
                ExprKind::Match {
                    scrutinee: Box::new(scrutinee),
                    arms: out_arms,
                },
                span,
            ))
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => Ok(Expr::new(
            ExprKind::If {
                cond: Box::new(rewrite_fields(
                    *cond,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                then_branch: Box::new(rewrite_fields(
                    *then_branch,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                else_branch: Box::new(rewrite_fields(
                    *else_branch,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::Binary { op, left, right } => Ok(Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(rewrite_fields(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
                right: Box::new(rewrite_fields(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        ExprKind::Call { callee, args } => Ok(Expr::new(
            ExprKind::Call {
                callee,
                args: args
                    .into_iter()
                    .map(|e| rewrite_fields(e, enums, funs, methods, current_module, env))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => Ok(Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args
                    .into_iter()
                    .map(|e| rewrite_fields(e, enums, funs, methods, current_module, env))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Lambda { param, body } => Ok(Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(rewrite_fields(
                    *body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?),
            },
            span,
        )),
        other => Ok(Expr::new(other, span)),
    }
}

fn infer(
    expr: &Expr,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
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
                infer(e, enums, funs, methods, current_module, env)?;
            }
            Ok(Type::List)
        }
        ExprKind::Interpolate { parts } => {
            for part in parts {
                match part {
                    crate::ast::InterpPart::Lit(_) => {}
                    crate::ast::InterpPart::Expr(e) => {
                        let t = infer(e, enums, funs, methods, current_module, env)?;
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
            let t = infer(e, enums, funs, methods, current_module, env)?;
            expect_ty(&t, &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        ExprKind::IoSleep(e) => {
            let t = infer(e, enums, funs, methods, current_module, env)?;
            expect_ty(&t, &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        ExprKind::IoDelayUnit => Ok(Type::Io(Box::new(Type::Unit))),
        ExprKind::IoPure(inner) => {
            let t = infer(inner, enums, funs, methods, current_module, env)?;
            Ok(Type::Io(Box::new(t)))
        }
        ExprKind::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| TypeError::Msg(format!("unbound variable {name}"))),
        ExprKind::Field { base, field } => {
            let bt = infer(base, enums, funs, methods, current_module, env)?;
            field_type(&bt, field, enums, current_module)
        },
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => {
            let rt = infer(receiver, enums, funs, methods, current_module, env)?;
            let Type::Adt(id) = &rt else {
                return Err(TypeError::Msg(format!(
                    "method .{method} needs a record/ADT receiver, got {rt:?}"
                )));
            };
            let entry = methods.lookup(id, method)?;
            if args.len() != entry.params.len() {
                return Err(TypeError::Msg(format!(
                    ".{method} expects {} arg(s), got {}",
                    entry.params.len(),
                    args.len()
                )));
            }
            for (arg, want) in args.iter().zip(entry.params.iter()) {
                let at = infer(arg, enums, funs, methods, current_module, env)?;
                expect_ty(&at, want)?;
            }
            Ok(entry.ret.clone())
        },
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => {
            let (en, id) = lookup_enum(enums, enum_name, current_module)?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!("unknown case {enum_name}.{case_name}"))
            })?;
            check_payload_fields(&id, case)?;
            if args.len() != case.fields.len() {
                return Err(TypeError::Msg(format!(
                    "{enum_name}.{case_name} expects {} arg(s), got {}",
                    case.fields.len(),
                    args.len()
                )));
            }
            for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                let at = infer(arg, enums, funs, methods, current_module, env)?;
                let want = resolve_type(fty, enums, &en.module)?;
                expect_ty(&at, &want)?;
            }
            Ok(Type::Adt(id))
        }
        ExprKind::Let { name, value, body } => {
            let vt = infer(value, enums, funs, methods, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let bt = infer(body, enums, funs, methods, current_module, env)?;
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
            let ct = infer(cond, enums, funs, methods, current_module, env)?;
            if !matches!(ct, Type::Int | Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "if condition must be Int/Bool, got {ct:?}"
                )));
            }
            let tt = infer(then_branch, enums, funs, methods, current_module, env)?;
            let et = infer(else_branch, enums, funs, methods, current_module, env)?;
            if !types_compat(&tt, &et) {
                return Err(TypeError::Msg(format!(
                    "if branches disagree: {tt:?} vs {et:?}"
                )));
            }
            Ok(tt)
        }
        ExprKind::Binary { op, left, right } => {
            let lt = infer(left, enums, funs, methods, current_module, env)?;
            let rt = infer(right, enums, funs, methods, current_module, env)?;
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
        ExprKind::Call { callee, args } => {
            infer_call(callee, args, enums, funs, methods, current_module, env)
        }
        ExprKind::Match { scrutinee, arms } => {
            let st = infer(scrutinee, enums, funs, methods, current_module, env)?;
            let mut result: Option<Type> = None;
            for arm in arms {
                let bound = bind_pattern(&arm.pattern, &st, enums, current_module, env)?;
                let bt = infer(&arm.body, enums, funs, methods, current_module, env)?;
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
            let it = infer(inner, enums, funs, methods, current_module, env)?;
            let Type::Io(inner_t) = it else {
                return Err(TypeError::Msg("flatMap receiver must be IO[_]".into()));
            };
            let old = if let Some(p) = param {
                env.insert(p.clone(), (*inner_t).clone())
            } else {
                None
            };
            let bt = infer(body, enums, funs, methods, current_module, env)?;
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
            let it = infer(inner, enums, funs, methods, current_module, env)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body, enums, funs, methods, current_module, env)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "handleErrorWith body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
        ExprKind::Attempt { inner } => {
            let it = infer(inner, enums, funs, methods, current_module, env)?;
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
            let _ = infer(body, enums, funs, methods, current_module, env)?;
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
            let lt = infer(left, enums, funs, methods, current_module, env)?;
            let rt = infer(right, enums, funs, methods, current_module, env)?;
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
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let mut arg_tys = Vec::new();
    for a in args {
        arg_tys.push(infer(a, enums, funs, methods, current_module, env)?);
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
        "Ref.of" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Opaque("Ref".into()))))
        }
        "Ref.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Ref.set" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Queue.unbounded" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::Opaque("Queue".into()))))
        }
        "Queue.offer" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Queue.take" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Deferred.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::Opaque("Deferred".into()))))
        }
        "Deferred.complete" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Deferred.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(Type::String)))
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
        "View.column" | "View.row" | "View.stack" => {
            // Nullary or children: `View.column(a, b, …)` adds each child.
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
        "View.expanded" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.center" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.align" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.positioned" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.padding" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.sized" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.minSize" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.background" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
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
            if f.type_params.is_empty() {
                for (p, a) in f.params.iter().zip(arg_tys.iter()) {
                    let want = resolve_type(&p.ty, enums, &f.module)?;
                    if !types_compat(a, &want) {
                        return Err(TypeError::Msg(format!(
                            "{callee} arg type mismatch: expected {:?}, got {:?}",
                            want, a
                        )));
                    }
                }
                resolve_type(&f.ret, enums, &f.module)
            } else {
                let mut subst: HashMap<String, Type> = HashMap::new();
                for (p, a) in f.params.iter().zip(arg_tys.iter()) {
                    let want = resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?;
                    unify_types(&want, a, &mut subst)?;
                }
                let ret = resolve_type_in(&f.ret, enums, &f.module, &f.type_params)?;
                Ok(apply_subst(&ret, &subst))
            }
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
    enums: &EnumIndex<'_>,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Vec<(String, Option<Type>)>, TypeError> {
    match pat {
        crate::ast::Pattern::Wildcard => Ok(Vec::new()),
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
        } => {
            let (en, id) = lookup_enum(enums, enum_name, current_module)?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!(
                    "unknown case {enum_name}.{case_name} in pattern"
                ))
            })?;
            check_payload_fields(&id, case)?;
            match scrut {
                Type::Adt(n) if n == &id => {}
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
                let fty = resolve_type(fty, enums, &en.module)?;
                let old = env.insert(name.clone(), fty);
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
        (Type::Var(x), Type::Var(y)) => x == y,
        (Type::Opaque(_), _) | (_, Type::Opaque(_)) => true,
        _ => false,
    }
}

fn unify_types(
    pattern: &Type,
    concrete: &Type,
    subst: &mut HashMap<String, Type>,
) -> Result<(), TypeError> {
    match (pattern, concrete) {
        (Type::Var(n), t) => {
            if let Some(prev) = subst.get(n) {
                if !types_compat(prev, t) {
                    return Err(TypeError::Msg(format!(
                        "type parameter {n} constrained to both {prev:?} and {t:?}"
                    )));
                }
            } else {
                if !mono_type_ok(t) {
                    return Err(TypeError::Msg(format!(
                        "cannot monomorphize type parameter {n} to {t:?}"
                    )));
                }
                subst.insert(n.clone(), t.clone());
            }
            Ok(())
        }
        (Type::Io(a), Type::Io(b)) => unify_types(a, b, subst),
        (a, b) if types_compat(a, b) => Ok(()),
        (a, b) => Err(TypeError::Msg(format!(
            "type mismatch: expected {a:?}, got {b:?}"
        ))),
    }
}

fn mono_type_ok(t: &Type) -> bool {
    matches!(
        t,
        Type::Unit | Type::Int | Type::String | Type::Bool | Type::List | Type::Adt(_)
    )
}

fn apply_subst(ty: &Type, subst: &HashMap<String, Type>) -> Type {
    match ty {
        Type::Var(n) => subst.get(n).cloned().unwrap_or_else(|| ty.clone()),
        Type::Io(inner) => Type::Io(Box::new(apply_subst(inner, subst))),
        other => other.clone(),
    }
}

fn type_mangle(t: &Type) -> String {
    match t {
        Type::Unit => "Unit".into(),
        Type::Int => "Int".into(),
        Type::String => "String".into(),
        Type::Bool => "Bool".into(),
        Type::List => "List".into(),
        Type::Adt(id) => id.replace('.', "_"),
        Type::Io(inner) => format!("IO_{}", type_mangle(inner)),
        Type::Var(n) => n.clone(),
        Type::Opaque(n) => n.clone(),
    }
}

fn mono_def_name(def_name: &str, subst: &HashMap<String, Type>, type_params: &[String]) -> String {
    let mut parts = vec![format!("__gen_{def_name}")];
    for p in type_params {
        let t = subst.get(p).expect("subst complete");
        parts.push(type_mangle(t));
    }
    parts.join("_")
}

/// Specialize generic defs at call sites; drop templates.
pub fn monomorphize(mut program: Program) -> Result<Program, TypeError> {
    let enums_owned = program.enums.clone();
    let imports_owned = program.imports.clone();
    let traits_owned = program.traits.clone();
    let impls_owned = program.impls.clone();
    let defs_owned = program.defs.clone();
    let enums = EnumIndex::build(&enums_owned, &imports_owned).map_err(|e| {
        TypeError::Msg(e.to_string())
    })?;
    let methods = MethodIndex::build(&impls_owned, &traits_owned, &enums)?;
    let funs = FunIndex::build(&defs_owned, &imports_owned, &enums_owned).map_err(|e| {
        TypeError::Msg(e.to_string())
    })?;

    let mut specialized: HashMap<String, FunDef> = HashMap::new();
    for d in &mut program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(
                p.name.clone(),
                resolve_type_in(&p.ty, &enums, &d.module, &d.type_params)?,
            );
        }
        d.body = mono_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &enums,
            &funs,
            &methods,
            &d.module,
            &mut env,
            &mut specialized,
        )?;
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    let main_mod = program.main.module.clone();
    program.main.body = mono_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &enums,
        &funs,
        &methods,
        &main_mod,
        &mut env,
        &mut specialized,
    )?;

    // Drop generic templates; keep non-generic defs + specialized clones.
    program.defs.retain(|d| d.type_params.is_empty());
    for (name, def) in specialized {
        if program.defs.iter().any(|d| d.module == def.module && d.name == name) {
            continue;
        }
        program.defs.push(def);
    }
    Ok(program)
}

fn mono_expr(
    expr: Expr,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    specialized: &mut HashMap<String, FunDef>,
) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Call { callee, args } => {
            let args = args
                .into_iter()
                .map(|a| mono_expr(a, enums, funs, methods, current_module, env, specialized))
                .collect::<Result<Vec<_>, _>>()?;
            if let Ok(f) = funs.resolve(&callee, current_module) {
                if !f.type_params.is_empty() {
                    let mut arg_tys = Vec::new();
                    for a in &args {
                        arg_tys.push(infer(a, enums, funs, methods, current_module, env)?);
                    }
                    let mut subst: HashMap<String, Type> = HashMap::new();
                    for (p, a) in f.params.iter().zip(arg_tys.iter()) {
                        let want = resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?;
                        unify_types(&want, a, &mut subst)?;
                    }
                    let mangled = mono_def_name(&f.name, &subst, &f.type_params);
                    if !specialized.contains_key(&mangled) {
                        let params: Result<Vec<_>, _> = f
                            .params
                            .iter()
                            .map(|p| {
                                let ty = resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?;
                                Ok(Param {
                                    name: p.name.clone(),
                                    ty: apply_subst(&ty, &subst),
                                })
                            })
                            .collect();
                        let ret = apply_subst(
                            &resolve_type_in(&f.ret, enums, &f.module, &f.type_params)?,
                            &subst,
                        );
                        specialized.insert(
                            mangled.clone(),
                            FunDef {
                                module: f.module.clone(),
                                name: mangled.clone(),
                                is_private: f.is_private,
                                type_params: Vec::new(),
                                params: params?,
                                ret,
                                body: f.body.clone(),
                            },
                        );
                    }
                    return Ok(Expr::new(
                        ExprKind::Call {
                            callee: mangled,
                            args,
                        },
                        span,
                    ));
                }
            }
            Ok(Expr::new(ExprKind::Call { callee, args }, span))
        }
        ExprKind::IoPrintln(e) => Ok(Expr::new(
            ExprKind::IoPrintln(Box::new(mono_expr(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?)),
            span,
        )),
        ExprKind::IoSleep(e) => Ok(Expr::new(
            ExprKind::IoSleep(Box::new(mono_expr(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?)),
            span,
        )),
        ExprKind::IoFail(e) => Ok(Expr::new(
            ExprKind::IoFail(Box::new(mono_expr(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?)),
            span,
        )),
        ExprKind::IoPure(e) => Ok(Expr::new(
            ExprKind::IoPure(Box::new(mono_expr(
                *e,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?)),
            span,
        )),
        ExprKind::FlatMap { inner, param, body } => {
            let inner = mono_expr(*inner, enums, funs, methods, current_module, env, specialized)?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let old = if let (Type::Io(inner_t), Some(ref p)) = (&it, &param) {
                env.insert(p.clone(), (**inner_t).clone())
            } else {
                None
            };
            let body = mono_expr(*body, enums, funs, methods, current_module, env, specialized)?;
            if let Some(p) = &param {
                if let Some(v) = old {
                    env.insert(p.clone(), v);
                } else {
                    env.remove(p);
                }
            }
            Ok(Expr::new(
                ExprKind::FlatMap {
                    inner: Box::new(inner),
                    param,
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::HandleErrorWith { inner, body } => Ok(Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(mono_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                body: Box::new(mono_expr(
                    *body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::Attempt { inner } => Ok(Expr::new(
            ExprKind::Attempt {
                inner: Box::new(mono_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::IoRace { left, right } => Ok(Expr::new(
            ExprKind::IoRace {
                left: Box::new(mono_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                right: Box::new(mono_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::IoBoth { left, right } => Ok(Expr::new(
            ExprKind::IoBoth {
                left: Box::new(mono_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                right: Box::new(mono_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::Let { name, value, body } => {
            let value = mono_expr(*value, enums, funs, methods, current_module, env, specialized)?;
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let body = mono_expr(*body, enums, funs, methods, current_module, env, specialized)?;
            if let Some(v) = old {
                env.insert(name.clone(), v);
            } else {
                env.remove(&name);
            }
            Ok(Expr::new(
                ExprKind::Let {
                    name,
                    value: Box::new(value),
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::ListLit { elems } => Ok(Expr::new(
            ExprKind::ListLit {
                elems: elems
                    .into_iter()
                    .map(|e| mono_expr(e, enums, funs, methods, current_module, env, specialized))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Interpolate { parts } => Ok(Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        crate::ast::InterpPart::Lit(s) => Ok(crate::ast::InterpPart::Lit(s)),
                        crate::ast::InterpPart::Expr(e) => Ok(crate::ast::InterpPart::Expr(
                            mono_expr(e, enums, funs, methods, current_module, env, specialized)?,
                        )),
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Match { scrutinee, arms } => {
            let scrutinee =
                mono_expr(*scrutinee, enums, funs, methods, current_module, env, specialized)?;
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound = bind_pattern(&arm.pattern, &st, enums, current_module, env)?;
                let body =
                    mono_expr(arm.body, enums, funs, methods, current_module, env, specialized)?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm {
                    pattern: arm.pattern,
                    body,
                });
            }
            Ok(Expr::new(
                ExprKind::Match {
                    scrutinee: Box::new(scrutinee),
                    arms: out_arms,
                },
                span,
            ))
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => Ok(Expr::new(
            ExprKind::If {
                cond: Box::new(mono_expr(
                    *cond,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                then_branch: Box::new(mono_expr(
                    *then_branch,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                else_branch: Box::new(mono_expr(
                    *else_branch,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::Binary { op, left, right } => Ok(Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(mono_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                right: Box::new(mono_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => Ok(Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args
                    .into_iter()
                    .map(|e| mono_expr(e, enums, funs, methods, current_module, env, specialized))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Field { base, field } => Ok(Expr::new(
            ExprKind::Field {
                base: Box::new(mono_expr(
                    *base,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                field,
            },
            span,
        )),
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => Ok(Expr::new(
            ExprKind::MethodCall {
                receiver: Box::new(mono_expr(
                    *receiver,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                method,
                args: args
                    .into_iter()
                    .map(|e| mono_expr(e, enums, funs, methods, current_module, env, specialized))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Lambda { param, body } => Ok(Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(mono_expr(
                    *body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
            },
            span,
        )),
        other => Ok(Expr::new(other, span)),
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

    #[test]
    fn import_disambiguates_ambiguous_bare() {
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
                "import A.tag\n@main def main: IO[Unit] = IO.println(Str.concat(tag(), B.tag()))\n"
                    .into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("import should disambiguate bare tag");
    }

    #[test]
    fn cannot_import_private_def() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "private def helper(): String = \"a\"\ndef tag(): String = helper()\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "import A.helper\n@main def main: IO[Unit] = IO.println(helper())\n".into(),
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
}

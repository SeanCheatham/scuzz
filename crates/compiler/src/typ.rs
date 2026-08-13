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
    for en in &program.enums {
        let id = crate::resolve::enum_id(&en.module, &en.name);
        for case in &en.cases {
            check_payload_fields(&id, en, case)?;
            for (_fname, fty) in &case.fields {
                resolve_type_in(fty, &enums, &en.module, &en.type_params)?;
            }
        }
    }
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
                    TypeError::Msg(format!("unknown enum {n}: {e}"))
                })?;
                Ok(Type::Adt(id))
            }
        }
        Type::App(n, args) => {
            let en = enums.resolve(n, module).map_err(|e| {
                TypeError::Msg(format!("unknown enum {n}: {e}"))
            })?;
            if en.type_params.is_empty() {
                return Err(TypeError::Msg(format!(
                    "enum {n} is not generic; remove type arguments"
                )));
            }
            if en.type_params.len() != args.len() {
                return Err(TypeError::Msg(format!(
                    "enum {n} expects {} type argument(s), got {}",
                    en.type_params.len(),
                    args.len()
                )));
            }
            let id = enums.resolve_id(n, module).map_err(|e| {
                TypeError::Msg(format!("unknown enum {n}: {e}"))
            })?;
            let rargs = args
                .iter()
                .map(|a| resolve_type_in(a, enums, module, type_params))
                .collect::<Result<Vec<_>, _>>()?;
            Ok(Type::App(id, rargs))
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
    let (id, targs): (&str, &[Type]) = match base_ty {
        Type::Adt(id) => (id, &[]),
        Type::App(id, args) => (id, args),
        other => {
            return Err(TypeError::Msg(format!(
                "field access .{field} needs a record type, got {other:?}"
            )))
        }
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
    let resolved = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
    let mut subst: HashMap<String, Type> = HashMap::new();
    let mut skipped: Vec<String> = Vec::new();
    for (p, t) in en.type_params.iter().zip(targs.iter()) {
        if matches!(t, Type::Opaque(_)) {
            skipped.push(p.clone());
        } else {
            subst.insert(p.clone(), t.clone());
        }
    }
    Ok(erase_vars(&apply_subst(&resolved, &subst), &skipped))
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
                            type_args: Vec::new(),
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
            type_args,
        } => Ok(Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args
                    .into_iter()
                    .map(|e| rewrite_fields(e, enums, funs, methods, current_module, env))
                    .collect::<Result<Vec<_>, _>>()?,
                type_args,
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
            ..
        } => {
            let (en, id) = lookup_enum(enums, enum_name, current_module)?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!("unknown case {enum_name}.{case_name}"))
            })?;
            check_payload_fields(&id, en, case)?;
            if args.len() != case.fields.len() {
                return Err(TypeError::Msg(format!(
                    "{enum_name}.{case_name} expects {} arg(s), got {}",
                    case.fields.len(),
                    args.len()
                )));
            }
            if en.type_params.is_empty() {
                for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                    let at = infer(arg, enums, funs, methods, current_module, env)?;
                    let want = resolve_type(fty, enums, &en.module)?;
                    expect_ty(&at, &want)?;
                }
                Ok(Type::Adt(id))
            } else {
                let mut subst: HashMap<String, Type> = HashMap::new();
                for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                    let at = infer(arg, enums, funs, methods, current_module, env)?;
                    let want = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
                    unify_construct(&want, &at, &mut subst)?;
                }
                // Params the args did not determine stay opaque placeholders;
                // elaboration resolves them from the expected type or errors.
                let targs = en
                    .type_params
                    .iter()
                    .map(|p| {
                        subst
                            .get(p)
                            .cloned()
                            .unwrap_or_else(|| Type::Opaque(format!("__unbound_{p}")))
                    })
                    .collect();
                Ok(Type::App(id, targs))
            }
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
        "Sys.spawn" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Sys.alive" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
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
        "Net.serveOnce" | "Net.serve" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
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
        "Resource.make" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Io(Box::new(Type::String)))?;
            Ok(Type::Opaque("Resource".into()))
        }
        "Resource.use" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Stream.emit" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.emits" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.eval" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Io(Box::new(Type::String)))?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.concat" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.take" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.drop" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.evalMap" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.filter" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.takeWhile" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.dropWhile" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.find" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Opaque("Stream".into()))
        }
        "Stream.exists" => {
            expect_arity(callee, &arg_tys, 2)?;
            Ok(Type::Io(Box::new(Type::Bool)))
        }
        "Stream.compileToList" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(Type::List)))
        }
        "Stream.drain" => {
            expect_arity(callee, &arg_tys, 1)?;
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
        "Law.signalStr" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::String)
        }
        "Law.signalListLen" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Int)
        }
        "Law.signalListAt" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::String)
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
        "View.aspectRatio" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.fraction" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
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
        "View.showWhen" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "Ui.run" => {
            expect_arity(callee, &arg_tys, 1)?;
            let ok = matches!(
                &arg_tys[0],
                Type::Opaque(n) if n == "View" || n == "TapFn"
            );
            if !ok {
                return Err(TypeError::Msg(format!(
                    "Ui.run expects View or _ => View, got {:?}",
                    arg_tys[0]
                )));
            }
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
                if let Some(p) = f.type_params.iter().find(|p| !subst.contains_key(*p)) {
                    return Err(TypeError::Msg(format!(
                        "cannot infer type parameter {p} for {} — argument types are ambiguous",
                        f.name
                    )));
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

fn check_payload_fields(
    enum_name: &str,
    en: &EnumDef,
    case: &crate::ast::EnumCase,
) -> Result<(), TypeError> {
    for (fname, fty) in &case.fields {
        check_payload_ty(enum_name, en, case, fname, fty)?;
    }
    Ok(())
}

fn check_payload_ty(
    enum_name: &str,
    en: &EnumDef,
    case: &crate::ast::EnumCase,
    fname: &str,
    fty: &Type,
) -> Result<(), TypeError> {
    match fty {
        Type::Int | Type::String | Type::List | Type::Adt(_) => Ok(()),
        Type::App(_, args) => {
            for a in args {
                check_payload_ty(enum_name, en, case, fname, a)?;
            }
            Ok(())
        }
        Type::Var(n) => {
            if en.type_params.iter().any(|p| p == n) {
                Ok(())
            } else {
                Err(TypeError::Msg(format!(
                    "{enum_name}.{} field {fname}: {n} is not a type parameter of {}",
                    case.name, en.name
                )))
            }
        }
        other => Err(TypeError::Msg(format!(
            "{enum_name}.{} field {fname}: payload types are Int, String, List, an ADT, or the enum's type parameter(s), got {other:?}",
            case.name
        ))),
    }
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
            ..
        } => {
            let (en, id) = lookup_enum(enums, enum_name, current_module)?;
            let case = en.cases.iter().find(|c| c.name == *case_name).ok_or_else(|| {
                TypeError::Msg(format!(
                    "unknown case {enum_name}.{case_name} in pattern"
                ))
            })?;
            check_payload_fields(&id, en, case)?;
            let mut subst: HashMap<String, Type> = HashMap::new();
            let mut skipped: Vec<String> = Vec::new();
            match scrut {
                Type::Adt(n) if n == &id && en.type_params.is_empty() => {}
                Type::App(n, targs) if n == &id => {
                    for (p, t) in en.type_params.iter().zip(targs.iter()) {
                        if matches!(t, Type::Opaque(_)) {
                            skipped.push(p.clone());
                        } else {
                            subst.insert(p.clone(), t.clone());
                        }
                    }
                }
                Type::Opaque(_) if en.type_params.is_empty() => {}
                Type::Opaque(_) => {
                    return Err(TypeError::Msg(format!(
                        "cannot match an untyped value against generic enum {enum_name}"
                    )))
                }
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
                let resolved = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
                let fty = erase_vars(&apply_subst(&resolved, &subst), &skipped);
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
        (Type::App(x, a), Type::App(y, b)) => {
            x == y
                && a.len() == b.len()
                && a.iter().zip(b.iter()).all(|(u, v)| types_compat(u, v))
        }
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
        // Opaque carries no information (untyped List elements, ambiguous
        // generic ctors); callers check subst completeness afterwards.
        (Type::Opaque(_), _) | (_, Type::Opaque(_)) => Ok(()),
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
        (Type::App(x, a), Type::App(y, b)) if x == y && a.len() == b.len() => {
            for (u, v) in a.iter().zip(b.iter()) {
                unify_types(u, v, subst)?;
            }
            Ok(())
        }
        (a, b) if types_compat(a, b) => Ok(()),
        (a, b) => Err(TypeError::Msg(format!(
            "type mismatch: expected {a:?}, got {b:?}"
        ))),
    }
}

/// Like `unify_types`, but for generic enum construction: def-scope type
/// parameters (`Var`) may bind — concretization happens at monomorphization.
fn unify_construct(
    pattern: &Type,
    concrete: &Type,
    subst: &mut HashMap<String, Type>,
) -> Result<(), TypeError> {
    match (pattern, concrete) {
        (Type::Opaque(_), _) | (_, Type::Opaque(_)) => Ok(()),
        (Type::Var(n), t) => {
            if let Some(prev) = subst.get(n) {
                if !types_compat(prev, t) {
                    return Err(TypeError::Msg(format!(
                        "type parameter {n} constrained to both {prev:?} and {t:?}"
                    )));
                }
            } else if !contains_unbound(t) {
                // Placeholder-laden types carry no information; the expected
                // type at the construction site fills the parameter instead.
                subst.insert(n.clone(), t.clone());
            }
            Ok(())
        }
        (Type::Io(a), Type::Io(b)) => unify_construct(a, b, subst),
        (Type::App(x, a), Type::App(y, b)) if x == y && a.len() == b.len() => {
            for (u, v) in a.iter().zip(b.iter()) {
                unify_construct(u, v, subst)?;
            }
            Ok(())
        }
        (a, b) if types_compat(a, b) => Ok(()),
        (a, b) => Err(TypeError::Msg(format!(
            "type mismatch: expected {a:?}, got {b:?}"
        ))),
    }
}

fn mono_type_ok(t: &Type) -> bool {
    match t {
        Type::Unit | Type::Int | Type::String | Type::Bool | Type::List | Type::Adt(_) => true,
        Type::App(_, args) => args.iter().all(mono_type_ok),
        _ => false,
    }
}

fn apply_subst(ty: &Type, subst: &HashMap<String, Type>) -> Type {
    match ty {
        Type::Var(n) => subst.get(n).cloned().unwrap_or_else(|| ty.clone()),
        Type::Io(inner) => Type::Io(Box::new(apply_subst(inner, subst))),
        Type::App(n, args) => Type::App(
            n.clone(),
            args.iter().map(|a| apply_subst(a, subst)).collect(),
        ),
        other => other.clone(),
    }
}

/// Replace leftover `Var`s (enum type parameters that stayed unbound, e.g.
/// behind an `Opaque` scrutinee) with opaque types.
fn erase_vars(ty: &Type, names: &[String]) -> Type {
    match ty {
        Type::Var(n) if names.iter().any(|p| p == n) => Type::Opaque(n.clone()),
        Type::Io(inner) => Type::Io(Box::new(erase_vars(inner, names))),
        Type::App(n, args) => Type::App(
            n.clone(),
            args.iter().map(|a| erase_vars(a, names)).collect(),
        ),
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
        Type::App(id, args) => format!(
            "{}_{}",
            id.replace('.', "_"),
            args.iter().map(type_mangle).collect::<Vec<_>>().join("_")
        ),
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
    specialize_enums(program)
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
            // Infer arg types BEFORE rewriting: processed args may reference
            // monomorphized callees the pre-mono index cannot resolve.
            let orig_arg_tys: Option<Vec<Type>> = match funs.resolve(&callee, current_module) {
                Ok(f) if !f.type_params.is_empty() => Some(
                    args.iter()
                        .map(|a| infer(a, enums, funs, methods, current_module, env))
                        .collect::<Result<Vec<_>, _>>()?,
                ),
                _ => None,
            };
            let args = args
                .into_iter()
                .map(|a| mono_expr(a, enums, funs, methods, current_module, env, specialized))
                .collect::<Result<Vec<_>, _>>()?;
            if let Ok(f) = funs.resolve(&callee, current_module) {
                if !f.type_params.is_empty() {
                    let arg_tys = orig_arg_tys.expect("generic call arg types");
                    let mut subst: HashMap<String, Type> = HashMap::new();
                    for (p, a) in f.params.iter().zip(arg_tys.iter()) {
                        let want = resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?;
                        unify_types(&want, a, &mut subst)?;
                    }
                    if let Some(p) = f.type_params.iter().find(|p| !subst.contains_key(*p)) {
                        return Err(TypeError::Msg(format!(
                            "cannot infer type parameter {p} for {} — argument types are ambiguous",
                            f.name
                        )));
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
                                body: subst_node_targs(f.body.clone(), &subst),
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
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let inner = mono_expr(*inner, enums, funs, methods, current_module, env, specialized)?;
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
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let value = mono_expr(*value, enums, funs, methods, current_module, env, specialized)?;
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
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let scrutinee =
                mono_expr(*scrutinee, enums, funs, methods, current_module, env, specialized)?;
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
            type_args,
        } => Ok(Expr::new(
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: args
                    .into_iter()
                    .map(|e| mono_expr(e, enums, funs, methods, current_module, env, specialized))
                    .collect::<Result<Vec<_>, _>>()?,
                type_args,
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

/// Annotate generic enum constructions/patterns with their instantiations
/// (`type_args`). Runs after typecheck in both `check` and `build`; errors when
/// an instantiation is determined neither by constructor args nor by the
/// expected type at the construction site.
pub fn elaborate_generics(mut program: Program) -> Result<Program, TypeError> {
    let enums_owned = program.enums.clone();
    let imports_owned = program.imports.clone();
    let traits_owned = program.traits.clone();
    let impls_owned = program.impls.clone();
    let defs_owned = program.defs.clone();
    let enums = EnumIndex::build(&enums_owned, &imports_owned)
        .map_err(|e| TypeError::Msg(e.to_string()))?;
    let methods = MethodIndex::build(&impls_owned, &traits_owned, &enums)?;
    let funs = FunIndex::build(&defs_owned, &imports_owned, &enums_owned)
        .map_err(|e| TypeError::Msg(e.to_string()))?;
    for d in &mut program.defs {
        let mut env: HashMap<String, Type> = HashMap::new();
        for p in &d.params {
            env.insert(
                p.name.clone(),
                resolve_type_in(&p.ty, &enums, &d.module, &d.type_params)?,
            );
        }
        let expected = resolve_type_in(&d.ret, &enums, &d.module, &d.type_params)?;
        d.body = elaborate_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &enums,
            &funs,
            &methods,
            &d.module,
            &mut env,
            Some(&expected),
            &d.type_params,
        )?;
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    let main_mod = program.main.module.clone();
    program.main.body = elaborate_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &enums,
        &funs,
        &methods,
        &main_mod,
        &mut env,
        Some(&Type::Io(Box::new(Type::Unit))),
        &[],
    )?;
    Ok(program)
}

/// An expected type is usable for filling a construction's instantiation when
/// it carries no opaque holes and its type variables are in scope.
fn usable_expected(ty: &Type, tparams: &[String]) -> bool {
    match ty {
        Type::Opaque(_) => false,
        Type::Var(n) => tparams.iter().any(|p| p == n),
        Type::Io(inner) => usable_expected(inner, tparams),
        Type::App(_, args) => args.iter().all(|a| usable_expected(a, tparams)),
        _ => true,
    }
}

/// True when `t` still holds an undetermined instantiation placeholder.
fn contains_unbound(t: &Type) -> bool {
    match t {
        Type::Opaque(n) => n.starts_with("__unbound_"),
        Type::Io(inner) => contains_unbound(inner),
        Type::App(_, args) => args.iter().any(contains_unbound),
        _ => false,
    }
}

fn check_targs(
    enum_name: &str,
    case_name: &str,
    targs: &[Type],
    tparams: &[String],
    span: &Span,
) -> Result<(), TypeError> {
    for t in targs {
        check_targ(enum_name, case_name, t, tparams, span)?;
    }
    Ok(())
}

fn check_targ(
    enum_name: &str,
    case_name: &str,
    ty: &Type,
    tparams: &[String],
    span: &Span,
) -> Result<(), TypeError> {
    match ty {
        Type::Opaque(n) if n.starts_with("__unbound_") => Err(TypeError::At {
            msg: format!(
                "cannot infer type parameter {} in {enum_name}.{case_name} — bind it through the def's return type or a typed parameter",
                n.trim_start_matches("__unbound_")
            ),
            span: span.clone(),
        }),
        Type::Var(n) if !tparams.iter().any(|p| p == n) => Err(TypeError::At {
            msg: format!(
                "cannot infer type parameter {n} in {enum_name}.{case_name} — bind it through the def's return type or a typed parameter"
            ),
            span: span.clone(),
        }),
        Type::App(_, args) => {
            for a in args {
                check_targ(enum_name, case_name, a, tparams, span)?;
            }
            Ok(())
        }
        _ => Ok(()),
    }
}

fn elaborate_expr(
    expr: Expr,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    expected: Option<&Type>,
    tparams: &[String],
) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            ..
        } => {
            let (en, id) = lookup_enum(enums, &enum_name, current_module)?;
            let case = en
                .cases
                .iter()
                .find(|c| c.name == case_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("unknown case {enum_name}.{case_name}"))
                })?;
            if en.type_params.is_empty() {
                let args = args
                    .into_iter()
                    .map(|a| {
                        elaborate_expr(
                            a,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            None,
                            tparams,
                        )
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                return Ok(Expr::new(
                    ExprKind::AdtConstruct {
                        enum_name,
                        case_name,
                        args,
                        type_args: Vec::new(),
                    },
                    span,
                ));
            }
            // Constructor args determine the instantiation first.
            let mut subst: HashMap<String, Type> = HashMap::new();
            for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                let at = infer(arg, enums, funs, methods, current_module, env)?;
                let want = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
                unify_construct(&want, &at, &mut subst)?;
            }
            // Then the expected type at the construction site.
            if let Some(Type::App(eid, eargs)) = expected {
                if eid == &id && eargs.len() == en.type_params.len() {
                    for (p, ea) in en.type_params.iter().zip(eargs.iter()) {
                        if !subst.contains_key(p) && !matches!(ea, Type::Opaque(_)) {
                            subst.insert(p.clone(), ea.clone());
                        }
                    }
                }
            }
            let targs: Vec<Type> = en
                .type_params
                .iter()
                .map(|p| {
                    subst
                        .get(p)
                        .cloned()
                        .unwrap_or_else(|| Type::Opaque(format!("__unbound_{p}")))
                })
                .collect();
            check_targs(&enum_name, &case_name, &targs, tparams, &span)?;
            let new_args = args
                .into_iter()
                .zip(case.fields.iter())
                .map(|(a, (_fname, fty))| {
                    let want = apply_subst(
                        &resolve_type_in(fty, enums, &en.module, &en.type_params)?,
                        &subst,
                    );
                    let exp = if usable_expected(&want, tparams) {
                        Some(want)
                    } else {
                        None
                    };
                    elaborate_expr(
                        a,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        exp.as_ref(),
                        tparams,
                    )
                })
                .collect::<Result<Vec<_>, _>>()?;
            Ok(Expr::new(
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args: new_args,
                    type_args: targs,
                },
                span,
            ))
        }
        ExprKind::Match { scrutinee, arms } => {
            let scrutinee = elaborate_expr(
                *scrutinee, enums, funs, methods, current_module, env, None, tparams,
            )?;
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound = bind_pattern(&arm.pattern, &st, enums, current_module, env)?;
                let pattern = match &arm.pattern {
                    crate::ast::Pattern::Wildcard => arm.pattern.clone(),
                    crate::ast::Pattern::Adt {
                        enum_name,
                        case_name,
                        binds,
                        ..
                    } => {
                        let (en, id) = lookup_enum(enums, enum_name, current_module)?;
                        let targs: Vec<Type> = if en.type_params.is_empty() {
                            Vec::new()
                        } else {
                            match &st {
                                Type::App(eid, eargs) if eid == &id => eargs.clone(),
                                other => {
                                    return Err(TypeError::Msg(format!(
                                        "pattern {enum_name}.{case_name} does not match scrutinee {other:?}"
                                    )))
                                }
                            }
                        };
                        check_targs(enum_name, case_name, &targs, tparams, &span)?;
                        crate::ast::Pattern::Adt {
                            enum_name: enum_name.clone(),
                            case_name: case_name.clone(),
                            binds: binds.clone(),
                            type_args: targs,
                        }
                    }
                };
                let body = elaborate_expr(
                    arm.body, enums, funs, methods, current_module, env, expected, tparams,
                )?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm { pattern, body });
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
                cond: Box::new(elaborate_expr(
                    *cond, enums, funs, methods, current_module, env, None, tparams,
                )?),
                then_branch: Box::new(elaborate_expr(
                    *then_branch, enums, funs, methods, current_module, env, expected, tparams,
                )?),
                else_branch: Box::new(elaborate_expr(
                    *else_branch, enums, funs, methods, current_module, env, expected, tparams,
                )?),
            },
            span,
        )),
        ExprKind::Call { callee, args } => {
            if let Ok(f) = funs.resolve(&callee, current_module) {
                let mut subst: HashMap<String, Type> = HashMap::new();
                for (a, p) in args.iter().zip(f.params.iter()) {
                    // Lambda args mention binder names that are unbound here;
                    // they contribute nothing to the constructor substitution.
                    if let Ok(at) = infer(a, enums, funs, methods, current_module, env) {
                        let want = resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?;
                        unify_construct(&want, &at, &mut subst)?;
                    }
                }
                let new_args = args
                    .into_iter()
                    .zip(f.params.iter())
                    .map(|(a, p)| {
                        let want = apply_subst(
                            &resolve_type_in(&p.ty, enums, &f.module, &f.type_params)?,
                            &subst,
                        );
                        let exp = if usable_expected(&want, tparams) {
                            Some(want)
                        } else {
                            None
                        };
                        elaborate_expr(
                            a,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            exp.as_ref(),
                            tparams,
                        )
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Expr::new(ExprKind::Call { callee, args: new_args }, span))
            } else {
                let args = args
                    .into_iter()
                    .map(|a| {
                        elaborate_expr(
                            a,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            None,
                            tparams,
                        )
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Expr::new(ExprKind::Call { callee, args }, span))
            }
        }
        ExprKind::Let { name, value, body } => {
            let value = elaborate_expr(
                *value, enums, funs, methods, current_module, env, None, tparams,
            )?;
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let body = elaborate_expr(
                *body, enums, funs, methods, current_module, env, expected, tparams,
            )?;
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
        ExprKind::FlatMap { inner, param, body } => {
            let inner = elaborate_expr(
                *inner, enums, funs, methods, current_module, env, None, tparams,
            )?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let mut old = None;
            if let (Type::Io(inner_t), Some(p)) = (&it, &param) {
                old = env.insert(p.clone(), (**inner_t).clone());
            }
            let body = elaborate_expr(
                *body, enums, funs, methods, current_module, env, expected, tparams,
            )?;
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
        ExprKind::IoPure(inner) => {
            let inner_expected = match expected {
                Some(Type::Io(t)) => Some(&**t),
                _ => None,
            };
            Ok(Expr::new(
                ExprKind::IoPure(Box::new(elaborate_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    inner_expected,
                    tparams,
                )?)),
                span,
            ))
        }
        ExprKind::IoPrintln(inner) => Ok(Expr::new(
            ExprKind::IoPrintln(Box::new(elaborate_expr(
                *inner, enums, funs, methods, current_module, env, None, tparams,
            )?)),
            span,
        )),
        ExprKind::IoSleep(inner) => Ok(Expr::new(
            ExprKind::IoSleep(Box::new(elaborate_expr(
                *inner, enums, funs, methods, current_module, env, None, tparams,
            )?)),
            span,
        )),
        ExprKind::IoFail(inner) => Ok(Expr::new(
            ExprKind::IoFail(Box::new(elaborate_expr(
                *inner, enums, funs, methods, current_module, env, None, tparams,
            )?)),
            span,
        )),
        ExprKind::HandleErrorWith { inner, body } => Ok(Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(elaborate_expr(
                    *inner, enums, funs, methods, current_module, env, expected, tparams,
                )?),
                body: Box::new(elaborate_expr(
                    *body, enums, funs, methods, current_module, env, expected, tparams,
                )?),
            },
            span,
        )),
        ExprKind::Attempt { inner } => Ok(Expr::new(
            ExprKind::Attempt {
                inner: Box::new(elaborate_expr(
                    *inner, enums, funs, methods, current_module, env, None, tparams,
                )?),
            },
            span,
        )),
        ExprKind::IoRace { left, right } => Ok(Expr::new(
            ExprKind::IoRace {
                left: Box::new(elaborate_expr(
                    *left, enums, funs, methods, current_module, env, expected, tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right, enums, funs, methods, current_module, env, expected, tparams,
                )?),
            },
            span,
        )),
        ExprKind::IoBoth { left, right } => Ok(Expr::new(
            ExprKind::IoBoth {
                left: Box::new(elaborate_expr(
                    *left, enums, funs, methods, current_module, env, None, tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right, enums, funs, methods, current_module, env, None, tparams,
                )?),
            },
            span,
        )),
        ExprKind::Field { base, field } => Ok(Expr::new(
            ExprKind::Field {
                base: Box::new(elaborate_expr(
                    *base, enums, funs, methods, current_module, env, None, tparams,
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
                receiver: Box::new(elaborate_expr(
                    *receiver, enums, funs, methods, current_module, env, None, tparams,
                )?),
                method,
                args: args
                    .into_iter()
                    .map(|a| {
                        elaborate_expr(
                            a,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            None,
                            tparams,
                        )
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::ListLit { elems } => Ok(Expr::new(
            ExprKind::ListLit {
                elems: elems
                    .into_iter()
                    .map(|e| {
                        elaborate_expr(
                            e,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            None,
                            tparams,
                        )
                    })
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
                            elaborate_expr(
                                e,
                                enums,
                                funs,
                                methods,
                                current_module,
                                env,
                                None,
                                tparams,
                            )?,
                        )),
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Binary { op, left, right } => Ok(Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(elaborate_expr(
                    *left, enums, funs, methods, current_module, env, None, tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right, enums, funs, methods, current_module, env, None, tparams,
                )?),
            },
            span,
        )),
        ExprKind::Lambda { param, body } => Ok(Expr::new(
            ExprKind::Lambda {
                param,
                body: Box::new(elaborate_expr(
                    *body, enums, funs, methods, current_module, env, None, tparams,
                )?),
            },
            span,
        )),
        ExprKind::For { .. } => panic!("internal: unlowered `for` in elaboration"),
        other => Ok(Expr::new(other, span)),
    }
}

/// Apply a def specialization's substitution to the `type_args` stored on
/// generic enum nodes inside a cloned template body.
fn subst_node_targs(expr: Expr, subst: &HashMap<String, Type>) -> Expr {
    let span = expr.span.clone();
    let kind = match expr.kind {
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args: args.into_iter().map(|a| subst_node_targs(a, subst)).collect(),
            type_args: type_args.iter().map(|t| apply_subst(t, subst)).collect(),
        },
        ExprKind::Match { scrutinee, arms } => ExprKind::Match {
            scrutinee: Box::new(subst_node_targs(*scrutinee, subst)),
            arms: arms
                .into_iter()
                .map(|a| crate::ast::MatchArm {
                    pattern: match a.pattern {
                        crate::ast::Pattern::Adt {
                            enum_name,
                            case_name,
                            binds,
                            type_args,
                        } => crate::ast::Pattern::Adt {
                            enum_name,
                            case_name,
                            binds,
                            type_args: type_args.iter().map(|t| apply_subst(t, subst)).collect(),
                        },
                        p => p,
                    },
                    body: subst_node_targs(a.body, subst),
                })
                .collect(),
        },
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => ExprKind::If {
            cond: Box::new(subst_node_targs(*cond, subst)),
            then_branch: Box::new(subst_node_targs(*then_branch, subst)),
            else_branch: Box::new(subst_node_targs(*else_branch, subst)),
        },
        ExprKind::Call { callee, args } => ExprKind::Call {
            callee,
            args: args.into_iter().map(|a| subst_node_targs(a, subst)).collect(),
        },
        ExprKind::Let { name, value, body } => ExprKind::Let {
            name,
            value: Box::new(subst_node_targs(*value, subst)),
            body: Box::new(subst_node_targs(*body, subst)),
        },
        ExprKind::FlatMap { inner, param, body } => ExprKind::FlatMap {
            inner: Box::new(subst_node_targs(*inner, subst)),
            param,
            body: Box::new(subst_node_targs(*body, subst)),
        },
        ExprKind::IoPrintln(e) => ExprKind::IoPrintln(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoSleep(e) => ExprKind::IoSleep(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoFail(e) => ExprKind::IoFail(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoPure(e) => ExprKind::IoPure(Box::new(subst_node_targs(*e, subst))),
        ExprKind::HandleErrorWith { inner, body } => ExprKind::HandleErrorWith {
            inner: Box::new(subst_node_targs(*inner, subst)),
            body: Box::new(subst_node_targs(*body, subst)),
        },
        ExprKind::Attempt { inner } => ExprKind::Attempt {
            inner: Box::new(subst_node_targs(*inner, subst)),
        },
        ExprKind::IoRace { left, right } => ExprKind::IoRace {
            left: Box::new(subst_node_targs(*left, subst)),
            right: Box::new(subst_node_targs(*right, subst)),
        },
        ExprKind::IoBoth { left, right } => ExprKind::IoBoth {
            left: Box::new(subst_node_targs(*left, subst)),
            right: Box::new(subst_node_targs(*right, subst)),
        },
        ExprKind::Field { base, field } => ExprKind::Field {
            base: Box::new(subst_node_targs(*base, subst)),
            field,
        },
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => ExprKind::MethodCall {
            receiver: Box::new(subst_node_targs(*receiver, subst)),
            method,
            args: args.into_iter().map(|a| subst_node_targs(a, subst)).collect(),
        },
        ExprKind::ListLit { elems } => ExprKind::ListLit {
            elems: elems.into_iter().map(|e| subst_node_targs(e, subst)).collect(),
        },
        ExprKind::Interpolate { parts } => ExprKind::Interpolate {
            parts: parts
                .into_iter()
                .map(|p| match p {
                    crate::ast::InterpPart::Lit(s) => crate::ast::InterpPart::Lit(s),
                    crate::ast::InterpPart::Expr(e) => {
                        crate::ast::InterpPart::Expr(subst_node_targs(e, subst))
                    }
                })
                .collect(),
        },
        ExprKind::Binary { op, left, right } => ExprKind::Binary {
            op,
            left: Box::new(subst_node_targs(*left, subst)),
            right: Box::new(subst_node_targs(*right, subst)),
        },
        ExprKind::Lambda { param, body } => ExprKind::Lambda {
            param,
            body: Box::new(subst_node_targs(*body, subst)),
        },
        other => other,
    };
    Expr::new(kind, span)
}

fn mono_enum_name(en: &EnumDef, args: &[Type]) -> String {
    let mut parts = vec![format!("__gen_{}", en.name)];
    for a in args {
        parts.push(type_mangle(a));
    }
    parts.join("_")
}

/// Collect `(template-id, args)` instantiations from a resolved type.
fn collect_apps_in_type(ty: &Type, out: &mut Vec<(String, Vec<Type>)>) {
    match ty {
        Type::App(id, args) => {
            out.push((id.clone(), args.clone()));
            for a in args {
                collect_apps_in_type(a, out);
            }
        }
        Type::Io(inner) => collect_apps_in_type(inner, out),
        _ => {}
    }
}

/// Collect instantiations stored on elaborated construct/pattern nodes.
fn collect_node_targs(expr: &Expr, out: &mut Vec<(String, Vec<Type>)>) {
    match &expr.kind {
        ExprKind::AdtConstruct {
            enum_name,
            args,
            type_args,
            ..
        } => {
            if !type_args.is_empty() {
                out.push((enum_name.clone(), type_args.clone()));
                for t in type_args {
                    collect_apps_in_type(t, out);
                }
            }
            for a in args {
                collect_node_targs(a, out);
            }
        }
        ExprKind::Match { scrutinee, arms } => {
            collect_node_targs(scrutinee, out);
            for a in arms {
                if let crate::ast::Pattern::Adt {
                    enum_name,
                    type_args,
                    ..
                } = &a.pattern
                {
                    if !type_args.is_empty() {
                        out.push((enum_name.clone(), type_args.clone()));
                        for t in type_args {
                            collect_apps_in_type(t, out);
                        }
                    }
                }
                collect_node_targs(&a.body, out);
            }
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            collect_node_targs(cond, out);
            collect_node_targs(then_branch, out);
            collect_node_targs(else_branch, out);
        }
        ExprKind::Call { args, .. } => {
            for a in args {
                collect_node_targs(a, out);
            }
        }
        ExprKind::Let { value, body, .. } => {
            collect_node_targs(value, out);
            collect_node_targs(body, out);
        }
        ExprKind::FlatMap { inner, body, .. } => {
            collect_node_targs(inner, out);
            collect_node_targs(body, out);
        }
        ExprKind::IoPrintln(e)
        | ExprKind::IoSleep(e)
        | ExprKind::IoFail(e)
        | ExprKind::IoPure(e)
        | ExprKind::Attempt { inner: e } => collect_node_targs(e, out),
        ExprKind::HandleErrorWith { inner, body } => {
            collect_node_targs(inner, out);
            collect_node_targs(body, out);
        }
        ExprKind::IoRace { left, right } | ExprKind::IoBoth { left, right } => {
            collect_node_targs(left, out);
            collect_node_targs(right, out);
        }
        ExprKind::Field { base, .. } => collect_node_targs(base, out),
        ExprKind::MethodCall { receiver, args, .. } => {
            collect_node_targs(receiver, out);
            for a in args {
                collect_node_targs(a, out);
            }
        }
        ExprKind::ListLit { elems } => {
            for e in elems {
                collect_node_targs(e, out);
            }
        }
        ExprKind::Interpolate { parts } => {
            for p in parts {
                if let crate::ast::InterpPart::Expr(e) = p {
                    collect_node_targs(e, out);
                }
            }
        }
        ExprKind::Binary { left, right, .. } => {
            collect_node_targs(left, out);
            collect_node_targs(right, out);
        }
        ExprKind::Lambda { body, .. } => collect_node_targs(body, out),
        _ => {}
    }
}

/// Rewrite elaborated construct/pattern nodes to their cloned enum ids.
fn rewrite_enum_refs(
    expr: Expr,
    clones: &HashMap<(String, Vec<Type>), String>,
) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    let lookup = |enum_name: &str, type_args: &[Type]| -> Result<String, TypeError> {
        clones
            .get(&(enum_name.to_string(), type_args.to_vec()))
            .cloned()
            .ok_or_else(|| {
                TypeError::Msg(format!(
                    "internal: missing enum clone for {enum_name}{type_args:?}"
                ))
            })
    };
    let kind = match expr.kind {
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => {
            let args = args
                .into_iter()
                .map(|a| rewrite_enum_refs(a, clones))
                .collect::<Result<Vec<_>, _>>()?;
            if type_args.is_empty() {
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args,
                    type_args,
                }
            } else {
                ExprKind::AdtConstruct {
                    enum_name: lookup(&enum_name, &type_args)?,
                    case_name,
                    args,
                    type_args: Vec::new(),
                }
            }
        }
        ExprKind::Match { scrutinee, arms } => ExprKind::Match {
            scrutinee: Box::new(rewrite_enum_refs(*scrutinee, clones)?),
            arms: arms
                .into_iter()
                .map(|a| {
                    let pattern = match a.pattern {
                        crate::ast::Pattern::Adt {
                            enum_name,
                            case_name,
                            binds,
                            type_args,
                        } if !type_args.is_empty() => crate::ast::Pattern::Adt {
                            enum_name: lookup(&enum_name, &type_args)?,
                            case_name,
                            binds,
                            type_args: Vec::new(),
                        },
                        p => p,
                    };
                    Ok(crate::ast::MatchArm {
                        pattern,
                        body: rewrite_enum_refs(a.body, clones)?,
                    })
                })
                .collect::<Result<Vec<_>, TypeError>>()?,
        },
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => ExprKind::If {
            cond: Box::new(rewrite_enum_refs(*cond, clones)?),
            then_branch: Box::new(rewrite_enum_refs(*then_branch, clones)?),
            else_branch: Box::new(rewrite_enum_refs(*else_branch, clones)?),
        },
        ExprKind::Call { callee, args } => ExprKind::Call {
            callee,
            args: args
                .into_iter()
                .map(|a| rewrite_enum_refs(a, clones))
                .collect::<Result<Vec<_>, _>>()?,
        },
        ExprKind::Let { name, value, body } => ExprKind::Let {
            name,
            value: Box::new(rewrite_enum_refs(*value, clones)?),
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        ExprKind::FlatMap { inner, param, body } => ExprKind::FlatMap {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
            param,
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        ExprKind::IoPrintln(e) => ExprKind::IoPrintln(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoSleep(e) => ExprKind::IoSleep(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoFail(e) => ExprKind::IoFail(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoPure(e) => ExprKind::IoPure(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::HandleErrorWith { inner, body } => ExprKind::HandleErrorWith {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        ExprKind::Attempt { inner } => ExprKind::Attempt {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
        },
        ExprKind::IoRace { left, right } => ExprKind::IoRace {
            left: Box::new(rewrite_enum_refs(*left, clones)?),
            right: Box::new(rewrite_enum_refs(*right, clones)?),
        },
        ExprKind::IoBoth { left, right } => ExprKind::IoBoth {
            left: Box::new(rewrite_enum_refs(*left, clones)?),
            right: Box::new(rewrite_enum_refs(*right, clones)?),
        },
        ExprKind::Field { base, field } => ExprKind::Field {
            base: Box::new(rewrite_enum_refs(*base, clones)?),
            field,
        },
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => ExprKind::MethodCall {
            receiver: Box::new(rewrite_enum_refs(*receiver, clones)?),
            method,
            args: args
                .into_iter()
                .map(|a| rewrite_enum_refs(a, clones))
                .collect::<Result<Vec<_>, _>>()?,
        },
        ExprKind::ListLit { elems } => ExprKind::ListLit {
            elems: elems
                .into_iter()
                .map(|e| rewrite_enum_refs(e, clones))
                .collect::<Result<Vec<_>, _>>()?,
        },
        ExprKind::Interpolate { parts } => ExprKind::Interpolate {
            parts: parts
                .into_iter()
                .map(|p| match p {
                    crate::ast::InterpPart::Lit(s) => Ok(crate::ast::InterpPart::Lit(s)),
                    crate::ast::InterpPart::Expr(e) => {
                        Ok(crate::ast::InterpPart::Expr(rewrite_enum_refs(e, clones)?))
                    }
                })
                .collect::<Result<Vec<_>, TypeError>>()?,
        },
        ExprKind::Binary { op, left, right } => ExprKind::Binary {
            op,
            left: Box::new(rewrite_enum_refs(*left, clones)?),
            right: Box::new(rewrite_enum_refs(*right, clones)?),
        },
        ExprKind::Lambda { param, body } => ExprKind::Lambda {
            param,
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        other => other,
    };
    Ok(Expr::new(kind, span))
}

/// Replace applied types with their cloned enum ids (post-specialization).
fn concretize_type(
    ty: &Type,
    clones: &HashMap<(String, Vec<Type>), String>,
) -> Result<Type, TypeError> {
    match ty {
        Type::App(id, args) => {
            let cid = clones
                .get(&(id.clone(), args.clone()))
                .ok_or_else(|| {
                    TypeError::Msg(format!("internal: missing enum clone for {id}{args:?}"))
                })?;
            Ok(Type::Adt(cid.clone()))
        }
        Type::Io(inner) => Ok(Type::Io(Box::new(concretize_type(inner, clones)?))),
        other => Ok(other.clone()),
    }
}

/// Clone generic enums per concrete instantiation; erase `App` everywhere.
/// Runs at the end of monomorphization so codegen only sees concrete enums.
fn specialize_enums(mut program: Program) -> Result<Program, TypeError> {
    let enums_owned = program.enums.clone();
    let imports_owned = program.imports.clone();
    let enums = EnumIndex::build(&enums_owned, &imports_owned)
        .map_err(|e| TypeError::Msg(e.to_string()))?;

    let mut instances: Vec<(String, Vec<Type>)> = Vec::new();
    for d in &program.defs {
        for p in &d.params {
            let r = resolve_type(&p.ty, &enums, &d.module)?;
            collect_apps_in_type(&r, &mut instances);
        }
        let r = resolve_type(&d.ret, &enums, &d.module)?;
        collect_apps_in_type(&r, &mut instances);
        collect_node_targs(&d.body, &mut instances);
    }
    collect_node_targs(&program.main.body, &mut instances);

    let mut clones: HashMap<(String, Vec<Type>), String> = HashMap::new();
    let mut clone_defs: Vec<EnumDef> = Vec::new();
    let mut i = 0;
    while i < instances.len() {
        let (id, args) = instances[i].clone();
        i += 1;
        if clones.contains_key(&(id.clone(), args.clone())) {
            continue;
        }
        if args.iter().any(|a| !mono_type_ok(a)) {
            return Err(TypeError::Msg(format!(
                "internal: non-concrete instantiation {id}{args:?} survived monomorphization"
            )));
        }
        let en = enums.resolve(&id, "").map_err(|e| {
            TypeError::Msg(format!("internal: unknown generic enum {id}: {e}"))
        })?;
        let subst: HashMap<String, Type> = en
            .type_params
            .iter()
            .cloned()
            .zip(args.iter().cloned())
            .collect();
        let mangled = mono_enum_name(en, &args);
        let clone_id = crate::resolve::enum_id(&en.module, &mangled);
        let mut cases = Vec::new();
        for case in &en.cases {
            let mut fields = Vec::new();
            for (fname, fty) in &case.fields {
                let r = resolve_type_in(fty, &enums, &en.module, &en.type_params)?;
                let s = apply_subst(&r, &subst);
                collect_apps_in_type(&s, &mut instances);
                fields.push((fname.clone(), s));
            }
            cases.push(crate::ast::EnumCase {
                name: case.name.clone(),
                fields,
            });
        }
        clones.insert((id, args), clone_id);
        clone_defs.push(EnumDef {
            module: en.module.clone(),
            name: mangled,
            type_params: Vec::new(),
            cases,
            is_record: en.is_record,
        });
    }

    if clones.is_empty() {
        return Ok(program);
    }

    for d in &mut program.defs {
        for p in &mut d.params {
            let r = resolve_type(&p.ty, &enums, &d.module)?;
            p.ty = concretize_type(&r, &clones)?;
        }
        let r = resolve_type(&d.ret, &enums, &d.module)?;
        d.ret = concretize_type(&r, &clones)?;
        d.body = rewrite_enum_refs(std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)), &clones)?;
    }
    program.main.body = rewrite_enum_refs(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &clones,
    )?;
    for c in &mut clone_defs {
        for case in &mut c.cases {
            for (_fname, fty) in &mut case.fields {
                *fty = concretize_type(fty, &clones)?;
            }
        }
    }
    program.enums.retain(|e| e.type_params.is_empty());
    program.enums.extend(clone_defs);
    Ok(program)
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
    fn typechecks_resource_make_use() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
    res2 = Resource.make(IO.pure("tok2"), t => IO.println(t))
    _ <- Resource.use(res2, t => IO.fail("boom")).handleErrorWith(_ => IO.println("recovered"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Resource.make/use should typecheck");
    }

    #[test]
    fn typechecks_stream_emit_evalmap_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.concat(
      Stream.evalMap(Stream.emits(["a", "b"]), x => IO.pure(Str.concat(x, "!"))),
      Stream.eval(IO.pure("c"))
    )
    xs <- Stream.compileToList(s)
    _ <- IO.println(List.join(xs, ","))
    _ <- Stream.drain(Stream.evalMap(Stream.emit("d"), x => IO.println(s"drain:$x")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.emit/evalMap/compileToList should typecheck");
    }

    #[test]
    fn typechecks_stream_take() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.take(Stream.emits(["a", "b", "c"]), 2))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.take should typecheck");
    }

    #[test]
    fn typechecks_stream_drop() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.drop(Stream.emits(["a", "b", "c"]), 1))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.drop should typecheck");
    }

    #[test]
    fn typechecks_stream_filter() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.filter(Stream.emits(["a", "", "b"]), x => Str.len(x) > 0))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.filter should typecheck");
    }

    #[test]
    fn typechecks_stream_map() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.map(Stream.emits(["a", "b"]), x => Str.concat(x, "!")))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.map should typecheck");
    }

    #[test]
    fn typechecks_stream_takewhile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.takeWhile(Stream.emits(["a", "b", "", "c"]), x => Str.len(x) > 0))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.takeWhile should typecheck");
    }

    #[test]
    fn typechecks_stream_dropwhile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.dropWhile(Stream.emits(["", "", "a", "b"]), x => Str.len(x) == 0))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.dropWhile should typecheck");
    }

    #[test]
    fn typechecks_stream_find() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.find(Stream.emits(["", "a", "b"]), x => Str.len(x) > 0))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.find should typecheck");
    }

    #[test]
    fn typechecks_stream_exists() {
        let src = r#"@main def main: IO[Unit] =
  for {
    hit <- Stream.exists(Stream.emits(["", "a", "b"]), x => Str.len(x) > 0)
    _ <- IO.println(Str.fromInt(hit))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.exists should typecheck");
    }

    #[test]
    fn typechecks_law_signal_list_at() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Law.signalListAt(0, 0))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Law.signalListAt should typecheck");
    }

    #[test]
    fn typechecks_net_serve_once() {
        let src = r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.println(s"served:$path"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Net.serveOnce should typecheck");
    }

    #[test]
    fn typechecks_net_serve() {
        let src = r#"@main def main: IO[Unit] =
  Net.serve(8080, path => IO.println(s"served:$path"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Net.serve should typecheck");
    }

    #[test]
    fn typechecks_sys_spawn_alive() {
        let src = r#"@main def main: IO[Unit] =
  Sys.spawn("true").flatMap(pid => Sys.alive(pid).flatMap(_ => IO.pure(())))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Sys.spawn/alive should typecheck");
    }

    #[test]
    fn typechecks_ui_run_rebuild_lambda() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.text("x"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Ui.run factory should typecheck");
    }

    #[test]
    fn rejects_ui_run_non_view() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(1)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Ui.run expects View"),
            "expected Ui.run type error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_view_add_child() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(View.addChild(View.column(), View.text("x")))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("unknown function View.addChild")
                || err.message().contains("View.addChild"),
            "expected unknown View.addChild, got {}",
            err.message()
        );
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

    #[test]
    fn typechecks_generic_enum_construct_and_match() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] = Opt.Some(1) match {
  case Opt.Some(n) => IO.println(Str.fromInt(n + 1))
  case Opt.None => IO.println("none")
}
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("generic enum construct/match should typecheck");
    }

    #[test]
    fn typechecks_generic_def_over_generic_enum() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def getOrElse[T](o: Opt[T], default: T): T = o match {
  case Opt.Some(x) => x
  case Opt.None => default
}
@main def main: IO[Unit] = IO.println(Str.fromInt(getOrElse(Opt.Some(1), 0)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("generic def over generic enum should typecheck");
    }

    #[test]
    fn typechecks_multi_param_either() {
        let src = r#"
enum Either[L, R]:
  case Left(x: L)
  case Right(y: R)
def describe(e: Either[Int, String]): String = e match {
  case Either.Left(n) => Str.fromInt(n)
  case Either.Right(s) => s
}
@main def main: IO[Unit] = IO.println(describe(Either.Left(1)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("multi-param Either should typecheck");
    }

    #[test]
    fn typechecks_generic_record_field_access() {
        let src = r#"
record Box[T](x: T)
def unbox[T](b: Box[T]): T = b.x
@main def main: IO[Unit] = IO.println(Str.fromInt(unbox(Box(3))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("generic record field access should typecheck");
    }

    #[test]
    fn rejects_wrong_arity_application() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def f(o: Opt[Int, String]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expects 1 type argument(s)"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn rejects_application_of_non_generic_enum() {
        let src = r#"
enum Color:
  case Red
def f(c: Color[Int]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("is not generic"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn rejects_unknown_applied_enum() {
        let src = r#"
def f(x: Nope[Int]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("unknown enum Nope"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn rejects_payload_var_outside_type_params() {
        let src = r#"
enum Bad[T]:
  case C(x: U)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("unknown enum U"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn rejects_opaque_scrutinee_against_generic_pattern() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] = List.head(["a"]) match {
  case Opt.Some(n) => IO.println("some")
  case Opt.None => IO.println("none")
}
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("untyped value against generic enum"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn rejects_mismatched_generic_construct_arg() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def f(o: Opt[Int]): Int = 1
@main def main: IO[Unit] = IO.println(Str.fromInt(f(Opt.Some("s"))))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("mismatch"),
            "unexpected: {}",
            err.message()
        );
    }

    fn gen_pipeline(src: &str) -> Program {
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        monomorphize(p).expect("monomorphize")
    }

    fn mentions_app(ty: &Type) -> bool {
        match ty {
            Type::App(_, _) => true,
            Type::Io(inner) => mentions_app(inner),
            _ => false,
        }
    }

    #[test]
    fn mono_clones_generic_enum_per_instantiation() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(Opt.Some(1) match {
      case Opt.Some(n) => n
      case Opt.None => 0
    }))
    _ <- IO.println(Opt.Some("s") match {
      case Opt.Some(x) => x
      case Opt.None => "n"
    })
  } yield ()
"#;
        let p = gen_pipeline(src);
        assert!(p.enums.iter().all(|e| e.type_params.is_empty()));
        assert!(
            p.enums.iter().any(|e| e.name.contains("__gen_Opt_Int")),
            "missing Opt[Int] clone: {:?}",
            p.enums.iter().map(|e| &e.name).collect::<Vec<_>>()
        );
        assert!(
            p.enums.iter().any(|e| e.name.contains("__gen_Opt_String")),
            "missing Opt[String] clone"
        );
        for d in &p.defs {
            for prm in &d.params {
                assert!(!mentions_app(&prm.ty));
            }
            assert!(!mentions_app(&d.ret));
        }
    }

    #[test]
    fn mono_rewrites_construct_to_cloned_enum() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] = Opt.Some(1) match {
  case Opt.Some(n) => IO.println(Str.fromInt(n))
  case Opt.None => IO.println("none")
}
"#;
        let p = gen_pipeline(src);
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, .. } => match &scrutinee.kind {
                ExprKind::AdtConstruct {
                    enum_name,
                    type_args,
                    ..
                } => {
                    assert!(type_args.is_empty(), "type_args must be cleared");
                    assert!(
                        enum_name.contains("__gen_Opt_Int"),
                        "construct not rewritten: {enum_name}"
                    );
                }
                other => panic!("expected AdtConstruct, got {other:?}"),
            },
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn elaborate_infers_nullary_case_from_def_ret() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def noneInt(): Opt[Int] = Opt.None
@main def main: IO[Unit] = noneInt() match {
  case Opt.Some(n) => IO.println(Str.fromInt(n))
  case Opt.None => IO.println("none")
}
"#;
        let p = gen_pipeline(src);
        assert!(p.enums.iter().any(|e| e.name.contains("__gen_Opt_Int")));
    }

    #[test]
    fn elaborate_errors_on_uninferrable_nullary() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    x = Opt.None
  } yield IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("typecheck");
        let err = elaborate_generics(p).unwrap_err();
        assert!(
            err.message().contains("cannot infer type parameter T"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn mono_specializes_generic_record() {
        let src = r#"
record Box[T](x: T)
def unbox[T](b: Box[T]): T = b.x
@main def main: IO[Unit] = IO.println(Str.fromInt(unbox(Box(3))))
"#;
        let p = gen_pipeline(src);
        assert!(p.enums.iter().any(|e| e.name.contains("__gen_Box_Int")));
        assert!(p.enums.iter().all(|e| e.type_params.is_empty()));
    }

    #[test]
    fn mono_handles_nested_application() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def wrap2(x: Int): Opt[Opt[Int]] = Opt.Some(Opt.Some(x))
@main def main: IO[Unit] = wrap2(1) match {
  case Opt.Some(inner) => inner match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("inner-none")
  }
  case Opt.None => IO.println("outer-none")
}
"#;
        let p = gen_pipeline(src);
        assert!(
            p.enums.iter().any(|e| e.name.contains("__gen_Opt_Int")),
            "missing inner clone"
        );
        assert!(
            p.enums.iter().any(|e| e.name.contains("__gen_Opt_Opt")),
            "missing nested clone: {:?}",
            p.enums.iter().map(|e| &e.name).collect::<Vec<_>>()
        );
    }

    #[test]
    fn mono_handles_generic_call_inside_for_binder() {
        // A specialized call inside a `<-` binder must not be re-inferred
        // against the pre-mono index (mangled names are not in it).
        let src = r#"
def id[T](x: T): T = x
@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(id(7)))
    _ <- IO.println("done")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        monomorphize(p).expect("monomorphize");
    }

    #[test]
    fn mono_multi_param_either_clone() {        let src = r#"
enum Either[L, R]:
  case Left(x: L)
  case Right(y: R)
def describe(e: Either[Int, String]): String = e match {
  case Either.Left(n) => Str.fromInt(n)
  case Either.Right(s) => s
}
@main def main: IO[Unit] = IO.println(describe(Either.Left(1)))
"#;
        let p = gen_pipeline(src);
        assert!(
            p.enums.iter().any(|e| e.name.contains("__gen_Either_Int_String")),
            "missing Either clone: {:?}",
            p.enums.iter().map(|e| &e.name).collect::<Vec<_>>()
        );
    }
}

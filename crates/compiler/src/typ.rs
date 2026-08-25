use crate::ast::{
    BinOp, EnumDef, Expr, ExprKind, FunDef, ImplDef, Param, Program, TraitDef, Type, TypeAlias,
    UnOp,
};
use crate::hover::kit_sig;
use crate::resolve::{enum_bare_name, enum_id, EnumIndex, FunIndex, ResolveError};
use crate::signature::param_names_from_label;
use crate::span::Span;
use std::collections::{HashMap, HashSet};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TypeError {
    #[error("type error: {0}")]
    Msg(String),
    #[error("type error: {msg}")]
    At { msg: String, span: Span },
}

impl TypeError {
    #[cfg(test)]
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
fn impl_method_name(trait_name: &str, for_type: &str, method: &str) -> String {
    format!("__impl_{trait_name}_{for_type}_{method}")
}

/// Mangled def name for a type method (`record Box[T]: def get()` / `enum Opt[T]: def getOrElse()` → `__rec_*`).
fn rec_method_name(for_type: &str, method: &str) -> String {
    format!("__rec_{for_type}_{method}")
}

#[derive(Debug, Clone)]
struct MethodEntry {
    mangled: String,
    /// Parameter types after `self`, already resolved.
    params: Vec<Type>,
    ret: Type,
    /// Type parameters of a generic receiver (`Box[T]`); empty for non-generic impls.
    type_params: Vec<String>,
}

#[derive(Debug, Default)]
struct MethodIndex {
    /// `(type_id, method)` → entry. One method name per type.
    by_type_method: HashMap<(String, String), MethodEntry>,
}

/// Instantiate `trait Get[T]` from explicit `impl Get[Int]` args, or from the target's params.
fn impl_trait_subst(
    tr: &TraitDef,
    im: &ImplDef,
    for_en: &EnumDef,
    enums: &EnumIndex<'_>,
) -> Result<HashMap<String, Type>, TypeError> {
    if !im.trait_args.is_empty() {
        if tr.type_params.is_empty() {
            return Err(TypeError::Msg(format!(
                "impl {} for {}: trait is not generic",
                im.trait_name, im.for_type
            )));
        }
        if im.trait_args.len() != tr.type_params.len() {
            return Err(TypeError::Msg(format!(
                "impl {} for {}: trait expects {} type argument(s)",
                im.trait_name,
                im.for_type,
                tr.type_params.len()
            )));
        }
        let mut subst = HashMap::new();
        for (p, arg) in tr.type_params.iter().zip(im.trait_args.iter()) {
            subst.insert(
                p.clone(),
                resolve_type_in(arg, enums, &im.module, &for_en.type_params)?,
            );
        }
        return Ok(subst);
    }
    if !tr.type_params.is_empty() && tr.type_params.len() != for_en.type_params.len() {
        return Err(TypeError::Msg(format!(
            "impl {} for {}: trait expects {} type argument(s)",
            im.trait_name,
            im.for_type,
            tr.type_params.len()
        )));
    }
    Ok(trait_receiver_subst(&tr.type_params, &for_en.type_params))
}

fn trait_receiver_subst(trait_tparams: &[String], for_tparams: &[String]) -> HashMap<String, Type> {
    trait_tparams
        .iter()
        .zip(for_tparams.iter())
        .map(|(a, b)| (a.clone(), Type::Var(b.clone())))
        .collect()
}

impl MethodIndex {
    fn build(
        impls: &[ImplDef],
        traits: &[TraitDef],
        enums: &EnumIndex<'_>,
        enum_defs: &[EnumDef],
    ) -> Result<Self, TypeError> {
        let mut by_type_method = HashMap::new();
        for im in impls {
            let tr = traits
                .iter()
                .find(|t| t.name == im.trait_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("impl of unknown trait {}", im.trait_name))
                })?;
            let for_en = enums.resolve(&im.for_type, &im.module).map_err(|e| {
                TypeError::Msg(format!(
                    "impl {} for {}: unknown type: {e}",
                    im.trait_name, im.for_type
                ))
            })?;
            let for_id = crate::resolve::enum_id(&for_en.module, &for_en.name);
            let trait_subst = impl_trait_subst(tr, im, for_en, enums)?;
            for method in &im.methods {
                let tm = tr
                    .methods
                    .iter()
                    .find(|m| m.name == method.name)
                    .ok_or_else(|| {
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
                    let at = apply_subst(
                        &resolve_type_in(&a.ty, enums, &tr.module, &tr.type_params)?,
                        &trait_subst,
                    );
                    let bt = resolve_type_in(&b.ty, enums, &im.module, &for_en.type_params)?;
                    if !types_compat(&at, &bt) {
                        return Err(TypeError::Msg(format!(
                            "impl {} for {}.{}: param type mismatch",
                            im.trait_name, im.for_type, method.name
                        )));
                    }
                }
                let want_ret = apply_subst(
                    &resolve_type_in(&tm.ret, enums, &tr.module, &tr.type_params)?,
                    &trait_subst,
                );
                let got_ret = resolve_type_in(&method.ret, enums, &im.module, &for_en.type_params)?;
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
                    .map(|p| resolve_type_in(&p.ty, enums, &im.module, &for_en.type_params))
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
                        type_params: for_en.type_params.clone(),
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
        for en in enum_defs {
            let for_id = crate::resolve::enum_id(&en.module, &en.name);
            for method in &en.methods {
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
                    .map(|p| resolve_type_in(&p.ty, enums, &en.module, &en.type_params))
                    .collect();
                let ret = resolve_type_in(&method.ret, enums, &en.module, &en.type_params)?;
                by_type_method.insert(
                    key,
                    MethodEntry {
                        mangled: rec_method_name(&en.name, &method.name),
                        params: params?,
                        ret,
                        type_params: en.type_params.clone(),
                    },
                );
            }
        }
        Ok(Self { by_type_method })
    }

    fn lookup(&self, type_id: &str, method: &str) -> Result<&MethodEntry, TypeError> {
        self.by_type_method
            .get(&(type_id.to_string(), method.to_string()))
            .ok_or_else(|| TypeError::Msg(format!("no impl method {method} for type {type_id}")))
    }
}

/// Shared pass setup: clone the program tables, build the three indexes, then
/// hand `&mut Program` back to the closure so the pass can rewrite in place.
fn with_pass_indexes<R>(
    program: &mut Program,
    f: impl FnOnce(&mut Program, &EnumIndex<'_>, &MethodIndex, &FunIndex<'_>) -> Result<R, TypeError>,
) -> Result<R, TypeError> {
    inject_builtin_enums(&mut program.enums);
    let enums_owned = program.enums.clone();
    let imports_owned = program.imports.clone();
    let defs_owned = program.defs.clone();
    let traits_owned = program.traits.clone();
    let impls_owned = program.impls.clone();
    let enums = EnumIndex::build(&enums_owned, &imports_owned)
        .map_err(|e| TypeError::Msg(e.to_string()))?;
    let methods = MethodIndex::build(&impls_owned, &traits_owned, &enums, &enums_owned)?;
    let aliases_owned = program.aliases.clone();
    let funs = FunIndex::build(&defs_owned, &imports_owned, &enums_owned, &aliases_owned)
        .map_err(|e| TypeError::Msg(e.to_string()))?;
    f(program, &enums, &methods, &funs)
}

fn instantiate_method(entry: &MethodEntry, targs: &[Type]) -> Result<(Vec<Type>, Type), TypeError> {
    if !entry.type_params.is_empty() && targs.len() != entry.type_params.len() {
        return Err(TypeError::Msg(format!(
            "method .{} needs a generic receiver",
            entry.mangled
        )));
    }
    let mut subst = HashMap::new();
    for (p, t) in entry.type_params.iter().zip(targs.iter()) {
        subst.insert(p.clone(), t.clone());
    }
    Ok((
        entry
            .params
            .iter()
            .map(|t| apply_subst(t, &subst))
            .collect(),
        apply_subst(&entry.ret, &subst),
    ))
}

fn method_receiver_parts<'a>(
    ty: &'a Type,
    method: &str,
) -> Result<(&'a str, &'a [Type]), TypeError> {
    match ty {
        Type::Adt(id) => Ok((id, &[])),
        Type::App(id, args) => Ok((id, args)),
        other => Err(TypeError::Msg(format!(
            "method .{method} needs a record/ADT receiver, got {other:?}"
        ))),
    }
}

fn enum_self_ty(en: &EnumDef) -> Type {
    let id = crate::resolve::enum_id(&en.module, &en.name);
    if en.type_params.is_empty() {
        Type::Adt(id)
    } else {
        Type::App(
            id,
            en.type_params
                .iter()
                .map(|p| Type::Var(p.clone()))
                .collect(),
        )
    }
}

/// Expand `type Name = T` in signatures and ascriptions. Idempotent.
pub fn expand_type_aliases(mut program: Program) -> Result<Program, TypeError> {
    for a in &program.aliases {
        if program
            .enums
            .iter()
            .any(|e| e.module == a.module && e.name == a.name)
        {
            return Err(TypeError::Msg(format!(
                "type {} conflicts with enum {}",
                a.name, a.name
            )));
        }
    }
    let aliases = program.aliases.clone();
    let imports = program.imports.clone();
    for a in &mut program.aliases {
        let mut stack = vec![crate::resolve::enum_id(&a.module, &a.name)];
        a.target = expand_alias_ty(&a.target, &aliases, &imports, &a.module, &mut stack)?;
    }
    let aliases = program.aliases.clone();
    for d in &mut program.defs {
        expand_types_in_def(d, &aliases, &imports)?;
    }
    {
        let module = program.main.module.clone();
        program.main.body =
            expand_types_in_expr(program.main.body.clone(), &aliases, &imports, &module)?;
    }
    for en in &mut program.enums {
        let module = en.module.clone();
        let tparams = en.type_params.clone();
        for c in &mut en.cases {
            for (_, ty) in &mut c.fields {
                *ty = expand_alias_ty_tparams(ty, &aliases, &imports, &module, &tparams)?;
            }
        }
        for m in &mut en.methods {
            expand_types_in_method(m, &aliases, &imports, &module, &tparams)?;
        }
    }
    for t in &mut program.traits {
        let module = t.module.clone();
        let tparams = t.type_params.clone();
        for m in &mut t.methods {
            m.ret = expand_alias_ty_tparams(&m.ret, &aliases, &imports, &module, &tparams)?;
            for p in &mut m.params {
                p.ty = expand_alias_ty_tparams(&p.ty, &aliases, &imports, &module, &tparams)?;
            }
        }
    }
    for im in &mut program.impls {
        let module = im.module.clone();
        for m in &mut im.methods {
            expand_types_in_method(m, &aliases, &imports, &module, &[])?;
        }
    }
    Ok(program)
}

fn expand_types_in_def(
    d: &mut FunDef,
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
) -> Result<(), TypeError> {
    let module = d.module.clone();
    let tparams = d.type_params.clone();
    d.ret = expand_alias_ty_tparams(&d.ret, aliases, imports, &module, &tparams)?;
    for p in &mut d.params {
        p.ty = expand_alias_ty_tparams(&p.ty, aliases, imports, &module, &tparams)?;
    }
    d.body = expand_types_in_expr(d.body.clone(), aliases, imports, &module)?;
    Ok(())
}

fn expand_types_in_method(
    m: &mut crate::ast::ImplMethod,
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
    module: &str,
    tparams: &[String],
) -> Result<(), TypeError> {
    m.ret = expand_alias_ty_tparams(&m.ret, aliases, imports, module, tparams)?;
    for p in &mut m.params {
        p.ty = expand_alias_ty_tparams(&p.ty, aliases, imports, module, tparams)?;
    }
    m.body = expand_types_in_expr(m.body.clone(), aliases, imports, module)?;
    Ok(())
}

fn expand_types_in_expr(
    expr: Expr,
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
    module: &str,
) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Ascribe { expr, ty } => {
            let ty = expand_alias_ty(&ty, aliases, imports, module, &mut Vec::new())?;
            let expr = expand_types_in_expr(*expr, aliases, imports, module)?;
            Ok(Expr::new(
                ExprKind::Ascribe {
                    expr: Box::new(expr),
                    ty,
                },
                span,
            ))
        }
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => {
            let param_ty = match param_ty {
                Some(ty) => Some(expand_alias_ty(
                    &ty,
                    aliases,
                    imports,
                    module,
                    &mut Vec::new(),
                )?),
                None => None,
            };
            let body = expand_types_in_expr(*body, aliases, imports, module)?;
            Ok(Expr::new(
                ExprKind::Lambda {
                    param,
                    param_ty,
                    pat,
                    body: Box::new(body),
                },
                span,
            ))
        }
        kind => Expr { kind, span }
            .try_map_children(|c| expand_types_in_expr(c, aliases, imports, module)),
    }
}

fn expand_alias_ty_tparams(
    ty: &Type,
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
    module: &str,
    tparams: &[String],
) -> Result<Type, TypeError> {
    match ty {
        Type::Var(n) if tparams.iter().any(|p| p == n) => Ok(ty.clone()),
        Type::Adt(n) if tparams.iter().any(|p| p == n) => Ok(Type::Var(n.clone())),
        _ => expand_alias_ty(ty, aliases, imports, module, &mut Vec::new()),
    }
}

fn expand_alias_ty(
    ty: &Type,
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
    module: &str,
    stack: &mut Vec<String>,
) -> Result<Type, TypeError> {
    match ty {
        Type::List(inner) => Ok(Type::List(Box::new(expand_alias_ty(
            inner, aliases, imports, module, stack,
        )?))),
        Type::Io(inner) => Ok(Type::Io(Box::new(expand_alias_ty(
            inner, aliases, imports, module, stack,
        )?))),
        Type::Fun(a, b) => Ok(Type::Fun(
            Box::new(expand_alias_ty(a, aliases, imports, module, stack)?),
            Box::new(expand_alias_ty(b, aliases, imports, module, stack)?),
        )),
        Type::Tuple(xs) => Ok(Type::Tuple(
            xs.iter()
                .map(|t| expand_alias_ty(t, aliases, imports, module, stack))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        Type::Adt(n) => expand_alias_name(n, &[], aliases, imports, module, stack),
        Type::App(n, args) => {
            let args = args
                .iter()
                .map(|a| expand_alias_ty(a, aliases, imports, module, stack))
                .collect::<Result<Vec<_>, _>>()?;
            expand_alias_name(n, &args, aliases, imports, module, stack)
        }
        other => Ok(other.clone()),
    }
}

fn expand_alias_name(
    name: &str,
    args: &[Type],
    aliases: &[TypeAlias],
    imports: &[crate::ast::Import],
    module: &str,
    stack: &mut Vec<String>,
) -> Result<Type, TypeError> {
    let Some(alias) = lookup_alias(aliases, imports, name, module)? else {
        return Ok(if args.is_empty() {
            Type::Adt(name.to_string())
        } else {
            Type::App(name.to_string(), args.to_vec())
        });
    };
    let key = crate::resolve::enum_id(&alias.module, &alias.name);
    if stack.iter().any(|s| s == &key) {
        return Err(TypeError::Msg(format!("cyclic type {name}")));
    }
    if alias.type_params.len() != args.len() {
        if alias.type_params.is_empty() {
            return Err(TypeError::Msg(format!(
                "type {name} takes no type arguments"
            )));
        }
        return Err(TypeError::Msg(format!(
            "type {name} expects {} type argument(s)",
            alias.type_params.len()
        )));
    }
    let mut subst = HashMap::new();
    for (p, t) in alias.type_params.iter().zip(args.iter()) {
        subst.insert(p.clone(), t.clone());
    }
    let inst = apply_subst(&alias.target, &subst);
    stack.push(key);
    let out = expand_alias_ty(&inst, aliases, imports, module, stack)?;
    stack.pop();
    Ok(out)
}

fn lookup_alias<'a>(
    aliases: &'a [TypeAlias],
    imports: &[crate::ast::Import],
    name: &str,
    module: &str,
) -> Result<Option<&'a TypeAlias>, TypeError> {
    if let Some((m, n)) = crate::resolve::split_dotted(name) {
        return Ok(aliases.iter().find(|a| a.module == m && a.name == n));
    }
    if let Some(a) = aliases
        .iter()
        .find(|a| a.module == module && a.name == name)
    {
        return Ok(Some(a));
    }
    if let Some((from, src)) = crate::resolve::bind_import(imports, &[], &[], aliases, module, name)
    {
        return Ok(aliases.iter().find(|a| a.module == from && a.name == src));
    }
    let hits: Vec<_> = aliases.iter().filter(|a| a.name == name).collect();
    match hits.as_slice() {
        [] => Ok(None),
        [a] => Ok(Some(*a)),
        _ => Err(TypeError::Msg(format!("ambiguous type {name}"))),
    }
}

/// Turn `impl` methods into ordinary defs (`self` first) for FunIndex / codegen.
pub fn expand_impls(mut program: Program) -> Result<Program, TypeError> {
    program = expand_type_aliases(program)?;
    inject_builtin_enums(&mut program.enums);
    let enums = EnumIndex::build(&program.enums, &program.imports)
        .map_err(|e| TypeError::Msg(e.to_string()))?;
    // Validate with MethodIndex build.
    let _ = MethodIndex::build(&program.impls, &program.traits, &enums, &program.enums)?;
    for im in &program.impls {
        let for_en = enums
            .resolve(&im.for_type, &im.module)
            .map_err(|e| TypeError::Msg(e.to_string()))?;
        let for_id = enums
            .resolve_id(&im.for_type, &im.module)
            .map_err(|e| TypeError::Msg(e.to_string()))?;
        let bare = enum_bare_name(&for_id).to_string();
        let self_ty = enum_self_ty(for_en);
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
                ty: self_ty.clone(),
                rfn: None,
                default: None,
                span: Span::dummy(),
            }];
            params.extend(method.params.clone());
            program.defs.push(FunDef {
                module: im.module.clone(),
                name: mangled,
                name_span: Span::dummy(),
                is_private: false,
                is_driver: false,
                type_params: for_en.type_params.clone(),
                params,
                ret: method.ret.clone(),
                body: method.body.clone(),
            });
        }
    }
    let extra_methods: Vec<FunDef> = {
        let mut extra = Vec::new();
        for en in &program.enums {
            let self_ty = enum_self_ty(en);
            for method in &en.methods {
                let mangled = rec_method_name(&en.name, &method.name);
                if program
                    .defs
                    .iter()
                    .any(|d| d.module == en.module && d.name == mangled)
                    || extra.iter().any(|d: &FunDef| d.name == mangled)
                {
                    return Err(TypeError::Msg(format!("method name collision {mangled}")));
                }
                let mut params = vec![Param {
                    name: "self".into(),
                    ty: self_ty.clone(),
                    rfn: None,
                    default: None,
                    span: Span::dummy(),
                }];
                params.extend(method.params.clone());
                extra.push(FunDef {
                    module: en.module.clone(),
                    name: mangled,
                    name_span: Span::dummy(),
                    is_private: false,
                    is_driver: false,
                    type_params: en.type_params.clone(),
                    params,
                    ret: method.ret.clone(),
                    body: method.body.clone(),
                });
            }
        }
        extra
    };
    program.defs.extend(extra_methods);
    Ok(program)
}

/// Structural check for the kernel dialect: @main is IO[Unit]; defs/calls resolve.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    typecheck_all(program)
        .into_iter()
        .next()
        .map_or(Ok(()), Err)
}

pub fn inject_builtin_enums(enums: &mut Vec<EnumDef>) {
    if !enums.iter().any(|e| e.name == "Result") {
        enums.push(builtin_result_enum());
    }
    if !enums.iter().any(|e| e.name == "Json") {
        enums.push(builtin_json_enum());
    }
}

fn builtin_json_enum() -> EnumDef {
    let json = Type::Adt("Json".into());
    EnumDef {
        module: String::new(),
        name: "Json".into(),
        type_params: Vec::new(),
        cases: vec![
            crate::ast::EnumCase {
                name: "Null".into(),
                fields: Vec::new(),
                field_rfns: Vec::new(),
            },
            crate::ast::EnumCase {
                name: "Bool".into(),
                fields: vec![("value".into(), Type::Bool)],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Int".into(),
                fields: vec![("value".into(), Type::Int)],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Float".into(),
                fields: vec![("value".into(), Type::Float)],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Str".into(),
                fields: vec![("value".into(), Type::String)],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Arr".into(),
                fields: vec![("value".into(), Type::List(Box::new(json.clone())))],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Obj".into(),
                fields: vec![(
                    "value".into(),
                    Type::List(Box::new(Type::Tuple(vec![Type::String, json]))),
                )],
                field_rfns: vec![None],
            },
        ],
        is_record: false,
        methods: Vec::new(),
    }
}

fn builtin_result_enum() -> EnumDef {
    EnumDef {
        module: String::new(),
        name: "Result".into(),
        type_params: vec!["T".into()],
        cases: vec![
            crate::ast::EnumCase {
                name: "Err".into(),
                fields: vec![("msg".into(), Type::String)],
                field_rfns: vec![None],
            },
            crate::ast::EnumCase {
                name: "Ok".into(),
                fields: vec![("value".into(), Type::Var("T".into()))],
                field_rfns: vec![None],
            },
        ],
        is_record: false,
        methods: Vec::new(),
    }
}

/// Same checks as [`typecheck`]. Continues past def-level failures.
pub fn typecheck_all(program: &Program) -> Vec<TypeError> {
    let program = match resolve_named_args(program.clone()) {
        Ok(p) => p,
        Err(e) => return vec![e],
    };
    let program = match expand_type_aliases(program) {
        Ok(p) => p,
        Err(e) => return vec![e],
    };
    let mut errs = Vec::new();
    let mut enums_storage = program.enums.clone();
    inject_builtin_enums(&mut enums_storage);
    let enums = match EnumIndex::build(&enums_storage, &program.imports) {
        Ok(e) => e,
        Err(e) => {
            return vec![match e {
                ResolveError::Duplicate { module, name } => {
                    TypeError::Msg(format!("duplicate enum {module}.{name}"))
                }
                other => TypeError::Msg(other.to_string()),
            }];
        }
    };
    let methods = match MethodIndex::build(&program.impls, &program.traits, &enums, &program.enums)
    {
        Ok(m) => m,
        Err(e) => return vec![e],
    };
    let funs = match FunIndex::build(
        &program.defs,
        &program.imports,
        &program.enums,
        &program.aliases,
    ) {
        Ok(f) => f,
        Err(e) => {
            return vec![match e {
                ResolveError::Duplicate { module, name } => {
                    TypeError::Msg(format!("duplicate def {module}.{name}"))
                }
                other => TypeError::Msg(other.to_string()),
            }];
        }
    };
    for en in &program.enums {
        let id = crate::resolve::enum_id(&en.module, &en.name);
        for case in &en.cases {
            if let Err(e) = check_payload_fields(&id, en, case) {
                errs.push(e);
            }
            for (i, (fname, fty)) in case.fields.iter().enumerate() {
                if let Err(e) = resolve_type_in(fty, &enums, &en.module, &en.type_params) {
                    errs.push(e);
                    continue;
                }
                if let Some(rfn) = case.field_rfn(i) {
                    if crate::overlay::expr_has_property(rfn) {
                        errs.push(TypeError::Msg(format!(
                            "where on `{}.{}` must not call Property.*",
                            en.name, fname
                        )));
                        continue;
                    }
                    let mut env: HashMap<String, Type> = HashMap::new();
                    for (n, t) in &case.fields {
                        match resolve_type_in(t, &enums, &en.module, &en.type_params) {
                            Ok(ty) => {
                                env.insert(n.clone(), ty);
                            }
                            Err(e) => {
                                errs.push(e);
                            }
                        }
                    }
                    match infer(rfn, &enums, &funs, &methods, &en.module, &mut env) {
                        Ok(Type::Bool) => {}
                        Ok(rty) => errs.push(TypeError::Msg(format!(
                            "where on `{}.{}` must be Bool, got {rty:?}",
                            en.name, fname
                        ))),
                        Err(e) => errs.push(e),
                    }
                }
            }
        }
    }
    for d in &program.defs {
        if let Err(e) = typecheck_def(d, &enums, &funs, &methods) {
            errs.push(e);
        }
    }
    if program.main.name.is_empty() {
        return errs;
    }
    let mut env: HashMap<String, Type> = HashMap::new();
    match infer(
        &program.main.body,
        &enums,
        &funs,
        &methods,
        &program.main.module,
        &mut env,
    ) {
        Ok(Type::Io(inner)) if matches!(*inner, Type::Unit) => {}
        Ok(other) => errs.push(TypeError::At {
            msg: format!("@main body must be IO[Unit], got {other:?}"),
            span: program.main.body.span.clone(),
        }),
        Err(e) => errs.push(e),
    }
    errs
}

/// Rewrite `name = expr` call arguments into positional order.
pub fn resolve_named_args(mut program: Program) -> Result<Program, TypeError> {
    let cx = named_cx(&program);
    for d in &mut program.defs {
        let module = d.module.clone();
        for p in &mut d.params {
            if let Some(rfn) = p.rfn.take() {
                p.rfn = Some(resolve_named_expr(rfn, &cx, &module)?);
            }
            if let Some(dflt) = p.default.take() {
                p.default = Some(resolve_named_expr(dflt, &cx, &module)?);
            }
        }
    }
    let cx = named_cx(&program);
    for d in &mut program.defs {
        let module = d.module.clone();
        d.body = resolve_named_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &cx,
            &module,
        )?;
    }
    let main_mod = program.main.module.clone();
    program.main.body = resolve_named_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &cx,
        &main_mod,
    )?;
    for im in &mut program.impls {
        let module = im.module.clone();
        for m in &mut im.methods {
            m.body = resolve_named_expr(
                std::mem::replace(&mut m.body, Expr::dummy(ExprKind::Unit)),
                &cx,
                &module,
            )?;
        }
    }
    for en in &mut program.enums {
        let module = en.module.clone();
        for case in &mut en.cases {
            for slot in &mut case.field_rfns {
                if let Some(rfn) = slot.take() {
                    *slot = Some(resolve_named_expr(rfn, &cx, &module)?);
                }
            }
        }
        for m in &mut en.methods {
            m.body = resolve_named_expr(
                std::mem::replace(&mut m.body, Expr::dummy(ExprKind::Unit)),
                &cx,
                &module,
            )?;
        }
    }
    Ok(program)
}

fn named_cx(program: &Program) -> NamedCx {
    NamedCx {
        def_params: program
            .defs
            .iter()
            .map(|d| {
                (
                    d.module.clone(),
                    d.name.clone(),
                    d.params.iter().map(|p| p.name.clone()).collect(),
                    d.params.iter().map(|p| p.default.clone()).collect(),
                    d.is_private,
                )
            })
            .collect(),
        enums: program.enums.clone(),
        traits: program.traits.clone(),
        impls: program.impls.clone(),
        imports: program.imports.clone(),
    }
}

struct NamedCx {
    def_params: Vec<(String, String, Vec<String>, Vec<Option<Expr>>, bool)>,
    enums: Vec<crate::ast::EnumDef>,
    traits: Vec<crate::ast::TraitDef>,
    impls: Vec<crate::ast::ImplDef>,
    imports: Vec<crate::ast::Import>,
}

fn is_param_ident(name: &str) -> bool {
    let mut chars = name.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    (first.is_ascii_alphabetic() || first == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn has_named_arg(args: &[Expr]) -> bool {
    args.iter()
        .any(|a| matches!(a.kind, ExprKind::NamedArg { .. }))
}

fn bind_named_args(
    callee: &str,
    params: &[String],
    args: Vec<Expr>,
) -> Result<Vec<Expr>, TypeError> {
    let none = vec![None; params.len()];
    bind_named_args_with_defaults(callee, params, &none, args)
}

fn bind_named_args_with_defaults(
    callee: &str,
    params: &[String],
    defaults: &[Option<Expr>],
    args: Vec<Expr>,
) -> Result<Vec<Expr>, TypeError> {
    if !has_named_arg(&args) {
        return pad_defaults(callee, params, defaults, args);
    }
    if params.is_empty() || params.iter().any(|p| !is_param_ident(p)) {
        return Err(TypeError::Msg(format!(
            "{callee} does not take named arguments"
        )));
    }
    let mut seen_named = false;
    let mut slots: Vec<Option<Expr>> = params.iter().map(|_| None).collect();
    let mut used: HashSet<String> = HashSet::new();
    let mut pos = 0usize;
    for a in args {
        match a.kind {
            ExprKind::NamedArg { name, value } => {
                seen_named = true;
                if !used.insert(name.clone()) {
                    return Err(TypeError::Msg(format!(
                        "{callee} argument `{name}` is given more than once"
                    )));
                }
                let Some(idx) = params.iter().position(|p| p == &name) else {
                    return Err(TypeError::Msg(format!("{callee} has no argument `{name}`")));
                };
                if slots[idx].is_some() {
                    return Err(TypeError::Msg(format!(
                        "{callee} argument `{name}` is already given"
                    )));
                }
                slots[idx] = Some(*value);
            }
            _ => {
                if seen_named {
                    return Err(TypeError::Msg(format!(
                        "{callee}: positional argument follows named argument"
                    )));
                }
                if pos >= params.len() {
                    return Err(TypeError::Msg(format!(
                        "{callee} expects {} args, got more",
                        params.len()
                    )));
                }
                slots[pos] = Some(a);
                pos += 1;
            }
        }
    }
    let mut out = Vec::with_capacity(params.len());
    for (i, slot) in slots.into_iter().enumerate() {
        match slot {
            Some(e) => out.push(e),
            None => match defaults.get(i).and_then(|d| d.clone()) {
                Some(d) => out.push(d),
                None => {
                    return Err(TypeError::Msg(format!(
                        "{callee} missing argument `{}`",
                        params[i]
                    )));
                }
            },
        }
    }
    Ok(out)
}

/// Fill omitted trailing args from defaults. Leave short required calls for arity check.
fn pad_defaults(
    _callee: &str,
    params: &[String],
    defaults: &[Option<Expr>],
    args: Vec<Expr>,
) -> Result<Vec<Expr>, TypeError> {
    if args.len() >= params.len() || params.is_empty() {
        return Ok(args);
    }
    let mut extra = Vec::new();
    for i in args.len()..params.len() {
        match defaults.get(i).and_then(|d| d.as_ref()) {
            Some(d) => extra.push(d.clone()),
            None => return Ok(args),
        }
    }
    let mut out = args;
    out.extend(extra);
    Ok(out)
}

/// Place `.copy` args onto record fields. Missing fields stay `None`.
fn bind_copy_slots(
    callee: &str,
    fields: &[String],
    args: Vec<Expr>,
) -> Result<Vec<Option<Expr>>, TypeError> {
    if fields.is_empty() {
        if args.is_empty() {
            return Ok(Vec::new());
        }
        return Err(TypeError::Msg(format!(
            "{callee} expects 0 field updates, got {}",
            args.len()
        )));
    }
    let mut seen_named = false;
    let mut slots: Vec<Option<Expr>> = fields.iter().map(|_| None).collect();
    let mut used: HashSet<String> = HashSet::new();
    let mut pos = 0usize;
    for a in args {
        match a.kind {
            ExprKind::NamedArg { name, value } => {
                seen_named = true;
                if !used.insert(name.clone()) {
                    return Err(TypeError::Msg(format!(
                        "{callee} field `{name}` is given more than once"
                    )));
                }
                let Some(idx) = fields.iter().position(|f| f == &name) else {
                    return Err(TypeError::Msg(format!("{callee} has no field `{name}`")));
                };
                if slots[idx].is_some() {
                    return Err(TypeError::Msg(format!(
                        "{callee} field `{name}` is already given"
                    )));
                }
                slots[idx] = Some(*value);
            }
            _ => {
                if seen_named {
                    return Err(TypeError::Msg(format!(
                        "{callee}: positional argument follows named argument"
                    )));
                }
                if pos >= fields.len() {
                    return Err(TypeError::Msg(format!(
                        "{callee} expects {} field update(s), got more",
                        fields.len()
                    )));
                }
                slots[pos] = Some(a);
                pos += 1;
            }
        }
    }
    Ok(slots)
}

fn record_from_type<'a>(
    ty: &Type,
    enums: &'a EnumIndex<'a>,
    current_module: &str,
    span: &crate::span::Span,
) -> Result<(&'a crate::ast::EnumDef, String), TypeError> {
    let id = match ty {
        Type::Adt(id) | Type::App(id, _) => id.as_str(),
        other => {
            return Err(
                TypeError::Msg(format!(".copy needs a record type, got {other:?}"))
                    .with_span_if_bare(span),
            );
        }
    };
    let (en, eid) = lookup_enum(enums, id, current_module)?;
    if !is_record_like(en) {
        return Err(TypeError::Msg(format!(
            ".copy requires a record type, got enum {}",
            en.name
        ))
        .with_span_if_bare(span));
    }
    Ok((en, eid))
}

fn infer_copy(
    receiver_ty: &Type,
    args: &[Expr],
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    span: &crate::span::Span,
) -> Result<Type, TypeError> {
    let (en, _) = record_from_type(receiver_ty, enums, current_module, span)?;
    let case = en.cases.first().ok_or_else(|| {
        TypeError::Msg("internal: record has no case".into()).with_span_if_bare(span)
    })?;
    let names: Vec<String> = case.fields.iter().map(|(n, _)| n.clone()).collect();
    let slots =
        bind_copy_slots(".copy", &names, args.to_vec()).map_err(|e| e.with_span_if_bare(span))?;
    for (i, slot) in slots.iter().enumerate() {
        let Some(arg) = slot else {
            continue;
        };
        let at = infer(arg, enums, funs, methods, current_module, env)?;
        let want = field_type(receiver_ty, &names[i], enums, current_module)?;
        expect_ty(&at, &want)?;
    }
    Ok(receiver_ty.clone())
}

fn rewrite_copy(
    receiver: Expr,
    args: Vec<Expr>,
    span: crate::span::Span,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Expr, TypeError> {
    let rt = infer(&receiver, enums, funs, methods, current_module, env)?;
    if matches!(&rt, Type::App(_, _)) {
        return Ok(Expr::new(
            ExprKind::MethodCall {
                receiver: Box::new(receiver),
                method: "copy".into(),
                args,
            },
            span,
        ));
    }
    let (en, eid) = record_from_type(&rt, enums, current_module, &span)?;
    let case = en.cases.first().ok_or_else(|| {
        TypeError::Msg("internal: record has no case".into()).with_span_if_bare(&span)
    })?;
    let names: Vec<String> = case.fields.iter().map(|(n, _)| n.clone()).collect();
    let slots = bind_copy_slots(".copy", &names, args).map_err(|e| e.with_span_if_bare(&span))?;
    let bind_names: Vec<String> = (0..names.len()).map(|i| format!("__c{i}")).collect();
    let ctor_args: Vec<Expr> = slots
        .into_iter()
        .enumerate()
        .map(|(i, slot)| {
            slot.unwrap_or_else(|| Expr::new(ExprKind::Var(bind_names[i].clone()), span.clone()))
        })
        .collect();
    let body = crate::overlay::residualize_adt(
        eid.clone(),
        case.name.clone(),
        ctor_args,
        Vec::new(),
        span.clone(),
        std::slice::from_ref(en),
        current_module,
    );
    Ok(Expr::new(
        ExprKind::Match {
            scrutinee: Box::new(receiver),
            arms: vec![crate::ast::MatchArm::new(
                crate::ast::Pattern::Adt {
                    enum_name: eid,
                    case_name: case.name.clone(),
                    binds: bind_names
                        .into_iter()
                        .map(crate::ast::Pattern::Bind)
                        .collect(),
                    type_args: Vec::new(),
                },
                body,
            )],
        },
        span,
    ))
}

fn bind_one_named(callee: &str, param: &str, expr: Expr) -> Result<Expr, TypeError> {
    match expr.kind {
        ExprKind::NamedArg { name, value } => {
            if name == param {
                Ok(*value)
            } else {
                Err(TypeError::Msg(format!("{callee} has no argument `{name}`")))
            }
        }
        kind => Ok(Expr {
            kind,
            span: expr.span,
        }),
    }
}

fn call_param_names(
    callee: &str,
    current_module: &str,
    cx: &NamedCx,
) -> Option<(Vec<String>, Vec<Option<Expr>>)> {
    if let Some(sig) = kit_sig(callee) {
        let names = param_names_from_label(sig);
        if names.iter().all(|n| is_param_ident(n)) {
            let n = names.len();
            return Some((names, vec![None; n]));
        }
        return Some((Vec::new(), Vec::new()));
    }
    if let Some((m, name)) = crate::resolve::split_dotted(callee) {
        if let Some((_, _, p, d, _)) = cx.def_params.iter().find(|(mod_name, n, _, _, is_priv)| {
            mod_name == m && n == name && (!*is_priv || mod_name == current_module)
        }) {
            return Some((p.clone(), d.clone()));
        }
        return record_ctor_param_names(callee, current_module, &cx.enums).map(|names| {
            let n = names.len();
            (names, vec![None; n])
        });
    }
    if let Some((_, _, p, d, _)) = cx
        .def_params
        .iter()
        .find(|(m, n, _, _, _)| m == current_module && n == callee)
    {
        return Some((p.clone(), d.clone()));
    }
    if let Some((_, _, p, d, _)) = imported_def_params(callee, current_module, cx) {
        return Some((p.clone(), d.clone()));
    }
    let hits: Vec<_> = cx
        .def_params
        .iter()
        .filter(|(m, n, _, _, is_priv)| n == callee && (!*is_priv || m == current_module))
        .map(|(_, _, p, d, _)| (p, d))
        .collect();
    if hits.len() == 1 {
        return Some((hits[0].0.clone(), hits[0].1.clone()));
    }
    record_ctor_param_names(callee, current_module, &cx.enums).map(|names| {
        let n = names.len();
        (names, vec![None; n])
    })
}

/// Field names for an unlowered `Point(y = 1, x = 0)` call. Residualize still sees `Call`.
fn record_ctor_param_names(
    callee: &str,
    current_module: &str,
    enums: &[crate::ast::EnumDef],
) -> Option<Vec<String>> {
    let exact: Vec<_> = enums
        .iter()
        .filter(|e| {
            is_record_like(e)
                && (e.name == callee
                    || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == callee))
        })
        .collect();
    let hits = if exact.len() == 1 {
        exact
    } else {
        let local: Vec<_> = enums
            .iter()
            .filter(|e| is_record_like(e) && e.module == current_module && e.name == callee)
            .collect();
        if local.len() == 1 {
            local
        } else {
            enums
                .iter()
                .filter(|e| is_record_like(e) && e.name == callee)
                .collect()
        }
    };
    if hits.len() != 1 {
        return None;
    }
    let case = hits[0].cases.first()?;
    Some(case.fields.iter().map(|(n, _)| n.clone()).collect())
}

fn imported_def_params<'a>(
    callee: &str,
    current_module: &str,
    cx: &'a NamedCx,
) -> Option<&'a (String, String, Vec<String>, Vec<Option<Expr>>, bool)> {
    for im in &cx.imports {
        if im.in_module != current_module {
            continue;
        }
        if im.is_wildcard() {
            if let Some(hit) = cx
                .def_params
                .iter()
                .find(|(m, n, _, _, is_priv)| m == &im.from_module && n == callee && !*is_priv)
            {
                return Some(hit);
            }
        } else if im.local_name() == callee {
            return cx
                .def_params
                .iter()
                .find(|(m, n, _, _, _)| m == &im.from_module && n == &im.name);
        }
    }
    None
}

fn adt_ctor_names(enums: &[crate::ast::EnumDef], enum_name: &str, case_name: &str) -> Vec<String> {
    let en = enums.iter().find(|e| {
        e.name == enum_name
            || (!e.module.is_empty() && format!("{}.{}", e.module, e.name) == enum_name)
            || enum_name.ends_with(&format!(".{}", e.name))
    });
    let Some(en) = en else {
        return Vec::new();
    };
    let Some(case) = en.cases.iter().find(|c| c.name == case_name) else {
        return Vec::new();
    };
    case.fields.iter().map(|(n, _)| n.clone()).collect()
}

fn trait_method_names(
    traits: &[crate::ast::TraitDef],
    impls: &[crate::ast::ImplDef],
    enums: &[crate::ast::EnumDef],
    method: &str,
) -> Vec<String> {
    let mut hits: Vec<Vec<String>> = Vec::new();
    for t in traits {
        for m in &t.methods {
            if m.name == method {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    for im in impls {
        for m in &im.methods {
            if m.name == method {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    for en in enums {
        for m in &en.methods {
            if m.name == method {
                hits.push(m.params.iter().map(|p| p.name.clone()).collect());
            }
        }
    }
    hits.sort();
    hits.dedup();
    if hits.len() == 1 {
        hits.remove(0)
    } else {
        Vec::new()
    }
}

fn resolve_named_expr(expr: Expr, cx: &NamedCx, module: &str) -> Result<Expr, TypeError> {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Call { callee, args } => {
            let args = args
                .into_iter()
                .map(|a| resolve_named_expr(a, cx, module))
                .collect::<Result<Vec<_>, _>>()?;
            let names = call_param_names(&callee, module, cx);
            let args = match names {
                Some((n, d)) => bind_named_args_with_defaults(&callee, &n, &d, args)?,
                None if has_named_arg(&args) => {
                    return Err(
                        TypeError::Msg(format!("{callee} does not take named arguments"))
                            .with_span_if_bare(&span),
                    );
                }
                None => args,
            };
            Ok(Expr::new(ExprKind::Call { callee, args }, span))
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => {
            let args = args
                .into_iter()
                .map(|a| resolve_named_expr(a, cx, module))
                .collect::<Result<Vec<_>, _>>()?;
            let names = adt_ctor_names(&cx.enums, &enum_name, &case_name);
            let label = format!("{enum_name}.{case_name}");
            let args = bind_named_args(&label, &names, args)?;
            Ok(Expr::new(
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args,
                    type_args,
                },
                span,
            ))
        }
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => {
            let receiver = resolve_named_expr(*receiver, cx, module)?;
            let args = args
                .into_iter()
                .map(|a| resolve_named_expr(a, cx, module))
                .collect::<Result<Vec<_>, _>>()?;
            if method == "require" {
                if has_named_arg(&args) {
                    return Err(
                        TypeError::Msg(".require does not take named arguments".into())
                            .with_span_if_bare(&span),
                    );
                }
                return Ok(Expr::new(
                    ExprKind::MethodCall {
                        receiver: Box::new(receiver),
                        method,
                        args,
                    },
                    span,
                ));
            }
            if method == "copy" {
                return Ok(Expr::new(
                    ExprKind::MethodCall {
                        receiver: Box::new(receiver),
                        method,
                        args,
                    },
                    span,
                ));
            }
            let names = trait_method_names(&cx.traits, &cx.impls, &cx.enums, &method);
            let args = bind_named_args(&format!(".{method}"), &names, args)?;
            Ok(Expr::new(
                ExprKind::MethodCall {
                    receiver: Box::new(receiver),
                    method,
                    args,
                },
                span,
            ))
        }
        ExprKind::IoPrintln(e) => {
            let e = bind_one_named("IO.println", "s", resolve_named_expr(*e, cx, module)?)?;
            Ok(Expr::new(ExprKind::IoPrintln(Box::new(e)), span))
        }
        ExprKind::IoSleep(e) => {
            let e = bind_one_named("IO.sleep", "ms", resolve_named_expr(*e, cx, module)?)?;
            Ok(Expr::new(ExprKind::IoSleep(Box::new(e)), span))
        }
        ExprKind::IoFail(e) => {
            let e = bind_one_named("IO.fail", "s", resolve_named_expr(*e, cx, module)?)?;
            Ok(Expr::new(ExprKind::IoFail(Box::new(e)), span))
        }
        ExprKind::IoPure(e) => {
            let e = bind_one_named("IO.pure", "x", resolve_named_expr(*e, cx, module)?)?;
            Ok(Expr::new(ExprKind::IoPure(Box::new(e)), span))
        }
        ExprKind::IoRace { left, right } => {
            let args = bind_named_args(
                "IO.race",
                &["a".into(), "b".into()],
                vec![
                    resolve_named_expr(*left, cx, module)?,
                    resolve_named_expr(*right, cx, module)?,
                ],
            )?;
            let mut it = args.into_iter();
            Ok(Expr::new(
                ExprKind::IoRace {
                    left: Box::new(it.next().unwrap()),
                    right: Box::new(it.next().unwrap()),
                },
                span,
            ))
        }
        ExprKind::IoBoth { left, right } => {
            let args = bind_named_args(
                "IO.both",
                &["a".into(), "b".into()],
                vec![
                    resolve_named_expr(*left, cx, module)?,
                    resolve_named_expr(*right, cx, module)?,
                ],
            )?;
            let mut it = args.into_iter();
            Ok(Expr::new(
                ExprKind::IoBoth {
                    left: Box::new(it.next().unwrap()),
                    right: Box::new(it.next().unwrap()),
                },
                span,
            ))
        }
        ExprKind::IoEnsure { inner, finalizer } => {
            let args = bind_named_args(
                "IO.ensure",
                &["inner".into(), "finalizer".into()],
                vec![
                    resolve_named_expr(*inner, cx, module)?,
                    resolve_named_expr(*finalizer, cx, module)?,
                ],
            )?;
            let mut it = args.into_iter();
            Ok(Expr::new(
                ExprKind::IoEnsure {
                    inner: Box::new(it.next().unwrap()),
                    finalizer: Box::new(it.next().unwrap()),
                },
                span,
            ))
        }
        ExprKind::IoTimeout { ms, inner } => {
            let args = bind_named_args(
                "IO.timeout",
                &["ms".into(), "inner".into()],
                vec![
                    resolve_named_expr(*ms, cx, module)?,
                    resolve_named_expr(*inner, cx, module)?,
                ],
            )?;
            let mut it = args.into_iter();
            Ok(Expr::new(
                ExprKind::IoTimeout {
                    ms: Box::new(it.next().unwrap()),
                    inner: Box::new(it.next().unwrap()),
                },
                span,
            ))
        }
        ExprKind::NamedArg { name, value } => {
            let value = resolve_named_expr(*value, cx, module)?;
            Ok(Expr::new(
                ExprKind::NamedArg {
                    name,
                    value: Box::new(value),
                },
                span,
            ))
        }
        kind => Expr { kind, span }.try_map_children(|c| resolve_named_expr(c, cx, module)),
    }
}

fn typecheck_def(
    d: &FunDef,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
) -> Result<(), TypeError> {
    let mut env: HashMap<String, Type> = HashMap::new();
    for p in &d.params {
        env.insert(
            p.name.clone(),
            resolve_type_in(&p.ty, enums, &d.module, &d.type_params)?,
        );
    }
    for p in &d.params {
        if let Some(dflt) = &p.default {
            for other in &d.params {
                if crate::resolve::uses_name(dflt, &other.name) {
                    return Err(TypeError::At {
                        msg: format!(
                            "default for `{}` must not use parameter `{}`",
                            p.name, other.name
                        ),
                        span: dflt.span.clone(),
                    });
                }
            }
            if crate::overlay::expr_has_property(dflt) {
                return Err(TypeError::At {
                    msg: format!("default for `{}` must not call Property.*", p.name),
                    span: dflt.span.clone(),
                });
            }
            let mut denv: HashMap<String, Type> = HashMap::new();
            let dty = infer(dflt, enums, funs, methods, &d.module, &mut denv)?;
            let want = resolve_type_in(&p.ty, enums, &d.module, &d.type_params)?;
            if !types_compat(&dty, &want) {
                return Err(TypeError::At {
                    msg: format!("default for `{}` expected {:?}, got {dty:?}", p.name, want),
                    span: dflt.span.clone(),
                });
            }
        }
        if let Some(rfn) = &p.rfn {
            if crate::overlay::expr_has_property(rfn) {
                return Err(TypeError::At {
                    msg: format!("where on `{}` must not call Property.*", p.name),
                    span: rfn.span.clone(),
                });
            }
            let rty = infer(rfn, enums, funs, methods, &d.module, &mut env)?;
            if !matches!(rty, Type::Bool) {
                return Err(TypeError::At {
                    msg: format!("where on `{}` must be Bool, got {rty:?}", p.name),
                    span: rfn.span.clone(),
                });
            }
        }
    }
    let ret = resolve_type_in(&d.ret, enums, &d.module, &d.type_params)?;
    let body_ty = if let Type::Fun(a, b) = &ret {
        infer_lambda_arg(
            &d.name,
            &d.body,
            a.as_ref().clone(),
            Some(b.as_ref().clone()),
            enums,
            funs,
            methods,
            &d.module,
            &mut env,
        )?
    } else {
        infer_with_expected(
            &d.body,
            Some(&ret),
            enums,
            funs,
            methods,
            &d.module,
            &mut env,
        )?
    };
    if !types_compat(&body_ty, &ret) {
        return Err(TypeError::At {
            msg: format!(
                "def {} body {:?} does not match declared {:?}",
                d.name, body_ty, ret
            ),
            span: d.body.span.clone(),
        });
    }
    Ok(())
}

fn resolve_type(ty: &Type, enums: &EnumIndex<'_>, module: &str) -> Result<Type, TypeError> {
    resolve_type_in(ty, enums, module, &[])
}

/// Resolve `e: T`. A single-letter unknown name is a type parameter (`T` in `Opt[T]`).
fn resolve_ascribe_type(ty: &Type, enums: &EnumIndex<'_>, module: &str) -> Result<Type, TypeError> {
    let mut tparams = Vec::new();
    collect_ascribe_tparams(ty, enums, module, &mut tparams);
    resolve_type_in(ty, enums, module, &tparams)
}

fn collect_ascribe_tparams(ty: &Type, enums: &EnumIndex<'_>, module: &str, out: &mut Vec<String>) {
    match ty {
        Type::Adt(n) | Type::Var(n) => {
            if is_ascribe_tparam(n)
                && !out.iter().any(|p| p == n)
                && enums.resolve(n, module).is_err()
            {
                out.push(n.clone());
            }
        }
        Type::App(_, args) => {
            for a in args {
                collect_ascribe_tparams(a, enums, module, out);
            }
        }
        Type::List(inner) | Type::Io(inner) => {
            collect_ascribe_tparams(inner, enums, module, out);
        }
        Type::Fun(a, b) => {
            collect_ascribe_tparams(a, enums, module, out);
            collect_ascribe_tparams(b, enums, module, out);
        }
        Type::Tuple(xs) => {
            for t in xs {
                collect_ascribe_tparams(t, enums, module, out);
            }
        }
        _ => {}
    }
}

fn is_ascribe_tparam(n: &str) -> bool {
    let mut chars = n.chars();
    matches!(chars.next(), Some(c) if c.is_ascii_uppercase()) && chars.next().is_none()
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
                let id = enums
                    .resolve_id(n, module)
                    .map_err(|e| TypeError::Msg(format!("unknown enum {n}: {e}")))?;
                Ok(Type::Adt(id))
            }
        }
        Type::App(n, args) => {
            if is_handle_ctor(n) {
                let rargs = args
                    .iter()
                    .map(|a| resolve_type_in(a, enums, module, type_params))
                    .collect::<Result<Vec<_>, _>>()?;
                return Ok(Type::App(n.clone(), rargs));
            }
            let en = enums
                .resolve(n, module)
                .map_err(|e| TypeError::Msg(format!("unknown enum {n}: {e}")))?;
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
            let id = enums
                .resolve_id(n, module)
                .map_err(|e| TypeError::Msg(format!("unknown enum {n}: {e}")))?;
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
        Type::List(inner) => Ok(Type::List(Box::new(resolve_type_in(
            inner,
            enums,
            module,
            type_params,
        )?))),
        Type::Fun(a, b) => Ok(Type::Fun(
            Box::new(resolve_type_in(a, enums, module, type_params)?),
            Box::new(resolve_type_in(b, enums, module, type_params)?),
        )),
        Type::Tuple(xs) => Ok(Type::Tuple(
            xs.iter()
                .map(|t| resolve_type_in(t, enums, module, type_params))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        other => Ok(other.clone()),
    }
}

fn lookup_enum<'a>(
    enums: &'a EnumIndex<'a>,
    enum_name: &str,
    current_module: &str,
) -> Result<(&'a EnumDef, String), TypeError> {
    let en = enums
        .resolve(enum_name, current_module)
        .map_err(|e| TypeError::Msg(format!("unknown enum {enum_name}: {e}")))?;
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
    if let Type::Tuple(slots) = base_ty {
        let Some(idx) = crate::ast::tuple_slot(field) else {
            return Err(TypeError::Msg(format!(
                "tuple has fields _1 through _{}, not {field}",
                slots.len()
            )));
        };
        return slots.get(idx).cloned().ok_or_else(|| {
            TypeError::Msg(format!(
                "tuple has fields _1 through _{}, not {field}",
                slots.len()
            ))
        });
    }
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
    let case = en
        .cases
        .first()
        .ok_or_else(|| TypeError::Msg(format!("record {} has no cases", en.name)))?;
    let (_, fty) = case
        .fields
        .iter()
        .find(|(n, _)| n == field)
        .ok_or_else(|| TypeError::Msg(format!("record {} has no field {field}", en.name)))?;
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
    with_pass_indexes(&mut program, |program, enums, methods, funs| {
        for d in &mut program.defs {
            let mut env: HashMap<String, Type> = HashMap::new();
            for p in &d.params {
                env.insert(
                    p.name.clone(),
                    resolve_type_in(&p.ty, enums, &d.module, &d.type_params)?,
                );
            }
            d.body = rewrite_fields(
                std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
                enums,
                funs,
                methods,
                &d.module,
                &mut env,
            )?;
        }
        let mut env: HashMap<String, Type> = HashMap::new();
        let main_mod = program.main.module.clone();
        program.main.body = rewrite_fields(
            std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
            enums,
            funs,
            methods,
            &main_mod,
            &mut env,
        )?;
        Ok(())
    })?;
    Ok(program)
}

fn require_pred_name(pred: &Expr) -> String {
    match &pred.kind {
        ExprKind::Var(n) => n.rsplit('.').next().unwrap_or(n).to_string(),
        ExprKind::Call { callee, args } if args.is_empty() => {
            callee.rsplit('.').next().unwrap_or(callee).to_string()
        }
        _ => "require".into(),
    }
}

fn split_require_args(args: &[Expr]) -> Result<(String, Expr), TypeError> {
    match args {
        [pred] => Ok((require_pred_name(pred), pred.clone())),
        [name, pred] => match &name.kind {
            ExprKind::StrLit(s) => Ok((s.clone(), pred.clone())),
            _ => Err(TypeError::Msg(
                ".require name must be a string literal when two args are given".into(),
            )),
        },
        _ => Err(TypeError::Msg(format!(
            ".require expects 1 or 2 args, got {}",
            args.len()
        ))),
    }
}

fn is_nullary_bool_property(
    name: &str,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> Result<bool, TypeError> {
    match funs.resolve(name, current_module) {
        Ok(f) if f.params.is_empty() && matches!(f.ret, Type::Bool) => Ok(true),
        Ok(_) => Ok(false),
        Err(crate::resolve::ResolveError::Unknown(_)) => Ok(false),
        Err(e) => Err(TypeError::Msg(e.to_string())),
    }
}

fn pred_ok_ty(t: &Type) -> bool {
    match t {
        Type::Bool => true,
        Type::Io(inner) => matches!(**inner, Type::Bool),
        _ => false,
    }
}

fn infer_require_pred(
    pred: &Expr,
    receiver_ty: &Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    match &pred.kind {
        ExprKind::Var(name) if is_nullary_bool_property(name, funs, current_module)? => {
            Ok(Type::Bool)
        }
        ExprKind::Lambda { param, body, .. } => {
            let inner = match receiver_ty {
                Type::Io(t) => t.as_ref(),
                other => other,
            };
            let old = if let Some(p) = param {
                if p != "_" {
                    env.insert(p.clone(), inner.clone())
                } else {
                    None
                }
            } else {
                None
            };
            let bt = infer(body, enums, funs, methods, current_module, env)?;
            if let Some(p) = param {
                if p != "_" {
                    match old {
                        Some(v) => {
                            env.insert(p.clone(), v);
                        }
                        None => {
                            env.remove(p);
                        }
                    }
                }
            }
            if !pred_ok_ty(&bt) {
                return Err(TypeError::Msg(format!(
                    ".require lambda must return Bool or IO[Bool], got {bt:?}"
                )));
            }
            Ok(bt)
        }
        _ => {
            let t = infer(pred, enums, funs, methods, current_module, env)?;
            if !pred_ok_ty(&t) {
                return Err(TypeError::Msg(format!(
                    ".require predicate must be Bool or IO[Bool], got {t:?}"
                )));
            }
            Ok(t)
        }
    }
}

fn infer_require_args(
    args: &[Expr],
    receiver_ty: &Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<(), TypeError> {
    let (_name, pred) = split_require_args(args)?;
    let _ = infer_require_pred(
        &pred,
        receiver_ty,
        enums,
        funs,
        methods,
        current_module,
        env,
    )?;
    Ok(())
}

fn normalize_require_pred(
    pred: Expr,
    recv_var: &str,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> Expr {
    let span = pred.span.clone();
    match pred.kind {
        ExprKind::Var(name)
            if is_nullary_bool_property(&name, funs, current_module).unwrap_or(false) =>
        {
            Expr::new(
                ExprKind::Call {
                    callee: name,
                    args: vec![],
                },
                span,
            )
        }
        ExprKind::Lambda { param, body, .. } => {
            if let Some(p) = param {
                if p == "_" {
                    *body
                } else {
                    Expr::new(
                        ExprKind::Let {
                            name: p,
                            value: Box::new(Expr::new(
                                ExprKind::Var(recv_var.into()),
                                span.clone(),
                            )),
                            body,
                        },
                        span,
                    )
                }
            } else {
                *body
            }
        }
        other => Expr::new(other, span),
    }
}

fn force_pred_if_io(pred: Expr, pred_ty: &Type) -> Expr {
    if matches!(pred_ty, Type::Io(_)) {
        let span = pred.span.clone();
        Expr::new(
            ExprKind::Call {
                callee: "Property.force".into(),
                args: vec![pred],
            },
            span,
        )
    } else {
        pred
    }
}

// Arity comes from the shared typecheck context (enums, funs, methods, module, env).
#[allow(clippy::too_many_arguments)]
fn rewrite_require(
    receiver: Expr,
    args: Vec<Expr>,
    span: Span,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Expr, TypeError> {
    let (name, pred) = split_require_args(&args)?;
    let rt = infer(&receiver, enums, funs, methods, current_module, env)?;
    let pred_ty = infer_require_pred(&pred, &rt, enums, funs, methods, current_module, env)?;
    let name_lit = Expr::new(ExprKind::StrLit(name), span.clone());
    match &rt {
        Type::Io(_) => {
            let recv_var = "__req";
            let pred = normalize_require_pred(pred, recv_var, funs, current_module);
            let pred_is_io = matches!(pred_ty, Type::Io(_));
            let yield_v = Expr::new(
                ExprKind::IoPure(Box::new(Expr::new(
                    ExprKind::Var(recv_var.into()),
                    span.clone(),
                ))),
                span.clone(),
            );
            let after_assert = Expr::new(
                ExprKind::FlatMap {
                    inner: Box::new(Expr::new(
                        ExprKind::Call {
                            callee: "Property.assert".into(),
                            args: vec![
                                name_lit,
                                Expr::new(ExprKind::Var("__ok".into()), span.clone()),
                            ],
                        },
                        span.clone(),
                    )),
                    param: Some("_".into()),
                    body: Box::new(yield_v),
                },
                span.clone(),
            );
            let body = if pred_is_io {
                Expr::new(
                    ExprKind::FlatMap {
                        inner: Box::new(pred),
                        param: Some("__ok".into()),
                        body: Box::new(after_assert),
                    },
                    span.clone(),
                )
            } else {
                Expr::new(
                    ExprKind::Let {
                        name: "__ok".into(),
                        value: Box::new(pred),
                        body: Box::new(after_assert),
                    },
                    span.clone(),
                )
            };
            Ok(Expr::new(
                ExprKind::FlatMap {
                    inner: Box::new(receiver),
                    param: Some(recv_var.into()),
                    body: Box::new(body),
                },
                span,
            ))
        }
        _ => {
            let recv_var = "__req";
            let pred = normalize_require_pred(pred, recv_var, funs, current_module);
            let pred = force_pred_if_io(pred, &pred_ty);
            let check = Expr::new(
                ExprKind::Call {
                    callee: "Property.check".into(),
                    args: vec![
                        name_lit,
                        pred,
                        Expr::new(ExprKind::Var(recv_var.into()), span.clone()),
                    ],
                },
                span.clone(),
            );
            Ok(Expr::new(
                ExprKind::Let {
                    name: recv_var.into(),
                    value: Box::new(receiver),
                    body: Box::new(check),
                },
                span,
            ))
        }
    }
}

/// Kit lambdas bind a known item type. Bare lambdas (`View.button` tap) stay Opaque.
pub(crate) fn kit_lambda_param_ty(callee: &str, arg_i: usize, nargs: usize) -> Option<Type> {
    kit_lambda_param_ty_at(callee, arg_i, nargs, &[])
}

fn kit_lambda_param_ty_at(
    callee: &str,
    arg_i: usize,
    nargs: usize,
    prior: &[Type],
) -> Option<Type> {
    match (callee, arg_i) {
        ("Ui.run", 0) => Some(Type::Opaque("Param".into())),
        ("Signal.map", 1) => Some(Type::Int),
        ("View.each", 1) if nargs == 2 => Some(Type::String),
        (
            "List.filter"
            | "List.filterNot"
            | "List.map"
            | "List.flatMap"
            | "List.find"
            | "List.findLast"
            | "List.exists"
            | "List.count"
            | "List.takeWhile"
            | "List.dropWhile"
            | "List.span"
            | "List.partition"
            | "List.forall"
            | "List.indexWhere"
            | "List.lastIndexWhere"
            | "List.prefixLength"
            | "List.segmentLength"
            | "List.sortBy"
            | "List.maxBy"
            | "List.minBy"
            | "List.groupBy"
            | "List.distinctBy",
            1,
        ) => prior.first().and_then(|t| list_elem(t).ok()),
        ("IO.foreach" | "IO.foreachDiscard", 1) => prior.first().and_then(|t| list_elem(t).ok()),
        ("Ref.update" | "Ref.updateAndGet", 1) => prior
            .first()
            .and_then(|t| handle_payload_ty(t, "Ref").ok().map(default_cell_payload)),
        ("Map.filter" | "Map.exists" | "Map.forall" | "Map.mapValues", 1) => {
            prior.first().and_then(|t| map_kv(t).ok().map(|(_, v)| v))
        }
        ("Set.filter" | "Set.exists" | "Set.forall" | "Set.map", 1) => {
            prior.first().and_then(|t| set_elem(t).ok())
        }
        ("List.tabulate", 1) => Some(Type::Int),
        ("List.foldLeft" | "List.scanLeft", 2) => {
            let elem = prior.first().and_then(|t| list_elem(t).ok())?;
            let z = prior.get(1)?.clone();
            Some(Type::Tuple(vec![z, elem]))
        }
        ("List.foldRight" | "List.scanRight", 2) => {
            let elem = prior.first().and_then(|t| list_elem(t).ok())?;
            let z = prior.get(1)?.clone();
            Some(Type::Tuple(vec![elem, z]))
        }
        ("List.reduceLeft" | "List.reduceRight", 1) => {
            let elem = prior.first().and_then(|t| list_elem(t).ok())?;
            Some(Type::Tuple(vec![elem.clone(), elem]))
        }
        (
            "Stream.filter" | "Stream.map" | "Stream.takeWhile" | "Stream.dropWhile"
            | "Stream.find" | "Stream.exists" | "Stream.evalMap",
            1,
        ) => prior
            .first()
            .and_then(|t| handle_payload_ty(t, "Stream").ok()),
        ("Resource.make", 1) => prior.first().and_then(|t| match t {
            Type::Io(inner) => Some((**inner).clone()),
            _ => None,
        }),
        ("Resource.use", 1) => prior
            .first()
            .and_then(|t| handle_payload_ty(t, "Resource").ok()),
        ("Net.serve" | "Net.serveOnce", 1) => Some(Type::String),
        _ => None,
    }
}

fn user_fun_lambda_expected(
    callee: &str,
    arg_i: usize,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> Option<(Type, Option<Type>)> {
    let f = funs.resolve(callee, current_module).ok()?;
    let p = f.params.get(arg_i)?;
    let want = resolve_type_in(&p.ty, enums, &f.module, &f.type_params).ok()?;
    match want {
        Type::Fun(a, b) => Some((*a, Some(*b))),
        _ => None,
    }
}

fn type_mentions_params(ty: &Type, params: &[String]) -> bool {
    match ty {
        Type::Var(n) => params.iter().any(|p| p == n),
        Type::List(t) | Type::Io(t) => type_mentions_params(t, params),
        Type::Fun(a, b) => type_mentions_params(a, params) || type_mentions_params(b, params),
        Type::Tuple(xs) | Type::App(_, xs) => xs.iter().any(|t| type_mentions_params(t, params)),
        _ => false,
    }
}

fn user_fun_param_ty(
    callee: &str,
    arg_i: usize,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> Option<Type> {
    let f = funs.resolve(callee, current_module).ok()?;
    let p = f.params.get(arg_i)?;
    let ty = resolve_type_in(&p.ty, enums, &f.module, &f.type_params).ok()?;
    if type_mentions_params(&ty, &f.type_params) {
        None
    } else {
        Some(ty)
    }
}

fn kit_lambda_ret_ty(callee: &str, arg_i: usize, nargs: usize) -> Option<Type> {
    match (callee, arg_i) {
        ("Ui.run", 0) => Some(Type::Opaque("View".into())),
        ("View.each", 1) if nargs == 2 => Some(Type::Opaque("View".into())),
        ("Signal.map", 1) => Some(Type::String),
        ("List.flatMap", 1) => Some(list_of(Type::Opaque("Elem".into()))),
        (
            "List.filter"
            | "List.filterNot"
            | "List.find"
            | "List.findLast"
            | "List.exists"
            | "List.count"
            | "List.takeWhile"
            | "List.dropWhile"
            | "List.span"
            | "List.partition"
            | "List.forall"
            | "List.indexWhere"
            | "List.lastIndexWhere"
            | "List.prefixLength"
            | "List.segmentLength"
            | "Stream.filter"
            | "Stream.takeWhile"
            | "Stream.dropWhile"
            | "Stream.find"
            | "Stream.exists"
            | "Map.filter"
            | "Map.exists"
            | "Map.forall"
            | "Set.filter"
            | "Set.exists"
            | "Set.forall",
            1,
        ) => Some(Type::Bool),
        ("List.sortBy" | "List.maxBy" | "List.minBy", 1) => Some(Type::Int),
        (
            "Stream.evalMap" | "Resource.make" | "Resource.use" | "IO.foreach"
            | "IO.foreachDiscard",
            1,
        ) => Some(Type::Io(Box::new(Type::Opaque("Elem".into())))),
        ("Net.serve" | "Net.serveOnce", 1) => Some(Type::Io(Box::new(Type::String))),
        _ => None,
    }
}

fn kit_ret_label(ty: &Type) -> &'static str {
    match ty {
        Type::Opaque(n) if n == "View" => "View",
        Type::List(_) => "List[_]",
        Type::String => "String",
        Type::Bool => "Bool",
        Type::Int => "Int",
        Type::Io(inner) if matches!(inner.as_ref(), Type::String) => "IO[String]",
        Type::Io(_) => "IO[_]",
        _ => "the expected type",
    }
}

fn kit_lambda_body_ok(got: &Type, want: &Type) -> bool {
    match want {
        Type::Opaque(n) if n == "View" => matches!(got, Type::Opaque(g) if g == "View"),
        Type::List(_) => matches!(got, Type::List(_)),
        Type::String => matches!(got, Type::String | Type::Int | Type::Float),
        Type::Bool => matches!(got, Type::Bool),
        Type::Int => matches!(got, Type::Int),
        Type::Io(_) => matches!(got, Type::Io(_)),
        _ => types_compat(got, want),
    }
}

fn bind_opt(
    param: Option<&String>,
    ty: Type,
    env: &mut HashMap<String, Type>,
) -> Option<(String, Option<Type>)> {
    param.map(|p| (p.clone(), env.insert(p.clone(), ty)))
}

fn restore_opt(old: Option<(String, Option<Type>)>, env: &mut HashMap<String, Type>) {
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
}

fn rewrite_lambda_arg(
    expr: Expr,
    param_ty: Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Expr, TypeError> {
    let expr = as_lambda_arg(expr, env, funs, current_module, Some(&param_ty))?;
    if let ExprKind::Lambda {
        param,
        param_ty: ann,
        pat,
        body,
    } = expr.kind
    {
        let span = expr.span;
        let old = bind_opt(param.as_ref(), param_ty, env);
        let body = rewrite_fields(*body, enums, funs, methods, current_module, env)?;
        restore_opt(old, env);
        Ok(Expr::new(
            ExprKind::Lambda {
                param,
                param_ty: ann,
                pat,
                body: Box::new(body),
            },
            span,
        ))
    } else {
        rewrite_fields(expr, enums, funs, methods, current_module, env)
    }
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
            if method == "require" {
                return rewrite_require(
                    receiver,
                    args,
                    span,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                );
            }
            if method == "copy" {
                return rewrite_copy(
                    receiver,
                    args,
                    span,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                );
            }
            let rt = infer(&receiver, enums, funs, methods, current_module, env)?;
            let (id, targs) =
                method_receiver_parts(&rt, &method).map_err(|e| e.with_span_if_bare(&span))?;
            let entry = methods.lookup(id, &method)?;
            let _ = instantiate_method(entry, targs)?;
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
            if let Type::Tuple(slots) = &bt {
                let Some(idx) = crate::ast::tuple_slot(&field) else {
                    return Err(TypeError::Msg(format!(
                        "tuple has fields _1 through _{}, not {field}",
                        slots.len()
                    ))
                    .with_span_if_bare(&span));
                };
                if idx >= slots.len() {
                    return Err(TypeError::Msg(format!(
                        "tuple has fields _1 through _{}, not {field}",
                        slots.len()
                    ))
                    .with_span_if_bare(&span));
                }
                let binds: Vec<crate::ast::Pattern> = (0..slots.len())
                    .map(|i| crate::ast::Pattern::Bind(format!("__f{i}")))
                    .collect();
                let body = Expr::new(ExprKind::Var(format!("__f{idx}")), span.clone());
                return Ok(Expr::new(
                    ExprKind::Match {
                        scrutinee: Box::new(base),
                        arms: vec![crate::ast::MatchArm::new(
                            crate::ast::Pattern::Tuple {
                                elems: binds,
                                tys: slots.clone(),
                            },
                            body,
                        )],
                    },
                    span,
                ));
            }
            if matches!(&bt, Type::App(_, _)) {
                return Ok(Expr::new(
                    ExprKind::Field {
                        base: Box::new(base),
                        field,
                    },
                    span,
                ));
            }
            let Type::Adt(id) = &bt else {
                return Err(TypeError::Msg(format!(
                    "field access .{field} needs a record type, got {bt:?}"
                ))
                .with_span_if_bare(&span));
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
            let names: Vec<String> = (0..case.fields.len()).map(|i| format!("__f{i}")).collect();
            let body = Expr::new(ExprKind::Var(names[idx].clone()), span.clone());
            Ok(Expr::new(
                ExprKind::Match {
                    scrutinee: Box::new(base),
                    arms: vec![crate::ast::MatchArm::new(
                        crate::ast::Pattern::Adt {
                            enum_name: eid,
                            case_name: case.name.clone(),
                            binds: names.into_iter().map(crate::ast::Pattern::Bind).collect(),
                            type_args: Vec::new(),
                        },
                        body,
                    )],
                },
                span,
            ))
        }
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
        ExprKind::IoMap { inner, param, body } => {
            let inner = rewrite_fields(*inner, enums, funs, methods, current_module, env)?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let Type::Io(inner_t) = it else {
                return Ok(Expr::new(
                    ExprKind::IoMap {
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
                ExprKind::IoMap {
                    inner: Box::new(inner),
                    param,
                    body: Box::new(body),
                },
                span,
            ))
        }
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
        ExprKind::Match { scrutinee, arms } => {
            let scrutinee = rewrite_fields(*scrutinee, enums, funs, methods, current_module, env)?;
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound =
                    bind_pattern(&arm.pattern, &st, enums, current_module, env, arm.unpack)?;
                let guard = match arm.guard {
                    Some(g) => Some(rewrite_fields(
                        g,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                    )?),
                    None => None,
                };
                let body = rewrite_fields(arm.body, enums, funs, methods, current_module, env)?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm {
                    pattern: arm.pattern,
                    guard,
                    body,
                    unpack: arm.unpack,
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
        ExprKind::Call { callee, args } => {
            let nargs = args.len();
            let mut out = Vec::with_capacity(nargs);
            let mut prior: Vec<Type> = Vec::new();
            for (i, a) in args.into_iter().enumerate() {
                let rewritten = if let Some(pty) = kit_lambda_param_ty_at(&callee, i, nargs, &prior)
                {
                    rewrite_lambda_arg(a, pty, enums, funs, methods, current_module, env)?
                } else if let Some((pty, _)) =
                    user_fun_lambda_expected(&callee, i, enums, funs, current_module)
                {
                    rewrite_lambda_arg(a, pty, enums, funs, methods, current_module, env)?
                } else {
                    rewrite_fields(a, enums, funs, methods, current_module, env)?
                };
                let ty = infer(&rewritten, enums, funs, methods, current_module, env)
                    .unwrap_or_else(|_| Type::Opaque("Rewrite".into()));
                prior.push(ty);
                out.push(rewritten);
            }
            Ok(Expr::new(ExprKind::Call { callee, args: out }, span))
        }
        ExprKind::Apply { fun, arg } => {
            let arg = rewrite_fields(*arg, enums, funs, methods, current_module, env)?;
            let at = infer(&arg, enums, funs, methods, current_module, env)?;
            let fun = rewrite_lambda_arg(*fun, at, enums, funs, methods, current_module, env)?;
            Ok(Expr::new(
                ExprKind::Apply {
                    fun: Box::new(fun),
                    arg: Box::new(arg),
                },
                span,
            ))
        }
        kind => Ok(Expr { kind, span }
            .try_map_children(|c| rewrite_fields(c, enums, funs, methods, current_module, env))?),
    }
}

fn is_ctor_ident(name: &str) -> bool {
    name.starts_with(|c: char| c.is_ascii_uppercase())
}

fn name_is_free(
    name: &str,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> bool {
    !env.contains_key(name) && funs.resolve(name, current_module).is_err()
}

fn is_bare_ctor_expr(
    expr: &Expr,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> bool {
    match &expr.kind {
        ExprKind::Var(name) => is_ctor_ident(name) && name_is_free(name, env, funs, current_module),
        ExprKind::Call { callee, .. } if !callee.contains('.') => {
            is_ctor_ident(callee) && name_is_free(callee, env, funs, current_module)
        }
        _ => false,
    }
}

fn type_adt_id(ty: &Type) -> Option<&str> {
    match ty {
        Type::Adt(id) | Type::App(id, _) => Some(id.as_str()),
        _ => None,
    }
}

fn infer_bare_ctor(
    expr: &Expr,
    want: &Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let Some(eid) = type_adt_id(want) else {
        return Err(TypeError::Msg(format!(
            "constructor needs an enum type, got {want:?}"
        )));
    };
    let (en, id) = lookup_enum(enums, eid, current_module)?;
    let _ = id;
    let (ctor_name, args): (&str, &[Expr]) = match &expr.kind {
        ExprKind::Var(name) => (name.as_str(), &[]),
        ExprKind::Call { callee, args } => (callee.as_str(), args.as_slice()),
        _ => {
            return Err(TypeError::Msg(
                "internal: infer_bare_ctor on a non-constructor".into(),
            ))
        }
    };
    let case = en
        .cases
        .iter()
        .find(|c| c.name == ctor_name)
        .ok_or_else(|| TypeError::Msg(format!("unknown case {ctor_name} for {}", en.name)))?;
    if args.len() != case.fields.len() {
        if case.fields.is_empty() {
            return Err(TypeError::Msg(format!(
                "{}.{} is nullary; remove payload",
                en.name, case.name
            )));
        }
        return Err(TypeError::Msg(format!(
            "{}.{} expects {} arg(s), got {}",
            en.name,
            case.name,
            case.fields.len(),
            args.len()
        )));
    }
    let field_tys = payload_field_types(en, case, want, enums)?;
    for (arg, fty) in args.iter().zip(field_tys.iter()) {
        let got = infer_with_expected(arg, Some(fty), enums, funs, methods, current_module, env)?;
        expect_ty(&got, fty).map_err(|e| e.with_span_if_bare(&arg.span))?;
    }
    Ok(want.clone())
}

fn infer_with_expected(
    expr: &Expr,
    expected: Option<&Type>,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let expected = expected.filter(|t| !matches!(t, Type::Opaque(_)));
    if let Some(want) = expected {
        if is_bare_ctor_expr(expr, env, funs, current_module) {
            return infer_bare_ctor(expr, want, enums, funs, methods, current_module, env)
                .map_err(|e| e.with_span_if_bare(&expr.span));
        }
        match &expr.kind {
            ExprKind::ListLit { elems } => {
                if let Type::List(elem) = want {
                    if elems.is_empty() {
                        return Ok(want.clone());
                    }
                    let mut got_elem: Option<Type> = None;
                    for e in elems {
                        let t = infer_with_expected(
                            e,
                            Some(elem),
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                        )?;
                        got_elem = Some(match got_elem {
                            None => t,
                            Some(prev) => prefer_elem(&prev, &t)?,
                        });
                    }
                    return Ok(Type::List(Box::new(got_elem.unwrap())));
                }
            }
            ExprKind::Tuple { elems } => {
                if let Type::Tuple(tys) = want {
                    if tys.len() == elems.len() {
                        let mut got = Vec::new();
                        for (e, t) in elems.iter().zip(tys.iter()) {
                            got.push(infer_with_expected(
                                e,
                                Some(t),
                                enums,
                                funs,
                                methods,
                                current_module,
                                env,
                            )?);
                        }
                        return Ok(Type::Tuple(got));
                    }
                }
            }
            ExprKind::If {
                cond,
                then_branch,
                else_branch,
                implicit_else,
            } => {
                let ct = infer(cond, enums, funs, methods, current_module, env)?;
                if !matches!(ct, Type::Bool) {
                    return Err(
                        TypeError::Msg(format!("if condition must be Bool, got {ct:?}"))
                            .with_span_if_bare(&expr.span),
                    );
                }
                let tt = infer_with_expected(
                    then_branch,
                    Some(want),
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?;
                if *implicit_else {
                    return implicit_else_ty(&tt);
                }
                let et = infer_with_expected(
                    else_branch,
                    Some(want),
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?;
                return prefer_join(&tt, &et).map_err(|_| {
                    TypeError::Msg(format!("if branches disagree: {tt:?} vs {et:?}"))
                });
            }
            ExprKind::IoPure(inner) => {
                if let Type::Io(t) = want {
                    let inner_ty = infer_with_expected(
                        inner,
                        Some(t),
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                    )?;
                    return Ok(Type::Io(Box::new(inner_ty)));
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                let st = infer(scrutinee, enums, funs, methods, current_module, env)?;
                let mut result: Option<Type> = None;
                for arm in arms {
                    let bound =
                        bind_pattern(&arm.pattern, &st, enums, current_module, env, arm.unpack)?;
                    if let Some(g) = &arm.guard {
                        let gt = infer(g, enums, funs, methods, current_module, env)?;
                        if !matches!(gt, Type::Bool) {
                            unbind_pattern(bound, env);
                            return Err(TypeError::Msg(format!(
                                "match guard must be Bool, got {gt:?}"
                            )));
                        }
                    }
                    let bt = infer_with_expected(
                        &arm.body,
                        Some(want),
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                    )?;
                    unbind_pattern(bound, env);
                    match &result {
                        None => result = Some(bt),
                        Some(prev) => match prefer_join(prev, &bt) {
                            Ok(joined) => result = Some(joined),
                            Err(_) => {
                                return Err(TypeError::Msg(format!(
                                    "match arms disagree: {prev:?} vs {bt:?}"
                                )))
                            }
                        },
                    }
                }
                check_match_exhaustive(&st, arms, enums, current_module)?;
                return result.ok_or_else(|| TypeError::Msg("empty match".into()));
            }
            ExprKind::Let { name, value, body } => {
                let vt = infer(value, enums, funs, methods, current_module, env)?;
                let old = env.insert(name.clone(), vt);
                let bt = infer_with_expected(
                    body,
                    Some(want),
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?;
                if let Some(v) = old {
                    env.insert(name.clone(), v);
                } else {
                    env.remove(name);
                }
                return Ok(bt);
            }
            _ => {}
        }
        return infer(expr, enums, funs, methods, current_module, env);
    }
    infer(expr, enums, funs, methods, current_module, env)
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
            ExprKind::FloatLit(_) => Ok(Type::Float),
            ExprKind::BoolLit(_) => Ok(Type::Bool),
            ExprKind::StrLit(_) => Ok(Type::String),
            ExprKind::ListLit { elems } => {
                let mut elem: Option<Type> = None;
                for e in elems {
                    let t = infer(e, enums, funs, methods, current_module, env)?;
                    elem = Some(match elem {
                        None => t,
                        Some(prev) => prefer_elem(&prev, &t)?,
                    });
                }
                Ok(Type::List(Box::new(
                    elem.unwrap_or_else(|| Type::Opaque("Elem".into())),
                )))
            }
            ExprKind::Tuple { elems } => {
                let mut tys = Vec::new();
                for e in elems {
                    tys.push(infer(e, enums, funs, methods, current_module, env)?);
                }
                if tys.len() < 2 {
                    return Err(TypeError::Msg("tuple needs two or more slots".into()));
                }
                Ok(Type::Tuple(tys))
            }
            ExprKind::Interpolate { parts } => {
                for part in parts {
                    match part {
                        crate::ast::InterpPart::Lit(_) => {}
                        crate::ast::InterpPart::Expr(e) => {
                            let t = infer(e, enums, funs, methods, current_module, env)?;
                            if !matches!(t, Type::String | Type::Int | Type::Float)
                                && !is_meta_opaque(&t)
                            {
                                return Err(TypeError::Msg(format!(
                                    "interpolation hole must be String, Int, or Float, got {t:?}"
                                )));
                            }
                        }
                    }
                }
                Ok(Type::String)
            }
            ExprKind::IoPrintln(e) => {
                let t = infer(e, enums, funs, methods, current_module, env)?;
                expect_ty(&t, &Type::String)?;
                Ok(Type::Io(Box::new(Type::Unit)))
            }
            ExprKind::IoFail(e) => {
                let t = infer(e, enums, funs, methods, current_module, env)?;
                expect_ty(&t, &Type::String)?;
                Ok(fail_ty())
            }
            ExprKind::IoSleep(e) => {
                let t = infer(e, enums, funs, methods, current_module, env)?;
                expect_ty(&t, &Type::Int)?;
                Ok(Type::Io(Box::new(Type::Unit)))
            }
            ExprKind::IoPure(inner) => {
                let t = infer(inner, enums, funs, methods, current_module, env)?;
                Ok(Type::Io(Box::new(t)))
            }
            ExprKind::Var(name) => env
                .get(name)
                .cloned()
                .ok_or_else(|| TypeError::Msg(format!("unbound variable {name}"))),
            ExprKind::Placeholder => Err(TypeError::At {
                msg: "placeholder `_` needs a function argument (`List.map(xs, _ + 1)`)".into(),
                span: expr.span.clone(),
            }),
            ExprKind::Field { base, field } => {
                let bt = infer(base, enums, funs, methods, current_module, env)?;
                field_type(&bt, field, enums, current_module)
            }
            ExprKind::MethodCall {
                receiver,
                method,
                args,
            } => {
                let rt = infer(receiver, enums, funs, methods, current_module, env)?;
                if method == "require" {
                    infer_require_args(args, &rt, enums, funs, methods, current_module, env)?;
                    return Ok(rt);
                }
                if method == "copy" {
                    return infer_copy(
                        &rt,
                        args,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        &expr.span,
                    );
                }
                let (id, targs) = method_receiver_parts(&rt, method)?;
                let entry = methods.lookup(id, method)?;
                let (params, ret) = instantiate_method(entry, targs)?;
                if args.len() != params.len() {
                    return Err(TypeError::Msg(format!(
                        ".{method} expects {} arg(s), got {}",
                        params.len(),
                        args.len()
                    )));
                }
                for (arg, want) in args.iter().zip(params.iter()) {
                    let at = infer(arg, enums, funs, methods, current_module, env)?;
                    expect_ty(&at, want)?;
                }
                Ok(ret)
            }
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args,
                type_args,
            } => {
                let (en, id) = lookup_enum(enums, enum_name, current_module)?;
                let case = en
                    .cases
                    .iter()
                    .find(|c| c.name == *case_name)
                    .ok_or_else(|| {
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
                } else if type_args.len() == en.type_params.len()
                    && type_args.iter().all(|t| !contains_unbound(t))
                {
                    Ok(Type::App(id, type_args.clone()))
                } else {
                    let mut subst: HashMap<String, Type> = HashMap::new();
                    for (arg, (_fname, fty)) in args.iter().zip(case.fields.iter()) {
                        let at = infer(arg, enums, funs, methods, current_module, env)?;
                        let want = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
                        unify_construct(&want, &at, &mut subst)?;
                    }
                    // Params the args did not determine stay opaque placeholders.
                    // Elaboration resolves them from the expected type or errors.
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
                implicit_else,
            } => {
                let ct = infer(cond, enums, funs, methods, current_module, env)?;
                if !matches!(ct, Type::Bool) {
                    return Err(TypeError::Msg(format!(
                        "if condition must be Bool, got {ct:?}"
                    )));
                }
                let tt = infer(then_branch, enums, funs, methods, current_module, env)?;
                if *implicit_else {
                    implicit_else_ty(&tt)
                } else {
                    let et = infer(else_branch, enums, funs, methods, current_module, env)?;
                    prefer_join(&tt, &et).map_err(|_| {
                        TypeError::Msg(format!("if branches disagree: {tt:?} vs {et:?}"))
                    })
                }
            }
            ExprKind::Binary { op, left, right } => {
                let lt = infer(left, enums, funs, methods, current_module, env)?;
                let rt = infer(right, enums, funs, methods, current_module, env)?;
                match op {
                    BinOp::Add if matches!(lt, Type::String) && matches!(rt, Type::String) => {
                        Ok(Type::String)
                    }
                    BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                        if matches!(lt, Type::Float) && matches!(rt, Type::Float) {
                            if matches!(op, BinOp::Mod) {
                                return Err(TypeError::Msg("% needs Int".into()));
                            }
                            Ok(Type::Float)
                        } else if matches!(lt, Type::Int) && matches!(rt, Type::Int) {
                            if matches!(op, BinOp::Div | BinOp::Mod)
                                && matches!(right.kind, ExprKind::IntLit(0))
                            {
                                return Err(TypeError::Msg("division by zero".into()));
                            }
                            Ok(Type::Int)
                        } else {
                            Err(TypeError::Msg(format!(
                                "arithmetic needs Int or Float, got {lt:?} and {rt:?}"
                            )))
                        }
                    }
                    BinOp::Eq | BinOp::Ne => {
                        if types_compat(&lt, &rt)
                            || (matches!(lt, Type::String) && matches!(rt, Type::String))
                        {
                            Ok(Type::Bool)
                        } else {
                            Err(TypeError::Msg(format!(
                                "comparison type mismatch {lt:?} vs {rt:?}"
                            )))
                        }
                    }
                    BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                        if matches!(lt, Type::Int) && matches!(rt, Type::Int)
                            || matches!(lt, Type::Float) && matches!(rt, Type::Float)
                        {
                            Ok(Type::Bool)
                        } else {
                            Err(TypeError::Msg("ordered compare needs Int or Float".into()))
                        }
                    }
                    BinOp::And | BinOp::Or => {
                        if matches!(lt, Type::Bool) && matches!(rt, Type::Bool) {
                            Ok(Type::Bool)
                        } else {
                            Err(TypeError::Msg("&&/|| need Bool".into()))
                        }
                    }
                    BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor | BinOp::Shl | BinOp::Shr => {
                        if matches!(lt, Type::Int) && matches!(rt, Type::Int) {
                            Ok(Type::Int)
                        } else {
                            Err(TypeError::Msg(format!(
                                "bitwise ops need Int, got {lt:?} and {rt:?}"
                            )))
                        }
                    }
                }
            }
            ExprKind::Unary { op, expr } => {
                let t = infer(expr, enums, funs, methods, current_module, env)?;
                match op {
                    UnOp::Neg if matches!(t, Type::Int | Type::Float) => Ok(t),
                    UnOp::Not if matches!(t, Type::Bool) => Ok(Type::Bool),
                    UnOp::BitNot if matches!(t, Type::Int) => Ok(Type::Int),
                    UnOp::Neg => Err(TypeError::Msg(format!(
                        "unary `-` needs Int or Float, got {t:?}"
                    ))),
                    UnOp::Not => Err(TypeError::Msg(format!("unary `!` needs Bool, got {t:?}"))),
                    UnOp::BitNot => Err(TypeError::Msg(format!("unary `~` needs Int, got {t:?}"))),
                }
            }
            ExprKind::Ascribe { expr, ty } => {
                let want = resolve_ascribe_type(ty, enums, current_module)?;
                if let Type::Fun(a, b) = &want {
                    let got = infer_lambda_arg(
                        "ascribe",
                        expr,
                        a.as_ref().clone(),
                        Some(b.as_ref().clone()),
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                    )?;
                    expect_ty(&got, &want)?;
                    return Ok(want);
                }
                let got = infer_with_expected(
                    expr,
                    Some(&want),
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?;
                expect_ty(&got, &want)?;
                Ok(want)
            }
            ExprKind::NamedArg { name, .. } => Err(TypeError::Msg(format!(
                "named argument `{name}` is only allowed in a call"
            ))),
            ExprKind::Call { callee, args } => {
                if let Some(Type::Fun(param_ty, ret_ty)) = env.get(callee).cloned() {
                    let at = infer_fun_apply_args(
                        callee,
                        &param_ty,
                        args,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                    )?;
                    if args.len() == 1 && !types_compat(&at, &param_ty) {
                        return Err(TypeError::Msg(format!(
                            "{callee} arg type mismatch: expected {param_ty:?}, got {at:?}"
                        )));
                    }
                    Ok(*ret_ty)
                } else {
                    infer_call(callee, args, enums, funs, methods, current_module, env)
                }
            }
            ExprKind::Apply { fun, arg } => {
                let at = infer(arg, enums, funs, methods, current_module, env)?;
                let ft = infer_lambda_arg(
                    "apply",
                    fun,
                    at.clone(),
                    None,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?;
                let Type::Fun(a, b) = ft else {
                    return Err(TypeError::Msg(format!("apply needs A => B, got {ft:?}")));
                };
                if !types_compat(&at, &a) {
                    return Err(TypeError::Msg(format!(
                        "apply arg type mismatch: expected {a:?}, got {at:?}"
                    )));
                }
                Ok(*b)
            }
            ExprKind::Match { scrutinee, arms } => {
                let st = infer(scrutinee, enums, funs, methods, current_module, env)?;
                let mut result: Option<Type> = None;
                for arm in arms {
                    let bound =
                        bind_pattern(&arm.pattern, &st, enums, current_module, env, arm.unpack)?;
                    if let Some(g) = &arm.guard {
                        let gt = infer(g, enums, funs, methods, current_module, env)?;
                        if !matches!(gt, Type::Bool) {
                            unbind_pattern(bound, env);
                            return Err(TypeError::Msg(format!(
                                "match guard must be Bool, got {gt:?}"
                            )));
                        }
                    }
                    let bt = infer(&arm.body, enums, funs, methods, current_module, env)?;
                    unbind_pattern(bound, env);
                    match &result {
                        None => result = Some(bt),
                        Some(prev) => match prefer_join(prev, &bt) {
                            Ok(joined) => result = Some(joined),
                            Err(_) => {
                                return Err(TypeError::Msg(format!(
                                    "match arms disagree: {prev:?} vs {bt:?}"
                                )))
                            }
                        },
                    }
                }
                let ty = result.ok_or_else(|| TypeError::Msg("empty match".into()))?;
                check_match_exhaustive(&st, arms, enums, current_module)?;
                Ok(ty)
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
                    return Err(TypeError::Msg("flatMap body must return IO[_]".into()));
                }
                Ok(bt)
            }
            ExprKind::IoMap { inner, param, body } => {
                let it = infer(inner, enums, funs, methods, current_module, env)?;
                let Type::Io(inner_t) = it else {
                    return Err(TypeError::Msg("map receiver must be IO[_]".into()));
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
                Ok(Type::Io(Box::new(bt)))
            }
            ExprKind::HandleErrorWith { inner, param, body } => {
                let it = infer(inner, enums, funs, methods, current_module, env)?;
                if !matches!(it, Type::Io(_)) {
                    return Err(TypeError::Msg(
                        "handleErrorWith receiver must be IO[_]".into(),
                    ));
                }
                let old = bind_opt(param.as_ref(), Type::String, env);
                let bt = infer(body, enums, funs, methods, current_module, env)?;
                restore_opt(old, env);
                if !matches!(bt, Type::Io(_)) {
                    return Err(TypeError::Msg(
                        "handleErrorWith body must return IO[_]".into(),
                    ));
                }
                if types_compat(&it, &bt) {
                    Ok(prefer_concrete(&it, &bt))
                } else {
                    Ok(bt)
                }
            }
            ExprKind::Attempt { inner } => {
                let it = infer(inner, enums, funs, methods, current_module, env)?;
                let Type::Io(inner_ty) = it else {
                    return Err(TypeError::Msg("attempt receiver must be IO[_]".into()));
                };
                Ok(Type::Io(Box::new(Type::App(
                    "Result".into(),
                    vec![*inner_ty],
                ))))
            }
            ExprKind::Lambda {
                param,
                param_ty,
                body,
                ..
            } => {
                // Bare lambdas (View.button tap). Kit Call args bind String/Int
                // and check the body type in infer_call via infer_lambda_arg.
                // `(x: T) =>` is an `A => B` value. Untyped `x =>` stays a tap.
                let bind_ty = if let Some(ann) = param_ty {
                    resolve_ascribe_type(ann, enums, current_module)?
                } else {
                    Type::Opaque("Param".into())
                };
                let old = bind_opt(param.as_ref(), bind_ty.clone(), env);
                let result = infer(body, enums, funs, methods, current_module, env);
                restore_opt(old, env);
                let bt = result?;
                if param_ty.is_some() {
                    Ok(Type::Fun(Box::new(bind_ty), Box::new(bt)))
                } else {
                    Ok(Type::Opaque("TapFn".into()))
                }
            }
            ExprKind::IoRace { left, right } => {
                let lt = infer(left, enums, funs, methods, current_module, env)?;
                let rt = infer(right, enums, funs, methods, current_module, env)?;
                let Type::Io(a) = lt else {
                    return Err(TypeError::Msg("IO.race arguments must be IO[_]".into()));
                };
                let Type::Io(b) = rt else {
                    return Err(TypeError::Msg("IO.race arguments must be IO[_]".into()));
                };
                if !types_compat(&a, &b) {
                    return Err(TypeError::Msg(format!(
                        "IO.race arms disagree: {a:?} vs {b:?}"
                    )));
                }
                Ok(Type::Io(Box::new(prefer_concrete(&a, &b))))
            }
            ExprKind::IoBoth { left, right } => {
                let lt = infer(left, enums, funs, methods, current_module, env)?;
                let rt = infer(right, enums, funs, methods, current_module, env)?;
                let Type::Io(a) = lt else {
                    return Err(TypeError::Msg("IO.both arguments must be IO[_]".into()));
                };
                let Type::Io(b) = rt else {
                    return Err(TypeError::Msg("IO.both arguments must be IO[_]".into()));
                };
                Ok(Type::Io(Box::new(Type::Tuple(vec![*a, *b]))))
            }
            ExprKind::IoEnsure { inner, finalizer } => {
                let it = infer(inner, enums, funs, methods, current_module, env)?;
                let ft = infer(finalizer, enums, funs, methods, current_module, env)?;
                let Type::Io(inner_ty) = it else {
                    return Err(TypeError::Msg("IO.ensure inner must be IO[_]".into()));
                };
                if !types_compat(&ft, &Type::Io(Box::new(Type::Unit))) {
                    return Err(TypeError::Msg(
                        "IO.ensure finalizer must be IO[Unit]".into(),
                    ));
                }
                Ok(Type::Io(inner_ty))
            }
            ExprKind::IoTimeout { ms, inner } => {
                let mt = infer(ms, enums, funs, methods, current_module, env)?;
                expect_ty(&mt, &Type::Int)?;
                let it = infer(inner, enums, funs, methods, current_module, env)?;
                let Type::Io(inner_ty) = it else {
                    return Err(TypeError::Msg("IO.timeout inner must be IO[_]".into()));
                };
                Ok(Type::Io(inner_ty))
            }
            ExprKind::For { .. } => Err(TypeError::Msg(
                "internal: unlowered `for` (run lower before typecheck)".into(),
            )),
        };
        result
    })()
    .map_err(|e| e.with_span_if_bare(&expr.span))
}

/// Wrap one `_` hole as `__ph => …`. Nested lambdas keep their own holes.
fn wrap_placeholder(expr: Expr) -> Result<Expr, TypeError> {
    let n = crate::ast::count_placeholders(&expr);
    if n == 0 {
        return Ok(expr);
    }
    if n > 1 {
        return Err(TypeError::At {
            msg: "placeholder `_` allows one hole per lambda".into(),
            span: expr.span.clone(),
        });
    }
    let span = expr.span.clone();
    let body = crate::ast::replace_placeholder(expr, crate::ast::PLACEHOLDER_PARAM);
    Ok(Expr::new(
        ExprKind::Lambda {
            param: Some(crate::ast::PLACEHOLDER_PARAM.into()),
            param_ty: None,
            pat: None,
            body: Box::new(body),
        },
        span,
    ))
}

fn infer_fun_apply_args(
    callee: &str,
    param_ty: &Type,
    args: &[Expr],
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    if args.len() == 1 {
        return infer_with_expected(
            &args[0],
            Some(param_ty),
            enums,
            funs,
            methods,
            current_module,
            env,
        );
    }
    match param_ty {
        Type::Tuple(slots) if slots.len() == args.len() => {
            for (a, slot) in args.iter().zip(slots.iter()) {
                let t =
                    infer_with_expected(a, Some(slot), enums, funs, methods, current_module, env)?;
                if !types_compat(&t, slot) {
                    return Err(TypeError::Msg(format!(
                        "{callee} arg type mismatch: expected {slot:?}, got {t:?}"
                    )));
                }
            }
            Ok(param_ty.clone())
        }
        Type::Tuple(slots) => Err(TypeError::Msg(format!(
            "{callee} expects {} args, got {}",
            slots.len(),
            args.len()
        ))),
        _ => Err(TypeError::Msg(format!(
            "{callee} expects 1 arg, got {}",
            args.len()
        ))),
    }
}

/// Wrap a def as `x => callee(x)`, or `(a, b) => callee(a, b)` when the expected
/// parameter is a tuple and the def has that many params.
fn wrap_eta_arg(
    expr: Expr,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    module: &str,
    expected_param: Option<&Type>,
) -> Expr {
    if matches!(expr.kind, ExprKind::Lambda { .. }) {
        return expr;
    }
    if is_fun_value_call(&expr, funs, module) {
        return expr;
    }
    let span = expr.span.clone();
    let callee = match &expr.kind {
        ExprKind::Call { callee, args } if args.is_empty() => callee.clone(),
        ExprKind::Var(name) if !env.contains_key(name) => name.clone(),
        _ => return expr,
    };
    if let Some(Type::Tuple(slots)) = expected_param {
        let n = slots.len();
        if (2..=crate::ast::MAX_TUPLE_ARITY).contains(&n)
            && funs
                .resolve(&callee, module)
                .ok()
                .is_some_and(|f| f.params.len() == n)
        {
            return wrap_eta_tuple(callee, slots, span);
        }
    }
    let param = crate::ast::ETA_PARAM.to_string();
    let arg = Expr::new(ExprKind::Var(param.clone()), span.clone());
    let body = Expr::new(
        ExprKind::Call {
            callee,
            args: vec![arg],
        },
        span.clone(),
    );
    Expr::new(
        ExprKind::Lambda {
            param: Some(param),
            param_ty: None,
            pat: None,
            body: Box::new(body),
        },
        span,
    )
}

fn wrap_eta_tuple(callee: String, slots: &[Type], span: crate::span::Span) -> Expr {
    let param = crate::ast::ETA_PARAM.to_string();
    let mut binds = Vec::with_capacity(slots.len());
    let mut args = Vec::with_capacity(slots.len());
    for i in 0..slots.len() {
        let name = format!("{}{i}", crate::ast::ETA_PARAM);
        binds.push(crate::ast::Pattern::Bind(name.clone()));
        args.push(Expr::new(ExprKind::Var(name), span.clone()));
    }
    let body = Expr::new(
        ExprKind::Match {
            scrutinee: Box::new(Expr::new(ExprKind::Var(param.clone()), span.clone())),
            arms: vec![crate::ast::MatchArm::unpack(
                crate::ast::Pattern::Tuple {
                    elems: binds,
                    tys: slots.to_vec(),
                },
                Expr::new(ExprKind::Call { callee, args }, span.clone()),
            )],
        },
        span.clone(),
    );
    Expr::new(
        ExprKind::Lambda {
            param: Some(param),
            param_ty: None,
            pat: None,
            body: Box::new(body),
        },
        span,
    )
}

fn as_lambda_arg(
    expr: Expr,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    module: &str,
    expected_param: Option<&Type>,
) -> Result<Expr, TypeError> {
    if matches!(expr.kind, ExprKind::Lambda { .. }) {
        Ok(expr)
    } else if crate::ast::count_placeholders(&expr) > 0 {
        wrap_placeholder(expr)
    } else {
        Ok(wrap_eta_arg(expr, env, funs, module, expected_param))
    }
}

fn is_fun_value_call(e: &Expr, funs: &FunIndex<'_>, module: &str) -> bool {
    match &e.kind {
        ExprKind::Call { callee, args } if args.is_empty() => funs
            .resolve(callee, module)
            .ok()
            .is_some_and(|f| f.params.is_empty() && matches!(f.ret, Type::Fun(_, _))),
        _ => false,
    }
}

fn is_eta_candidate(
    e: &Expr,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    module: &str,
) -> bool {
    if is_fun_value_call(e, funs, module) {
        return false;
    }
    match &e.kind {
        ExprKind::Call { args, .. } if args.is_empty() => true,
        ExprKind::Var(name) if !env.contains_key(name) => true,
        _ => false,
    }
}

fn is_callback_shape(e: &Expr, env: &HashMap<String, Type>) -> bool {
    matches!(e.kind, ExprKind::Lambda { .. })
        || crate::ast::count_placeholders(e) > 0
        || match &e.kind {
            ExprKind::Call { args, .. } if args.is_empty() => true,
            ExprKind::Var(name) if !env.contains_key(name) => true,
            ExprKind::Var(name) if matches!(env.get(name), Some(Type::Fun(_, _))) => true,
            ExprKind::Ascribe {
                ty: Type::Fun(_, _),
                ..
            } => true,
            _ => false,
        }
}

// Arity comes from the shared typecheck context (enums, funs, methods, module, env).
#[allow(clippy::too_many_arguments)]
fn infer_lambda_arg(
    callee: &str,
    expr: &Expr,
    param_ty: Type,
    ret_ty: Option<Type>,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
) -> Result<Type, TypeError> {
    let wrapped;
    let expr = if !matches!(expr.kind, ExprKind::Lambda { .. })
        && crate::ast::count_placeholders(expr) > 0
    {
        wrapped = wrap_placeholder(expr.clone())?;
        &wrapped
    } else if !matches!(expr.kind, ExprKind::Lambda { .. })
        && is_eta_candidate(expr, env, funs, current_module)
    {
        wrapped = wrap_eta_arg(expr.clone(), env, funs, current_module, Some(&param_ty));
        &wrapped
    } else {
        expr
    };
    if let ExprKind::Lambda {
        param,
        param_ty: ann,
        body,
        ..
    } = &expr.kind
    {
        if let Some(ann) = ann {
            let got = resolve_ascribe_type(ann, enums, current_module)?;
            if !types_compat(&got, &param_ty) {
                return Err(TypeError::Msg(format!(
                    "lambda param type mismatch: expected {param_ty}, got {got}"
                )));
            }
        }
        let old = bind_opt(param.as_ref(), param_ty.clone(), env);
        let result = infer(body, enums, funs, methods, current_module, env);
        restore_opt(old, env);
        let bt = result?;
        if let Some(want) = ret_ty {
            if !kit_lambda_body_ok(&bt, &want) {
                return Err(TypeError::Msg(format!(
                    "{callee} lambda must return {}, got {bt:?}",
                    kit_ret_label(&want)
                )));
            }
        }
        Ok(Type::Fun(Box::new(param_ty), Box::new(bt)))
    } else if callee == "Ui.run" {
        Err(TypeError::Msg("Ui.run expects _ => View".into()))
    } else {
        let got = infer(expr, enums, funs, methods, current_module, env)?;
        if let Type::Fun(a, b) = &got {
            if !types_compat(a, &param_ty) {
                return Err(TypeError::Msg(format!(
                    "{callee} function param mismatch: expected {param_ty}, got {a}"
                )));
            }
            if let Some(want) = ret_ty {
                if !kit_lambda_body_ok(b, &want) {
                    return Err(TypeError::Msg(format!(
                        "{callee} lambda must return {}, got {b:?}",
                        kit_ret_label(&want)
                    )));
                }
            }
        }
        Ok(got)
    }
}

#[inline(never)]
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
    let nargs = args.len();
    for (i, a) in args.iter().enumerate() {
        arg_tys.push(
            if let Some(pty) = kit_lambda_param_ty_at(callee, i, nargs, &arg_tys) {
                infer_lambda_arg(
                    callee,
                    a,
                    pty,
                    kit_lambda_ret_ty(callee, i, nargs),
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?
            } else if let Some((pty, rty)) =
                user_fun_lambda_expected(callee, i, enums, funs, current_module)
            {
                infer_lambda_arg(
                    callee,
                    a,
                    pty,
                    rty,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                )?
            } else if let Some(want) = user_fun_param_ty(callee, i, enums, funs, current_module) {
                infer_with_expected(a, Some(&want), enums, funs, methods, current_module, env)?
            } else {
                infer(a, enums, funs, methods, current_module, env)?
            },
        );
    }
    match callee {
        "Str.concat" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "Str.len" | "Str.charAt" | "Str.indexOf" | "Str.lastIndexOf" => {
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
        "Str.startsWith" | "Str.eq" | "Str.contains" | "Str.endsWith" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Bool)
        }
        "Str.toInt" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Int)
        }
        "Str.replace" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::String)
        }
        "Str.split" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(list_of(Type::String))
        }
        "Str.slice" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            Ok(Type::String)
        }
        "Str.take" | "Str.drop" | "Str.takeRight" | "Str.dropRight" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::String)
        }
        "Str.reverse" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::String)
        }
        "Str.fromInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::String)
        }
        "Float.fromInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Float)
        }
        "Float.toInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Float)?;
            Ok(Type::Int)
        }
        "Str.lines" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(list_of(Type::String))
        }
        "Str.trim" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::String)
        }
        "Str.isEmpty" | "Str.nonEmpty" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Bool)
        }
        "Str.toLower" | "Str.toUpper" | "Str.capitalize" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::String)
        }
        "Str.repeat" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::String)
        }
        "Str.stripPrefix" | "Str.stripSuffix" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "Str.padLeft" | "Str.padRight" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::String)
        }
        "Str.isBlank" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Bool)
        }
        "List.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(list_of(Type::Opaque("Elem".into())))
        }
        "List.cons" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = prefer_elem(&arg_tys[0], &list_elem(&arg_tys[1])?)?;
            Ok(list_of(elem))
        }
        "List.isEmpty" | "List.nonEmpty" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "List.head" | "List.at" => {
            if callee == "List.head" {
                expect_arity(callee, &arg_tys, 1)?;
            } else {
                expect_arity(callee, &arg_tys, 2)?;
                expect_ty(&arg_tys[1], &Type::Int)?;
            }
            list_elem(&arg_tys[0])
        }
        "List.tail" | "List.reverse" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(arg_tys[0].clone())
        }
        "List.len" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "List.join" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &list_of(Type::String))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "List.append" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[1])?;
            Ok(list_of(elem))
        }
        "List.setAt" => {
            expect_arity(callee, &arg_tys, 3)?;
            let elem = prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[2])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(list_of(elem))
        }
        "List.filter" | "List.filterNot" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(elem))
        }
        "List.take" | "List.drop" | "List.takeRight" | "List.dropRight" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(arg_tys[0].clone())
        }
        "List.splitAt" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(list_of(list_of(elem)))
        }
        "List.slice" => {
            expect_arity(callee, &arg_tys, 3)?;
            list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            Ok(arg_tys[0].clone())
        }
        "List.indices" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(list_of(Type::Int))
        }
        "List.concat" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[1])?;
            Ok(list_of(prefer_elem(&a, &b)?))
        }
        "List.flatten" => {
            expect_arity(callee, &arg_tys, 1)?;
            let inner = list_elem(&arg_tys[0])?;
            let elem = list_elem(&inner)?;
            Ok(list_of(elem))
        }
        "List.init" | "List.last" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(elem))
        }
        "List.getOrElse" => {
            expect_arity(callee, &arg_tys, 3)?;
            let elem = list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            prefer_elem(&elem, &arg_tys[2])
        }
        "List.fill" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(list_of(arg_tys[1].clone()))
        }
        "List.range" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(list_of(Type::Int))
        }
        "List.tabulate" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            let out = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                Type::Opaque(_) => Type::Opaque("Elem".into()),
                other => other.clone(),
            };
            Ok(list_of(out))
        }
        "List.intersperse" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[1])?;
            Ok(list_of(elem))
        }
        "List.grouped" | "List.sliding" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(list_of(list_of(elem)))
        }
        "List.inits" | "List.tails" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(list_of(elem)))
        }
        "List.zip" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[1])?;
            Ok(list_of(Type::Tuple(vec![a, b])))
        }
        "List.zipAll" => {
            expect_arity(callee, &arg_tys, 4)?;
            let a = prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[2])?;
            let b = prefer_elem(&list_elem(&arg_tys[1])?, &arg_tys[3])?;
            Ok(list_of(Type::Tuple(vec![a, b])))
        }
        "List.unzip" => {
            expect_arity(callee, &arg_tys, 1)?;
            let inner = list_elem(&arg_tys[0])?;
            if is_meta_opaque(&inner) {
                return Ok(Type::Tuple(vec![list_of(inner.clone()), list_of(inner)]));
            }
            match inner {
                Type::Tuple(xs) if xs.len() == 2 => Ok(Type::Tuple(vec![
                    list_of(xs[0].clone()),
                    list_of(xs[1].clone()),
                ])),
                other => Err(TypeError::Msg(format!(
                    "List.unzip needs List[(A, B)], got List[{other:?}]"
                ))),
            }
        }
        "List.transpose" => {
            expect_arity(callee, &arg_tys, 1)?;
            let inner = list_elem(&arg_tys[0])?;
            let elem = list_elem(&inner)?;
            Ok(list_of(list_of(elem)))
        }
        "List.contains" => {
            expect_arity(callee, &arg_tys, 2)?;
            prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[1])?;
            Ok(Type::Bool)
        }
        "List.indexOf" | "List.lastIndexOf" => {
            expect_arity(callee, &arg_tys, 2)?;
            prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[1])?;
            Ok(Type::Int)
        }
        "List.distinct" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(arg_tys[0].clone())
        }
        "List.distinctBy" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            let key = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                other => other.clone(),
            };
            expect_map_key(callee, &key)?;
            Ok(arg_tys[0].clone())
        }
        "List.toMap" => {
            expect_arity(callee, &arg_tys, 1)?;
            let inner = list_elem(&arg_tys[0])?;
            if is_meta_opaque(&inner) {
                return Ok(Type::App(
                    "Map".into(),
                    vec![Type::Opaque("Elem".into()), Type::Opaque("Elem".into())],
                ));
            }
            match inner {
                Type::Tuple(xs) if xs.len() == 2 => {
                    if !matches!(xs[0], Type::Int | Type::String) && !is_meta_opaque(&xs[0]) {
                        return Err(TypeError::Msg(format!(
                            "List.toMap key must be Int or String, got {}",
                            xs[0]
                        )));
                    }
                    Ok(Type::App("Map".into(), vec![xs[0].clone(), xs[1].clone()]))
                }
                other => Err(TypeError::Msg(format!(
                    "List.toMap needs List[(K, V)], got List[{other:?}]"
                ))),
            }
        }
        "List.toSet" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            if !matches!(elem, Type::Int | Type::String) && !is_meta_opaque(&elem) {
                return Err(TypeError::Msg(format!(
                    "List.toSet needs List[Int] or List[String], got List[{elem}]"
                )));
            }
            Ok(Type::App("Set".into(), vec![elem]))
        }
        "List.diff" | "List.intersect" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[1])?;
            Ok(list_of(prefer_elem(&a, &b)?))
        }
        "List.startsWith" | "List.endsWith" | "List.sameElements" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[1])?;
            prefer_elem(&a, &b)?;
            Ok(Type::Bool)
        }
        "List.patch" => {
            expect_arity(callee, &arg_tys, 4)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[2])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[3], &Type::Int)?;
            Ok(list_of(prefer_elem(&a, &b)?))
        }
        "List.find" | "List.findLast" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(elem))
        }
        "List.prefixLength" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "List.segmentLength" => {
            expect_arity(callee, &arg_tys, 3)?;
            list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            Ok(Type::Int)
        }
        "List.indexOfSlice" | "List.lastIndexOfSlice" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = list_elem(&arg_tys[0])?;
            let b = list_elem(&arg_tys[1])?;
            prefer_elem(&a, &b)?;
            Ok(Type::Int)
        }
        "List.isDefinedAt" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Bool)
        }
        "List.lengthCompare" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Int)
        }
        "List.sort" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            expect_ordered_elem(callee, &elem)?;
            Ok(arg_tys[0].clone())
        }
        "List.max" | "List.min" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            expect_ordered_elem(callee, &elem)?;
            Ok(elem)
        }
        "List.sortBy" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            Ok(arg_tys[0].clone())
        }
        "List.maxBy" | "List.minBy" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])
        }
        "List.groupBy" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            let key = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                other => other.clone(),
            };
            expect_map_key(callee, &key)?;
            Ok(Type::App("Map".into(), vec![key, list_of(elem)]))
        }
        "List.sum" | "List.product" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_int_elem(callee, &list_elem(&arg_tys[0])?)?;
            Ok(Type::Int)
        }
        "List.zipWithIndex" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(Type::Tuple(vec![Type::Int, elem])))
        }
        "List.foldLeft" | "List.foldRight" => {
            expect_arity(callee, &arg_tys, 3)?;
            list_elem(&arg_tys[0])?;
            let ret = match &arg_tys[2] {
                Type::Fun(_, r) => (**r).clone(),
                other => other.clone(),
            };
            prefer_named(&arg_tys[1], &ret, "fold accumulator")
        }
        "List.scanLeft" | "List.scanRight" => {
            expect_arity(callee, &arg_tys, 3)?;
            list_elem(&arg_tys[0])?;
            let ret = match &arg_tys[2] {
                Type::Fun(_, r) => (**r).clone(),
                other => other.clone(),
            };
            Ok(list_of(prefer_named(
                &arg_tys[1],
                &ret,
                "scan accumulator",
            )?))
        }
        "List.reduceLeft" | "List.reduceRight" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            let ret = match &arg_tys[1] {
                Type::Fun(_, r) => (**r).clone(),
                other => other.clone(),
            };
            prefer_named(&elem, &ret, "reduce element")
        }
        "List.exists" | "List.forall" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "List.indexWhere" | "List.lastIndexWhere" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "List.count" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "List.takeWhile" | "List.dropWhile" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(elem))
        }
        "List.span" | "List.partition" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(list_of(list_of(elem)))
        }
        "List.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            let out = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                Type::Opaque(_) => Type::Opaque("Elem".into()),
                other => other.clone(),
            };
            Ok(list_of(out))
        }
        "List.flatMap" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            let inner = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                other => other.clone(),
            };
            let elem = list_elem(&inner)?;
            Ok(list_of(elem))
        }
        "List.padTo" => {
            expect_arity(callee, &arg_tys, 3)?;
            let elem = prefer_elem(&list_elem(&arg_tys[0])?, &arg_tys[2])?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(list_of(elem))
        }
        "Map.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::App(
                "Map".into(),
                vec![Type::Opaque("Elem".into()), Type::Opaque("Elem".into())],
            ))
        }
        "Map.set" => {
            expect_arity(callee, &arg_tys, 3)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            let k = prefer_elem(&k, &arg_tys[1])?;
            let v = prefer_elem(&v, &arg_tys[2])?;
            Ok(Type::App("Map".into(), vec![k, v]))
        }
        "Map.get" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            prefer_elem(&k, &arg_tys[1])?;
            Ok(list_of(v))
        }
        "Map.getOrElse" => {
            expect_arity(callee, &arg_tys, 3)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            prefer_elem(&k, &arg_tys[1])?;
            prefer_elem(&v, &arg_tys[2])
        }
        "Map.contains" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k, _) = map_kv(&arg_tys[0])?;
            prefer_elem(&k, &arg_tys[1])?;
            Ok(Type::Bool)
        }
        "Map.remove" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            let k = prefer_elem(&k, &arg_tys[1])?;
            Ok(Type::App("Map".into(), vec![k, v]))
        }
        "Map.keys" => {
            expect_arity(callee, &arg_tys, 1)?;
            let (k, _) = map_kv(&arg_tys[0])?;
            Ok(list_of(k))
        }
        "Map.values" => {
            expect_arity(callee, &arg_tys, 1)?;
            let (_, v) = map_kv(&arg_tys[0])?;
            Ok(list_of(v))
        }
        "Map.toList" => {
            expect_arity(callee, &arg_tys, 1)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            Ok(list_of(Type::Tuple(vec![k, v])))
        }
        "Map.size" => {
            expect_arity(callee, &arg_tys, 1)?;
            map_kv(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "Map.isEmpty" | "Map.nonEmpty" => {
            expect_arity(callee, &arg_tys, 1)?;
            map_kv(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "Map.union" | "Map.intersect" | "Map.diff" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k1, v1) = map_kv(&arg_tys[0])?;
            let (k2, v2) = map_kv(&arg_tys[1])?;
            let k = prefer_elem(&k1, &k2)?;
            let v = prefer_elem(&v1, &v2)?;
            Ok(Type::App("Map".into(), vec![k, v]))
        }
        "Map.filter" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k, v) = map_kv(&arg_tys[0])?;
            Ok(Type::App("Map".into(), vec![k, v]))
        }
        "Map.mapValues" => {
            expect_arity(callee, &arg_tys, 2)?;
            let (k, _) = map_kv(&arg_tys[0])?;
            let out = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                Type::Opaque(_) => Type::Opaque("Elem".into()),
                other => other.clone(),
            };
            Ok(Type::App("Map".into(), vec![k, out]))
        }
        "Map.exists" | "Map.forall" => {
            expect_arity(callee, &arg_tys, 2)?;
            map_kv(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "Set.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::App("Set".into(), vec![Type::Opaque("Elem".into())]))
        }
        "Set.add" => {
            expect_arity(callee, &arg_tys, 2)?;
            let e = set_elem(&arg_tys[0])?;
            let e = prefer_elem(&e, &arg_tys[1])?;
            Ok(Type::App("Set".into(), vec![e]))
        }
        "Set.contains" => {
            expect_arity(callee, &arg_tys, 2)?;
            let e = set_elem(&arg_tys[0])?;
            prefer_elem(&e, &arg_tys[1])?;
            Ok(Type::Bool)
        }
        "Set.remove" => {
            expect_arity(callee, &arg_tys, 2)?;
            let e = set_elem(&arg_tys[0])?;
            let e = prefer_elem(&e, &arg_tys[1])?;
            Ok(Type::App("Set".into(), vec![e]))
        }
        "Set.toList" => {
            expect_arity(callee, &arg_tys, 1)?;
            let e = set_elem(&arg_tys[0])?;
            Ok(list_of(e))
        }
        "Set.size" => {
            expect_arity(callee, &arg_tys, 1)?;
            set_elem(&arg_tys[0])?;
            Ok(Type::Int)
        }
        "Set.isEmpty" | "Set.nonEmpty" => {
            expect_arity(callee, &arg_tys, 1)?;
            set_elem(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "Set.union" | "Set.intersect" | "Set.diff" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = set_elem(&arg_tys[0])?;
            let b = set_elem(&arg_tys[1])?;
            Ok(Type::App("Set".into(), vec![prefer_elem(&a, &b)?]))
        }
        "Set.isSubset" | "Set.isDisjoint" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = set_elem(&arg_tys[0])?;
            let b = set_elem(&arg_tys[1])?;
            prefer_elem(&a, &b)?;
            Ok(Type::Bool)
        }
        "Set.filter" => {
            expect_arity(callee, &arg_tys, 2)?;
            let e = set_elem(&arg_tys[0])?;
            Ok(Type::App("Set".into(), vec![e]))
        }
        "Set.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            set_elem(&arg_tys[0])?;
            let key = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                other => other.clone(),
            };
            expect_map_key(callee, &key)?;
            Ok(Type::App("Set".into(), vec![key]))
        }
        "Set.exists" | "Set.forall" => {
            expect_arity(callee, &arg_tys, 2)?;
            set_elem(&arg_tys[0])?;
            Ok(Type::Bool)
        }
        "Fs.read" | "Fs.list" | "Fs.mkdirs" | "Fs.canonicalize" | "Fs.exists" | "Fs.delete"
        | "Fs.walk" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(match callee {
                "Fs.read" => Type::Io(Box::new(Type::String)),
                "Fs.list" | "Fs.walk" => Type::Io(Box::new(list_of(Type::Tuple(vec![
                    Type::String,
                    Type::Bool,
                ])))),
                "Fs.canonicalize" => Type::Io(Box::new(Type::String)),
                "Fs.exists" => Type::Io(Box::new(Type::Bool)),
                _ => Type::Io(Box::new(Type::Unit)),
            })
        }
        "Fs.join" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::String)
        }
        "Fs.dirname" | "Fs.basename" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::String)
        }
        "Json.parse" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Adt("Json".into()))
        }
        "Json.stringify" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Adt("Json".into()))?;
            Ok(Type::String)
        }
        "Fs.write" | "Fs.rename" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Sys.args" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(list_of(Type::String))))
        }
        "Sys.readLine" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Sys.read" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Sys.write" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Sys.exec" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Tuple(vec![
                Type::Int,
                Type::String,
                Type::String,
            ]))))
        }
        "Sys.spawn" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Sys.childWrite" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Sys.childRead" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::String)))
        }
        "Sys.childClose" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Sys.alive" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Int)))
        }
        "Sys.kill" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
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
            match &arg_tys[1] {
                Type::Fun(_, ret) if matches!(ret.as_ref(), Type::Io(p) if matches!(p.as_ref(), Type::String)) =>
                    {}
                _ => {
                    return Err(TypeError::Msg(format!(
                        "{callee} lambda must return IO[String]"
                    )));
                }
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Impurity.runKit" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ref.of" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Io(Box::new(handle_ty("Ref", arg_tys[0].clone()))))
        }
        "Ref.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Ref")?);
            Ok(Type::Io(Box::new(payload)))
        }
        "Ref.set" => {
            expect_arity(callee, &arg_tys, 2)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Ref")?);
            expect_ty(&arg_tys[1], &payload)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ref.update" | "Ref.updateAndGet" => {
            expect_arity(callee, &arg_tys, 2)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Ref")?);
            if !is_callback_shape(&args[1], env) {
                return Err(TypeError::Msg(format!(
                    "{callee} callback must be a lambda"
                )));
            }
            let Type::Fun(a, b) = &arg_tys[1] else {
                return Err(TypeError::Msg(format!(
                    "{callee} callback must be a lambda"
                )));
            };
            expect_ty(a, &payload)?;
            expect_ty(b, &payload)?;
            if callee == "Ref.update" {
                Ok(Type::Io(Box::new(Type::Unit)))
            } else {
                Ok(Type::Io(Box::new(payload)))
            }
        }
        "Queue.unbounded" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(handle_ty(
                "Queue",
                Type::Opaque("Elem".into()),
            ))))
        }
        "Queue.offer" => {
            expect_arity(callee, &arg_tys, 2)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Queue")?);
            expect_ty(&arg_tys[1], &payload)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Queue.take" => {
            expect_arity(callee, &arg_tys, 1)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Queue")?);
            Ok(Type::Io(Box::new(payload)))
        }
        "Deferred.empty" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Io(Box::new(handle_ty(
                "Deferred",
                Type::Opaque("Elem".into()),
            ))))
        }
        "Deferred.complete" => {
            expect_arity(callee, &arg_tys, 2)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Deferred")?);
            expect_ty(&arg_tys[1], &payload)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Deferred.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            let payload = default_cell_payload(handle_payload_ty(&arg_tys[0], "Deferred")?);
            Ok(Type::Io(Box::new(payload)))
        }
        "Fiber.fork" => {
            expect_arity(callee, &arg_tys, 1)?;
            let Type::Io(inner) = arg_tys[0].clone() else {
                return Err(TypeError::Msg("Fiber.fork argument must be IO[_]".into()));
            };
            Ok(Type::Io(Box::new(handle_ty("Fiber", *inner))))
        }
        "Fiber.join" => {
            expect_arity(callee, &arg_tys, 1)?;
            let args = expect_handle(&arg_tys[0], "Fiber")?;
            let payload = args
                .first()
                .cloned()
                .unwrap_or_else(|| Type::Opaque("Any".into()));
            Ok(Type::Io(Box::new(payload)))
        }
        "Fiber.interrupt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_handle(&arg_tys[0], "Fiber")?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "IO.forever" => {
            expect_arity(callee, &arg_tys, 1)?;
            if !matches!(arg_tys[0], Type::Io(_)) {
                return Err(TypeError::Msg("IO.forever argument must be IO[_]".into()));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "IO.repeatN" | "IO.retryN" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            let Type::Io(inner_ty) = arg_tys[1].clone() else {
                return Err(TypeError::Msg(format!("{callee} inner must be IO[_]")));
            };
            Ok(Type::Io(inner_ty))
        }
        "IO.foreach" | "IO.foreachDiscard" => {
            expect_arity(callee, &arg_tys, 2)?;
            list_elem(&arg_tys[0])?;
            if !is_callback_shape(&args[1], env) {
                return Err(TypeError::Msg(format!(
                    "{callee} callback must be a lambda"
                )));
            }
            let Type::Fun(_, ret) = &arg_tys[1] else {
                return Err(TypeError::Msg(format!(
                    "{callee} callback must be a lambda"
                )));
            };
            let Type::Io(inner) = ret.as_ref() else {
                return Err(TypeError::Msg(format!("{callee} lambda must return IO[_]")));
            };
            if callee == "IO.foreachDiscard" {
                Ok(Type::Io(Box::new(Type::Unit)))
            } else {
                Ok(Type::Io(Box::new(list_of((**inner).clone()))))
            }
        }
        "IO.when" | "IO.unless" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Bool)?;
            expect_ty(&arg_tys[1], &Type::Io(Box::new(Type::Unit)))?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Resource.make" => {
            expect_arity(callee, &arg_tys, 2)?;
            let Type::Io(inner) = &arg_tys[0] else {
                return Err(TypeError::Msg("Resource.make acquire must be IO[_]".into()));
            };
            let payload = (**inner).clone();
            if !is_callback_shape(&args[1], env) {
                return Err(TypeError::Msg(
                    "Resource.make callback must be a lambda".into(),
                ));
            }
            let Type::Fun(a, bt) = &arg_tys[1] else {
                return Err(TypeError::Msg(
                    "Resource.make callback must be a lambda".into(),
                ));
            };
            expect_ty(a, &payload)?;
            expect_ty(bt, &Type::Io(Box::new(Type::Unit)))?;
            Ok(handle_ty("Resource", payload))
        }
        "Resource.use" => {
            expect_arity(callee, &arg_tys, 2)?;
            let payload = handle_payload_ty(&arg_tys[0], "Resource")?;
            if !is_callback_shape(&args[1], env) {
                return Err(TypeError::Msg(
                    "Resource.use callback must be a lambda".into(),
                ));
            }
            let Type::Fun(a, ret) = &arg_tys[1] else {
                return Err(TypeError::Msg(
                    "Resource.use callback must be a lambda".into(),
                ));
            };
            expect_ty(a, &payload)?;
            let Type::Io(inner) = ret.as_ref() else {
                return Err(TypeError::Msg(
                    "Resource.use lambda must return IO[_]".into(),
                ));
            };
            Ok(Type::Io(inner.clone()))
        }
        "Stream.emit" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(handle_ty("Stream", arg_tys[0].clone()))
        }
        "Stream.emits" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = list_elem(&arg_tys[0])?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.eval" => {
            expect_arity(callee, &arg_tys, 1)?;
            let Type::Io(inner) = &arg_tys[0] else {
                return Err(TypeError::Msg("Stream.eval argument must be IO[_]".into()));
            };
            Ok(handle_ty("Stream", (**inner).clone()))
        }
        "Stream.concat" => {
            expect_arity(callee, &arg_tys, 2)?;
            let a = handle_payload_ty(&arg_tys[0], "Stream")?;
            let b = handle_payload_ty(&arg_tys[1], "Stream")?;
            let elem = prefer_named(&a, &b, "Stream")?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.take" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.drop" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.evalMap" => {
            expect_arity(callee, &arg_tys, 2)?;
            handle_payload_ty(&arg_tys[0], "Stream")?;
            let out = match &arg_tys[1] {
                Type::Fun(_, ret) => match ret.as_ref() {
                    Type::Io(inner) => (**inner).clone(),
                    other => {
                        return Err(TypeError::Msg(format!(
                            "Stream.evalMap lambda must return IO[_], got {other:?}"
                        )));
                    }
                },
                Type::Opaque(_) => Type::Opaque("Elem".into()),
                other => {
                    return Err(TypeError::Msg(format!(
                        "Stream.evalMap callback must be a lambda, got {other:?}"
                    )));
                }
            };
            Ok(handle_ty("Stream", out))
        }
        "Stream.filter" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            handle_payload_ty(&arg_tys[0], "Stream")?;
            let out = match &arg_tys[1] {
                Type::Fun(_, ret) => (**ret).clone(),
                Type::Opaque(_) => Type::Opaque("Elem".into()),
                other => other.clone(),
            };
            Ok(handle_ty("Stream", out))
        }
        "Stream.takeWhile" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.dropWhile" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.find" => {
            expect_arity(callee, &arg_tys, 2)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(handle_ty("Stream", elem))
        }
        "Stream.exists" => {
            expect_arity(callee, &arg_tys, 2)?;
            handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(Type::Io(Box::new(Type::Bool)))
        }
        "Stream.compileToList" => {
            expect_arity(callee, &arg_tys, 1)?;
            let elem = handle_payload_ty(&arg_tys[0], "Stream")?;
            Ok(Type::Io(Box::new(list_of(elem))))
        }
        "Stream.drain" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_handle(&arg_tys[0], "Stream")?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Signal.int" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Opaque("SignalInt".into()))
        }
        "Signal.get" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            Ok(Type::Int)
        }
        "Signal.set" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
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
            expect_ty(&arg_tys[0], &Type::Opaque("SignalStr".into()))?;
            Ok(Type::String)
        }
        "Signal.setStr" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalStr".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Unit)
        }
        "Signal.list" => {
            expect_arity(callee, &arg_tys, 1)?;
            list_elem(&arg_tys[0])?;
            Ok(Type::Opaque("SignalList".into()))
        }
        "Signal.getList" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalList".into()))?;
            Ok(list_of(Type::String))
        }
        "Signal.setList" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalList".into()))?;
            list_elem(&arg_tys[1])?;
            Ok(Type::Unit)
        }
        "Signal.map" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            Ok(Type::Opaque("SignalStr".into()))
        }
        "Property.signalInt" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Int)
        }
        "Property.signalStr" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::String)
        }
        "Property.signalListLen" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Int)
        }
        "Property.signalListAt" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::String)
        }
        "Property.a11yHas" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Bool)
        }
        "Property.lastHitHas" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Bool)
        }
        "Property.assert" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            if !matches!(arg_tys[1], Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "Property.assert ok must be Bool, got {:?}",
                    arg_tys[1]
                )));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Property.check" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            if !matches!(arg_tys[1], Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "Property.check ok must be Bool, got {:?}",
                    arg_tys[1]
                )));
            }
            Ok(arg_tys[2].clone())
        }
        "Property.force" => {
            expect_arity(callee, &arg_tys, 1)?;
            match &arg_tys[0] {
                Type::Io(inner) if matches!(**inner, Type::Bool) => Ok(Type::Bool),
                other => Err(TypeError::Msg(format!(
                    "Property.force expects IO[Bool], got {other:?}"
                ))),
            }
        }
        "Property.sometimes" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Unit)
        }
        "Property.classify" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            if !matches!(arg_tys[1], Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "Property.classify hit must be Bool, got {:?}",
                    arg_tys[1]
                )));
            }
            Ok(Type::Bool)
        }
        "View.text" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.bindText" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalStr".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.button" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.iconButton" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.fab" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.outlinedButton" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.textButton" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.actionChip" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.checkbox" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.radio" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.slider" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.progress" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.circularProgress" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.avatar" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.switch" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.chip" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.filterChip" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.inputChip" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.choiceChip" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.listTile" => {
            if arg_tys.is_empty() || arg_tys.len() > 2 {
                return Err(TypeError::Msg(format!(
                    "View.listTile expects 1 or 2 args, got {}",
                    arg_tys.len()
                )));
            }
            expect_ty(&arg_tys[0], &Type::String)?;
            if arg_tys.len() == 2 {
                expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            }
            Ok(Type::Opaque("View".into()))
        }
        "View.checkboxListTile" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.switchListTile" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.radioListTile" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.segmented" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            expect_ty(&arg_tys[2], &Type::String)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.badge" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.tooltip" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.onSecondary" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.placeholder" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.semantics" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.mergeSemantics" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.inkWell" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            expect_ty(&arg_tys[2], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.visibility" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.offstage" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.unconstrainedBox" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("View".into()))?;
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
        "Color.rgba" => {
            expect_arity(callee, &arg_tys, 4)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            expect_ty(&arg_tys[2], &Type::Int)?;
            expect_ty(&arg_tys[3], &Type::Int)?;
            Ok(Type::Int)
        }
        "View.column" | "View.row" | "View.wrap" | "View.stack" => {
            // Nullary or children: `View.column(a, b, …)` adds each child.
            Ok(Type::Opaque("View".into()))
        }
        "View.divider" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.verticalDivider" => {
            expect_arity(callee, &arg_tys, 0)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.expansionTile" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::String)?;
            expect_ty(&arg_tys[2], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.grid" => {
            if arg_tys.is_empty() {
                return Err(TypeError::Msg(
                    "View.grid expects column count, got 0 args".into(),
                ));
            }
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.each" => {
            if arg_tys.len() != 1 && arg_tys.len() != 2 {
                return Err(TypeError::Msg(format!(
                    "View.each expects 1 or 2 args, got {}",
                    arg_tys.len()
                )));
            }
            Ok(Type::Opaque("View".into()))
        }
        "View.scroll" | "View.scrollH" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.expanded"
        | "View.stretch"
        | "View.center"
        | "View.clip"
        | "View.focusGroup"
        | "View.ellipsis"
        | "View.ignorePointer"
        | "View.absorbPointer"
        | "View.excludeSemantics"
        | "View.card" => {
            expect_arity(callee, &arg_tys, 1)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.padding" | "View.opacity" | "View.maxLines" | "View.textColor" | "View.gap"
        | "View.fontSize" | "View.background" | "View.radius" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            Ok(Type::Opaque("View".into()))
        }
        "View.align" | "View.positioned" | "View.sized" | "View.minSize" | "View.maxSize"
        | "View.aspectRatio" | "View.fraction" | "View.border" => {
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
        "View.editor" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalStr".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.split" => {
            expect_arity(callee, &arg_tys, 3)?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            expect_ty(&arg_tys[2], &Type::Opaque("View".into()))?;
            Ok(Type::Opaque("View".into()))
        }
        "View.overlay" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[1], &Type::Opaque("View".into()))?;
            expect_ty(&arg_tys[0], &Type::Opaque("SignalInt".into()))?;
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
            let ok = match &arg_tys[0] {
                Type::Fun(_, ret) => {
                    matches!(ret.as_ref(), Type::Opaque(n) if n == "View")
                }
                _ => false,
            };
            if !ok {
                return Err(TypeError::Msg(format!(
                    "Ui.run expects _ => View, got {:?}",
                    arg_tys[0]
                )));
            }
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setTitle" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::String)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setEditorCaret" => {
            expect_arity(callee, &arg_tys, 2)?;
            expect_ty(&arg_tys[0], &Type::Int)?;
            expect_ty(&arg_tys[1], &Type::Int)?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setEditorDiagnostics" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(
                &arg_tys[0],
                &Type::List(Box::new(Type::Tuple(vec![Type::Int, Type::Int]))),
            )?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setEditorTokens" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(&arg_tys[0], &Type::List(Box::new(Type::Int)))?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setEditorInlays" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(
                &arg_tys[0],
                &Type::List(Box::new(Type::Tuple(vec![
                    Type::Int,
                    Type::Int,
                    Type::String,
                ]))),
            )?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        "Ui.setEditorFolds" => {
            expect_arity(callee, &arg_tys, 1)?;
            expect_ty(
                &arg_tys[0],
                &Type::List(Box::new(Type::Tuple(vec![Type::Int, Type::Int]))),
            )?;
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        _ => {
            let f = funs
                .resolve(callee, current_module)
                .map_err(|e| TypeError::Msg(e.to_string()))?;
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
        Type::Int | Type::Float | Type::String | Type::Bool | Type::Adt(_) => Ok(()),
        Type::List(inner) => check_payload_ty(enum_name, en, case, fname, inner),
        Type::Tuple(xs) => {
            for t in xs {
                check_payload_ty(enum_name, en, case, fname, t)?;
            }
            Ok(())
        }
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
            "{enum_name}.{} field {fname}: payload types are Int, Float, String, Bool, List[T], an ADT, or the enum's type parameter(s), got {other:?}",
            case.name
        ))),
    }
}

/// Typecheck a pattern and bind payload names into `env`. Returns previous bindings to restore.
fn resolve_bare_ctor_pat(
    pat: &crate::ast::Pattern,
    scrut: &Type,
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<crate::ast::Pattern, TypeError> {
    match pat {
        crate::ast::Pattern::Bind(name) if is_ctor_ident(name) => {
            if let Some(eid) = type_adt_id(scrut) {
                if let Ok((en, id)) = lookup_enum(enums, eid, current_module) {
                    if en.cases.iter().any(|c| c.name == *name) {
                        return Ok(crate::ast::Pattern::Adt {
                            enum_name: id,
                            case_name: name.clone(),
                            binds: Vec::new(),
                            type_args: Vec::new(),
                        });
                    }
                }
            }
            Ok(pat.clone())
        }
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => {
            let (en, kept_name) = if enum_name == case_name {
                if let Some(eid) = type_adt_id(scrut) {
                    if let Ok((en, id)) = lookup_enum(enums, eid, current_module) {
                        if en.cases.iter().any(|c| c.name == *case_name) {
                            (en, id)
                        } else {
                            let (en, _) = lookup_enum(enums, enum_name, current_module)?;
                            (en, enum_name.clone())
                        }
                    } else {
                        let (en, _) = lookup_enum(enums, enum_name, current_module)?;
                        (en, enum_name.clone())
                    }
                } else {
                    let (en, _) = lookup_enum(enums, enum_name, current_module)?;
                    (en, enum_name.clone())
                }
            } else {
                let (en, _) = lookup_enum(enums, enum_name, current_module)?;
                (en, enum_name.clone())
            };
            let case = en
                .cases
                .iter()
                .find(|c| c.name == *case_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("unknown case {enum_name}.{case_name} in pattern"))
                })?;
            let field_tys = payload_field_types(en, case, scrut, enums)?;
            let mut out_binds = Vec::with_capacity(binds.len());
            for (i, b) in binds.iter().enumerate() {
                let fty = field_tys.get(i).unwrap_or(scrut);
                out_binds.push(resolve_bare_ctor_pat(b, fty, enums, current_module)?);
            }
            Ok(crate::ast::Pattern::Adt {
                enum_name: kept_name,
                case_name: case_name.clone(),
                binds: out_binds,
                type_args: type_args.clone(),
            })
        }
        crate::ast::Pattern::Or(alts) => Ok(crate::ast::Pattern::Or(
            alts.iter()
                .map(|a| resolve_bare_ctor_pat(a, scrut, enums, current_module))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        crate::ast::Pattern::As { name, inner } => Ok(crate::ast::Pattern::As {
            name: name.clone(),
            inner: Box::new(resolve_bare_ctor_pat(inner, scrut, enums, current_module)?),
        }),
        crate::ast::Pattern::Cons { head, tail, elem } => {
            let et = if let Ok(e) = list_elem(scrut) {
                e
            } else {
                return Ok(pat.clone());
            };
            Ok(crate::ast::Pattern::Cons {
                head: Box::new(resolve_bare_ctor_pat(head, &et, enums, current_module)?),
                tail: Box::new(resolve_bare_ctor_pat(
                    tail,
                    &list_of(et),
                    enums,
                    current_module,
                )?),
                elem: elem.clone(),
            })
        }
        crate::ast::Pattern::Tuple { elems, tys } => {
            let stys = match scrut {
                Type::Tuple(ts) if ts.len() == elems.len() => ts.clone(),
                _ => return Ok(pat.clone()),
            };
            let mut out = Vec::with_capacity(elems.len());
            for (e, t) in elems.iter().zip(stys.iter()) {
                out.push(resolve_bare_ctor_pat(e, t, enums, current_module)?);
            }
            Ok(crate::ast::Pattern::Tuple {
                elems: out,
                tys: tys.clone(),
            })
        }
        crate::ast::Pattern::Named { name, inner } => Ok(crate::ast::Pattern::Named {
            name: name.clone(),
            inner: Box::new(resolve_bare_ctor_pat(inner, scrut, enums, current_module)?),
        }),
        other => Ok(other.clone()),
    }
}

fn bind_pattern(
    pat: &crate::ast::Pattern,
    scrut: &Type,
    enums: &EnumIndex<'_>,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    loose: bool,
) -> Result<Vec<(String, Option<Type>)>, TypeError> {
    let pat = resolve_bare_ctor_pat(pat, scrut, enums, current_module)?;
    match &pat {
        crate::ast::Pattern::Named { .. } => Err(TypeError::Msg(
            "named field pattern is only allowed in a constructor payload".into(),
        )),
        crate::ast::Pattern::Wildcard => Ok(Vec::new()),
        crate::ast::Pattern::Int(_) => {
            if !matches!(scrut, Type::Int) {
                return Err(TypeError::Msg(format!(
                    "int literal pattern does not match scrutinee {scrut:?}"
                )));
            }
            Ok(Vec::new())
        }
        crate::ast::Pattern::Float(_) => {
            if !matches!(scrut, Type::Float) {
                return Err(TypeError::Msg(format!(
                    "float literal pattern does not match scrutinee {scrut:?}"
                )));
            }
            Ok(Vec::new())
        }
        crate::ast::Pattern::Bool(_) => {
            if !matches!(scrut, Type::Bool) {
                return Err(TypeError::Msg(format!(
                    "bool literal pattern does not match scrutinee {scrut:?}"
                )));
            }
            Ok(Vec::new())
        }
        crate::ast::Pattern::Str(_) => {
            if !matches!(scrut, Type::String) {
                return Err(TypeError::Msg(format!(
                    "string literal pattern does not match scrutinee {scrut:?}"
                )));
            }
            Ok(Vec::new())
        }
        crate::ast::Pattern::Bind(name) => {
            let old = env.insert(name.clone(), scrut.clone());
            Ok(vec![(name.clone(), old)])
        }
        crate::ast::Pattern::As { name, inner } => {
            let old = env.insert(name.clone(), scrut.clone());
            let mut restored = vec![(name.clone(), old)];
            restored.extend(bind_pattern(
                inner,
                scrut,
                enums,
                current_module,
                env,
                loose,
            )?);
            Ok(restored)
        }
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
            ..
        } => {
            let (en, id) = lookup_enum(enums, enum_name, current_module)?;
            let case = en
                .cases
                .iter()
                .find(|c| c.name == *case_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("unknown case {enum_name}.{case_name} in pattern"))
                })?;
            check_payload_fields(&id, en, case)?;
            match scrut {
                Type::Adt(n) if n == &id && en.type_params.is_empty() => {}
                Type::App(n, _) if n == &id => {}
                Type::Opaque(_) if is_meta_opaque(scrut) && en.type_params.is_empty() => {}
                Type::Opaque(_) if loose => {}
                Type::Opaque(_) if is_meta_opaque(scrut) => {
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
            let field_names: Vec<String> = case.fields.iter().map(|(n, _)| n.clone()).collect();
            let ctor = if en.name == case_name.as_str() {
                en.name.clone()
            } else {
                format!("{}.{}", en.name, case_name)
            };
            let binds = crate::ast::rewrite_named_payload(&ctor, binds.clone(), &field_names)
                .map_err(TypeError::Msg)?;
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
            let field_tys = payload_field_types(en, case, scrut, enums)?;
            let mut restored = Vec::new();
            for (nested, fty) in binds.iter().zip(field_tys.iter()) {
                restored.extend(bind_pattern(
                    nested,
                    fty,
                    enums,
                    current_module,
                    env,
                    loose,
                )?);
            }
            Ok(restored)
        }
        crate::ast::Pattern::Or(alts) => {
            let mut it = alts.iter();
            let first = it
                .next()
                .ok_or_else(|| TypeError::Msg("empty or-pattern".into()))?;
            let bound = bind_pattern(first, scrut, enums, current_module, env, loose)?;
            for alt in it {
                let extra = bind_pattern(alt, scrut, enums, current_module, env, loose)?;
                unbind_pattern(extra, env);
            }
            Ok(bound)
        }
        crate::ast::Pattern::Nil => {
            list_elem(scrut)?;
            Ok(Vec::new())
        }
        crate::ast::Pattern::Cons { head, tail, .. } => {
            let elem = if loose && matches!(scrut, Type::Opaque(_)) {
                Type::Opaque("Elem".into())
            } else {
                list_elem(scrut)?
            };
            let mut restored = bind_pattern(head, &elem, enums, current_module, env, loose)?;
            restored.extend(bind_pattern(
                tail,
                &list_of(elem),
                enums,
                current_module,
                env,
                loose,
            )?);
            Ok(restored)
        }
        crate::ast::Pattern::Tuple { elems, .. } => {
            let slots = match scrut {
                Type::Tuple(xs) => xs.clone(),
                Type::Opaque(_) => vec![scrut.clone(); elems.len()],
                other => {
                    return Err(TypeError::Msg(format!(
                        "tuple pattern does not match scrutinee {other:?}"
                    )))
                }
            };
            if slots.len() != elems.len() {
                return Err(TypeError::Msg(format!(
                    "tuple pattern has {} slots, scrutinee has {}",
                    elems.len(),
                    slots.len()
                )));
            }
            let mut restored = Vec::new();
            for (p, t) in elems.iter().zip(slots.iter()) {
                restored.extend(bind_pattern(p, t, enums, current_module, env, loose)?);
            }
            Ok(restored)
        }
    }
}

fn payload_field_types(
    en: &crate::ast::EnumDef,
    case: &crate::ast::EnumCase,
    scrut: &Type,
    enums: &EnumIndex<'_>,
) -> Result<Vec<Type>, TypeError> {
    let mut subst: HashMap<String, Type> = HashMap::new();
    let mut skipped: Vec<String> = Vec::new();
    if let Type::App(_, targs) = scrut {
        for (p, t) in en.type_params.iter().zip(targs.iter()) {
            if matches!(t, Type::Opaque(_)) {
                skipped.push(p.clone());
            } else {
                subst.insert(p.clone(), t.clone());
            }
        }
    } else if matches!(scrut, Type::Opaque(_)) {
        skipped.extend(en.type_params.iter().cloned());
    }
    let mut out = Vec::with_capacity(case.fields.len());
    for (_, fty) in &case.fields {
        let resolved = resolve_type_in(fty, enums, &en.module, &en.type_params)?;
        out.push(erase_vars(&apply_subst(&resolved, &subst), &skipped));
    }
    Ok(out)
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

fn check_match_exhaustive(
    scrut: &Type,
    arms: &[crate::ast::MatchArm],
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<(), TypeError> {
    let rewritten: Vec<crate::ast::Pattern> = arms
        .iter()
        .map(|a| {
            let p = resolve_bare_ctor_pat(&a.pattern, scrut, enums, current_module)?;
            rewrite_named_in_pattern(&p, enums, current_module)
        })
        .collect::<Result<Vec<_>, _>>()?;
    for pat in &rewritten {
        check_unique_binds(pat)?;
    }
    if arms.iter().any(|a| a.unpack) {
        return Ok(());
    }
    let covering: Vec<&crate::ast::Pattern> = arms
        .iter()
        .zip(rewritten.iter())
        .filter(|(a, _)| a.guard.is_none())
        .map(|(_, p)| p)
        .collect();
    let missing = uncovered_pats(scrut, &covering, enums, current_module)?;
    if !missing.is_empty() {
        return Err(TypeError::Msg(format!(
            "non-exhaustive match: missing {}",
            missing.join(", ")
        )));
    }
    for i in 1..arms.len() {
        let prev: Vec<&crate::ast::Pattern> = arms[..i]
            .iter()
            .zip(rewritten[..i].iter())
            .filter(|(a, _)| a.guard.is_none())
            .map(|(_, p)| p)
            .collect();
        if uncovered_pats(scrut, &prev, enums, current_module)?.is_empty() {
            return Err(TypeError::Msg(format!("unreachable match arm {}", i + 1)));
        }
    }
    Ok(())
}

fn rewrite_named_in_pattern(
    pat: &crate::ast::Pattern,
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<crate::ast::Pattern, TypeError> {
    match pat {
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => {
            let binds = binds
                .iter()
                .map(|b| rewrite_named_in_pattern(b, enums, current_module))
                .collect::<Result<Vec<_>, _>>()?;
            let (en, _) = lookup_enum(enums, enum_name, current_module)?;
            let case = en
                .cases
                .iter()
                .find(|c| c.name == *case_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("unknown case {enum_name}.{case_name} in pattern"))
                })?;
            let names: Vec<String> = case.fields.iter().map(|(n, _)| n.clone()).collect();
            let ctor = if en.name == case_name.as_str() {
                en.name.clone()
            } else {
                format!("{}.{}", en.name, case_name)
            };
            let binds =
                crate::ast::rewrite_named_payload(&ctor, binds, &names).map_err(TypeError::Msg)?;
            Ok(crate::ast::Pattern::Adt {
                enum_name: enum_name.clone(),
                case_name: case_name.clone(),
                binds,
                type_args: type_args.clone(),
            })
        }
        crate::ast::Pattern::Or(alts) => Ok(crate::ast::Pattern::Or(
            alts.iter()
                .map(|a| rewrite_named_in_pattern(a, enums, current_module))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        crate::ast::Pattern::As { name, inner } => Ok(crate::ast::Pattern::As {
            name: name.clone(),
            inner: Box::new(rewrite_named_in_pattern(inner, enums, current_module)?),
        }),
        crate::ast::Pattern::Cons { head, tail, elem } => Ok(crate::ast::Pattern::Cons {
            head: Box::new(rewrite_named_in_pattern(head, enums, current_module)?),
            tail: Box::new(rewrite_named_in_pattern(tail, enums, current_module)?),
            elem: elem.clone(),
        }),
        crate::ast::Pattern::Tuple { elems, tys } => Ok(crate::ast::Pattern::Tuple {
            elems: elems
                .iter()
                .map(|e| rewrite_named_in_pattern(e, enums, current_module))
                .collect::<Result<Vec<_>, _>>()?,
            tys: tys.clone(),
        }),
        crate::ast::Pattern::Named { name, inner } => Ok(crate::ast::Pattern::Named {
            name: name.clone(),
            inner: Box::new(rewrite_named_in_pattern(inner, enums, current_module)?),
        }),
        other => Ok(other.clone()),
    }
}

fn check_unique_binds(pat: &crate::ast::Pattern) -> Result<(), TypeError> {
    if let crate::ast::Pattern::Or(alts) = pat {
        for a in alts {
            check_unique_binds(a)?;
        }
        return Ok(());
    }
    let mut names = Vec::new();
    collect_bind_names(pat, &mut names);
    let mut seen = HashMap::new();
    for n in names {
        if n == "_" {
            continue;
        }
        if seen.insert(n.clone(), ()).is_some() {
            return Err(TypeError::Msg(format!("duplicate pattern binder {n}")));
        }
    }
    Ok(())
}

fn collect_bind_names(pat: &crate::ast::Pattern, out: &mut Vec<String>) {
    match pat {
        crate::ast::Pattern::Bind(n) => out.push(n.clone()),
        crate::ast::Pattern::As { name, inner } => {
            out.push(name.clone());
            collect_bind_names(inner, out);
        }
        crate::ast::Pattern::Adt { binds, .. } => {
            for b in binds {
                collect_bind_names(b, out);
            }
        }
        crate::ast::Pattern::Cons { head, tail, .. } => {
            collect_bind_names(head, out);
            collect_bind_names(tail, out);
        }
        crate::ast::Pattern::Tuple { elems, .. } => {
            for e in elems {
                collect_bind_names(e, out);
            }
        }
        crate::ast::Pattern::Named { inner, .. } => collect_bind_names(inner, out),
        crate::ast::Pattern::Or(alts) => {
            if let Some(first) = alts.first() {
                collect_bind_names(first, out);
            }
        }
        crate::ast::Pattern::Wildcard
        | crate::ast::Pattern::Nil
        | crate::ast::Pattern::Int(_)
        | crate::ast::Pattern::Float(_)
        | crate::ast::Pattern::Bool(_)
        | crate::ast::Pattern::Str(_) => {}
    }
}

fn uncovered_pats(
    scrut: &Type,
    pats: &[&crate::ast::Pattern],
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<Vec<String>, TypeError> {
    let rows: Vec<Vec<crate::ast::Pattern>> = pats
        .iter()
        .flat_map(|p| p.flatten_or().into_iter().map(|q| vec![q.strip_as()]))
        .collect();
    uncovered_product(std::slice::from_ref(scrut), &rows, enums, current_module)
}

fn uncovered_product(
    tys: &[Type],
    rows: &[Vec<crate::ast::Pattern>],
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Result<Vec<String>, TypeError> {
    if rows.iter().any(|r| r.iter().all(|p| p.is_irrefutable())) {
        return Ok(Vec::new());
    }
    if tys.is_empty() {
        return Ok(if rows.is_empty() {
            vec!["()".into()]
        } else {
            Vec::new()
        });
    }
    let head = &tys[0];
    let tail = &tys[1..];
    match head {
        Type::Adt(n) | Type::App(n, _) => {
            let (en, _) = match lookup_enum(enums, n, current_module) {
                Ok(pair) => pair,
                Err(_) => {
                    if rows
                        .iter()
                        .any(|r| r.first().is_some_and(|p| p.is_irrefutable()))
                    {
                        let rest: Vec<Vec<crate::ast::Pattern>> = rows
                            .iter()
                            .filter(|r| r.first().is_some_and(|p| p.is_irrefutable()))
                            .map(|r| r.iter().skip(1).cloned().collect())
                            .collect();
                        return uncovered_product(tail, &rest, enums, current_module);
                    }
                    return Ok(vec![head.to_string()]);
                }
            };
            let mut missing = Vec::new();
            for case in &en.cases {
                let mut spec: Vec<Vec<crate::ast::Pattern>> = Vec::new();
                for r in rows {
                    match r.first() {
                        Some(p) if p.is_irrefutable() => {
                            let mut rest = vec![crate::ast::Pattern::Wildcard; case.fields.len()];
                            rest.extend(r.iter().skip(1).cloned());
                            spec.push(rest);
                        }
                        Some(crate::ast::Pattern::Adt {
                            case_name, binds, ..
                        }) if case_name == &case.name => {
                            let mut rest = binds.clone();
                            while rest.len() < case.fields.len() {
                                rest.push(crate::ast::Pattern::Wildcard);
                            }
                            rest.extend(r.iter().skip(1).cloned());
                            spec.push(rest);
                        }
                        _ => {}
                    }
                }
                if spec.is_empty() {
                    missing.push(format!("{}.{}", en.name, case.name));
                    continue;
                }
                let mut nested = payload_field_types(en, case, head, enums)?;
                nested.extend(tail.iter().cloned());
                for m in uncovered_product(&nested, &spec, enums, current_module)? {
                    if case.fields.is_empty() && tail.is_empty() {
                        missing.push(format!("{}.{}", en.name, case.name));
                    } else {
                        missing.push(format!("{}.{}({})", en.name, case.name, m));
                    }
                }
            }
            Ok(missing)
        }
        Type::Bool => {
            let mut missing = Vec::new();
            for (lit, name) in [(true, "true"), (false, "false")] {
                let mut spec: Vec<Vec<crate::ast::Pattern>> = Vec::new();
                for r in rows {
                    match r.first() {
                        Some(p) if p.is_irrefutable() => {
                            spec.push(r.iter().skip(1).cloned().collect());
                        }
                        Some(crate::ast::Pattern::Bool(b)) if *b == lit => {
                            spec.push(r.iter().skip(1).cloned().collect());
                        }
                        _ => {}
                    }
                }
                if spec.is_empty() {
                    missing.push(name.into());
                    continue;
                }
                for m in uncovered_product(tail, &spec, enums, current_module)? {
                    if tail.is_empty() {
                        missing.push(name.into());
                    } else {
                        missing.push(format!("{name}, {m}"));
                    }
                }
            }
            Ok(missing)
        }
        Type::List(elem) => {
            let mut missing = Vec::new();
            let mut nil_spec: Vec<Vec<crate::ast::Pattern>> = Vec::new();
            let mut cons_spec: Vec<Vec<crate::ast::Pattern>> = Vec::new();
            for r in rows {
                match r.first() {
                    Some(p) if p.is_irrefutable() => {
                        nil_spec.push(r.iter().skip(1).cloned().collect());
                        let mut rest =
                            vec![crate::ast::Pattern::Wildcard, crate::ast::Pattern::Wildcard];
                        rest.extend(r.iter().skip(1).cloned());
                        cons_spec.push(rest);
                    }
                    Some(crate::ast::Pattern::Nil) => {
                        nil_spec.push(r.iter().skip(1).cloned().collect());
                    }
                    Some(crate::ast::Pattern::Cons { head, tail: tp, .. }) => {
                        let mut rest = vec![(**head).clone(), (**tp).clone()];
                        rest.extend(r.iter().skip(1).cloned());
                        cons_spec.push(rest);
                    }
                    _ => {}
                }
            }
            if nil_spec.is_empty() {
                missing.push("[]".into());
            } else {
                for m in uncovered_product(tail, &nil_spec, enums, current_module)? {
                    if tail.is_empty() {
                        missing.push("[]".into());
                    } else {
                        missing.push(format!("[], {m}"));
                    }
                }
            }
            if cons_spec.is_empty() {
                missing.push("_ :: _".into());
            } else {
                let mut nested = vec![(**elem).clone(), Type::List(elem.clone())];
                nested.extend(tail.iter().cloned());
                if !uncovered_product(&nested, &cons_spec, enums, current_module)?.is_empty() {
                    missing.push("_ :: _".into());
                }
            }
            Ok(missing)
        }
        Type::Tuple(slots) => {
            let mut spec: Vec<Vec<crate::ast::Pattern>> = Vec::new();
            for r in rows {
                match r.first() {
                    Some(p) if p.is_irrefutable() => {
                        let mut rest = vec![crate::ast::Pattern::Wildcard; slots.len()];
                        rest.extend(r.iter().skip(1).cloned());
                        spec.push(rest);
                    }
                    Some(crate::ast::Pattern::Tuple { elems, .. })
                        if elems.len() == slots.len() =>
                    {
                        let mut rest = elems.clone();
                        rest.extend(r.iter().skip(1).cloned());
                        spec.push(rest);
                    }
                    _ => {}
                }
            }
            if spec.is_empty() {
                return Ok(vec![head.to_string()]);
            }
            let mut nested = slots.clone();
            nested.extend(tail.iter().cloned());
            let missing = uncovered_product(&nested, &spec, enums, current_module)?;
            if missing.is_empty() {
                Ok(Vec::new())
            } else {
                Ok(missing.into_iter().map(|m| format!("({m})")).collect())
            }
        }
        Type::Int | Type::Float | Type::String => {
            if rows
                .iter()
                .any(|r| r.first().is_some_and(|p| p.is_irrefutable()))
            {
                let rest: Vec<Vec<crate::ast::Pattern>> = rows
                    .iter()
                    .filter(|r| r.first().is_some_and(|p| p.is_irrefutable()))
                    .map(|r| r.iter().skip(1).cloned().collect())
                    .collect();
                uncovered_product(tail, &rest, enums, current_module)
            } else if rows.is_empty() {
                Ok(vec![head.to_string()])
            } else {
                let rest_missing = if tail.is_empty() {
                    Vec::new()
                } else {
                    let rest: Vec<Vec<crate::ast::Pattern>> = rows
                        .iter()
                        .map(|r| r.iter().skip(1).cloned().collect())
                        .collect();
                    uncovered_product(tail, &rest, enums, current_module)?
                };
                if rest_missing.is_empty() {
                    Ok(vec!["_".into()])
                } else {
                    Ok(rest_missing)
                }
            }
        }
        _ => {
            if rows
                .iter()
                .any(|r| r.first().is_some_and(|p| p.is_irrefutable()))
            {
                let rest: Vec<Vec<crate::ast::Pattern>> = rows
                    .iter()
                    .filter(|r| r.first().is_some_and(|p| p.is_irrefutable()))
                    .map(|r| r.iter().skip(1).cloned().collect())
                    .collect();
                uncovered_product(tail, &rest, enums, current_module)
            } else if rows.is_empty() {
                Ok(vec![head.to_string()])
            } else {
                Ok(Vec::new())
            }
        }
    }
}

fn elaborate_pattern(
    pat: &crate::ast::Pattern,
    scrut: &Type,
    enums: &EnumIndex<'_>,
    current_module: &str,
    tparams: &[String],
    span: &Span,
    loose: bool,
) -> Result<crate::ast::Pattern, TypeError> {
    let pat = resolve_bare_ctor_pat(pat, scrut, enums, current_module)?;
    match &pat {
        crate::ast::Pattern::Wildcard
        | crate::ast::Pattern::Bind(_)
        | crate::ast::Pattern::Int(_)
        | crate::ast::Pattern::Float(_)
        | crate::ast::Pattern::Bool(_)
        | crate::ast::Pattern::Str(_) => Ok(pat.clone()),
        crate::ast::Pattern::As { name, inner } => Ok(crate::ast::Pattern::As {
            name: name.clone(),
            inner: Box::new(elaborate_pattern(
                inner,
                scrut,
                enums,
                current_module,
                tparams,
                span,
                loose,
            )?),
        }),
        crate::ast::Pattern::Or(alts) => {
            let mut out = Vec::with_capacity(alts.len());
            for a in alts {
                out.push(elaborate_pattern(
                    a,
                    scrut,
                    enums,
                    current_module,
                    tparams,
                    span,
                    loose,
                )?);
            }
            Ok(crate::ast::Pattern::Or(out))
        }
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
                match scrut {
                    Type::App(eid, eargs) if eid == &id => eargs.clone(),
                    Type::Opaque(_) if loose => Vec::new(),
                    other => {
                        return Err(TypeError::Msg(format!(
                            "pattern {enum_name}.{case_name} does not match scrutinee {other:?}"
                        )))
                    }
                }
            };
            check_targs(enum_name, case_name, &targs, tparams, span)?;
            let case = en
                .cases
                .iter()
                .find(|c| c.name == *case_name)
                .ok_or_else(|| {
                    TypeError::Msg(format!("unknown case {enum_name}.{case_name} in pattern"))
                })?;
            let field_names: Vec<String> = case.fields.iter().map(|(n, _)| n.clone()).collect();
            let ctor = if en.name == case_name.as_str() {
                en.name.clone()
            } else {
                format!("{}.{}", en.name, case_name)
            };
            let binds = crate::ast::rewrite_named_payload(&ctor, binds.clone(), &field_names)
                .map_err(TypeError::Msg)?;
            let field_tys = payload_field_types(en, case, scrut, enums)?;
            let mut out_binds = Vec::with_capacity(binds.len());
            for (nested, fty) in binds.iter().zip(field_tys.iter()) {
                out_binds.push(elaborate_pattern(
                    nested,
                    fty,
                    enums,
                    current_module,
                    tparams,
                    span,
                    loose,
                )?);
            }
            Ok(crate::ast::Pattern::Adt {
                enum_name: enum_name.clone(),
                case_name: case_name.clone(),
                binds: out_binds,
                type_args: targs,
            })
        }
        crate::ast::Pattern::Nil => {
            list_elem(scrut)?;
            Ok(crate::ast::Pattern::Nil)
        }
        crate::ast::Pattern::Cons { head, tail, .. } => {
            let elem = if loose && matches!(scrut, Type::Opaque(_)) {
                Type::Opaque("Elem".into())
            } else {
                list_elem(scrut)?
            };
            Ok(crate::ast::Pattern::Cons {
                head: Box::new(elaborate_pattern(
                    head,
                    &elem,
                    enums,
                    current_module,
                    tparams,
                    span,
                    loose,
                )?),
                tail: Box::new(elaborate_pattern(
                    tail,
                    &list_of(elem.clone()),
                    enums,
                    current_module,
                    tparams,
                    span,
                    loose,
                )?),
                elem,
            })
        }
        crate::ast::Pattern::Tuple { elems, .. } => {
            let slots = match scrut {
                Type::Tuple(xs) => xs.clone(),
                Type::Opaque(_) => vec![scrut.clone(); elems.len()],
                _ => {
                    return Err(TypeError::Msg(format!(
                        "tuple pattern does not match scrutinee {scrut:?}"
                    ))
                    .with_span_if_bare(span));
                }
            };
            if slots.len() != elems.len() {
                return Err(TypeError::Msg(format!(
                    "tuple pattern has {} slots, scrutinee has {}",
                    elems.len(),
                    slots.len()
                ))
                .with_span_if_bare(span));
            }
            let mut out_elems = Vec::new();
            for (p, t) in elems.iter().zip(slots.iter()) {
                out_elems.push(elaborate_pattern(
                    p,
                    t,
                    enums,
                    current_module,
                    tparams,
                    span,
                    loose,
                )?);
            }
            Ok(crate::ast::Pattern::Tuple {
                elems: out_elems,
                tys: slots,
            })
        }
        crate::ast::Pattern::Named { name, inner: _ } => Err(TypeError::Msg(format!(
            "named field pattern `{name}` is only allowed in a constructor payload"
        ))
        .with_span_if_bare(span)),
    }
}

fn types_compat(a: &Type, b: &Type) -> bool {
    match (a, b) {
        (Type::Unit, Type::Unit) => true,
        (Type::Int, Type::Int) => true,
        (Type::Float, Type::Float) => true,
        (Type::Bool, Type::Bool) => true,
        (Type::String, Type::String) => true,
        (Type::List(x), Type::List(y)) => types_compat(x, y),
        (Type::Tuple(a), Type::Tuple(b)) => {
            a.len() == b.len() && a.iter().zip(b.iter()).all(|(u, v)| types_compat(u, v))
        }
        (Type::Fun(a0, a1), Type::Fun(b0, b1)) => types_compat(a0, b0) && types_compat(a1, b1),
        (Type::Io(x), Type::Io(y)) => types_compat(x, y),
        (Type::Adt(x), Type::Adt(y)) => x == y,
        (Type::App(x, a), Type::App(y, b)) => {
            x == y && a.len() == b.len() && a.iter().zip(b.iter()).all(|(u, v)| types_compat(u, v))
        }
        (Type::Var(x), Type::Var(y)) => x == y,
        (Type::Opaque(x), Type::Opaque(y)) if x == y => true,
        (Type::Opaque(_), _) if is_meta_opaque(a) => true,
        (_, Type::Opaque(_)) if is_meta_opaque(b) => true,
        _ => false,
    }
}

fn is_meta_opaque(t: &Type) -> bool {
    match t {
        Type::Opaque(n) => {
            matches!(
                n.as_str(),
                "Elem" | "Param" | "Any" | "Rewrite" | "TapFn" | "Fail"
            ) || n.starts_with("__unbound_")
        }
        _ => false,
    }
}

fn fail_ty() -> Type {
    Type::Io(Box::new(Type::Opaque("Fail".into())))
}

/// Missing `else` is `()` or `IO.pure(())`.
fn implicit_else_ty(then_ty: &Type) -> Result<Type, TypeError> {
    match then_ty {
        Type::Unit => Ok(Type::Unit),
        Type::Io(inner) if matches!(inner.as_ref(), Type::Unit) || is_meta_opaque(inner) => {
            Ok(Type::Io(Box::new(Type::Unit)))
        }
        other => Err(TypeError::Msg(format!(
            "if without else needs Unit or IO[Unit], got {other:?}"
        ))),
    }
}

fn prefer_join(a: &Type, b: &Type) -> Result<Type, TypeError> {
    if types_compat(a, b) {
        Ok(prefer_concrete(a, b))
    } else {
        Err(TypeError::Msg(format!("type mismatch: {a:?} vs {b:?}")))
    }
}

fn prefer_concrete(a: &Type, b: &Type) -> Type {
    match (a, b) {
        (Type::Io(x), Type::Io(y)) => Type::Io(Box::new(prefer_concrete(x, y))),
        (Type::List(x), Type::List(y)) => Type::List(Box::new(prefer_concrete(x, y))),
        (Type::Tuple(xs), Type::Tuple(ys)) if xs.len() == ys.len() => Type::Tuple(
            xs.iter()
                .zip(ys.iter())
                .map(|(u, v)| prefer_concrete(u, v))
                .collect(),
        ),
        (Type::Fun(a0, a1), Type::Fun(b0, b1)) => Type::Fun(
            Box::new(prefer_concrete(a0, b0)),
            Box::new(prefer_concrete(a1, b1)),
        ),
        (Type::App(n, xs), Type::App(m, ys)) if n == m && xs.len() == ys.len() => Type::App(
            n.clone(),
            xs.iter()
                .zip(ys.iter())
                .map(|(u, v)| prefer_concrete(u, v))
                .collect(),
        ),
        (x, y) if is_meta_opaque(x) => y.clone(),
        (x, _) => x.clone(),
    }
}

fn is_handle_ctor(name: &str) -> bool {
    matches!(
        name,
        "Fiber" | "Ref" | "Queue" | "Deferred" | "Resource" | "Stream" | "Map" | "Set"
    )
}

fn handle_ty(name: &str, payload: Type) -> Type {
    Type::App(name.to_string(), vec![payload])
}

fn expect_handle<'a>(got: &'a Type, name: &str) -> Result<&'a [Type], TypeError> {
    match got {
        Type::App(n, args) if n == name => Ok(args),
        other => Err(TypeError::Msg(format!("expected {name}[_], got {other:?}"))),
    }
}

fn handle_payload_ty(got: &Type, name: &str) -> Result<Type, TypeError> {
    Ok(expect_handle(got, name)?
        .first()
        .cloned()
        .unwrap_or_else(|| Type::Opaque("Elem".into())))
}

/// `Queue.unbounded` / `Deferred.empty` have no witness. A missing pin is String.
fn default_cell_payload(payload: Type) -> Type {
    if is_meta_opaque(&payload) {
        Type::String
    } else {
        payload
    }
}

fn list_elem(t: &Type) -> Result<Type, TypeError> {
    match t {
        Type::List(e) => Ok((**e).clone()),
        Type::Opaque(_) if is_meta_opaque(t) => Ok(Type::Opaque("Elem".into())),
        other => Err(TypeError::Msg(format!("expected List[_], got {other:?}"))),
    }
}

fn expect_ordered_elem(callee: &str, elem: &Type) -> Result<(), TypeError> {
    if matches!(elem, Type::Int | Type::String) || is_meta_opaque(elem) {
        Ok(())
    } else {
        Err(TypeError::Msg(format!(
            "{callee} needs List[Int] or List[String], got List[{elem}]"
        )))
    }
}

fn expect_int_elem(callee: &str, elem: &Type) -> Result<(), TypeError> {
    if matches!(elem, Type::Int) || is_meta_opaque(elem) {
        Ok(())
    } else {
        Err(TypeError::Msg(format!(
            "{callee} needs List[Int], got List[{elem}]"
        )))
    }
}

fn expect_map_key(callee: &str, key: &Type) -> Result<(), TypeError> {
    if matches!(key, Type::Int | Type::String) || is_meta_opaque(key) {
        Ok(())
    } else {
        Err(TypeError::Msg(format!(
            "{callee} lambda must return Int or String, got {key}"
        )))
    }
}

fn prefer_elem(a: &Type, b: &Type) -> Result<Type, TypeError> {
    prefer_named(a, b, "list element")
}

fn prefer_named(a: &Type, b: &Type, what: &str) -> Result<Type, TypeError> {
    if types_compat(a, b) {
        if is_meta_opaque(a) {
            return Ok(b.clone());
        }
        return Ok(a.clone());
    }
    Err(TypeError::Msg(format!(
        "{what} type mismatch: {a:?} vs {b:?}"
    )))
}

fn list_of(t: Type) -> Type {
    Type::List(Box::new(t))
}

fn map_kv(t: &Type) -> Result<(Type, Type), TypeError> {
    match t {
        Type::App(n, args) if n == "Map" && args.len() == 2 => {
            Ok((args[0].clone(), args[1].clone()))
        }
        other if is_meta_opaque(other) => {
            Ok((Type::Opaque("Elem".into()), Type::Opaque("Elem".into())))
        }
        other => Err(TypeError::Msg(format!("expected Map[_, _], got {other:?}"))),
    }
}

fn set_elem(t: &Type) -> Result<Type, TypeError> {
    match t {
        Type::App(n, args) if n == "Set" && args.len() == 1 => Ok(args[0].clone()),
        other if is_meta_opaque(other) => Ok(Type::Opaque("Elem".into())),
        other => Err(TypeError::Msg(format!("expected Set[_], got {other:?}"))),
    }
}

fn unify_types(
    pattern: &Type,
    concrete: &Type,
    subst: &mut HashMap<String, Type>,
) -> Result<(), TypeError> {
    match (pattern, concrete) {
        (Type::Opaque(_), _) if is_meta_opaque(pattern) => Ok(()),
        (_, Type::Opaque(_)) if is_meta_opaque(concrete) => Ok(()),
        (Type::Opaque(x), Type::Opaque(y)) if x == y => Ok(()),
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
        (Type::List(a), Type::List(b)) => unify_types(a, b, subst),
        (Type::Tuple(a), Type::Tuple(b)) if a.len() == b.len() => {
            for (u, v) in a.iter().zip(b.iter()) {
                unify_types(u, v, subst)?;
            }
            Ok(())
        }
        (Type::Fun(a0, a1), Type::Fun(b0, b1)) => {
            unify_types(a0, b0, subst)?;
            unify_types(a1, b1, subst)
        }
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

/// Like `unify_types`, but for generic enum construction. Def-scope type
/// parameters (`Var`) may bind. Concretization happens at monomorphization.
fn unify_construct(
    pattern: &Type,
    concrete: &Type,
    subst: &mut HashMap<String, Type>,
) -> Result<(), TypeError> {
    match (pattern, concrete) {
        (Type::Opaque(_), _) if is_meta_opaque(pattern) => Ok(()),
        (_, Type::Opaque(_)) if is_meta_opaque(concrete) => Ok(()),
        (Type::Opaque(x), Type::Opaque(y)) if x == y => Ok(()),
        (Type::Var(n), t) => {
            if let Some(prev) = subst.get(n) {
                if !types_compat(prev, t) {
                    return Err(TypeError::Msg(format!(
                        "type parameter {n} constrained to both {prev:?} and {t:?}"
                    )));
                }
            } else if !contains_unbound(t) {
                // Placeholder-laden types carry no information. The expected
                // type at the construction site fills the parameter instead.
                subst.insert(n.clone(), t.clone());
            }
            Ok(())
        }
        (Type::Io(a), Type::Io(b)) => unify_construct(a, b, subst),
        (Type::List(a), Type::List(b)) => unify_construct(a, b, subst),
        (Type::Tuple(a), Type::Tuple(b)) if a.len() == b.len() => {
            for (u, v) in a.iter().zip(b.iter()) {
                unify_construct(u, v, subst)?;
            }
            Ok(())
        }
        (Type::Fun(a0, a1), Type::Fun(b0, b1)) => {
            unify_construct(a0, b0, subst)?;
            unify_construct(a1, b1, subst)
        }
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
        Type::Unit
        | Type::Int
        | Type::Float
        | Type::String
        | Type::Bool
        | Type::List(_)
        | Type::Adt(_) => true,
        Type::Tuple(xs) => xs.iter().all(mono_type_ok),
        Type::App(_, args) => args.iter().all(mono_type_ok),
        Type::Io(inner) => mono_type_ok(inner),
        _ => false,
    }
}

fn apply_subst(ty: &Type, subst: &HashMap<String, Type>) -> Type {
    match ty {
        Type::Var(n) => subst.get(n).cloned().unwrap_or_else(|| ty.clone()),
        Type::Io(inner) => Type::Io(Box::new(apply_subst(inner, subst))),
        Type::List(inner) => Type::List(Box::new(apply_subst(inner, subst))),
        Type::Tuple(xs) => Type::Tuple(xs.iter().map(|t| apply_subst(t, subst)).collect()),
        Type::Fun(a, b) => Type::Fun(
            Box::new(apply_subst(a, subst)),
            Box::new(apply_subst(b, subst)),
        ),
        Type::App(n, args) => Type::App(
            n.clone(),
            args.iter().map(|a| apply_subst(a, subst)).collect(),
        ),
        other => other.clone(),
    }
}

/// Replace leftover `Var`s (enum type parameters that stayed unbound, for
/// example behind an `Opaque` scrutinee) with opaque types.
fn erase_vars(ty: &Type, names: &[String]) -> Type {
    match ty {
        Type::Var(n) if names.iter().any(|p| p == n) => Type::Opaque(n.clone()),
        Type::Io(inner) => Type::Io(Box::new(erase_vars(inner, names))),
        Type::List(inner) => Type::List(Box::new(erase_vars(inner, names))),
        Type::Tuple(xs) => Type::Tuple(xs.iter().map(|t| erase_vars(t, names)).collect()),
        Type::Fun(a, b) => Type::Fun(
            Box::new(erase_vars(a, names)),
            Box::new(erase_vars(b, names)),
        ),
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
        Type::Float => "Float".into(),
        Type::String => "String".into(),
        Type::Bool => "Bool".into(),
        Type::List(inner) => format!("List_{}", type_mangle(inner)),
        Type::Tuple(xs) => format!(
            "Tup_{}",
            xs.iter().map(type_mangle).collect::<Vec<_>>().join("_")
        ),
        Type::Fun(a, b) => format!("Fun_{}_{}", type_mangle(a), type_mangle(b)),
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
    inject_builtin_enums(&mut program.enums);
    let mut specialized: HashMap<String, FunDef> = HashMap::new();
    with_pass_indexes(&mut program, |program, enums, methods, funs| {
        for d in &mut program.defs {
            let mut env: HashMap<String, Type> = HashMap::new();
            for p in &d.params {
                env.insert(
                    p.name.clone(),
                    resolve_type_in(&p.ty, enums, &d.module, &d.type_params)?,
                );
            }
            d.body = mono_expr(
                std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
                enums,
                funs,
                methods,
                &d.module,
                &mut env,
                &mut specialized,
            )?;
        }
        let mut env: HashMap<String, Type> = HashMap::new();
        let main_mod = program.main.module.clone();
        program.main.body = mono_expr(
            std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
            enums,
            funs,
            methods,
            &main_mod,
            &mut env,
            &mut specialized,
        )?;
        Ok(())
    })?;

    // Drop generic templates; keep non-generic defs + specialized clones.
    program.defs.retain(|d| d.type_params.is_empty());
    for (name, def) in specialized {
        if program
            .defs
            .iter()
            .any(|d| d.module == def.module && d.name == name)
        {
            continue;
        }
        program.defs.push(def);
    }
    specialize_enums(program)
}

// Arity comes from the shared typecheck context (enums, funs, methods, module, env).
#[allow(clippy::too_many_arguments)]
fn mono_lambda_arg(
    expr: Expr,
    param_ty: Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    specialized: &mut HashMap<String, FunDef>,
) -> Result<Expr, TypeError> {
    let expr = as_lambda_arg(expr, env, funs, current_module, Some(&param_ty))?;
    if let ExprKind::Lambda {
        param,
        param_ty: ann,
        pat,
        body,
    } = expr.kind
    {
        let span = expr.span;
        let bind_ty = if let Some(ref a) = ann {
            resolve_ascribe_type(a, enums, current_module)?
        } else {
            param_ty
        };
        let old = bind_opt(param.as_ref(), bind_ty, env);
        let body = mono_expr(
            *body,
            enums,
            funs,
            methods,
            current_module,
            env,
            specialized,
        )?;
        restore_opt(old, env);
        Ok(Expr::new(
            ExprKind::Lambda {
                param,
                param_ty: ann,
                pat,
                body: Box::new(body),
            },
            span,
        ))
    } else {
        mono_expr(expr, enums, funs, methods, current_module, env, specialized)
    }
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
            // Infer arg types BEFORE rewriting. Processed args may reference
            // monomorphized callees the pre-mono index cannot resolve.
            let orig_arg_tys: Option<Vec<Type>> = match funs.resolve(&callee, current_module) {
                Ok(f) if !f.type_params.is_empty() => Some(
                    args.iter()
                        .map(|a| infer(a, enums, funs, methods, current_module, env))
                        .collect::<Result<Vec<_>, _>>()?,
                ),
                _ => None,
            };
            let nargs = args.len();
            let mut prior: Vec<Type> = Vec::new();
            let mut out = Vec::with_capacity(nargs);
            for (i, a) in args.into_iter().enumerate() {
                let rewritten = if let Some(pty) = kit_lambda_param_ty_at(&callee, i, nargs, &prior)
                {
                    mono_lambda_arg(
                        a,
                        pty,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        specialized,
                    )?
                } else if let Some((pty, _)) =
                    user_fun_lambda_expected(&callee, i, enums, funs, current_module)
                {
                    mono_lambda_arg(
                        a,
                        pty,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        specialized,
                    )?
                } else {
                    mono_expr(a, enums, funs, methods, current_module, env, specialized)?
                };
                let ty = infer(&rewritten, enums, funs, methods, current_module, env)
                    .unwrap_or_else(|_| Type::Opaque("Rewrite".into()));
                prior.push(ty);
                out.push(rewritten);
            }
            let args = out;
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
                                    rfn: p.rfn.clone(),
                                    default: p
                                        .default
                                        .as_ref()
                                        .map(|e| subst_node_targs(e.clone(), &subst)),
                                    span: p.span.clone(),
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
                                name_span: f.name_span.clone(),
                                is_private: f.is_private,
                                is_driver: f.is_driver,
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
            let inner = mono_expr(
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            let old = if let (Type::Io(inner_t), Some(ref p)) = (&it, &param) {
                env.insert(p.clone(), (**inner_t).clone())
            } else {
                None
            };
            let body = mono_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
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
        ExprKind::IoMap { inner, param, body } => {
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let inner = mono_expr(
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            let old = if let (Type::Io(inner_t), Some(ref p)) = (&it, &param) {
                env.insert(p.clone(), (**inner_t).clone())
            } else {
                None
            };
            let body = mono_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            if let Some(p) = &param {
                if let Some(v) = old {
                    env.insert(p.clone(), v);
                } else {
                    env.remove(p);
                }
            }
            Ok(Expr::new(
                ExprKind::IoMap {
                    inner: Box::new(inner),
                    param,
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::HandleErrorWith { inner, param, body } => Ok(Expr::new(
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
                param: param.clone(),
                body: {
                    let old = bind_opt(param.as_ref(), Type::String, env);
                    let body = mono_expr(
                        *body,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        specialized,
                    )?;
                    restore_opt(old, env);
                    Box::new(body)
                },
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
        ExprKind::Tuple { elems } => Ok(Expr::new(
            ExprKind::Tuple {
                elems: elems
                    .into_iter()
                    .map(|e| mono_expr(e, enums, funs, methods, current_module, env, specialized))
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::IoEnsure { inner, finalizer } => Ok(Expr::new(
            ExprKind::IoEnsure {
                inner: Box::new(mono_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                finalizer: Box::new(mono_expr(
                    *finalizer,
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
        ExprKind::IoTimeout { ms, inner } => Ok(Expr::new(
            ExprKind::IoTimeout {
                ms: Box::new(mono_expr(
                    *ms,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
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
        ExprKind::Let { name, value, body } => {
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let value = mono_expr(
                *value,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            let old = env.insert(name.clone(), vt);
            let body = mono_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
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
            let scrutinee = mono_expr(
                *scrutinee,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound =
                    bind_pattern(&arm.pattern, &st, enums, current_module, env, arm.unpack)?;
                let guard = match arm.guard {
                    Some(g) => Some(mono_expr(
                        g,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        specialized,
                    )?),
                    None => None,
                };
                let body = mono_expr(
                    arm.body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm {
                    pattern: arm.pattern,
                    guard,
                    body,
                    unpack: arm.unpack,
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
            implicit_else,
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
                implicit_else,
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
        ExprKind::Unary { op, expr } => Ok(Expr::new(
            ExprKind::Unary {
                op,
                expr: Box::new(mono_expr(
                    *expr,
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
        ExprKind::NamedArg { name, value } => Ok(Expr::new(
            ExprKind::NamedArg {
                name,
                value: Box::new(mono_expr(
                    *value,
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
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => {
            let bind_ty = if let Some(ann) = &param_ty {
                resolve_ascribe_type(ann, enums, current_module)?
            } else {
                Type::Opaque("Param".into())
            };
            let old = bind_opt(param.as_ref(), bind_ty, env);
            let body = mono_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                specialized,
            )?;
            restore_opt(old, env);
            Ok(Expr::new(
                ExprKind::Lambda {
                    param,
                    param_ty,
                    pat,
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::Apply { fun, arg } => Ok(Expr::new(
            ExprKind::Apply {
                fun: Box::new(mono_expr(
                    *fun,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    specialized,
                )?),
                arg: Box::new(mono_expr(
                    *arg,
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
/// (`type_args`). Runs after typecheck in both `check` and `build`. Errors when
/// constructor args and the expected type at the construction site do not
/// determine an instantiation.
pub fn elaborate_generics(mut program: Program) -> Result<Program, TypeError> {
    inject_builtin_enums(&mut program.enums);
    with_pass_indexes(&mut program, |program, enums, methods, funs| {
        for d in &mut program.defs {
            let mut env: HashMap<String, Type> = HashMap::new();
            for p in &d.params {
                env.insert(
                    p.name.clone(),
                    resolve_type_in(&p.ty, enums, &d.module, &d.type_params)?,
                );
            }
            let expected = resolve_type_in(&d.ret, enums, &d.module, &d.type_params)?;
            d.body = elaborate_expr(
                std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
                enums,
                funs,
                methods,
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
            enums,
            funs,
            methods,
            &main_mod,
            &mut env,
            Some(&Type::Io(Box::new(Type::Unit))),
            &[],
        )?;
        Ok(())
    })?;
    Ok(program)
}

/// An expected type can fill a construction's instantiation when
/// it has no opaque holes and its type variables are in scope.
fn usable_expected(ty: &Type, tparams: &[String]) -> bool {
    match ty {
        Type::Opaque(_) => false,
        Type::Var(n) => tparams.iter().any(|p| p == n),
        Type::Io(inner) => usable_expected(inner, tparams),
        Type::List(inner) => usable_expected(inner, tparams),
        Type::Fun(a, b) => usable_expected(a, tparams) && usable_expected(b, tparams),
        Type::Tuple(xs) => xs.iter().all(|t| usable_expected(t, tparams)),
        Type::App(_, args) => args.iter().all(|a| usable_expected(a, tparams)),
        _ => true,
    }
}

/// True when `t` still holds an undetermined instantiation placeholder.
fn contains_unbound(t: &Type) -> bool {
    match t {
        Type::Opaque(n) => n.starts_with("__unbound_"),
        Type::Io(inner) | Type::List(inner) => contains_unbound(inner),
        Type::Fun(a, b) => contains_unbound(a) || contains_unbound(b),
        Type::Tuple(xs) => xs.iter().any(contains_unbound),
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
        Type::List(inner) | Type::Io(inner) => {
            check_targ(enum_name, case_name, inner, tparams, span)
        }
        Type::Fun(a, b) => {
            check_targ(enum_name, case_name, a, tparams, span)?;
            check_targ(enum_name, case_name, b, tparams, span)
        }
        Type::Tuple(xs) => {
            for t in xs {
                check_targ(enum_name, case_name, t, tparams, span)?;
            }
            Ok(())
        }
        _ => Ok(()),
    }
}

fn elaborate_lambda_arg(
    expr: Expr,
    param_ty: Type,
    enums: &EnumIndex<'_>,
    funs: &FunIndex<'_>,
    methods: &MethodIndex,
    current_module: &str,
    env: &mut HashMap<String, Type>,
    tparams: &[String],
) -> Result<Expr, TypeError> {
    let expr = as_lambda_arg(expr, env, funs, current_module, Some(&param_ty))?;
    if let ExprKind::Lambda {
        param,
        param_ty: ann,
        pat,
        body,
    } = expr.kind
    {
        let span = expr.span;
        let bind_ty = if let Some(ref a) = ann {
            resolve_ascribe_type(a, enums, current_module)?
        } else {
            param_ty
        };
        let old = bind_opt(param.as_ref(), bind_ty, env);
        let body = elaborate_expr(
            *body,
            enums,
            funs,
            methods,
            current_module,
            env,
            None,
            tparams,
        )?;
        restore_opt(old, env);
        Ok(Expr::new(
            ExprKind::Lambda {
                param,
                param_ty: ann,
                pat,
                body: Box::new(body),
            },
            span,
        ))
    } else {
        elaborate_expr(
            expr,
            enums,
            funs,
            methods,
            current_module,
            env,
            None,
            tparams,
        )
    }
}

// Arity comes from the shared typecheck context (enums, funs, methods, module, env).
#[allow(clippy::too_many_arguments)]
fn rewrite_bare_ctor_expr(
    expr: Expr,
    expected: Option<&Type>,
    enums: &EnumIndex<'_>,
    env: &HashMap<String, Type>,
    funs: &FunIndex<'_>,
    current_module: &str,
) -> Expr {
    let Some(want) = expected else {
        return expr;
    };
    if !is_bare_ctor_expr(&expr, env, funs, current_module) {
        return expr;
    }
    let Some(eid) = type_adt_id(want) else {
        return expr;
    };
    let Ok((en, id)) = lookup_enum(enums, eid, current_module) else {
        return expr;
    };
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::Var(name)
            if en
                .cases
                .iter()
                .any(|c| c.name == name && c.fields.is_empty()) =>
        {
            Expr::new(
                ExprKind::AdtConstruct {
                    enum_name: id,
                    case_name: name,
                    args: Vec::new(),
                    type_args: Vec::new(),
                },
                span,
            )
        }
        ExprKind::Call { callee, args }
            if en
                .cases
                .iter()
                .any(|c| c.name == callee && c.fields.len() == args.len()) =>
        {
            Expr::new(
                ExprKind::AdtConstruct {
                    enum_name: id,
                    case_name: callee,
                    args,
                    type_args: Vec::new(),
                },
                span,
            )
        }
        kind => Expr::new(kind, span),
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
    let expr = if let Some(Type::Fun(a, _)) = expected {
        as_lambda_arg(expr, env, funs, current_module, Some(a.as_ref()))?
    } else {
        expr
    };
    let expr = rewrite_bare_ctor_expr(expr, expected, enums, env, funs, current_module);
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
                .ok_or_else(|| TypeError::Msg(format!("unknown case {enum_name}.{case_name}")))?;
            if en.type_params.is_empty() {
                let args = args
                    .into_iter()
                    .map(|a| {
                        elaborate_expr(a, enums, funs, methods, current_module, env, None, tparams)
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
                *scrutinee,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?;
            let st = infer(&scrutinee, enums, funs, methods, current_module, env)?;
            let mut out_arms = Vec::new();
            for arm in arms {
                let bound =
                    bind_pattern(&arm.pattern, &st, enums, current_module, env, arm.unpack)?;
                let pattern = elaborate_pattern(
                    &arm.pattern,
                    &st,
                    enums,
                    current_module,
                    tparams,
                    &span,
                    arm.unpack,
                )?;
                let guard = match arm.guard {
                    Some(g) => Some(elaborate_expr(
                        g,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        None,
                        tparams,
                    )?),
                    None => None,
                };
                let body = elaborate_expr(
                    arm.body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?;
                unbind_pattern(bound, env);
                out_arms.push(crate::ast::MatchArm {
                    pattern,
                    guard,
                    body,
                    unpack: arm.unpack,
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
            implicit_else,
        } => {
            let then_branch = elaborate_expr(
                *then_branch,
                enums,
                funs,
                methods,
                current_module,
                env,
                expected,
                tparams,
            )?;
            let else_branch = if implicit_else {
                let tt = infer(&then_branch, enums, funs, methods, current_module, env)?;
                match tt {
                    Type::Io(_) => Expr::new(
                        ExprKind::IoPure(Box::new(Expr::new(ExprKind::Unit, span.clone()))),
                        span.clone(),
                    ),
                    _ => *else_branch,
                }
            } else {
                elaborate_expr(
                    *else_branch,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?
            };
            Ok(Expr::new(
                ExprKind::If {
                    cond: Box::new(elaborate_expr(
                        *cond,
                        enums,
                        funs,
                        methods,
                        current_module,
                        env,
                        None,
                        tparams,
                    )?),
                    then_branch: Box::new(then_branch),
                    else_branch: Box::new(else_branch),
                    implicit_else,
                },
                span,
            ))
        }
        ExprKind::Call { callee, args } => {
            if let Ok(f) = funs.resolve(&callee, current_module) {
                let mut subst: HashMap<String, Type> = HashMap::new();
                for (a, p) in args.iter().zip(f.params.iter()) {
                    // Lambda args mention binder names that are unbound here.
                    // They contribute nothing to the constructor substitution.
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
                Ok(Expr::new(
                    ExprKind::Call {
                        callee,
                        args: new_args,
                    },
                    span,
                ))
            } else {
                let nargs = args.len();
                let mut out = Vec::with_capacity(nargs);
                let mut prior: Vec<Type> = Vec::new();
                for (i, a) in args.into_iter().enumerate() {
                    let elaborated = if let Some(pty) =
                        kit_lambda_param_ty_at(&callee, i, nargs, &prior)
                    {
                        elaborate_lambda_arg(
                            a,
                            pty,
                            enums,
                            funs,
                            methods,
                            current_module,
                            env,
                            tparams,
                        )?
                    } else {
                        elaborate_expr(a, enums, funs, methods, current_module, env, None, tparams)?
                    };
                    let ty = infer(&elaborated, enums, funs, methods, current_module, env)
                        .unwrap_or_else(|_| Type::Opaque("Rewrite".into()));
                    prior.push(ty);
                    out.push(elaborated);
                }
                Ok(Expr::new(ExprKind::Call { callee, args: out }, span))
            }
        }
        ExprKind::Let { name, value, body } => {
            let value = elaborate_expr(
                *value,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?;
            let vt = infer(&value, enums, funs, methods, current_module, env)?;
            let old = env.insert(name.clone(), vt);
            let body = elaborate_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                expected,
                tparams,
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
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let mut old = None;
            if let (Type::Io(inner_t), Some(p)) = (&it, &param) {
                old = env.insert(p.clone(), (**inner_t).clone());
            }
            let body = elaborate_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                expected,
                tparams,
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
        ExprKind::IoMap { inner, param, body } => {
            let inner = elaborate_expr(
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?;
            let it = infer(&inner, enums, funs, methods, current_module, env)?;
            let mut old = None;
            if let (Type::Io(inner_t), Some(p)) = (&it, &param) {
                old = env.insert(p.clone(), (**inner_t).clone());
            }
            let body_expected = match expected {
                Some(Type::Io(t)) => Some(&**t),
                _ => None,
            };
            let body = elaborate_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                body_expected,
                tparams,
            )?;
            if let Some(p) = &param {
                if let Some(v) = old {
                    env.insert(p.clone(), v);
                } else {
                    env.remove(p);
                }
            }
            Ok(Expr::new(
                ExprKind::IoMap {
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
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?)),
            span,
        )),
        ExprKind::IoSleep(inner) => Ok(Expr::new(
            ExprKind::IoSleep(Box::new(elaborate_expr(
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?)),
            span,
        )),
        ExprKind::IoFail(inner) => Ok(Expr::new(
            ExprKind::IoFail(Box::new(elaborate_expr(
                *inner,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?)),
            span,
        )),
        ExprKind::HandleErrorWith { inner, param, body } => Ok(Expr::new(
            ExprKind::HandleErrorWith {
                inner: Box::new(elaborate_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
                param,
                body: Box::new(elaborate_expr(
                    *body,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Attempt { inner } => Ok(Expr::new(
            ExprKind::Attempt {
                inner: Box::new(elaborate_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::IoRace { left, right } => Ok(Expr::new(
            ExprKind::IoRace {
                left: Box::new(elaborate_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::IoBoth { left, right } => Ok(Expr::new(
            ExprKind::IoBoth {
                left: Box::new(elaborate_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Tuple { elems } => {
            let slot_exp: Vec<Option<&Type>> = match expected {
                Some(Type::Tuple(xs)) if xs.len() == elems.len() => xs.iter().map(Some).collect(),
                _ => vec![None; elems.len()],
            };
            Ok(Expr::new(
                ExprKind::Tuple {
                    elems: elems
                        .into_iter()
                        .zip(slot_exp)
                        .map(|(e, exp)| {
                            elaborate_expr(
                                e,
                                enums,
                                funs,
                                methods,
                                current_module,
                                env,
                                exp,
                                tparams,
                            )
                        })
                        .collect::<Result<Vec<_>, _>>()?,
                },
                span,
            ))
        }
        ExprKind::IoEnsure { inner, finalizer } => Ok(Expr::new(
            ExprKind::IoEnsure {
                inner: Box::new(elaborate_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
                finalizer: Box::new(elaborate_expr(
                    *finalizer,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::IoTimeout { ms, inner } => Ok(Expr::new(
            ExprKind::IoTimeout {
                ms: Box::new(elaborate_expr(
                    *ms,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
                inner: Box::new(elaborate_expr(
                    *inner,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    expected,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Field { base, field } => Ok(Expr::new(
            ExprKind::Field {
                base: Box::new(elaborate_expr(
                    *base,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
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
                    *receiver,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
                method,
                args: args
                    .into_iter()
                    .map(|a| {
                        elaborate_expr(a, enums, funs, methods, current_module, env, None, tparams)
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::ListLit { elems } => {
            let elem_expected = match expected {
                Some(Type::List(t)) => Some(t.as_ref()),
                _ => None,
            };
            Ok(Expr::new(
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
                                elem_expected,
                                tparams,
                            )
                        })
                        .collect::<Result<Vec<_>, _>>()?,
                },
                span,
            ))
        }
        ExprKind::Interpolate { parts } => Ok(Expr::new(
            ExprKind::Interpolate {
                parts: parts
                    .into_iter()
                    .map(|p| match p {
                        crate::ast::InterpPart::Lit(s) => Ok(crate::ast::InterpPart::Lit(s)),
                        crate::ast::InterpPart::Expr(e) => {
                            Ok(crate::ast::InterpPart::Expr(elaborate_expr(
                                e,
                                enums,
                                funs,
                                methods,
                                current_module,
                                env,
                                None,
                                tparams,
                            )?))
                        }
                    })
                    .collect::<Result<Vec<_>, _>>()?,
            },
            span,
        )),
        ExprKind::Binary { op, left, right } => Ok(Expr::new(
            ExprKind::Binary {
                op,
                left: Box::new(elaborate_expr(
                    *left,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
                right: Box::new(elaborate_expr(
                    *right,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Unary { op, expr } => Ok(Expr::new(
            ExprKind::Unary {
                op,
                expr: Box::new(elaborate_expr(
                    *expr,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Ascribe { expr, ty } => {
            let want = resolve_ascribe_type(&ty, enums, current_module)?;
            let inner = elaborate_expr(
                *expr,
                enums,
                funs,
                methods,
                current_module,
                env,
                Some(&want),
                tparams,
            )?;
            let got = infer(&inner, enums, funs, methods, current_module, env)?;
            expect_ty(&got, &want).map_err(|e| e.with_span_if_bare(&span))?;
            // Keep the pin. `Queue.unbounded` / `Deferred.empty` have no witness.
            Ok(Expr::new(
                ExprKind::Ascribe {
                    expr: Box::new(inner),
                    ty: want,
                },
                span,
            ))
        }
        ExprKind::NamedArg { name, value } => Ok(Expr::new(
            ExprKind::NamedArg {
                name,
                value: Box::new(elaborate_expr(
                    *value,
                    enums,
                    funs,
                    methods,
                    current_module,
                    env,
                    None,
                    tparams,
                )?),
            },
            span,
        )),
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => {
            let bind_ty = if let Some(ann) = &param_ty {
                resolve_ascribe_type(ann, enums, current_module)?
            } else if let Some(Type::Fun(a, _)) = expected {
                a.as_ref().clone()
            } else {
                Type::Opaque("Param".into())
            };
            let body_expected = match expected {
                Some(Type::Fun(_, b)) => Some(b.as_ref()),
                _ => None,
            };
            let old = bind_opt(param.as_ref(), bind_ty.clone(), env);
            let body = elaborate_expr(
                *body,
                enums,
                funs,
                methods,
                current_module,
                env,
                body_expected,
                tparams,
            )?;
            restore_opt(old, env);
            let param_ty = param_ty.or(if matches!(bind_ty, Type::Opaque(_)) {
                None
            } else {
                Some(bind_ty)
            });
            Ok(Expr::new(
                ExprKind::Lambda {
                    param,
                    param_ty,
                    pat,
                    body: Box::new(body),
                },
                span,
            ))
        }
        ExprKind::For { .. } => panic!("internal: unlowered `for` in elaboration"),
        ExprKind::Apply { fun, arg } => {
            let arg = elaborate_expr(
                *arg,
                enums,
                funs,
                methods,
                current_module,
                env,
                None,
                tparams,
            )?;
            let at = infer(&arg, enums, funs, methods, current_module, env)?;
            let want = Type::Fun(Box::new(at), Box::new(Type::Opaque("Elem".into())));
            let fun = elaborate_expr(
                *fun,
                enums,
                funs,
                methods,
                current_module,
                env,
                Some(&want),
                tparams,
            )?;
            Ok(Expr::new(
                ExprKind::Apply {
                    fun: Box::new(fun),
                    arg: Box::new(arg),
                },
                span,
            ))
        }
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
            args: args
                .into_iter()
                .map(|a| subst_node_targs(a, subst))
                .collect(),
            type_args: type_args.iter().map(|t| apply_subst(t, subst)).collect(),
        },
        ExprKind::Match { scrutinee, arms } => ExprKind::Match {
            scrutinee: Box::new(subst_node_targs(*scrutinee, subst)),
            arms: arms
                .into_iter()
                .map(|a| crate::ast::MatchArm {
                    pattern: subst_pattern(a.pattern, subst),
                    guard: a.guard.map(|g| subst_node_targs(g, subst)),
                    body: subst_node_targs(a.body, subst),
                    unpack: a.unpack,
                })
                .collect(),
        },
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
            implicit_else,
        } => ExprKind::If {
            cond: Box::new(subst_node_targs(*cond, subst)),
            then_branch: Box::new(subst_node_targs(*then_branch, subst)),
            else_branch: Box::new(subst_node_targs(*else_branch, subst)),
            implicit_else,
        },
        ExprKind::Call { callee, args } => ExprKind::Call {
            callee,
            args: args
                .into_iter()
                .map(|a| subst_node_targs(a, subst))
                .collect(),
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
        ExprKind::IoMap { inner, param, body } => ExprKind::IoMap {
            inner: Box::new(subst_node_targs(*inner, subst)),
            param,
            body: Box::new(subst_node_targs(*body, subst)),
        },
        ExprKind::IoPrintln(e) => ExprKind::IoPrintln(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoSleep(e) => ExprKind::IoSleep(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoFail(e) => ExprKind::IoFail(Box::new(subst_node_targs(*e, subst))),
        ExprKind::IoPure(e) => ExprKind::IoPure(Box::new(subst_node_targs(*e, subst))),
        ExprKind::HandleErrorWith { inner, param, body } => ExprKind::HandleErrorWith {
            inner: Box::new(subst_node_targs(*inner, subst)),
            param,
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
        ExprKind::Tuple { elems } => ExprKind::Tuple {
            elems: elems
                .into_iter()
                .map(|e| subst_node_targs(e, subst))
                .collect(),
        },
        ExprKind::IoEnsure { inner, finalizer } => ExprKind::IoEnsure {
            inner: Box::new(subst_node_targs(*inner, subst)),
            finalizer: Box::new(subst_node_targs(*finalizer, subst)),
        },
        ExprKind::IoTimeout { ms, inner } => ExprKind::IoTimeout {
            ms: Box::new(subst_node_targs(*ms, subst)),
            inner: Box::new(subst_node_targs(*inner, subst)),
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
            args: args
                .into_iter()
                .map(|a| subst_node_targs(a, subst))
                .collect(),
        },
        ExprKind::ListLit { elems } => ExprKind::ListLit {
            elems: elems
                .into_iter()
                .map(|e| subst_node_targs(e, subst))
                .collect(),
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
        ExprKind::Unary { op, expr } => ExprKind::Unary {
            op,
            expr: Box::new(subst_node_targs(*expr, subst)),
        },
        ExprKind::NamedArg { name, value } => ExprKind::NamedArg {
            name,
            value: Box::new(subst_node_targs(*value, subst)),
        },
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => ExprKind::Lambda {
            param,
            param_ty: param_ty.map(|t| apply_subst(&t, subst)),
            pat,
            body: Box::new(subst_node_targs(*body, subst)),
        },
        ExprKind::Ascribe { expr, ty } => ExprKind::Ascribe {
            expr: Box::new(subst_node_targs(*expr, subst)),
            ty: apply_subst(&ty, subst),
        },
        ExprKind::Apply { fun, arg } => ExprKind::Apply {
            fun: Box::new(subst_node_targs(*fun, subst)),
            arg: Box::new(subst_node_targs(*arg, subst)),
        },
        other => other,
    };
    Expr::new(kind, span)
}

fn subst_pattern(pat: crate::ast::Pattern, subst: &HashMap<String, Type>) -> crate::ast::Pattern {
    match pat {
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds: binds.into_iter().map(|b| subst_pattern(b, subst)).collect(),
            type_args: type_args.iter().map(|t| apply_subst(t, subst)).collect(),
        },
        crate::ast::Pattern::Or(alts) => {
            crate::ast::Pattern::Or(alts.into_iter().map(|a| subst_pattern(a, subst)).collect())
        }
        crate::ast::Pattern::As { name, inner } => crate::ast::Pattern::As {
            name,
            inner: Box::new(subst_pattern(*inner, subst)),
        },
        crate::ast::Pattern::Cons { head, tail, elem } => crate::ast::Pattern::Cons {
            head: Box::new(subst_pattern(*head, subst)),
            tail: Box::new(subst_pattern(*tail, subst)),
            elem: apply_subst(&elem, subst),
        },
        crate::ast::Pattern::Tuple { elems, tys } => crate::ast::Pattern::Tuple {
            elems: elems.into_iter().map(|e| subst_pattern(e, subst)).collect(),
            tys: tys.iter().map(|t| apply_subst(t, subst)).collect(),
        },
        crate::ast::Pattern::Named { name, inner } => crate::ast::Pattern::Named {
            name,
            inner: Box::new(subst_pattern(*inner, subst)),
        },
        p => p,
    }
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
            if !is_handle_ctor(id) {
                out.push((id.clone(), args.clone()));
            }
            for a in args {
                collect_apps_in_type(a, out);
            }
        }
        Type::Io(inner) | Type::List(inner) => collect_apps_in_type(inner, out),
        Type::Fun(a, b) => {
            collect_apps_in_type(a, out);
            collect_apps_in_type(b, out);
        }
        Type::Tuple(xs) => {
            for t in xs {
                collect_apps_in_type(t, out);
            }
        }
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
                collect_pattern_targs(&a.pattern, out);
                if let Some(g) = &a.guard {
                    collect_node_targs(g, out);
                }
                collect_node_targs(&a.body, out);
            }
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
            ..
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
        ExprKind::FlatMap { inner, body, .. } | ExprKind::IoMap { inner, body, .. } => {
            collect_node_targs(inner, out);
            collect_node_targs(body, out);
        }
        ExprKind::IoPrintln(e)
        | ExprKind::IoSleep(e)
        | ExprKind::IoFail(e)
        | ExprKind::IoPure(e)
        | ExprKind::Attempt { inner: e } => collect_node_targs(e, out),
        ExprKind::HandleErrorWith { inner, body, .. } => {
            collect_node_targs(inner, out);
            collect_node_targs(body, out);
        }
        ExprKind::IoRace { left, right }
        | ExprKind::IoBoth { left, right }
        | ExprKind::IoEnsure {
            inner: left,
            finalizer: right,
        }
        | ExprKind::IoTimeout {
            ms: left,
            inner: right,
        } => {
            collect_node_targs(left, out);
            collect_node_targs(right, out);
        }
        ExprKind::Tuple { elems } => {
            for e in elems {
                collect_node_targs(e, out);
            }
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
        ExprKind::Binary { left, right, .. }
        | ExprKind::Apply {
            fun: left,
            arg: right,
        } => {
            collect_node_targs(left, out);
            collect_node_targs(right, out);
        }
        ExprKind::Unary { expr, .. } | ExprKind::NamedArg { value: expr, .. } => {
            collect_node_targs(expr, out)
        }
        ExprKind::Ascribe { expr, ty } => {
            collect_apps_in_type(ty, out);
            collect_node_targs(expr, out);
        }
        ExprKind::Lambda { param_ty, body, .. } => {
            if let Some(t) = param_ty {
                collect_apps_in_type(t, out);
            }
            collect_node_targs(body, out);
        }
        _ => {}
    }
}

fn collect_pattern_targs(pat: &crate::ast::Pattern, out: &mut Vec<(String, Vec<Type>)>) {
    if let crate::ast::Pattern::Adt {
        enum_name,
        binds,
        type_args,
        ..
    } = pat
    {
        if !type_args.is_empty() {
            out.push((enum_name.clone(), type_args.clone()));
            for t in type_args {
                collect_apps_in_type(t, out);
            }
        }
        for b in binds {
            collect_pattern_targs(b, out);
        }
    } else if let crate::ast::Pattern::Or(alts) = pat {
        for a in alts {
            collect_pattern_targs(a, out);
        }
    } else if let crate::ast::Pattern::As { inner, .. } = pat {
        collect_pattern_targs(inner, out);
    } else if let crate::ast::Pattern::Cons { head, tail, elem } = pat {
        collect_apps_in_type(elem, out);
        collect_pattern_targs(head, out);
        collect_pattern_targs(tail, out);
    } else if let crate::ast::Pattern::Named { inner, .. } = pat {
        collect_pattern_targs(inner, out);
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
                    Ok(crate::ast::MatchArm {
                        pattern: rewrite_pattern(a.pattern, clones)?,
                        guard: match a.guard {
                            Some(g) => Some(rewrite_enum_refs(g, clones)?),
                            None => None,
                        },
                        body: rewrite_enum_refs(a.body, clones)?,
                        unpack: a.unpack,
                    })
                })
                .collect::<Result<Vec<_>, TypeError>>()?,
        },
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
            implicit_else,
        } => ExprKind::If {
            cond: Box::new(rewrite_enum_refs(*cond, clones)?),
            then_branch: Box::new(rewrite_enum_refs(*then_branch, clones)?),
            else_branch: Box::new(rewrite_enum_refs(*else_branch, clones)?),
            implicit_else,
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
        ExprKind::IoMap { inner, param, body } => ExprKind::IoMap {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
            param,
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        ExprKind::IoPrintln(e) => ExprKind::IoPrintln(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoSleep(e) => ExprKind::IoSleep(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoFail(e) => ExprKind::IoFail(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::IoPure(e) => ExprKind::IoPure(Box::new(rewrite_enum_refs(*e, clones)?)),
        ExprKind::HandleErrorWith { inner, param, body } => ExprKind::HandleErrorWith {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
            param,
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
        ExprKind::Tuple { elems } => ExprKind::Tuple {
            elems: elems
                .into_iter()
                .map(|e| rewrite_enum_refs(e, clones))
                .collect::<Result<Vec<_>, _>>()?,
        },
        ExprKind::IoEnsure { inner, finalizer } => ExprKind::IoEnsure {
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
            finalizer: Box::new(rewrite_enum_refs(*finalizer, clones)?),
        },
        ExprKind::IoTimeout { ms, inner } => ExprKind::IoTimeout {
            ms: Box::new(rewrite_enum_refs(*ms, clones)?),
            inner: Box::new(rewrite_enum_refs(*inner, clones)?),
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
        ExprKind::Unary { op, expr } => ExprKind::Unary {
            op,
            expr: Box::new(rewrite_enum_refs(*expr, clones)?),
        },
        ExprKind::NamedArg { name, value } => ExprKind::NamedArg {
            name,
            value: Box::new(rewrite_enum_refs(*value, clones)?),
        },
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => ExprKind::Lambda {
            param,
            param_ty: match param_ty {
                Some(t) => Some(concretize_type(&t, clones)?),
                None => None,
            },
            pat,
            body: Box::new(rewrite_enum_refs(*body, clones)?),
        },
        ExprKind::Ascribe { expr, ty } => ExprKind::Ascribe {
            expr: Box::new(rewrite_enum_refs(*expr, clones)?),
            ty: concretize_type(&ty, clones)?,
        },
        ExprKind::Apply { fun, arg } => ExprKind::Apply {
            fun: Box::new(rewrite_enum_refs(*fun, clones)?),
            arg: Box::new(rewrite_enum_refs(*arg, clones)?),
        },
        other => other,
    };
    Ok(Expr::new(kind, span))
}

fn rewrite_pattern(
    pat: crate::ast::Pattern,
    clones: &HashMap<(String, Vec<Type>), String>,
) -> Result<crate::ast::Pattern, TypeError> {
    match pat {
        crate::ast::Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => {
            let binds = binds
                .into_iter()
                .map(|b| rewrite_pattern(b, clones))
                .collect::<Result<Vec<_>, _>>()?;
            if type_args.is_empty() {
                Ok(crate::ast::Pattern::Adt {
                    enum_name,
                    case_name,
                    binds,
                    type_args,
                })
            } else {
                let id = clones
                    .get(&(enum_name.clone(), type_args.clone()))
                    .cloned()
                    .ok_or_else(|| {
                        TypeError::Msg(format!(
                            "internal: missing enum clone for {enum_name}{type_args:?}"
                        ))
                    })?;
                Ok(crate::ast::Pattern::Adt {
                    enum_name: id,
                    case_name,
                    binds,
                    type_args: Vec::new(),
                })
            }
        }
        crate::ast::Pattern::Or(alts) => Ok(crate::ast::Pattern::Or(
            alts.into_iter()
                .map(|a| rewrite_pattern(a, clones))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        crate::ast::Pattern::As { name, inner } => Ok(crate::ast::Pattern::As {
            name,
            inner: Box::new(rewrite_pattern(*inner, clones)?),
        }),
        crate::ast::Pattern::Cons { head, tail, elem } => Ok(crate::ast::Pattern::Cons {
            head: Box::new(rewrite_pattern(*head, clones)?),
            tail: Box::new(rewrite_pattern(*tail, clones)?),
            elem: concretize_type(&elem, clones)?,
        }),
        crate::ast::Pattern::Tuple { elems, tys } => Ok(crate::ast::Pattern::Tuple {
            elems: elems
                .into_iter()
                .map(|e| rewrite_pattern(e, clones))
                .collect::<Result<Vec<_>, _>>()?,
            tys: tys
                .iter()
                .map(|t| concretize_type(t, clones))
                .collect::<Result<Vec<_>, _>>()?,
        }),
        crate::ast::Pattern::Named { name, inner } => Ok(crate::ast::Pattern::Named {
            name,
            inner: Box::new(rewrite_pattern(*inner, clones)?),
        }),
        p => Ok(p),
    }
}

/// Replace applied types with their cloned enum ids (post-specialization).
fn concretize_type(
    ty: &Type,
    clones: &HashMap<(String, Vec<Type>), String>,
) -> Result<Type, TypeError> {
    match ty {
        Type::App(id, args) => {
            if is_handle_ctor(id) {
                let nargs = args
                    .iter()
                    .map(|a| concretize_type(a, clones))
                    .collect::<Result<Vec<_>, _>>()?;
                return Ok(Type::App(id.clone(), nargs));
            }
            let cid = clones.get(&(id.clone(), args.clone())).ok_or_else(|| {
                TypeError::Msg(format!("internal: missing enum clone for {id}{args:?}"))
            })?;
            Ok(Type::Adt(cid.clone()))
        }
        Type::Io(inner) => Ok(Type::Io(Box::new(concretize_type(inner, clones)?))),
        Type::List(inner) => Ok(Type::List(Box::new(concretize_type(inner, clones)?))),
        Type::Tuple(xs) => Ok(Type::Tuple(
            xs.iter()
                .map(|t| concretize_type(t, clones))
                .collect::<Result<Vec<_>, _>>()?,
        )),
        Type::Fun(a, b) => Ok(Type::Fun(
            Box::new(concretize_type(a, clones)?),
            Box::new(concretize_type(b, clones)?),
        )),
        other => Ok(other.clone()),
    }
}

/// Clone generic enums per concrete instantiation. Erase `App` everywhere.
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
        let en = enums
            .resolve(&id, "")
            .map_err(|e| TypeError::Msg(format!("internal: unknown generic enum {id}: {e}")))?;
        let subst: HashMap<String, Type> = en
            .type_params
            .iter()
            .cloned()
            .zip(args.iter().cloned())
            .collect();
        let mangled = mono_enum_name(en, &args);
        let clone_id = crate::resolve::enum_id(&en.module, &mangled);
        if clone_defs
            .iter()
            .any(|e| e.module == en.module && e.name == mangled)
        {
            clones.insert((id, args), clone_id);
            continue;
        }
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
                field_rfns: case.field_rfns.clone(),
            });
        }
        clones.insert((id, args), clone_id);
        clone_defs.push(EnumDef {
            module: en.module.clone(),
            name: mangled,
            type_params: Vec::new(),
            cases,
            is_record: en.is_record,
            methods: Vec::new(),
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
        d.body = rewrite_enum_refs(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &clones,
        )?;
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
    // Generic templates are gone. Drop impls that targeted them so later
    // MethodIndex rebuilds (second resolve_field_access) still typecheck.
    program
        .impls
        .retain(|im| program.enums.iter().any(|e| e.name == im.for_type));
    Ok(program)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lower::lower_program;
    use crate::parser::{parse, parse_file};

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
    fn rejects_io_payload_mismatch() {
        let src = r#"
def asString(): IO[String] = IO.pure("x")
@main def main: IO[Unit] = asString()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("IO") || err.contains("String"), "{err}");
    }

    #[test]
    fn typechecks_io_fail_as_io_int() {
        let src = r#"
def boom(): IO[Int] = IO.fail("x")
def choose(ok: Bool): IO[Int] =
  if (ok) IO.pure(1) else IO.fail("nope")
def recover(): IO[Int] =
  IO.fail("x").handleErrorWith(_ => IO.pure(2))
def raceFail(): IO[Int] =
  IO.race(IO.fail("x"), IO.pure(3))
@main def main: IO[Unit] =
  for {
    a <- boom()
    b <- choose(true)
    c <- recover()
    d <- raceFail()
    _ <- IO.println(Str.fromInt(a + b + c + d))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("IO.fail joins IO[Int]");
    }

    #[test]
    fn typechecks_for_if_guard() {
        let src = r#"
def positive(n: Int): IO[Int] =
  for {
    x <- IO.pure(n)
    if x > 0
  } yield x
@main def main: IO[Unit] =
  for {
    n <- positive(3)
    _ <- IO.println(Str.fromInt(n))
    _ <- positive(0).handleErrorWith(_ => IO.println("miss"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("for if guard typecheck");
    }

    #[test]
    fn typechecks_io_map() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    n <- IO.pure(3).map(x => x + 1)
    s <- IO.pure("a").map(x => Str.concat(x, "!"))
    z <- IO.pure(1).map(_ => 7)
    _ <- IO.println(Str.fromInt(n))
    _ <- IO.println(s)
    _ <- IO.println(Str.fromInt(z))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("IO.map typecheck");
    }

    #[test]
    fn rejects_io_map_on_int() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(1.map(n => n + 1)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("map receiver must be IO[_]"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_if_without_else_io_unit() {
        let src = r#"
def maybeYes(ok: Bool): IO[Unit] =
  if (ok) IO.println("y")
@main def main: IO[Unit] =
  for {
    _ <- maybeYes(true)
    _ <- maybeYes(false)
    _ <- if (true) IO.println("y")
    _ <- if (false) IO.println("n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("if without else IO[Unit]");
    }

    #[test]
    fn typechecks_if_without_else_unit() {
        let src = r#"
def flagUnit(ok: Bool): Unit =
  if (ok) ()
@main def main: IO[Unit] =
  for {
    _ = flagUnit(true)
    _ = if (false) ()
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("if without else Unit");
    }

    #[test]
    fn typechecks_if_without_else_fail() {
        let src = r#"
def boom(ok: Bool): IO[Unit] =
  if (ok) IO.fail("x")
@main def main: IO[Unit] =
  boom(false).handleErrorWith(_ => IO.println("ok"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("if without else IO.fail");
    }

    #[test]
    fn rejects_if_without_else_int() {
        let src = r#"
def bad(ok: Bool): Int =
  if (ok) 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("if without else"), "{err}");
    }

    #[test]
    fn rejects_if_without_else_io_int() {
        let src = r#"
def bad(ok: Bool): IO[Int] =
  if (ok) IO.pure(1)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("if without else"), "{err}");
    }

    #[test]
    fn rejects_if_fail_vs_string_io() {
        let src = r#"
def bad(ok: Bool): IO[Int] =
  if (ok) IO.pure(1) else IO.pure("x")
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("if branches disagree"), "{err}");
    }

    #[test]
    fn rejects_mixed_list_literal() {
        let src = r#"
@main def main: IO[Unit] = IO.println(List.join([1, "x"], ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("list element type mismatch") || err.contains("expected"),
            "{err}"
        );
    }

    #[test]
    fn fiber_join_preserves_payload() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    f <- Fiber.fork(IO.pure("ok"))
    v <- Fiber.join(f)
    _ <- IO.println(v)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fiber.join String payload");
    }

    #[test]
    fn rejects_signal_str_as_signal_int() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    s = Signal.str("x")
    _ <- Ui.run(_ => View.checkbox(s, "n"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("SignalInt") || err.contains("SignalStr") || err.contains("expected"),
            "{err}"
        );
    }

    #[test]
    fn rejects_unreachable_match_arm() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case _ => IO.println("all")
    case Color.Red => IO.println("r")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("unreachable match arm"), "{err}");
    }

    #[test]
    fn first_order_function_type_on_def() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] =
  IO.println(apply(x => Str.fromInt(x), 1))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Int => String param");
    }

    #[test]
    fn typechecks_placeholder_add_on_list_map() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.map([1, 2], _ + 1), Str.fromInt(_)), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("placeholder `_ + 1` and `Str.fromInt(_)` on List.map");
    }

    #[test]
    fn typechecks_placeholder_filter() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.filter(["a", "b", "c"], _ != "b"), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("placeholder `_ != \"b\"` on List.filter");
    }

    #[test]
    fn typechecks_placeholder_on_user_fun() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] =
  IO.println(apply(Str.fromInt(_), 1))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("placeholder on A => B param");
    }

    #[test]
    fn typechecks_eta_kit_on_list_map() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([1, 2], Str.fromInt()), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("eta Str.fromInt on List.map");
        let p = crate::typ::elaborate_generics(p).expect("elaborate eta map");
        crate::typ::monomorphize(p).expect("mono eta map");
    }

    #[test]
    fn typechecks_eta_kit_on_user_fun() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] =
  IO.println(apply(Str.fromInt(), 1))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("eta Str.fromInt on A => B param");
        let p = crate::typ::elaborate_generics(p).expect("elaborate eta fun");
        crate::typ::monomorphize(p).expect("mono eta fun");
    }

    #[test]
    fn typechecks_eta_user_def_on_fun_param() {
        let src = r#"
def bump(n: Int): Int = n + 1
def apply(f: Int => Int, n: Int): Int = f(n)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(apply(bump, 3)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("eta unary def on A => B param");
        let p = crate::typ::elaborate_generics(p).expect("elaborate eta def");
        crate::typ::monomorphize(p).expect("mono eta def");
    }

    #[test]
    fn typechecks_eta_generic_id() {
        let src = r#"
def id[T](x: T): T = x
def apply(f: Int => Int, n: Int): Int = f(n)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(apply(id, 3)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("eta generic id on Int => Int");
        let p = crate::typ::elaborate_generics(p).expect("elaborate eta id");
        crate::typ::monomorphize(p).expect("mono eta id");
    }

    #[test]
    fn typechecks_eta_empty_list_pin() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([]: List[Int], Str.fromInt()), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("eta on empty List[Int] pin");
    }

    #[test]
    fn typechecks_let_bound_fun() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    inc = (_ + 1): Int => Int
    typed = (n: Int) => n + 1
    eta = Str.fromInt: Int => String
    _ <- IO.println(Str.fromInt(inc(3)))
    _ <- IO.println(Str.fromInt(typed(4)))
    _ <- IO.println(eta(7))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("let-bound A => B");
        let p = crate::typ::elaborate_generics(p).expect("elaborate let fun");
        crate::typ::monomorphize(p).expect("mono let fun");
    }

    #[test]
    fn typechecks_fun_return_and_apply() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
def addN(n: Int): Int => Int = (m: Int) => n + m
def apply(f: Int => Int, n: Int): Int = f(n)
@main def main: IO[Unit] =
  for {
    f = plusOne()
    add3 = addN(3)
    inc = (_ + 1): Int => Int
    _ <- IO.println(Str.fromInt(f(5)))
    _ <- IO.println(Str.fromInt(add3(4)))
    _ <- IO.println(Str.fromInt(apply(inc, 5)))
    _ <- IO.println(Str.fromInt(apply(plusOne(), 6)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fun return and apply");
        let p = crate::typ::elaborate_generics(p).expect("elaborate fun ret");
        crate::typ::monomorphize(p).expect("mono fun ret");
    }

    #[test]
    fn typechecks_fun_expr_apply() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
def addN(n: Int): Int => Int = (m: Int) => n + m
@main def main: IO[Unit] =
  for {
    inc = (_ + 1): Int => Int
    _ <- IO.println(Str.fromInt(plusOne()(5)))
    _ <- IO.println(Str.fromInt(addN(3)(4)))
    _ <- IO.println(Str.fromInt(((n: Int) => n + 1)(6)))
    _ <- IO.println(Str.fromInt((inc)(3)))
    _ <- IO.println(Str.fromInt((_ + 1)(8)))
    _ <- IO.println(Str.fromInt()(7))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("A => B expression apply");
        let p = crate::typ::elaborate_generics(p).expect("elaborate apply");
        crate::typ::monomorphize(p).expect("mono apply");
    }

    #[test]
    fn typechecks_fun_tuple_apply() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
def applyPair(f: (Int, Int) => Int, x: Int, y: Int): Int = f(x, y)
def applyTup(f: (Int, Int) => Int, p: (Int, Int)): Int = f(p)
@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(applyPair((a, b) => a + b, 2, 3)))
    _ <- IO.println(Str.fromInt(applyPair(add, 2, 3)))
    _ <- IO.println(Str.fromInt(applyTup(add, (2, 3))))
    _ <- IO.println(Str.fromInt(((a, b) => a + b)(2, 3)))
    _ <- IO.println(Str.fromInt(List.foldLeft([1, 2, 3], 0, add)))
    _ <- IO.println(Str.fromInt(List.foldLeft([]: List[Int], 7, add)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("tuple Fun apply and n-ary eta");
        let p = crate::typ::elaborate_generics(p).expect("elaborate tuple apply");
        crate::typ::monomorphize(p).expect("mono tuple apply");
    }

    #[test]
    fn rejects_unary_fun_tuple_apply() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
@main def main: IO[Unit] =
  IO.println(Str.fromInt(plusOne()(1, 2)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("mismatch") || err.message().contains("apply"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_tuple_fun_arity() {
        let src = r#"
def applyPair(f: (Int, Int) => Int, x: Int): Int = f(x, 1, 2)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(applyPair((a, b) => a + b, 1)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expects 2 args") || err.message().contains("arg"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_apply_on_non_fun() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt((1 + 2)(3)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("apply needs A => B") || err.message().contains("apply"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_apply_arg_mismatch() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
@main def main: IO[Unit] =
  IO.println(Str.fromInt(plusOne()("x")))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("mismatch") || err.message().contains("apply"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_named_fun_on_list_map() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    eta = Str.fromInt: Int => String
    _ <- IO.println(List.join(List.map([1, 2], eta), ","))
    _ <- IO.println(List.join(List.map([]: List[Int], eta), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("named Fun on List.map");
        let p = crate::typ::elaborate_generics(p).expect("elaborate named map");
        crate::typ::monomorphize(p).expect("mono named map");
    }

    #[test]
    fn rejects_named_fun_param_mismatch() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    inc = (_ + 1): Int => Int
    _ <- IO.println(List.join(List.map(["a"], inc), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("param mismatch") || err.message().contains("mismatch"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_eta_arity_mismatch() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map(["a"], Str.concat), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expects 2 args") || err.message().contains("arg"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_placeholder_identity() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map(["a", "b"], _), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("placeholder `_` identity on List.map");
    }

    #[test]
    fn rejects_two_placeholder_holes() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([1, 2], _ + _), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(err.message().contains("one hole"), "{}", err.message());
    }

    #[test]
    fn rejects_placeholder_without_function_expected() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(_ + 1)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(err.message().contains("placeholder"), "{}", err.message());
    }

    #[test]
    fn typechecks_typed_lambda_on_list_map() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([1, 2], (n: Int) => Str.fromInt(n)), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("typed Int lambda on List.map");
    }

    #[test]
    fn typechecks_typed_lambda_on_user_fun() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] =
  IO.println(apply((x: Int) => Str.fromInt(x), 1))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("typed Int lambda on Fun param");
    }

    #[test]
    fn typechecks_case_lambda_on_list_map() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def noneInt(): Opt[Int] = Opt.None
@main def main: IO[Unit] =
  IO.println(List.join(List.map([Opt.Some(1), noneInt()], { case Opt.Some(n) => Str.fromInt(n) case Opt.None => "n" }), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("case lambda on List.map");
        let p = crate::typ::elaborate_generics(p).expect("elaborate case lambda map");
        crate::typ::monomorphize(p).expect("mono case lambda map");
    }

    #[test]
    fn typechecks_case_lambda_on_user_fun() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def apply(f: Opt[Int] => String, o: Opt[Int]): String = f(o)
@main def main: IO[Unit] =
  IO.println(apply({ case Opt.Some(n) => Str.fromInt(n) case Opt.None => "?" }, Opt.Some(3)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("case lambda on A => B param");
        let p = crate::typ::elaborate_generics(p).expect("elaborate case lambda");
        crate::typ::monomorphize(p).expect("mono case lambda");
    }

    #[test]
    fn typechecks_case_lambda_on_io_map() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.pure(Opt.Some(1)).map({ case Opt.Some(n) => n case Opt.None => 0 }).flatMap(n => IO.println(Str.fromInt(n)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("case lambda on IO.map");
    }

    #[test]
    fn typechecks_case_lambda_empty_list_pin() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.println(List.join(List.map([]: List[Opt[Int]], { case Opt.Some(n) => Str.fromInt(n) case Opt.None => "n" }), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("case lambda on pinned empty List");
    }

    #[test]
    fn typechecks_case_lambda_literal() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([0, 2], { case 0 => "z" case _ => "n" }), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("case lambda literal arms");
    }

    #[test]
    fn rejects_case_lambda_nonexhaustive() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  IO.println(List.join(List.map([Color.Red], { case Color.Red => "r" }), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing Color.Blue"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_typed_lambda_param_mismatch() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([1, 2], (n: String) => n), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("lambda param type mismatch")
                && err.contains("Int")
                && err.contains("String"),
            "{err}"
        );
    }

    #[test]
    fn rejects_typed_lambda_on_user_fun_mismatch() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] =
  IO.println(apply((x: String) => x, 1))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("lambda param type mismatch"), "{err}");
    }

    #[test]
    fn rejects_nonexhaustive_match() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red => IO.println("r")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing Color.Blue"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_match_with_wildcard() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red => IO.println("r")
    case _ => IO.println("other")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("wildcard should make match exhaustive");
    }

    #[test]
    fn typechecks_match_guard() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case Opt.Some(n) if n > 0 => IO.println("pos")
    case Opt.Some(n) => IO.println("nonpos")
    case Opt.None => IO.println("none")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("unguarded cases still cover Some and None");
    }

    #[test]
    fn typechecks_bare_ctor_patterns() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
enum Color:
  case Red
  case Blue
def describe(o: Opt[Int]): String =
  o match {
    case Some(n) => Str.fromInt(n)
    case None => "n"
  }
def hue(c: Color): String =
  c match {
    case Red | Blue => "p"
  }
@main def main: IO[Unit] =
  IO.println(describe(Some(1)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("bare Some/None/Red patterns");
        let p = crate::typ::elaborate_generics(p).expect("elaborate bare patterns");
        crate::typ::monomorphize(p).expect("mono bare patterns");
    }

    #[test]
    fn typechecks_bare_ctor_from_expected() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def noneInt(): Opt[Int] = None
def someN(n: Int): Opt[Int] = Some(n)
def pick(ok: Bool): Opt[Int] =
  if (ok) Some(1) else None
def wrap(o: Opt[Int]): Opt[Int] = o
@main def main: IO[Unit] =
  for {
    a = None: Opt[Int]
    b = [Some(1), None]: List[Opt[Int]]
    c = wrap(Some(3))
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("bare constructors from expected type");
        let p = crate::typ::elaborate_generics(p).expect("elaborate bare ctor");
        crate::typ::monomorphize(p).expect("mono bare ctor");
    }

    #[test]
    fn rejects_bare_none_as_nonexhaustive() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("non-exhaustive match"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_lowercase_bind_as_ctor() {
        let src = r#"
enum Color:
  case Red
  case Blue
def hue(c: Color): String =
  c match {
    case red => "r"
  }
@main def main: IO[Unit] =
  IO.println(hue(Color.Red))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("lowercase red stays a catch-all bind");
    }

    #[test]
    fn rejects_non_bool_match_guard() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red if 1 => IO.println("r")
    case Color.Blue => IO.println("b")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("match guard must be Bool"), "{err}");
    }

    #[test]
    fn rejects_guarded_only_match_as_nonexhaustive() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red if true => IO.println("r")
    case Color.Blue => IO.println("b")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing Color.Red"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_unreachable_arm_after_unguarded_wildcard() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case _ => IO.println("all")
    case Color.Red if true => IO.println("r")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("unreachable match arm"), "{err}");
    }

    #[test]
    fn typechecks_match_guard_kit_bool() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red if List.nonEmpty([1]) => IO.println("hit")
    case Color.Red => IO.println("miss")
    case Color.Blue => IO.println("blue")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("kit Bool guard typechecks");
    }

    #[test]
    fn typechecks_int_literal_match() {
        let src = r#"
@main def main: IO[Unit] =
  0 match {
    case 0 => IO.println("z")
    case _ => IO.println("o")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("int literal plus wildcard is exhaustive");
    }

    #[test]
    fn typechecks_bool_literal_match() {
        let src = r#"
@main def main: IO[Unit] =
  true match {
    case true => IO.println("t")
    case false => IO.println("f")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("true and false cover Bool");
    }

    #[test]
    fn typechecks_unary_and_bitwise() {
        let src = r#"
def bits(n: Int, b: Bool): Int =
  if (!b) -n else (0xFF & n) | 1 << 2 ^ ~0b1
@main def main: IO[Unit] = IO.println(Str.fromInt(bits(3, false)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("unary and bitwise typecheck");
    }

    #[test]
    fn typechecks_separated_scientific_and_triple_string() {
        let src = r#"
def n(): Int = 1_000 + 0xFF_00
def x(): Float = 1.5e1
@main def main: IO[Unit] =
  for {
    note = """a
b"""
    _ <- IO.println(Str.fromInt(n() + Float.toInt(x())))
    _ <- IO.println(note)
    _ <- IO.println(s"""k:${n()}""")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("separated, scientific, and triple strings typecheck");
    }

    #[test]
    fn typechecks_named_args_reorder() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(add(m = 2, n = 1) + Point(y = 4, x = 3).x))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("named args typecheck");
    }

    #[test]
    fn resolves_named_record_ctor_before_lower() {
        let src = r#"
record Point(x: Int where x >= 0, y: Int where y == y)
def sum(p: Point): Int = p.x + p.y
@main def main: IO[Unit] =
  IO.println(Str.fromInt(sum(Point(y = 5, x = 3))))
"#;
        let p = resolve_named_args(parse(src).unwrap()).expect("named record ctor before lower");
        let mut p = p;
        crate::overlay::residualize_refinements(&mut p);
        let p = lower_program(p);
        typecheck(&p).expect("verify graph named record ctor");
    }

    #[test]
    fn typechecks_default_args() {
        let src = r#"
def add(n: Int, m: Int = 1): Int = n + m
def greet(name: String, punct: String = "!"): String = Str.concat(name, punct)
@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(add(3)))
    _ <- IO.println(Str.fromInt(add(n = 3)))
    _ <- IO.println(Str.fromInt(add(3, 4)))
    _ <- IO.println(greet("hi"))
    _ <- IO.println(greet("hi", "?"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("default args typecheck");
    }

    #[test]
    fn typechecks_default_with_where() {
        let src = r#"
def note(n: Int where n >= 0 = 0): Int = n
@main def main: IO[Unit] = IO.println(Str.fromInt(note()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("where + default typecheck");
    }

    #[test]
    fn rejects_default_type_mismatch() {
        let src = r#"
def add(n: Int, m: Int = "x"): Int = n + m
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("default for `m`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_default_that_uses_param() {
        let src = r#"
def add(n: Int, m: Int = n): Int = n + m
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("must not use parameter"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_omitted_required_before_default() {
        let src = r#"
def add(n: Int, m: Int = 1): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add()))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expects 2 args") || err.message().contains("missing argument"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_ascription_on_nullary_generic() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    x = Opt.None: Opt[Int]
  } yield x match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("ascribe Opt.None typecheck");
        let p = elaborate_generics(p).expect("ascribe Opt.None elaborate");
        monomorphize(p).expect("ascribe Opt.None mono");
    }

    #[test]
    fn typechecks_ascription_empty_list() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    xs = []: List[Int]
  } yield IO.println(Str.fromInt(List.len(xs)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("ascribe empty list typecheck");
    }

    #[test]
    fn typechecks_ascription_infix() {
        let src = r#"
@main def main: IO[Unit] = IO.println(Str.fromInt(1 + 2: Int))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("ascribe infix typecheck");
    }

    #[test]
    fn rejects_ascription_mismatch() {
        let src = r#"
@main def main: IO[Unit] = IO.println(1: String)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected") && err.message().contains("String"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_unknown_ascribe_type() {
        let src = r#"
@main def main: IO[Unit] = IO.println(1: Nope)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("unknown enum Nope"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_record_copy_named_and_positional() {
        let src = r#"
record Point(x: Int, y: Int)
def sum(p: Point): Int = p.x + p.y
@main def main: IO[Unit] =
  IO.println(Str.fromInt(sum(Point(3, 5).copy(y = 9))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("record copy named typecheck");
        let p = resolve_field_access(p).expect("copy lower");
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Call { args, .. } => {
                        assert!(
                            matches!(&args[0].kind, ExprKind::Match { .. }),
                            "copy lowers to match, got {:?}",
                            args[0].kind
                        );
                    }
                    other => panic!("expected sum call arg, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
        let src = r#"
record Point(x: Int, y: Int)
def sum(p: Point): Int = p.x + p.y
@main def main: IO[Unit] =
  IO.println(Str.fromInt(sum(Point(3, 5).copy(1))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("record copy positional typecheck");
    }

    #[test]
    fn typechecks_generic_record_copy() {
        let src = r#"
record Box[T](x: T):
  def get(): T =
    self.x
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Box(4).copy(x = 5).get()))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("generic record copy typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("copy after mono");
    }

    #[test]
    fn typechecks_record_copy_in_interp_with_where() {
        let src = r#"
record Point(x: Int where x >= 0, y: Int where y == y)
def sum(p: Point): Int = p.x + p.y
@main def main: IO[Unit] =
  IO.println(s"copy:${sum(Point(3, 5).copy(y = 9))}")
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("copy in interpolation typecheck");
        let mut p = parse(src).unwrap();
        crate::overlay::residualize_refinements(&mut p);
        let p = lower_program(p);
        match typecheck(&p) {
            Ok(()) => {}
            Err(e) => panic!("copy after residualize typecheck: {}", e.message()),
        }
    }

    #[test]
    fn copy_lower_residualizes_where() {
        let src = r#"
record Point(x: Int where x >= 0, y: Int)
def origin(): Point = Point(3, 5)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(origin().copy(x = -1).x))
"#;
        let mut p = parse(src).unwrap();
        crate::overlay::residualize_refinements(&mut p);
        let p = lower_program(p);
        let p = resolve_named_args(p).expect("named args");
        let p = resolve_field_access(p).expect("copy lower");
        let dumped = format!("{:?}", p.main.body.kind);
        assert!(
            dumped.contains("Property.check"),
            "copy-lower should wrap where: {dumped}"
        );
    }

    #[test]
    fn typechecks_record_copy_identity() {
        let src = r#"
record Point(x: Int, y: Int)
def sum(p: Point): Int = p.x + p.y
@main def main: IO[Unit] =
  IO.println(Str.fromInt(sum(Point(3, 5).copy())))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("empty copy typecheck");
    }

    #[test]
    fn rejects_copy_unknown_field() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Point(3, 5).copy(z = 1).x))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("has no field `z`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_copy_on_enum() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red.copy() match {
    case Color.Red => IO.println("r")
    case Color.Blue => IO.println("b")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("record type") || err.message().contains(".copy"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_copy_field_type_mismatch() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Point(3, 5).copy(y = "no").x))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected") || err.message().contains("String"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_copy_duplicate_field() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Point(3, 5).copy(x = 1, x = 2).x))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("more than once") || err.message().contains("already given"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_unknown_named_arg() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(n = 1, z = 2)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("has no argument `z`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_positional_after_named() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(n = 1, 2)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("positional argument follows named"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_missing_named_arg() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(n = 1)))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("missing argument `m`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_not_on_int() {
        let src = r#"
def bad(n: Int): Bool = !n
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("unary `!` needs Bool"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_bitwise_on_float() {
        let src = r#"
def bad(x: Float): Float = x & 1.0
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("bitwise ops need Int"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_name_bind_covers_int() {
        let src = r#"
@main def main: IO[Unit] =
  1 match {
    case 0 => IO.println("z")
    case n => IO.println(Str.fromInt(n))
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("name bind covers remaining Int");
    }

    #[test]
    fn typechecks_nested_int_literal_match() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0) => IO.println("z")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested int literal plus wildcard payload is exhaustive");
    }

    #[test]
    fn typechecks_list_nil_and_cons() {
        let src = r#"
def describe(xs: List[String]): String =
  xs match {
    case [] => "empty"
    case x :: xs => x
  }
@main def main: IO[Unit] = IO.println(describe(["a"]))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("[] and cons cover List");
    }

    #[test]
    fn typechecks_list_literal_pattern() {
        let src = r#"
def isPair(xs: List[String]): String =
  xs match {
    case ["a", "b"] => "ab"
    case _ => "no"
  }
@main def main: IO[Unit] = IO.println(isPair(["a", "b"]))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("list lit plus wildcard covers List");
    }

    #[test]
    fn typechecks_tuple_construct_project_match_and_both() {
        let src = r#"
def swap[A, B](p: (A, B)): (B, A) =
  p match {
    case (a, b) => (b, a)
  }
def take(p: (Int, String)): String = p._2
@main def main: IO[Unit] =
  for {
    p = (42, "ok")
    n = p._1
    s = p._2
    q = swap(p)
    both <- IO.both(IO.pure(1), IO.pure("x"))
    _ <- IO.println(Str.fromInt(n))
    _ <- IO.println(s)
    _ <- IO.println(q._1)
    _ <- IO.println(take(both))
    _ <- IO.println(if (p == (42, "ok") && (1, "x") != (2, "x")) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("tuple surface");
        let p = resolve_field_access(p).expect("tuple fields");
        assert!(
            matches!(&p.defs[0].body.kind, ExprKind::Match { .. }),
            "swap matches a tuple"
        );
    }

    #[test]
    fn typechecks_tuple_for_binder_and_lambda() {
        let src = r#"
def firsts(xs: List[(Int, String)]): List[Int] =
  List.map(xs, (n, _) => n)
@main def main: IO[Unit] =
  for {
    (n, s) = (42, "ok")
    (a, (b, c)) = (1, (2, "x"))
    (d, e) <- IO.both(IO.pure(1), IO.pure("x"))
    _ <- IO.println(Str.fromInt(n))
    _ <- IO.println(s)
    _ <- IO.println(Str.fromInt(a + b))
    _ <- IO.println(c)
    _ <- IO.println(Str.fromInt(d))
    _ <- IO.println(e)
    _ <- IO.println(Str.fromInt(List.head(firsts([(2, "z")]))))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("tuple unpack in for and lambda");
        let p = crate::typ::elaborate_generics(p).expect("elaborate tuple unpack");
        let p = crate::typ::resolve_field_access(p).expect("fields");
        crate::typ::monomorphize(p).expect("mono tuple unpack");
    }

    #[test]
    fn typechecks_three_slot_tuple() {
        let src = r#"
def rot(t: (Int, String, Bool)): (String, Bool, Int) =
  t match {
    case (a, b, c) => (b, c, a)
  }
def mid(t: (Int, String, Bool)): String = t._2
@main def main: IO[Unit] =
  for {
    (n, s, ok) = (1, "x", true)
    r = rot((n, s, ok))
    _ <- IO.println(Str.fromInt(n))
    _ <- IO.println(s)
    _ <- IO.println(if (ok) "y" else "n")
    _ <- IO.println(mid((n, s, ok)))
    _ <- IO.println(Str.fromInt(r._3))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("3-slot tuple");
        let p = crate::typ::elaborate_generics(p).expect("elaborate 3-slot");
        let p = crate::typ::resolve_field_access(p).expect("fields");
        crate::typ::monomorphize(p).expect("mono 3-slot");
    }

    #[test]
    fn rejects_arity_mismatch_tuple_pattern() {
        let src = r#"
@main def main: IO[Unit] =
  (1, "x") match {
    case (a, b, c) => IO.println(a)
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("tuple pattern has 3 slots") || err.contains("scrutinee has 2"),
            "{err}"
        );
    }

    #[test]
    fn typechecks_ctor_for_binder_and_lambda() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
record Point(x: Int, y: Int)
def xs(): List[Opt[Int]] = [Opt.Some(1), Opt.Some(2)]
def heads(ys: List[Opt[Int]]): List[Int] =
  List.map(ys, (Opt.Some(n)) => n)
@main def main: IO[Unit] =
  for {
    Point(x, y) = Point(3, 5)
    Opt.Some(n) = Opt.Some(7)
    h :: _t = [8, 9]
    got <- IO.pure(Opt.Some(4))
    Opt.Some(m) = got
    _ <- IO.println(Str.fromInt(x + y + n + h + m))
    _ <- IO.println(Str.fromInt(List.head(heads(xs()))))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("ctor unpack in for and lambda");
        let p = crate::typ::elaborate_generics(p).expect("elaborate ctor unpack");
        let p = crate::typ::resolve_field_access(p).expect("fields");
        crate::typ::monomorphize(p).expect("mono ctor unpack");
    }

    #[test]
    fn rejects_ctor_for_binder_on_int() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    Opt.Some(n) = 1
  } yield IO.println(Str.fromInt(n))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("Opt") || err.contains("mismatch") || err.contains("Int"),
            "{err}"
        );
    }

    #[test]
    fn rejects_tuple_for_binder_on_int() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    (a, b) = 1
  } yield IO.println("no")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("tuple") || err.contains("mismatch") || err.contains("Int"),
            "{err}"
        );
    }

    #[test]
    fn rejects_unknown_tuple_field() {
        let src = r#"
@main def main: IO[Unit] = IO.println((1, "x")._3)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("tuple has fields _1 through _2"), "{err}");
    }

    #[test]
    fn rejects_tuple_pattern_on_int() {
        let src = r#"
@main def main: IO[Unit] =
  1 match {
    case (a, b) => IO.println("no")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("tuple pattern does not match"), "{err}");
    }

    #[test]
    fn rejects_io_race_payload_mismatch() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    _ <- IO.race(IO.pure(1), IO.pure("x"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("IO.race arms disagree"), "{err}");
    }

    #[test]
    fn typechecks_named_field_pattern_omitted_is_wildcard() {
        let src = r#"
record Point(x: Int, y: Int)
def originX(p: Point): Int =
  p match {
    case Point(x = n) => n
  }
@main def main: IO[Unit] = IO.println(Str.fromInt(originX(Point(3, 5))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("named field plus omitted y covers Point");
    }

    #[test]
    fn typechecks_named_enum_payload() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
def get(o: Opt): Int =
  o match {
    case Opt.Some(x = n) => n
    case Opt.None => 0
  }
@main def main: IO[Unit] = IO.println(Str.fromInt(get(Opt.Some(1))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("named Some payload plus None covers Opt");
    }

    #[test]
    fn typechecks_cons_expr() {
        let src = r#"
def nest(): List[String] =
  "a" :: "b" :: List.empty()
@main def main: IO[Unit] = IO.println(List.join(nest(), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect(":: cons expression typechecks");
    }

    #[test]
    fn rejects_unknown_named_field() {
        let src = r#"
record Point(x: Int, y: Int)
def bad(p: Point): Int =
  p match {
    case Point(z = n) => n
  }
@main def main: IO[Unit] = IO.println(Str.fromInt(bad(Point(1, 2))))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("has no field `z`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_positional_after_named_field() {
        let src = r#"
record Point(x: Int, y: Int)
def bad(p: Point): Int =
  p match {
    case Point(x = n, m) => n
  }
@main def main: IO[Unit] = IO.println(Str.fromInt(bad(Point(1, 2))))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("positional pattern follows named"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_duplicate_named_field() {
        let src = r#"
record Point(x: Int, y: Int)
def bad(p: Point): Int =
  p match {
    case Point(x = a, x = b) => a
  }
@main def main: IO[Unit] = IO.println(Str.fromInt(bad(Point(1, 2))))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("duplicate field `x`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_match_missing_cons() {
        let src = r#"
def describe(xs: List[String]): String =
  xs match {
    case [] => "empty"
  }
@main def main: IO[Unit] = IO.println(describe([]))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing _ :: _"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_match_missing_nil() {
        let src = r#"
def describe(xs: List[String]): String =
  xs match {
    case _ :: _ => "n"
  }
@main def main: IO[Unit] = IO.println(describe([]))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("non-exhaustive match: missing []"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_pattern_on_int() {
        let src = r#"
@main def main: IO[Unit] =
  0 match {
    case [] => IO.println("e")
    case _ => IO.println("o")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(
            err.contains("List") || err.contains("expected List"),
            "{err}"
        );
    }

    #[test]
    fn typechecks_or_pattern_covers_enum() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("or-pattern should cover both cases");
    }

    #[test]
    fn typechecks_or_pattern_covers_bool() {
        let src = r#"
@main def main: IO[Unit] =
  true match {
    case true | false => IO.println("b")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("true | false covers Bool");
    }

    #[test]
    fn typechecks_nested_or_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0 | 1) => IO.println("s")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested or plus wildcard payload is exhaustive");
    }

    #[test]
    fn typechecks_or_pattern_shared_bind() {
        let src = r#"
enum Either:
  case Left(x: Int)
  case Right(y: Int)
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Either.Left(1) match {
    case Either.Left(n) | Either.Right(n) => n
  }))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("shared bind across or-alternatives");
    }

    #[test]
    fn typechecks_or_pattern_with_guard() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red | Color.Blue if false => IO.println("skip")
    case Color.Red | Color.Blue => IO.println("hit")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("unguarded or-pattern still covers");
    }

    #[test]
    fn rejects_or_pattern_int_without_wildcard() {
        let src = r#"
@main def main: IO[Unit] =
  0 match {
    case 0 | 1 => IO.println("s")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("non-exhaustive match: missing _"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_or_pattern_bind_missing_on_one_alt() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Opt.Some(1) match {
    case Opt.Some(n) | Opt.None => n
  }))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("unknown") || err.contains("n"), "{err}");
    }

    #[test]
    fn typechecks_as_pattern_binds_scrutinee() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
def keep(o: Opt): Opt =
  o match {
    case s @ Opt.Some(_) => s
    case Opt.None => Opt.None
  }
@main def main: IO[Unit] =
  IO.println(keep(Opt.Some(1)) match {
    case Opt.Some(n) => Str.fromInt(n)
    case Opt.None => "none"
  })
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("as-pattern bind has scrutinee type");
    }

    #[test]
    fn typechecks_as_pattern_covers_or() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case p @ Color.Red | Color.Blue => IO.println(p match {
      case Color.Red => "r"
      case Color.Blue => "b"
    })
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("as wrapping or covers and binds Color");
    }

    #[test]
    fn typechecks_nested_as_literal() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Opt.Some(0) match {
    case Opt.Some(n @ 0) => n
    case Opt.Some(_) => 1
    case Opt.None => 2
  }))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested as-pattern on a literal");
    }

    #[test]
    fn rejects_duplicate_as_bind() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case n @ Opt.Some(n) => IO.println("x")
    case Opt.None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("duplicate pattern binder n"), "{err}");
    }

    #[test]
    fn rejects_int_literal_match_without_wildcard() {
        let src = r#"
@main def main: IO[Unit] =
  0 match {
    case 0 => IO.println("z")
    case 1 => IO.println("o")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("non-exhaustive match: missing _"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_bool_literal_match_missing_false() {
        let src = r#"
@main def main: IO[Unit] =
  true match {
    case true => IO.println("t")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing false"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_string_literal_on_int() {
        let src = r#"
@main def main: IO[Unit] =
  0 match {
    case "x" => IO.println("s")
    case _ => IO.println("o")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("string literal pattern does not match scrutinee"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_nested_int_literal_without_payload_wildcard() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0) => IO.println("z")
    case Opt.None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("non-exhaustive match: missing")
                && err.message().contains("Opt.Some"),
            "{}",
            err.message()
        );
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
    fn typechecks_resource_use_payload() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    got <- Resource.use(res, t => IO.pure(t))
    _ <- IO.println(got)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Resource.use should yield IO[T]");
    }

    #[test]
    fn rejects_resource_use_non_lambda() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, "nope")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Resource.use callback must be a lambda"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_resource_make_non_unit_release() {
        let src = r#"@main def main: IO[Unit] =
  Resource.make(IO.pure("tok"), t => IO.pure("x"))
"#;
        let p = lower_program(parse(src).unwrap());
        assert!(
            typecheck(&p).is_err(),
            "Resource.make release must be IO[Unit]"
        );
    }

    #[test]
    fn rejects_resource_make_non_lambda() {
        let src = r#"@main def main: IO[Unit] =
  Resource.make(IO.pure("tok"), "nope")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Resource.make callback must be a lambda"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_io_timeout() {
        let src = r#"@main def main: IO[Unit] =
  for {
    got <- IO.timeout(50, IO.pure("ok"))
    _ <- IO.println(got)
    _ <- IO.timeout(1, IO.sleep(100)).handleErrorWith(_ => IO.println("timed-out"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("IO.timeout should typecheck");
    }

    #[test]
    fn typechecks_fiber_fork_join_interrupt() {
        let src = r#"@main def main: IO[Unit] =
  for {
    f <- Fiber.fork(IO.pure("ok"))
    v <- Fiber.join(f)
    _ <- IO.println(v)
    g <- Fiber.fork(IO.sleep(50))
    _ <- Fiber.interrupt(g)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fiber.fork/join/interrupt should typecheck");
    }

    #[test]
    fn typechecks_io_forever_repeat_retry() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n <- IO.repeatN(2, IO.pure("repeat-ok"))
    _ <- IO.println(n)
    t <- IO.retryN(1, IO.pure("retry-ok"))
    _ <- IO.println(t)
    h <- Fiber.fork(IO.forever(IO.sleep(50)))
    _ <- Fiber.interrupt(h)
    _ <- Fiber.join(h).handleErrorWith(_ => IO.println("forever-stopped"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("IO.forever/repeatN/retryN should typecheck");
    }

    #[test]
    fn typechecks_io_foreach_when() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- IO.foreach(["a", "b"], x => IO.pure(Str.concat(x, "!")))
    _ <- IO.println(List.join(xs, ","))
    ns <- IO.foreach([1, 2], n => IO.pure(n + 1))
    _ <- IO.println(Str.fromInt(List.head(ns)))
    _ <- IO.foreachDiscard(["a"], x => IO.println(x))
    _ <- IO.when(true, IO.println("y"))
    _ <- IO.unless(false, IO.println("n"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("IO.foreach/when should typecheck");
    }

    #[test]
    fn typechecks_generic_ref_queue_deferred() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of(1)
    _ <- Ref.set(r, 2)
    n <- Ref.get(r)
    _ <- Ref.update(r, x => x + 1)
    m <- Ref.updateAndGet(r, x => x + 1)
    _ <- IO.println(Str.fromInt(n + m))
    s <- Ref.of("a")
    _ <- Ref.update(s, t => Str.concat(t, "!"))
    q <- Queue.unbounded(): IO[Queue[Int]]
    _ <- Queue.offer(q, 3)
    k <- Queue.take(q)
    _ <- IO.println(Str.fromInt(k))
    d <- Deferred.empty(): IO[Deferred[Int]]
    _ <- Deferred.complete(d, 4)
    g <- Deferred.get(d)
    f <- Fiber.fork(IO.pure(5))
    j <- Fiber.join(f)
    _ <- IO.println(Str.fromInt(g + j))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("generic Ref/Queue/Deferred/Fiber should typecheck");
        let p = elaborate_generics(p).expect("generic cells keep ascribe pin");
        monomorphize(p).expect("generic cells monomorphize");
    }

    #[test]
    fn typechecks_generic_stream_resource() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure(3), n => IO.println(Str.fromInt(n)))
    got <- Resource.use(res, n => IO.pure(n + 1))
    _ <- IO.println(Str.fromInt(got))
    ns <- Stream.compileToList(Stream.map(Stream.emits([1, 2]), n => n + 1))
    _ <- IO.println(Str.fromInt(List.head(ns)))
    fs <- Stream.compileToList(Stream.filter(Stream.emit(4), n => n > 0))
    _ <- IO.println(Str.fromInt(List.head(fs)))
    es <- Stream.compileToList(Stream.evalMap(Stream.eval(IO.pure(5)), n => IO.pure(n + 1)))
    _ <- IO.println(Str.fromInt(List.head(es)))
    cs <- Stream.compileToList(Stream.concat(Stream.take(Stream.emit(6), 1), Stream.drop(Stream.emit(7), 0)))
    _ <- IO.println(Str.fromInt(List.head(cs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("generic Stream/Resource should typecheck");
    }

    #[test]
    fn rejects_stream_concat_payload_mismatch() {
        let src = r#"@main def main: IO[Unit] =
  Stream.drain(Stream.concat(Stream.emit(1), Stream.emit("a")))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Stream type mismatch"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_queue_int_needs_pin() {
        let src = r#"@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- Queue.offer(q, 1)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected") || err.message().contains("String"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_ref_update_rejects_non_lambda() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of(1)
    _ <- Ref.update(r, 1)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Ref.update callback must be a lambda"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_io_foreach_rejects_non_lambda() {
        let src = r#"@main def main: IO[Unit] =
  IO.foreach(["a"], "nope")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("IO.foreach callback must be a lambda"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn typechecks_io_when_needs_io_unit() {
        let src = r#"@main def main: IO[Unit] =
  IO.when(true, IO.pure("x"))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Io(Unit)") || err.message().contains("Io(String)"),
            "{}",
            err.message()
        );
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
    fn typechecks_list_filter() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.filter(["a", "b", "a"], x => x != "b")
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.filter should typecheck");
    }

    #[test]
    fn rejects_list_filter_non_bool() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.filter(["a"], x => x)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.filter lambda must return Bool"),
            "expected Bool body, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_list_map() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.map(["a", "b"], x => Str.concat(x, "!"))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.map should typecheck");
    }

    #[test]
    fn typechecks_list_map_int_body() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.map(["a"], x => 1)
    _ <- IO.println(Str.fromInt(List.len(xs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.map Int body returns List[Int]");
    }

    #[test]
    fn typechecks_list_map_view_body() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ = List.map(["a"], x => View.text(x))
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.map may return List[View]");
    }

    #[test]
    fn typechecks_list_set_at() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.setAt(["a", "b"], 0, "c")
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.setAt should typecheck");
    }

    #[test]
    fn typechecks_list_take_drop_find_exists() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    t = List.take(xs, 2)
    d = List.drop(xs, 1)
    f = List.find(xs, x => x == "b")
    hit = List.exists(xs, x => x == "c")
    _ <- IO.println(List.join(t, ","))
    _ <- IO.println(List.join(d, ","))
    _ <- IO.println(List.join(f, ","))
    _ <- IO.println(if (hit) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.take/drop/find/exists should typecheck");
    }

    #[test]
    fn typechecks_list_take_right_drop_right_init_last() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    t = List.takeRight(xs, 2)
    d = List.dropRight(xs, 1)
    i = List.init(xs)
    last = List.last(xs)
    _ <- IO.println(List.join(t, ","))
    _ <- IO.println(List.join(d, ","))
    _ <- IO.println(List.join(i, ","))
    _ <- IO.println(List.join(last, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.takeRight/dropRight/init/last should typecheck");
    }

    #[test]
    fn typechecks_list_get_or_else_fill_map_is_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    _ <- IO.println(List.getOrElse(xs, 1, "z"))
    _ <- IO.println(List.getOrElse(xs, 9, "z"))
    _ <- IO.println(List.join(List.fill(3, "a"), ","))
    _ <- IO.println(if (Map.isEmpty(Map.empty())) "y" else "n")
    _ <- IO.println(if (Set.isEmpty(Set.empty())) "y" else "n")
    _ <- IO.println(if (Map.nonEmpty(Map.set(Map.empty(), "a", "1"))) "y" else "n")
    _ <- IO.println(if (Set.nonEmpty(Set.add(Set.empty(), "x"))) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.getOrElse/fill and Map/Set.isEmpty should typecheck");
    }

    #[test]
    fn typechecks_list_get_or_else_int() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.getOrElse([1, 2], 0, 9)))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.getOrElse Int list should typecheck");
    }

    #[test]
    fn typechecks_list_take_while_drop_while_forall() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    tw = List.takeWhile(xs, x => x != "b")
    dw = List.dropWhile(xs, x => x != "b")
    all = List.forall(xs, x => x != "z")
    _ <- IO.println(List.join(tw, ","))
    _ <- IO.println(List.join(dw, ","))
    _ <- IO.println(if (all) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.takeWhile/dropWhile/forall should typecheck");
    }

    #[test]
    fn typechecks_list_split_at_span_partition() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    _ <- IO.println(List.join(List.map(List.splitAt(xs, 1), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.span(xs, x => x != "c"), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.partition(xs, x => x == "b"), g => List.join(g, ",")), "|"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.splitAt/span/partition should typecheck");
    }

    #[test]
    fn typechecks_list_count_filter_not_str_reverse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    _ <- IO.println(Str.fromInt(List.count(xs, x => x != "b")))
    _ <- IO.println(List.join(List.filterNot(xs, x => x == "b"), ","))
    _ <- IO.println(Str.reverse("abc"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.count/filterNot and Str.reverse should typecheck");
    }

    #[test]
    fn typechecks_list_flat_map_pad_to_non_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b"]
    _ <- IO.println(List.join(List.flatMap(xs, x => [x, x]), ","))
    _ <- IO.println(List.join(List.padTo(["a"], 3, "z"), ","))
    _ <- IO.println(if (List.nonEmpty(xs)) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.flatMap/padTo/nonEmpty should typecheck");
    }

    #[test]
    fn typechecks_list_range_tabulate_intersperse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.map(List.range(1, 4), n => Str.fromInt(n)), ","))
    _ <- IO.println(List.join(List.tabulate(3, i => Str.fromInt(i)), ","))
    _ <- IO.println(List.join(List.intersperse(["a", "b", "c"], "|"), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.range/tabulate/intersperse should typecheck");
    }

    #[test]
    fn typechecks_list_grouped_sliding_non_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    _ <- IO.println(List.join(List.map(List.grouped(xs, 2), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.sliding(xs, 2), g => List.join(g, ",")), "|"))
    _ <- IO.println(if (Str.nonEmpty("a")) "y" else "n")
    _ <- IO.println(if (Map.nonEmpty(Map.set(Map.empty(), "a", "1"))) "y" else "n")
    _ <- IO.println(if (Set.nonEmpty(Set.add(Set.empty(), "x"))) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.grouped/sliding and nonEmpty should typecheck");
    }

    #[test]
    fn typechecks_list_slice_index_where() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    _ <- IO.println(List.join(List.slice(xs, 1, 3), ","))
    _ <- IO.println(Str.fromInt(List.indexWhere(xs, x => x == "b")))
    _ <- IO.println(Str.fromInt(List.lastIndexWhere(xs, x => x != "z")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.slice/indexWhere/lastIndexWhere should typecheck");
    }

    #[test]
    fn typechecks_map_get_str_capitalize_list_indices() {
        let src = r#"@main def main: IO[Unit] =
  for {
    m = Map.set(Map.empty(), "a", "1")
    xs = ["a", "b", "c"]
    _ <- IO.println(List.join(Map.get(m, "a"), ","))
    _ <- IO.println(List.join(Map.get(m, "z"), ","))
    _ <- IO.println(Str.capitalize("hello"))
    _ <- IO.println(List.join(List.map(List.indices(xs), n => Str.fromInt(n)), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Map.get/Str.capitalize/List.indices should typecheck");
    }

    #[test]
    fn rejects_list_flat_map_non_list() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.flatMap(["a"], x => x), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.flatMap lambda must return List[_]"),
            "expected List body, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_take_while_non_bool() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.takeWhile(["a"], x => x)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.takeWhile lambda must return Bool"),
            "expected Bool body, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_find_non_bool() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.find(["a"], x => x)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("List.find lambda must return Bool"),
            "expected Bool body, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_map_and_set() {
        let src = r#"@main def main: IO[Unit] =
  for {
    m = Map.set(Map.set(Map.empty(), "a", "1"), "b", "2")
    s = Set.add(Set.empty(), "x")
    _ <- IO.println(Map.getOrElse(m, "a", "?"))
    _ <- IO.println(if (Map.contains(m, "b") && Set.contains(s, "x")) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Map/Set should typecheck");
    }

    #[test]
    fn typechecks_map_remove_keys_size() {
        let src = r#"@main def main: IO[Unit] =
  for {
    m = Map.set(Map.set(Map.empty(), "a", "1"), "b", "2")
    s = Set.add(Set.add(Set.empty(), "x"), "y")
    gone = Map.remove(m, "b")
    dropped = Set.remove(s, "x")
    _ <- IO.println(List.join(Map.keys(m), ","))
    _ <- IO.println(List.join(Map.values(m), ","))
    _ <- IO.println(s"${Map.size(m)}")
    _ <- IO.println(if (Map.contains(gone, "b")) "y" else "n")
    _ <- IO.println(List.join(Set.toList(s), ","))
    _ <- IO.println(s"${Set.size(s)}")
    _ <- IO.println(if (Set.contains(dropped, "x")) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Map.remove/keys/size should typecheck");
    }

    #[test]
    fn typechecks_set_union_intersect_diff() {
        let src = r#"@main def main: IO[Unit] =
  for {
    a = Set.add(Set.add(Set.empty(), "x"), "y")
    b = Set.add(Set.add(Set.empty(), "y"), "z")
    _ <- IO.println(List.join(Set.toList(Set.union(a, b)), ","))
    _ <- IO.println(List.join(Set.toList(Set.intersect(a, b)), ","))
    _ <- IO.println(List.join(Set.toList(Set.diff(a, b)), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Set.union/intersect/diff should typecheck");
    }

    #[test]
    fn typechecks_list_inits_tails_set_subset() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b"]
    a = Set.add(Set.empty(), "x")
    b = Set.add(Set.add(Set.empty(), "x"), "y")
    _ <- IO.println(List.join(List.map(List.inits(xs), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.tails(xs), g => List.join(g, ",")), "|"))
    _ <- IO.println(if (Set.isSubset(a, b)) "y" else "n")
    _ <- IO.println(if (Set.isDisjoint(a, Set.add(Set.empty(), "z"))) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.inits/tails and Set.isSubset/isDisjoint should typecheck");
    }

    #[test]
    fn typechecks_list_zip_unzip_transpose() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "c"]
    (as, bs) = List.unzip(List.zip(xs, ["1", "2"]))
    _ <- IO.println(List.join(List.map(List.zip(xs, ["1", "2"]), (a, b) => s"$a,$b"), "|"))
    _ <- IO.println(List.join(List.map(List.zipAll(xs, ["1"], "z", "9"), (a, b) => s"$a,$b"), "|"))
    _ <- IO.println(List.join([List.join(as, ","), List.join(bs, ",")], "|"))
    _ <- IO.println(List.join(List.map(List.zip([1, 2], ["a", "b"]), (n, s) => s"${Str.fromInt(n)},$s"), "|"))
    _ <- IO.println(List.join(List.map(List.transpose([["a", "b"], ["c", "d"]]), g => List.join(g, ",")), "|"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.zip/zipAll/unzip/transpose should typecheck");
    }

    #[test]
    fn typechecks_list_zip_with_index_and_fold() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = []: List[Int]
    _ <- IO.println(List.join(List.map(List.zipWithIndex(["a", "b"]), (i, s) => s"${Str.fromInt(i)},$s"), "|"))
    _ <- IO.println(Str.fromInt(List.foldLeft([1, 2, 3], 0, (acc, n) => acc + n)))
    _ <- IO.println(List.foldLeft(["a", "b"], "!", (acc, x) => Str.concat(acc, x)))
    _ <- IO.println(List.foldRight(["a", "b"], "!", (x, acc) => Str.concat(x, acc)))
    _ <- IO.println(Str.fromInt(List.foldLeft(xs, 7, (acc, n) => acc + n)))
    _ <- IO.println(Str.fromInt(List.foldLeft([]: List[Int], 7, (acc, n) => acc + n)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.zipWithIndex/foldLeft/foldRight should typecheck");
    }

    #[test]
    fn typechecks_list_scan_and_reduce() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = []: List[Int]
    _ <- IO.println(List.join(List.map(List.scanLeft([1, 2, 3], 0, (acc, n) => acc + n), Str.fromInt(_)), ","))
    _ <- IO.println(List.join(List.map(List.scanRight([1, 2, 3], 0, (n, acc) => n + acc), Str.fromInt(_)), ","))
    _ <- IO.println(List.join(List.scanLeft(["a", "b"], "!", (acc, x) => Str.concat(acc, x)), "|"))
    _ <- IO.println(List.join(List.scanRight(["a", "b"], "!", (x, acc) => Str.concat(x, acc)), "|"))
    _ <- IO.println(List.join(List.map(List.scanLeft([]: List[Int], 7, (acc, n) => acc + n), Str.fromInt(_)), ","))
    _ <- IO.println(List.join(List.map(List.scanLeft(xs, 7, (acc, n) => acc + n), Str.fromInt(_)), ","))
    _ <- IO.println(Str.fromInt(List.reduceLeft([1, 2, 3], (acc, n) => acc + n)))
    _ <- IO.println(List.reduceLeft(["a", "b"], (acc, x) => Str.concat(acc, x)))
    _ <- IO.println(List.reduceRight(["a", "b"], (x, acc) => Str.concat(x, acc)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.scanLeft/scanRight/reduceLeft/reduceRight should typecheck");
    }

    #[test]
    fn typechecks_list_contains_distinct_diff() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "a", "c"]
    _ <- IO.println(if (List.contains(xs, "b")) "y" else "n")
    _ <- IO.println(Str.fromInt(List.indexOf(xs, "a")))
    _ <- IO.println(Str.fromInt(List.lastIndexOf(xs, "a")))
    _ <- IO.println(List.join(List.distinct(xs), ","))
    _ <- IO.println(List.join(List.diff(xs, ["a", "z"]), ","))
    _ <- IO.println(List.join(List.intersect(xs, ["a", "z"]), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.contains/indexOf/distinct/diff/intersect should typecheck");
    }

    #[test]
    fn typechecks_list_starts_with_patch_find_last() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "a", "c"]
    _ <- IO.println(if (List.startsWith(xs, ["a", "b"])) "y" else "n")
    _ <- IO.println(if (List.endsWith(xs, ["a", "c"])) "y" else "n")
    _ <- IO.println(if (List.sameElements(xs, ["a", "b", "a", "c"])) "y" else "n")
    _ <- IO.println(List.join(List.patch(xs, 1, ["x", "y"], 2), ","))
    _ <- IO.println(List.join(List.findLast(xs, x => x == "a"), ","))
    _ <- IO.println(Str.fromInt(List.prefixLength(xs, x => x == "a")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p)
            .expect("List.startsWith/endsWith/patch/findLast/prefixLength should typecheck");
    }

    #[test]
    fn typechecks_list_index_of_slice_segment_length() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["a", "b", "a", "b"]
    _ <- IO.println(Str.fromInt(List.indexOfSlice(xs, ["a", "b"])))
    _ <- IO.println(Str.fromInt(List.lastIndexOfSlice(xs, ["a", "b"])))
    _ <- IO.println(Str.fromInt(List.segmentLength(xs, x => x == "a", 0)))
    _ <- IO.println(if (List.isDefinedAt(xs, 3)) "y" else "n")
    _ <- IO.println(Str.fromInt(List.lengthCompare(xs, 4)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p)
            .expect("List.indexOfSlice/lastIndexOfSlice/segmentLength/isDefinedAt/lengthCompare should typecheck");
    }

    #[test]
    fn typechecks_list_sort_max_min() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = ["c", "a", "b"]
    ns = [3, 1, 2]
    _ <- IO.println(List.join(List.sort(xs), ","))
    _ <- IO.println(List.join(List.map(List.sort(ns), n => Str.fromInt(n)), ","))
    _ <- IO.println(List.join(List.sortBy(["bb", "a", "ccc"], x => Str.len(x)), ","))
    _ <- IO.println(List.max(xs))
    _ <- IO.println(List.min(xs))
    _ <- IO.println(Str.fromInt(List.max(ns)))
    _ <- IO.println(Str.fromInt(List.min(ns)))
    _ <- IO.println(List.maxBy(["bb", "a", "ccc"], x => Str.len(x)))
    _ <- IO.println(List.minBy(["bb", "a", "ccc"], x => Str.len(x)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.sort/max/min/sortBy/maxBy/minBy should typecheck");
    }

    #[test]
    fn rejects_list_sort_bool() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.max([true, false])) "y" else "n")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.max needs List[Int] or List[String]"),
            "expected ordered-elem error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_sort_by_non_int() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.sortBy(["a"], x => x), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("List.sortBy lambda must return Int"),
            "expected Int key, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_list_group_by_sum_product() {
        let src = r#"@main def main: IO[Unit] =
  for {
    g = List.groupBy(["aa", "b", "cc"], x => Str.len(x))
    gs = List.groupBy(["ab", "ac", "b"], x => Str.take(x, 1))
    _ <- IO.println(List.join(List.map(Map.keys(g), n => Str.fromInt(n)), ","))
    _ <- IO.println(List.join(List.map(Map.values(g), row => List.join(row, ":")), "|"))
    _ <- IO.println(List.join(Map.keys(gs), ","))
    _ <- IO.println(Str.fromInt(List.sum([1, 2, 3])))
    _ <- IO.println(Str.fromInt(List.product([2, 3, 4])))
    _ <- IO.println(Str.fromInt(List.sum([]: List[Int])))
    _ <- IO.println(Str.fromInt(List.product([]: List[Int])))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.groupBy/sum/product should typecheck");
    }

    #[test]
    fn typechecks_list_distinct_by_to_map_to_set() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = []: List[String]
    pairs = []: List[(String, Int)]
    emptyM = Map.empty(): Map[String, Int]
    _ <- IO.println(List.join(List.distinctBy(["aa", "b", "cc"], x => Str.len(x)), ","))
    _ <- IO.println(List.join(List.distinctBy(xs, x => x), ","))
    _ <- IO.println(List.join(List.distinctBy([]: List[String], x => x), ","))
    _ <- IO.println(Map.getOrElse(List.toMap([("a", "1"), ("b", "2")]), "a", "?"))
    _ <- IO.println(Str.fromInt(Map.size(List.toMap(pairs))))
    _ <- IO.println(Str.fromInt(Map.size(List.toMap([]: List[(String, Int)]))))
    _ <- IO.println(List.join(Set.toList(List.toSet(["b", "a", "b"])), ","))
    _ <- IO.println(List.join(List.map(Set.toList(List.toSet([1, 2, 1])), Str.fromInt(_)), ","))
    _ <- IO.println(List.join(List.map(Map.toList(Map.set(Map.empty(), "a", "1")), (k, v) => s"$k:$v"), "|"))
    _ <- IO.println(Str.fromInt(List.len(Map.toList(emptyM))))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List.distinctBy/toMap/toSet and Map.toList should typecheck");
    }

    #[test]
    fn rejects_list_distinct_by_bool_key() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.distinctBy(["a"], x => true), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.distinctBy lambda must return Int or String"),
            "expected map-key error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_to_map_non_pair() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Map.size(List.toMap(["a"]))))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("List.toMap needs List[(K, V)]"),
            "expected pair-list error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_to_set_bool() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Set.toList(List.toSet([true])), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.toSet needs List[Int] or List[String]"),
            "expected Int/String list error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_group_by_bool_key() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.keys(List.groupBy(["a"], x => true)), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("List.groupBy lambda must return Int or String"),
            "expected map-key error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_list_sum_string() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.sum(["a"])))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("List.sum needs List[Int]"),
            "expected Int list error, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_map_union_intersect_diff() {
        let src = r#"@main def main: IO[Unit] =
  for {
    a = Map.set(Map.set(Map.empty(), "a", "1"), "b", "2")
    b = Map.set(Map.set(Map.empty(), "b", "9"), "c", "3")
    _ <- IO.println(List.join(Map.values(Map.union(a, b)), ","))
    _ <- IO.println(List.join(Map.values(Map.intersect(a, b)), ","))
    _ <- IO.println(List.join(Map.values(Map.diff(a, b)), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Map.union/intersect/diff should typecheck");
    }

    #[test]
    fn typechecks_map_set_combinators() {
        let src = r#"@main def main: IO[Unit] =
  for {
    a = Map.set(Map.set(Map.empty(), "a", "1"), "b", "2")
    s = Set.add(Set.add(Set.empty(), "x"), "y")
    _ <- IO.println(List.join(Map.values(Map.filter(a, v => v != "2")), ","))
    _ <- IO.println(List.join(Map.values(Map.mapValues(a, v => Str.concat(v, "!"))), ","))
    _ <- IO.println(if (Map.exists(a, v => v == "2")) "y" else "n")
    _ <- IO.println(if (Map.forall(a, v => v != "z")) "y" else "n")
    _ <- IO.println(List.join(Set.toList(Set.filter(s, x => x != "y")), ","))
    _ <- IO.println(List.join(Set.toList(Set.map(s, x => Str.concat(x, "!"))), ","))
    _ <- IO.println(if (Set.exists(s, x => x == "x")) "y" else "n")
    _ <- IO.println(if (Set.forall(s, x => x != "z")) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Map/Set combinators should typecheck");
    }

    #[test]
    fn typechecks_map_filter_lambda_must_be_bool() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.filter(Map.set(Map.empty(), "a", "1"), v => v)), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Map.filter lambda must return Bool"),
            "expected Bool lambda error, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_set_map_lambda_must_be_key() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Set.toList(Set.map(Set.add(Set.empty(), "x"), x => true)), ","))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Set.map lambda must return Int or String"),
            "expected key lambda error, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_str_is_empty_case_repeat() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(if (Str.isEmpty("")) "y" else "n")
    _ <- IO.println(Str.toLower("Ab"))
    _ <- IO.println(Str.toUpper("Ab"))
    _ <- IO.println(Str.repeat("a", 3))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.isEmpty/toLower/toUpper/repeat should typecheck");
    }

    #[test]
    fn typechecks_str_strip_pad_blank() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.stripPrefix("abc", "a"))
    _ <- IO.println(Str.stripSuffix("abc", "c"))
    _ <- IO.println(Str.padLeft("a", 3, "x"))
    _ <- IO.println(Str.padRight("a", 3, "x"))
    _ <- IO.println(if (Str.isBlank(" \t")) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.strip/pad/isBlank should typecheck");
    }

    #[test]
    fn typechecks_str_last_index_take_drop_list_reverse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(Str.lastIndexOf("ababa", "ba")))
    _ <- IO.println(Str.take("abc", 2))
    _ <- IO.println(Str.drop("abc", 1))
    _ <- IO.println(Str.takeRight("abc", 2))
    _ <- IO.println(Str.dropRight("abc", 1))
    _ <- IO.println(List.join(List.reverse(["a", "b", "c"]), ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect(
            "Str.lastIndexOf/take/drop/takeRight/dropRight and List.reverse should typecheck",
        );
    }

    #[test]
    fn typechecks_str_starts_with() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.startsWith("done:milk", "done:")) "yes" else "no")
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.startsWith should typecheck");
    }

    #[test]
    fn typechecks_structural_eq_on_list_adt_map() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  for {
    xs = ["a", "b"]
    mp = Map.set(Map.set(Map.empty(), "a", "1"), "b", "2")
    mp2 = Map.set(Map.set(Map.empty(), "b", "2"), "a", "1")
    _ <- IO.println(if (xs == ["a", "b"] && xs != ["a"]) "y" else "n")
    _ <- IO.println(if (Color.Red == Color.Red && Color.Red != Color.Blue) "y" else "n")
    _ <- IO.println(if (mp == mp2) "y" else "n")
    _ <- IO.println(if ("ab" == "ab") "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("structural == on list/adt/map should typecheck");
    }

    #[test]
    fn rejects_list_eq_int() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (["a"] == 1) "y" else "n")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("comparison type mismatch"),
            "expected mismatch, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_str_contains_ends_toint_replace() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(if (Str.contains("ab", "b") && Str.endsWith("ab", "b")) "y" else "n")
    _ <- IO.println(s"${Str.toInt("7", 0)}")
    _ <- IO.println(Str.replace("a-b", "-", ":"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.contains/endsWith/toInt/replace should typecheck");
    }

    #[test]
    fn typechecks_str_split_list_concat_flatten() {
        let src = r#"@main def main: IO[Unit] =
  for {
    parts = Str.split("a,b", ",")
    ys = List.concat(["a"], ["b", "c"])
    xss = List.append(List.append(List.empty(), ["a"]), ["b", "c"])
    flat = List.flatten(xss)
    _ <- IO.println(List.join(parts, ":"))
    _ <- IO.println(List.join(ys, ","))
    _ <- IO.println(List.join(flat, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.split/List.concat/flatten should typecheck");
    }

    #[test]
    fn typechecks_float_arith_and_convert() {
        let src = r#"
def scale(x: Float): Float = x * 2.0
@main def main: IO[Unit] =
  IO.println(s"${scale(1.5) + Float.fromInt(1)} ${Float.toInt(2.9)}")
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Float arith should typecheck");
    }

    #[test]
    fn rejects_float_mod() {
        let src = r#"@main def main: IO[Unit] = IO.println(s"${1.5 % 2.0}")"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("%"), "{err}");
    }

    #[test]
    fn rejects_mixed_int_float_arith() {
        let src = r#"@main def main: IO[Unit] = IO.println(s"${1 + 1.5}")"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err().to_string();
        assert!(err.contains("Float") || err.contains("Int"), "{err}");
    }

    #[test]
    fn typechecks_str_trim() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.trim("  x  "))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Str.trim should typecheck");
    }

    #[test]
    fn rejects_list_map_fromint_on_string() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.map(["a"], x => Str.fromInt(x))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected Int, got String"),
            "expected String/Int mismatch, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_signal_map() {
        let src = r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => Str.fromInt(n))
    _ <- Ui.run(_ => View.bindText(label))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Signal.map should typecheck");
    }

    #[test]
    fn rejects_signal_map_concat_int() {
        let src = r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => Str.concat("n=", n))
    _ <- Ui.run(_ => View.bindText(label))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected String, got Int"),
            "expected Int/String mismatch, got {}",
            err.message()
        );
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
    _ <- IO.println(if (hit) "1" else "0")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.exists should typecheck");
    }

    #[test]
    fn typechecks_stream_map_int() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.map(Stream.emits([1, 2]), n => n + 1))
    _ <- IO.println(Str.fromInt(List.head(xs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Stream.map Int should typecheck");
    }

    #[test]
    fn typechecks_property_signal_list_at() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Property.signalListAt(0, 0))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Property.signalListAt should typecheck");
    }

    #[test]
    fn typechecks_property_check_sometimes() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ = Property.sometimes("hit")
    s = Property.check("ok", true, "x")
    _ <- IO.println(s)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Property.check / Property.sometimes should typecheck");
    }

    #[test]
    fn typechecks_property_classify() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(if (Property.classify("square", true)) 1 else 0))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Property.classify should typecheck");
    }

    #[test]
    fn typechecks_param_where() {
        let src = r#"
def note(n: Int where n >= 0): Unit = ()
@main def main: IO[Unit] = IO.pure(note(1))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("param where should typecheck");
    }

    #[test]
    fn rejects_non_bool_where() {
        let src = r#"
def note(n: Int where "x"): Unit = ()
@main def main: IO[Unit] = IO.pure(note(1))
"#;
        let p = lower_program(parse(src).unwrap());
        assert!(typecheck(&p).is_err());
    }

    #[test]
    fn typechecks_net_serve_once() {
        let src = r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.pure(path))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Net.serveOnce should typecheck");
    }

    #[test]
    fn typechecks_net_serve() {
        let src = r#"@main def main: IO[Unit] =
  Net.serve(8080, path => IO.pure(path))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Net.serve should typecheck");
    }

    #[test]
    fn rejects_net_serve_non_io() {
        let src = r#"@main def main: IO[Unit] =
  Net.serve(8080, path => path)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Net.serve lambda must return IO[String]"),
            "expected IO[String] handler, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_net_serve_io_int() {
        let src = r#"@main def main: IO[Unit] =
  Net.serve(8080, path => IO.pure(1))
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("Net.serve lambda must return IO[String]"),
            "expected IO[String] handler, got {}",
            err.message()
        );
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
    fn typechecks_sys_kill() {
        let src = r#"@main def main: IO[Unit] =
  Sys.spawn("true").flatMap(pid => Sys.kill(pid))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Sys.kill should typecheck");
    }

    #[test]
    fn typechecks_sys_child_stdio() {
        let src = r#"@main def main: IO[Unit] =
  Sys.spawn("cat").flatMap(pid =>
    Sys.childWrite(pid, "x").flatMap(_ =>
      Sys.childRead(pid, 1).flatMap(_ => Sys.childClose(pid))))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Sys.childWrite/Read/Close should typecheck");
    }

    #[test]
    fn typechecks_sys_read_write() {
        let src = r#"@main def main: IO[Unit] =
  Sys.read(4).flatMap(s => Sys.write(s))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Sys.read/write should typecheck");
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
    fn rejects_ui_run_factory_non_view() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => 1)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Ui.run lambda must return View"),
            "expected View factory, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_view_wrappers() {
        for call in [
            "View.column(View.stretch(View.text(\"x\")), View.button(\"Go\", _ => ()))",
            "View.wrap(View.text(\"a\"), View.text(\"b\"))",
            "View.grid(2, View.text(\"a\"), View.text(\"b\"))",
            "View.scrollH(View.text(\"x\"))",
            "View.maxSize(40, 30, View.text(\"x\"))",
            "View.clip(View.text(\"x\"))",
            "View.focusGroup(View.button(\"Go\", _ => ()))",
            "View.opacity(50, View.text(\"x\"))",
            "View.maxLines(2, View.text(\"x\"))",
            "View.ellipsis(View.text(\"x\"))",
            "View.textColor(Color.rgb(255, 0, 0), View.text(\"x\"))",
            "View.gap(0, View.column(View.text(\"a\"), View.text(\"b\")))",
            "View.fontSize(16, View.text(\"x\"))",
            "View.border(2, Color.rgb(255, 0, 0), View.text(\"x\"))",
            "View.radius(8, View.text(\"x\"))",
            "View.ignorePointer(View.button(\"Go\", _ => ()))",
            "View.absorbPointer(View.button(\"Go\", _ => ()))",
            "View.excludeSemantics(View.button(\"Go\", _ => ()))",
        ] {
            let src = format!("@main def main: IO[Unit] =\n  Ui.run(_ => {call})\n");
            let p = lower_program(parse(&src).unwrap());
            typecheck(&p).unwrap_or_else(|e| panic!("{call} should typecheck: {e:?}"));
        }
    }

    #[test]
    fn typechecks_view_checkbox() {
        let src = r#"@main def main: IO[Unit] =
  for {
    c = Signal.int(0)
    _ <- Ui.run(_ => View.checkbox(c, "Done"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.checkbox should typecheck");
    }

    #[test]
    fn typechecks_view_radio() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radio(n, 1, "On"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.radio should typecheck");
    }

    #[test]
    fn typechecks_view_slider() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.slider(n))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.slider should typecheck");
    }

    #[test]
    fn typechecks_view_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.progress(n))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.progress should typecheck");
    }

    #[test]
    fn typechecks_view_circular_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.circularProgress(n))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.circularProgress should typecheck");
    }

    #[test]
    fn typechecks_view_avatar() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.avatar("S"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.avatar should typecheck");
    }

    #[test]
    fn typechecks_view_switch() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switch(n, "On"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.switch should typecheck");
    }

    #[test]
    fn typechecks_view_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.chip(n, "Pin"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.chip should typecheck");
    }

    #[test]
    fn typechecks_view_filter_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.filterChip(n, "Tag"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.filterChip should typecheck");
    }

    #[test]
    fn typechecks_view_choice_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.choiceChip(n, 0, "Day"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.choiceChip should typecheck");
    }

    #[test]
    fn typechecks_view_action_chip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.actionChip("Go", _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.actionChip should typecheck");
    }

    #[test]
    fn typechecks_view_input_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.inputChip(n, "In"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.inputChip should typecheck");
    }

    #[test]
    fn typechecks_view_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("milk"))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.listTile should typecheck");
    }

    #[test]
    fn typechecks_view_list_tile_trailing() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("milk", View.button("Del", _ => ())))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.listTile trailing should typecheck");
    }

    #[test]
    fn typechecks_view_checkbox_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.checkboxListTile(n, "Star"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.checkboxListTile should typecheck");
    }

    #[test]
    fn typechecks_view_switch_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switchListTile(n, "Quiet"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.switchListTile should typecheck");
    }

    #[test]
    fn typechecks_view_radio_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radioListTile(n, 1, "Night"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.radioListTile should typecheck");
    }

    #[test]
    fn typechecks_view_segmented() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.segmented(n, "List", "Grid"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.segmented should typecheck");
    }

    #[test]
    fn typechecks_view_fab() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fab("+", _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.fab should typecheck");
    }

    #[test]
    fn typechecks_view_editor() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Signal.str("x")
    _ <- Ui.run(_ => View.editor(s))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.editor should typecheck");
    }

    #[test]
    fn typechecks_view_split() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.split(n, View.text("a"), View.text("b")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.split should typecheck");
    }

    #[test]
    fn typechecks_view_overlay() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.overlay(n, View.text("p")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.overlay should typecheck");
    }

    #[test]
    fn typechecks_ui_set_title() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- Ui.setTitle("Hello")
    _ <- Ui.run(_ => View.text("x"))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Ui.setTitle should typecheck");
    }

    #[test]
    fn typechecks_ui_set_editor_caret_and_diagnostics() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- Ui.setEditorCaret(1, 1)
    _ <- Ui.setEditorDiagnostics([(1, 1)])
    _ <- Ui.setEditorDiagnostics([]: List[(Int, Int)])
    _ <- Ui.setEditorTokens([0, 0, 1, 8, 0])
    _ <- Ui.setEditorTokens([]: List[Int])
    _ <- Ui.setEditorInlays([(0, 1, "T")])
    _ <- Ui.setEditorInlays([]: List[(Int, Int, String)])
    _ <- Ui.setEditorFolds([(0, 1)])
    _ <- Ui.setEditorFolds([]: List[(Int, Int)])
    _ <- Ui.run(_ => View.editor(Signal.str("x")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Ui.setEditorCaret/Diagnostics/Tokens should typecheck");
    }

    #[test]
    fn typechecks_fs_list_entries() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs <- Fs.list(".")
    _ <- IO.println(Str.fromInt(List.len(xs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fs.list entries should typecheck");
    }

    #[test]
    fn typechecks_fs_path_helpers() {
        let src = r#"@main def main: IO[Unit] =
  for {
    ok <- Fs.exists("a")
    _ <- IO.println(Fs.join("a", "b"))
    _ <- IO.println(Fs.dirname("a/b"))
    _ <- IO.println(Fs.basename("a/b"))
    _ <- IO.println(if (ok) "y" else "n")
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fs path helpers should typecheck");
    }

    #[test]
    fn typechecks_fs_delete_rename_walk() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- Fs.delete("a")
    _ <- Fs.rename("a", "b")
    xs <- Fs.walk(".")
    _ <- IO.println(Str.fromInt(List.len(xs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Fs delete/rename/walk should typecheck");
    }

    #[test]
    fn typechecks_sys_exec_capture() {
        let src = r#"@main def main: IO[Unit] =
  for {
    (code, out, err) <- Sys.exec("true")
    _ <- IO.println(Str.fromInt(code))
    _ <- IO.println(out)
    _ <- IO.println(err)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Sys.exec capture should typecheck");
    }

    #[test]
    fn typechecks_json_parse_stringify() {
        let src = r#"@main def main: IO[Unit] =
  for {
    j = Json.parse("{\"a\":1}")
    n = Json.Null
    _ <- IO.println(Json.stringify(j))
    _ <- IO.println(Json.stringify(n))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Json.parse/stringify should typecheck");
    }

    #[test]
    fn typechecks_clock_fs_poll() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- Clock.monotonic
    body <- Fs.read("x")
    _ <- IO.sleep(50)
    next <- Fs.read("x")
    _ <- IO.println(next)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Clock plus Fs poll should typecheck");
    }

    #[test]
    fn typechecks_view_outlined_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.outlinedButton("Edit", _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.outlinedButton should typecheck");
    }

    #[test]
    fn typechecks_view_text_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textButton("Open", _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.textButton should typecheck");
    }

    #[test]
    fn typechecks_view_tooltip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.tooltip("Sean", View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.tooltip should typecheck");
    }

    #[test]
    fn typechecks_view_on_secondary() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.onSecondary(View.avatar("S"), _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.onSecondary should typecheck");
    }

    #[test]
    fn typechecks_view_focus_group() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.focusGroup(View.button("a", _ => ())))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.focusGroup should typecheck");
    }

    #[test]
    fn typechecks_view_placeholder() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.placeholder(View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.placeholder should typecheck");
    }

    #[test]
    fn typechecks_view_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.semantics("mark", View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.semantics should typecheck");
    }

    #[test]
    fn typechecks_view_merge_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.mergeSemantics("logo", View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.mergeSemantics should typecheck");
    }

    #[test]
    fn typechecks_view_ink_well() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.inkWell("face", _ => (), View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.inkWell should typecheck");
    }

    #[test]
    fn typechecks_view_visibility() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.visibility(n, View.avatar("S")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.visibility should typecheck");
    }

    #[test]
    fn typechecks_view_offstage() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.offstage(n, View.avatar("S")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.offstage should typecheck");
    }

    #[test]
    fn typechecks_view_unconstrained_box() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.unconstrainedBox(View.avatar("S")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.unconstrainedBox should typecheck");
    }

    #[test]
    fn typechecks_view_badge() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(3)
    _ <- Ui.run(_ => View.badge(n, View.text("x")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.badge should typecheck");
    }

    #[test]
    fn typechecks_view_card() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.card(View.text("x")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.card should typecheck");
    }

    #[test]
    fn typechecks_view_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.divider())
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.divider should typecheck");
    }

    #[test]
    fn typechecks_view_vertical_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.verticalDivider())
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.verticalDivider should typecheck");
    }

    #[test]
    fn typechecks_view_expansion_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.expansionTile(n, "More", View.text("x")))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.expansionTile should typecheck");
    }

    #[test]
    fn typechecks_view_icon_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.iconButton("i", _ => ()))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.iconButton should typecheck");
    }

    #[test]
    fn typechecks_view_each() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.each should typecheck");
    }

    #[test]
    fn typechecks_view_each_mapper() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => View.text(s)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("View.each mapper should typecheck");
    }

    #[test]
    fn rejects_view_each_mapper_non_view() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => s))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("View.each lambda must return View"),
            "expected View mapper, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_view_each_fromint_on_string() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => View.text(Str.fromInt(s))))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expected Int, got String"),
            "expected String/Int mismatch, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_view_each_wrong_arity() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.each())
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("View.each expects 1 or 2 args"),
            "expected arity error, got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_color_rgba() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.background(Color.rgba(1, 2, 3, 4), View.text("x")))
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("Color.rgba should typecheck");
    }

    #[test]
    fn rejects_ui_run_non_view() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(1)
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Ui.run expects _ => View"),
            "expected Ui.run type error, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_ui_run_bare_view() {
        let src = r#"@main def main: IO[Unit] =
  for {
    root = View.text("x")
    _ <- Ui.run(root)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("Ui.run expects _ => View"),
            "expected factory-only Ui.run, got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_view_add_child() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.addChild(View.column(), View.text("x")))
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
  case Of(xs: List[String])
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
    fn typechecks_nested_adt_pattern() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Color.Red) => IO.println("red")
    case Wrap.Box(Color.Blue) => IO.println("blue")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested ADT pattern should typecheck");
    }

    #[test]
    fn typechecks_nested_adt_pattern_wildcard_payload() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Color.Red) => IO.println("red")
    case Wrap.Box(_) => IO.println("other")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("nested wildcard payload should be exhaustive");
    }

    #[test]
    fn rejects_nonexhaustive_nested_adt_pattern() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Color.Red) => IO.println("red")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message()
                .contains("non-exhaustive match: missing Wrap.Box(Color.Blue)"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_opaque_kit_match_against_enum() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  View.text("x") match {
    case Color.Red => IO.println("r")
    case Color.Blue => IO.println("b")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("does not match scrutinee"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn rejects_nested_pattern_type_mismatch() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Opt:
  case Some(x: Int)
  case None
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Opt.None) => IO.println("x")
    case Wrap.Box(_) => IO.println("other")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("does not match scrutinee"),
            "{}",
            err.message()
        );
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
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".into()),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
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
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".into()),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
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
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".into()),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
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
    fn import_alias_and_wildcard_typecheck() {
        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "def tag(): String = \"a\"\ndef one(): Int = 1\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "import A.tag\nimport A.*\n@main def main: IO[Unit] = IO.println(tag())\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("duplicate import"),
            "unexpected: {}",
            err.message()
        );

        let p = crate::parser::parse_sources(&[
            (
                "A.scuzz".into(),
                "def tag(): String = \"a\"\ndef one(): Int = 1\n".into(),
            ),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
            (
                "Main.scuzz".into(),
                "import A.tag as fromA\n@main def main: IO[Unit] = IO.println(Str.concat(fromA(), B.tag()))\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("alias should typecheck");

        let p = crate::parser::parse_sources(&[
            ("A.scuzz".into(), "def greet(): String = \"a\"\n".into()),
            (
                "Main.scuzz".into(),
                "import A.*\n@main def main: IO[Unit] = IO.println(greet())\n".into(),
            ),
        ])
        .unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("wildcard should typecheck");
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
    fn typechecks_generic_record_method() {
        let src = r#"
record Box[T](x: T):
  def get(): T =
    self.x
@main def main: IO[Unit] =
  for {
    b = Box(4)
  } yield IO.println(Str.fromInt(b.get()))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("generic record method should typecheck");
    }

    #[test]
    fn typechecks_generic_enum_method() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] =
  for {
    n = Opt.Some(3).getOrElse(0)
  } yield IO.println(Str.fromInt(n))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("generic enum method should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
    }

    #[test]
    fn typechecks_generic_impl() {
        let src = r#"
record Box[T](x: T)
trait Show:
  def show(): String
impl Show for Box:
  def show(): String =
    "box"
@main def main: IO[Unit] =
  for {
    b = Box(4)
  } yield IO.println(b.show())
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("generic impl should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
        let p = parse_file(src, "trait/src/Main.scuzz").unwrap();
        let p = expand_impls(lower_program(p)).expect("expand labeled");
        typecheck(&p).expect("generic impl with module should typecheck");
    }

    #[test]
    fn typechecks_generic_enum_impl() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
trait Show:
  def show(): String
impl Show for Opt:
  def show(): String =
    self match {
      case Opt.Some(x) => "some"
      case Opt.None => "none"
    }
@main def main: IO[Unit] =
  IO.println(Opt.Some(1).show())
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("generic enum impl should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
        let p = parse_file(src, "trait/src/Main.scuzz").unwrap();
        let p = expand_impls(lower_program(p)).expect("expand labeled");
        typecheck(&p).expect("generic enum impl with module should typecheck");
        let p = elaborate_generics(p).expect("elaborate labeled");
        let p = resolve_field_access(p).expect("fields before mono labeled");
        let p = monomorphize(p).expect("mono labeled");
        resolve_field_access(p).expect("fields after mono labeled");
    }

    #[test]
    fn typechecks_impl_method_mentions_tparam() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
trait Get[T]:
  def getOrElse(default: T): T
impl Get for Opt:
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Opt.Some(2).getOrElse(0)))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("impl method mentioning T should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
        let p = parse_file(src, "trait/src/Main.scuzz").unwrap();
        let p = expand_impls(lower_program(p)).expect("expand labeled");
        typecheck(&p).expect("impl method mentioning T with module should typecheck");
        let p = elaborate_generics(p).expect("elaborate labeled");
        let p = resolve_field_access(p).expect("fields before mono labeled");
        let p = monomorphize(p).expect("mono labeled");
        resolve_field_access(p).expect("fields after mono labeled");
    }

    #[test]
    fn rejects_generic_trait_on_non_generic_type() {
        let src = r#"
record Point(x: Int)
trait Get[T]:
  def getOrElse(default: T): T
impl Get for Point:
  def getOrElse(default: Int): Int = self.x
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = expand_impls(lower_program(parse(src).unwrap())).unwrap_err();
        assert!(
            err.message().contains("expects 1 type argument(s)"),
            "unexpected: {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_impl_trait_args() {
        let src = r#"
record Point(x: Int)
trait Get[T]:
  def getOrElse(default: T): T
impl Get[Int] for Point:
  def getOrElse(default: Int): Int = self.x
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Point(3).getOrElse(0)))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("impl Get[Int] for Point should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
        let p = parse_file(src, "trait/src/Main.scuzz").unwrap();
        let p = expand_impls(lower_program(p)).expect("expand labeled");
        typecheck(&p).expect("impl Get[Int] for Point with module should typecheck");
        let p = elaborate_generics(p).expect("elaborate labeled");
        let p = resolve_field_access(p).expect("fields before mono labeled");
        let p = monomorphize(p).expect("mono labeled");
        resolve_field_access(p).expect("fields after mono labeled");
    }

    #[test]
    fn typechecks_impl_trait_args_on_generic() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
trait Get[T]:
  def getOrElse(default: T): T
impl Get[T] for Opt:
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Opt.Some(2).getOrElse(0)))
"#;
        let p = expand_impls(lower_program(parse(src).unwrap())).expect("expand");
        typecheck(&p).expect("impl Get[T] for Opt should typecheck");
        let p = elaborate_generics(p).expect("elaborate");
        let p = resolve_field_access(p).expect("fields before mono");
        let p = monomorphize(p).expect("mono");
        resolve_field_access(p).expect("fields after mono");
        let p = parse_file(src, "trait/src/Main.scuzz").unwrap();
        let p = expand_impls(lower_program(p)).expect("expand labeled");
        typecheck(&p).expect("impl Get[T] for Opt with module should typecheck");
        let p = elaborate_generics(p).expect("elaborate labeled");
        let p = resolve_field_access(p).expect("fields before mono labeled");
        let p = monomorphize(p).expect("mono labeled");
        resolve_field_access(p).expect("fields after mono labeled");
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
@main def main: IO[Unit] = List.head(List.empty()) match {
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
        // against the pre-mono index. Mangled names are not in it.
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
    fn mono_multi_param_either_clone() {
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
        let p = gen_pipeline(src);
        assert!(
            p.enums
                .iter()
                .any(|e| e.name.contains("__gen_Either_Int_String")),
            "missing Either clone: {:?}",
            p.enums.iter().map(|e| &e.name).collect::<Vec<_>>()
        );
    }

    #[test]
    fn typechecks_require_on_pure_and_io() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    n = 1.require(n => n >= 0)
    _ <- IO.println(Str.fromInt(n)).require(1 == 1)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect(".require should typecheck");
    }

    #[test]
    fn resolve_require_to_property_check_and_assert() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    n = 1.require("nonNeg", n => n >= 0)
    _ <- IO.println("x").require(1 == 1)
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).unwrap();
        let p = resolve_field_access(p).expect("resolve require");
        let dumped = format!("{:?}", p.main.body);
        assert!(
            dumped.contains("Property.check"),
            "expected Property.check residual: {dumped}"
        );
        assert!(
            dumped.contains("Property.assert"),
            "expected Property.assert residual: {dumped}"
        );
    }

    #[test]
    fn typechecks_bool_literals() {
        let src = r#"
@main def main: IO[Unit] = if (true) IO.println("a") else IO.println("b")
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("true/false if");
    }

    #[test]
    fn rejects_int_if_condition() {
        let src = r#"
@main def main: IO[Unit] = if (1) IO.println("a") else IO.println("b")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("if condition must be Bool"),
            "got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_list_int_map_and_filter() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    ys = List.filter([1, 2, 0], x => x > 0)
    xs = List.map(ys, x => x + 1)
    _ <- IO.println(Str.fromInt(List.head(xs)))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("List[Int] map/filter");
    }

    #[test]
    fn typechecks_type_alias() {
        let src = r#"
type UserId = Int
type Labels = List[String]
type BoxList[T] = List[T]
def idOf(n: UserId): UserId =
  n
def joinLabels(xs: Labels): String =
  List.join(xs, ",")
def lenBoxes(xs: BoxList[Int]): Int =
  List.len(xs)
@main def main: IO[Unit] =
  for {
    n = idOf(3)
    s = joinLabels(["a", "b"])
    k = lenBoxes([1, 2])
    ascribe = 1: UserId
    _ <- IO.println(Str.fromInt(n + k))
    _ <- IO.println(s)
    _ <- IO.println(Str.fromInt(ascribe))
  } yield ()
"#;
        let p = lower_program(parse(src).unwrap());
        typecheck(&p).expect("type aliases should typecheck");
    }

    #[test]
    fn rejects_cyclic_type_alias() {
        let src = r#"
type A = B
type B = A
def f(x: A): A = x
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("cyclic type"),
            "got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_type_alias_arity() {
        let src = r#"
type UserId = Int
def f(x: UserId[Int]): Int = x
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("takes no type arguments"),
            "got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_generic_type_alias_without_args() {
        let src = r#"
type BoxList[T] = List[T]
def f(xs: BoxList): Int = List.len(xs)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("expects 1 type argument"),
            "got {}",
            err.message()
        );
    }

    #[test]
    fn rejects_type_alias_enum_clash() {
        let src = r#"
record Point(x: Int, y: Int)
type Point = Int
def f(n: Point): Point = n
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        let err = typecheck(&p).unwrap_err();
        assert!(
            err.message().contains("conflicts with enum"),
            "got {}",
            err.message()
        );
    }

    #[test]
    fn typechecks_imported_type_alias() {
        let sources = vec![
            (
                "A.scuzz".to_string(),
                "type Tag = String\ndef one(): Int = 1\n".to_string(),
            ),
            (
                "Main.scuzz".to_string(),
                "import A.Tag\ndef show(t: Tag): String = t\n@main def main: IO[Unit] = IO.println(show(\"ok\"))\n"
                    .to_string(),
            ),
        ];
        let p = crate::parser::parse_sources(&sources).unwrap();
        let p = lower_program(p);
        typecheck(&p).expect("imported type alias");
    }

    #[test]
    fn typecheck_all_reports_two_def_errors() {
        let src = r#"
def a(): Int = "x"
def b(): String = 1
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = lower_program(parse(src).unwrap());
        let errs = typecheck_all(&p);
        assert!(
            errs.len() >= 2,
            "expected two def errors, got {}: {:?}",
            errs.len(),
            errs
        );
    }
}

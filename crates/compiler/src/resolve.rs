//! File-as-module name resolution and LLVM symbol mangling.
//!
//! Module id is the source file stem (`Foo.scuzz` → `Foo`). Defs and enums are
//! namespaced. Bare names resolve locally first, then through `import Module.name`
//! / `import Module.name as alias` / `import Module.*` in the current module, then
//! to a unique cross-module **public** def/enum when unambiguous. `private def`
//! is only visible within its module (qualified or bare). Enums have no privacy yet.
//! `import Module.*` binds every public def and enum. It does not bind private defs.

use crate::ast::{EnumDef, Expr, ExprKind, FunDef, Import, Pattern, Program, Type};
use std::collections::{HashMap, HashSet};

/// LLVM symbol for a user def: `@sz_user_{Module}_{name}` (or `@sz_user_{name}` when module is empty).
pub fn user_symbol(module: &str, name: &str) -> String {
    if module.is_empty() {
        format!("sz_user_{name}")
    } else {
        format!("sz_user_{module}_{name}")
    }
}

/// Qualified key `Module.name` used in the fun/enum index.
fn qual_key(module: &str, name: &str) -> String {
    format!("{module}.{name}")
}

/// Canonical enum id for types/tags: bare `Name` when module is empty, else `Module.Name`.
pub fn enum_id(module: &str, name: &str) -> String {
    if module.is_empty() {
        name.to_string()
    } else {
        qual_key(module, name)
    }
}

/// Bare enum name from a canonical id (`Module.Name` → `Name`, or unchanged if bare).
pub fn enum_bare_name(id: &str) -> &str {
    match split_dotted(id) {
        Some((_, n)) => n,
        None => id,
    }
}

pub fn module_id_from_label(label: &str) -> String {
    std::path::Path::new(label)
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_string()
}

pub fn split_dotted(callee: &str) -> Option<(&str, &str)> {
    let (a, b) = callee.split_once('.')?;
    if a.is_empty() || b.is_empty() || b.contains('.') {
        return None;
    }
    Some((a, b))
}

fn visible_from(d: &FunDef, current_module: &str) -> bool {
    !d.is_private || d.module == current_module
}

#[derive(Debug)]
pub struct FunIndex<'a> {
    by_qual: HashMap<String, &'a FunDef>,
    by_name: HashMap<String, Vec<&'a FunDef>>,
    /// `(in_module, bare_name)` → `(from_module, source_name)`.
    imports: HashMap<(String, String), (String, String)>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResolveError {
    Unknown(String),
    Ambiguous(String),
    Private { module: String, name: String },
    Duplicate { module: String, name: String },
    DuplicateImport { module: String, name: String },
    ImportPrivate { module: String, name: String },
    ImportUnknown { module: String, name: String },
}

impl std::fmt::Display for ResolveError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ResolveError::Unknown(c) => write!(f, "unknown function {c}"),
            ResolveError::Ambiguous(c) => {
                write!(f, "ambiguous function {c}: qualify as Module.{c}")
            }
            ResolveError::Private { module, name } => {
                write!(f, "private def {module}.{name} is not visible here")
            }
            ResolveError::Duplicate { module, name } => {
                write!(f, "duplicate def {module}.{name}")
            }
            ResolveError::DuplicateImport { module, name } => {
                write!(f, "duplicate import of {name} in module {module}")
            }
            ResolveError::ImportPrivate { module, name } => {
                write!(f, "cannot import private def {module}.{name}")
            }
            ResolveError::ImportUnknown { module, name } => {
                write!(f, "unknown import {module}.{name}")
            }
        }
    }
}

impl<'a> FunIndex<'a> {
    pub fn build(
        defs: &'a [FunDef],
        imports: &'a [Import],
        enums: &'a [EnumDef],
    ) -> Result<Self, ResolveError> {
        let mut by_qual: HashMap<String, &'a FunDef> = HashMap::new();
        let mut by_name: HashMap<String, Vec<&'a FunDef>> = HashMap::new();
        for d in defs {
            let q = qual_key(&d.module, &d.name);
            if by_qual.insert(q, d).is_some() {
                return Err(ResolveError::Duplicate {
                    module: d.module.clone(),
                    name: d.name.clone(),
                });
            }
            by_name.entry(d.name.clone()).or_default().push(d);
        }
        let enum_by_qual: HashMap<String, &'a EnumDef> = enums
            .iter()
            .map(|e| (qual_key(&e.module, &e.name), e))
            .collect();
        let mut import_map: HashMap<(String, String), (String, String)> = HashMap::new();
        for im in imports {
            let binds = import_bindings(im, defs, enums)?;
            for (local, source) in binds {
                let q = qual_key(&im.from_module, &source);
                if let Some(d) = by_qual.get(&q).copied() {
                    if d.is_private {
                        return Err(ResolveError::ImportPrivate {
                            module: im.from_module.clone(),
                            name: source,
                        });
                    }
                } else if !enum_by_qual.contains_key(&q) {
                    return Err(ResolveError::ImportUnknown {
                        module: im.from_module.clone(),
                        name: source,
                    });
                }
                let key = (im.in_module.clone(), local.clone());
                if import_map
                    .insert(key, (im.from_module.clone(), source.clone()))
                    .is_some()
                {
                    return Err(ResolveError::DuplicateImport {
                        module: im.in_module.clone(),
                        name: local,
                    });
                }
            }
        }
        Ok(Self {
            by_qual,
            by_name,
            imports: import_map,
        })
    }

    pub fn resolve(&self, callee: &str, current_module: &str) -> Result<&'a FunDef, ResolveError> {
        if let Some((m, n)) = split_dotted(callee) {
            let d = self
                .by_qual
                .get(&qual_key(m, n))
                .copied()
                .ok_or_else(|| ResolveError::Unknown(callee.to_string()))?;
            if !visible_from(d, current_module) {
                return Err(ResolveError::Private {
                    module: m.to_string(),
                    name: n.to_string(),
                });
            }
            return Ok(d);
        }
        let local = qual_key(current_module, callee);
        if let Some(d) = self.by_qual.get(&local) {
            return Ok(*d);
        }
        if let Some((from, name)) = self
            .imports
            .get(&(current_module.to_string(), callee.to_string()))
        {
            return self
                .by_qual
                .get(&qual_key(from, name))
                .copied()
                .ok_or_else(|| ResolveError::Unknown(callee.to_string()));
        }
        let visible: Vec<&'a FunDef> = self
            .by_name
            .get(callee)
            .map(|v| v.as_slice())
            .unwrap_or(&[])
            .iter()
            .copied()
            .filter(|d| visible_from(d, current_module))
            .collect();
        match visible.as_slice() {
            [] => Err(ResolveError::Unknown(callee.to_string())),
            [d] => Ok(*d),
            _ => Err(ResolveError::Ambiguous(callee.to_string())),
        }
    }
}

#[derive(Debug)]
pub struct EnumIndex<'a> {
    by_qual: HashMap<String, &'a EnumDef>,
    by_name: HashMap<String, Vec<&'a EnumDef>>,
    imports: HashMap<(String, String), (String, String)>,
}

impl<'a> EnumIndex<'a> {
    pub fn build(enums: &'a [EnumDef], imports: &'a [Import]) -> Result<Self, ResolveError> {
        let mut by_qual: HashMap<String, &'a EnumDef> = HashMap::new();
        let mut by_name: HashMap<String, Vec<&'a EnumDef>> = HashMap::new();
        for e in enums {
            let q = qual_key(&e.module, &e.name);
            if by_qual.insert(q, e).is_some() {
                return Err(ResolveError::Duplicate {
                    module: e.module.clone(),
                    name: e.name.clone(),
                });
            }
            by_name.entry(e.name.clone()).or_default().push(e);
        }
        let mut import_map: HashMap<(String, String), (String, String)> = HashMap::new();
        for im in imports {
            if im.is_wildcard() {
                for e in enums {
                    if e.module == im.from_module {
                        import_map.insert(
                            (im.in_module.clone(), e.name.clone()),
                            (im.from_module.clone(), e.name.clone()),
                        );
                    }
                }
            } else {
                import_map.insert(
                    (im.in_module.clone(), im.local_name().to_string()),
                    (im.from_module.clone(), im.name.clone()),
                );
            }
        }
        Ok(Self {
            by_qual,
            by_name,
            imports: import_map,
        })
    }

    pub fn resolve(&self, name: &str, current_module: &str) -> Result<&'a EnumDef, ResolveError> {
        if let Some((m, n)) = split_dotted(name) {
            return self
                .by_qual
                .get(&qual_key(m, n))
                .copied()
                .ok_or_else(|| ResolveError::Unknown(name.to_string()));
        }
        let local = qual_key(current_module, name);
        if let Some(e) = self.by_qual.get(&local) {
            return Ok(*e);
        }
        if let Some((from, src)) = self
            .imports
            .get(&(current_module.to_string(), name.to_string()))
        {
            if let Some(e) = self.by_qual.get(&qual_key(from, src)) {
                return Ok(*e);
            }
            // Import may target a def, not an enum.
            return Err(ResolveError::Unknown(name.to_string()));
        }
        match self.by_name.get(name).map(|v| v.as_slice()).unwrap_or(&[]) {
            [] => Err(ResolveError::Unknown(name.to_string())),
            [e] => Ok(*e),
            _ => Err(ResolveError::Ambiguous(name.to_string())),
        }
    }

    pub fn resolve_id(&self, name: &str, current_module: &str) -> Result<String, ResolveError> {
        let e = self.resolve(name, current_module)?;
        Ok(enum_id(&e.module, &e.name))
    }
}

fn module_exists(defs: &[FunDef], enums: &[EnumDef], module: &str) -> bool {
    defs.iter().any(|d| d.module == module) || enums.iter().any(|e| e.module == module)
}

/// Public def or enum `name` in `module`.
pub fn public_in(defs: &[FunDef], enums: &[EnumDef], module: &str, name: &str) -> bool {
    defs.iter()
        .any(|d| d.module == module && d.name == name && !d.is_private)
        || enums.iter().any(|e| e.module == module && e.name == name)
}

/// `(local_name, source_name)` pairs this import binds.
pub fn import_bindings(
    im: &Import,
    defs: &[FunDef],
    enums: &[EnumDef],
) -> Result<Vec<(String, String)>, ResolveError> {
    if im.is_wildcard() {
        if !module_exists(defs, enums, &im.from_module) {
            return Err(ResolveError::ImportUnknown {
                module: im.from_module.clone(),
                name: "*".into(),
            });
        }
        let mut out = Vec::new();
        for d in defs {
            if d.module == im.from_module && !d.is_private {
                out.push((d.name.clone(), d.name.clone()));
            }
        }
        for e in enums {
            if e.module == im.from_module {
                out.push((e.name.clone(), e.name.clone()));
            }
        }
        return Ok(out);
    }
    Ok(vec![(im.local_name().to_string(), im.name.clone())])
}

/// Resolve a bare imported name to `(from_module, source_name)`.
pub fn bind_import<'a>(
    imports: &'a [Import],
    defs: &'a [FunDef],
    enums: &'a [EnumDef],
    in_module: &str,
    local: &str,
) -> Option<(&'a str, String)> {
    for im in imports {
        if im.in_module != in_module {
            continue;
        }
        if im.is_wildcard() {
            if public_in(defs, enums, &im.from_module, local) {
                return Some((im.from_module.as_str(), local.to_string()));
            }
        } else if im.local_name() == local {
            return Some((im.from_module.as_str(), im.name.clone()));
        }
    }
    None
}

/// Imports in `program` whose bound bare names never appear in that module.
pub fn unused_imports(program: &Program) -> Vec<&Import> {
    let mut used: HashMap<String, HashSet<String>> = HashMap::new();
    for d in &program.defs {
        let set = used.entry(d.module.clone()).or_default();
        collect_type_names(&d.ret, set);
        for p in &d.params {
            collect_type_names(&p.ty, set);
            if let Some(r) = &p.rfn {
                collect_expr_names(r, set);
            }
        }
        collect_expr_names(&d.body, set);
    }
    if !program.main.name.is_empty() {
        collect_expr_names(
            &program.main.body,
            used.entry(program.main.module.clone()).or_default(),
        );
    }
    for im in &program.impls {
        let set = used.entry(im.module.clone()).or_default();
        for m in &im.methods {
            collect_type_names(&m.ret, set);
            for p in &m.params {
                collect_type_names(&p.ty, set);
                if let Some(r) = &p.rfn {
                    collect_expr_names(r, set);
                }
            }
            collect_expr_names(&m.body, set);
        }
    }
    for en in &program.enums {
        let set = used.entry(en.module.clone()).or_default();
        for c in &en.cases {
            for (_, ty) in &c.fields {
                collect_type_names(ty, set);
            }
            for r in &c.field_rfns {
                if let Some(e) = r {
                    collect_expr_names(e, set);
                }
            }
        }
        for m in &en.methods {
            collect_type_names(&m.ret, set);
            for p in &m.params {
                collect_type_names(&p.ty, set);
                if let Some(r) = &p.rfn {
                    collect_expr_names(r, set);
                }
            }
            collect_expr_names(&m.body, set);
        }
    }
    program
        .imports
        .iter()
        .filter(|im| {
            let empty = HashSet::new();
            let names = used.get(&im.in_module).unwrap_or(&empty);
            if im.is_wildcard() {
                match import_bindings(im, &program.defs, &program.enums) {
                    Ok(binds) => binds.iter().all(|(local, _)| !names.contains(local)),
                    Err(_) => true,
                }
            } else {
                !names.contains(im.local_name())
            }
        })
        .collect()
}

fn mark_name(name: &str, out: &mut HashSet<String>) {
    if let Some((m, _)) = split_dotted(name) {
        out.insert(m.to_string());
    } else {
        out.insert(name.to_string());
    }
}

fn collect_type_names(ty: &Type, out: &mut HashSet<String>) {
    match ty {
        Type::Adt(n) | Type::App(n, _) | Type::Var(n) | Type::Opaque(n) => mark_name(n, out),
        Type::List(t) | Type::Io(t) => collect_type_names(t, out),
        Type::Fun(a, b) => {
            collect_type_names(a, out);
            collect_type_names(b, out);
        }
        _ => {}
    }
    if let Type::App(_, args) = ty {
        for a in args {
            collect_type_names(a, out);
        }
    }
}

fn collect_pat_names(p: &Pattern, out: &mut HashSet<String>) {
    match p {
        Pattern::Adt {
            enum_name, binds, ..
        } => {
            mark_name(enum_name, out);
            for b in binds {
                collect_pat_names(b, out);
            }
        }
        Pattern::Or(ps) => {
            for p in ps {
                collect_pat_names(p, out);
            }
        }
        _ => {}
    }
}

fn collect_expr_names(e: &Expr, out: &mut HashSet<String>) {
    match &e.kind {
        ExprKind::Call { callee, .. } => mark_name(callee, out),
        ExprKind::AdtConstruct { enum_name, .. } => mark_name(enum_name, out),
        ExprKind::Match { arms, .. } => {
            for a in arms {
                collect_pat_names(&a.pattern, out);
            }
        }
        ExprKind::Ascribe { ty, .. } => collect_type_names(ty, out),
        _ => {}
    }
    e.for_each_child(|c| collect_expr_names(c, out));
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::{EnumCase, EnumDef, Expr, ExprKind, FunDef, Type};
    use crate::span::Span;

    fn def(module: &str, name: &str) -> FunDef {
        FunDef {
            module: module.into(),
            name: name.into(),
            is_private: false,
            is_law: false,
            is_driver: false,
            type_params: vec![],
            params: vec![],
            ret: Type::String,
            body: Expr::dummy(ExprKind::StrLit("x".into())),
        }
    }

    fn private_def(module: &str, name: &str) -> FunDef {
        let mut d = def(module, name);
        d.is_private = true;
        d
    }

    fn en(module: &str, name: &str) -> EnumDef {
        EnumDef {
            module: module.into(),
            name: name.into(),
            type_params: vec![],
            cases: vec![EnumCase {
                name: "X".into(),
                fields: vec![],
                field_rfns: vec![],
            }],
            is_record: false,
            methods: vec![],
        }
    }

    fn imp(in_module: &str, from_module: &str, name: &str) -> Import {
        Import {
            in_module: in_module.into(),
            from_module: from_module.into(),
            name: name.into(),
            alias: None,
            span: Span::dummy(),
        }
    }

    fn imp_as(in_module: &str, from_module: &str, name: &str, alias: &str) -> Import {
        let mut im = imp(in_module, from_module, name);
        im.alias = Some(alias.into());
        im
    }

    fn imp_star(in_module: &str, from_module: &str) -> Import {
        imp(in_module, from_module, "*")
    }

    #[test]
    fn duplicate_across_modules_ok() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs, &[], &[]).unwrap();
        assert_eq!(idx.resolve("A.tag", "Main").unwrap().module, "A");
        assert_eq!(idx.resolve("B.tag", "Main").unwrap().module, "B");
        assert!(matches!(
            idx.resolve("tag", "Main"),
            Err(ResolveError::Ambiguous(_))
        ));
    }

    #[test]
    fn import_disambiguates_ambiguous_bare() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let imports = vec![imp("Main", "A", "tag")];
        let idx = FunIndex::build(&defs, &imports, &[]).unwrap();
        assert_eq!(idx.resolve("tag", "Main").unwrap().module, "A");
        assert_eq!(idx.resolve("B.tag", "Main").unwrap().module, "B");
    }

    #[test]
    fn bare_resolves_locally() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs, &[], &[]).unwrap();
        assert_eq!(idx.resolve("tag", "A").unwrap().module, "A");
        assert_eq!(idx.resolve("tag", "B").unwrap().module, "B");
    }

    #[test]
    fn unique_cross_module_bare_ok() {
        let defs = vec![def("Shared", "greet")];
        let idx = FunIndex::build(&defs, &[], &[]).unwrap();
        assert_eq!(idx.resolve("greet", "Main").unwrap().module, "Shared");
    }

    #[test]
    fn private_not_visible_cross_module() {
        let defs = vec![private_def("A", "helper"), def("A", "tag")];
        let idx = FunIndex::build(&defs, &[], &[]).unwrap();
        assert!(matches!(
            idx.resolve("A.helper", "Main"),
            Err(ResolveError::Private { .. })
        ));
        assert!(matches!(
            idx.resolve("helper", "Main"),
            Err(ResolveError::Unknown(_))
        ));
        assert_eq!(idx.resolve("helper", "A").unwrap().name, "helper");
        assert_eq!(idx.resolve("A.helper", "A").unwrap().name, "helper");
        assert_eq!(idx.resolve("A.tag", "Main").unwrap().name, "tag");
    }

    #[test]
    fn cannot_import_private() {
        let defs = vec![private_def("A", "helper")];
        let imports = vec![imp("Main", "A", "helper")];
        assert!(matches!(
            FunIndex::build(&defs, &imports, &[]),
            Err(ResolveError::ImportPrivate { .. })
        ));
    }

    #[test]
    fn private_foreign_does_not_ambiguate() {
        let defs = vec![def("A", "tag"), private_def("B", "tag")];
        let idx = FunIndex::build(&defs, &[], &[]).unwrap();
        assert_eq!(idx.resolve("tag", "Main").unwrap().module, "A");
        assert!(matches!(
            idx.resolve("B.tag", "Main"),
            Err(ResolveError::Private { .. })
        ));
    }

    #[test]
    fn import_enum_ok() {
        let enums = vec![en("A", "Msg")];
        let imports = vec![imp("Main", "A", "Msg")];
        let idx = FunIndex::build(&[], &imports, &enums).unwrap();
        let _ = idx; // build succeeds for enum-only import
        let eidx = EnumIndex::build(&enums, &imports).unwrap();
        assert_eq!(eidx.resolve("Msg", "Main").unwrap().module, "A");
        assert_eq!(eidx.resolve_id("Msg", "Main").unwrap(), "A.Msg");
    }

    #[test]
    fn enum_duplicate_across_modules_ok() {
        let enums = vec![en("A", "Msg"), en("B", "Msg")];
        let eidx = EnumIndex::build(&enums, &[]).unwrap();
        assert_eq!(eidx.resolve("Msg", "A").unwrap().module, "A");
        assert!(matches!(
            eidx.resolve("Msg", "Main"),
            Err(ResolveError::Ambiguous(_))
        ));
        let imports = vec![imp("Main", "B", "Msg")];
        let eidx = EnumIndex::build(&enums, &imports).unwrap();
        assert_eq!(eidx.resolve("Msg", "Main").unwrap().module, "B");
    }

    #[test]
    fn mangle_includes_module() {
        assert_eq!(user_symbol("A", "tag"), "sz_user_A_tag");
        assert_eq!(user_symbol("", "add1"), "sz_user_add1");
    }

    #[test]
    fn import_alias_resolves_bare() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let imports = vec![imp_as("Main", "A", "tag", "fromA")];
        let idx = FunIndex::build(&defs, &imports, &[]).unwrap();
        assert_eq!(idx.resolve("fromA", "Main").unwrap().module, "A");
        assert!(matches!(
            idx.resolve("tag", "Main"),
            Err(ResolveError::Ambiguous(_))
        ));
    }

    #[test]
    fn import_wildcard_binds_public_only() {
        let defs = vec![def("A", "tag"), private_def("A", "hidden"), def("A", "one")];
        let enums = vec![en("A", "Msg")];
        let imports = vec![imp_star("Main", "A")];
        let idx = FunIndex::build(&defs, &imports, &enums).unwrap();
        assert_eq!(idx.resolve("tag", "Main").unwrap().module, "A");
        assert_eq!(idx.resolve("one", "Main").unwrap().module, "A");
        assert!(matches!(
            idx.resolve("hidden", "Main"),
            Err(ResolveError::Unknown(_))
        ));
        let eidx = EnumIndex::build(&enums, &imports).unwrap();
        assert_eq!(eidx.resolve("Msg", "Main").unwrap().module, "A");
    }

    #[test]
    fn import_wildcard_unknown_module() {
        let imports = vec![imp_star("Main", "Missing")];
        assert!(matches!(
            FunIndex::build(&[], &imports, &[]),
            Err(ResolveError::ImportUnknown { .. })
        ));
    }

    #[test]
    fn import_wildcard_duplicate_with_specific() {
        let defs = vec![def("A", "tag")];
        let imports = vec![imp("Main", "A", "tag"), imp_star("Main", "A")];
        assert!(matches!(
            FunIndex::build(&defs, &imports, &[]),
            Err(ResolveError::DuplicateImport { .. })
        ));
    }
}

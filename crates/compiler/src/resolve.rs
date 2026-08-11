//! File-as-module name resolution and LLVM symbol mangling.
//!
//! Module id = source file stem (`Foo.scuzz` → `Foo`). Defs are namespaced;
//! enums stay globally unique. Bare names resolve locally first, then to a
//! unique cross-module def when unambiguous.

use crate::ast::FunDef;
use std::collections::HashMap;

/// LLVM symbol for a user def: `@sz_user_{Module}_{name}` (or `@sz_user_{name}` when module is empty).
pub fn user_symbol(module: &str, name: &str) -> String {
    if module.is_empty() {
        format!("sz_user_{name}")
    } else {
        format!("sz_user_{module}_{name}")
    }
}

/// Qualified key `Module.name` used in the fun index.
pub fn qual_key(module: &str, name: &str) -> String {
    format!("{module}.{name}")
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

#[derive(Debug)]
pub struct FunIndex<'a> {
    by_qual: HashMap<String, &'a FunDef>,
    by_name: HashMap<String, Vec<&'a FunDef>>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResolveError {
    Unknown(String),
    Ambiguous(String),
    Duplicate { module: String, name: String },
}

impl std::fmt::Display for ResolveError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ResolveError::Unknown(c) => write!(f, "unknown function {c}"),
            ResolveError::Ambiguous(c) => write!(
                f,
                "ambiguous function {c}: qualify as Module.{c}"
            ),
            ResolveError::Duplicate { module, name } => {
                write!(f, "duplicate def {module}.{name}")
            }
        }
    }
}

impl<'a> FunIndex<'a> {
    pub fn build(defs: &'a [FunDef]) -> Result<Self, ResolveError> {
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
        Ok(Self { by_qual, by_name })
    }

    pub fn resolve(&self, callee: &str, current_module: &str) -> Result<&'a FunDef, ResolveError> {
        if let Some((m, n)) = split_dotted(callee) {
            return self
                .by_qual
                .get(&qual_key(m, n))
                .copied()
                .ok_or_else(|| ResolveError::Unknown(callee.to_string()));
        }
        let local = qual_key(current_module, callee);
        if let Some(d) = self.by_qual.get(&local) {
            return Ok(*d);
        }
        match self.by_name.get(callee).map(|v| v.as_slice()).unwrap_or(&[]) {
            [] => Err(ResolveError::Unknown(callee.to_string())),
            [d] => Ok(*d),
            _ => Err(ResolveError::Ambiguous(callee.to_string())),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::{Expr, ExprKind, FunDef, Type};

    fn def(module: &str, name: &str) -> FunDef {
        FunDef {
            module: module.into(),
            name: name.into(),
            params: vec![],
            ret: Type::String,
            body: Expr::dummy(ExprKind::StrLit("x".into())),
        }
    }

    #[test]
    fn duplicate_across_modules_ok() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs).unwrap();
        assert_eq!(idx.resolve("A.tag", "Main").unwrap().module, "A");
        assert_eq!(idx.resolve("B.tag", "Main").unwrap().module, "B");
        assert!(matches!(
            idx.resolve("tag", "Main"),
            Err(ResolveError::Ambiguous(_))
        ));
    }

    #[test]
    fn bare_resolves_locally() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs).unwrap();
        assert_eq!(idx.resolve("tag", "A").unwrap().module, "A");
        assert_eq!(idx.resolve("tag", "B").unwrap().module, "B");
    }

    #[test]
    fn unique_cross_module_bare_ok() {
        let defs = vec![def("Shared", "greet")];
        let idx = FunIndex::build(&defs).unwrap();
        assert_eq!(idx.resolve("greet", "Main").unwrap().module, "Shared");
    }

    #[test]
    fn mangle_includes_module() {
        assert_eq!(user_symbol("A", "tag"), "sz_user_A_tag");
        assert_eq!(user_symbol("", "add1"), "sz_user_add1");
    }
}

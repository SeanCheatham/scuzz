//! File-as-module name resolution and LLVM symbol mangling.
//!
//! Module id = source file stem (`Foo.scuzz` → `Foo`). Defs are namespaced;
//! enums stay globally unique. Bare names resolve locally first, then via
//! `import Module.name` in the current module, then to a unique cross-module
//! **public** def when unambiguous. `private def` is only visible within its
//! module (qualified or bare).

use crate::ast::{FunDef, Import};
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

fn visible_from(d: &FunDef, current_module: &str) -> bool {
    !d.is_private || d.module == current_module
}

#[derive(Debug)]
pub struct FunIndex<'a> {
    by_qual: HashMap<String, &'a FunDef>,
    by_name: HashMap<String, Vec<&'a FunDef>>,
    /// `(in_module, bare_name)` → import.
    imports: HashMap<(String, String), &'a Import>,
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
            ResolveError::Ambiguous(c) => write!(
                f,
                "ambiguous function {c}: qualify as Module.{c}"
            ),
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
    pub fn build(defs: &'a [FunDef], imports: &'a [Import]) -> Result<Self, ResolveError> {
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
        let mut import_map: HashMap<(String, String), &'a Import> = HashMap::new();
        for im in imports {
            let key = (im.in_module.clone(), im.name.clone());
            if import_map.insert(key, im).is_some() {
                return Err(ResolveError::DuplicateImport {
                    module: im.in_module.clone(),
                    name: im.name.clone(),
                });
            }
            let Some(d) = by_qual.get(&qual_key(&im.from_module, &im.name)).copied() else {
                return Err(ResolveError::ImportUnknown {
                    module: im.from_module.clone(),
                    name: im.name.clone(),
                });
            };
            if d.is_private {
                return Err(ResolveError::ImportPrivate {
                    module: im.from_module.clone(),
                    name: im.name.clone(),
                });
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
        if let Some(im) = self
            .imports
            .get(&(current_module.to_string(), callee.to_string()))
        {
            return self
                .by_qual
                .get(&qual_key(&im.from_module, &im.name))
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::{Expr, ExprKind, FunDef, Type};

    fn def(module: &str, name: &str) -> FunDef {
        FunDef {
            module: module.into(),
            name: name.into(),
            is_private: false,
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

    fn imp(in_module: &str, from_module: &str, name: &str) -> Import {
        Import {
            in_module: in_module.into(),
            from_module: from_module.into(),
            name: name.into(),
        }
    }

    #[test]
    fn duplicate_across_modules_ok() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs, &[]).unwrap();
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
        let idx = FunIndex::build(&defs, &imports).unwrap();
        assert_eq!(idx.resolve("tag", "Main").unwrap().module, "A");
        assert_eq!(idx.resolve("B.tag", "Main").unwrap().module, "B");
    }

    #[test]
    fn bare_resolves_locally() {
        let defs = vec![def("A", "tag"), def("B", "tag")];
        let idx = FunIndex::build(&defs, &[]).unwrap();
        assert_eq!(idx.resolve("tag", "A").unwrap().module, "A");
        assert_eq!(idx.resolve("tag", "B").unwrap().module, "B");
    }

    #[test]
    fn unique_cross_module_bare_ok() {
        let defs = vec![def("Shared", "greet")];
        let idx = FunIndex::build(&defs, &[]).unwrap();
        assert_eq!(idx.resolve("greet", "Main").unwrap().module, "Shared");
    }

    #[test]
    fn private_not_visible_cross_module() {
        let defs = vec![private_def("A", "helper"), def("A", "tag")];
        let idx = FunIndex::build(&defs, &[]).unwrap();
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
            FunIndex::build(&defs, &imports),
            Err(ResolveError::ImportPrivate { .. })
        ));
    }

    #[test]
    fn private_foreign_does_not_ambiguate() {
        let defs = vec![def("A", "tag"), private_def("B", "tag")];
        let idx = FunIndex::build(&defs, &[]).unwrap();
        assert_eq!(idx.resolve("tag", "Main").unwrap().module, "A");
        assert!(matches!(
            idx.resolve("B.tag", "Main"),
            Err(ResolveError::Private { .. })
        ));
    }

    #[test]
    fn mangle_includes_module() {
        assert_eq!(user_symbol("A", "tag"), "sz_user_A_tag");
        assert_eq!(user_symbol("", "add1"), "sz_user_add1");
    }
}

//! Type hierarchy from the same parse as `check`. No second typer.
//!
//! A trait lists impl target types as subtypes. A record or enum lists the
//! traits it implements as supertypes. A trait method lists impl methods.
//! An impl method lists the trait method.

use crate::ast::Program;
use crate::definition::{decl_kw_name, source_for_module, DeclKind};
use crate::hover::{enum_named, ident_at_opts, unique_enum};
use crate::implement::{
    impl_block_end, impl_method_span, skip_brackets, trait_named, unique_trait,
};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use crate::symbols::{KIND_ENUM, KIND_STRUCT};

pub const KIND_METHOD: u8 = 6;
pub const KIND_INTERFACE: u8 = 11;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TypeItem {
    pub file: String,
    pub name: String,
    pub kind: u8,
    pub detail: String,
    pub sel_start: usize,
    pub sel_end: usize,
}

enum Hit {
    Trait(String),
    Type(String),
    TraitMethod {
        trait_name: String,
        method: String,
    },
    ImplMethod {
        trait_name: String,
        for_type: String,
        method: String,
    },
}

/// Type item at `offset`. Kits and params have no item.
pub fn prepare_type_hierarchy(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<TypeItem> {
    match hit_at(program, current_file, current_source, offset) {
        Some(Hit::Trait(n)) => trait_item(program, sources, &n).into_iter().collect(),
        Some(Hit::Type(n)) => type_item(program, sources, &n).into_iter().collect(),
        Some(Hit::TraitMethod { trait_name, method }) => {
            method_item(program, sources, &trait_name, None, &method)
                .into_iter()
                .collect()
        }
        Some(Hit::ImplMethod {
            trait_name,
            for_type,
            method,
        }) => method_item(program, sources, &trait_name, Some(&for_type), &method)
            .into_iter()
            .collect(),
        None => Vec::new(),
    }
}

/// Supertypes of the item at `offset`.
pub fn type_supertypes(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<TypeItem> {
    match hit_at(program, current_file, current_source, offset) {
        Some(Hit::Type(name)) => program
            .impls
            .iter()
            .filter(|im| im.for_type == name)
            .filter_map(|im| trait_item(program, sources, &im.trait_name))
            .collect(),
        Some(Hit::ImplMethod {
            trait_name, method, ..
        }) => method_item(program, sources, &trait_name, None, &method)
            .into_iter()
            .collect(),
        _ => Vec::new(),
    }
}

/// Subtypes of the item at `offset`.
pub fn type_subtypes(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<TypeItem> {
    match hit_at(program, current_file, current_source, offset) {
        Some(Hit::Trait(name)) => program
            .impls
            .iter()
            .filter(|im| im.trait_name == name)
            .filter_map(|im| type_item(program, sources, &im.for_type))
            .collect(),
        Some(Hit::TraitMethod { trait_name, method }) => {
            method_items(program, sources, &trait_name, &method)
        }
        _ => Vec::new(),
    }
}

fn hit_at(program: &Program, current_file: &str, source: &str, offset: usize) -> Option<Hit> {
    let (_, name) = ident_at_opts(source, offset, false)?;
    let module = module_id_from_label(current_file);
    match owner_at(source, offset) {
        Some(Hit::Trait(tr)) => {
            if name == tr {
                return Some(Hit::Trait(name));
            }
            if program
                .traits
                .iter()
                .any(|t| t.name == tr && t.methods.iter().any(|m| m.name == name))
            {
                return Some(Hit::TraitMethod {
                    trait_name: tr,
                    method: name,
                });
            }
        }
        Some(Hit::ImplMethod {
            trait_name: tn,
            for_type: ft,
            ..
        }) => {
            if name == tn {
                return Some(Hit::Trait(tn));
            }
            if name == ft {
                return Some(Hit::Type(ft));
            }
            if program.impls.iter().any(|im| {
                im.trait_name == tn
                    && im.for_type == ft
                    && im.methods.iter().any(|m| m.name == name)
            }) {
                return Some(Hit::ImplMethod {
                    trait_name: tn,
                    for_type: ft,
                    method: name,
                });
            }
        }
        _ => {}
    }
    if unique_trait(program, &name).is_some() || trait_named(program, &module, &name).is_some() {
        return Some(Hit::Trait(name));
    }
    if unique_enum(program, &name).is_some() || enum_named(program, &module, &name).is_some() {
        return Some(Hit::Type(name));
    }
    None
}

fn named_item(
    file: &str,
    name: &str,
    kind: u8,
    detail: String,
    start: usize,
    end: usize,
) -> TypeItem {
    TypeItem {
        file: file.to_string(),
        name: name.to_string(),
        kind,
        detail,
        sel_start: start,
        sel_end: end,
    }
}

fn trait_item(program: &Program, sources: &[(String, String)], name: &str) -> Option<TypeItem> {
    let tr =
        unique_trait(program, name).or_else(|| program.traits.iter().find(|t| t.name == name))?;
    let (file, text) = source_for_module(sources, &tr.module)?;
    let (_, ns, ne) = decl_kw_name(text, DeclKind::Trait, name)?;
    Some(named_item(
        file,
        name,
        KIND_INTERFACE,
        "trait".into(),
        ns,
        ne,
    ))
}

fn type_item(program: &Program, sources: &[(String, String)], name: &str) -> Option<TypeItem> {
    let en =
        unique_enum(program, name).or_else(|| program.enums.iter().find(|e| e.name == name))?;
    let (file, text) = source_for_module(sources, &en.module)?;
    let (_, ns, ne) = decl_kw_name(text, DeclKind::Enum, name)?;
    let (kind, detail) = if en.is_record {
        (KIND_STRUCT, "record")
    } else {
        (KIND_ENUM, "enum")
    };
    Some(named_item(file, name, kind, detail.into(), ns, ne))
}

fn method_item(
    program: &Program,
    sources: &[(String, String)],
    trait_name: &str,
    for_type: Option<&str>,
    method: &str,
) -> Option<TypeItem> {
    if let Some(ft) = for_type {
        let im = program.impls.iter().find(|im| {
            im.trait_name == trait_name
                && im.for_type == ft
                && im.methods.iter().any(|m| m.name == method)
        })?;
        let (file, text) = source_for_module(sources, &im.module)?;
        let (s, e) = impl_method_span(text, trait_name, ft, method)?;
        return Some(named_item(
            file,
            method,
            KIND_METHOD,
            format!("{trait_name} for {ft}"),
            s,
            e,
        ));
    }
    let tr = unique_trait(program, trait_name)
        .or_else(|| program.traits.iter().find(|t| t.name == trait_name))?;
    if !tr.methods.iter().any(|m| m.name == method) {
        return None;
    }
    let (file, text) = source_for_module(sources, &tr.module)?;
    let (s, e) = trait_method_span(text, trait_name, method)?;
    Some(named_item(
        file,
        method,
        KIND_METHOD,
        format!("{trait_name}.{method}"),
        s,
        e,
    ))
}

fn method_items(
    program: &Program,
    sources: &[(String, String)],
    trait_name: &str,
    method: &str,
) -> Vec<TypeItem> {
    program
        .impls
        .iter()
        .filter(|im| im.trait_name == trait_name && im.methods.iter().any(|m| m.name == method))
        .filter_map(|im| method_item(program, sources, &im.trait_name, Some(&im.for_type), method))
        .collect()
}

fn trait_method_span(source: &str, trait_name: &str, method: &str) -> Option<(usize, usize)> {
    let toks = lex(source).ok()?;
    let mut i = 0;
    while i < toks.len() {
        if matches!(toks[i].token, Token::Trait) {
            if let Some(Token::Ident(n)) = toks.get(i + 1).map(|t| &t.token) {
                if n == trait_name {
                    let mut j = skip_brackets(&toks, i + 2);
                    while j < toks.len() && !impl_block_end(source, &toks[j]) {
                        if matches!(toks[j].token, Token::Def) {
                            if let Some(Token::Ident(mn)) = toks.get(j + 1).map(|t| &t.token) {
                                if mn == method {
                                    let m = &toks[j + 1];
                                    return Some((m.span.start, m.span.end));
                                }
                            }
                        }
                        j += 1;
                    }
                }
            }
        }
        i += 1;
    }
    None
}

fn block_end(source: &str, toks: &[crate::lexer::SpannedToken], start: usize) -> usize {
    toks[start..]
        .iter()
        .find(|t| impl_block_end(source, t))
        .map(|t| t.span.start)
        .unwrap_or(source.len())
}

fn owner_at(source: &str, offset: usize) -> Option<Hit> {
    let toks = lex(source).ok()?;
    let mut i = 0;
    while i < toks.len() {
        if matches!(toks[i].token, Token::Trait) {
            if let Some(Token::Ident(n)) = toks.get(i + 1).map(|t| &t.token) {
                let end = block_end(source, &toks, skip_brackets(&toks, i + 2));
                if toks[i].span.start <= offset && offset < end {
                    return Some(Hit::Trait(n.clone()));
                }
            }
        }
        if matches!(toks[i].token, Token::Impl) {
            if let Some(Token::Ident(tn)) = toks.get(i + 1).map(|t| &t.token) {
                let j = skip_brackets(&toks, i + 2);
                if matches!(toks.get(j).map(|t| &t.token), Some(Token::For)) {
                    if let Some(Token::Ident(n)) = toks.get(j + 1).map(|t| &t.token) {
                        let end = block_end(source, &toks, j + 2);
                        if toks[i].span.start <= offset && offset < end {
                            return Some(Hit::ImplMethod {
                                trait_name: tn.clone(),
                                for_type: n.clone(),
                                method: String::new(),
                            });
                        }
                    }
                }
            }
        }
        i += 1;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn show_src() -> &'static str {
        "\
record Point(x: Int, y: Int)
trait Show:
  def show(): String
impl Show for Point:
  def show(): String =
    \"p\"
record Box[T](x: T)
impl Show for Box:
  def show(): String =
    \"b\"
@main def main: IO[Unit] =
  IO.println(\"x\")
"
    }

    fn names(items: &[TypeItem]) -> Vec<&str> {
        items.iter().map(|i| i.name.as_str()).collect()
    }

    fn at(
        src: &str,
        offset: usize,
        f: fn(&Program, &[(String, String)], &str, &str, usize) -> Vec<TypeItem>,
    ) -> Vec<TypeItem> {
        let program = parse_file(src, "Main.scuzz").unwrap();
        f(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            offset,
        )
    }

    #[test]
    fn type_hierarchy_links() {
        let src = show_src();
        let prep = at(src, src.find("Show:").unwrap(), prepare_type_hierarchy);
        assert_eq!(names(&prep), vec!["Show"]);
        assert_eq!(
            names(&at(src, src.find("Show:").unwrap(), type_subtypes)),
            vec!["Point", "Box"]
        );
        assert_eq!(
            names(&at(src, src.find("Point(").unwrap(), type_supertypes)),
            vec!["Show"]
        );
        assert_eq!(
            names(&at(
                src,
                src.find("for Point").unwrap() + 4,
                prepare_type_hierarchy
            )),
            vec!["Point"]
        );
        let trait_show = src.find("def show(): String\nimpl").unwrap() + 4;
        let subs = at(src, trait_show, type_subtypes);
        assert_eq!(subs[0].detail, "Show for Point");
        let impl_at = src.find("impl Show for Point").unwrap();
        let impl_show = src[impl_at..].find("def show()").unwrap() + impl_at + 4;
        assert_eq!(at(src, impl_show, type_supertypes)[0].detail, "Show.show");
        let g = "\
trait Get[T]:
  def getOrElse(default: T): T
record Point(x: Int, y: Int)
impl Get[Int] for Point:
  def getOrElse(default: Int): Int =
    self.x
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        assert_eq!(
            names(&at(g, g.find("Get[T]").unwrap(), type_subtypes)),
            vec!["Point"]
        );
        let methods = at(g, g.find("getOrElse").unwrap(), type_subtypes);
        assert_eq!(names(&methods), vec!["getOrElse"]);
        assert!(at(
            src,
            src.find("def main").unwrap() + 4,
            prepare_type_hierarchy
        )
        .is_empty());
    }
}

//! Go-to-definition from the same parse as `check`. No second typer.

use crate::ast::{Program, Type};
use crate::hover::{
    def_named, enum_named, ident_at_opts, imported_def, imported_enum, unique_def, unique_enum,
};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DefLoc {
    pub file: String,
    pub start: usize,
    pub end: usize,
}

/// Declaration location for the ident at `offset` in `current_file`.
pub fn definition_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Option<DefLoc> {
    let (qual, name) = ident_at_opts(current_source, offset, false)?;
    let module = module_id_from_label(current_file);
    if let Some(q) = &qual {
        if let Some(d) = def_named(program, q, &name) {
            return loc_for_def(sources, d.module.as_str(), DeclKind::Def, &d.name);
        }
        if let Some(en) =
            enum_named(program, q, &name).or_else(|| unique_enum(program, name.as_str()))
        {
            if en.name == *name {
                return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
            }
        }
        if let Some(en) = enum_named(program, &module, q).or_else(|| unique_enum(program, q)) {
            if en.cases.iter().any(|c| c.name == name) {
                return loc_for_def(sources, en.module.as_str(), DeclKind::Case, &name);
            }
        }
        return None;
    }
    if let Some(d) = def_named(program, &module, &name)
        .or_else(|| imported_def(program, &module, &name))
        .or_else(|| unique_def(program, &name))
    {
        return loc_for_def(sources, d.module.as_str(), DeclKind::Def, &d.name);
    }
    if let Some(en) = enum_named(program, &module, &name)
        .or_else(|| imported_enum(program, &module, &name))
        .or_else(|| unique_enum(program, &name))
    {
        return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
    }
    if let Some(a) = program
        .aliases
        .iter()
        .find(|a| a.module == module && a.name == name)
        .or_else(|| {
            let (from, src) = crate::resolve::bind_import(
                &program.imports,
                &program.defs,
                &program.enums,
                &program.aliases,
                &module,
                &name,
            )?;
            program
                .aliases
                .iter()
                .find(|a| a.module == from && a.name == src)
        })
        .or_else(|| {
            let hits: Vec<_> = program.aliases.iter().filter(|a| a.name == name).collect();
            if hits.len() == 1 {
                Some(hits[0])
            } else {
                None
            }
        })
    {
        return loc_for_def(sources, a.module.as_str(), DeclKind::TypeAlias, &a.name);
    }
    let case_hits: Vec<_> = program
        .enums
        .iter()
        .filter(|e| e.cases.iter().any(|c| c.name == name))
        .collect();
    if case_hits.len() == 1 {
        return loc_for_def(sources, case_hits[0].module.as_str(), DeclKind::Case, &name);
    }
    if let Some(span) = param_decl_span(program, sources, &module, &name, offset, current_file) {
        return Some(span);
    }
    None
}

/// Import that bound the ident, or the same location as [`definition_in_sources`].
pub fn declaration_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Option<DefLoc> {
    let (qual, name) = ident_at_opts(current_source, offset, false)?;
    let module = module_id_from_label(current_file);
    let import_hit = program.imports.iter().find(|im| {
        im.in_module == module
            && im.span.end > im.span.start
            && match &qual {
                Some(q) => im.from_module == *q && (im.name == name || im.is_wildcard()),
                None => {
                    im.local_name() == name
                        || (im.is_wildcard()
                            && crate::resolve::public_in(
                                &program.defs,
                                &program.enums,
                                &program.aliases,
                                &im.from_module,
                                &name,
                            ))
                }
            }
    });
    if let Some(im) = import_hit {
        let on_import = offset >= im.span.start && offset < im.span.end;
        let local = qual.is_none()
            && (def_named(program, &module, &name).is_some()
                || enum_named(program, &module, &name).is_some());
        if on_import || !local {
            let file = if !im.span.file.is_empty() {
                im.span.file.clone()
            } else {
                current_file.to_string()
            };
            return Some(DefLoc {
                file,
                start: im.span.start,
                end: im.span.end,
            });
        }
    }
    definition_in_sources(program, sources, current_file, current_source, offset)
}

/// Named enum/record for the ident at `offset` (param/def return type, or the ident itself).
pub fn type_definition_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Option<DefLoc> {
    let (qual, name) = ident_at_opts(current_source, offset, false)?;
    let module = module_id_from_label(current_file);
    if is_builtin_type(&name) && qual.is_none() {
        return None;
    }
    if let Some(q) = &qual {
        if let Some(en) =
            enum_named(program, q, &name).or_else(|| unique_enum(program, name.as_str()))
        {
            if en.name == *name {
                return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
            }
        }
        if let Some(en) = enum_named(program, q, q)
            .or_else(|| enum_named(program, &module, q))
            .or_else(|| unique_enum(program, q))
        {
            if en.cases.iter().any(|c| c.name == name) {
                return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
            }
        }
    }
    if let Some(en) = enum_named(program, &module, &name).or_else(|| unique_enum(program, &name)) {
        return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
    }
    let case_hits: Vec<_> = program
        .enums
        .iter()
        .filter(|e| e.cases.iter().any(|c| c.name == name))
        .collect();
    if case_hits.len() == 1 {
        return loc_for_def(
            sources,
            case_hits[0].module.as_str(),
            DeclKind::Enum,
            &case_hits[0].name,
        );
    }
    if let Some(d) = def_named(program, &module, &name).or_else(|| unique_def(program, &name)) {
        return loc_from_type(program, sources, &d.module, &d.ret);
    }
    if let Some(ty) = param_ty_in_module(program, sources, &module, &name, offset, current_file) {
        return loc_from_type(program, sources, &module, ty);
    }
    None
}

fn is_builtin_type(name: &str) -> bool {
    matches!(
        name,
        "Int" | "Float" | "String" | "Bool" | "Unit" | "List" | "Map" | "Set" | "IO"
    )
}

fn peel_type(ty: &Type) -> &Type {
    match ty {
        Type::List(t) | Type::Io(t) => peel_type(t),
        Type::Fun(_, r) | Type::Tuple(_, r) => peel_type(r),
        other => other,
    }
}

fn loc_from_type(
    program: &Program,
    sources: &[(String, String)],
    module: &str,
    ty: &Type,
) -> Option<DefLoc> {
    let t = peel_type(ty);
    let n = match t {
        Type::Adt(n) | Type::App(n, _) => n.as_str(),
        _ => return None,
    };
    if is_builtin_type(n) {
        return None;
    }
    let en = enum_named(program, module, n).or_else(|| unique_enum(program, n))?;
    loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name)
}

fn param_ty_in_module<'a>(
    program: &'a Program,
    sources: &[(String, String)],
    module: &str,
    name: &str,
    offset: usize,
    current_file: &str,
) -> Option<&'a Type> {
    let d = program.defs.iter().find(|d| {
        d.module == module
            && d.params.iter().any(|p| p.name == name)
            && d.body.span.file == current_file
            && def_covers_offset(sources, d, offset)
    })?;
    d.params.iter().find(|p| p.name == name).map(|p| &p.ty)
}

#[derive(Clone, Copy)]
pub(crate) enum DeclKind {
    Def,
    Enum,
    Case,
    Trait,
    TypeAlias,
}

pub(crate) fn loc_for_def(
    sources: &[(String, String)],
    module: &str,
    kind: DeclKind,
    name: &str,
) -> Option<DefLoc> {
    let (file, text) = source_for_module(sources, module)?;
    let (start, end) = decl_span(text, kind, name)?;
    Some(DefLoc {
        file: file.to_string(),
        start,
        end,
    })
}

pub(crate) fn source_for_module<'a>(
    sources: &'a [(String, String)],
    module: &str,
) -> Option<(&'a str, &'a str)> {
    sources
        .iter()
        .find(|(label, _)| module_id_from_label(label) == module)
        .map(|(l, t)| (l.as_str(), t.as_str()))
}

pub(crate) fn decl_span(source: &str, kind: DeclKind, name: &str) -> Option<(usize, usize)> {
    decl_kw_name(source, kind, name).map(|(_, s, e)| (s, e))
}

/// Keyword start plus name span for a declaration.
pub(crate) fn decl_kw_name(
    source: &str,
    kind: DeclKind,
    name: &str,
) -> Option<(usize, usize, usize)> {
    let toks = lex(source).ok()?;
    for i in 0..toks.len() {
        let ok = match kind {
            DeclKind::Def => matches!(toks[i].token, Token::Def | Token::Law),
            DeclKind::Enum => matches!(toks[i].token, Token::Enum | Token::Record),
            DeclKind::Case => matches!(toks[i].token, Token::Case),
            DeclKind::Trait => matches!(toks[i].token, Token::Trait),
            DeclKind::TypeAlias => matches!(toks[i].token, Token::Type),
        };
        if !ok {
            continue;
        }
        if let Some(t) = toks.get(i + 1) {
            if let Token::Ident(n) = &t.token {
                if n == name {
                    return Some((toks[i].span.start, t.span.start, t.span.end));
                }
            }
        }
    }
    None
}

fn param_decl_span(
    program: &Program,
    sources: &[(String, String)],
    module: &str,
    name: &str,
    offset: usize,
    current_file: &str,
) -> Option<DefLoc> {
    let d = program.defs.iter().find(|d| {
        d.module == module
            && d.params.iter().any(|p| p.name == name)
            && d.body.span.file == current_file
            && def_covers_offset(sources, d, offset)
    })?;
    let (file, text) = source_for_module(sources, &d.module)?;
    let toks = lex(text).ok()?;
    let mut after_def = false;
    for t in &toks {
        if matches!(t.token, Token::Def | Token::Law) {
            after_def = false;
        }
        if let Token::Ident(n) = &t.token {
            if !after_def && n == &d.name && t.span.start < d.body.span.start {
                after_def = true;
                continue;
            }
            if after_def && n == name {
                return Some(DefLoc {
                    file: file.to_string(),
                    start: t.span.start,
                    end: t.span.end,
                });
            }
        }
        if after_def && t.span.start >= d.body.span.start {
            break;
        }
    }
    None
}

fn def_covers_offset(sources: &[(String, String)], d: &crate::ast::FunDef, offset: usize) -> bool {
    if d.body.span.start <= offset && offset <= d.body.span.end {
        return true;
    }
    if offset > d.body.span.end {
        return false;
    }
    let Some((_, text)) = source_for_module(sources, &d.module) else {
        return false;
    };
    let Ok(toks) = lex(text) else {
        return false;
    };
    let mut name_start = None;
    for i in 0..toks.len() {
        if !matches!(toks[i].token, Token::Def | Token::Law) {
            continue;
        }
        let Some(t) = toks.get(i + 1) else {
            continue;
        };
        let Token::Ident(n) = &t.token else {
            continue;
        };
        if n == &d.name && t.span.start < d.body.span.start {
            name_start = Some(t.span.start);
        }
    }
    match name_start {
        Some(s) => offset >= s && offset < d.body.span.start,
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn loc(src: &str, needle: &str) -> DefLoc {
        let program = parse_file(src, "Main.scuzz").unwrap();
        let offset = src.find(needle).expect(needle);
        definition_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            offset,
        )
        .unwrap_or_else(|| panic!("no definition at {needle:?}"))
    }

    #[test]
    fn defines_def_from_call() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(Str.fromInt(add(1)))\n";
        let call = src.rfind("add").unwrap();
        let program = parse_file(src, "Main.scuzz").unwrap();
        let d = definition_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            call,
        )
        .unwrap();
        let decl = src.find("add").unwrap();
        assert_eq!(d.start, decl);
        let _ = loc(src, "add");
    }

    #[test]
    fn defines_enum_and_case() {
        let src =
            "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let program = parse_file(src, "Main.scuzz").unwrap();
        let sources = [("Main.scuzz".into(), src.to_string())];
        let color = src.find("Color").unwrap();
        let d = definition_in_sources(&program, &sources, "Main.scuzz", src, color).unwrap();
        assert_eq!(d.start, color);
        let red_use = src.find("Red").unwrap();
        let d = definition_in_sources(&program, &sources, "Main.scuzz", src, red_use).unwrap();
        assert_eq!(d.start, red_use);
    }

    #[test]
    fn defines_param_from_signature() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let program = parse_file(src, "Main.scuzz").unwrap();
        let sources = [("Main.scuzz".into(), src.to_string())];
        let param = src.find("n:").unwrap();
        let d = definition_in_sources(&program, &sources, "Main.scuzz", src, param).unwrap();
        assert_eq!(d.start, param);
        let body = src.rfind("= n").unwrap() + 2;
        let d = definition_in_sources(&program, &sources, "Main.scuzz", src, body).unwrap();
        assert_eq!(d.start, param);
    }

    fn ty_loc(src: &str, needle: &str) -> Option<DefLoc> {
        let program = parse_file(src, "Main.scuzz").unwrap();
        let offset = src.find(needle).expect(needle);
        type_definition_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            offset,
        )
    }

    #[test]
    fn type_def_param_and_return_go_to_enum() {
        let src = "\
enum Color:
  case Red
  case Blue
def paint(c: Color): Color =
  c
def wrap(xs: List[Color]): List[Color] =
  xs
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let color = src.find("Color").unwrap();
        let param = src.find("c:").unwrap();
        assert_eq!(ty_loc(src, "c:").unwrap().start, color);
        let body_c = src.rfind("\n  c\n").unwrap() + 3;
        let program = parse_file(src, "Main.scuzz").unwrap();
        let d = type_definition_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            body_c,
        )
        .expect("body c");
        assert_eq!(d.start, color);
        assert_eq!(ty_loc(src, "paint").unwrap().start, color);
        assert_eq!(ty_loc(src, "wrap").unwrap().start, color);
        let red = src.find("Red").unwrap();
        assert_eq!(
            type_definition_in_sources(
                &program,
                &[("Main.scuzz".into(), src.into())],
                "Main.scuzz",
                src,
                red,
            )
            .unwrap()
            .start,
            color
        );
        let _ = param;
    }

    #[test]
    fn type_def_skips_builtin_int() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        assert!(ty_loc(src, "add").is_none());
        assert!(ty_loc(src, "n:").is_none());
        assert!(ty_loc(src, "Int").is_none());
    }

    #[test]
    fn defines_type_alias_from_use() {
        let src = r#"
type UserId = Int
def idOf(n: UserId): UserId = n
@main def main: IO[Unit] = IO.println(Str.fromInt(idOf(1)))
"#;
        let program = parse_file(src, "Main.scuzz").unwrap();
        let sources = [("Main.scuzz".into(), src.to_string())];
        let use_at = src.rfind("UserId").unwrap();
        let d = definition_in_sources(&program, &sources, "Main.scuzz", src, use_at).unwrap();
        let decl = src.find("UserId").unwrap();
        assert_eq!(d.start, decl);
    }
}

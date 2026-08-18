//! Go-to-definition from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::hover::{def_named, enum_named, ident_at_opts, unique_def, unique_enum};
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
    if let Some(d) = def_named(program, &module, &name).or_else(|| unique_def(program, &name)) {
        return loc_for_def(sources, d.module.as_str(), DeclKind::Def, &d.name);
    }
    if let Some(en) = enum_named(program, &module, &name).or_else(|| unique_enum(program, &name)) {
        return loc_for_def(sources, en.module.as_str(), DeclKind::Enum, &en.name);
    }
    let case_hits: Vec<_> = program
        .enums
        .iter()
        .filter(|e| e.module == module || e.cases.iter().any(|c| c.name == name))
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

#[derive(Clone, Copy)]
pub(crate) enum DeclKind {
    Def,
    Enum,
    Case,
}

fn loc_for_def(
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
}

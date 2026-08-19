//! Go-to-implementation from the same parse as `check`. No second typer.

use crate::ast::{Program, TraitDef};
use crate::definition::DefLoc;
use crate::hover::ident_at_opts;
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;

/// Impl locations for the ident at `offset` in `current_file`.
///
/// A trait name lists each `impl Trait for Type` (`Type` span).
/// A trait method or `recv.method` lists each matching impl method name.
pub fn implementation_in_sources(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<DefLoc> {
    let Some((qual, name)) = ident_at_opts(current_source, offset, false) else {
        return Vec::new();
    };
    let module = module_id_from_label(current_file);
    if let Some(q) = &qual {
        if let Some(tr) = trait_named(program, &module, q).or_else(|| unique_trait(program, q)) {
            return impl_method_locs(program, sources, &tr.name, Some(&name));
        }
        return impl_method_locs(program, sources, "", Some(&name));
    }
    if unique_trait(program, &name).is_some() {
        return impl_for_type_locs(program, sources, &name);
    }
    impl_method_locs(program, sources, "", Some(&name))
}

pub(crate) fn trait_named<'a>(
    program: &'a Program,
    module: &str,
    name: &str,
) -> Option<&'a TraitDef> {
    program
        .traits
        .iter()
        .find(|t| t.module == module && t.name == name)
}

pub(crate) fn unique_trait<'a>(program: &'a Program, name: &str) -> Option<&'a TraitDef> {
    let hits: Vec<_> = program.traits.iter().filter(|t| t.name == name).collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

fn impl_for_type_locs(
    program: &Program,
    sources: &[(String, String)],
    trait_name: &str,
) -> Vec<DefLoc> {
    let mut out = Vec::new();
    for im in &program.impls {
        if im.trait_name != trait_name {
            continue;
        }
        let Some((file, text)) = source_for(sources, &im.module) else {
            continue;
        };
        if let Some((start, end)) = impl_for_type_span(text, trait_name, &im.for_type) {
            out.push(DefLoc {
                file: file.to_string(),
                start,
                end,
            });
        }
    }
    out
}

fn impl_method_locs(
    program: &Program,
    sources: &[(String, String)],
    trait_name: &str,
    method: Option<&str>,
) -> Vec<DefLoc> {
    let Some(method) = method else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for im in &program.impls {
        if !trait_name.is_empty() && im.trait_name != trait_name {
            continue;
        }
        if !im.methods.iter().any(|m| m.name == method) {
            continue;
        }
        if !trait_name.is_empty()
            || program
                .traits
                .iter()
                .any(|t| t.name == im.trait_name && t.methods.iter().any(|m| m.name == method))
        {
            let Some((file, text)) = source_for(sources, &im.module) else {
                continue;
            };
            if let Some((start, end)) = impl_method_span(text, &im.trait_name, &im.for_type, method)
            {
                out.push(DefLoc {
                    file: file.to_string(),
                    start,
                    end,
                });
            }
        }
    }
    out
}

fn source_for<'a>(sources: &'a [(String, String)], module: &str) -> Option<(&'a str, &'a str)> {
    sources
        .iter()
        .find(|(label, _)| module_id_from_label(label) == module)
        .map(|(l, t)| (l.as_str(), t.as_str()))
}

pub(crate) fn impl_for_type_span(
    source: &str,
    trait_name: &str,
    for_type: &str,
) -> Option<(usize, usize)> {
    let (toks, i) = find_impl_header(source, trait_name, for_type)?;
    let t = toks.get(i)?;
    Some((t.span.start, t.span.end))
}

pub(crate) fn impl_method_span(
    source: &str,
    trait_name: &str,
    for_type: &str,
    method: &str,
) -> Option<(usize, usize)> {
    let (toks, type_i) = find_impl_header(source, trait_name, for_type)?;
    let mut i = type_i + 1;
    while i < toks.len() {
        if impl_block_end(source, &toks[i]) {
            break;
        }
        if matches!(toks[i].token, Token::Def) {
            if let Some(t) = toks.get(i + 1) {
                if let Token::Ident(n) = &t.token {
                    if n == method {
                        return Some((t.span.start, t.span.end));
                    }
                }
            }
        }
        i += 1;
    }
    None
}

/// Index of the `for Type` ident in `toks`.
fn find_impl_header(
    source: &str,
    trait_name: &str,
    for_type: &str,
) -> Option<(Vec<crate::lexer::SpannedToken>, usize)> {
    let toks = lex(source).ok()?;
    let mut i = 0;
    while i < toks.len() {
        if !matches!(toks[i].token, Token::Impl) {
            i += 1;
            continue;
        }
        let Some(t) = toks.get(i + 1) else {
            break;
        };
        let Token::Ident(tn) = &t.token else {
            i += 1;
            continue;
        };
        if tn != trait_name {
            i += 1;
            continue;
        }
        let j = skip_brackets(&toks, i + 2);
        if !matches!(toks.get(j).map(|t| &t.token), Some(Token::For)) {
            i += 1;
            continue;
        }
        let Some(ft) = toks.get(j + 1) else {
            break;
        };
        let Token::Ident(n) = &ft.token else {
            i += 1;
            continue;
        };
        if n == for_type {
            return Some((toks, j + 1));
        }
        i += 1;
    }
    None
}

pub(crate) fn skip_brackets(toks: &[crate::lexer::SpannedToken], mut i: usize) -> usize {
    if !matches!(toks.get(i).map(|t| &t.token), Some(Token::LBracket)) {
        return i;
    }
    let mut depth = 0;
    while i < toks.len() {
        match toks[i].token {
            Token::LBracket => depth += 1,
            Token::RBracket => {
                depth -= 1;
                i += 1;
                if depth == 0 {
                    return i;
                }
                continue;
            }
            _ => {}
        }
        i += 1;
    }
    i
}

pub(crate) fn impl_block_end(source: &str, tok: &crate::lexer::SpannedToken) -> bool {
    match tok.token {
        Token::Impl
        | Token::Trait
        | Token::Enum
        | Token::Record
        | Token::Import
        | Token::Law
        | Token::AtMain => true,
        Token::Def => unindented(source, tok.span.start),
        _ => false,
    }
}

fn unindented(source: &str, offset: usize) -> bool {
    let line_start = source[..offset].rfind('\n').map(|i| i + 1).unwrap_or(0);
    source[line_start..offset].is_empty()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn locs(src: &str, needle: &str) -> Vec<DefLoc> {
        let program = parse_file(src, "Main.scuzz").unwrap();
        let offset = src.find(needle).expect(needle);
        implementation_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            offset,
        )
    }

    fn locs_at(src: &str, offset: usize) -> Vec<DefLoc> {
        let program = parse_file(src, "Main.scuzz").unwrap();
        implementation_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            offset,
        )
    }

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

    #[test]
    fn trait_name_lists_for_types() {
        let src = show_src();
        let found = locs(src, "Show:");
        let names: Vec<&str> = found.iter().map(|l| &src[l.start..l.end]).collect();
        assert_eq!(names, vec!["Point", "Box"], "{found:?}");
    }

    #[test]
    fn trait_method_lists_impl_methods() {
        let src = show_src();
        let trait_show = src.find("def show(): String\nimpl").unwrap();
        let program = parse_file(src, "Main.scuzz").unwrap();
        let found = implementation_in_sources(
            &program,
            &[("Main.scuzz".into(), src.into())],
            "Main.scuzz",
            src,
            trait_show + 4,
        );
        assert_eq!(found.len(), 2, "{found:?}");
        for l in &found {
            assert_eq!(&src[l.start..l.end], "show");
        }
        let first_impl = src.find("impl Show for Point").unwrap();
        assert!(found[0].start > first_impl);
    }

    #[test]
    fn call_site_lists_impl_methods() {
        let src = "\
record Point(x: Int, y: Int)
trait Show:
  def show(): String
impl Show for Point:
  def show(): String =
    \"p\"
def paint(p: Point): String =
  p.show()
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let show_call = src.find(".show()").unwrap() + 1;
        let found = locs_at(src, show_call);
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(&src[found[0].start..found[0].end], "show");
        let impl_show = src.find("impl Show").unwrap();
        assert!(found[0].start > impl_show);
    }

    #[test]
    fn skips_unknown_ident() {
        let src = show_src();
        let at = src.find("def main").unwrap() + 4;
        let found = locs_at(src, at);
        assert!(found.is_empty(), "{found:?}");
    }

    #[test]
    fn generic_impl_header() {
        let src = "\
trait Get[T]:
  def getOrElse(default: T): T
record Point(x: Int, y: Int)
impl Get[Int] for Point:
  def getOrElse(default: Int): Int =
    self.x
@main def main: IO[Unit] =
  IO.println(\"x\")
";
        let found = locs(src, "Get[T]");
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(&src[found[0].start..found[0].end], "Point");
        let found = locs(src, "getOrElse");
        assert_eq!(found.len(), 1, "{found:?}");
        assert_eq!(&src[found[0].start..found[0].end], "getOrElse");
        let impl_at = src.find("impl Get").unwrap();
        assert!(found[0].start > impl_at);
    }
}

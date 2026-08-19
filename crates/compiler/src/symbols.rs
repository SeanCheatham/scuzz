//! Document outline from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::{decl_kw_name, DeclKind};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;

pub const KIND_METHOD: u8 = 6;
pub const KIND_FIELD: u8 = 8;
pub const KIND_ENUM: u8 = 10;
pub const KIND_INTERFACE: u8 = 11;
pub const KIND_FN: u8 = 12;
pub const KIND_ENUM_MEMBER: u8 = 22;
pub const KIND_STRUCT: u8 = 23;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DocSymbol {
    pub name: String,
    pub kind: u8,
    pub range_start: usize,
    pub range_end: usize,
    pub sel_start: usize,
    pub sel_end: usize,
    pub children: Vec<DocSymbol>,
}

/// Outline for `current_file`. Decls in other modules are omitted.
pub fn symbols_in_source(program: &Program, current_file: &str, source: &str) -> Vec<DocSymbol> {
    let module = module_id_from_label(current_file);
    let mut out = Vec::new();
    for en in &program.enums {
        if en.module != module {
            continue;
        }
        let Some((kw, ns, ne)) = decl_kw_name(source, DeclKind::Enum, &en.name) else {
            continue;
        };
        let mut children = Vec::new();
        if en.is_record {
            for (fname, fs, fe) in record_field_spans(source, &en.name) {
                children.push(leaf(&fname, KIND_FIELD, fs, fe));
            }
        } else {
            for c in &en.cases {
                if let Some((ckw, cs, ce)) = decl_kw_name(source, DeclKind::Case, &c.name) {
                    children.push(sym(&c.name, KIND_ENUM_MEMBER, ckw, ce, cs, ce, vec![]));
                }
            }
        }
        let range_end = children
            .iter()
            .map(|c| c.range_end)
            .max()
            .unwrap_or(ne)
            .max(ne);
        let kind = if en.is_record { KIND_STRUCT } else { KIND_ENUM };
        out.push(sym(&en.name, kind, kw, range_end, ns, ne, children));
    }
    for d in &program.defs {
        if d.module != module || d.name.starts_with("__") {
            continue;
        }
        let Some((kw, ns, ne)) = decl_kw_name(source, DeclKind::Def, &d.name) else {
            continue;
        };
        let end = d.body.span.end.max(ne);
        out.push(sym(&d.name, KIND_FN, kw, end, ns, ne, vec![]));
    }
    if program.main.module == module && !program.main.name.is_empty() {
        if let Some((kw, ns, ne)) = main_span(source, &program.main.name) {
            let end = program.main.body.span.end.max(ne);
            out.push(sym(&program.main.name, KIND_FN, kw, end, ns, ne, vec![]));
        }
    }
    out.sort_by_key(|s| s.range_start);
    out
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct WorkspaceSymbol {
    pub name: String,
    pub kind: u8,
    pub start: usize,
    pub end: usize,
    pub container: Option<String>,
}

/// Flattened outline entries whose names match `query` (empty query keeps all).
pub fn workspace_symbols_in_source(
    program: &Program,
    current_file: &str,
    source: &str,
    query: &str,
) -> Vec<WorkspaceSymbol> {
    let docs = symbols_in_source(program, current_file, source);
    let mut out = Vec::new();
    flatten_workspace(&docs, None, &mut out);
    if !query.is_empty() {
        let q = query.to_ascii_lowercase();
        out.retain(|s| s.name.to_ascii_lowercase().contains(&q));
    }
    out
}

fn flatten_workspace(syms: &[DocSymbol], container: Option<&str>, out: &mut Vec<WorkspaceSymbol>) {
    for s in syms {
        out.push(WorkspaceSymbol {
            name: s.name.clone(),
            kind: s.kind,
            start: s.sel_start,
            end: s.sel_end,
            container: container.map(str::to_string),
        });
        flatten_workspace(&s.children, Some(&s.name), out);
    }
}

fn leaf(name: &str, kind: u8, start: usize, end: usize) -> DocSymbol {
    sym(name, kind, start, end, start, end, vec![])
}

fn sym(
    name: &str,
    kind: u8,
    range_start: usize,
    range_end: usize,
    sel_start: usize,
    sel_end: usize,
    children: Vec<DocSymbol>,
) -> DocSymbol {
    DocSymbol {
        name: name.to_string(),
        kind,
        range_start,
        range_end: range_end.max(sel_end),
        sel_start,
        sel_end,
        children,
    }
}

fn main_span(source: &str, name: &str) -> Option<(usize, usize, usize)> {
    let toks = lex(source).ok()?;
    for i in 0..toks.len() {
        if !matches!(toks[i].token, Token::AtMain) {
            continue;
        }
        for j in i + 1..toks.len() {
            if matches!(toks[j].token, Token::Def) {
                if let Some(t) = toks.get(j + 1) {
                    if let Token::Ident(n) = &t.token {
                        if n == name {
                            return Some((toks[i].span.start, t.span.start, t.span.end));
                        }
                    }
                }
                break;
            }
        }
    }
    None
}

fn record_field_spans(source: &str, rec: &str) -> Vec<(String, usize, usize)> {
    let Ok(toks) = lex(source) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for i in 0..toks.len() {
        if !matches!(toks[i].token, Token::Record) {
            continue;
        }
        let Some(t) = toks.get(i + 1) else {
            continue;
        };
        let Token::Ident(n) = &t.token else {
            continue;
        };
        if n != rec {
            continue;
        }
        let mut j = i + 2;
        while j < toks.len() && !matches!(toks[j].token, Token::LParen) {
            j += 1;
        }
        j += 1;
        while j < toks.len() && !matches!(toks[j].token, Token::RParen) {
            if let Token::Ident(fname) = &toks[j].token {
                if toks
                    .get(j + 1)
                    .is_some_and(|n| matches!(n.token, Token::Colon))
                {
                    out.push((fname.clone(), toks[j].span.start, toks[j].span.end));
                }
            }
            j += 1;
        }
        break;
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn outlines_def_enum_record_and_main() {
        let src = r#"
enum Color:
  case Red
  case Blue
record Point(x: Int, y: Int)
def add(n: Int): Int = n
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let syms = symbols_in_source(&p, "Main.scuzz", src);
        let names: Vec<_> = syms.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"Color"), "{names:?}");
        assert!(names.contains(&"Point"), "{names:?}");
        assert!(names.contains(&"add"), "{names:?}");
        assert!(names.contains(&"main"), "{names:?}");
        let color = syms.iter().find(|s| s.name == "Color").unwrap();
        assert_eq!(color.kind, KIND_ENUM);
        let cases: Vec<_> = color.children.iter().map(|c| c.name.as_str()).collect();
        assert_eq!(cases, ["Red", "Blue"]);
        let point = syms.iter().find(|s| s.name == "Point").unwrap();
        assert_eq!(point.kind, KIND_STRUCT);
        let fields: Vec<_> = point.children.iter().map(|c| c.name.as_str()).collect();
        assert_eq!(fields, ["x", "y"]);
    }

    #[test]
    fn workspace_query_filters_and_flattens_children() {
        let src = r#"
enum Color:
  case Red
  case Blue
def add(n: Int): Int = n
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let all = workspace_symbols_in_source(&p, "Main.scuzz", src, "");
        let names: Vec<_> = all.iter().map(|s| s.name.as_str()).collect();
        assert!(names.contains(&"add"), "{names:?}");
        assert!(names.contains(&"Red"), "{names:?}");
        let red = all.iter().find(|s| s.name == "Red").unwrap();
        assert_eq!(red.container.as_deref(), Some("Color"));
        let hits = workspace_symbols_in_source(&p, "Main.scuzz", src, "ADD");
        assert_eq!(hits.len(), 1, "{hits:?}");
        assert_eq!(hits[0].name, "add");
        let none = workspace_symbols_in_source(&p, "Main.scuzz", src, "zzzz");
        assert!(none.is_empty(), "{none:?}");
    }
}

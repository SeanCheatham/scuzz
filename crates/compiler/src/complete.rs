//! Completions from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::hover::{show_def, show_enum, show_param, KIT_SIGS};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use std::collections::BTreeSet;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Completion {
    pub label: String,
    pub kind: u8,
    pub detail: String,
    pub insert_text: String,
}

pub const KIND_FN: u8 = 3;
pub const KIND_VAR: u8 = 6;
pub const KIND_ENUM: u8 = 13;
pub const KIND_KEYWORD: u8 = 14;
pub const KIND_MODULE: u8 = 9;
pub const KIND_TYPE: u8 = 22;
pub const KIND_MEMBER: u8 = 20;

/// Completions at a byte offset. `program` may be missing when parse fails; kits still appear.
pub fn complete_in_source(
    program: Option<&Program>,
    file: &str,
    source: &str,
    offset: usize,
) -> Vec<Completion> {
    let (qual, prefix) = prefix_at(source, offset);
    let module = module_id_from_label(file);
    let mut out = Vec::new();
    let mut seen = BTreeSet::new();
    let mut push = |c: Completion| {
        if seen.insert(c.label.clone()) {
            out.push(c);
        }
    };
    if let Some(q) = &qual {
        for (callee, sig) in KIT_SIGS {
            if let Some(method) = callee.strip_prefix(&format!("{q}.")) {
                if method.starts_with(&prefix) {
                    push(Completion {
                        label: (*callee).to_string(),
                        kind: KIND_FN,
                        detail: (*sig).to_string(),
                        insert_text: method.to_string(),
                    });
                }
            }
        }
        if let Some(p) = program {
            for d in &p.defs {
                if d.module == *q && d.name.starts_with(&prefix) && !d.is_private {
                    push(Completion {
                        label: format!("{q}.{}", d.name),
                        kind: KIND_FN,
                        detail: show_def(d),
                        insert_text: d.name.clone(),
                    });
                }
            }
            for en in &p.enums {
                if en.name == *q || (en.module == module && en.name == *q) {
                    for c in &en.cases {
                        if c.name.starts_with(&prefix) {
                            push(Completion {
                                label: format!("{}.{}", en.name, c.name),
                                kind: KIND_MEMBER,
                                detail: show_enum(en),
                                insert_text: c.name.clone(),
                            });
                        }
                    }
                }
            }
        }
        out.sort_by(|a, b| a.label.cmp(&b.label));
        return out;
    }
    for (callee, sig) in KIT_SIGS {
        let method = callee.split('.').nth(1).unwrap_or(callee);
        let kit = callee.split('.').next().unwrap_or(callee);
        if callee.starts_with(&prefix) || method.starts_with(&prefix) {
            push(Completion {
                label: (*callee).to_string(),
                kind: KIND_FN,
                detail: (*sig).to_string(),
                insert_text: (*callee).to_string(),
            });
        }
        if kit.starts_with(&prefix) {
            push(Completion {
                label: kit.to_string(),
                kind: KIND_MODULE,
                detail: format!("{kit} kit"),
                insert_text: kit.to_string(),
            });
        }
    }
    for ty in ["Int", "String", "Bool", "Unit", "List", "IO"] {
        if ty.starts_with(&prefix) {
            push(Completion {
                label: ty.to_string(),
                kind: KIND_TYPE,
                detail: ty.to_string(),
                insert_text: ty.to_string(),
            });
        }
    }
    for kw in [
        "def", "law", "match", "for", "yield", "case", "import", "enum", "record", "trait", "impl",
    ] {
        if kw.starts_with(&prefix) {
            push(Completion {
                label: kw.to_string(),
                kind: KIND_KEYWORD,
                detail: kw.to_string(),
                insert_text: kw.to_string(),
            });
        }
    }
    if let Some(p) = program {
        for d in &p.defs {
            if !d.name.starts_with(&prefix) {
                continue;
            }
            if d.module == module || !d.is_private {
                push(Completion {
                    label: d.name.clone(),
                    kind: KIND_FN,
                    detail: show_def(d),
                    insert_text: d.name.clone(),
                });
            }
        }
        for en in &p.enums {
            if en.name.starts_with(&prefix) {
                push(Completion {
                    label: en.name.clone(),
                    kind: KIND_ENUM,
                    detail: show_enum(en),
                    insert_text: en.name.clone(),
                });
            }
        }
        for d in p.defs.iter().filter(|d| d.module == module) {
            if !(d.body.span.start <= offset && offset <= d.body.span.end) {
                continue;
            }
            for param in &d.params {
                if param.name.starts_with(&prefix) {
                    push(Completion {
                        label: param.name.clone(),
                        kind: KIND_VAR,
                        detail: show_param(param),
                        insert_text: param.name.clone(),
                    });
                }
            }
        }
    }
    out.sort_by(|a, b| a.label.cmp(&b.label));
    out
}

fn prefix_at(source: &str, offset: usize) -> (Option<String>, String) {
    let Ok(toks) = lex(source) else {
        return (None, String::new());
    };
    let mut last = None;
    for (i, t) in toks.iter().enumerate() {
        if t.span.start < offset {
            last = Some(i);
        }
    }
    let Some(i) = last else {
        return (None, String::new());
    };
    let t = &toks[i];
    let qual_before_ident = |i: usize| -> Option<String> {
        if i >= 2 && matches!(toks[i - 1].token, Token::Dot) {
            if let Token::Ident(q) = &toks[i - 2].token {
                return Some(q.clone());
            }
        }
        None
    };
    if matches!(t.token, Token::Ident(_)) && t.span.start < offset && offset <= t.span.end {
        let prefix = source.get(t.span.start..offset).unwrap_or("").to_string();
        return (qual_before_ident(i), prefix);
    }
    if matches!(t.token, Token::Dot) && t.span.end <= offset {
        if i >= 1 {
            if let Token::Ident(q) = &toks[i - 1].token {
                return (Some(q.clone()), String::new());
            }
        }
    }
    (None, String::new())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn labels_at(src: &str, at: &str) -> Vec<String> {
        let program = parse_file(src, "Main.scuzz").ok();
        let offset = src.find(at).expect(at) + at.len();
        complete_in_source(program.as_ref(), "Main.scuzz", src, offset)
            .into_iter()
            .map(|c| c.label)
            .collect()
    }

    #[test]
    fn completes_io_methods_after_dot() {
        let src = "@main def main: IO[Unit] = IO.\n";
        let labels = labels_at(src, "IO.");
        assert!(labels.iter().any(|l| l == "IO.println"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "IO.sleep"), "{labels:?}");
    }

    #[test]
    fn completes_def_by_prefix() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = ad\n";
        let labels = labels_at(src, " ad");
        assert!(labels.iter().any(|l| l == "add"), "{labels:?}");
    }

    #[test]
    fn completes_enum_cases() {
        let live =
            "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let program = parse_file(live, "Main.scuzz").unwrap();
        let src = "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = Color.\n";
        let offset = src.find("Color.").unwrap() + 6;
        let labels: Vec<String> = complete_in_source(Some(&program), "Main.scuzz", src, offset)
            .into_iter()
            .map(|c| c.label)
            .collect();
        assert!(labels.iter().any(|l| l.contains("Red")), "{labels:?}");
        assert!(labels.iter().any(|l| l.contains("Blue")), "{labels:?}");
    }
}

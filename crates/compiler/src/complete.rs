//! Completions from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::hover::{kit_lambda_locals, show_def, show_enum, show_param, KIT_SIGS};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use crate::signature::{
    callee_before_paren, innermost_call_paren, param_names_from_label, sig_label,
};
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
    if let Some(p) = program {
        for c in named_arg_completions(p, &module, source, offset, &prefix) {
            push(c);
        }
    }
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
    for ty in [
        "Int", "Float", "String", "Bool", "Unit", "List", "Map", "Set", "IO",
    ] {
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
        let mut bodies: Vec<&crate::ast::Expr> = p.defs.iter().map(|d| &d.body).collect();
        bodies.push(&p.main.body);
        for body in bodies {
            for (name, ty) in kit_lambda_locals(body, offset) {
                if name.starts_with(&prefix) {
                    push(Completion {
                        label: name.clone(),
                        kind: KIND_VAR,
                        detail: format!("{name}: {ty}"),
                        insert_text: name,
                    });
                }
            }
        }
    }
    out.sort_by(|a, b| a.label.cmp(&b.label));
    out
}

fn named_arg_completions(
    program: &Program,
    module: &str,
    source: &str,
    offset: usize,
    prefix: &str,
) -> Vec<Completion> {
    let Ok(toks) = lex(source) else {
        return Vec::new();
    };
    let Some(paren) = innermost_call_paren(&toks, offset) else {
        return Vec::new();
    };
    let prev = toks.iter().rev().find(|t| t.span.end <= offset);
    if prev.is_some_and(|t| matches!(t.token, Token::Eq)) {
        return Vec::new();
    }
    let Some((qual, name)) = callee_before_paren(&toks, paren) else {
        return Vec::new();
    };
    let mut names = if let Some(label) = sig_label(program, module, qual.as_deref(), &name) {
        param_names_from_label(&label)
    } else {
        construct_fields(program, &name)
    };
    names.retain(|n| {
        let mut chars = n.chars();
        let Some(first) = chars.next() else {
            return false;
        };
        (first.is_ascii_alphabetic() || first == '_')
            && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
    });
    if names.is_empty() {
        return Vec::new();
    }
    let used = named_args_used(&toks, paren, offset);
    let mut out = Vec::new();
    for n in names {
        if used.contains(&n) {
            continue;
        }
        if !n.starts_with(prefix) {
            continue;
        }
        out.push(Completion {
            label: format!("{n} ="),
            kind: KIND_VAR,
            detail: format!("named argument `{n}`"),
            insert_text: format!("{n} = "),
        });
    }
    out
}

fn construct_fields(program: &Program, name: &str) -> Vec<String> {
    let mut hits: Vec<Vec<String>> = Vec::new();
    for en in &program.enums {
        for c in &en.cases {
            if c.name == name && !c.fields.is_empty() {
                hits.push(c.fields.iter().map(|(n, _)| n.clone()).collect());
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

fn named_args_used(
    toks: &[crate::lexer::SpannedToken],
    paren: usize,
    offset: usize,
) -> BTreeSet<String> {
    let mut used = BTreeSet::new();
    let mut depth = 1i32;
    for w in toks[paren + 1..].windows(2) {
        if w[0].span.start >= offset {
            break;
        }
        match w[0].token {
            Token::LParen | Token::LBracket | Token::LBrace => depth += 1,
            Token::RParen | Token::RBracket | Token::RBrace => {
                depth -= 1;
                if depth <= 0 {
                    break;
                }
            }
            Token::Ident(ref n) if depth == 1 && matches!(w[1].token, Token::Eq) => {
                used.insert(n.clone());
            }
            _ => {}
        }
    }
    used
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
    if matches!(t.token, Token::Dot) && t.span.end <= offset && i >= 1 {
        if let Token::Ident(q) = &toks[i - 1].token {
            return (Some(q.clone()), String::new());
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

    #[test]
    fn completes_kit_methods_after_dot() {
        for (callee, _) in KIT_SIGS {
            let kit = callee.split('.').next().unwrap();
            let src = match kit {
                "View" | "Color" | "Ui" | "Signal" => {
                    format!("@main def main: IO[Unit] = Ui.run(_ => {kit}.\n")
                }
                _ => format!("@main def main: IO[Unit] = {kit}.\n"),
            };
            let at = format!("{kit}.");
            let labels = labels_at(&src, &at);
            assert!(
                labels.iter().any(|l| l == *callee),
                "missing {callee} in {labels:?}"
            );
        }
    }

    #[test]
    fn completes_kit_prefix() {
        for (typed, expect) in [
            ("View.st", "View.stretch"),
            ("View.st", "View.stack"),
            ("List.takeW", "List.takeWhile"),
            ("List.ini", "List.init"),
            ("List.ini", "List.inits"),
            ("List.zi", "List.zip"),
            ("List.zi", "List.zipAll"),
            ("List.un", "List.unzip"),
            ("List.tr", "List.transpose"),
            ("List.con", "List.contains"),
            ("List.ind", "List.indexOf"),
            ("List.lastI", "List.lastIndexOf"),
            ("List.di", "List.distinct"),
            ("List.di", "List.diff"),
            ("List.int", "List.intersect"),
            ("List.st", "List.startsWith"),
            ("List.en", "List.endsWith"),
            ("List.sa", "List.sameElements"),
            ("List.pa", "List.patch"),
            ("List.findL", "List.findLast"),
            ("List.pr", "List.prefixLength"),
            ("List.indexOfS", "List.indexOfSlice"),
            ("List.lastIndexOfS", "List.lastIndexOfSlice"),
            ("List.seg", "List.segmentLength"),
            ("List.isD", "List.isDefinedAt"),
            ("List.lengthC", "List.lengthCompare"),
            ("Color.rgb", "Color.rgb"),
            ("Color.rgb", "Color.rgba"),
        ] {
            let src = if typed.starts_with("View.") || typed.starts_with("Color.") {
                format!("@main def main: IO[Unit] = Ui.run(_ => {typed}\n")
            } else {
                format!("@main def main: IO[Unit] = {typed}\n")
            };
            let labels = labels_at(&src, typed);
            assert!(
                labels.iter().any(|l| l == expect),
                "{typed} missing {expect} in {labels:?}"
            );
        }
    }

    #[test]
    fn completes_view_each_row_param() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, row => View.text(row)))
  } yield ()
"#;
        let program = parse_file(src, "Main.scuzz").unwrap();
        let at = "View.text(ro";
        let offset = src.find(at).unwrap() + at.len();
        let hits: Vec<_> = complete_in_source(Some(&program), "Main.scuzz", src, offset);
        assert!(
            hits.iter()
                .any(|c| c.label == "row" && c.detail.contains("row: String")),
            "{hits:?}"
        );
    }

    #[test]
    fn completes_named_args() {
        let src = r#"def add(n: Int, m: Int): Int = n
@main def main: IO[Unit] = IO.println(Str.fromInt(add()))
"#;
        let labels = labels_at(src, "fromInt(add(");
        assert!(labels.iter().any(|l| l == "n ="), "{labels:?}");
        assert!(labels.iter().any(|l| l == "m ="), "{labels:?}");
        let src2 = r#"def add(n: Int, m: Int): Int = n
@main def main: IO[Unit] = IO.println(Str.fromInt(add(n = 1, )))
"#;
        let labels2 = labels_at(src2, "n = 1, ");
        assert!(labels2.iter().any(|l| l == "m ="), "{labels2:?}");
        assert!(!labels2.iter().any(|l| l == "n ="), "{labels2:?}");
    }

    #[test]
    fn completes_record_copy_fields() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy()))
"#;
        let labels = labels_at(src, ".copy(");
        assert!(labels.iter().any(|l| l == "x ="), "{labels:?}");
        assert!(labels.iter().any(|l| l == "y ="), "{labels:?}");
        let src2 = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy(x = 1, )))
"#;
        let labels2 = labels_at(src2, "x = 1, ");
        assert!(labels2.iter().any(|l| l == "y ="), "{labels2:?}");
        assert!(!labels2.iter().any(|l| l == "x ="), "{labels2:?}");
    }
}

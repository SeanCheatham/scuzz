//! Signature help from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::hover::{def_named, kit_sig, show_def, unique_def, KIT_SIGS};
use crate::lexer::{lex, SpannedToken, Token};
use crate::resolve::module_id_from_label;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SigHelp {
    pub label: String,
    pub parameters: Vec<String>,
    pub active_parameter: u32,
}

/// Signature of the innermost call that covers `offset`.
pub fn signature_help_in_source(
    program: &Program,
    current_file: &str,
    source: &str,
    offset: usize,
) -> Option<SigHelp> {
    let toks = lex(source).ok()?;
    let paren = innermost_call_paren(&toks, offset)?;
    let (qual, name) = callee_before_paren(&toks, paren)?;
    let module = module_id_from_label(current_file);
    let label = sig_label(program, &module, qual.as_deref(), &name)?;
    let parameters = params_from_label(&label);
    let commas = commas_before(&toks, paren, offset);
    let active = if parameters.is_empty() {
        0
    } else {
        commas.min(parameters.len().saturating_sub(1) as u32)
    };
    Some(SigHelp {
        label,
        parameters,
        active_parameter: active,
    })
}

pub(crate) fn innermost_call_paren(toks: &[SpannedToken], offset: usize) -> Option<usize> {
    let mut best = None;
    for (i, t) in toks.iter().enumerate() {
        if !matches!(t.token, Token::LParen) || t.span.start > offset {
            continue;
        }
        let mut depth = 1usize;
        let mut end = None;
        for u in &toks[i + 1..] {
            match u.token {
                Token::LParen => depth += 1,
                Token::RParen => {
                    depth -= 1;
                    if depth == 0 {
                        end = Some(u.span.start);
                        break;
                    }
                }
                _ => {}
            }
        }
        let covers = match end {
            Some(e) => offset <= e,
            None => true,
        };
        if covers {
            best = Some(i);
        }
    }
    best
}

pub(crate) fn callee_before_paren(
    toks: &[SpannedToken],
    paren: usize,
) -> Option<(Option<String>, String)> {
    if paren == 0 {
        return None;
    }
    let Token::Ident(name) = &toks[paren - 1].token else {
        return None;
    };
    if paren >= 3
        && matches!(toks[paren - 2].token, Token::Dot)
        && matches!(&toks[paren - 3].token, Token::Ident(_))
    {
        if let Token::Ident(q) = &toks[paren - 3].token {
            return Some((Some(q.clone()), name.clone()));
        }
    }
    Some((None, name.clone()))
}

pub(crate) fn sig_label(
    program: &Program,
    module: &str,
    qual: Option<&str>,
    name: &str,
) -> Option<String> {
    if let Some(q) = qual {
        let callee = format!("{q}.{name}");
        if let Some(s) = kit_sig(&callee) {
            return Some(s.to_string());
        }
        if let Some(d) = def_named(program, q, name) {
            return Some(show_def(d));
        }
    }
    if let Some(d) = def_named(program, module, name).or_else(|| unique_def(program, name)) {
        return Some(show_def(d));
    }
    if name == "copy" {
        return copy_sig_label(program);
    }
    unique_kit_suffix(name).map(str::to_string)
}

fn record_like(en: &crate::ast::EnumDef) -> bool {
    en.is_record || (en.cases.len() == 1 && en.cases[0].name == en.name)
}

fn copy_sig_label(program: &Program) -> Option<String> {
    let mut names: Vec<String> = Vec::new();
    for en in &program.enums {
        if !record_like(en) {
            continue;
        }
        let Some(case) = en.cases.first() else {
            continue;
        };
        for (n, _) in &case.fields {
            if !names.iter().any(|k| k == n) {
                names.push(n.clone());
            }
        }
    }
    if names.is_empty() {
        return Some("copy(field = value, …): T".into());
    }
    Some(format!("copy({}): T", names.join(", ")))
}

fn unique_kit_suffix(name: &str) -> Option<&'static str> {
    let suf = format!(".{name}");
    let hits: Vec<_> = KIT_SIGS
        .iter()
        .filter(|(k, _)| *k == name || k.ends_with(&suf))
        .map(|(_, s)| *s)
        .collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

pub(crate) fn param_names_from_label(label: &str) -> Vec<String> {
    params_from_label(label)
        .into_iter()
        .map(|p| p.split(':').next().unwrap_or("").trim().to_string())
        .filter(|s| !s.is_empty())
        .collect()
}

pub(crate) fn params_from_label(label: &str) -> Vec<String> {
    let Some(start) = label.find('(') else {
        return Vec::new();
    };
    let bytes = label.as_bytes();
    let mut depth = 0i32;
    let mut end = None;
    for (i, &b) in bytes.iter().enumerate().skip(start) {
        match b {
            b'(' => depth += 1,
            b')' => {
                depth -= 1;
                if depth == 0 {
                    end = Some(i);
                    break;
                }
            }
            _ => {}
        }
    }
    let Some(end) = end else {
        return Vec::new();
    };
    let inner = &label[start + 1..end];
    if inner.trim().is_empty() {
        return Vec::new();
    }
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut d = 0i32;
    for c in inner.chars() {
        match c {
            '(' | '[' => {
                d += 1;
                cur.push(c);
            }
            ')' | ']' => {
                d -= 1;
                cur.push(c);
            }
            ',' if d == 0 => {
                let p = cur.trim();
                if !p.is_empty() {
                    out.push(p.to_string());
                }
                cur.clear();
            }
            _ => cur.push(c),
        }
    }
    let p = cur.trim();
    if !p.is_empty() {
        out.push(p.to_string());
    }
    out
}

fn commas_before(toks: &[SpannedToken], paren: usize, offset: usize) -> u32 {
    let mut depth = 1i32;
    let mut n = 0u32;
    for t in &toks[paren + 1..] {
        if t.span.start >= offset {
            break;
        }
        match t.token {
            Token::LParen | Token::LBracket | Token::LBrace => depth += 1,
            Token::RParen | Token::RBracket | Token::RBrace => depth -= 1,
            Token::Comma if depth == 1 => n += 1,
            _ => {}
        }
    }
    n
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    #[test]
    fn helps_kit_and_user_def() {
        let src = r#"def add(n: Int, m: Int): Int = n
@main def main: IO[Unit] = IO.println(Str.fromInt(add(1, 2)))
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let at = src.find("add(1").unwrap() + 4;
        let h = signature_help_in_source(&p, "Main.scuzz", src, at).unwrap();
        assert!(h.label.contains("def add(n: Int, m: Int): Int"), "{h:?}");
        assert_eq!(h.parameters, ["n: Int", "m: Int"]);
        assert_eq!(h.active_parameter, 0);
        let second = src.find(", 2").unwrap();
        let h = signature_help_in_source(&p, "Main.scuzz", src, second + 1).unwrap();
        assert_eq!(h.active_parameter, 1);
        let kit = src.find("println(").unwrap() + 8;
        let h = signature_help_in_source(&p, "Main.scuzz", src, kit).unwrap();
        assert!(h.label.contains("IO.println"), "{h:?}");
        assert_eq!(h.parameters.len(), 1, "{h:?}");
    }

    #[test]
    fn helps_record_copy() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy(y = 9).x))
"#;
        let p = parse_file(src, "Main.scuzz").unwrap();
        let at = src.find("copy(").unwrap() + 5;
        let h = signature_help_in_source(&p, "Main.scuzz", src, at).unwrap();
        assert!(h.label.contains("copy("), "{h:?}");
        assert!(h.parameters.iter().any(|p| p.contains("x")), "{h:?}");
        assert!(h.parameters.iter().any(|p| p.contains("y")), "{h:?}");
    }

    #[test]
    fn skips_if_and_unknown_callee() {
        let src = "@main def main: IO[Unit] = if (true) IO.println(\"x\") else IO.println(\"y\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let at = src.find("(true)").unwrap();
        assert!(signature_help_in_source(&p, "Main.scuzz", src, at + 1).is_none());
        let at = src.find("true)").unwrap();
        assert!(signature_help_in_source(&p, "Main.scuzz", src, at).is_none());
    }
}

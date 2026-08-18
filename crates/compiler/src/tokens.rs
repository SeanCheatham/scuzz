//! Semantic tokens from the same parse as `check`. No second typer.

use crate::lexer::{lex, SpannedToken, Token};
use crate::span::offset_to_utf16_pos;

pub const TOKEN_TYPES: &[&str] = &[
    "namespace",
    "type",
    "enum",
    "enumMember",
    "function",
    "method",
    "parameter",
    "variable",
    "keyword",
    "string",
    "number",
    "operator",
    "modifier",
];

pub const TOKEN_MODIFIERS: &[&str] = &["declaration"];
pub const MOD_DECLARATION: u32 = 1;

const TY_NAMESPACE: u32 = 0;
const TY_TYPE: u32 = 1;
const TY_ENUM: u32 = 2;
const TY_ENUM_MEMBER: u32 = 3;
const TY_FUNCTION: u32 = 4;
const TY_PARAMETER: u32 = 6;
const TY_VARIABLE: u32 = 7;
const TY_KEYWORD: u32 = 8;
const TY_STRING: u32 = 9;
const TY_NUMBER: u32 = 10;
const TY_OPERATOR: u32 = 11;
const TY_MODIFIER: u32 = 12;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SemToken {
    pub start: usize,
    pub end: usize,
    pub ty: u32,
    pub modifiers: u32,
}

/// Lexer tokens in `source`, classified for `textDocument/semanticTokens/full`.
pub fn semantic_tokens_in_source(source: &str) -> Vec<SemToken> {
    let Ok(toks) = lex(source) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for (i, t) in toks.iter().enumerate() {
        if matches!(t.token, Token::Eof) {
            continue;
        }
        let prev = i.checked_sub(1).and_then(|j| toks.get(j));
        let next = toks.get(i + 1);
        if let Some((ty, modifiers)) = classify(&t.token, prev, next) {
            push_lines(source, t.span.start, t.span.end, ty, modifiers, &mut out);
        }
    }
    out
}

/// LSP packed deltas: deltaLine, deltaStart, length, tokenType, tokenModifiers.
pub fn encode_semantic_tokens(source: &str, toks: &[SemToken]) -> Vec<u32> {
    let mut data = Vec::with_capacity(toks.len() * 5);
    let mut prev_line = 0u32;
    let mut prev_start = 0u32;
    for t in toks {
        let (line, start) = offset_to_utf16_pos(source, t.start);
        let (_, end_col) = offset_to_utf16_pos(source, t.end);
        let length = end_col.saturating_sub(start);
        if length == 0 {
            continue;
        }
        let delta_line = line.saturating_sub(prev_line);
        let delta_start = if delta_line == 0 {
            start.saturating_sub(prev_start)
        } else {
            start
        };
        data.push(delta_line);
        data.push(delta_start);
        data.push(length);
        data.push(t.ty);
        data.push(t.modifiers);
        prev_line = line;
        prev_start = start;
    }
    data
}

fn push_lines(
    source: &str,
    start: usize,
    end: usize,
    ty: u32,
    modifiers: u32,
    out: &mut Vec<SemToken>,
) {
    let end = end.min(source.len());
    let mut cur = start.min(end);
    while cur < end {
        let rest = &source[cur..end];
        let take = rest.find('\n').unwrap_or(rest.len());
        if take > 0 {
            out.push(SemToken {
                start: cur,
                end: cur + take,
                ty,
                modifiers,
            });
        }
        cur += take;
        if cur < end && source.as_bytes().get(cur) == Some(&b'\n') {
            cur += 1;
        }
    }
}

fn classify(
    tok: &Token,
    prev: Option<&SpannedToken>,
    next: Option<&SpannedToken>,
) -> Option<(u32, u32)> {
    match tok {
        Token::Private => Some((TY_MODIFIER, 0)),
        Token::AtMain
        | Token::Package
        | Token::Enum
        | Token::Record
        | Token::Trait
        | Token::Impl
        | Token::Case
        | Token::Match
        | Token::Def
        | Token::Law
        | Token::Where
        | Token::Import
        | Token::For
        | Token::Yield
        | Token::If
        | Token::Else
        | Token::True
        | Token::False => Some((TY_KEYWORD, 0)),
        Token::StringLit(_) | Token::InterpString(_) => Some((TY_STRING, 0)),
        Token::IntLit(_) | Token::FloatLit(_) => Some((TY_NUMBER, 0)),
        Token::Plus
        | Token::Minus
        | Token::Star
        | Token::Slash
        | Token::Percent
        | Token::EqEq
        | Token::BangEq
        | Token::Lt
        | Token::LtEq
        | Token::Gt
        | Token::GtEq
        | Token::AmpAmp
        | Token::PipePipe
        | Token::Arrow
        | Token::LeftArrow
        | Token::Dot
        | Token::Eq => Some((TY_OPERATOR, 0)),
        Token::Ident(name) => Some(classify_ident(name, prev, next)),
        _ => None,
    }
}

fn classify_ident(
    name: &str,
    prev: Option<&SpannedToken>,
    next: Option<&SpannedToken>,
) -> (u32, u32) {
    let prev_tok = prev.map(|t| &t.token);
    let next_tok = next.map(|t| &t.token);
    match prev_tok {
        Some(Token::Def) | Some(Token::Law) => return (TY_FUNCTION, MOD_DECLARATION),
        Some(Token::Enum) | Some(Token::Record) => return (TY_ENUM, MOD_DECLARATION),
        Some(Token::Trait) => return (TY_TYPE, MOD_DECLARATION),
        Some(Token::Case) => return (TY_ENUM_MEMBER, MOD_DECLARATION),
        Some(Token::Package) | Some(Token::Import) => return (TY_NAMESPACE, 0),
        Some(Token::Colon) => return (TY_TYPE, 0),
        Some(Token::LParen) | Some(Token::Comma) if matches!(next_tok, Some(Token::Colon)) => {
            return (TY_PARAMETER, MOD_DECLARATION);
        }
        _ => {}
    }
    if matches!(
        name,
        "Int" | "Float" | "String" | "Bool" | "Unit" | "List" | "Map" | "Set" | "IO"
    ) {
        return (TY_TYPE, 0);
    }
    if matches!(next_tok, Some(Token::LParen)) {
        return (TY_FUNCTION, 0);
    }
    (TY_VARIABLE, 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_def_param_call_and_lits() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let toks = semantic_tokens_in_source(src);
        assert!(
            toks.iter()
                .any(|t| src[t.start..t.end] == *"def" && t.ty == TY_KEYWORD),
            "{toks:?}"
        );
        let add = toks
            .iter()
            .find(|t| src[t.start..t.end] == *"add")
            .expect("add");
        assert_eq!(add.ty, TY_FUNCTION);
        assert_eq!(add.modifiers, MOD_DECLARATION);
        let n = toks
            .iter()
            .find(|t| src[t.start..t.end] == *"n" && t.start == src.find("n:").unwrap())
            .expect("param n");
        assert_eq!(n.ty, TY_PARAMETER);
        assert!(
            toks.iter()
                .any(|t| src[t.start..t.end] == *"Int" && t.ty == TY_TYPE),
            "{toks:?}"
        );
        assert!(
            toks.iter()
                .any(|t| src[t.start..t.end] == *"println" && t.ty == TY_FUNCTION),
            "{toks:?}"
        );
        assert!(
            toks.iter()
                .any(|t| src[t.start..t.end].contains('x') && t.ty == TY_STRING),
            "{toks:?}"
        );
        assert!(TOKEN_TYPES[TY_KEYWORD as usize] == "keyword");
        assert!(TOKEN_MODIFIERS[0] == "declaration");
    }

    #[test]
    fn encodes_relative_deltas() {
        let src = "def add(n: Int): Int = n\n";
        let toks = semantic_tokens_in_source(src);
        let data = encode_semantic_tokens(src, &toks);
        assert!(data.len() % 5 == 0 && !data.is_empty(), "{data:?}");
        assert_eq!(data[0], 0);
        assert_eq!(data.len(), toks.len() * 5);
        assert!(toks.iter().any(|t| t.ty == TY_KEYWORD));
    }

    #[test]
    fn classifies_enum_case_and_private() {
        let src = "private enum Color:\n  case Red\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let toks = semantic_tokens_in_source(src);
        assert!(
            toks.iter()
                .any(|t| src[t.start..t.end] == *"private" && t.ty == TY_MODIFIER),
            "{toks:?}"
        );
        let color = toks
            .iter()
            .find(|t| src[t.start..t.end] == *"Color")
            .expect("Color");
        assert_eq!(color.ty, TY_ENUM);
        let red = toks
            .iter()
            .find(|t| src[t.start..t.end] == *"Red")
            .expect("Red");
        assert_eq!(red.ty, TY_ENUM_MEMBER);
        assert_eq!(red.modifiers, MOD_DECLARATION);
    }

    #[test]
    fn skips_unlexable_source() {
        let toks = semantic_tokens_in_source("@@@");
        assert!(toks.is_empty(), "{toks:?}");
    }
}

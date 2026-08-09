use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Token {
    AtMain, // @main
    Package,
    Enum,
    Case,
    Match,
    Def,
    Val,
    If,
    Else,
    Colon,
    Eq,
    Dot,
    Comma,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Arrow, // =>
    Underscore,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    AmpAmp,
    PipePipe,
    Ident(String),
    StringLit(String),
    /// `s"..."` fragments: lit / `$ident` / `${...}` raw source for the parser.
    InterpString(Vec<InterpTok>),
    IntLit(i64),
    Eof,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum InterpTok {
    Lit(String),
    Ident(String),
    /// Raw `${...}` body source (re-lexed by the parser).
    Brace(String),
}

#[derive(Debug, Error)]
pub enum LexError {
    #[error("unexpected character {0:?} at byte offset {1}")]
    UnexpectedChar(char, usize),
    #[error("unterminated string at byte offset {0}")]
    UnterminatedString(usize),
    #[error("bad string interpolation at byte offset {0}")]
    BadInterpolation(usize),
}

pub fn lex(input: &str) -> Result<Vec<Token>, LexError> {
    let mut tokens = Vec::new();
    let chars: Vec<char> = input.chars().collect();
    let mut i = 0usize;

    while i < chars.len() {
        let c = chars[i];
        if c.is_whitespace() {
            i += 1;
            continue;
        }
        if c == '/' && i + 1 < chars.len() && chars[i + 1] == '/' {
            i += 2;
            while i < chars.len() && chars[i] != '\n' {
                i += 1;
            }
            continue;
        }
        if c == '@' {
            let start = i;
            i += 1;
            let mut ident = String::new();
            while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                ident.push(chars[i]);
                i += 1;
            }
            if ident == "main" {
                tokens.push(Token::AtMain);
            } else {
                return Err(LexError::UnexpectedChar('@', start));
            }
            continue;
        }
        if c == '"' {
            let (s, next) = read_string_lit(&chars, i)?;
            i = next;
            tokens.push(Token::StringLit(s));
            continue;
        }
        if c == '=' && i + 1 < chars.len() && chars[i + 1] == '>' {
            tokens.push(Token::Arrow);
            i += 2;
            continue;
        }
        if c == '=' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(Token::EqEq);
            i += 2;
            continue;
        }
        if c == '!' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(Token::BangEq);
            i += 2;
            continue;
        }
        if c == '<' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(Token::LtEq);
            i += 2;
            continue;
        }
        if c == '>' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(Token::GtEq);
            i += 2;
            continue;
        }
        if c == '&' && i + 1 < chars.len() && chars[i + 1] == '&' {
            tokens.push(Token::AmpAmp);
            i += 2;
            continue;
        }
        if c == '|' && i + 1 < chars.len() && chars[i + 1] == '|' {
            tokens.push(Token::PipePipe);
            i += 2;
            continue;
        }
        if c.is_ascii_digit() {
            let mut n = 0i64;
            while i < chars.len() && chars[i].is_ascii_digit() {
                n = n
                    .saturating_mul(10)
                    .saturating_add((chars[i] as u8 - b'0') as i64);
                i += 1;
            }
            tokens.push(Token::IntLit(n));
            continue;
        }
        match c {
            ':' => {
                tokens.push(Token::Colon);
                i += 1;
            }
            '=' => {
                tokens.push(Token::Eq);
                i += 1;
            }
            '.' => {
                tokens.push(Token::Dot);
                i += 1;
            }
            ',' => {
                tokens.push(Token::Comma);
                i += 1;
            }
            '(' => {
                tokens.push(Token::LParen);
                i += 1;
            }
            ')' => {
                tokens.push(Token::RParen);
                i += 1;
            }
            '{' => {
                tokens.push(Token::LBrace);
                i += 1;
            }
            '}' => {
                tokens.push(Token::RBrace);
                i += 1;
            }
            '[' => {
                tokens.push(Token::LBracket);
                i += 1;
            }
            ']' => {
                tokens.push(Token::RBracket);
                i += 1;
            }
            '+' => {
                tokens.push(Token::Plus);
                i += 1;
            }
            '-' => {
                tokens.push(Token::Minus);
                i += 1;
            }
            '*' => {
                tokens.push(Token::Star);
                i += 1;
            }
            '/' => {
                tokens.push(Token::Slash);
                i += 1;
            }
            '%' => {
                tokens.push(Token::Percent);
                i += 1;
            }
            '<' => {
                tokens.push(Token::Lt);
                i += 1;
            }
            '>' => {
                tokens.push(Token::Gt);
                i += 1;
            }
            '_' => {
                tokens.push(Token::Underscore);
                i += 1;
            }
            ch if ch.is_ascii_alphabetic() => {
                let mut ident = String::new();
                while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                    ident.push(chars[i]);
                    i += 1;
                }
                if ident == "s" && i < chars.len() && chars[i] == '"' {
                    let (parts, next) = read_interp_string(&chars, i)?;
                    i = next;
                    tokens.push(Token::InterpString(parts));
                } else {
                    tokens.push(match ident.as_str() {
                        "package" => Token::Package,
                        "enum" => Token::Enum,
                        "case" => Token::Case,
                        "match" => Token::Match,
                        "def" => Token::Def,
                        "val" => Token::Val,
                        "if" => Token::If,
                        "else" => Token::Else,
                        _ => Token::Ident(ident),
                    });
                }
            }
            other => return Err(LexError::UnexpectedChar(other, i)),
        }
    }
    tokens.push(Token::Eof);
    Ok(tokens)
}

fn read_string_lit(chars: &[char], start_quote: usize) -> Result<(String, usize), LexError> {
    let start = start_quote;
    let mut i = start_quote + 1;
    let mut s = String::new();
    while i < chars.len() {
        let ch = chars[i];
        if ch == '"' {
            return Ok((s, i + 1));
        }
        if ch == '\\' {
            i += 1;
            if i >= chars.len() {
                return Err(LexError::UnterminatedString(start));
            }
            let esc = chars[i];
            s.push(match esc {
                'n' => '\n',
                't' => '\t',
                '\\' => '\\',
                '"' => '"',
                '$' => '$',
                other => other,
            });
            i += 1;
            continue;
        }
        s.push(ch);
        i += 1;
    }
    Err(LexError::UnterminatedString(start))
}

fn read_interp_string(chars: &[char], start_quote: usize) -> Result<(Vec<InterpTok>, usize), LexError> {
    let start = start_quote;
    let mut i = start_quote + 1;
    let mut parts = Vec::new();
    let mut lit = String::new();
    while i < chars.len() {
        let ch = chars[i];
        if ch == '"' {
            parts.push(InterpTok::Lit(std::mem::take(&mut lit)));
            let parts = compact_interp_parts(parts);
            return Ok((parts, i + 1));
        }
        if ch == '\\' {
            i += 1;
            if i >= chars.len() {
                return Err(LexError::UnterminatedString(start));
            }
            let esc = chars[i];
            lit.push(match esc {
                'n' => '\n',
                't' => '\t',
                '\\' => '\\',
                '"' => '"',
                '$' => '$',
                other => other,
            });
            i += 1;
            continue;
        }
        if ch == '$' {
            if !lit.is_empty() {
                parts.push(InterpTok::Lit(std::mem::take(&mut lit)));
            }
            i += 1;
            if i >= chars.len() {
                return Err(LexError::BadInterpolation(start));
            }
            if chars[i] == '{' {
                i += 1;
                let body_start = i;
                let mut depth = 1usize;
                while i < chars.len() {
                    if chars[i] == '{' {
                        depth += 1;
                    } else if chars[i] == '}' {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                    i += 1;
                }
                if i >= chars.len() || chars[i] != '}' {
                    return Err(LexError::BadInterpolation(start));
                }
                let body: String = chars[body_start..i].iter().collect();
                parts.push(InterpTok::Brace(body));
                i += 1;
                continue;
            }
            if chars[i].is_ascii_alphabetic() || chars[i] == '_' {
                let mut name = String::new();
                while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                    name.push(chars[i]);
                    i += 1;
                }
                parts.push(InterpTok::Ident(name));
                continue;
            }
            return Err(LexError::BadInterpolation(start));
        }
        lit.push(ch);
        i += 1;
    }
    Err(LexError::UnterminatedString(start))
}

fn compact_interp_parts(parts: Vec<InterpTok>) -> Vec<InterpTok> {
    if parts.len() == 1 {
        return parts;
    }
    parts
        .into_iter()
        .filter(|p| !matches!(p, InterpTok::Lit(s) if s.is_empty()))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lexes_hello() {
        let src = r#"@main def main: IO[Unit] = IO.println("hi")"#;
        let toks = lex(src).unwrap();
        assert!(matches!(toks[0], Token::AtMain));
        assert!(matches!(toks[1], Token::Def));
        assert!(toks
            .iter()
            .any(|t| matches!(t, Token::StringLit(s) if s == "hi")));
    }

    #[test]
    fn lexes_ops_and_if() {
        let src = r#"if (n == 0) a + b else c"#;
        let toks = lex(src).unwrap();
        assert!(toks.iter().any(|t| matches!(t, Token::If)));
        assert!(toks.iter().any(|t| matches!(t, Token::EqEq)));
        assert!(toks.iter().any(|t| matches!(t, Token::Plus)));
        assert!(toks.iter().any(|t| matches!(t, Token::Else)));
    }

    #[test]
    fn lexes_interpolated_string() {
        let toks = lex(r#"s"real:$t""#).unwrap();
        match &toks[0] {
            Token::InterpString(parts) => {
                assert_eq!(
                    parts,
                    &vec![InterpTok::Lit("real:".into()), InterpTok::Ident("t".into())]
                );
            }
            other => panic!("expected InterpString, got {other:?}"),
        }
    }
}

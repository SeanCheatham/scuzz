use crate::span::Span;
use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Token {
    AtMain, // @main
    Package,
    Enum,
    Case,
    Match,
    Def,
    Private,
    Import,
    For,
    Yield,
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
    LeftArrow, // <-
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
pub struct SpannedToken {
    pub token: Token,
    pub span: Span,
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

impl LexError {
    pub fn offset(&self) -> usize {
        match self {
            LexError::UnexpectedChar(_, o)
            | LexError::UnterminatedString(o)
            | LexError::BadInterpolation(o) => *o,
        }
    }
}

pub fn lex(input: &str) -> Result<Vec<SpannedToken>, LexError> {
    let mut tokens = Vec::new();
    let chars: Vec<char> = input.chars().collect();
    // char index → byte offset; last entry is `input.len()` for end-of-input.
    let mut char_byte: Vec<usize> = input.char_indices().map(|(b, _)| b).collect();
    char_byte.push(input.len());
    let byte_at = |ci: usize| char_byte[ci.min(char_byte.len() - 1)];

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
                tokens.push(SpannedToken {
                    token: Token::AtMain,
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            } else {
                return Err(LexError::UnexpectedChar('@', byte_at(start)));
            }
            continue;
        }
        if c == '"' {
            let start = i;
            let (s, next) = read_string_lit(&chars, i, &char_byte)?;
            i = next;
            tokens.push(SpannedToken {
                token: Token::StringLit(s),
                span: Span::new(String::new(), byte_at(start), byte_at(i)),
            });
            continue;
        }
        if c == '=' && i + 1 < chars.len() && chars[i + 1] == '>' {
            tokens.push(SpannedToken {
                token: Token::Arrow,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '=' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(SpannedToken {
                token: Token::EqEq,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '!' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(SpannedToken {
                token: Token::BangEq,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '<' && i + 1 < chars.len() && chars[i + 1] == '-' {
            tokens.push(SpannedToken {
                token: Token::LeftArrow,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '<' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(SpannedToken {
                token: Token::LtEq,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '>' && i + 1 < chars.len() && chars[i + 1] == '=' {
            tokens.push(SpannedToken {
                token: Token::GtEq,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '&' && i + 1 < chars.len() && chars[i + 1] == '&' {
            tokens.push(SpannedToken {
                token: Token::AmpAmp,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c == '|' && i + 1 < chars.len() && chars[i + 1] == '|' {
            tokens.push(SpannedToken {
                token: Token::PipePipe,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
            });
            i += 2;
            continue;
        }
        if c.is_ascii_digit() {
            let start = i;
            let mut n = 0i64;
            while i < chars.len() && chars[i].is_ascii_digit() {
                n = n
                    .saturating_mul(10)
                    .saturating_add((chars[i] as u8 - b'0') as i64);
                i += 1;
            }
            tokens.push(SpannedToken {
                token: Token::IntLit(n),
                span: Span::new(String::new(), byte_at(start), byte_at(i)),
            });
            continue;
        }
        match c {
            ':' | '=' | '.' | ',' | '(' | ')' | '{' | '}' | '[' | ']' | '+' | '-' | '*' | '/'
            | '%' | '<' | '>' | '_' => {
                let start = i;
                let token = match c {
                    ':' => Token::Colon,
                    '=' => Token::Eq,
                    '.' => Token::Dot,
                    ',' => Token::Comma,
                    '(' => Token::LParen,
                    ')' => Token::RParen,
                    '{' => Token::LBrace,
                    '}' => Token::RBrace,
                    '[' => Token::LBracket,
                    ']' => Token::RBracket,
                    '+' => Token::Plus,
                    '-' => Token::Minus,
                    '*' => Token::Star,
                    '/' => Token::Slash,
                    '%' => Token::Percent,
                    '<' => Token::Lt,
                    '>' => Token::Gt,
                    '_' => Token::Underscore,
                    _ => unreachable!(),
                };
                i += 1;
                tokens.push(SpannedToken {
                    token,
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            }
            ch if ch.is_ascii_alphabetic() => {
                let start = i;
                let mut ident = String::new();
                while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                    ident.push(chars[i]);
                    i += 1;
                }
                if ident == "s" && i < chars.len() && chars[i] == '"' {
                    let (parts, next) = read_interp_string(&chars, i, &char_byte)?;
                    i = next;
                    tokens.push(SpannedToken {
                        token: Token::InterpString(parts),
                        span: Span::new(String::new(), byte_at(start), byte_at(i)),
                    });
                } else {
                    let token = match ident.as_str() {
                        "package" => Token::Package,
                        "enum" => Token::Enum,
                        "case" => Token::Case,
                        "match" => Token::Match,
                        "def" => Token::Def,
                        "private" => Token::Private,
                        "import" => Token::Import,
                        "for" => Token::For,
                        "yield" => Token::Yield,
                        "if" => Token::If,
                        "else" => Token::Else,
                        _ => Token::Ident(ident),
                    };
                    tokens.push(SpannedToken {
                        token,
                        span: Span::new(String::new(), byte_at(start), byte_at(i)),
                    });
                }
            }
            other => return Err(LexError::UnexpectedChar(other, byte_at(i))),
        }
    }
    tokens.push(SpannedToken {
        token: Token::Eof,
        span: Span::new(String::new(), input.len(), input.len()),
    });
    Ok(tokens)
}

fn byte_at_chars(char_byte: &[usize], ci: usize) -> usize {
    char_byte[ci.min(char_byte.len() - 1)]
}

fn read_string_lit(
    chars: &[char],
    start_quote: usize,
    char_byte: &[usize],
) -> Result<(String, usize), LexError> {
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
                return Err(LexError::UnterminatedString(byte_at_chars(char_byte, start)));
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
    Err(LexError::UnterminatedString(byte_at_chars(char_byte, start)))
}

fn read_interp_string(
    chars: &[char],
    start_quote: usize,
    char_byte: &[usize],
) -> Result<(Vec<InterpTok>, usize), LexError> {
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
                return Err(LexError::UnterminatedString(byte_at_chars(char_byte, start)));
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
                return Err(LexError::BadInterpolation(byte_at_chars(char_byte, start)));
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
                    return Err(LexError::BadInterpolation(byte_at_chars(char_byte, start)));
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
            return Err(LexError::BadInterpolation(byte_at_chars(char_byte, start)));
        }
        lit.push(ch);
        i += 1;
    }
    Err(LexError::UnterminatedString(byte_at_chars(char_byte, start)))
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
        assert!(matches!(toks[0].token, Token::AtMain));
        assert!(matches!(toks[1].token, Token::Def));
        assert!(toks
            .iter()
            .any(|t| matches!(&t.token, Token::StringLit(s) if s == "hi")));
        assert_eq!(toks[0].span.start, 0);
        assert!(toks[0].span.end > 0);
    }

    #[test]
    fn lexes_ops_and_if() {
        let src = r#"if (n == 0) a + b else c"#;
        let toks = lex(src).unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::If)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::EqEq)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Plus)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Else)));
    }

    #[test]
    fn lexes_interpolated_string() {
        let toks = lex(r#"s"real:$t""#).unwrap();
        match &toks[0].token {
            Token::InterpString(parts) => {
                assert_eq!(
                    parts,
                    &vec![InterpTok::Lit("real:".into()), InterpTok::Ident("t".into())]
                );
            }
            other => panic!("expected InterpString, got {other:?}"),
        }
    }

    #[test]
    fn lexes_for_yield_left_arrow() {
        let toks = lex(r#"for { x = 1 y <- e } yield x"#).unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::For)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Yield)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::LeftArrow)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Eq)));
    }

    #[test]
    fn span_line_for_second_line_token() {
        let src = "def a: Int = 1\n@main def main: IO[Unit] = IO.println(\"x\")";
        let toks = lex(src).unwrap();
        let at_main = toks.iter().find(|t| matches!(t.token, Token::AtMain)).unwrap();
        let (line, _) = crate::span::offset_to_line_col(src, at_main.span.start);
        assert_eq!(line, 2);
    }
}

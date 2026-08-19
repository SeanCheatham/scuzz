use crate::span::Span;
use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Token {
    AtMain, // @main
    At,     // `@` in `n @ Pat`
    Package,
    Enum,
    Record,
    Trait,
    Impl,
    Case,
    Match,
    Def,
    Law,
    Where,
    Private,
    Import,
    For,
    Yield,
    If,
    Else,
    True,
    False,
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
    Arrow,     // =>
    LeftArrow, // <-
    Underscore,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    BangEq,
    Bang,
    Lt,
    LtEq,
    Gt,
    GtEq,
    AmpAmp,
    Amp,
    Pipe, // `|` in `case A | B` and bitwise or
    PipePipe,
    Caret,
    Tilde,
    Shl,
    Shr,
    Ident(String),
    StringLit(String),
    /// `s"..."` fragments: lit / `$ident` / `${...}` raw source for the parser.
    InterpString(Vec<InterpTok>),
    IntLit(i64),
    /// IEEE-754 bits of a `Float` literal (`1.5`).
    FloatLit(u64),
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
            let main = i + 5 <= chars.len()
                && chars[i + 1] == 'm'
                && chars[i + 2] == 'a'
                && chars[i + 3] == 'i'
                && chars[i + 4] == 'n'
                && chars
                    .get(i + 5)
                    .map(|ch| !(ch.is_ascii_alphanumeric() || *ch == '_'))
                    .unwrap_or(true);
            if main {
                i += 5;
                tokens.push(SpannedToken {
                    token: Token::AtMain,
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            } else {
                i += 1;
                tokens.push(SpannedToken {
                    token: Token::At,
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
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
        if c == '!' {
            tokens.push(SpannedToken {
                token: Token::Bang,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 1)),
            });
            i += 1;
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
        if c == '<' && i + 1 < chars.len() && chars[i + 1] == '<' {
            tokens.push(SpannedToken {
                token: Token::Shl,
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
        if c == '>' && i + 1 < chars.len() && chars[i + 1] == '>' {
            tokens.push(SpannedToken {
                token: Token::Shr,
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
        if c == '&' {
            tokens.push(SpannedToken {
                token: Token::Amp,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 1)),
            });
            i += 1;
            continue;
        }
        if c == '^' {
            tokens.push(SpannedToken {
                token: Token::Caret,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 1)),
            });
            i += 1;
            continue;
        }
        if c == '~' {
            tokens.push(SpannedToken {
                token: Token::Tilde,
                span: Span::new(String::new(), byte_at(i), byte_at(i + 1)),
            });
            i += 1;
            continue;
        }
        if c == '|' {
            if i + 1 < chars.len() && chars[i + 1] == '|' {
                tokens.push(SpannedToken {
                    token: Token::PipePipe,
                    span: Span::new(String::new(), byte_at(i), byte_at(i + 2)),
                });
                i += 2;
            } else {
                tokens.push(SpannedToken {
                    token: Token::Pipe,
                    span: Span::new(String::new(), byte_at(i), byte_at(i + 1)),
                });
                i += 1;
            }
            continue;
        }
        if c.is_ascii_digit() {
            let start = i;
            if c == '0' && i + 1 < chars.len() {
                let next = chars[i + 1];
                if (next == 'x' || next == 'X')
                    && i + 2 < chars.len()
                    && chars[i + 2].is_ascii_hexdigit()
                {
                    i += 2;
                    let mut n = 0i64;
                    while i < chars.len() && chars[i].is_ascii_hexdigit() {
                        let d = chars[i];
                        let v = if d.is_ascii_digit() {
                            (d as u8 - b'0') as i64
                        } else {
                            (d.to_ascii_lowercase() as u8 - b'a' + 10) as i64
                        };
                        n = n.saturating_mul(16).saturating_add(v);
                        i += 1;
                    }
                    tokens.push(SpannedToken {
                        token: Token::IntLit(n),
                        span: Span::new(String::new(), byte_at(start), byte_at(i)),
                    });
                    continue;
                }
                if (next == 'b' || next == 'B')
                    && i + 2 < chars.len()
                    && (chars[i + 2] == '0' || chars[i + 2] == '1')
                {
                    i += 2;
                    let mut n = 0i64;
                    while i < chars.len() && (chars[i] == '0' || chars[i] == '1') {
                        n = n
                            .saturating_mul(2)
                            .saturating_add((chars[i] as u8 - b'0') as i64);
                        i += 1;
                    }
                    tokens.push(SpannedToken {
                        token: Token::IntLit(n),
                        span: Span::new(String::new(), byte_at(start), byte_at(i)),
                    });
                    continue;
                }
            }
            while i < chars.len() && chars[i].is_ascii_digit() {
                i += 1;
            }
            let is_float = i < chars.len()
                && chars[i] == '.'
                && i + 1 < chars.len()
                && chars[i + 1].is_ascii_digit();
            if is_float {
                i += 1;
                while i < chars.len() && chars[i].is_ascii_digit() {
                    i += 1;
                }
                let lexeme: String = chars[start..i].iter().collect();
                let bits = lexeme.parse::<f64>().map(f64::to_bits).unwrap_or(0);
                tokens.push(SpannedToken {
                    token: Token::FloatLit(bits),
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            } else {
                let mut n = 0i64;
                for &ch in &chars[start..i] {
                    n = n
                        .saturating_mul(10)
                        .saturating_add((ch as u8 - b'0') as i64);
                }
                tokens.push(SpannedToken {
                    token: Token::IntLit(n),
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            }
            continue;
        }
        match c {
            '_' if i + 1 < chars.len()
                && (chars[i + 1].is_ascii_alphanumeric() || chars[i + 1] == '_') =>
            {
                let start = i;
                let mut ident = String::new();
                while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                    ident.push(chars[i]);
                    i += 1;
                }
                tokens.push(SpannedToken {
                    token: Token::Ident(ident),
                    span: Span::new(String::new(), byte_at(start), byte_at(i)),
                });
            }
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
                        "record" => Token::Record,
                        "trait" => Token::Trait,
                        "impl" => Token::Impl,
                        "case" => Token::Case,
                        "match" => Token::Match,
                        "def" => Token::Def,
                        "law" => Token::Law,
                        "where" => Token::Where,
                        "private" => Token::Private,
                        "import" => Token::Import,
                        "for" => Token::For,
                        "yield" => Token::Yield,
                        "if" => Token::If,
                        "else" => Token::Else,
                        "true" => Token::True,
                        "false" => Token::False,
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
                return Err(LexError::UnterminatedString(byte_at_chars(
                    char_byte, start,
                )));
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
    Err(LexError::UnterminatedString(byte_at_chars(
        char_byte, start,
    )))
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
                return Err(LexError::UnterminatedString(byte_at_chars(
                    char_byte, start,
                )));
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
    Err(LexError::UnterminatedString(byte_at_chars(
        char_byte, start,
    )))
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
    fn lexes_float_literal() {
        let toks = lex("1.5 + 10").unwrap();
        assert!(matches!(&toks[0].token, Token::FloatLit(b) if f64::from_bits(*b) == 1.5));
        assert!(matches!(toks[1].token, Token::Plus));
        assert!(matches!(toks[2].token, Token::IntLit(10)));
    }

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
    fn lexes_law() {
        let toks = lex("law always: Bool = 1 == 1").unwrap();
        assert!(matches!(toks[0].token, Token::Law));
        assert!(matches!(&toks[1].token, Token::Ident(s) if s == "always"));
    }

    #[test]
    fn lexes_underscore_ident() {
        let toks = lex("_n = 1").unwrap();
        assert!(matches!(&toks[0].token, Token::Ident(s) if s == "_n"));
        assert!(matches!(toks[1].token, Token::Eq));
        let toks = lex("_ => 1").unwrap();
        assert!(matches!(toks[0].token, Token::Underscore));
        assert!(matches!(toks[1].token, Token::Arrow));
    }

    #[test]
    fn lexes_where() {
        let toks = lex("def f(n: Int where n >= 0): Int = n").unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::Where)));
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
    fn lexes_or_pattern_pipe() {
        let toks = lex(r#"case Color.Red | Color.Blue => x || y"#).unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::Pipe)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::PipePipe)));
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
        let at_main = toks
            .iter()
            .find(|t| matches!(t.token, Token::AtMain))
            .unwrap();
        let (line, _) = crate::span::offset_to_line_col(src, at_main.span.start);
        assert_eq!(line, 2);
    }

    #[test]
    fn lexes_unary_bitwise_and_base_lits() {
        let toks = lex("!x ~1 0xFF 0b1010 a & b | c ^ d << 2 >> 1").unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::Bang)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Tilde)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::IntLit(255))));
        assert!(toks.iter().any(|t| matches!(t.token, Token::IntLit(10))));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Amp)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Pipe)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Caret)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Shl)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::Shr)));
    }

    #[test]
    fn lexes_at_as_pattern_and_keeps_at_main() {
        let toks = lex("n @ Opt.Some(x) @main").unwrap();
        assert!(toks.iter().any(|t| matches!(t.token, Token::At)));
        assert!(toks.iter().any(|t| matches!(t.token, Token::AtMain)));
        let glued = lex("n@x").unwrap();
        assert!(
            matches!(
                &glued[..3],
                [SpannedToken {
                    token: Token::Ident(a),
                    ..
                }, SpannedToken {
                    token: Token::At,
                    ..
                }, SpannedToken {
                    token: Token::Ident(b),
                    ..
                }] if a == "n" && b == "x"
            ),
            "{glued:?}"
        );
    }
}

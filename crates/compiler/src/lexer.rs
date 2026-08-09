use thiserror::Error;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Token {
    AtMain, // @main
    Def,
    Val,
    Colon,
    Eq,
    Dot,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Arrow, // =>
    Underscore,
    Ident(String),
    StringLit(String),
    Eof,
}

#[derive(Debug, Error)]
pub enum LexError {
    #[error("unexpected character {0:?} at byte offset {1}")]
    UnexpectedChar(char, usize),
    #[error("unterminated string at byte offset {0}")]
    UnterminatedString(usize),
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
            let start = i;
            i += 1;
            let mut s = String::new();
            let mut closed = false;
            while i < chars.len() {
                let ch = chars[i];
                if ch == '"' {
                    i += 1;
                    closed = true;
                    break;
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
                        other => other,
                    });
                    i += 1;
                    continue;
                }
                s.push(ch);
                i += 1;
            }
            if !closed {
                return Err(LexError::UnterminatedString(start));
            }
            tokens.push(Token::StringLit(s));
            continue;
        }
        if c == '=' && i + 1 < chars.len() && chars[i + 1] == '>' {
            tokens.push(Token::Arrow);
            i += 2;
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
                tokens.push(match ident.as_str() {
                    "def" => Token::Def,
                    "val" => Token::Val,
                    _ => Token::Ident(ident),
                });
            }
            other => return Err(LexError::UnexpectedChar(other, i)),
        }
    }
    tokens.push(Token::Eof);
    Ok(tokens)
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
        assert!(toks.iter().any(|t| matches!(t, Token::StringLit(s) if s == "hi")));
    }
}

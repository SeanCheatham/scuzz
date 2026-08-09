use crate::ast::{Expr, MainDef, Program, Type};
use crate::lexer::{lex, LexError, Token};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ParseError {
    #[error(transparent)]
    Lex(#[from] LexError),
    #[error("parse error: {0}")]
    Msg(String),
}

pub fn parse(source: &str) -> Result<Program, ParseError> {
    let tokens = lex(source)?;
    let mut p = Parser { tokens, i: 0 };
    p.parse_program()
}

struct Parser {
    tokens: Vec<Token>,
    i: usize,
}

impl Parser {
    fn peek(&self) -> &Token {
        self.tokens.get(self.i).unwrap_or(&Token::Eof)
    }

    fn bump(&mut self) -> Token {
        let t = self.tokens.get(self.i).cloned().unwrap_or(Token::Eof);
        if self.i < self.tokens.len() {
            self.i += 1;
        }
        t
    }

    fn expect(&mut self, expected: &Token) -> Result<(), ParseError> {
        let got = self.bump();
        if &got == expected {
            Ok(())
        } else {
            Err(ParseError::Msg(format!(
                "expected {expected:?}, got {got:?}"
            )))
        }
    }

    fn expect_ident(&mut self) -> Result<String, ParseError> {
        match self.bump() {
            Token::Ident(s) => Ok(s),
            other => Err(ParseError::Msg(format!("expected ident, got {other:?}"))),
        }
    }

    fn parse_program(&mut self) -> Result<Program, ParseError> {
        self.expect(&Token::AtMain)?;
        self.expect(&Token::Def)?;
        let name = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let ty = self.parse_type()?;
        match &ty {
            Type::Io(inner) if matches!(inner.as_ref(), Type::Unit) => {}
            _ => {
                return Err(ParseError::Msg(
                    "@main must have type IO[Unit] in Stage 0".into(),
                ))
            }
        }
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        if !matches!(self.peek(), Token::Eof) {
            return Err(ParseError::Msg(format!(
                "unexpected trailing token {:?}",
                self.peek()
            )));
        }
        Ok(Program {
            main: MainDef { name, body },
        })
    }

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        let name = self.expect_ident()?;
        if name == "Unit" {
            return Ok(Type::Unit);
        }
        if name == "IO" {
            self.expect(&Token::LBracket)?;
            let inner = self.parse_type()?;
            self.expect(&Token::RBracket)?;
            return Ok(Type::Io(Box::new(inner)));
        }
        Err(ParseError::Msg(format!("unknown type {name}")))
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        // postfix .flatMap(...)
        while matches!(self.peek(), Token::Dot) {
            self.bump();
            let method = self.expect_ident()?;
            if method != "flatMap" {
                return Err(ParseError::Msg(format!(
                    "unsupported method .{method} (Stage 0: flatMap only)"
                )));
            }
            self.expect(&Token::LParen)?;
            let body = self.parse_lambda_body()?;
            self.expect(&Token::RParen)?;
            expr = Expr::FlatMap {
                inner: Box::new(expr),
                body: Box::new(body),
            };
        }
        Ok(expr)
    }

    fn parse_lambda_body(&mut self) -> Result<Expr, ParseError> {
        // `_ => expr` or `() => expr`
        match self.peek() {
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                self.parse_expr()
            }
            Token::LParen => {
                self.bump();
                self.expect(&Token::RParen)?;
                self.expect(&Token::Arrow)?;
                self.parse_expr()
            }
            _ => Err(ParseError::Msg(
                "expected `_ => expr` or `() => expr` lambda".into(),
            )),
        }
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.peek() {
            Token::LParen => {
                self.bump();
                self.expect(&Token::RParen)?;
                Ok(Expr::Unit)
            }
            Token::Ident(name) if name == "IO" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                match method.as_str() {
                    "println" => {
                        self.expect(&Token::LParen)?;
                        let s = match self.bump() {
                            Token::StringLit(s) => s,
                            other => {
                                return Err(ParseError::Msg(format!(
                                    "IO.println expects string, got {other:?}"
                                )))
                            }
                        };
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoPrintln(s))
                    }
                    "delay" => {
                        // IO.delay(() => ()) — unit delay
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::RParen)?;
                        self.expect(&Token::Arrow)?;
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::RParen)?;
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoDelayUnit)
                    }
                    other => Err(ParseError::Msg(format!("unknown IO.{other}"))),
                }
            }
            Token::Ident(name) if name == "Ui" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                match method.as_str() {
                    "runHeadless" => {
                        self.expect(&Token::LParen)?;
                        let s = match self.bump() {
                            Token::StringLit(s) => s,
                            other => {
                                return Err(ParseError::Msg(format!(
                                    "Ui.runHeadless expects string, got {other:?}"
                                )))
                            }
                        };
                        self.expect(&Token::RParen)?;
                        Ok(Expr::UiRunHeadless(s))
                    }
                    other => Err(ParseError::Msg(format!("unknown Ui.{other}"))),
                }
            }
            other => Err(ParseError::Msg(format!("unexpected token {other:?}"))),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hello")"#).unwrap();
        assert_eq!(p.main.name, "main");
        assert!(matches!(p.main.body, Expr::IoPrintln(s) if s == "Hello"));
    }

    #[test]
    fn parse_flatmap() {
        let src = r#"@main def main: IO[Unit] = IO.println("a").flatMap(_ => IO.println("b"))"#;
        let p = parse(src).unwrap();
        assert!(matches!(p.main.body, Expr::FlatMap { .. }));
    }

    #[test]
    fn parse_ui_run_headless() {
        let p = parse(r#"@main def main: IO[Unit] = Ui.runHeadless("Hi")"#).unwrap();
        assert!(matches!(p.main.body, Expr::UiRunHeadless(s) if s == "Hi"));
    }
}

use crate::ast::{
    EnumDef, Expr, MainDef, MatchArm, Pattern, Program, Type,
};
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

/// Parse multiple source files into one program (packages must agree; enums merge).
pub fn parse_sources(sources: &[(String, String)]) -> Result<Program, ParseError> {
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut main: Option<MainDef> = None;

    for (name, src) in sources {
        let prog = parse(src).map_err(|e| ParseError::Msg(format!("{name}: {e}")))?;
        if !prog.package.is_empty() {
            match &package {
                None => package = Some(prog.package.clone()),
                Some(p) if *p == prog.package => {}
                Some(p) => {
                    return Err(ParseError::Msg(format!(
                        "{name}: package {:?} conflicts with {:?}",
                        prog.package, p
                    )))
                }
            }
        }
        for e in prog.enums {
            if enums.iter().any(|x| x.name == e.name) {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate enum {}",
                    e.name
                )));
            }
            enums.push(e);
        }
        // Enum-/package-only units leave an empty main name.
        if !prog.main.name.is_empty() {
            if main.is_some() {
                return Err(ParseError::Msg(format!(
                    "{name}: multiple @main definitions"
                )));
            }
            main = Some(prog.main);
        }
    }

    let main = main.ok_or_else(|| ParseError::Msg("no @main definition".into()))?;
    Ok(Program {
        package: package.unwrap_or_default(),
        enums,
        main,
    })
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
        let mut package = Vec::new();
        if matches!(self.peek(), Token::Package) {
            self.bump();
            package.push(self.expect_ident()?);
            while matches!(self.peek(), Token::Dot) {
                self.bump();
                package.push(self.expect_ident()?);
            }
        }

        let mut enums = Vec::new();
        while matches!(self.peek(), Token::Enum) {
            enums.push(self.parse_enum()?);
        }

        // Allow files that only declare package/enums (multi-file units).
        if matches!(self.peek(), Token::Eof) {
            return Ok(Program {
                package,
                enums,
                main: MainDef {
                    name: String::new(),
                    body: Expr::Unit,
                },
            });
        }

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
        let body = self.parse_block()?;
        if !matches!(self.peek(), Token::Eof) {
            return Err(ParseError::Msg(format!(
                "unexpected trailing token {:?}",
                self.peek()
            )));
        }
        Ok(Program {
            package,
            enums,
            main: MainDef { name, body },
        })
    }

    fn parse_enum(&mut self) -> Result<EnumDef, ParseError> {
        self.expect(&Token::Enum)?;
        let name = self.expect_ident()?;
        let mut cases = Vec::new();
        match self.peek() {
            Token::LBrace => {
                self.bump();
                loop {
                    self.expect(&Token::Case)?;
                    cases.push(self.expect_ident()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.bump();
                        continue;
                    }
                    break;
                }
                self.expect(&Token::RBrace)?;
            }
            Token::Colon => {
                self.bump();
                while matches!(self.peek(), Token::Case) {
                    self.bump();
                    cases.push(self.expect_ident()?);
                }
            }
            other => {
                return Err(ParseError::Msg(format!(
                    "enum body expected `:` or `{{`, got {other:?}"
                )))
            }
        }
        if cases.is_empty() {
            return Err(ParseError::Msg(format!("enum {name} has no cases")));
        }
        Ok(EnumDef { name, cases })
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
        Ok(Type::Adt(name))
    }

    /// Block: zero or more `val` bindings then a final expression.
    fn parse_block(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.peek(), Token::Val) {
            self.bump();
            let name = self.expect_ident()?;
            self.expect(&Token::Eq)?;
            let value = self.parse_expr()?;
            let body = self.parse_block()?;
            return Ok(Expr::Let {
                name,
                value: Box::new(value),
                body: Box::new(body),
            });
        }
        self.parse_expr()
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        // postfix: .flatMap / .handleErrorWith / .attempt / match
        loop {
            match self.peek() {
                Token::Dot => {
                    self.bump();
                    let method = self.expect_ident()?;
                    match method.as_str() {
                        "flatMap" => {
                            self.expect(&Token::LParen)?;
                            let body = self.parse_lambda_body()?;
                            self.expect(&Token::RParen)?;
                            expr = Expr::FlatMap {
                                inner: Box::new(expr),
                                body: Box::new(body),
                            };
                        }
                        "handleErrorWith" => {
                            self.expect(&Token::LParen)?;
                            let body = self.parse_lambda_body()?;
                            self.expect(&Token::RParen)?;
                            expr = Expr::HandleErrorWith {
                                inner: Box::new(expr),
                                body: Box::new(body),
                            };
                        }
                        "attempt" => {
                            if matches!(self.peek(), Token::LParen) {
                                self.bump();
                                self.expect(&Token::RParen)?;
                            }
                            expr = Expr::Attempt {
                                inner: Box::new(expr),
                            };
                        }
                        other => {
                            return Err(ParseError::Msg(format!(
                                "unsupported method .{other}"
                            )))
                        }
                    }
                }
                Token::Match => {
                    self.bump();
                    let arms = self.parse_match_arms()?;
                    expr = Expr::Match {
                        scrutinee: Box::new(expr),
                        arms,
                    };
                }
                _ => break,
            }
        }
        Ok(expr)
    }

    fn parse_match_arms(&mut self) -> Result<Vec<MatchArm>, ParseError> {
        let braced = matches!(self.peek(), Token::LBrace);
        if braced {
            self.bump();
        }
        let mut arms = Vec::new();
        while matches!(self.peek(), Token::Case) {
            self.bump();
            let pattern = self.parse_pattern()?;
            self.expect(&Token::Arrow)?;
            let body = self.parse_expr()?;
            arms.push(MatchArm { pattern, body });
        }
        if braced {
            self.expect(&Token::RBrace)?;
        }
        if arms.is_empty() {
            return Err(ParseError::Msg("match needs at least one case".into()));
        }
        Ok(arms)
    }

    fn parse_pattern(&mut self) -> Result<Pattern, ParseError> {
        match self.peek() {
            Token::Underscore => {
                self.bump();
                Ok(Pattern::Wildcard)
            }
            Token::Ident(_) => {
                let enum_name = self.expect_ident()?;
                self.expect(&Token::Dot)?;
                let case_name = self.expect_ident()?;
                Ok(Pattern::Adt {
                    enum_name,
                    case_name,
                })
            }
            other => Err(ParseError::Msg(format!(
                "expected pattern, got {other:?}"
            ))),
        }
    }

    fn parse_lambda_body(&mut self) -> Result<Expr, ParseError> {
        match self.peek() {
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                self.parse_block()
            }
            Token::LParen => {
                self.bump();
                self.expect(&Token::RParen)?;
                self.expect(&Token::Arrow)?;
                self.parse_block()
            }
            _ => Err(ParseError::Msg(
                "expected `_ => expr` or `() => expr` lambda".into(),
            )),
        }
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.peek().clone() {
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
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::RParen)?;
                        self.expect(&Token::Arrow)?;
                        self.expect(&Token::LParen)?;
                        self.expect(&Token::RParen)?;
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoDelayUnit)
                    }
                    "sleep" => {
                        self.expect(&Token::LParen)?;
                        let ms = match self.bump() {
                            Token::IntLit(n) => n,
                            other => {
                                return Err(ParseError::Msg(format!(
                                    "IO.sleep expects int, got {other:?}"
                                )))
                            }
                        };
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoSleep(ms))
                    }
                    "fail" => {
                        self.expect(&Token::LParen)?;
                        let s = match self.bump() {
                            Token::StringLit(s) => s,
                            other => {
                                return Err(ParseError::Msg(format!(
                                    "IO.fail expects string, got {other:?}"
                                )))
                            }
                        };
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoFail(s))
                    }
                    "race" => {
                        self.expect(&Token::LParen)?;
                        let left = self.parse_expr()?;
                        self.expect(&Token::Comma)?;
                        let right = self.parse_expr()?;
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoRace {
                            left: Box::new(left),
                            right: Box::new(right),
                        })
                    }
                    "both" => {
                        self.expect(&Token::LParen)?;
                        let left = self.parse_expr()?;
                        self.expect(&Token::Comma)?;
                        let right = self.parse_expr()?;
                        self.expect(&Token::RParen)?;
                        Ok(Expr::IoBoth {
                            left: Box::new(left),
                            right: Box::new(right),
                        })
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
                    "runCounter" => {
                        if matches!(self.peek(), Token::LParen) {
                            self.bump();
                            self.expect(&Token::RParen)?;
                        }
                        Ok(Expr::UiRunCounter)
                    }
                    "runTodo" => {
                        if matches!(self.peek(), Token::LParen) {
                            self.bump();
                            self.expect(&Token::RParen)?;
                        }
                        Ok(Expr::UiRunTodo)
                    }
                    other => Err(ParseError::Msg(format!("unknown Ui.{other}"))),
                }
            }
            Token::Ident(name) if name == "Effects" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                if method != "runKit" {
                    return Err(ParseError::Msg(format!("unknown Effects.{method}")));
                }
                if matches!(self.peek(), Token::LParen) {
                    self.bump();
                    self.expect(&Token::RParen)?;
                }
                Ok(Expr::EffectsRunKit)
            }
            Token::Ident(name) if name == "Lexer" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                if method != "classify" {
                    return Err(ParseError::Msg(format!("unknown Lexer.{method}")));
                }
                self.expect(&Token::LParen)?;
                let s = match self.bump() {
                    Token::StringLit(s) => s,
                    other => {
                        return Err(ParseError::Msg(format!(
                            "Lexer.classify expects string, got {other:?}"
                        )))
                    }
                };
                self.expect(&Token::RParen)?;
                Ok(Expr::LexerClassify(s))
            }
            Token::Ident(name) => {
                self.bump();
                if matches!(self.peek(), Token::Dot) {
                    // Could be Enum.Case
                    self.bump();
                    let case_name = self.expect_ident()?;
                    Ok(Expr::AdtConstruct {
                        enum_name: name,
                        case_name,
                    })
                } else {
                    Ok(Expr::Var(name))
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

    #[test]
    fn parse_ui_run_counter_todo() {
        let p = parse(r#"@main def main: IO[Unit] = Ui.runCounter"#).unwrap();
        assert!(matches!(p.main.body, Expr::UiRunCounter));
        let p = parse(r#"@main def main: IO[Unit] = Ui.runTodo()"#).unwrap();
        assert!(matches!(p.main.body, Expr::UiRunTodo));
    }

    #[test]
    fn parse_package_enum_match() {
        let src = r#"
package demo.color
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  val c = Color.Red
  c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.package, vec!["demo", "color"]);
        assert_eq!(p.enums.len(), 1);
        assert_eq!(p.enums[0].name, "Color");
        assert!(matches!(p.main.body, Expr::Let { .. }));
    }

    #[test]
    fn parse_effects_kit() {
        let p = parse(r#"@main def main: IO[Unit] = Effects.runKit"#).unwrap();
        assert!(matches!(p.main.body, Expr::EffectsRunKit));
    }
}

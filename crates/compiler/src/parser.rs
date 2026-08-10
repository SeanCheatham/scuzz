use crate::ast::{
    BinOp, EnumDef, Expr, FunDef, InterpPart, MainDef, MatchArm, Param, Pattern, Program, Type,
};
use crate::lexer::{lex, InterpTok, LexError, Token};
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

/// Parse multiple source files into one program (packages must agree; enums/defs merge).
pub fn parse_sources(sources: &[(String, String)]) -> Result<Program, ParseError> {
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut defs: Vec<FunDef> = Vec::new();
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
        for d in prog.defs {
            if defs.iter().any(|x| x.name == d.name) {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate def {}",
                    d.name
                )));
            }
            defs.push(d);
        }
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
        defs,
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
        let mut defs = Vec::new();
        let mut main = MainDef {
            name: String::new(),
            body: Expr::Unit,
        };

        loop {
            match self.peek() {
                Token::Enum => enums.push(self.parse_enum()?),
                Token::Def => defs.push(self.parse_def()?),
                Token::AtMain => {
                    if !main.name.is_empty() {
                        return Err(ParseError::Msg("multiple @main".into()));
                    }
                    main = self.parse_main()?;
                }
                Token::Eof => break,
                other => {
                    return Err(ParseError::Msg(format!(
                        "expected enum/def/@main, got {other:?}"
                    )))
                }
            }
        }

        Ok(Program {
            package,
            enums,
            defs,
            main,
        })
    }

    fn parse_main(&mut self) -> Result<MainDef, ParseError> {
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
        Ok(MainDef { name, body })
    }

    fn parse_def(&mut self) -> Result<FunDef, ParseError> {
        self.expect(&Token::Def)?;
        let name = self.expect_ident()?;
        self.expect(&Token::LParen)?;
        let mut params = Vec::new();
        if !matches!(self.peek(), Token::RParen) {
            loop {
                let pname = self.expect_ident()?;
                self.expect(&Token::Colon)?;
                let pty = self.parse_type()?;
                params.push(Param {
                    name: pname,
                    ty: pty,
                });
                if matches!(self.peek(), Token::Comma) {
                    self.bump();
                    continue;
                }
                break;
            }
        }
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type()?;
        self.expect(&Token::Eq)?;
        let body = self.parse_block()?;
        Ok(FunDef {
            name,
            params,
            ret,
            body,
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
        match name.as_str() {
            "Unit" => Ok(Type::Unit),
            "Int" => Ok(Type::Int),
            "String" => Ok(Type::String),
            "Bool" => Ok(Type::Bool),
            "List" => Ok(Type::List),
            "IO" => {
                self.expect(&Token::LBracket)?;
                let inner = self.parse_type()?;
                self.expect(&Token::RBracket)?;
                Ok(Type::Io(Box::new(inner)))
            }
            _ => Ok(Type::Adt(name)),
        }
    }

    /// Block: `val` bindings and expression statements, ending in a final expression.
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
        let expr = self.parse_expr()?;
        // Mid-block `val` after a statement expression (val-anywhere).
        if matches!(self.peek(), Token::Val) {
            return Ok(Expr::Let {
                name: "_".into(),
                value: Box::new(expr),
                body: Box::new(self.parse_block()?),
            });
        }
        Ok(expr)
    }

    /// `if` then/else: `val`-led block (same bindings as lambda bodies), or a
    /// single expression. Starting with `val` is required for multi-binding
    /// branches so mid-block `val x = if … else e` does not steal following vals.
    fn parse_if_branch(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.peek(), Token::Val) {
            self.parse_block()
        } else {
            self.parse_expr()
        }
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        self.parse_or()
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;
        while matches!(self.peek(), Token::PipePipe) {
            self.bump();
            let right = self.parse_and()?;
            left = Expr::Binary {
                op: BinOp::Or,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_cmp()?;
        while matches!(self.peek(), Token::AmpAmp) {
            self.bump();
            let right = self.parse_cmp()?;
            left = Expr::Binary {
                op: BinOp::And,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_cmp(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_add()?;
        loop {
            let op = match self.peek() {
                Token::EqEq => BinOp::Eq,
                Token::BangEq => BinOp::Ne,
                Token::Lt => BinOp::Lt,
                Token::LtEq => BinOp::Le,
                Token::Gt => BinOp::Gt,
                Token::GtEq => BinOp::Ge,
                _ => break,
            };
            self.bump();
            let right = self.parse_add()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_add(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_mul()?;
        loop {
            let op = match self.peek() {
                Token::Plus => BinOp::Add,
                Token::Minus => BinOp::Sub,
                _ => break,
            };
            self.bump();
            let right = self.parse_mul()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_mul(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_postfix()?;
        loop {
            let op = match self.peek() {
                Token::Star => BinOp::Mul,
                Token::Slash => BinOp::Div,
                Token::Percent => BinOp::Mod,
                _ => break,
            };
            self.bump();
            let right = self.parse_postfix()?;
            left = Expr::Binary {
                op,
                left: Box::new(left),
                right: Box::new(right),
            };
        }
        Ok(left)
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        loop {
            match self.peek() {
                Token::Dot => {
                    self.bump();
                    let method = self.expect_ident()?;
                    match method.as_str() {
                        "flatMap" => {
                            self.expect(&Token::LParen)?;
                            let (param, body) = self.parse_lambda()?;
                            self.expect(&Token::RParen)?;
                            expr = Expr::FlatMap {
                                inner: Box::new(expr),
                                param,
                                body: Box::new(body),
                            };
                        }
                        "handleErrorWith" => {
                            self.expect(&Token::LParen)?;
                            let (_param, body) = self.parse_lambda()?;
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
                            // Qual.method(args) already handled in primary for known modules.
                            // Enum.Case is primary; here treat as error for unknown method.
                            return Err(ParseError::Msg(format!(
                                "unsupported method .{other}"
                            )));
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

    fn parse_lambda(&mut self) -> Result<(Option<String>, Expr), ParseError> {
        match self.peek().clone() {
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                Ok((None, self.parse_block()?))
            }
            Token::LParen => {
                self.bump();
                self.expect(&Token::RParen)?;
                self.expect(&Token::Arrow)?;
                Ok((None, self.parse_block()?))
            }
            Token::Ident(name) => {
                self.bump();
                self.expect(&Token::Arrow)?;
                Ok((Some(name), self.parse_block()?))
            }
            _ => Err(ParseError::Msg(
                "expected `_ => expr`, `() => expr`, or `name => expr`".into(),
            )),
        }
    }

    fn parse_args(&mut self) -> Result<Vec<Expr>, ParseError> {
        self.expect(&Token::LParen)?;
        let mut args = Vec::new();
        if !matches!(self.peek(), Token::RParen) {
            loop {
                args.push(self.parse_expr()?);
                if matches!(self.peek(), Token::Comma) {
                    self.bump();
                    continue;
                }
                break;
            }
        }
        self.expect(&Token::RParen)?;
        Ok(args)
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.peek().clone() {
            Token::If => {
                self.bump();
                self.expect(&Token::LParen)?;
                let cond = self.parse_expr()?;
                self.expect(&Token::RParen)?;
                // Branches: full blocks when led by `val` (multi-binding else);
                // otherwise a single expr so `val x = if … else e` + following
                // `val` is not greedily absorbed into the branch.
                let then_branch = self.parse_if_branch()?;
                self.expect(&Token::Else)?;
                let else_branch = self.parse_if_branch()?;
                Ok(Expr::If {
                    cond: Box::new(cond),
                    then_branch: Box::new(then_branch),
                    else_branch: Box::new(else_branch),
                })
            }
            Token::LParen => {
                self.bump();
                if matches!(self.peek(), Token::RParen) {
                    self.bump();
                    return Ok(Expr::Unit);
                }
                let inner = self.parse_expr()?;
                self.expect(&Token::RParen)?;
                Ok(inner)
            }
            Token::LBracket => {
                self.bump();
                let mut elems = Vec::new();
                if !matches!(self.peek(), Token::RBracket) {
                    loop {
                        elems.push(self.parse_expr()?);
                        if matches!(self.peek(), Token::Comma) {
                            self.bump();
                        } else {
                            break;
                        }
                    }
                }
                self.expect(&Token::RBracket)?;
                Ok(Expr::ListLit { elems })
            }
            Token::IntLit(n) => {
                self.bump();
                Ok(Expr::IntLit(n))
            }
            Token::StringLit(s) => {
                self.bump();
                Ok(Expr::StrLit(s))
            }
            Token::InterpString(parts) => {
                self.bump();
                self.parse_interpolate(parts)
            }
            Token::Minus => {
                self.bump();
                let n = match self.bump() {
                    Token::IntLit(n) => n,
                    other => {
                        return Err(ParseError::Msg(format!(
                            "expected int after `-`, got {other:?}"
                        )))
                    }
                };
                Ok(Expr::IntLit(-n))
            }
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                let body = self.parse_block()?;
                Ok(Expr::Lambda {
                    param: None,
                    body: Box::new(body),
                })
            }
            Token::Ident(name) if name == "IO" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                match method.as_str() {
                    "println" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(ParseError::Msg("IO.println expects 1 arg".into()));
                        }
                        Ok(Expr::IoPrintln(Box::new(args.into_iter().next().unwrap())))
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
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(ParseError::Msg("IO.sleep expects 1 arg".into()));
                        }
                        Ok(Expr::IoSleep(Box::new(args.into_iter().next().unwrap())))
                    }
                    "fail" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(ParseError::Msg("IO.fail expects 1 arg".into()));
                        }
                        Ok(Expr::IoFail(Box::new(args.into_iter().next().unwrap())))
                    }
                    "pure" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(ParseError::Msg("IO.pure expects 1 arg".into()));
                        }
                        Ok(Expr::IoPure(Box::new(args.into_iter().next().unwrap())))
                    }
                    "race" | "both" => {
                        let args = self.parse_args()?;
                        if args.len() != 2 {
                            return Err(ParseError::Msg(format!(
                                "IO.{method} expects 2 args"
                            )));
                        }
                        let mut it = args.into_iter();
                        let left = it.next().unwrap();
                        let right = it.next().unwrap();
                        if method == "race" {
                            Ok(Expr::IoRace {
                                left: Box::new(left),
                                right: Box::new(right),
                            })
                        } else {
                            Ok(Expr::IoBoth {
                                left: Box::new(left),
                                right: Box::new(right),
                            })
                        }
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
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(ParseError::Msg("Ui.runHeadless expects 1 arg".into()));
                        }
                        Ok(Expr::UiRunHeadless(Box::new(
                            args.into_iter().next().unwrap(),
                        )))
                    }
                    "runCounter" => {
                        if matches!(self.peek(), Token::LParen) {
                            self.bump();
                            self.expect(&Token::RParen)?;
                        }
                        Ok(Expr::UiRunCounter)
                    }
                    "runLive" => {
                        if matches!(self.peek(), Token::LParen) {
                            self.bump();
                            self.expect(&Token::RParen)?;
                        }
                        Ok(Expr::UiRunLive)
                    }
                    "runTodo" => {
                        if matches!(self.peek(), Token::LParen) {
                            self.bump();
                            self.expect(&Token::RParen)?;
                        }
                        Ok(Expr::UiRunTodo)
                    }
                    other => {
                        let callee = format!("Ui.{other}");
                        let args = if matches!(self.peek(), Token::LParen) {
                            self.parse_args()?
                        } else {
                            Vec::new()
                        };
                        Ok(Expr::Call { callee, args })
                    }
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
            Token::Ident(name)
                if matches!(
                    name.as_str(),
                    "Str" | "List" | "Fs" | "Sys" | "Lexer" | "Clock" | "Random"
                        | "Net" | "Impurity" | "Signal" | "View" | "Theme"
                ) =>
            {
                self.bump();
                self.expect(&Token::Dot)?;
                let method = self.expect_ident()?;
                let callee = format!("{name}.{method}");
                let args = if matches!(self.peek(), Token::LParen) {
                    self.parse_args()?
                } else {
                    Vec::new()
                };
                if name == "Lexer" && method == "classify" {
                    if args.len() != 1 {
                        return Err(ParseError::Msg("Lexer.classify expects 1 arg".into()));
                    }
                    return Ok(Expr::LexerClassify(Box::new(
                        args.into_iter().next().unwrap(),
                    )));
                }
                Ok(Expr::Call { callee, args })
            }
            Token::Ident(name) => {
                self.bump();
                if matches!(self.peek(), Token::Arrow) {
                    self.bump();
                    let body = self.parse_block()?;
                    return Ok(Expr::Lambda {
                        param: Some(name),
                        body: Box::new(body),
                    });
                }
                if matches!(self.peek(), Token::LParen) {
                    let args = self.parse_args()?;
                    return Ok(Expr::Call {
                        callee: name,
                        args,
                    });
                }
                if matches!(self.peek(), Token::Dot) {
                    self.bump();
                    let case_name = self.expect_ident()?;
                    // `Color.rgb(...)` is a module call; `Color.Red` is an ADT case.
                    if matches!(self.peek(), Token::LParen) {
                        let args = self.parse_args()?;
                        return Ok(Expr::Call {
                            callee: format!("{name}.{case_name}"),
                            args,
                        });
                    }
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

    fn parse_interpolate(&mut self, parts: Vec<InterpTok>) -> Result<Expr, ParseError> {
        let mut out = Vec::new();
        for part in parts {
            match part {
                InterpTok::Lit(s) => out.push(InterpPart::Lit(s)),
                InterpTok::Ident(name) => out.push(InterpPart::Expr(Expr::Var(name))),
                InterpTok::Brace(body) => {
                    let tokens = lex(&body)?;
                    let mut nested = Parser { tokens, i: 0 };
                    let e = nested.parse_expr()?;
                    if !matches!(nested.peek(), Token::Eof) {
                        return Err(ParseError::Msg(
                            "trailing tokens in interpolation hole".into(),
                        ));
                    }
                    out.push(InterpPart::Expr(e));
                }
            }
        }
        if out.is_empty() {
            out.push(InterpPart::Lit(String::new()));
        }
        Ok(Expr::Interpolate { parts: out })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hello")"#).unwrap();
        assert_eq!(p.main.name, "main");
        assert!(matches!(p.main.body, Expr::IoPrintln(_)));
    }

    #[test]
    fn parse_lambda_literal_underscore() {
        let src = r#"@main def main: IO[Unit] = View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))"#;
        let p = parse(src).unwrap();
        match &p.main.body {
            Expr::Call { callee, args } if callee == "View.button" => {
                assert_eq!(args.len(), 2);
                assert!(matches!(args[1], Expr::Lambda { param: None, .. }));
            }
            other => panic!("expected View.button call, got {other:?}"),
        }
    }

    #[test]
    fn parse_lambda_literal_named_param() {
        let src = r#"@main def main: IO[Unit] = View.button("+1", self => Signal.set(count, 1))"#;
        let p = parse(src).unwrap();
        match &p.main.body {
            Expr::Call { args, .. } => assert!(matches!(
                args[1],
                Expr::Lambda { param: Some(ref n), .. } if n == "self"
            )),
            other => panic!("expected call, got {other:?}"),
        }
    }

    #[test]
    fn parse_flatmap_bound() {
        let src = r#"@main def main: IO[Unit] = Fs.read("x").flatMap(s => IO.println(s))"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            p.main.body,
            Expr::FlatMap {
                param: Some(ref n),
                ..
            } if n == "s"
        ));
    }

    #[test]
    fn parse_def_if() {
        let src = r#"
def add1(n: Int): Int = n + 1
@main def main: IO[Unit] =
  if (add1(0) == 1) IO.println("ok") else IO.println("bad")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.defs.len(), 1);
        assert!(matches!(p.main.body, Expr::If { .. }));
    }

    #[test]
    fn parse_if_val_led_else_block() {
        let src = r#"
@main def main: IO[Unit] =
  val n = 1
  if (n == 0) ()
  else
    val a = IO.println("a")
    val b = IO.println("b")
    IO.println("c")
"#;
        let p = parse(src).unwrap();
        match &p.main.body {
            Expr::Let { body, .. } => match body.as_ref() {
                Expr::If {
                    else_branch: box Expr::Let { name, body, .. },
                    ..
                } => {
                    assert_eq!(name, "a");
                    assert!(matches!(body.as_ref(), Expr::Let { name, .. } if name == "b"));
                }
                other => panic!("expected if with let-else, got {other:?}"),
            },
            other => panic!("expected let, got {other:?}"),
        }
    }

    #[test]
    fn parse_mid_block_if_does_not_steal_following_val() {
        let src = r#"
@main def main: IO[Unit] =
  val path = if (1 == 0) "a" else "b"
  val draft = "x"
  IO.println(draft)
"#;
        let p = parse(src).unwrap();
        match &p.main.body {
            Expr::Let {
                name,
                value: box Expr::If { .. },
                body: box Expr::Let { name: draft, .. },
            } => {
                assert_eq!(name, "path");
                assert_eq!(draft, "draft");
            }
            other => panic!("expected path if + draft let, got {other:?}"),
        }
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
        assert!(matches!(p.main.body, Expr::Let { .. }));
    }

    #[test]
    fn parse_list_literal_empty() {
        let p = parse(r#"@main def main: IO[Unit] = []"#).unwrap();
        match &p.main.body {
            Expr::ListLit { elems } => assert!(elems.is_empty()),
            other => panic!("expected ListLit, got {other:?}"),
        }
    }

    #[test]
    fn parse_list_literal_elems() {
        let p = parse(r#"@main def main: IO[Unit] = [a, b]"#).unwrap();
        match &p.main.body {
            Expr::ListLit { elems } => {
                assert_eq!(elems.len(), 2);
                assert!(matches!(elems[0], Expr::Var(ref n) if n == "a"));
                assert!(matches!(elems[1], Expr::Var(ref n) if n == "b"));
            }
            other => panic!("expected ListLit, got {other:?}"),
        }
    }
}

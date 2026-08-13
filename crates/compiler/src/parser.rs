use crate::ast::{
    BinOp, EnumCase, EnumDef, Expr, ExprKind, ForBinder, FunDef, ImplDef, ImplMethod, Import,
    InterpPart, MainDef, MatchArm, Param, Pattern, Program, TraitDef, TraitMethod, Type,
};
use crate::lexer::{lex, InterpTok, LexError, SpannedToken, Token};
use crate::resolve::module_id_from_label;
use crate::span::Span;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ParseError {
    #[error(transparent)]
    Lex(#[from] LexError),
    #[error("parse error: {0}")]
    Msg(String),
    #[error("parse error: {msg}")]
    At { msg: String, span: Span },
}

impl ParseError {
    pub fn message(&self) -> String {
        match self {
            ParseError::Lex(e) => e.to_string(),
            ParseError::Msg(m) => m.clone(),
            ParseError::At { msg, .. } => msg.clone(),
        }
    }

    pub fn span(&self) -> Option<&Span> {
        match self {
            ParseError::At { span, .. } => Some(span),
            _ => None,
        }
    }
}

pub fn parse(source: &str) -> Result<Program, ParseError> {
    parse_file(source, "")
}

pub fn parse_file(source: &str, file: &str) -> Result<Program, ParseError> {
    let mut tokens = match lex(source) {
        Ok(t) => t,
        Err(e) => {
            let off = e.offset();
            return Err(ParseError::At {
                msg: e.to_string(),
                span: Span::new(file.to_string(), off, off),
            });
        }
    };
    for t in &mut tokens {
        if t.span.file.is_empty() {
            t.span.file = file.to_string();
        }
    }
    let module = module_id_from_label(file);
    let mut p = Parser {
        tokens,
        i: 0,
        file: file.to_string(),
        module,
    };
    p.parse_program()
}

/// Parse multiple source files into one program (packages must agree; defs and
/// enums are namespaced by file-stem module — same bare name in two modules is allowed).
pub fn parse_sources(sources: &[(String, String)]) -> Result<Program, ParseError> {
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut traits: Vec<TraitDef> = Vec::new();
    let mut impls: Vec<ImplDef> = Vec::new();
    let mut defs: Vec<FunDef> = Vec::new();
    let mut imports: Vec<Import> = Vec::new();
    let mut main: Option<MainDef> = None;

    for (name, src) in sources {
        let prog = parse_file(src, name)?;
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
            if enums
                .iter()
                .any(|x| x.module == e.module && x.name == e.name)
            {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate enum {}.{}",
                    e.module, e.name
                )));
            }
            enums.push(e);
        }
        for t in prog.traits {
            if traits
                .iter()
                .any(|x| x.module == t.module && x.name == t.name)
            {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate trait {}.{}",
                    t.module, t.name
                )));
            }
            traits.push(t);
        }
        for im in prog.impls {
            if impls.iter().any(|x| {
                x.module == im.module && x.trait_name == im.trait_name && x.for_type == im.for_type
            }) {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate impl {} for {}",
                    im.trait_name, im.for_type
                )));
            }
            impls.push(im);
        }
        for d in prog.defs {
            if defs
                .iter()
                .any(|x| x.module == d.module && x.name == d.name)
            {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate def {}.{}",
                    d.module, d.name
                )));
            }
            defs.push(d);
        }
        imports.extend(prog.imports);
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
        traits,
        impls,
        defs,
        main,
        imports,
        law_names: Vec::new(),
        driver_names: Vec::new(),
    })
}

struct Parser {
    tokens: Vec<SpannedToken>,
    i: usize,
    file: String,
    module: String,
}

impl Parser {
    fn current_span(&self) -> Span {
        self.tokens
            .get(self.i)
            .map(|t| t.span.clone())
            .unwrap_or_else(|| Span::new(self.file.clone(), 0, 0))
    }

    fn peek(&self) -> &Token {
        if self.i < self.tokens.len() {
            &self.tokens[self.i].token
        } else {
            &self.tokens[self.tokens.len() - 1].token
        }
    }

    fn bump(&mut self) -> SpannedToken {
        let t = self
            .tokens
            .get(self.i)
            .cloned()
            .unwrap_or_else(|| SpannedToken {
                token: Token::Eof,
                span: Span::new(self.file.clone(), 0, 0),
            });
        if self.i < self.tokens.len() {
            self.i += 1;
        }
        t
    }

    fn expect(&mut self, expected: &Token) -> Result<Span, ParseError> {
        let got = self.bump();
        if &got.token == expected {
            Ok(got.span)
        } else {
            Err(ParseError::At {
                msg: format!("expected {expected:?}, got {:?}", got.token),
                span: got.span,
            })
        }
    }

    fn expect_ident(&mut self) -> Result<(String, Span), ParseError> {
        let got = self.bump();
        match got.token {
            Token::Ident(s) => Ok((s, got.span)),
            other => Err(ParseError::At {
                msg: format!("expected ident, got {other:?}"),
                span: got.span,
            }),
        }
    }

    fn mk(&self, kind: ExprKind, span: Span) -> Expr {
        Expr::new(kind, span)
    }

    fn err(&self, msg: impl Into<String>) -> ParseError {
        ParseError::At {
            msg: msg.into(),
            span: self.current_span(),
        }
    }

    fn parse_program(&mut self) -> Result<Program, ParseError> {
        let mut package = Vec::new();
        if matches!(self.peek(), Token::Package) {
            self.bump();
            package.push(self.expect_ident()?.0);
            while matches!(self.peek(), Token::Dot) {
                self.bump();
                package.push(self.expect_ident()?.0);
            }
        }

        let mut enums = Vec::new();
        let mut traits = Vec::new();
        let mut impls = Vec::new();
        let mut defs = Vec::new();
        let mut imports = Vec::new();
        let mut main = MainDef {
            module: self.module.clone(),
            name: String::new(),
            body: Expr::dummy(ExprKind::Unit),
        };

        loop {
            match self.peek() {
                Token::Enum => enums.push(self.parse_enum()?),
                Token::Record => enums.push(self.parse_record()?),
                Token::Trait => traits.push(self.parse_trait()?),
                Token::Impl => impls.push(self.parse_impl()?),
                Token::Import => imports.push(self.parse_import()?),
                Token::Private => {
                    self.bump();
                    defs.push(self.parse_def(true)?);
                }
                Token::Def => defs.push(self.parse_def(false)?),
                Token::Law => defs.push(self.parse_law()?),
                Token::AtMain => {
                    if !main.name.is_empty() {
                        return Err(self.err("multiple @main"));
                    }
                    main = self.parse_main()?;
                }
                Token::Eof => break,
                other => {
                    return Err(self.err(format!(
                    "expected enum/record/trait/impl/import/def/private def/law/@main, got {other:?}"
                )))
                }
            }
        }

        Ok(Program {
            package,
            enums,
            traits,
            impls,
            defs,
            main,
            imports,
            law_names: Vec::new(),
            driver_names: Vec::new(),
        })
    }

    fn parse_import(&mut self) -> Result<Import, ParseError> {
        self.expect(&Token::Import)?;
        let (from_module, _) = self.expect_ident()?;
        self.expect(&Token::Dot)?;
        let (name, _) = self.expect_ident()?;
        Ok(Import {
            in_module: self.module.clone(),
            from_module,
            name,
        })
    }

    fn parse_main(&mut self) -> Result<MainDef, ParseError> {
        self.expect(&Token::AtMain)?;
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let ty = self.parse_type()?;
        match &ty {
            Type::Io(inner) if matches!(inner.as_ref(), Type::Unit) => {}
            _ => return Err(self.err("@main must have type IO[Unit] in Stage 0")),
        }
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        Ok(MainDef {
            module: self.module.clone(),
            name,
            body,
        })
    }

    fn parse_def(&mut self, is_private: bool) -> Result<FunDef, ParseError> {
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::LParen)?;
        let params = self.parse_param_list_with_tparams(&type_params)?;
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type_with_tparams(&type_params)?;
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        Ok(FunDef {
            module: self.module.clone(),
            name,
            is_private,
            is_law: false,
            is_driver: false,
            type_params,
            params,
            ret,
            body,
        })
    }

    fn parse_law(&mut self) -> Result<FunDef, ParseError> {
        self.expect(&Token::Law)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type()?;
        if !matches!(ret, Type::Bool | Type::Int) {
            return Err(self.err(format!(
                "law `{name}` must return Bool (or Int), got {ret:?}"
            )));
        }
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        Ok(FunDef {
            module: self.module.clone(),
            name,
            is_private: false,
            is_law: true,
            is_driver: false,
            type_params: Vec::new(),
            params: Vec::new(),
            ret,
            body,
        })
    }

    fn parse_type_params(&mut self) -> Result<Vec<String>, ParseError> {
        self.expect(&Token::LBracket)?;
        let mut params = Vec::new();
        loop {
            let (name, _) = self.expect_ident()?;
            if params.iter().any(|p| p == &name) {
                return Err(self.err(format!("duplicate type parameter {name}")));
            }
            params.push(name);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                continue;
            }
            break;
        }
        self.expect(&Token::RBracket)?;
        if params.is_empty() {
            return Err(self.err("expected at least one type parameter"));
        }
        Ok(params)
    }

    fn parse_param_list_with_tparams(
        &mut self,
        type_params: &[String],
    ) -> Result<Vec<Param>, ParseError> {
        let mut params = Vec::new();
        if matches!(self.peek(), Token::RParen) {
            return Ok(params);
        }
        loop {
            let (pname, pty, rfn) = self.parse_name_ty_rfn(type_params)?;
            params.push(Param {
                name: pname,
                ty: pty,
                rfn,
            });
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                continue;
            }
            break;
        }
        Ok(params)
    }

    fn parse_name_ty_rfn(
        &mut self,
        type_params: &[String],
    ) -> Result<(String, Type, Option<Expr>), ParseError> {
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let ty = self.parse_type_with_tparams(type_params)?;
        let rfn = if matches!(self.peek(), Token::Where) {
            self.bump();
            Some(self.parse_or()?)
        } else {
            None
        };
        Ok((name, ty, rfn))
    }

    fn parse_enum(&mut self) -> Result<EnumDef, ParseError> {
        self.expect(&Token::Enum)?;
        let (name, _) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        let mut cases = Vec::new();
        match self.peek() {
            Token::LBrace => {
                self.bump();
                loop {
                    self.expect(&Token::Case)?;
                    cases.push(self.parse_enum_case(&type_params)?);
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
                    cases.push(self.parse_enum_case(&type_params)?);
                }
            }
            other => return Err(self.err(format!("enum body expected `:` or `{{`, got {other:?}"))),
        }
        if cases.is_empty() {
            return Err(self.err(format!("enum {name} has no cases")));
        }
        Ok(EnumDef {
            module: self.module.clone(),
            name,
            type_params,
            cases,
            is_record: false,
        })
    }

    /// `record Name(f1: T1, f2: T2, …)` — single-case enum sugar.
    fn parse_record(&mut self) -> Result<EnumDef, ParseError> {
        self.expect(&Token::Record)?;
        let (name, _) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::LParen)?;
        let mut fields = Vec::new();
        let mut field_rfns = Vec::new();
        loop {
            let (fname, fty, rfn) = self.parse_name_ty_rfn(&type_params)?;
            fields.push((fname, fty));
            field_rfns.push(rfn);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                continue;
            }
            break;
        }
        self.expect(&Token::RParen)?;
        if fields.is_empty() {
            return Err(self.err(format!("record {name} needs at least one field")));
        }
        Ok(EnumDef {
            module: self.module.clone(),
            name: name.clone(),
            type_params,
            cases: vec![EnumCase {
                name: name.clone(),
                fields,
                field_rfns,
            }],
            is_record: true,
        })
    }

    /// `trait Show: def show(): String`
    fn parse_trait(&mut self) -> Result<TraitDef, ParseError> {
        self.expect(&Token::Trait)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let mut methods = Vec::new();
        while matches!(self.peek(), Token::Def) {
            methods.push(self.parse_trait_method()?);
        }
        if methods.is_empty() {
            return Err(self.err(format!("trait {name} has no methods")));
        }
        Ok(TraitDef {
            module: self.module.clone(),
            name,
            methods,
        })
    }

    fn parse_trait_method(&mut self) -> Result<TraitMethod, ParseError> {
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::LParen)?;
        let params = self.parse_param_list()?;
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type()?;
        Ok(TraitMethod { name, params, ret })
    }

    /// `impl Show for Point: def show(): String = …`
    fn parse_impl(&mut self) -> Result<ImplDef, ParseError> {
        self.expect(&Token::Impl)?;
        let (trait_name, _) = self.expect_ident()?;
        self.expect(&Token::For)?;
        let (for_type, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let mut methods = Vec::new();
        while matches!(self.peek(), Token::Def) {
            methods.push(self.parse_impl_method()?);
        }
        if methods.is_empty() {
            return Err(self.err(format!("impl {trait_name} for {for_type} has no methods")));
        }
        Ok(ImplDef {
            module: self.module.clone(),
            trait_name,
            for_type,
            methods,
        })
    }

    fn parse_impl_method(&mut self) -> Result<ImplMethod, ParseError> {
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::LParen)?;
        let params = self.parse_param_list()?;
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type()?;
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        Ok(ImplMethod {
            name,
            params,
            ret,
            body,
        })
    }

    fn parse_param_list(&mut self) -> Result<Vec<Param>, ParseError> {
        self.parse_param_list_with_tparams(&[])
    }

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        self.parse_type_with_tparams(&[])
    }

    fn parse_type_with_tparams(&mut self, type_params: &[String]) -> Result<Type, ParseError> {
        let (name, _) = self.expect_ident()?;
        if type_params.iter().any(|p| p == &name) {
            if matches!(self.peek(), Token::LBracket) {
                return Err(self.err(format!("type parameter {name} takes no type arguments")));
            }
            return Ok(Type::Var(name));
        }
        match name.as_str() {
            "Unit" | "Int" | "String" | "Bool" | "List" => {
                if matches!(self.peek(), Token::LBracket) {
                    return Err(self.err(format!("{name} takes no type arguments")));
                }
                Ok(match name.as_str() {
                    "Unit" => Type::Unit,
                    "Int" => Type::Int,
                    "String" => Type::String,
                    "Bool" => Type::Bool,
                    _ => Type::List,
                })
            }
            "IO" => {
                self.expect(&Token::LBracket)?;
                let inner = self.parse_type_with_tparams(type_params)?;
                self.expect(&Token::RBracket)?;
                Ok(Type::Io(Box::new(inner)))
            }
            _ => {
                if matches!(self.peek(), Token::LBracket) {
                    self.bump();
                    let mut args = Vec::new();
                    loop {
                        args.push(self.parse_type_with_tparams(type_params)?);
                        if matches!(self.peek(), Token::Comma) {
                            self.bump();
                            continue;
                        }
                        break;
                    }
                    self.expect(&Token::RBracket)?;
                    return Ok(Type::App(name, args));
                }
                Ok(Type::Adt(name))
            }
        }
    }

    /// `Name` or `Name(f1: T1, f2: T2, …)` (payload types checked in typer).
    fn parse_enum_case(&mut self, type_params: &[String]) -> Result<EnumCase, ParseError> {
        let (name, _) = self.expect_ident()?;
        let mut fields = Vec::new();
        let mut field_rfns = Vec::new();
        if matches!(self.peek(), Token::LParen) {
            self.bump();
            loop {
                let (fname, fty, rfn) = self.parse_name_ty_rfn(type_params)?;
                fields.push((fname, fty));
                field_rfns.push(rfn);
                if matches!(self.peek(), Token::Comma) {
                    self.bump();
                    continue;
                }
                break;
            }
            self.expect(&Token::RParen)?;
        }
        Ok(EnumCase {
            name,
            fields,
            field_rfns,
        })
    }

    /// Expr body for `def` / `@main` / lambda (no statement-block grammar).
    fn parse_block(&mut self) -> Result<Expr, ParseError> {
        self.parse_expr()
    }

    /// `if` then/else: single expression (nested `for` for multi-bind arms).
    fn parse_if_branch(&mut self) -> Result<Expr, ParseError> {
        self.parse_expr()
    }

    /// Binder name: ident or `_`.
    fn parse_binder_name(&mut self) -> Result<String, ParseError> {
        let got = self.bump();
        match got.token {
            Token::Ident(s) => Ok(s),
            Token::Underscore => Ok("_".into()),
            other => Err(ParseError::At {
                msg: format!("expected binder name, got {other:?}"),
                span: got.span,
            }),
        }
    }

    /// `for { binders… } yield expr`
    fn parse_for(&mut self) -> Result<Expr, ParseError> {
        self.expect(&Token::For)?;
        self.expect(&Token::LBrace)?;
        let mut binders = Vec::new();
        loop {
            match self.peek() {
                Token::RBrace => break,
                Token::Yield => {
                    return Err(self.err("`yield` belongs after `}`: `for { … } yield e`"))
                }
                _ => {
                    let name = self.parse_binder_name()?;
                    match self.peek() {
                        Token::Eq => {
                            self.bump();
                            let value = self.parse_expr()?;
                            binders.push(ForBinder::Eq { name, value });
                        }
                        Token::LeftArrow => {
                            self.bump();
                            let value = self.parse_expr()?;
                            binders.push(ForBinder::Draw { name, value });
                        }
                        other => {
                            return Err(
                                self.err(format!("for binder expected `=` or `<-`, got {other:?}"))
                            )
                        }
                    }
                }
            }
        }
        self.expect(&Token::RBrace)?;
        self.expect(&Token::Yield)?;
        let body = self.parse_expr()?;
        let span = if let Some(b) = binders.first() {
            let vs = match b {
                ForBinder::Eq { value, .. } | ForBinder::Draw { value, .. } => value.span.clone(),
            };
            vs.cover(&body.span)
        } else {
            body.span.clone()
        };
        Ok(self.mk(
            ExprKind::For {
                binders,
                body: Box::new(body),
            },
            span,
        ))
    }

    fn parse_expr(&mut self) -> Result<Expr, ParseError> {
        self.parse_or()
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;
        while matches!(self.peek(), Token::PipePipe) {
            self.bump();
            let right = self.parse_and()?;
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op: BinOp::Or,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_cmp()?;
        while matches!(self.peek(), Token::AmpAmp) {
            self.bump();
            let right = self.parse_cmp()?;
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op: BinOp::And,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
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
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
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
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
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
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
        }
        Ok(left)
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;
        loop {
            match self.peek() {
                Token::Dot => {
                    self.bump();
                    let (method, _) = self.expect_ident()?;
                    match method.as_str() {
                        "flatMap" => {
                            self.expect(&Token::LParen)?;
                            let (param, body) = self.parse_lambda()?;
                            self.expect(&Token::RParen)?;
                            let span = expr.span.clone().cover(&body.span);
                            expr = self.mk(
                                ExprKind::FlatMap {
                                    inner: Box::new(expr),
                                    param,
                                    body: Box::new(body),
                                },
                                span,
                            );
                        }
                        "handleErrorWith" => {
                            self.expect(&Token::LParen)?;
                            let (_param, body) = self.parse_lambda()?;
                            self.expect(&Token::RParen)?;
                            let span = expr.span.clone().cover(&body.span);
                            expr = self.mk(
                                ExprKind::HandleErrorWith {
                                    inner: Box::new(expr),
                                    body: Box::new(body),
                                },
                                span,
                            );
                        }
                        "attempt" => {
                            if matches!(self.peek(), Token::LParen) {
                                self.bump();
                                self.expect(&Token::RParen)?;
                            }
                            let span = expr.span.clone();
                            expr = self.mk(
                                ExprKind::Attempt {
                                    inner: Box::new(expr),
                                },
                                span,
                            );
                        }
                        other => {
                            // `expr.method(args)` trait call, else `expr.field` projection.
                            if matches!(self.peek(), Token::LParen) {
                                let args = self.parse_args()?;
                                let end = args
                                    .last()
                                    .map(|a| a.span.clone())
                                    .unwrap_or_else(|| expr.span.clone());
                                let span = expr.span.clone().cover(&end);
                                expr = self.mk(
                                    ExprKind::MethodCall {
                                        receiver: Box::new(expr),
                                        method: other.to_string(),
                                        args,
                                    },
                                    span,
                                );
                            } else {
                                let span = expr.span.clone();
                                expr = self.mk(
                                    ExprKind::Field {
                                        base: Box::new(expr),
                                        field: other.to_string(),
                                    },
                                    span,
                                );
                            }
                        }
                    }
                }
                Token::Match => {
                    self.bump();
                    let arms = self.parse_match_arms()?;
                    let end = arms
                        .last()
                        .map(|a| a.body.span.clone())
                        .unwrap_or_else(|| expr.span.clone());
                    let span = expr.span.clone().cover(&end);
                    expr = self.mk(
                        ExprKind::Match {
                            scrutinee: Box::new(expr),
                            arms,
                        },
                        span,
                    );
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
            return Err(self.err("match needs at least one case"));
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
                let (name, _) = self.expect_ident()?;
                if matches!(self.peek(), Token::Dot) {
                    self.bump();
                    let (case_name, _) = self.expect_ident()?;
                    let binds = self.parse_pattern_binds()?;
                    Ok(Pattern::Adt {
                        enum_name: name,
                        case_name,
                        binds,
                        type_args: Vec::new(),
                    })
                } else if matches!(self.peek(), Token::LParen) {
                    // Record / single-case sugar: `case Point(x, y)`.
                    let binds = self.parse_pattern_binds()?;
                    Ok(Pattern::Adt {
                        enum_name: name.clone(),
                        case_name: name,
                        binds,
                        type_args: Vec::new(),
                    })
                } else {
                    Err(self.err(format!(
                        "expected `.Case` or `(binds)` after pattern {name}"
                    )))
                }
            }
            other => Err(self.err(format!("expected pattern, got {other:?}"))),
        }
    }

    fn parse_pattern_binds(&mut self) -> Result<Vec<String>, ParseError> {
        if !matches!(self.peek(), Token::LParen) {
            return Ok(Vec::new());
        }
        self.bump();
        let mut binds = Vec::new();
        loop {
            let (b, _) = self.expect_ident()?;
            binds.push(b);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                continue;
            }
            break;
        }
        self.expect(&Token::RParen)?;
        Ok(binds)
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
            _ => Err(self.err("expected `_ => expr`, `() => expr`, or `name => expr`")),
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
        let start = self.current_span();
        match self.peek().clone() {
            Token::For => self.parse_for(),
            Token::If => {
                self.bump();
                self.expect(&Token::LParen)?;
                let cond = self.parse_expr()?;
                self.expect(&Token::RParen)?;
                let then_branch = self.parse_if_branch()?;
                self.expect(&Token::Else)?;
                let else_branch = self.parse_if_branch()?;
                let span = start.cover(&else_branch.span);
                Ok(self.mk(
                    ExprKind::If {
                        cond: Box::new(cond),
                        then_branch: Box::new(then_branch),
                        else_branch: Box::new(else_branch),
                    },
                    span,
                ))
            }
            Token::LParen => {
                self.bump();
                if matches!(self.peek(), Token::RParen) {
                    let end = self.bump().span;
                    return Ok(self.mk(ExprKind::Unit, start.cover(&end)));
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
                let end = self.expect(&Token::RBracket)?;
                Ok(self.mk(ExprKind::ListLit { elems }, start.cover(&end)))
            }
            Token::IntLit(n) => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::IntLit(n), start.cover(&end)))
            }
            Token::StringLit(s) => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::StrLit(s), start.cover(&end)))
            }
            Token::InterpString(parts) => {
                self.bump();
                self.parse_interpolate(parts, start)
            }
            Token::Minus => {
                self.bump();
                let got = self.bump();
                let n = match got.token {
                    Token::IntLit(n) => n,
                    other => {
                        return Err(ParseError::At {
                            msg: format!("expected int after `-`, got {other:?}"),
                            span: got.span,
                        })
                    }
                };
                Ok(self.mk(ExprKind::IntLit(-n), start.cover(&got.span)))
            }
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                let body = self.parse_block()?;
                let span = start.cover(&body.span);
                Ok(self.mk(
                    ExprKind::Lambda {
                        param: None,
                        body: Box::new(body),
                    },
                    span,
                ))
            }
            Token::Ident(name) if name == "IO" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let (method, _) = self.expect_ident()?;
                match method.as_str() {
                    "println" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(self.err("IO.println expects 1 arg"));
                        }
                        let arg = args.into_iter().next().unwrap();
                        let span = start.cover(&arg.span);
                        Ok(self.mk(ExprKind::IoPrintln(Box::new(arg)), span))
                    }
                    "sleep" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(self.err("IO.sleep expects 1 arg"));
                        }
                        let arg = args.into_iter().next().unwrap();
                        let span = start.cover(&arg.span);
                        Ok(self.mk(ExprKind::IoSleep(Box::new(arg)), span))
                    }
                    "fail" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(self.err("IO.fail expects 1 arg"));
                        }
                        let arg = args.into_iter().next().unwrap();
                        let span = start.cover(&arg.span);
                        Ok(self.mk(ExprKind::IoFail(Box::new(arg)), span))
                    }
                    "pure" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(self.err("IO.pure expects 1 arg"));
                        }
                        let arg = args.into_iter().next().unwrap();
                        let span = start.cover(&arg.span);
                        Ok(self.mk(ExprKind::IoPure(Box::new(arg)), span))
                    }
                    "timeout" => {
                        let args = self.parse_args()?;
                        if args.len() != 2 {
                            return Err(self.err("IO.timeout expects 2 args"));
                        }
                        let mut it = args.into_iter();
                        let ms = it.next().unwrap();
                        let inner = it.next().unwrap();
                        let span = start.cover(&inner.span);
                        Ok(self.mk(
                            ExprKind::IoTimeout {
                                ms: Box::new(ms),
                                inner: Box::new(inner),
                            },
                            span,
                        ))
                    }
                    "race" | "both" | "ensure" => {
                        let args = self.parse_args()?;
                        if args.len() != 2 {
                            return Err(self.err(format!("IO.{method} expects 2 args")));
                        }
                        let mut it = args.into_iter();
                        let left = it.next().unwrap();
                        let right = it.next().unwrap();
                        let span = start.cover(&right.span);
                        if method == "race" {
                            Ok(self.mk(
                                ExprKind::IoRace {
                                    left: Box::new(left),
                                    right: Box::new(right),
                                },
                                span,
                            ))
                        } else if method == "both" {
                            Ok(self.mk(
                                ExprKind::IoBoth {
                                    left: Box::new(left),
                                    right: Box::new(right),
                                },
                                span,
                            ))
                        } else {
                            Ok(self.mk(
                                ExprKind::IoEnsure {
                                    inner: Box::new(left),
                                    finalizer: Box::new(right),
                                },
                                span,
                            ))
                        }
                    }
                    other => Err(self.err(format!("unknown IO.{other}"))),
                }
            }
            Token::Ident(name) if name == "Ui" => {
                self.bump();
                self.expect(&Token::Dot)?;
                let (method, _) = self.expect_ident()?;
                let callee = format!("Ui.{method}");
                let args = if matches!(self.peek(), Token::LParen) {
                    self.parse_args()?
                } else {
                    Vec::new()
                };
                let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                Ok(self.mk(ExprKind::Call { callee, args }, start.cover(&end)))
            }
            Token::Ident(name)
                if matches!(
                    name.as_str(),
                    "Str"
                        | "List"
                        | "Fs"
                        | "Sys"
                        | "Clock"
                        | "Random"
                        | "Net"
                        | "Impurity"
                        | "Signal"
                        | "View"
                        | "Theme"
                        | "Ref"
                        | "Queue"
                        | "Deferred"
                        | "Resource"
                        | "Stream"
                ) =>
            {
                self.bump();
                self.expect(&Token::Dot)?;
                let (method, _) = self.expect_ident()?;
                let callee = format!("{name}.{method}");
                let args = if matches!(self.peek(), Token::LParen) {
                    self.parse_args()?
                } else {
                    Vec::new()
                };
                let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                Ok(self.mk(ExprKind::Call { callee, args }, start.cover(&end)))
            }
            Token::Ident(name) => {
                self.bump();
                if matches!(self.peek(), Token::Arrow) {
                    self.bump();
                    let body = self.parse_block()?;
                    let span = start.cover(&body.span);
                    return Ok(self.mk(
                        ExprKind::Lambda {
                            param: Some(name),
                            body: Box::new(body),
                        },
                        span,
                    ));
                }
                if matches!(self.peek(), Token::LParen) {
                    let args = self.parse_args()?;
                    let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                    return Ok(self.mk(ExprKind::Call { callee: name, args }, start.cover(&end)));
                }
                if matches!(self.peek(), Token::Dot) {
                    self.bump();
                    let (case_name, case_span) = self.expect_ident()?;
                    if matches!(self.peek(), Token::LParen) {
                        let args = self.parse_args()?;
                        let end = args.last().map(|a| a.span.clone()).unwrap_or(case_span);
                        return Ok(self.mk(
                            ExprKind::Call {
                                callee: format!("{name}.{case_name}"),
                                args,
                            },
                            start.cover(&end),
                        ));
                    }
                    // Capitalized `Color.Red` / `A.tag` stay ADT/module; lowercase `p.x` is field access.
                    if name.chars().next().is_some_and(|c| c.is_ascii_uppercase()) {
                        Ok(self.mk(
                            ExprKind::AdtConstruct {
                                enum_name: name,
                                case_name,
                                args: Vec::new(),
                                type_args: Vec::new(),
                            },
                            start.cover(&case_span),
                        ))
                    } else {
                        Ok(self.mk(
                            ExprKind::Field {
                                base: Box::new(self.mk(ExprKind::Var(name), start.clone())),
                                field: case_name,
                            },
                            start.cover(&case_span),
                        ))
                    }
                } else {
                    Ok(self.mk(ExprKind::Var(name), start))
                }
            }
            other => Err(self.err(format!("unexpected token {other:?}"))),
        }
    }

    fn parse_interpolate(
        &mut self,
        parts: Vec<InterpTok>,
        start: Span,
    ) -> Result<Expr, ParseError> {
        let mut out = Vec::new();
        for part in parts {
            match part {
                InterpTok::Lit(s) => out.push(InterpPart::Lit(s)),
                InterpTok::Ident(name) => out.push(InterpPart::Expr(
                    self.mk(ExprKind::Var(name), start.clone()),
                )),
                InterpTok::Brace(body) => {
                    let tokens = lex(&body).map_err(|e| ParseError::At {
                        msg: e.to_string(),
                        span: start.clone(),
                    })?;
                    let mut nested = Parser {
                        tokens,
                        i: 0,
                        file: self.file.clone(),
                        module: self.module.clone(),
                    };
                    let e = nested.parse_expr()?;
                    if !matches!(nested.peek(), Token::Eof) {
                        return Err(self.err("trailing tokens in interpolation hole"));
                    }
                    out.push(InterpPart::Expr(e));
                }
            }
        }
        if out.is_empty() {
            out.push(InterpPart::Lit(String::new()));
        }
        Ok(self.mk(ExprKind::Interpolate { parts: out }, start))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hello")"#).unwrap();
        assert_eq!(p.main.name, "main");
        assert!(matches!(p.main.body.kind, ExprKind::IoPrintln(_)));
    }

    #[test]
    fn parse_lambda_literal_underscore() {
        let src = r#"@main def main: IO[Unit] = View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Call { callee, args } if callee == "View.button" => {
                assert_eq!(args.len(), 2);
                assert!(matches!(args[1].kind, ExprKind::Lambda { param: None, .. }));
            }
            other => panic!("expected View.button call, got {other:?}"),
        }
    }

    #[test]
    fn parse_resource_make_use() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert!(matches!(
                    &binders[0],
                    ForBinder::Eq { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "Resource.make")
                ));
                assert!(matches!(
                    &binders[1],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "Resource.use")
                ));
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_stream_emit_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.concat(Stream.emit("a"), Stream.eval(IO.pure("b")))
    xs <- Stream.compileToList(s)
    _ <- Stream.drain(Stream.emit("c"))
  } yield ()
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert!(matches!(
                    &binders[0],
                    ForBinder::Eq { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "Stream.concat")
                ));
                assert!(matches!(
                    &binders[1],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "Stream.compileToList")
                ));
                assert!(matches!(
                    &binders[2],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "Stream.drain")
                ));
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_net_serve_once() {
        let src = r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.pure(path))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Call { callee, args } if callee == "Net.serveOnce" => {
                assert_eq!(args.len(), 2);
                assert!(matches!(args[1].kind, ExprKind::Lambda { .. }));
            }
            other => panic!("expected Net.serveOnce, got {other:?}"),
        }
    }

    #[test]
    fn parse_lambda_literal_named_param() {
        let src = r#"@main def main: IO[Unit] = View.button("+1", self => Signal.set(count, 1))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Call { args, .. } => assert!(matches!(
                &args[1].kind,
                ExprKind::Lambda { param: Some(n), .. } if n == "self"
            )),
            other => panic!("expected call, got {other:?}"),
        }
    }

    #[test]
    fn parse_flatmap_bound() {
        let src = r#"@main def main: IO[Unit] = Fs.read("x").flatMap(s => IO.println(s))"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.main.body.kind,
            ExprKind::FlatMap {
                param: Some(n),
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
        assert!(matches!(p.main.body.kind, ExprKind::If { .. }));
    }

    #[test]
    fn parse_private_def() {
        let src = r#"
private def helper(): String = "x"
def tag(): String = helper()
@main def main: IO[Unit] = IO.println(tag())
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.defs.len(), 2);
        assert!(p.defs[0].is_private);
        assert!(!p.defs[1].is_private);
        assert_eq!(p.defs[0].name, "helper");
    }

    #[test]
    fn parse_law_declaration() {
        let src = r#"
law always: Bool = 1 == 1
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.defs.len(), 1);
        assert!(p.defs[0].is_law);
        assert_eq!(p.defs[0].name, "always");
        assert!(matches!(p.defs[0].ret, Type::Bool));
        assert!(p.defs[0].params.is_empty());
    }

    #[test]
    fn parse_param_where() {
        let src = r#"
def note(n: Int where n >= 0): Unit = ()
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert!(p.defs[0].params[0].rfn.is_some());
    }

    #[test]
    fn parse_record_field_where() {
        let src = r#"
record Point(x: Int where x >= 0, y: Int)
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert!(p.enums[0].cases[0].field_rfn(0).is_some());
        assert!(p.enums[0].cases[0].field_rfn(1).is_none());
    }

    #[test]
    fn parse_law_rejects_non_bool() {
        let src = r#"
law bad: String = "x"
@main def main: IO[Unit] = IO.println("ok")
"#;
        assert!(parse(src).is_err());
    }

    #[test]
    fn parse_if_for_led_else_block() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    n = 1
  } yield if (n == 0) ()
  else
    for {
      a = IO.println("a")
      b = IO.println("b")
    } yield IO.println("c")
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { body, .. } => match &body.kind {
                ExprKind::If { else_branch, .. } => match &else_branch.kind {
                    ExprKind::For { binders, .. } => {
                        assert_eq!(binders.len(), 2);
                    }
                    other => panic!("expected for-else, got {other:?}"),
                },
                other => panic!("expected if, got {other:?}"),
            },
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_if_does_not_steal_following_binder() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    path = if (1 == 0) "a" else "b"
    draft = "x"
  } yield IO.println(draft)
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert_eq!(binders.len(), 2);
                assert!(matches!(&binders[0], ForBinder::Eq { name, .. } if name == "path"));
                assert!(matches!(&binders[1], ForBinder::Eq { name, .. } if name == "draft"));
            }
            other => panic!("expected for, got {other:?}"),
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
  for {
    c = Color.Red
  } yield c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.package, vec!["demo", "color"]);
        assert_eq!(p.enums.len(), 1);
        assert_eq!(p.enums[0].cases.len(), 2);
        assert!(p.enums[0].cases[0].fields.is_empty());
        assert!(matches!(p.main.body.kind, ExprKind::For { .. }));
    }

    #[test]
    fn parse_payload_enum_and_match_bind() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].cases[0].name, "Some");
        assert_eq!(p.enums[0].cases[0].fields.len(), 1);
        assert_eq!(p.enums[0].cases[0].fields[0].0, "x");
        assert!(matches!(
            p.enums[0].cases[0].fields[0].1,
            crate::ast::Type::Int
        ));
        assert!(p.enums[0].cases[1].fields.is_empty());
        // Dotted call with args stays Call until lower resolves known enum ctors.
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, arms } => {
                assert!(matches!(
                    &scrutinee.kind,
                    ExprKind::Call { callee, args }
                        if callee == "Opt.Some" && args.len() == 1
                ));
                match &arms[0].pattern {
                    Pattern::Adt {
                        enum_name,
                        case_name,
                        binds,
                        ..
                    } => {
                        assert_eq!(enum_name, "Opt");
                        assert_eq!(case_name, "Some");
                        assert_eq!(binds, &["n".to_string()]);
                    }
                    other => panic!("expected payload pattern, got {other:?}"),
                }
                match &arms[1].pattern {
                    Pattern::Adt {
                        binds, case_name, ..
                    } if case_name == "None" && binds.is_empty() => {}
                    other => panic!("expected nullary None, got {other:?}"),
                }
            }
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_multi_field_payload_enum_and_match() {
        let src = r#"
enum Pair:
  case Pair(a: Int, b: String)
@main def main: IO[Unit] =
  Pair.Pair(1, "x") match {
    case Pair.Pair(x, y) => IO.println(y)
  }
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].cases[0].fields.len(), 2);
        assert_eq!(p.enums[0].cases[0].fields[0].0, "a");
        assert_eq!(p.enums[0].cases[0].fields[1].0, "b");
        assert!(matches!(
            p.enums[0].cases[0].fields[1].1,
            crate::ast::Type::String
        ));
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => {
                    assert_eq!(binds, &["x".to_string(), "y".to_string()]);
                }
                other => panic!("expected multi-bind pattern, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_list_literal_empty() {
        let p = parse(r#"@main def main: IO[Unit] = []"#).unwrap();
        match &p.main.body.kind {
            ExprKind::ListLit { elems } => assert!(elems.is_empty()),
            other => panic!("expected ListLit, got {other:?}"),
        }
    }

    #[test]
    fn parse_list_literal_elems() {
        let p = parse(r#"@main def main: IO[Unit] = [a, b]"#).unwrap();
        match &p.main.body.kind {
            ExprKind::ListLit { elems } => {
                assert_eq!(elems.len(), 2);
                assert!(matches!(&elems[0].kind, ExprKind::Var(n) if n == "a"));
                assert!(matches!(&elems[1].kind, ExprKind::Var(n) if n == "b"));
            }
            other => panic!("expected ListLit, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_eq_binders() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    root = View.column()
  } yield Ui.run(root)
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, body } => {
                assert_eq!(binders.len(), 2);
                assert!(matches!(
                    &binders[0],
                    ForBinder::Eq { name, .. } if name == "count"
                ));
                assert!(matches!(
                    &binders[1],
                    ForBinder::Eq { name, .. } if name == "root"
                ));
                assert!(matches!(&body.kind, ExprKind::Call { callee, .. } if callee == "Ui.run"));
            }
            other => panic!("expected For, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_mixed_eq_and_draw() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    path = "/tmp/x"
    s <- Fs.read(path)
    _ <- IO.println(s)
  } yield IO.pure(())
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert_eq!(binders.len(), 3);
                assert!(matches!(&binders[0], ForBinder::Eq { name, .. } if name == "path"));
                assert!(matches!(&binders[1], ForBinder::Draw { name, .. } if name == "s"));
                assert!(matches!(&binders[2], ForBinder::Draw { name, .. } if name == "_"));
            }
            other => panic!("expected For, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_rejects_statement_val_ident() {
        // `val` is an ordinary binder name in the kernel dialect.
        let src = r#"
@main def main: IO[Unit] =
  for {
    val = 1
  } yield IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(matches!(p.main.body.kind, ExprKind::For { .. }));
    }

    #[test]
    fn parse_sources_allows_same_def_in_two_modules() {
        let p = parse_sources(&[
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".into()),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
            ),
        ])
        .unwrap();
        assert_eq!(p.defs.len(), 2);
        assert_eq!(p.defs[0].module, "A");
        assert_eq!(p.defs[1].module, "B");
        assert_eq!(p.main.module, "Main");
    }

    #[test]
    fn parse_sources_rejects_duplicate_def_in_same_module() {
        let err = parse_sources(&[
            (
                "pkg/src/A.scuzz".into(),
                "def tag(): String = \"a\"\ndef tag(): String = \"x\"\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
            ),
        ])
        .unwrap_err()
        .to_string();
        assert!(err.contains("duplicate def"), "unexpected: {err}");
    }

    #[test]
    fn parse_generic_enum_decl() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].type_params, &["T".to_string()]);
        assert!(matches!(
            &p.enums[0].cases[0].fields[0].1,
            crate::ast::Type::Var(n) if n == "T"
        ));
    }

    #[test]
    fn parse_multi_param_enum_decl() {
        let src = r#"
enum Either[L, R]:
  case Left(x: L)
  case Right(y: R)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].type_params, &["L".to_string(), "R".to_string()]);
        assert!(matches!(
            &p.enums[0].cases[1].fields[0].1,
            crate::ast::Type::Var(n) if n == "R"
        ));
    }

    #[test]
    fn parse_generic_record_decl() {
        let src = r#"
record Pair[A, B](a: A, b: B)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(p.enums[0].is_record);
        assert_eq!(p.enums[0].type_params, &["A".to_string(), "B".to_string()]);
        assert!(matches!(
            &p.enums[0].cases[0].fields[1].1,
            crate::ast::Type::Var(n) if n == "B"
        ));
    }

    #[test]
    fn parse_type_application_in_def_sig() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def f(o: Opt[Int]): Int = 1
def g[T](o: Opt[T]): Opt[T] = o
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.defs[0].params[0].ty,
            crate::ast::Type::App(n, args)
                if n == "Opt" && matches!(&args[0], crate::ast::Type::Int)
        ));
        assert!(matches!(
            &p.defs[1].ret,
            crate::ast::Type::App(n, args)
                if n == "Opt" && matches!(&args[0], crate::ast::Type::Var(v) if v == "T")
        ));
    }

    #[test]
    fn parse_multi_param_def() {
        let src = r#"
def second[A, B](a: A, b: B): B = b
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.defs[0].type_params, &["A".to_string(), "B".to_string()]);
    }

    #[test]
    fn parse_rejects_type_args_on_type_param() {
        let src = r#"
def f[T](x: T[Int]): T = x
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("type parameter T takes no type arguments"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn parse_rejects_type_args_on_builtin() {
        let src = r#"
def f(x: List[Int]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("List takes no type arguments"),
            "unexpected: {err}"
        );
    }
}

use crate::ast::{
    BinOp, EnumCase, EnumDef, Expr, ExprKind, ForBinder, FunDef, ImplDef, ImplMethod, Import,
    InterpPart, MainDef, MatchArm, Param, Pattern, Program, TraitDef, TraitMethod, Type, UnOp,
};
use crate::lexer::{lex, InterpTok, LexError, SpannedToken, Token};
use crate::resolve::module_id_from_label;
use crate::span::{offset_to_line_col, Span};
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
        source: source.to_string(),
    };
    p.parse_program()
}

fn empty_program(file: &str) -> Program {
    Program {
        package: Vec::new(),
        enums: Vec::new(),
        traits: Vec::new(),
        impls: Vec::new(),
        defs: Vec::new(),
        main: MainDef {
            module: module_id_from_label(file),
            name: String::new(),
            body: Expr::dummy(ExprKind::Unit),
        },
        imports: Vec::new(),
        law_names: Vec::new(),
        driver_names: Vec::new(),
    }
}

/// Parse one file and recover to the next top-level item after an error.
pub fn parse_file_recovering(source: &str, file: &str) -> (Program, Vec<ParseError>) {
    let mut tokens = match lex(source) {
        Ok(t) => t,
        Err(e) => {
            let off = e.offset();
            return (
                empty_program(file),
                vec![ParseError::At {
                    msg: e.to_string(),
                    span: Span::new(file.to_string(), off, off),
                }],
            );
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
        source: source.to_string(),
    };
    p.parse_program_recovering()
}

/// Parse multiple source files into one program. Packages must agree. Defs and
/// enums are namespaced by file-stem module. The same bare name in two modules is allowed.
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

/// Parse many files. Recover inside each file. Collect every parse error.
pub fn parse_sources_recovering(
    sources: &[(String, String)],
) -> (Option<Program>, Vec<ParseError>) {
    let mut errors = Vec::new();
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut traits: Vec<TraitDef> = Vec::new();
    let mut impls: Vec<ImplDef> = Vec::new();
    let mut defs: Vec<FunDef> = Vec::new();
    let mut imports: Vec<Import> = Vec::new();
    let mut main: Option<MainDef> = None;

    for (name, src) in sources {
        let (prog, file_errs) = parse_file_recovering(src, name);
        errors.extend(file_errs);
        if !prog.package.is_empty() {
            match &package {
                None => package = Some(prog.package.clone()),
                Some(p) if *p == prog.package => {}
                Some(p) => errors.push(ParseError::Msg(format!(
                    "{name}: package {:?} conflicts with {:?}",
                    prog.package, p
                ))),
            }
        }
        for e in prog.enums {
            if enums
                .iter()
                .any(|x| x.module == e.module && x.name == e.name)
            {
                errors.push(ParseError::Msg(format!(
                    "{name}: duplicate enum {}.{}",
                    e.module, e.name
                )));
                continue;
            }
            enums.push(e);
        }
        for t in prog.traits {
            if traits
                .iter()
                .any(|x| x.module == t.module && x.name == t.name)
            {
                errors.push(ParseError::Msg(format!(
                    "{name}: duplicate trait {}.{}",
                    t.module, t.name
                )));
                continue;
            }
            traits.push(t);
        }
        for im in prog.impls {
            if impls.iter().any(|x| {
                x.module == im.module && x.trait_name == im.trait_name && x.for_type == im.for_type
            }) {
                errors.push(ParseError::Msg(format!(
                    "{name}: duplicate impl {} for {}",
                    im.trait_name, im.for_type
                )));
                continue;
            }
            impls.push(im);
        }
        for d in prog.defs {
            if defs
                .iter()
                .any(|x| x.module == d.module && x.name == d.name)
            {
                errors.push(ParseError::Msg(format!(
                    "{name}: duplicate def {}.{}",
                    d.module, d.name
                )));
                continue;
            }
            defs.push(d);
        }
        imports.extend(prog.imports);
        if !prog.main.name.is_empty() {
            if main.is_some() {
                errors.push(ParseError::Msg(format!(
                    "{name}: multiple @main definitions"
                )));
            } else {
                main = Some(prog.main);
            }
        }
    }

    let Some(main) = main else {
        errors.push(ParseError::Msg("no @main definition".into()));
        return (None, errors);
    };
    (
        Some(Program {
            package: package.unwrap_or_default(),
            enums,
            traits,
            impls,
            defs,
            main,
            imports,
            law_names: Vec::new(),
            driver_names: Vec::new(),
        }),
        errors,
    )
}

struct Parser {
    tokens: Vec<SpannedToken>,
    i: usize,
    file: String,
    module: String,
    source: String,
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

    fn at_item_start(&self) -> bool {
        matches!(
            self.peek(),
            Token::Enum
                | Token::Record
                | Token::Trait
                | Token::Impl
                | Token::Import
                | Token::Private
                | Token::Def
                | Token::Law
                | Token::AtMain
                | Token::Eof
        )
    }

    fn skip_to_item(&mut self) {
        if !matches!(self.peek(), Token::Eof) {
            self.bump();
        }
        while !self.at_item_start() {
            self.bump();
        }
    }

    fn parse_program(&mut self) -> Result<Program, ParseError> {
        let (p, errs) = self.parse_program_recovering();
        match errs.into_iter().next() {
            Some(e) => Err(e),
            None => Ok(p),
        }
    }

    fn parse_program_recovering(&mut self) -> (Program, Vec<ParseError>) {
        let mut errors = Vec::new();
        let mut package = Vec::new();
        if matches!(self.peek(), Token::Package) {
            self.bump();
            match self.expect_ident() {
                Ok((n, _)) => {
                    package.push(n);
                    while matches!(self.peek(), Token::Dot) {
                        self.bump();
                        match self.expect_ident() {
                            Ok((n, _)) => package.push(n),
                            Err(e) => {
                                errors.push(e);
                                self.skip_to_item();
                                break;
                            }
                        }
                    }
                }
                Err(e) => {
                    errors.push(e);
                    self.skip_to_item();
                }
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
                Token::Enum => match self.parse_enum() {
                    Ok(e) => enums.push(e),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Record => match self.parse_record() {
                    Ok(e) => enums.push(e),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Trait => match self.parse_trait() {
                    Ok(t) => traits.push(t),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Impl => match self.parse_impl() {
                    Ok(i) => impls.push(i),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Import => match self.parse_import() {
                    Ok(i) => imports.push(i),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Private => {
                    self.bump();
                    match self.parse_def(true) {
                        Ok(d) => defs.push(d),
                        Err(e) => {
                            errors.push(e);
                            self.skip_to_item();
                        }
                    }
                }
                Token::Def => match self.parse_def(false) {
                    Ok(d) => defs.push(d),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::Law => match self.parse_law() {
                    Ok(d) => defs.push(d),
                    Err(e) => {
                        errors.push(e);
                        self.skip_to_item();
                    }
                },
                Token::AtMain => {
                    if !main.name.is_empty() {
                        errors.push(self.err("multiple @main"));
                        self.skip_to_item();
                    } else {
                        match self.parse_main() {
                            Ok(m) => main = m,
                            Err(e) => {
                                errors.push(e);
                                self.skip_to_item();
                            }
                        }
                    }
                }
                Token::Eof => break,
                other => {
                    let msg = format!(
                    "expected enum/record/trait/impl/import/def/private def/law/@main, got {other:?}"
                );
                    errors.push(self.err(msg));
                    self.skip_to_item();
                }
            }
        }

        (
            Program {
                package,
                enums,
                traits,
                impls,
                defs,
                main,
                imports,
                law_names: Vec::new(),
                driver_names: Vec::new(),
            },
            errors,
        )
    }

    fn parse_import(&mut self) -> Result<Import, ParseError> {
        self.expect(&Token::Import)?;
        let (from_module, mod_span) = self.expect_ident()?;
        self.expect(&Token::Dot)?;
        let (name, name_span) = self.expect_ident()?;
        Ok(Import {
            in_module: self.module.clone(),
            from_module,
            name,
            span: mod_span.cover(&name_span),
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
            _ => return Err(self.err("@main must have type IO[Unit]")),
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
        let params = if matches!(self.peek(), Token::LParen) {
            self.bump();
            let params = self.parse_param_list_with_tparams(&[])?;
            self.expect(&Token::RParen)?;
            params
        } else {
            Vec::new()
        };
        self.expect(&Token::Colon)?;
        let ret = self.parse_type()?;
        if !matches!(ret, Type::Bool) {
            return Err(self.err(format!("law `{name}` must return Bool, got {ret:?}")));
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
            params,
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
                name: pname.clone(),
                ty: pty,
                rfn,
            });
            if params.iter().filter(|p| p.name == pname).count() > 1 {
                return Err(self.err(format!("duplicate parameter {pname}")));
            }
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
        let mut cases: Vec<crate::ast::EnumCase> = Vec::new();
        match self.peek() {
            Token::LBrace => {
                self.bump();
                loop {
                    self.expect(&Token::Case)?;
                    let case = self.parse_enum_case(&type_params)?;
                    if cases.iter().any(|c| c.name == case.name) {
                        return Err(
                            self.err(format!("duplicate case {} in enum {name}", case.name))
                        );
                    }
                    cases.push(case);
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
                    let case = self.parse_enum_case(&type_params)?;
                    if cases.iter().any(|c| c.name == case.name) {
                        return Err(
                            self.err(format!("duplicate case {} in enum {name}", case.name))
                        );
                    }
                    cases.push(case);
                }
            }
            other => return Err(self.err(format!("enum body expected `:` or `{{`, got {other:?}"))),
        }
        if cases.is_empty() {
            return Err(self.err(format!("enum {name} has no cases")));
        }
        let methods = self.parse_indented_methods(&type_params)?;
        Ok(EnumDef {
            module: self.module.clone(),
            name,
            type_params,
            cases,
            is_record: false,
            methods,
        })
    }

    /// `record Name(f1: T1, f2: T2, …)` — single-case enum sugar.
    /// Optional `:` + `def` methods (`self` implicit), same shape as `impl` methods.
    fn parse_record(&mut self) -> Result<EnumDef, ParseError> {
        self.expect(&Token::Record)?;
        let (name, _) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::LParen)?;
        let mut fields: Vec<(String, Type)> = Vec::new();
        let mut field_rfns = Vec::new();
        loop {
            let (fname, fty, rfn) = self.parse_name_ty_rfn(&type_params)?;
            if fields.iter().any(|(n, _)| n == &fname) {
                return Err(self.err(format!("duplicate field {fname}")));
            }
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
        let methods = if matches!(self.peek(), Token::Colon) {
            self.bump();
            let methods = self.parse_trailing_methods(&type_params)?;
            if methods.is_empty() {
                return Err(self.err(format!("record {name} has no methods")));
            }
            methods
        } else {
            Vec::new()
        };
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
            methods,
        })
    }

    /// `trait Show: def show(): String` / `trait Get[T]: def getOrElse(default: T): T`
    fn parse_trait(&mut self) -> Result<TraitDef, ParseError> {
        self.expect(&Token::Trait)?;
        let (name, _) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::Colon)?;
        let mut methods: Vec<crate::ast::TraitMethod> = Vec::new();
        while matches!(self.peek(), Token::Def) {
            let method = self.parse_trait_method(&type_params)?;
            if methods.iter().any(|m| m.name == method.name) {
                return Err(self.err(format!("duplicate method {} in trait {name}", method.name)));
            }
            methods.push(method);
        }
        if methods.is_empty() {
            return Err(self.err(format!("trait {name} has no methods")));
        }
        Ok(TraitDef {
            module: self.module.clone(),
            name,
            type_params,
            methods,
        })
    }

    fn parse_trait_method(&mut self, type_params: &[String]) -> Result<TraitMethod, ParseError> {
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::LParen)?;
        let params = self.parse_param_list_with_tparams(type_params)?;
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type_with_tparams(type_params)?;
        Ok(TraitMethod { name, params, ret })
    }

    /// `impl Show for Point:` / `impl Get[Int] for Point:`
    fn parse_impl(&mut self) -> Result<ImplDef, ParseError> {
        self.expect(&Token::Impl)?;
        let (trait_name, _) = self.expect_ident()?;
        let trait_args = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_arg_list()?
        } else {
            Vec::new()
        };
        self.expect(&Token::For)?;
        let (for_type, _) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let mut methods = Vec::new();
        while matches!(self.peek(), Token::Def) {
            methods.push(self.parse_impl_method(&[])?);
        }
        if methods.is_empty() {
            return Err(self.err(format!("impl {trait_name} for {for_type} has no methods")));
        }
        Ok(ImplDef {
            module: self.module.clone(),
            trait_name,
            trait_args,
            for_type,
            methods,
        })
    }

    fn parse_type_arg_list(&mut self) -> Result<Vec<Type>, ParseError> {
        self.expect(&Token::LBracket)?;
        let mut args = Vec::new();
        loop {
            args.push(self.parse_type()?);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                continue;
            }
            break;
        }
        self.expect(&Token::RBracket)?;
        Ok(args)
    }

    fn parse_trailing_methods(
        &mut self,
        type_params: &[String],
    ) -> Result<Vec<ImplMethod>, ParseError> {
        let mut methods = Vec::new();
        while matches!(self.peek(), Token::Def) {
            methods.push(self.parse_impl_method(type_params)?);
        }
        Ok(methods)
    }

    /// Enum methods sit in the colon body (`  def …`). A column-0 `def` is the next free def.
    fn parse_indented_methods(
        &mut self,
        type_params: &[String],
    ) -> Result<Vec<ImplMethod>, ParseError> {
        let mut methods = Vec::new();
        while matches!(self.peek(), Token::Def) && self.peek_column() > 1 {
            methods.push(self.parse_impl_method(type_params)?);
        }
        Ok(methods)
    }

    fn peek_column(&self) -> u32 {
        offset_to_line_col(&self.source, self.current_span().start).1
    }

    fn parse_impl_method(&mut self, type_params: &[String]) -> Result<ImplMethod, ParseError> {
        self.expect(&Token::Def)?;
        let (name, _) = self.expect_ident()?;
        self.expect(&Token::LParen)?;
        let params = self.parse_param_list_with_tparams(type_params)?;
        self.expect(&Token::RParen)?;
        self.expect(&Token::Colon)?;
        let ret = self.parse_type_with_tparams(type_params)?;
        self.expect(&Token::Eq)?;
        let body = self.parse_expr()?;
        Ok(ImplMethod {
            name,
            params,
            ret,
            body,
        })
    }

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        self.parse_type_with_tparams(&[])
    }

    fn parse_type_with_tparams(&mut self, type_params: &[String]) -> Result<Type, ParseError> {
        let left = self.parse_type_atom(type_params)?;
        if matches!(self.peek(), Token::Arrow) {
            self.bump();
            let right = self.parse_type_with_tparams(type_params)?;
            return Ok(Type::Fun(Box::new(left), Box::new(right)));
        }
        Ok(left)
    }

    fn parse_type_atom(&mut self, type_params: &[String]) -> Result<Type, ParseError> {
        if matches!(self.peek(), Token::LParen) {
            self.bump();
            let inner = self.parse_type_with_tparams(type_params)?;
            self.expect(&Token::RParen)?;
            return Ok(inner);
        }
        let (name, _) = self.expect_ident()?;
        if type_params.iter().any(|p| p == &name) {
            if matches!(self.peek(), Token::LBracket) {
                return Err(self.err(format!("type parameter {name} takes no type arguments")));
            }
            return Ok(Type::Var(name));
        }
        match name.as_str() {
            "Unit" | "Int" | "Float" | "String" | "Bool" => {
                if matches!(self.peek(), Token::LBracket) {
                    return Err(self.err(format!("{name} takes no type arguments")));
                }
                Ok(match name.as_str() {
                    "Unit" => Type::Unit,
                    "Int" => Type::Int,
                    "Float" => Type::Float,
                    "String" => Type::String,
                    _ => Type::Bool,
                })
            }
            "List" => {
                self.expect(&Token::LBracket)?;
                let inner = self.parse_type_with_tparams(type_params)?;
                self.expect(&Token::RBracket)?;
                Ok(Type::List(Box::new(inner)))
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
        let mut fields: Vec<(String, Type)> = Vec::new();
        let mut field_rfns = Vec::new();
        if matches!(self.peek(), Token::LParen) {
            self.bump();
            loop {
                let (fname, fty, rfn) = self.parse_name_ty_rfn(type_params)?;
                if fields.iter().any(|(n, _)| n == &fname) {
                    return Err(self.err(format!("duplicate field {fname}")));
                }
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
        let mut left = self.parse_bitor()?;
        while matches!(self.peek(), Token::AmpAmp) {
            self.bump();
            let right = self.parse_bitor()?;
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

    fn parse_bitor(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitxor()?;
        while matches!(self.peek(), Token::Pipe) {
            self.bump();
            let right = self.parse_bitxor()?;
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op: BinOp::BitOr,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
        }
        Ok(left)
    }

    fn parse_bitxor(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitand()?;
        while matches!(self.peek(), Token::Caret) {
            self.bump();
            let right = self.parse_bitand()?;
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op: BinOp::BitXor,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
        }
        Ok(left)
    }

    fn parse_bitand(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_cmp()?;
        while matches!(self.peek(), Token::Amp) {
            self.bump();
            let right = self.parse_cmp()?;
            let span = left.span.clone().cover(&right.span);
            left = self.mk(
                ExprKind::Binary {
                    op: BinOp::BitAnd,
                    left: Box::new(left),
                    right: Box::new(right),
                },
                span,
            );
        }
        Ok(left)
    }

    fn parse_cmp(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_shift()?;
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
            let right = self.parse_shift()?;
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

    fn parse_shift(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_add()?;
        loop {
            let op = match self.peek() {
                Token::Shl => BinOp::Shl,
                Token::Shr => BinOp::Shr,
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
        let mut left = self.parse_unary()?;
        loop {
            let op = match self.peek() {
                Token::Star => BinOp::Mul,
                Token::Slash => BinOp::Div,
                Token::Percent => BinOp::Mod,
                _ => break,
            };
            self.bump();
            let right = self.parse_unary()?;
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

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        let start = self.current_span();
        let op = match self.peek() {
            Token::Minus => UnOp::Neg,
            Token::Bang => UnOp::Not,
            Token::Tilde => UnOp::BitNot,
            _ => return self.parse_postfix(),
        };
        self.bump();
        let expr = self.parse_unary()?;
        let span = start.cover(&expr.span);
        Ok(self.mk(
            ExprKind::Unary {
                op,
                expr: Box::new(expr),
            },
            span,
        ))
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
                            let (param, body) = self.parse_lambda()?;
                            self.expect(&Token::RParen)?;
                            let span = expr.span.clone().cover(&body.span);
                            expr = self.mk(
                                ExprKind::HandleErrorWith {
                                    inner: Box::new(expr),
                                    param,
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
            let pattern = self.parse_or_pattern()?;
            let guard = if matches!(self.peek(), Token::If) {
                self.bump();
                Some(self.parse_expr()?)
            } else {
                None
            };
            self.expect(&Token::Arrow)?;
            let body = self.parse_expr()?;
            arms.push(MatchArm {
                pattern,
                guard,
                body,
            });
        }
        if braced {
            self.expect(&Token::RBrace)?;
        }
        if arms.is_empty() {
            return Err(self.err("match needs at least one case"));
        }
        Ok(arms)
    }

    fn parse_or_pattern(&mut self) -> Result<Pattern, ParseError> {
        let first = self.parse_pattern_atom()?;
        if !matches!(self.peek(), Token::Pipe) {
            return Ok(first);
        }
        let mut alts = vec![first];
        while matches!(self.peek(), Token::Pipe) {
            self.bump();
            alts.push(self.parse_pattern_atom()?);
        }
        Ok(Pattern::Or(alts))
    }

    fn parse_pattern_atom(&mut self) -> Result<Pattern, ParseError> {
        match self.peek().clone() {
            Token::Underscore => {
                self.bump();
                Ok(Pattern::Wildcard)
            }
            Token::IntLit(n) => {
                self.bump();
                Ok(Pattern::Int(n))
            }
            Token::FloatLit(bits) => {
                self.bump();
                Ok(Pattern::Float(bits))
            }
            Token::True => {
                self.bump();
                Ok(Pattern::Bool(true))
            }
            Token::False => {
                self.bump();
                Ok(Pattern::Bool(false))
            }
            Token::StringLit(s) => {
                self.bump();
                Ok(Pattern::Str(s))
            }
            Token::Minus => {
                self.bump();
                let got = self.bump();
                match got.token {
                    Token::IntLit(n) => Ok(Pattern::Int(-n)),
                    Token::FloatLit(bits) => Ok(Pattern::Float((-f64::from_bits(bits)).to_bits())),
                    other => Err(self.err(format!(
                        "expected number after `-` in pattern, got {other:?}"
                    ))),
                }
            }
            Token::Ident(_) => {
                let (name, _) = self.expect_ident()?;
                if matches!(self.peek(), Token::Dot) {
                    self.bump();
                    let (case_name, _) = self.expect_ident()?;
                    let binds = self.parse_pattern_payloads()?;
                    Ok(Pattern::Adt {
                        enum_name: name,
                        case_name,
                        binds,
                        type_args: Vec::new(),
                    })
                } else if matches!(self.peek(), Token::LParen) {
                    // Record / single-case sugar: `case Point(x, y)`.
                    let binds = self.parse_pattern_payloads()?;
                    Ok(Pattern::Adt {
                        enum_name: name.clone(),
                        case_name: name,
                        binds,
                        type_args: Vec::new(),
                    })
                } else {
                    Ok(Pattern::Bind(name))
                }
            }
            other => Err(self.err(format!("expected pattern, got {other:?}"))),
        }
    }

    fn parse_pattern_payloads(&mut self) -> Result<Vec<Pattern>, ParseError> {
        if !matches!(self.peek(), Token::LParen) {
            return Ok(Vec::new());
        }
        self.bump();
        let mut binds = Vec::new();
        loop {
            binds.push(self.parse_or_pattern()?);
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
            Token::FloatLit(bits) => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::FloatLit(bits), start.cover(&end)))
            }
            Token::True => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::BoolLit(true), start.cover(&end)))
            }
            Token::False => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::BoolLit(false), start.cover(&end)))
            }
            Token::StringLit(s) => {
                let end = self.bump().span;
                Ok(self.mk(ExprKind::StrLit(s), start.cover(&end)))
            }
            Token::InterpString(parts) => {
                self.bump();
                self.parse_interpolate(parts, start)
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
                    "forever" => {
                        let args = self.parse_args()?;
                        if args.len() != 1 {
                            return Err(self.err("IO.forever expects 1 arg"));
                        }
                        let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                        Ok(self.mk(
                            ExprKind::Call {
                                callee: "IO.forever".into(),
                                args,
                            },
                            start.cover(&end),
                        ))
                    }
                    "repeatN" | "retryN" => {
                        let args = self.parse_args()?;
                        if args.len() != 2 {
                            return Err(self.err(format!("IO.{method} expects 2 args")));
                        }
                        let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                        Ok(self.mk(
                            ExprKind::Call {
                                callee: format!("IO.{method}"),
                                args,
                            },
                            start.cover(&end),
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
                    other => {
                        let callee = format!("IO.{other}");
                        let args = if matches!(self.peek(), Token::LParen) {
                            self.parse_args()?
                        } else {
                            Vec::new()
                        };
                        let end = args.last().map(|a| a.span.clone()).unwrap_or(start.clone());
                        Ok(self.mk(ExprKind::Call { callee, args }, start.cover(&end)))
                    }
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
                        | "Fiber"
                        | "Resource"
                        | "Stream"
                        | "Map"
                        | "Set"
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
                    // Lowercase `b.get()` / `b.x` go through postfix (method / field).
                    // Capitalized `Opt.Some(…)` / `Color.Red` stay ADT/module in primary.
                    if !name.chars().next().is_some_and(|c| c.is_ascii_uppercase()) {
                        return Ok(self.mk(ExprKind::Var(name), start));
                    }
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
                        source: body.clone(),
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
    fn parse_float_literal_and_type() {
        let src = r#"
def scale(x: Float): Float = x * 2.0
@main def main: IO[Unit] = IO.println(s"${scale(-1.5)}")
"#;
        let p = parse(src).unwrap();
        assert!(matches!(p.defs[0].params[0].ty, Type::Float));
        assert!(matches!(p.defs[0].ret, Type::Float));
        match &p.defs[0].body.kind {
            ExprKind::Binary { right, .. } => {
                assert!(matches!(&right.kind, ExprKind::FloatLit(b) if f64::from_bits(*b) == 2.0));
            }
            other => panic!("expected binary, got {other:?}"),
        }
    }

    #[test]
    fn parse_io_forever_repeat_retry_as_calls() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.forever(IO.sleep(1))
    _ <- IO.repeatN(2, IO.pure("x"))
    _ <- IO.retryN(1, IO.pure("y"))
  } yield ()
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert!(matches!(
                    &binders[0],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "IO.forever")
                ));
                assert!(matches!(
                    &binders[1],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "IO.repeatN")
                ));
                assert!(matches!(
                    &binders[2],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "IO.retryN")
                ));
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_unknown_io_method_as_call() {
        let src = "@main def main: IO[Unit] = IO.printl(\"x\")\n";
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Call { callee, args } => {
                assert_eq!(callee, "IO.printl");
                assert_eq!(args.len(), 1);
            }
            other => panic!("expected call, got {other:?}"),
        }
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
    fn parse_law_with_params() {
        let src = r#"
law addComm(a: Int, b: Int): Bool = a + b == b + a
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert!(p.defs[0].is_law);
        assert_eq!(p.defs[0].params.len(), 2);
        assert!(matches!(p.defs[0].params[0].ty, Type::Int));
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
                        assert!(
                            matches!(&binds[..], [Pattern::Bind(n)] if n == "n"),
                            "{binds:?}"
                        );
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
    fn parse_match_guard() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case Opt.Some(n) if n > 0 => IO.println("pos")
    case Opt.Some(n) => IO.println("nonpos")
    case Opt.None => IO.println("none")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert!(arms[0].guard.is_some(), "first arm needs a guard");
                assert!(arms[1].guard.is_none(), "second arm is unguarded");
                match &arms[0].guard {
                    Some(g) => assert!(
                        matches!(g.kind, ExprKind::Binary { op: BinOp::Gt, .. }),
                        "{:?}",
                        g.kind
                    ),
                    None => panic!("missing guard"),
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
                    assert!(
                        matches!(
                            &binds[..],
                            [Pattern::Bind(x), Pattern::Bind(y)] if x == "x" && y == "y"
                        ),
                        "{binds:?}"
                    );
                }
                other => panic!("expected multi-bind pattern, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_nested_adt_pattern() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Color.Red) => IO.println("red")
    case Wrap.Box(_) => IO.println("other")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt {
                    enum_name,
                    case_name,
                    binds,
                    ..
                } => {
                    assert_eq!(enum_name, "Wrap");
                    assert_eq!(case_name, "Box");
                    match &binds[..] {
                        [Pattern::Adt {
                            enum_name: inner_en,
                            case_name: inner_cn,
                            binds: inner_binds,
                            ..
                        }] => {
                            assert_eq!(inner_en, "Color");
                            assert_eq!(inner_cn, "Red");
                            assert!(inner_binds.is_empty());
                        }
                        other => panic!("expected nested Color.Red, got {other:?}"),
                    }
                }
                other => panic!("expected Wrap.Box pattern, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[1].pattern {
                Pattern::Adt { binds, .. } => {
                    assert!(matches!(&binds[..], [Pattern::Wildcard]), "{binds:?}");
                }
                other => panic!("expected wildcard payload, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_literal_patterns() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  n match {
    case 0 => IO.println("z")
    case -1 => IO.println("n")
    case true => IO.println("t")
    case "ok" => IO.println("s")
    case 1.5 => IO.println("f")
    case x => IO.println("b")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert!(
                    matches!(&arms[0].pattern, Pattern::Int(0)),
                    "{:?}",
                    arms[0].pattern
                );
                assert!(
                    matches!(&arms[1].pattern, Pattern::Int(-1)),
                    "{:?}",
                    arms[1].pattern
                );
                assert!(
                    matches!(&arms[2].pattern, Pattern::Bool(true)),
                    "{:?}",
                    arms[2].pattern
                );
                assert!(
                    matches!(&arms[3].pattern, Pattern::Str(s) if s == "ok"),
                    "{:?}",
                    arms[3].pattern
                );
                assert!(
                    matches!(&arms[4].pattern, Pattern::Float(_)),
                    "{:?}",
                    arms[4].pattern
                );
                assert!(
                    matches!(&arms[5].pattern, Pattern::Bind(n) if n == "x"),
                    "{:?}",
                    arms[5].pattern
                );
            }
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_nested_int_literal_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0) => IO.println("z")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => {
                    assert!(matches!(&binds[..], [Pattern::Int(0)]), "{binds:?}");
                }
                other => panic!("expected Opt.Some(0), got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_or_pattern() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  c match {
    case Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Or(alts) => {
                    assert_eq!(alts.len(), 2, "{alts:?}");
                    assert!(
                        matches!(
                            &alts[0],
                            Pattern::Adt {
                                case_name,
                                ..
                            } if case_name == "Red"
                        ),
                        "{:?}",
                        alts[0]
                    );
                    assert!(
                        matches!(
                            &alts[1],
                            Pattern::Adt {
                                case_name,
                                ..
                            } if case_name == "Blue"
                        ),
                        "{:?}",
                        alts[1]
                    );
                }
                other => panic!("expected or-pattern, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_nested_or_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0 | 1) => IO.println("s")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => match &binds[..] {
                    [Pattern::Or(alts)] => {
                        assert!(matches!(&alts[..], [Pattern::Int(0), Pattern::Int(1)]));
                    }
                    other => panic!("expected nested or, got {other:?}"),
                },
                other => panic!("expected Opt.Some(0 | 1), got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_or_pattern_with_guard() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  c match {
    case Color.Red | Color.Blue if false => IO.println("skip")
    case Color.Red | Color.Blue => IO.println("hit")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert!(arms[0].guard.is_some(), "first arm needs a guard");
                assert!(matches!(&arms[0].pattern, Pattern::Or(_)));
                assert!(arms[1].guard.is_none());
                assert!(matches!(&arms[1].pattern, Pattern::Or(_)));
            }
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
    label = Signal.map(count, n => "x")
  } yield IO.println(label)
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
                    ForBinder::Eq { name, .. } if name == "label"
                ));
                assert!(matches!(&body.kind, ExprKind::IoPrintln(_)));
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
    fn parse_generic_enum_method() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].methods.len(), 1);
        assert_eq!(p.enums[0].methods[0].name, "getOrElse");
        assert!(p.defs.is_empty());
    }

    #[test]
    fn parse_generic_trait() {
        let src = r#"
trait Get[T]:
  def getOrElse(default: T): T
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.traits[0].name, "Get");
        assert_eq!(p.traits[0].type_params, &["T".to_string()]);
        assert!(matches!(
            &p.traits[0].methods[0].params[0].ty,
            crate::ast::Type::Var(n) if n == "T"
        ));
        assert!(matches!(
            &p.traits[0].methods[0].ret,
            crate::ast::Type::Var(n) if n == "T"
        ));
    }

    #[test]
    fn parse_impl_trait_args() {
        let src = r#"
record Point(x: Int)
trait Get[T]:
  def getOrElse(default: T): T
impl Get[Int] for Point:
  def getOrElse(default: Int): Int = self.x
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.impls[0].trait_name, "Get");
        assert_eq!(p.impls[0].trait_args, vec![crate::ast::Type::Int]);
        assert_eq!(p.impls[0].for_type, "Point");
    }

    #[test]
    fn parse_impl_trait_args_on_generic() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
trait Get[T]:
  def getOrElse(default: T): T
impl Get[T] for Opt:
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.impls[0].trait_name, "Get");
        assert_eq!(p.impls[0].for_type, "Opt");
        assert_eq!(p.impls[0].trait_args.len(), 1);
    }

    #[test]
    fn parse_enum_does_not_swallow_following_def() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def getOrElse[T](o: Opt[T], default: T): T =
  o match {
    case Opt.Some(x) => x
    case Opt.None => default
  }
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(p.enums[0].methods.is_empty());
        assert_eq!(p.defs[0].name, "getOrElse");
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
    fn parse_function_type_in_def_sig() {
        let src = r#"
def apply(f: Int => String, n: Int): String = f(n)
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.defs[0].params[0].ty,
            crate::ast::Type::Fun(a, b)
                if matches!(a.as_ref(), crate::ast::Type::Int)
                    && matches!(b.as_ref(), crate::ast::Type::String)
        ));
    }

    #[test]
    fn parse_rejects_duplicate_parameter() {
        let src = r#"
def add(n: Int, n: Int): Int = n
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(err.contains("duplicate parameter n"), "{err}");
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
def f(x: Int[String]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("Int takes no type arguments"),
            "unexpected: {err}"
        );
    }

    #[test]
    fn parse_list_type_arg() {
        let src = r#"
def f(x: List[Int]): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.defs[0].params[0].ty,
            Type::List(inner) if matches!(**inner, Type::Int)
        ));
    }

    #[test]
    fn parse_bool_literals() {
        let src = r#"
@main def main: IO[Unit] = if (true) IO.println("a") else IO.println("b")
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::If { cond, .. } => {
                assert!(matches!(cond.kind, ExprKind::BoolLit(true)));
            }
            other => panic!("expected If, got {other:?}"),
        }
    }

    #[test]
    fn parse_rejects_bare_list() {
        let src = r#"
def f(x: List): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("expected") || err.contains("["),
            "unexpected: {err}"
        );
    }

    #[test]
    fn parse_recovers_to_next_top_level_item() {
        let src = r#"
def a( : Int = 1
def b(): Int = 2
@main def main: IO[Unit] = IO.println("ok")
"#;
        let (p, errs) = parse_file_recovering(src, "rec.scuzz");
        assert!(!errs.is_empty(), "{errs:?}");
        assert!(
            p.defs.iter().any(|d| d.name == "b"),
            "expected recovered def b: {:?}",
            p.defs.iter().map(|d| &d.name).collect::<Vec<_>>()
        );
        assert_eq!(p.main.name, "main");
    }

    #[test]
    fn parse_unary_not_neg_bitnot() {
        let src = r#"
def f(n: Int, b: Bool): Int = if (!b) -n else ~n
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        match &p.defs[0].body.kind {
            ExprKind::If {
                cond,
                then_branch,
                else_branch,
            } => {
                assert!(
                    matches!(&cond.kind, ExprKind::Unary { op: UnOp::Not, .. }),
                    "{cond:?}"
                );
                assert!(
                    matches!(&then_branch.kind, ExprKind::Unary { op: UnOp::Neg, .. }),
                    "{then_branch:?}"
                );
                assert!(
                    matches!(
                        &else_branch.kind,
                        ExprKind::Unary {
                            op: UnOp::BitNot,
                            ..
                        }
                    ),
                    "{else_branch:?}"
                );
            }
            other => panic!("expected if, got {other:?}"),
        }
    }

    #[test]
    fn parse_bitwise_hex_bin() {
        let src = r#"
def mask(): Int = (0xFF & 0b1111) | 1 << 2 ^ 3
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        match &p.defs[0].body.kind {
            ExprKind::Binary {
                op: BinOp::BitOr, ..
            } => {}
            other => panic!("expected | at top, got {other:?}"),
        }
    }
}

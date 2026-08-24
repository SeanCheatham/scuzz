use crate::ast::{
    is_for_binder_pat, is_tuple_binder_pat, opaque_tuple_pat, simple_binder_name, BinOp, EnumCase,
    EnumDef, Expr, ExprKind, ForBinder, FunDef, ImplDef, ImplMethod, Import, InterpPart, MainDef,
    MatchArm, Param, Pattern, Program, TraitDef, TraitMethod, Type, TypeAlias, UnOp, CASE_LAMBDA,
    MAX_TUPLE_ARITY, TUP_UNPACK,
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
        bare_arrow_is_lambda: true,
    };
    p.parse_program()
}

fn empty_program(file: &str) -> Program {
    Program {
        package: Vec::new(),
        enums: Vec::new(),
        aliases: Vec::new(),
        traits: Vec::new(),
        impls: Vec::new(),
        defs: Vec::new(),
        main: MainDef {
            module: module_id_from_label(file),
            name: String::new(),
            body: Expr::dummy(ExprKind::Unit),
        },
        imports: Vec::new(),
        driver_names: Vec::new(),
        intent_always: Vec::new(),
        intent_eventually: Vec::new(),
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
        bare_arrow_is_lambda: true,
    };
    p.parse_program_recovering()
}

/// Parse multiple source files into one program. Packages must agree. Defs and
/// enums are namespaced by file-stem module. The same bare name in two modules is allowed.
pub fn parse_sources(sources: &[(String, String)]) -> Result<Program, ParseError> {
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut aliases: Vec<crate::ast::TypeAlias> = Vec::new();
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
        for a in prog.aliases {
            if aliases
                .iter()
                .any(|x| x.module == a.module && x.name == a.name)
            {
                return Err(ParseError::Msg(format!(
                    "{name}: duplicate type {}.{}",
                    a.module, a.name
                )));
            }
            aliases.push(a);
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
        aliases,
        traits,
        impls,
        defs,
        main,
        imports,
        driver_names: Vec::new(),
        intent_always: Vec::new(),
        intent_eventually: Vec::new(),
    })
}

/// Parse many files. Recover inside each file. Collect every parse error.
pub fn parse_sources_recovering(
    sources: &[(String, String)],
) -> (Option<Program>, Vec<ParseError>) {
    let mut errors = Vec::new();
    let mut package: Option<Vec<String>> = None;
    let mut enums: Vec<EnumDef> = Vec::new();
    let mut aliases: Vec<crate::ast::TypeAlias> = Vec::new();
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
        for a in prog.aliases {
            if aliases
                .iter()
                .any(|x| x.module == a.module && x.name == a.name)
            {
                errors.push(ParseError::Msg(format!(
                    "{name}: duplicate type {}.{}",
                    a.module, a.name
                )));
                continue;
            }
            aliases.push(a);
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
            aliases,
            traits,
            impls,
            defs,
            main,
            imports,
            driver_names: Vec::new(),
            intent_always: Vec::new(),
            intent_eventually: Vec::new(),
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
    /// When false, a bare ident or `_` before `=>` is not a lambda.
    bare_arrow_is_lambda: bool,
}

impl Parser {
    fn current_span(&self) -> Span {
        self.tokens
            .get(self.i)
            .map(|t| t.span.clone())
            .unwrap_or_else(|| Span::new(self.file.clone(), 0, 0))
    }

    fn peek(&self) -> &Token {
        self.peek_nth(0)
    }

    /// True when a newline sits between `from` and the next token. `e(x)` apply
    /// stays on one line so `(a, b) = e` after a value is a binder, not apply.
    fn newline_before_peek(&self, from: usize) -> bool {
        let to = self.current_span().start;
        self.source.get(from..to).is_some_and(|s| s.contains('\n'))
    }

    fn peek_nth(&self, n: usize) -> &Token {
        let i = self.i.saturating_add(n);
        if i < self.tokens.len() {
            &self.tokens[i].token
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

    fn check_tuple_arity(&self, n: usize) -> Result<(), ParseError> {
        if n < 2 {
            return Err(self.err("tuple needs two or more slots"));
        }
        if n > MAX_TUPLE_ARITY {
            return Err(self.err(format!("tuple has at most {MAX_TUPLE_ARITY} slots")));
        }
        Ok(())
    }

    fn at_item_start(&self) -> bool {
        matches!(
            self.peek(),
            Token::Enum
                | Token::Record
                | Token::Type
                | Token::Trait
                | Token::Impl
                | Token::Import
                | Token::Private
                | Token::Def
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
        let mut aliases: Vec<TypeAlias> = Vec::new();
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
                Token::Type => match self.parse_type_alias() {
                    Ok(a) => {
                        if aliases.iter().any(|x| x.name == a.name) {
                            errors.push(self.err(format!("duplicate type {}", a.name)));
                        } else {
                            aliases.push(a);
                        }
                    }
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
                    "expected enum/record/type/trait/impl/import/def/private def/@main, got {other:?}"
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
                aliases,
                traits,
                impls,
                defs,
                main,
                imports,
                driver_names: Vec::new(),
                intent_always: Vec::new(),
                intent_eventually: Vec::new(),
            },
            errors,
        )
    }

    fn parse_import(&mut self) -> Result<Import, ParseError> {
        self.expect(&Token::Import)?;
        let (from_module, mod_span) = self.expect_ident()?;
        self.expect(&Token::Dot)?;
        if matches!(self.peek(), Token::Star) {
            let star = self.bump();
            if self.peek_as() {
                return Err(self.err("`import Module.*` does not take `as`"));
            }
            return Ok(Import {
                in_module: self.module.clone(),
                from_module,
                name: "*".into(),
                alias: None,
                span: mod_span.cover(&star.span),
            });
        }
        let (name, name_span) = self.expect_ident()?;
        let mut end = name_span.clone();
        let alias = if self.peek_as() {
            self.bump();
            let (alias, alias_span) = self.expect_ident()?;
            if alias == name {
                return Err(self.err(format!(
                    "import alias `{alias}` is the same as `{from_module}.{name}`"
                )));
            }
            end = alias_span;
            Some(alias)
        } else {
            None
        };
        Ok(Import {
            in_module: self.module.clone(),
            from_module,
            name,
            alias,
            span: mod_span.cover(&end),
        })
    }

    fn peek_as(&self) -> bool {
        matches!(self.peek(), Token::Ident(s) if s == "as")
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
        let (name, name_span) = self.expect_ident()?;
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
            name_span,
            is_private,
            is_driver: false,
            type_params,
            params,
            ret,
            body,
        })
    }

    /// `type Name = T` / `type Name[T] = List[T]`.
    fn parse_type_alias(&mut self) -> Result<TypeAlias, ParseError> {
        self.expect(&Token::Type)?;
        let (name, name_span) = self.expect_ident()?;
        let type_params = if matches!(self.peek(), Token::LBracket) {
            self.parse_type_params()?
        } else {
            Vec::new()
        };
        self.expect(&Token::Eq)?;
        let target = self.parse_type_with_tparams(&type_params)?;
        Ok(TypeAlias {
            module: self.module.clone(),
            name,
            name_span,
            type_params,
            target,
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
            let (pname, pname_span, pty, rfn) = self.parse_name_ty_rfn(type_params)?;
            let default = if matches!(self.peek(), Token::Eq) {
                self.bump();
                Some(self.parse_expr()?)
            } else {
                None
            };
            params.push(Param {
                name: pname.clone(),
                ty: pty,
                rfn,
                default,
                span: pname_span,
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
        let mut seen_default = false;
        for p in &params {
            if p.default.is_some() {
                seen_default = true;
            } else if seen_default {
                return Err(self.err(format!(
                    "parameter `{}` needs a default (it follows a parameter with a default)",
                    p.name
                )));
            }
        }
        Ok(params)
    }

    fn parse_name_ty_rfn(
        &mut self,
        type_params: &[String],
    ) -> Result<(String, Span, Type, Option<Expr>), ParseError> {
        let (name, span) = self.expect_ident()?;
        self.expect(&Token::Colon)?;
        let ty = self.parse_type_with_tparams(type_params)?;
        let rfn = if matches!(self.peek(), Token::Where) {
            self.bump();
            Some(self.parse_or()?)
        } else {
            None
        };
        Ok((name, span, ty, rfn))
    }

    fn reject_param_defaults(&self, params: &[Param], ctx: &str) -> Result<(), ParseError> {
        if params.iter().any(|p| p.default.is_some()) {
            return Err(self.err(format!("{ctx} cannot have a default")));
        }
        Ok(())
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
            let (fname, _, fty, rfn) = self.parse_name_ty_rfn(&type_params)?;
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
            let methods = self.parse_indented_methods(&type_params)?;
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
        while matches!(self.peek(), Token::Def) && self.peek_column() > 1 {
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
        self.reject_param_defaults(&params, "trait method parameters")?;
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
        let methods = self.parse_indented_methods(&[])?;
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

    /// Methods sit in the colon body (`  def …`). A column-0 `def` is the next free def.
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
        self.reject_param_defaults(&params, "method parameters")?;
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
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                let mut elems = vec![inner];
                loop {
                    if matches!(self.peek(), Token::RParen) {
                        break;
                    }
                    elems.push(self.parse_type_with_tparams(type_params)?);
                    if matches!(self.peek(), Token::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
                self.check_tuple_arity(elems.len())?;
                self.expect(&Token::RParen)?;
                return Ok(Type::Tuple(elems));
            }
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
                let (fname, _, fty, rfn) = self.parse_name_ty_rfn(type_params)?;
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

    /// Name, `_`, `(a, b)`, or a constructor / list / as-pattern for a `for` binder.
    fn parse_for_binder_pat(&mut self) -> Result<(Pattern, Span), ParseError> {
        let start = self.current_span();
        let pat = self.parse_or_pattern()?;
        if !is_for_binder_pat(&pat) {
            return Err(
                self.err("for binder must be a name, `_`, `(a, b)`, or a constructor pattern")
            );
        }
        let span = start.cover(&self.prev_span());
        Ok((pat, span))
    }

    fn binder_from_pat(pat: Pattern, span: Span) -> (String, Span, Option<Pattern>) {
        if let Some(name) = simple_binder_name(&pat) {
            (name.to_string(), span, None)
        } else {
            (TUP_UNPACK.into(), span, Some(pat))
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
                Token::If => {
                    let if_span = self.bump().span;
                    let pred = self.parse_expr()?;
                    let span = if_span.cover(&pred.span);
                    binders.push(ForBinder::Guard { pred, span });
                }
                _ => {
                    let (pat, span) = self.parse_for_binder_pat()?;
                    let (name, span, unpack) = Self::binder_from_pat(pat, span);
                    match self.peek() {
                        Token::Eq => {
                            self.bump();
                            let value = self.parse_expr()?;
                            binders.push(ForBinder::Eq {
                                name,
                                span,
                                value,
                                pat: unpack,
                            });
                        }
                        Token::LeftArrow => {
                            self.bump();
                            let value = self.parse_expr()?;
                            binders.push(ForBinder::Draw {
                                name,
                                span,
                                value,
                                pat: unpack,
                            });
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
        let has_guard = binders.iter().any(|b| matches!(b, ForBinder::Guard { .. }));
        let has_draw = binders.iter().any(|b| matches!(b, ForBinder::Draw { .. }));
        if has_guard && !has_draw {
            return Err(self.err("`if` in `for` needs a `<-` binder so a miss is IO.fail"));
        }
        let span = if let Some(b) = binders.first() {
            let vs = match b {
                ForBinder::Eq { value, .. } | ForBinder::Draw { value, .. } => value.span.clone(),
                ForBinder::Guard { pred, .. } => pred.span.clone(),
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
        let expr = self.parse_or()?;
        if !matches!(self.peek(), Token::Colon) {
            return Ok(expr);
        }
        self.bump();
        let ty = self.parse_type()?;
        let end = self.prev_span();
        let span = expr.span.clone().cover(&end);
        Ok(self.mk(
            ExprKind::Ascribe {
                expr: Box::new(expr),
                ty,
            },
            span,
        ))
    }

    fn prev_span(&self) -> Span {
        if self.i == 0 {
            return self.current_span();
        }
        self.tokens[self.i - 1].span.clone()
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
        let mut left = self.parse_cons()?;
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
            let right = self.parse_cons()?;
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

    /// Right-associative `h :: t` → `List.cons(h, t)`.
    fn parse_cons(&mut self) -> Result<Expr, ParseError> {
        let left = self.parse_shift()?;
        if !matches!(self.peek(), Token::ColonColon) {
            return Ok(left);
        }
        self.bump();
        let right = self.parse_cons()?;
        let span = left.span.clone().cover(&right.span);
        Ok(self.mk(
            ExprKind::Call {
                callee: "List.cons".into(),
                args: vec![left, right],
            },
            span,
        ))
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
                    let (method, method_span) = self.expect_ident()?;
                    match method.as_str() {
                        "map" => {
                            self.expect(&Token::LParen)?;
                            let (param, body) = self.parse_lambda()?;
                            self.expect(&Token::RParen)?;
                            let span = expr.span.clone().cover(&body.span);
                            expr = self.mk(
                                ExprKind::IoMap {
                                    inner: Box::new(expr),
                                    param,
                                    body: Box::new(body),
                                },
                                span,
                            );
                        }
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
                            let mut span = expr.span.clone().cover(&method_span);
                            if matches!(self.peek(), Token::LParen) {
                                self.bump();
                                let end = self.expect(&Token::RParen)?;
                                span = span.cover(&end);
                            }
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
                                let span = expr.span.clone().cover(&method_span);
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
                Token::LParen => {
                    if self.newline_before_peek(expr.span.end) {
                        break;
                    }
                    let args = self.parse_args()?;
                    let arg = match args.len() {
                        0 => return Err(self.err("apply expects an argument")),
                        1 => args.into_iter().next().unwrap(),
                        n => {
                            self.check_tuple_arity(n)?;
                            let end = args
                                .last()
                                .map(|a| a.span.clone())
                                .unwrap_or(expr.span.clone());
                            let span = args[0].span.clone().cover(&end);
                            self.mk(ExprKind::Tuple { elems: args }, span)
                        }
                    };
                    let span = expr.span.clone().cover(&arg.span);
                    expr = self.mk(
                        ExprKind::Apply {
                            fun: Box::new(expr),
                            arg: Box::new(arg),
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
                let saved = self.bare_arrow_is_lambda;
                self.bare_arrow_is_lambda = false;
                let g = self.parse_expr();
                self.bare_arrow_is_lambda = saved;
                Some(g?)
            } else {
                None
            };
            self.expect(&Token::Arrow)?;
            let body = self.parse_expr()?;
            arms.push(MatchArm {
                pattern,
                guard,
                body,
                unpack: false,
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
        let first = self.parse_cons_pattern()?;
        if !matches!(self.peek(), Token::Pipe) {
            return Ok(first);
        }
        let mut alts = vec![first];
        while matches!(self.peek(), Token::Pipe) {
            self.bump();
            alts.push(self.parse_cons_pattern()?);
        }
        Ok(Pattern::Or(alts))
    }

    fn parse_cons_pattern(&mut self) -> Result<Pattern, ParseError> {
        let head = self.parse_as_pattern()?;
        if !matches!(self.peek(), Token::ColonColon) {
            return Ok(head);
        }
        self.bump();
        let tail = self.parse_cons_pattern()?;
        Ok(Pattern::Cons {
            head: Box::new(head),
            tail: Box::new(tail),
            elem: Type::Opaque("Elem".into()),
        })
    }

    fn parse_as_pattern(&mut self) -> Result<Pattern, ParseError> {
        if matches!(self.peek(), Token::Ident(_)) && matches!(self.peek_nth(1), Token::At) {
            let (name, _) = self.expect_ident()?;
            self.bump();
            let inner = self.parse_or_pattern()?;
            return Ok(Pattern::As {
                name,
                inner: Box::new(inner),
            });
        }
        self.parse_pattern_atom()
    }

    fn parse_list_pattern(&mut self) -> Result<Pattern, ParseError> {
        self.bump();
        if matches!(self.peek(), Token::RBracket) {
            self.bump();
            return Ok(Pattern::Nil);
        }
        let mut elems = Vec::new();
        loop {
            elems.push(self.parse_or_pattern()?);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                if matches!(self.peek(), Token::RBracket) {
                    break;
                }
                continue;
            }
            break;
        }
        self.expect(&Token::RBracket)?;
        Ok(elems
            .into_iter()
            .rev()
            .fold(Pattern::Nil, |tail, head| Pattern::Cons {
                head: Box::new(head),
                tail: Box::new(tail),
                elem: Type::Opaque("Elem".into()),
            }))
    }

    fn parse_pattern_atom(&mut self) -> Result<Pattern, ParseError> {
        match self.peek().clone() {
            Token::LParen => {
                self.bump();
                let first = self.parse_or_pattern()?;
                if !matches!(self.peek(), Token::Comma) {
                    self.expect(&Token::RParen)?;
                    return Ok(first);
                }
                self.bump();
                let mut elems = vec![first];
                loop {
                    if matches!(self.peek(), Token::RParen) {
                        break;
                    }
                    elems.push(self.parse_or_pattern()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.bump();
                    } else {
                        break;
                    }
                }
                self.check_tuple_arity(elems.len())?;
                self.expect(&Token::RParen)?;
                Ok(opaque_tuple_pat(elems))
            }
            Token::LBracket => self.parse_list_pattern(),
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
        if matches!(self.peek(), Token::RParen) {
            self.bump();
            return Ok(binds);
        }
        loop {
            binds.push(self.parse_named_or_pattern()?);
            if matches!(self.peek(), Token::Comma) {
                self.bump();
                if matches!(self.peek(), Token::RParen) {
                    break;
                }
                continue;
            }
            break;
        }
        self.expect(&Token::RParen)?;
        Ok(binds)
    }

    /// `name = pat` or a nested pattern.
    fn parse_named_or_pattern(&mut self) -> Result<Pattern, ParseError> {
        if matches!(self.peek(), Token::Ident(_)) && matches!(self.peek_nth(1), Token::Eq) {
            let (name, _) = self.expect_ident()?;
            self.bump();
            let inner = self.parse_or_pattern()?;
            return Ok(Pattern::Named {
                name,
                inner: Box::new(inner),
            });
        }
        self.parse_or_pattern()
    }

    fn parse_lambda(&mut self) -> Result<(Option<String>, Expr), ParseError> {
        match self.peek().clone() {
            Token::LBrace if matches!(self.peek_nth(1), Token::Case) => {
                let start = self.current_span();
                let lam = self.parse_case_lambda(start)?;
                match lam.kind {
                    ExprKind::Lambda { param, body, .. } => Ok((param, *body)),
                    _ => Err(self.err("internal: case lambda")),
                }
            }
            Token::Underscore => {
                self.bump();
                self.expect(&Token::Arrow)?;
                Ok((None, self.parse_block()?))
            }
            Token::LParen => {
                let start = self.current_span();
                self.bump();
                if matches!(self.peek(), Token::RParen) {
                    self.bump();
                    self.expect(&Token::Arrow)?;
                    return Ok((None, self.parse_block()?));
                }
                let lam = self.parse_paren_lambda_after_lparen(start)?;
                match lam.kind {
                    ExprKind::Lambda {
                        param, pat, body, ..
                    } => {
                        let body = if let Some(pat) = pat {
                            let name = param.clone().unwrap_or_else(|| TUP_UNPACK.into());
                            let sp = body.span.clone();
                            self.mk(
                                ExprKind::Match {
                                    scrutinee: Box::new(self.mk(ExprKind::Var(name), sp.clone())),
                                    arms: vec![MatchArm::unpack(*pat, *body)],
                                },
                                sp,
                            )
                        } else {
                            *body
                        };
                        Ok((param, body))
                    }
                    _ => Err(self.err(
                        "expected `_ => expr`, `() => expr`, `(x: T) => expr`, `(a, b) => expr`, or `name => expr`",
                    )),
                }
            }
            Token::Ident(name) => {
                self.bump();
                self.expect(&Token::Arrow)?;
                Ok((Some(name), self.parse_block()?))
            }
            _ => {
                Err(self
                    .err("expected `_ => expr`, `() => expr`, `(x: T) => expr`, `(a, b) => expr`, `{ case … }`, or `name => expr`"))
            }
        }
    }

    /// `{ case Pat => body case … }`. Matches the bound kit or `A => B` value.
    fn parse_case_lambda(&mut self, start: Span) -> Result<Expr, ParseError> {
        let arms = self.parse_match_arms()?;
        let span = start.cover(&self.prev_span());
        let param = CASE_LAMBDA.to_string();
        let body = self.mk(
            ExprKind::Match {
                scrutinee: Box::new(self.mk(ExprKind::Var(param.clone()), span.clone())),
                arms,
            },
            span.clone(),
        );
        Ok(self.mk_lambda(Some(param), None, None, body, span))
    }

    /// `LParen` is already consumed. Parse `(x) => body`, `(x: T) => body`, or `(a, b) => body`.
    fn parse_paren_lambda_after_lparen(&mut self, start: Span) -> Result<Expr, ParseError> {
        if matches!(self.peek(), Token::RParen) {
            self.bump();
            self.expect(&Token::Arrow)?;
            let body = self.parse_block()?;
            let span = start.cover(&body.span);
            return Ok(self.mk_lambda(None, None, None, body, span));
        }
        let left = self.parse_or_pattern()?;
        if matches!(self.peek(), Token::Colon) {
            let param = match &left {
                Pattern::Bind(n) => Some(n.clone()),
                Pattern::Wildcard => None,
                _ => {
                    return Err(self.err("typed lambda needs a name or `_`"));
                }
            };
            self.bump();
            let param_ty = Some(self.parse_type()?);
            self.expect(&Token::RParen)?;
            self.expect(&Token::Arrow)?;
            let body = self.parse_block()?;
            let span = start.cover(&body.span);
            return Ok(self.mk_lambda(param, param_ty, None, body, span));
        }
        if matches!(self.peek(), Token::Comma) {
            self.bump();
            let mut elems = vec![left];
            loop {
                if matches!(self.peek(), Token::RParen) {
                    break;
                }
                elems.push(self.parse_or_pattern()?);
                if matches!(self.peek(), Token::Comma) {
                    self.bump();
                } else {
                    break;
                }
            }
            self.check_tuple_arity(elems.len())?;
            self.expect(&Token::RParen)?;
            self.expect(&Token::Arrow)?;
            let body = self.parse_block()?;
            let pat = opaque_tuple_pat(elems);
            if !is_tuple_binder_pat(&pat) {
                return Err(self.err(
                    "tuple lambda must bind names, `_`, nested `(a, b)`, or a constructor pattern",
                ));
            }
            let span = start.cover(&body.span);
            return Ok(self.mk_lambda(Some(TUP_UNPACK.into()), None, Some(pat), body, span));
        }
        self.expect(&Token::RParen)?;
        self.expect(&Token::Arrow)?;
        let body = self.parse_block()?;
        let span = start.cover(&body.span);
        if let Some(name) = simple_binder_name(&left) {
            let param = if name == "_" {
                None
            } else {
                Some(name.to_string())
            };
            return Ok(self.mk_lambda(param, None, None, body, span));
        }
        if is_tuple_binder_pat(&left) {
            return Ok(self.mk_lambda(Some(TUP_UNPACK.into()), None, Some(left), body, span));
        }
        Err(self.err("lambda binder must be a name, `_`, `(a, b)`, or a constructor pattern"))
    }

    fn try_paren_lambda(&mut self, start: Span) -> Result<Option<Expr>, ParseError> {
        if !self.bare_arrow_is_lambda {
            return Ok(None);
        }
        let saved = self.i;
        match self.parse_paren_lambda_after_lparen(start) {
            Ok(e) => Ok(Some(e)),
            Err(e) => {
                // `expect` consumes the unexpected token. `(x => x)` hits Arrow
                // while looking for `)`. Keep that as a grouped lambda.
                let msg = e.message();
                if msg.contains("tuple lambda")
                    || msg.contains("lambda binder")
                    || msg.contains("constructor pattern")
                {
                    return Err(e);
                }
                self.i = saved;
                Ok(None)
            }
        }
    }

    fn mk_lambda(
        &self,
        param: Option<String>,
        param_ty: Option<Type>,
        pat: Option<Pattern>,
        body: Expr,
        span: Span,
    ) -> Expr {
        self.mk(
            ExprKind::Lambda {
                param,
                param_ty,
                pat: pat.map(Box::new),
                body: Box::new(body),
            },
            span,
        )
    }

    fn parse_args(&mut self) -> Result<Vec<Expr>, ParseError> {
        self.expect(&Token::LParen)?;
        let saved = self.bare_arrow_is_lambda;
        self.bare_arrow_is_lambda = true;
        let result = (|| {
            let mut args = Vec::new();
            if !matches!(self.peek(), Token::RParen) {
                loop {
                    args.push(self.parse_arg()?);
                    if matches!(self.peek(), Token::Comma) {
                        self.bump();
                        if matches!(self.peek(), Token::RParen) {
                            break;
                        }
                        continue;
                    }
                    break;
                }
            }
            self.expect(&Token::RParen)?;
            Ok(args)
        })();
        self.bare_arrow_is_lambda = saved;
        result
    }

    fn parse_arg(&mut self) -> Result<Expr, ParseError> {
        if let Token::Ident(name) = self.peek().clone() {
            if matches!(self.peek_nth(1), Token::Eq) {
                let start = self.current_span();
                self.bump();
                self.bump();
                let value = self.parse_expr()?;
                let span = start.cover(&value.span);
                return Ok(self.mk(
                    ExprKind::NamedArg {
                        name,
                        value: Box::new(value),
                    },
                    span,
                ));
            }
        }
        self.parse_expr()
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
                let (else_branch, implicit_else, span) = if matches!(self.peek(), Token::Else) {
                    self.bump();
                    let else_branch = self.parse_if_branch()?;
                    let span = start.cover(&else_branch.span);
                    (else_branch, false, span)
                } else {
                    let span = start.cover(&then_branch.span);
                    (
                        self.mk(ExprKind::Unit, then_branch.span.clone()),
                        true,
                        span,
                    )
                };
                Ok(self.mk(
                    ExprKind::If {
                        cond: Box::new(cond),
                        then_branch: Box::new(then_branch),
                        else_branch: Box::new(else_branch),
                        implicit_else,
                    },
                    span,
                ))
            }
            Token::LBrace => {
                if matches!(self.peek_nth(1), Token::Case) {
                    return self.parse_case_lambda(start);
                }
                Err(self.err(
                    "statement blocks are not allowed; write `{ case … }` or use `for` to bind names",
                ))
            }
            Token::LParen => {
                self.bump();
                if matches!(self.peek(), Token::RParen) {
                    let end = self.bump().span;
                    if self.bare_arrow_is_lambda && matches!(self.peek(), Token::Arrow) {
                        self.bump();
                        let body = self.parse_block()?;
                        let span = start.cover(&body.span);
                        return Ok(self.mk_lambda(None, None, None, body, span));
                    }
                    return Ok(self.mk(ExprKind::Unit, start.cover(&end)));
                }
                if let Some(lam) = self.try_paren_lambda(start.clone())? {
                    return Ok(lam);
                }
                let saved = self.bare_arrow_is_lambda;
                self.bare_arrow_is_lambda = true;
                let inner = self.parse_expr();
                self.bare_arrow_is_lambda = saved;
                let inner = inner?;
                if matches!(self.peek(), Token::Comma) {
                    self.bump();
                    let mut elems = vec![inner];
                    loop {
                        if matches!(self.peek(), Token::RParen) {
                            break;
                        }
                        elems.push(self.parse_expr()?);
                        if matches!(self.peek(), Token::Comma) {
                            self.bump();
                        } else {
                            break;
                        }
                    }
                    self.check_tuple_arity(elems.len())?;
                    let end = self.expect(&Token::RParen)?;
                    return Ok(self.mk(ExprKind::Tuple { elems }, start.cover(&end)));
                }
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
                let end = self.bump().span;
                if matches!(self.peek(), Token::Arrow) {
                    if !self.bare_arrow_is_lambda {
                        return Err(
                            self.err("`_ =>` is a lambda; use `_` as a hole in a kit argument")
                        );
                    }
                    self.bump();
                    let body = self.parse_block()?;
                    let span = start.cover(&body.span);
                    return Ok(self.mk_lambda(None, None, None, body, span));
                }
                Ok(self.mk(ExprKind::Placeholder, start.cover(&end)))
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
                    "repeatN" | "retryN" | "foreach" | "foreachDiscard" | "when" | "unless" => {
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
                if self.bare_arrow_is_lambda && matches!(self.peek(), Token::Arrow) {
                    self.bump();
                    let body = self.parse_block()?;
                    let span = start.cover(&body.span);
                    return Ok(self.mk_lambda(Some(name), None, None, body, span));
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
                InterpTok::Ident { name, start, end } => out.push(InterpPart::Expr(self.mk(
                    ExprKind::Var(name),
                    Span::new(self.file.clone(), start, end),
                ))),
                InterpTok::Brace { body, start, end } => {
                    let mut tokens = lex(&body).map_err(|e| ParseError::At {
                        msg: e.to_string(),
                        span: Span::new(self.file.clone(), start, end),
                    })?;
                    for t in &mut tokens {
                        t.span.start += start;
                        t.span.end += start;
                        t.span.file = self.file.clone();
                    }
                    let mut nested = Parser {
                        tokens,
                        i: 0,
                        file: self.file.clone(),
                        module: self.module.clone(),
                        source: self.source.clone(),
                        bare_arrow_is_lambda: true,
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
    fn parse_tuple_expr_type_and_pattern() {
        let src = r#"
def swap(p: (Int, String)): (String, Int) =
  (p._2, p._1)
@main def main: IO[Unit] =
  (1, "x") match {
    case (n, s) => IO.println(s)
  }
"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.defs[0].params[0].ty,
            Type::Tuple(xs) if matches!(xs.as_slice(), [Type::Int, Type::String])
        ));
        assert!(matches!(
            &p.defs[0].ret,
            Type::Tuple(xs) if matches!(xs.as_slice(), [Type::String, Type::Int])
        ));
        match &p.defs[0].body.kind {
            ExprKind::Tuple { elems } => {
                assert_eq!(elems.len(), 2);
                assert!(matches!(&elems[0].kind, ExprKind::Field { field, .. } if field == "_2"));
                assert!(matches!(&elems[1].kind, ExprKind::Field { field, .. } if field == "_1"));
            }
            other => panic!("expected tuple, got {other:?}"),
        }
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, arms } => {
                assert!(matches!(scrutinee.kind, ExprKind::Tuple { .. }));
                assert!(matches!(arms[0].pattern, Pattern::Tuple { .. }));
            }
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_tuple_for_binder_and_lambda() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    (n, s) = (1, "x")
    (a, b) <- IO.both(IO.pure(2), IO.pure("y"))
  } yield IO.println(List.join(List.map([(3, "z")], (i, t) => t), ","))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, body } => {
                assert_eq!(binders.len(), 2);
                match &binders[0] {
                    ForBinder::Eq { name, pat, .. } => {
                        assert_eq!(name, crate::ast::TUP_UNPACK);
                        assert!(matches!(pat, Some(Pattern::Tuple { .. })));
                    }
                    other => panic!("expected eq tuple binder, got {other:?}"),
                }
                match &binders[1] {
                    ForBinder::Draw { name, pat, .. } => {
                        assert_eq!(name, crate::ast::TUP_UNPACK);
                        assert!(matches!(pat, Some(Pattern::Tuple { .. })));
                    }
                    other => panic!("expected draw tuple binder, got {other:?}"),
                }
                let dumped = format!("{body:?}");
                assert!(dumped.contains("pat: Some"), "{dumped}");
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_nested_tuple_for_binder() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    (n, (s, t)) = (1, ("x", "y"))
  } yield IO.println(s)
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => match &binders[0] {
                ForBinder::Eq { name, pat, .. } => {
                    assert_eq!(name, crate::ast::TUP_UNPACK);
                    match pat {
                        Some(Pattern::Tuple { elems, .. }) => {
                            assert_eq!(elems.len(), 2);
                            assert!(matches!(elems[1], Pattern::Tuple { .. }));
                        }
                        other => panic!("expected nested tuple, got {other:?}"),
                    }
                }
                other => panic!("expected eq binder, got {other:?}"),
            },
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_literal_tuple_lambda() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.map([(1, 0)], (n, 0) => "z"), ","))
"#;
        let p = parse(src).unwrap();
        let dumped = format!("{:?}", p.main.body.kind);
        assert!(
            dumped.contains("pat: Some") || dumped.contains("Int(0)"),
            "{dumped}"
        );
    }

    #[test]
    fn parse_ctor_for_binder_and_lambda() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  for {
    Point(x, y) = Point(1, 2)
    Opt.Some(n) <- IO.pure(Opt.Some(3))
    h :: t = [4, 5]
  } yield IO.println(List.join(List.map([Opt.Some(6)], (Opt.Some(k)) => Str.fromInt(k)), ","))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, body } => {
                assert_eq!(binders.len(), 3);
                match &binders[0] {
                    ForBinder::Eq { name, pat, .. } => {
                        assert_eq!(name, crate::ast::TUP_UNPACK);
                        assert!(
                            matches!(pat, Some(Pattern::Adt { case_name, .. }) if case_name == "Point"),
                            "{pat:?}"
                        );
                    }
                    other => panic!("expected eq ctor binder, got {other:?}"),
                }
                match &binders[1] {
                    ForBinder::Draw { name, pat, .. } => {
                        assert_eq!(name, crate::ast::TUP_UNPACK);
                        assert!(
                            matches!(pat, Some(Pattern::Adt { case_name, .. }) if case_name == "Some"),
                            "{pat:?}"
                        );
                    }
                    other => panic!("expected draw ctor binder, got {other:?}"),
                }
                match &binders[2] {
                    ForBinder::Eq { name, pat, .. } => {
                        assert_eq!(name, crate::ast::TUP_UNPACK);
                        assert!(matches!(pat, Some(Pattern::Cons { .. })), "{pat:?}");
                    }
                    other => panic!("expected eq cons binder, got {other:?}"),
                }
                let dumped = format!("{body:?}");
                assert!(dumped.contains("pat: Some"), "{dumped}");
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn rejects_literal_for_binder() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    0 = 1
  } yield IO.println("no")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("for binder") || err.contains("expected binder"),
            "{err}"
        );
    }

    #[test]
    fn parse_three_slot_tuple() {
        let src = r#"
def f(p: (Int, String, Bool)): Int = p._1
@main def main: IO[Unit] =
  for {
    (a, b, c) = (1, "x", true)
  } yield IO.println(Str.fromInt(f((a, b, c))))
"#;
        let p = parse(src).unwrap();
        assert!(matches!(
            &p.defs[0].params[0].ty,
            Type::Tuple(xs) if xs.len() == 3
        ));
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => match &binders[0] {
                ForBinder::Eq {
                    pat: Some(Pattern::Tuple { elems, .. }),
                    ..
                } => {
                    assert_eq!(elems.len(), 3);
                }
                other => panic!("expected 3-slot binder, got {other:?}"),
            },
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_rejects_nine_slot_tuple() {
        let src = r#"@main def main: IO[Unit] = IO.println((1, 2, 3, 4, 5, 6, 7, 8, 9))"#;
        let err = parse(src).unwrap_err();
        assert!(err.to_string().contains("at most 8 slots"), "{err}");
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
    fn parse_separated_and_scientific_literals() {
        let src = r#"
def n(): Int = 1_000 + 0xFF_00 + 0b1010_0001
def x(): Float = 1.5e1 + 1e-3
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        match &p.defs[0].body.kind {
            ExprKind::Binary { left, .. } => match &left.kind {
                ExprKind::Binary { left, .. } => {
                    assert!(matches!(&left.kind, ExprKind::IntLit(1000)));
                }
                other => panic!("expected 1_000, got {other:?}"),
            },
            other => panic!("expected +, got {other:?}"),
        }
        match &p.defs[1].body.kind {
            ExprKind::Binary { left, right, .. } => {
                assert!(matches!(&left.kind, ExprKind::FloatLit(b) if f64::from_bits(*b) == 15.0));
                assert!(matches!(
                    &right.kind,
                    ExprKind::FloatLit(b) if (f64::from_bits(*b) - 0.001).abs() < 1e-12
                ));
            }
            other => panic!("expected float +, got {other:?}"),
        }
    }

    #[test]
    fn parse_triple_quoted_string() {
        let src = "@main def main: IO[Unit] = IO.println(\"\"\"a\nb\"\"\")\n";
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::StrLit(s) => assert_eq!(s, "a\nb"),
                other => panic!("expected StrLit, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_triple_interpolated_string() {
        let src = "@main def main: IO[Unit] =\n  for {\n    n = 3\n  } yield IO.println(s\"\"\"n=$n\"\"\")\n";
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { body, .. } => match &body.kind {
                ExprKind::IoPrintln(inner) => match &inner.kind {
                    ExprKind::Interpolate { parts } => {
                        assert!(
                            parts.iter().any(|p| matches!(p, InterpPart::Expr(_))),
                            "{parts:?}"
                        );
                    }
                    other => panic!("expected interpolate, got {other:?}"),
                },
                other => panic!("expected println, got {other:?}"),
            },
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_interp_hole_var_span_is_the_name() {
        let src = r#"@main def main: IO[Unit] = IO.println(s"${n}")"#;
        let p = parse(src).unwrap();
        let n_start = src.find("${n}").unwrap() + 2;
        let n_end = n_start + 1;
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Interpolate { parts } => match parts.as_slice() {
                    [InterpPart::Expr(e)] => {
                        assert!(
                            matches!(&e.kind, ExprKind::Var(name) if name == "n"),
                            "{:?}",
                            e.kind
                        );
                        assert_eq!(e.span.start, n_start, "start {:?}", e.span);
                        assert_eq!(e.span.end, n_end, "end {:?}", e.span);
                    }
                    other => panic!("expected one hole, got {other:?}"),
                },
                other => panic!("expected interpolate, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_interp_hole_with_string_brace() {
        let src = r#"@main def main: IO[Unit] = IO.println(s"${Str.concat("}", "x")}")"#;
        parse(src).unwrap();
    }

    #[test]
    fn parse_io_forever_repeat_retry_as_calls() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.forever(IO.sleep(1))
    _ <- IO.repeatN(2, IO.pure("x"))
    _ <- IO.retryN(1, IO.pure("y"))
    _ <- IO.foreach(["a"], x => IO.println(x))
    _ <- IO.when(true, IO.println("y"))
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
                assert!(matches!(
                    &binders[3],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "IO.foreach")
                ));
                assert!(matches!(
                    &binders[4],
                    ForBinder::Draw { value, .. }
                        if matches!(&value.kind, ExprKind::Call { callee, .. } if callee == "IO.when")
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
    fn parse_case_lambda_on_list_map() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.println(List.join(List.map([Opt.Some(1)], { case Opt.Some(n) => Str.fromInt(n) case Opt.None => "n" }), ","))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { callee, args } if callee == "List.join" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "List.map" => {
                        let ExprKind::Lambda { param, body, .. } = &args[1].kind else {
                            panic!("expected lambda, got {:?}", args[1].kind);
                        };
                        let arms = crate::ast::case_lambda_match_arms(param.as_deref(), body)
                            .expect("expected `{ case … }` lambda");
                        assert_eq!(arms.len(), 2, "{arms:?}");
                    }
                    other => panic!("expected List.map, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_case_lambda_on_io_map() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.pure(Opt.Some(1)).map({ case Opt.Some(n) => n case Opt.None => 0 }).flatMap(n => IO.println(Str.fromInt(n)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::FlatMap { inner, .. } => match &inner.kind {
                ExprKind::IoMap { param, body, .. } => {
                    assert_eq!(param.as_deref(), Some(crate::ast::CASE_LAMBDA));
                    assert!(
                        matches!(body.kind, ExprKind::Match { .. }),
                        "expected match body, got {:?}",
                        body.kind
                    );
                }
                other => panic!("expected io.map, got {other:?}"),
            },
            other => panic!("expected flatMap, got {other:?}"),
        }
    }

    #[test]
    fn rejects_statement_block() {
        let src = r#"@main def main: IO[Unit] = { IO.println("x") }"#;
        let err = parse(src).unwrap_err();
        assert!(
            err.message().contains("statement blocks"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn parse_placeholder_hole() {
        let src = r#"@main def main: IO[Unit] = IO.println(List.join(List.map([1], _ + 1), ","))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { callee, args } if callee == "List.join" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "List.map" => {
                        match &args[1].kind {
                            ExprKind::Binary { op, left, right } => {
                                assert!(matches!(op, crate::ast::BinOp::Add), "expected `_ + 1`");
                                assert!(
                                    matches!(left.kind, ExprKind::Placeholder),
                                    "left hole: {:?}",
                                    left.kind
                                );
                                assert!(
                                    matches!(right.kind, ExprKind::IntLit(1)),
                                    "right 1: {:?}",
                                    right.kind
                                );
                            }
                            other => panic!("expected `_ + 1`, got {other:?}"),
                        }
                    }
                    other => panic!("expected List.map, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_placeholder_call_arg() {
        let src = r#"@main def main: IO[Unit] = IO.println(List.join(List.map([1], Str.fromInt(_)), ","))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Call { args, .. } => match &args[1].kind {
                        ExprKind::Call { callee, args } if callee == "Str.fromInt" => {
                            assert!(
                                matches!(args[0].kind, ExprKind::Placeholder),
                                "expected Str.fromInt(_), got {:?}",
                                args[0].kind
                            );
                        }
                        other => panic!("expected Str.fromInt(_), got {other:?}"),
                    },
                    other => panic!("expected List.map, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_fun_expr_apply() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
def addN(n: Int): Int => Int = (m: Int) => n + m
@main def main: IO[Unit] =
  IO.println(Str.fromInt(plusOne()(5)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { callee, args } if callee == "Str.fromInt" => match &args[0].kind {
                    ExprKind::Apply { fun, arg } => {
                        assert!(
                            matches!(&fun.kind, ExprKind::Call { callee, args } if callee == "plusOne" && args.is_empty()),
                            "expected plusOne(), got {:?}",
                            fun.kind
                        );
                        assert!(
                            matches!(arg.kind, ExprKind::IntLit(5)),
                            "expected 5, got {:?}",
                            arg.kind
                        );
                    }
                    other => panic!("expected apply, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
        let src = r#"
def addN(n: Int): Int => Int = (m: Int) => n + m
@main def main: IO[Unit] =
  IO.println(Str.fromInt(addN(3)(4)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Apply { fun, arg } => {
                        assert!(
                            matches!(&fun.kind, ExprKind::Call { callee, args } if callee == "addN" && args.len() == 1),
                            "expected addN(3), got {:?}",
                            fun.kind
                        );
                        assert!(
                            matches!(arg.kind, ExprKind::IntLit(4)),
                            "expected 4, got {:?}",
                            arg.kind
                        );
                    }
                    other => panic!("expected apply, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_lambda_literal_apply() {
        let src = r#"@main def main: IO[Unit] = IO.println(Str.fromInt(((n: Int) => n + 1)(6)))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Apply { fun, arg } => {
                        assert!(
                            matches!(&fun.kind, ExprKind::Lambda { param: Some(n), param_ty: Some(Type::Int), .. } if n == "n"),
                            "expected typed lambda, got {:?}",
                            fun.kind
                        );
                        assert!(
                            matches!(arg.kind, ExprKind::IntLit(6)),
                            "expected 6, got {:?}",
                            arg.kind
                        );
                    }
                    other => panic!("expected apply, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn rejects_empty_apply() {
        let err = parse(r#"@main def main: IO[Unit] = IO.println(plusOne()())"#).unwrap_err();
        assert!(
            err.message().contains("apply expects an argument"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn parse_fun_tuple_apply() {
        let src = r#"
def plusOne(): Int => Int = (n: Int) => n + 1
@main def main: IO[Unit] =
  IO.println(Str.fromInt(((a, b) => a + b)(2, 3)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Apply { fun, arg } => {
                        assert!(
                            matches!(&fun.kind, ExprKind::Lambda { .. }),
                            "expected tuple lambda, got {:?}",
                            fun.kind
                        );
                        assert!(
                            matches!(&arg.kind, ExprKind::Tuple { elems } if elems.len() == 2),
                            "expected (2, 3), got {:?}",
                            arg.kind
                        );
                    }
                    other => panic!("expected apply, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_tuple_binder_after_tuple_value() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    (n, s) = (1, "x")
    (a, b) <- IO.both(IO.pure(2), IO.pure("y"))
  } yield IO.println("ok")
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert_eq!(binders.len(), 2);
                assert!(matches!(
                    &binders[0],
                    ForBinder::Eq {
                        pat: Some(Pattern::Tuple { .. }),
                        ..
                    }
                ));
                assert!(matches!(
                    &binders[1],
                    ForBinder::Draw {
                        pat: Some(Pattern::Tuple { .. }),
                        ..
                    }
                ));
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_typed_lambda_param() {
        let src = r#"@main def main: IO[Unit] = IO.println(List.join(List.map([1], (n: Int) => Str.fromInt(n)), ","))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { callee, args } if callee == "List.join" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "List.map" => {
                        match &args[1].kind {
                            ExprKind::Lambda {
                                param: Some(n),
                                param_ty: Some(Type::Int),
                                ..
                            } if n == "n" => {}
                            other => panic!("expected typed lambda, got {other:?}"),
                        }
                    }
                    other => panic!("expected List.map, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_paren_untyped_lambda() {
        let src =
            r#"@main def main: IO[Unit] = IO.println(List.join(List.map(["a"], (x) => x), ","))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(arg) => match &arg.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Call { args, .. } => match &args[1].kind {
                        ExprKind::Lambda {
                            param: Some(n),
                            param_ty: None,
                            ..
                        } if n == "x" => {}
                        other => panic!("expected untyped paren lambda, got {other:?}"),
                    },
                    other => panic!("expected List.map, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
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
    fn parse_io_map_bound() {
        let src = r#"@main def main: IO[Unit] = IO.pure(1).map(n => n + 1).flatMap(n => IO.println(Str.fromInt(n)))"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::FlatMap { inner, .. } => match &inner.kind {
                ExprKind::IoMap { param: Some(n), .. } if n == "n" => {}
                other => panic!("expected IoMap, got {other:?}"),
            },
            other => panic!("expected FlatMap, got {other:?}"),
        }
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
    fn parse_if_without_else() {
        let src = r#"
def maybeYes(ok: Bool): IO[Unit] =
  if (ok) IO.println("y")
@main def main: IO[Unit] =
  if (true) IO.println("ok")
"#;
        let p = parse(src).unwrap();
        match &p.defs[0].body.kind {
            ExprKind::If { implicit_else, .. } => assert!(*implicit_else),
            other => panic!("expected If, got {other:?}"),
        }
        match &p.main.body.kind {
            ExprKind::If { implicit_else, .. } => assert!(*implicit_else),
            other => panic!("expected If, got {other:?}"),
        }
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
    fn parse_rejects_property_keyword() {
        let src = r#"
property always: Bool = 1 == 1
@main def main: IO[Unit] = IO.println("ok")
"#;
        assert!(parse(src).is_err());
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
    fn parse_param_default() {
        let src = r#"
def add(n: Int, m: Int = 1): Int = n + m
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert!(p.defs[0].params[0].default.is_none());
        match &p.defs[0].params[1].default {
            Some(e) => assert!(matches!(e.kind, ExprKind::IntLit(1))),
            None => panic!("expected default on m"),
        }
    }

    #[test]
    fn parse_param_where_then_default() {
        let src = r#"
def note(n: Int where n >= 0 = 0): Int = n
@main def main: IO[Unit] = IO.println("ok")
"#;
        let p = parse(src).unwrap();
        assert!(p.defs[0].params[0].rfn.is_some());
        match &p.defs[0].params[0].default {
            Some(e) => assert!(matches!(e.kind, ExprKind::IntLit(0))),
            None => panic!("expected default on n"),
        }
    }

    #[test]
    fn parse_rejects_non_trailing_default() {
        let src = r#"
def add(n: Int = 1, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println("ok")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(err.contains("needs a default"), "unexpected: {err}");
    }

    #[test]
    fn parse_rejects_method_param_default() {
        let src = r#"
record Point(x: Int, y: Int):
  def bump(n: Int = 1): Int = self.x + n
@main def main: IO[Unit] = IO.println("ok")
"#;
        let err = parse(src).unwrap_err().to_string();
        assert!(
            err.contains("method parameters cannot have a default"),
            "unexpected: {err}"
        );
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
    fn parse_for_if_guard() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    x <- IO.pure(1)
    if x > 0
  } yield IO.println(Str.fromInt(x))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                assert_eq!(binders.len(), 2);
                assert!(matches!(&binders[0], ForBinder::Draw { name, .. } if name == "x"));
                assert!(matches!(&binders[1], ForBinder::Guard { .. }));
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_for_if_guard_needs_draw() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    x = 1
    if x > 0
  } yield IO.println("x")
"#;
        let err = parse(src).unwrap_err();
        assert!(
            err.message().contains("`if` in `for` needs a `<-` binder"),
            "{}",
            err.message()
        );
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

    fn parse_guard_program(guard: &str) -> Program {
        let src = format!(
            r#"
@main def main: IO[Unit] =
  v match {{
    case v if {guard} => IO.println("ok")
    case _ => IO.println("no")
  }}
"#
        );
        parse(&src).unwrap_or_else(|e| panic!("guard `{guard}` failed: {e}"))
    }

    #[test]
    fn parse_match_guard_does_not_steal_case_arrow() {
        for guard in [
            "flag",
            "n > m",
            "a && b",
            "!flag",
            "(flag)",
            "pred(n => n > 0)",
            "(flag: Bool)",
        ] {
            let p = parse_guard_program(guard);
            match &p.main.body.kind {
                ExprKind::Match { arms, .. } => {
                    assert!(
                        arms[0].guard.is_some(),
                        "guard `{guard}` needs a guard expr"
                    );
                    assert!(arms[1].guard.is_none());
                }
                other => panic!("guard `{guard}`: expected match, got {other:?}"),
            }
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
    fn parse_as_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  o match {
    case s @ Opt.Some(n) => IO.println("s")
    case Opt.None => IO.println("n")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::As { name, inner } => {
                    assert_eq!(name, "s");
                    assert!(
                        matches!(
                            inner.as_ref(),
                            Pattern::Adt { case_name, binds, .. }
                                if case_name == "Some"
                                    && matches!(&binds[..], [Pattern::Bind(n)] if n == "n")
                        ),
                        "{inner:?}"
                    );
                }
                other => panic!("expected as-pattern, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_as_pattern_wraps_or() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  c match {
    case p @ Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::As { name, inner } => {
                    assert_eq!(name, "p");
                    assert!(matches!(inner.as_ref(), Pattern::Or(alts) if alts.len() == 2));
                }
                other => panic!("expected as wrapping or, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_nested_as_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(n @ 0) => IO.println("z")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => match &binds[..] {
                    [Pattern::As { name, inner }] => {
                        assert_eq!(name, "n");
                        assert!(matches!(inner.as_ref(), Pattern::Int(0)));
                    }
                    other => panic!("expected nested as, got {other:?}"),
                },
                other => panic!("expected Opt.Some(n @ 0), got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_nil_and_cons_patterns() {
        let src = r#"
@main def main: IO[Unit] =
  xs match {
    case [] => IO.println("e")
    case x :: xs => IO.println(x)
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert!(
                    matches!(&arms[0].pattern, Pattern::Nil),
                    "{:?}",
                    arms[0].pattern
                );
                match &arms[1].pattern {
                    Pattern::Cons { head, tail, .. } => {
                        assert!(matches!(head.as_ref(), Pattern::Bind(n) if n == "x"));
                        assert!(matches!(tail.as_ref(), Pattern::Bind(n) if n == "xs"));
                    }
                    other => panic!("expected cons, got {other:?}"),
                }
            }
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_list_literal_pattern() {
        let src = r#"
@main def main: IO[Unit] =
  xs match {
    case ["a", "b"] => IO.println("ab")
    case _ => IO.println("no")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Cons { head, tail, .. } => {
                    assert!(matches!(head.as_ref(), Pattern::Str(s) if s == "a"));
                    match tail.as_ref() {
                        Pattern::Cons { head, tail, .. } => {
                            assert!(matches!(head.as_ref(), Pattern::Str(s) if s == "b"));
                            assert!(matches!(tail.as_ref(), Pattern::Nil));
                        }
                        other => panic!("expected inner cons, got {other:?}"),
                    }
                }
                other => panic!("expected list lit pat, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_named_field_pattern() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  Point(3, 5) match {
    case Point(x = n) => IO.println(Str.fromInt(n))
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => match &binds[..] {
                    [Pattern::Named { name, inner }] => {
                        assert_eq!(name, "x");
                        assert!(matches!(inner.as_ref(), Pattern::Bind(n) if n == "n"));
                    }
                    other => panic!("expected named field, got {other:?}"),
                },
                other => panic!("expected Point named, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_named_field_mixed_positional() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  Point(3, 5) match {
    case Point(n, y = 0) => IO.println(Str.fromInt(n))
    case Point(_, _) => IO.println("o")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => {
                    assert!(matches!(&binds[0], Pattern::Bind(n) if n == "n"));
                    assert!(
                        matches!(&binds[1], Pattern::Named { name, inner } if name == "y" && matches!(inner.as_ref(), Pattern::Int(0)))
                    );
                }
                other => panic!("expected mixed named, got {other:?}"),
            },
            other => panic!("expected match, got {other:?}"),
        }
    }

    #[test]
    fn parse_cons_expr() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join("a" :: "b" :: List.empty(), ","))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Call { callee, args } if callee == "List.join" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "List.cons" => {
                        assert!(matches!(&args[0].kind, ExprKind::StrLit(s) if s == "a"));
                        match &args[1].kind {
                            ExprKind::Call { callee, .. } if callee == "List.cons" => {}
                            other => panic!("expected nested cons, got {other:?}"),
                        }
                    }
                    other => panic!("expected List.cons, got {other:?}"),
                },
                other => panic!("expected List.join, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_as_wrapping_cons() {
        let src = r#"
@main def main: IO[Unit] =
  xs match {
    case ys @ _ :: _ => IO.println("n")
    case [] => IO.println("e")
  }
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::As { name, inner } => {
                    assert_eq!(name, "ys");
                    assert!(matches!(inner.as_ref(), Pattern::Cons { .. }));
                }
                other => panic!("expected as-cons, got {other:?}"),
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
    fn parse_record_does_not_swallow_following_def() {
        let src = r#"
record Box[T](x: T):
  def get(): T = self.x
def extra(): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.enums[0].methods.len(), 1);
        assert_eq!(p.enums[0].methods[0].name, "get");
        assert_eq!(p.defs[0].name, "extra");
    }

    #[test]
    fn parse_impl_does_not_swallow_following_def() {
        let src = r#"
record Point(x: Int)
trait Show:
  def show(): String
impl Show for Point:
  def show(): String = "p"
def extra(): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.impls[0].methods.len(), 1);
        assert_eq!(p.impls[0].methods[0].name, "show");
        assert_eq!(p.defs[0].name, "extra");
    }

    #[test]
    fn parse_trait_does_not_swallow_following_def() {
        let src = r#"
trait Show:
  def show(): String
def extra(): Int = 1
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.traits[0].methods.len(), 1);
        assert_eq!(p.traits[0].methods[0].name, "show");
        assert_eq!(p.defs[0].name, "extra");
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
                implicit_else,
            } => {
                assert!(!*implicit_else);
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

    #[test]
    fn parse_named_args() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(m = 2, n = 1)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Call { callee, args } if callee == "Str.fromInt" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "add" => {
                        assert_eq!(args.len(), 2);
                        assert!(
                            matches!(&args[0].kind, ExprKind::NamedArg { name, .. } if name == "m"),
                            "{:?}",
                            args[0]
                        );
                        assert!(
                            matches!(&args[1].kind, ExprKind::NamedArg { name, .. } if name == "n"),
                            "{:?}",
                            args[1]
                        );
                    }
                    other => panic!("expected add call, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_named_args_trailing_comma() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(m = 2, n = 1,)))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Call { callee, args } if callee == "Str.fromInt" => match &args[0].kind {
                    ExprKind::Call { callee, args } if callee == "add" => {
                        assert_eq!(args.len(), 2);
                    }
                    other => panic!("expected add call, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_record_copy() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy(y = 9).x))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::IoPrintln(inner) => match &inner.kind {
                ExprKind::Call { args, .. } => match &args[0].kind {
                    ExprKind::Field { base, field } if field == "x" => match &base.kind {
                        ExprKind::MethodCall {
                            method,
                            args: cargs,
                            ..
                        } if method == "copy" => {
                            assert_eq!(cargs.len(), 1);
                            assert!(
                                matches!(
                                    &cargs[0].kind,
                                    ExprKind::NamedArg { name, .. } if name == "y"
                                ),
                                "{:?}",
                                cargs[0]
                            );
                        }
                        other => panic!("expected copy, got {other:?}"),
                    },
                    other => panic!("expected field, got {other:?}"),
                },
                other => panic!("expected Str.fromInt, got {other:?}"),
            },
            other => panic!("expected println, got {other:?}"),
        }
    }

    #[test]
    fn parse_field_access_span_covers_name() {
        let src = r#"
record Point(x: Int)
def f(p: Point): Int = p.x
@main def main: IO[Unit] = IO.println("x")
"#;
        let p = parse(src).unwrap();
        let needle = "p.x";
        let start = src.find(needle).unwrap();
        let end = start + needle.len();
        match &p.defs[0].body.kind {
            ExprKind::Field { field, .. } if field == "x" => {
                assert_eq!(
                    p.defs[0].body.span.start, start,
                    "{:?}",
                    p.defs[0].body.span
                );
                assert_eq!(p.defs[0].body.span.end, end, "{:?}", p.defs[0].body.span);
            }
            other => panic!("expected field, got {other:?}"),
        }
    }

    #[test]
    fn parse_type_ascription() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    x = Opt.None: Opt[Int]
    n = 1 + 2: Int
  } yield IO.println(Str.fromInt(n))
"#;
        let p = parse(src).unwrap();
        match &p.main.body.kind {
            ExprKind::For { binders, .. } => {
                match &binders[0] {
                    ForBinder::Eq { value, .. } => match &value.kind {
                        ExprKind::Ascribe { ty, expr } => {
                            assert!(
                                matches!(ty, Type::App(n, args) if n == "Opt" && matches!(&args[0], Type::Int)),
                                "{ty:?}"
                            );
                            assert!(matches!(expr.kind, ExprKind::AdtConstruct { .. }));
                        }
                        other => panic!("expected ascribe, got {other:?}"),
                    },
                    other => panic!("expected eq binder, got {other:?}"),
                }
                match &binders[1] {
                    ForBinder::Eq { value, .. } => match &value.kind {
                        ExprKind::Ascribe { ty, expr } => {
                            assert!(matches!(ty, Type::Int), "{ty:?}");
                            assert!(matches!(expr.kind, ExprKind::Binary { .. }));
                        }
                        other => panic!("expected ascribe sum, got {other:?}"),
                    },
                    other => panic!("expected eq binder, got {other:?}"),
                }
            }
            other => panic!("expected for, got {other:?}"),
        }
    }

    #[test]
    fn parse_import_alias_and_wildcard() {
        let src = r#"
import A.tag as fromA
import A.*
@main def main: IO[Unit] = IO.println(fromA())
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.imports.len(), 2);
        assert_eq!(p.imports[0].from_module, "A");
        assert_eq!(p.imports[0].name, "tag");
        assert_eq!(p.imports[0].alias.as_deref(), Some("fromA"));
        assert!(p.imports[1].is_wildcard());
        assert_eq!(
            &src[p.imports[0].span.start..p.imports[0].span.end],
            "A.tag as fromA"
        );
        assert_eq!(&src[p.imports[1].span.start..p.imports[1].span.end], "A.*");
    }

    #[test]
    fn parse_import_star_rejects_as() {
        let src = "import A.* as x\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let err = parse(src).unwrap_err();
        assert!(
            err.message().contains("does not take `as`"),
            "{}",
            err.message()
        );
    }

    #[test]
    fn parse_import_rejects_same_alias() {
        let src = "import A.tag as tag\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let err = parse(src).unwrap_err();
        assert!(err.message().contains("same as"), "{}", err.message());
    }

    #[test]
    fn parse_type_alias() {
        let src = r#"
type UserId = Int
type Labels = List[String]
type BoxList[T] = List[T]
def idOf(n: UserId): UserId = n
@main def main: IO[Unit] = IO.println(Str.fromInt(idOf(1)))
"#;
        let p = parse(src).unwrap();
        assert_eq!(p.aliases.len(), 3);
        assert_eq!(p.aliases[0].name, "UserId");
        assert!(p.aliases[0].type_params.is_empty());
        assert!(matches!(p.aliases[0].target, Type::Int));
        assert_eq!(p.aliases[1].name, "Labels");
        assert!(matches!(p.aliases[1].target, Type::List(ref t) if matches!(**t, Type::String)));
        assert_eq!(p.aliases[2].name, "BoxList");
        assert_eq!(p.aliases[2].type_params, vec!["T".to_string()]);
        assert!(
            matches!(p.aliases[2].target, Type::List(ref t) if matches!(**t, Type::Var(ref n) if n == "T"))
        );
    }

    #[test]
    fn parse_rejects_duplicate_type_alias() {
        let src = r#"
type UserId = Int
type UserId = String
@main def main: IO[Unit] = IO.println("x")
"#;
        let err = parse(src).unwrap_err();
        assert!(
            err.message().contains("duplicate type UserId"),
            "{}",
            err.message()
        );
    }
}

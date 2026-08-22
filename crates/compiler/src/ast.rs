//! AST for the kernel dialect.

use crate::span::Span;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dotted package path, for example `["scuzz", "compiler"]`.
    pub package: Vec<String>,
    pub enums: Vec<EnumDef>,
    /// `type Name = T` / `type Name[T] = List[T]`. Expanded before typecheck.
    pub aliases: Vec<TypeAlias>,
    pub traits: Vec<TraitDef>,
    pub impls: Vec<ImplDef>,
    pub defs: Vec<FunDef>,
    pub main: MainDef,
    /// `import Module.name` / `import Module.name as alias` / `import Module.*`.
    /// Bare `alias` (or `name`) in `in_module` resolves to `from_module.name`.
    pub imports: Vec<Import>,
    /// Law def names residualized under TestRuntime (empty for live builds).
    pub law_names: Vec<String>,
    /// Driver def names merged from `*.scuzz_drivers` (empty for live builds).
    pub driver_names: Vec<String>,
}

/// `trait Show:` / `trait Get[T]: def getOrElse(default: T): T`
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TraitDef {
    pub module: String,
    pub name: String,
    /// `trait Get[T]:` — instantiated from the impl target's type params.
    pub type_params: Vec<String>,
    pub methods: Vec<TraitMethod>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TraitMethod {
    pub name: String,
    pub params: Vec<Param>,
    pub ret: Type,
}

/// `impl Show for Point:` / `impl Get[Int] for Point:` (`self` is implicit in the body).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImplDef {
    pub module: String,
    pub trait_name: String,
    /// `impl Get[Int] for Point` — empty means instantiate from the target's type params.
    pub trait_args: Vec<Type>,
    pub for_type: String,
    pub methods: Vec<ImplMethod>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImplMethod {
    pub name: String,
    pub params: Vec<Param>,
    pub ret: Type,
    pub body: Expr,
}

/// `type Name = T` / `type Name[T] = List[T]` in file-stem module `module`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TypeAlias {
    pub module: String,
    pub name: String,
    /// Span of the alias name.
    pub name_span: Span,
    /// `type BoxList[T] = List[T]` — empty when the alias is not generic.
    pub type_params: Vec<String>,
    pub target: Type,
}

/// Top-level `import FromModule.name` in file-stem module `in_module`.
/// `name` is `"*"` for `import Module.*`. `alias` is `Some` for `as alias`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Import {
    pub in_module: String,
    pub from_module: String,
    pub name: String,
    pub alias: Option<String>,
    /// Span of `Module.name`, `Module.name as alias`, or `Module.*`.
    pub span: Span,
}

impl Import {
    /// Bare name this import binds in `in_module`. Wildcard has no single local name.
    pub fn local_name(&self) -> &str {
        self.alias.as_deref().unwrap_or(&self.name)
    }

    pub fn is_wildcard(&self) -> bool {
        self.name == "*"
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumDef {
    /// File-stem module id (`Foo.scuzz` → `Foo`). Empty when parsed without a path.
    pub module: String,
    pub name: String,
    /// `enum Opt[T]:` — monomorphized to per-instantiation clones before codegen.
    pub type_params: Vec<String>,
    pub cases: Vec<EnumCase>,
    /// `record Name(…)` — single case with the same name; surface sugar for construct/match.
    pub is_record: bool,
    /// `record Box[T](x: T): def get()` / `enum Opt[T]: def getOrElse()` — expanded to `__rec_*` before typecheck.
    pub methods: Vec<ImplMethod>,
}

/// One `case Name` / `case Name(x: T)` in an enum.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumCase {
    pub name: String,
    /// Empty = nullary. Multi-field payloads pack as `List` in `sz_adt_payload`.
    pub fields: Vec<(String, Type)>,
    /// Parallel to `fields`; `where` predicates on record (or case) fields.
    pub field_rfns: Vec<Option<Expr>>,
}

impl EnumCase {
    pub fn field_rfn(&self, i: usize) -> Option<&Expr> {
        self.field_rfns.get(i).and_then(|o| o.as_ref())
    }

    pub fn has_rfns(&self) -> bool {
        self.field_rfns.iter().any(|o| o.is_some())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunDef {
    /// File-stem module id (`Foo.scuzz` → `Foo`). Empty when parsed without a path.
    pub module: String,
    pub name: String,
    /// Span of the def name.
    pub name_span: Span,
    /// `private def` — visible only within `module`. Default public.
    pub is_private: bool,
    /// Top-level `law name: Bool = …` — erased from live builds; residualized under verify.
    pub is_law: bool,
    /// Def merged from `*.scuzz_drivers` (verify graph only).
    pub is_driver: bool,
    /// `def foo[T](…)` — monomorphized before codegen.
    pub type_params: Vec<String>,
    pub params: Vec<Param>,
    pub ret: Type,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Param {
    pub name: String,
    pub ty: Type,
    /// `n: Int where n >= 0` — residualized at calls under the verify graph.
    pub rfn: Option<Expr>,
    /// `m: Int = 1` — filled at the call when the argument is omitted.
    pub default: Option<Expr>,
    /// Span of the parameter name.
    pub span: Span,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MainDef {
    /// File-stem module id of the `@main` source file.
    pub module: String,
    pub name: String,
    pub body: Expr,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    And,
    Or,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
}

/// Prefix operator: `-e`, `!e`, `~e`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnOp {
    Neg,
    Not,
    BitNot,
}

/// Expression with source span.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Expr {
    pub kind: ExprKind,
    pub span: Span,
}

impl Expr {
    pub fn new(kind: ExprKind, span: Span) -> Self {
        Self { kind, span }
    }

    pub fn dummy(kind: ExprKind) -> Self {
        Self {
            kind,
            span: Span::dummy(),
        }
    }

    pub fn map_children(self, mut f: impl FnMut(Expr) -> Expr) -> Expr {
        self.try_map_children(|e| Ok::<_, std::convert::Infallible>(f(e)))
            .unwrap()
    }

    pub fn try_map_children<E>(
        self,
        mut f: impl FnMut(Expr) -> Result<Expr, E>,
    ) -> Result<Self, E> {
        let span = self.span;
        let kind = match self.kind {
            ExprKind::IoPrintln(x) => ExprKind::IoPrintln(Box::new(f(*x)?)),
            ExprKind::IoSleep(x) => ExprKind::IoSleep(Box::new(f(*x)?)),
            ExprKind::IoFail(x) => ExprKind::IoFail(Box::new(f(*x)?)),
            ExprKind::IoPure(x) => ExprKind::IoPure(Box::new(f(*x)?)),
            ExprKind::Attempt { inner } => ExprKind::Attempt {
                inner: Box::new(f(*inner)?),
            },
            ExprKind::Field { base, field } => ExprKind::Field {
                base: Box::new(f(*base)?),
                field,
            },
            ExprKind::Lambda {
                param,
                param_ty,
                pat,
                body,
            } => ExprKind::Lambda {
                param,
                param_ty,
                pat,
                body: Box::new(f(*body)?),
            },
            ExprKind::FlatMap { inner, param, body } => ExprKind::FlatMap {
                inner: Box::new(f(*inner)?),
                param,
                body: Box::new(f(*body)?),
            },
            ExprKind::HandleErrorWith { inner, param, body } => ExprKind::HandleErrorWith {
                inner: Box::new(f(*inner)?),
                param,
                body: Box::new(f(*body)?),
            },
            ExprKind::Let { name, value, body } => ExprKind::Let {
                name,
                value: Box::new(f(*value)?),
                body: Box::new(f(*body)?),
            },
            ExprKind::IoEnsure { inner, finalizer } => ExprKind::IoEnsure {
                inner: Box::new(f(*inner)?),
                finalizer: Box::new(f(*finalizer)?),
            },
            ExprKind::IoRace { left, right } => ExprKind::IoRace {
                left: Box::new(f(*left)?),
                right: Box::new(f(*right)?),
            },
            ExprKind::IoBoth { left, right } => ExprKind::IoBoth {
                left: Box::new(f(*left)?),
                right: Box::new(f(*right)?),
            },
            ExprKind::Tuple { elems } => ExprKind::Tuple {
                elems: elems.into_iter().map(f).collect::<Result<Vec<_>, _>>()?,
            },
            ExprKind::IoTimeout { ms, inner } => ExprKind::IoTimeout {
                ms: Box::new(f(*ms)?),
                inner: Box::new(f(*inner)?),
            },
            ExprKind::Binary { op, left, right } => ExprKind::Binary {
                op,
                left: Box::new(f(*left)?),
                right: Box::new(f(*right)?),
            },
            ExprKind::Unary { op, expr } => ExprKind::Unary {
                op,
                expr: Box::new(f(*expr)?),
            },
            ExprKind::Ascribe { expr, ty } => ExprKind::Ascribe {
                expr: Box::new(f(*expr)?),
                ty,
            },
            ExprKind::NamedArg { name, value } => ExprKind::NamedArg {
                name,
                value: Box::new(f(*value)?),
            },
            ExprKind::If {
                cond,
                then_branch,
                else_branch,
            } => ExprKind::If {
                cond: Box::new(f(*cond)?),
                then_branch: Box::new(f(*then_branch)?),
                else_branch: Box::new(f(*else_branch)?),
            },
            ExprKind::MethodCall {
                receiver,
                method,
                args,
            } => ExprKind::MethodCall {
                receiver: Box::new(f(*receiver)?),
                method,
                args: map_expr_vec(args, &mut f)?,
            },
            ExprKind::Call { callee, args } => ExprKind::Call {
                callee,
                args: map_expr_vec(args, &mut f)?,
            },
            ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args,
                type_args,
            } => ExprKind::AdtConstruct {
                enum_name,
                case_name,
                args: map_expr_vec(args, &mut f)?,
                type_args,
            },
            ExprKind::ListLit { elems } => ExprKind::ListLit {
                elems: map_expr_vec(elems, &mut f)?,
            },
            ExprKind::For { binders, body } => {
                let mut out = Vec::with_capacity(binders.len());
                for b in binders {
                    out.push(match b {
                        ForBinder::Eq {
                            name,
                            span,
                            value,
                            pat,
                        } => ForBinder::Eq {
                            name,
                            span,
                            value: f(value)?,
                            pat,
                        },
                        ForBinder::Draw {
                            name,
                            span,
                            value,
                            pat,
                        } => ForBinder::Draw {
                            name,
                            span,
                            value: f(value)?,
                            pat,
                        },
                        ForBinder::Guard { pred, span } => ForBinder::Guard {
                            pred: f(pred)?,
                            span,
                        },
                    });
                }
                ExprKind::For {
                    binders: out,
                    body: Box::new(f(*body)?),
                }
            }
            ExprKind::Match { scrutinee, arms } => {
                let mut out = Vec::with_capacity(arms.len());
                for a in arms {
                    out.push(MatchArm {
                        pattern: a.pattern,
                        guard: match a.guard {
                            Some(g) => Some(f(g)?),
                            None => None,
                        },
                        body: f(a.body)?,
                        unpack: a.unpack,
                    });
                }
                ExprKind::Match {
                    scrutinee: Box::new(f(*scrutinee)?),
                    arms: out,
                }
            }
            ExprKind::Interpolate { parts } => {
                let mut out = Vec::with_capacity(parts.len());
                for p in parts {
                    out.push(match p {
                        InterpPart::Lit(s) => InterpPart::Lit(s),
                        InterpPart::Expr(e) => InterpPart::Expr(f(e)?),
                    });
                }
                ExprKind::Interpolate { parts: out }
            }
            leaf @ (ExprKind::Var(_)
            | ExprKind::Unit
            | ExprKind::IntLit(_)
            | ExprKind::FloatLit(_)
            | ExprKind::BoolLit(_)
            | ExprKind::StrLit(_)
            | ExprKind::Placeholder) => leaf,
        };
        Ok(Expr { kind, span })
    }

    pub fn for_each_child(&self, mut f: impl FnMut(&Expr)) {
        match &self.kind {
            ExprKind::IoPrintln(x)
            | ExprKind::IoSleep(x)
            | ExprKind::IoFail(x)
            | ExprKind::IoPure(x)
            | ExprKind::Attempt { inner: x }
            | ExprKind::Field { base: x, .. }
            | ExprKind::Lambda { body: x, .. } => f(x),
            ExprKind::FlatMap { inner, body, .. }
            | ExprKind::HandleErrorWith { inner, body, .. }
            | ExprKind::Let {
                value: inner, body, ..
            }
            | ExprKind::IoEnsure {
                inner,
                finalizer: body,
            }
            | ExprKind::IoRace {
                left: inner,
                right: body,
            }
            | ExprKind::IoBoth {
                left: inner,
                right: body,
            }
            | ExprKind::Binary {
                left: inner,
                right: body,
                ..
            }
            | ExprKind::IoTimeout {
                ms: inner,
                inner: body,
            } => {
                f(inner);
                f(body);
            }
            ExprKind::Tuple { elems } => {
                for e in elems {
                    f(e);
                }
            }
            ExprKind::Unary { expr, .. }
            | ExprKind::NamedArg { value: expr, .. }
            | ExprKind::Ascribe { expr, .. } => f(expr),
            ExprKind::If {
                cond,
                then_branch,
                else_branch,
            } => {
                f(cond);
                f(then_branch);
                f(else_branch);
            }
            ExprKind::MethodCall { receiver, args, .. } => {
                f(receiver);
                for a in args {
                    f(a);
                }
            }
            ExprKind::Call { args, .. }
            | ExprKind::AdtConstruct { args, .. }
            | ExprKind::ListLit { elems: args } => {
                for a in args {
                    f(a);
                }
            }
            ExprKind::For { binders, body } => {
                for b in binders {
                    match b {
                        ForBinder::Eq { value, .. } | ForBinder::Draw { value, .. } => f(value),
                        ForBinder::Guard { pred, .. } => f(pred),
                    }
                }
                f(body);
            }
            ExprKind::Match { scrutinee, arms } => {
                f(scrutinee);
                for a in arms {
                    if let Some(g) = &a.guard {
                        f(g);
                    }
                    f(&a.body);
                }
            }
            ExprKind::Interpolate { parts } => {
                for p in parts {
                    if let InterpPart::Expr(e) = p {
                        f(e);
                    }
                }
            }
            ExprKind::Var(_)
            | ExprKind::Unit
            | ExprKind::IntLit(_)
            | ExprKind::FloatLit(_)
            | ExprKind::BoolLit(_)
            | ExprKind::StrLit(_)
            | ExprKind::Placeholder => {}
        }
    }
}

/// Binder name for a wrapped `_` hole. Starts with `_` so unused stays quiet.
pub const PLACEHOLDER_PARAM: &str = "__ph";

/// Count `_` holes. Nested lambdas keep their own holes.
pub fn count_placeholders(e: &Expr) -> usize {
    match &e.kind {
        ExprKind::Placeholder => 1,
        ExprKind::Lambda { .. } => 0,
        _ => {
            let mut n = 0;
            e.for_each_child(|c| n += count_placeholders(c));
            n
        }
    }
}

/// Replace each `_` hole with `name`. Nested lambdas stay unchanged.
pub fn replace_placeholder(e: Expr, name: &str) -> Expr {
    match e.kind {
        ExprKind::Placeholder => Expr::new(ExprKind::Var(name.into()), e.span),
        ExprKind::Lambda { .. } => e,
        kind => Expr { kind, span: e.span }.map_children(|c| replace_placeholder(c, name)),
    }
}

fn map_expr_vec<E>(
    args: Vec<Expr>,
    f: &mut impl FnMut(Expr) -> Result<Expr, E>,
) -> Result<Vec<Expr>, E> {
    let mut out = Vec::with_capacity(args.len());
    for a in args {
        out.push(f(a)?);
    }
    Ok(out)
}

impl Program {
    pub fn map_bodies_mut(&mut self, mut f: impl FnMut(Expr) -> Expr) {
        for d in &mut self.defs {
            d.body = f(std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)));
        }
        self.main.body = f(std::mem::replace(
            &mut self.main.body,
            Expr::dummy(ExprKind::Unit),
        ));
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExprKind {
    /// `IO.println(expr)`
    IoPrintln(Box<Expr>),
    /// `IO.sleep(expr)`
    IoSleep(Box<Expr>),
    /// `IO.fail(expr)`
    IoFail(Box<Expr>),
    /// `IO.pure(expr)`
    IoPure(Box<Expr>),
    /// `left.flatMap(param => right)` — param None means `_`
    FlatMap {
        inner: Box<Expr>,
        param: Option<String>,
        body: Box<Expr>,
    },
    /// `io.handleErrorWith(err => body)` — param None means `_`
    HandleErrorWith {
        inner: Box<Expr>,
        param: Option<String>,
        body: Box<Expr>,
    },
    /// `io.attempt`
    Attempt { inner: Box<Expr> },
    /// `IO.race(a, b)`
    IoRace { left: Box<Expr>, right: Box<Expr> },
    /// `IO.both(a, b)`
    IoBoth { left: Box<Expr>, right: Box<Expr> },
    /// `IO.ensure(inner, finalizer)` — run finalizer on success, failure, and cancel
    IoEnsure {
        inner: Box<Expr>,
        finalizer: Box<Expr>,
    },
    /// `IO.timeout(ms, inner)` — race sleep-fail vs inner; preserves inner's `IO[T]`
    IoTimeout { ms: Box<Expr>, inner: Box<Expr> },
    /// Lowered `for` pure binder: `name = value` then `body`
    Let {
        name: String,
        value: Box<Expr>,
        body: Box<Expr>,
    },
    /// `for { binders… } yield body` — sugar over `Let` / `FlatMap` (lowered before codegen).
    For {
        binders: Vec<ForBinder>,
        body: Box<Expr>,
    },
    /// Local binding / parameter reference
    Var(String),
    /// `p.x` record field projection (resolved to match before codegen).
    Field { base: Box<Expr>, field: String },
    /// `p.show(args)` trait method call (resolved to Call before codegen).
    MethodCall {
        receiver: Box<Expr>,
        method: String,
        args: Vec<Expr>,
    },
    /// `Color.Red` / `Opt.Some(x)` ADT case construct
    AdtConstruct {
        enum_name: String,
        case_name: String,
        args: Vec<Expr>,
        /// Instantiation args for generic enums, filled by elaboration; empty otherwise.
        type_args: Vec<Type>,
    },
    /// `scrutinee match { case Pat => expr ... }`
    Match {
        scrutinee: Box<Expr>,
        arms: Vec<MatchArm>,
    },
    /// `()`
    Unit,
    /// Integer literal
    IntLit(i64),
    /// `Float` literal as IEEE-754 bits (`1.5`, `1.5e-3`).
    FloatLit(u64),
    /// `true` / `false`
    BoolLit(bool),
    /// String literal
    StrLit(String),
    /// List literal `[a, b, c]`
    ListLit { elems: Vec<Expr> },
    /// `(a, b)` / `(a, b, c)` — two or more slots. Runtime is right-nested `SzPair`.
    Tuple { elems: Vec<Expr> },
    /// `s"...$x..."` / `s"...${expr}..."` — typed concat (Int / Float holes stringify).
    Interpolate { parts: Vec<InterpPart> },
    /// `if (cond) then else else_`
    If {
        cond: Box<Expr>,
        then_branch: Box<Expr>,
        else_branch: Box<Expr>,
    },
    /// Binary operator
    Binary {
        op: BinOp,
        left: Box<Expr>,
        right: Box<Expr>,
    },
    /// Prefix `-e` / `!e` / `~e`
    Unary { op: UnOp, expr: Box<Expr> },
    /// `e: T` — pin the type of `e`. Elaboration strips this after it fills expected types.
    Ascribe { expr: Box<Expr>, ty: Type },
    /// Builtin or user call: `Str.concat(a,b)`, `foo(x)`, `Fs.read(p)`
    Call { callee: String, args: Vec<Expr> },
    /// `name = expr` in a call argument list. Rewritten to positional before typecheck.
    NamedArg { name: String, value: Box<Expr> },
    /// `_ => expr` or `x => expr` — single-param lambda literal (tap callbacks).
    /// `(a, b) =>` sets `pat` and binds `param` to `"__tup"` until lower unpacks.
    Lambda {
        param: Option<String>,
        /// `Some` for `(x: T) =>`. Kit args still bind from the callee.
        param_ty: Option<Type>,
        /// `Some` for `(a, b) =>`. Lower rewrites the body to a match.
        pat: Option<Box<Pattern>>,
        body: Box<Expr>,
    },
    /// `_` in an expression. A kit or `A => B` argument wraps one hole as a lambda.
    Placeholder,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum InterpPart {
    Lit(String),
    Expr(Expr),
}

/// Synthetic binder for `(a, b) = e` / `Opt.Some(n) =>` until lower unpacks the pattern.
pub const TUP_UNPACK: &str = "__tup";

/// Max tuple slots. Nest tuples for a larger product.
pub const MAX_TUPLE_ARITY: usize = 8;

/// Parse `_1` … `_8` as a 0-based slot index.
pub fn tuple_slot(field: &str) -> Option<usize> {
    let n = field.strip_prefix('_')?;
    if n.is_empty() || !n.bytes().all(|b| b.is_ascii_digit()) || n.starts_with('0') {
        return None;
    }
    let i: usize = n.parse().ok()?;
    if (1..=MAX_TUPLE_ARITY).contains(&i) {
        Some(i - 1)
    } else {
        None
    }
}

/// Binder inside `for { … }`: `x = e` (pure), `x <- e` (effect), or `if pred`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ForBinder {
    /// `name = value` — `pat` unpacks `(a, b) = value` / `Opt.Some(n) = value`.
    Eq {
        name: String,
        span: Span,
        value: Expr,
        pat: Option<Pattern>,
    },
    /// `name <- value` (`name` may be `"_"`) — `pat` unpacks `(a, b) <- value` / `Opt.Some(n) <- value`.
    Draw {
        name: String,
        span: Span,
        value: Expr,
        pat: Option<Pattern>,
    },
    /// `if pred` — a miss is `IO.fail`. The `for` needs a `<-` binder.
    Guard { pred: Expr, span: Span },
}

impl ForBinder {
    pub fn name(&self) -> &str {
        match self {
            ForBinder::Eq { name, .. } | ForBinder::Draw { name, .. } => name,
            ForBinder::Guard { .. } => "",
        }
    }

    pub fn name_span(&self) -> &Span {
        match self {
            ForBinder::Eq { span, .. }
            | ForBinder::Draw { span, .. }
            | ForBinder::Guard { span, .. } => span,
        }
    }

    pub fn value(&self) -> &Expr {
        match self {
            ForBinder::Eq { value, .. } | ForBinder::Draw { value, .. } => value,
            ForBinder::Guard { pred, .. } => pred,
        }
    }

    /// Unpack pattern for `(a, b) = e` / `Opt.Some(n) <- e`.
    pub fn unpack_pat(&self) -> Option<&Pattern> {
        match self {
            ForBinder::Eq { pat, .. } | ForBinder::Draw { pat, .. } => pat.as_ref(),
            ForBinder::Guard { .. } => None,
        }
    }
}

/// Name, `_`, tuple, constructor, list, as, or named-field pattern for a binder / lambda.
/// Nested patterns are allowed. Nested literals are allowed (`Opt.Some(0)`).
pub fn is_tuple_binder_pat(p: &Pattern) -> bool {
    is_unpack_binder_pat(p)
}

/// Same as [`is_tuple_binder_pat`]. Nested literals are allowed.
pub fn is_unpack_binder_pat(p: &Pattern) -> bool {
    match p {
        Pattern::Wildcard | Pattern::Bind(_) => true,
        Pattern::Int(_) | Pattern::Float(_) | Pattern::Bool(_) | Pattern::Str(_) | Pattern::Nil => {
            true
        }
        Pattern::Tuple { elems, .. } => elems.iter().all(is_unpack_binder_pat),
        Pattern::Adt { binds, .. } => binds.iter().all(is_unpack_binder_pat),
        Pattern::Cons { head, tail, .. } => {
            is_unpack_binder_pat(head) && is_unpack_binder_pat(tail)
        }
        Pattern::As { inner, .. } | Pattern::Named { inner, .. } => is_unpack_binder_pat(inner),
        Pattern::Or(alts) => !alts.is_empty() && alts.iter().all(is_unpack_binder_pat),
    }
}

/// Top-level `for` binder. A lone literal is not a binder (`0 = 1`).
pub fn is_for_binder_pat(p: &Pattern) -> bool {
    !matches!(
        p,
        Pattern::Int(_) | Pattern::Float(_) | Pattern::Bool(_) | Pattern::Str(_)
    ) && is_unpack_binder_pat(p)
}

/// Tuple pattern with Opaque slot types (parser / lambda unpack).
pub fn opaque_tuple_pat(elems: Vec<Pattern>) -> Pattern {
    let n = elems.len();
    Pattern::Tuple {
        elems,
        tys: vec![Type::Opaque("Elem".into()); n],
    }
}

/// Simple `x` / `_` binder. `None` for a tuple unpack.
pub fn simple_binder_name(p: &Pattern) -> Option<&str> {
    match p {
        Pattern::Wildcard => Some("_"),
        Pattern::Bind(n) => Some(n.as_str()),
        _ => None,
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MatchArm {
    pub pattern: Pattern,
    /// `case Pat if pred =>` — `pred` is Bool. `None` when there is no guard.
    pub guard: Option<Expr>,
    pub body: Expr,
    /// Binder / lambda unpack. Skip exhaustiveness. A miss panics.
    pub unpack: bool,
}

impl MatchArm {
    pub fn new(pattern: Pattern, body: Expr) -> Self {
        Self {
            pattern,
            guard: None,
            body,
            unpack: false,
        }
    }

    pub fn unpack(pattern: Pattern, body: Expr) -> Self {
        Self {
            pattern,
            guard: None,
            body,
            unpack: true,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pattern {
    /// `Color.Red` / `Opt.Some(n)` / `Pair.Pair(x, y)` / `Opt.Some(Color.Red)` —
    /// `binds` are nested payload patterns (empty = nullary).
    Adt {
        enum_name: String,
        case_name: String,
        binds: Vec<Pattern>,
        /// Instantiation args for generic enums, filled by elaboration; empty otherwise.
        type_args: Vec<Type>,
    },
    /// `_`
    Wildcard,
    /// Payload name bind (`n` in `case Opt.Some(n)`), or a top-level catch-all bind.
    Bind(String),
    /// `case 0` / `case -1`
    Int(i64),
    /// `case 1.5`
    Float(u64),
    /// `case true` / `case false`
    Bool(bool),
    /// `case "ok"`
    Str(String),
    /// `Color.Red | Color.Blue` / nested `Opt.Some(0 | 1)`.
    /// Lower expands this into separate arms.
    Or(Vec<Pattern>),
    /// `n @ Pat` — bind `n` to the value at this position, then match `Pat`.
    As { name: String, inner: Box<Pattern> },
    /// `[]`
    Nil,
    /// `h :: t`. `[a, b]` sugar folds to Cons ending in Nil.
    Cons {
        head: Box<Pattern>,
        tail: Box<Pattern>,
        /// Element type. Parser leaves `Opaque("Elem")`. Elaborate fills it.
        elem: Type,
    },
    /// `(a, b)` / `(a, b, c)`. Parser leaves types Opaque. Elaborate fills them from the scrutinee.
    Tuple { elems: Vec<Pattern>, tys: Vec<Type> },
    /// `x = pat` in an ADT payload. Typecheck rewrites to positional.
    Named { name: String, inner: Box<Pattern> },
}

/// Rewrite named ADT payload binds to positional field order.
/// Omitted fields become `_`. When no bind is named, return `binds` unchanged.
pub fn rewrite_named_payload(
    ctor: &str,
    binds: Vec<Pattern>,
    field_names: &[String],
) -> Result<Vec<Pattern>, String> {
    let mut positional = Vec::new();
    let mut named: Vec<(String, Pattern)> = Vec::new();
    let mut seen_named = false;
    for b in binds {
        match b {
            Pattern::Named { name, inner } => {
                seen_named = true;
                named.push((name, *inner));
            }
            _ if seen_named => {
                return Err(format!(
                    "{ctor}: positional pattern follows named field pattern"
                ));
            }
            other => positional.push(other),
        }
    }
    if named.is_empty() {
        return Ok(positional);
    }
    if field_names.is_empty() {
        return Err(format!("{ctor} is nullary; remove named field pattern"));
    }
    if positional.len() + named.len() > field_names.len() {
        return Err(format!(
            "{ctor} expects {} binder(s), got {}",
            field_names.len(),
            positional.len() + named.len()
        ));
    }
    let mut used = std::collections::HashSet::new();
    for name in field_names.iter().take(positional.len()) {
        used.insert(name.clone());
    }
    let mut by_name = std::collections::HashMap::new();
    for (name, pat) in named {
        if !field_names.iter().any(|f| f == &name) {
            return Err(format!("{ctor} has no field `{name}`"));
        }
        if !used.insert(name.clone()) {
            return Err(format!("{ctor}: duplicate field `{name}`"));
        }
        by_name.insert(name, pat);
    }
    let mut out = Vec::with_capacity(field_names.len());
    for (i, fname) in field_names.iter().enumerate() {
        if i < positional.len() {
            out.push(positional[i].clone());
        } else if let Some(p) = by_name.remove(fname) {
            out.push(p);
        } else {
            out.push(Pattern::Wildcard);
        }
    }
    Ok(out)
}

impl Pattern {
    /// `_` or a name bind — matches any value at this position.
    /// An or-pattern is irrefutable when any alternative is.
    pub fn is_irrefutable(&self) -> bool {
        match self {
            Pattern::Wildcard | Pattern::Bind(_) => true,
            Pattern::As { inner, .. } | Pattern::Named { inner, .. } => inner.is_irrefutable(),
            Pattern::Tuple { elems, .. } => elems.iter().all(|e| e.is_irrefutable()),
            Pattern::Or(alts) => alts.iter().any(|a| a.is_irrefutable()),
            _ => false,
        }
    }

    /// Drop `n @` wrappers. Exhaustiveness uses the inner pattern.
    pub fn strip_as(&self) -> Pattern {
        match self {
            Pattern::As { inner, .. } => inner.strip_as(),
            Pattern::Adt {
                enum_name,
                case_name,
                binds,
                type_args,
            } => Pattern::Adt {
                enum_name: enum_name.clone(),
                case_name: case_name.clone(),
                binds: binds.iter().map(|b| b.strip_as()).collect(),
                type_args: type_args.clone(),
            },
            Pattern::Or(alts) => Pattern::Or(alts.iter().map(|a| a.strip_as()).collect()),
            Pattern::Cons { head, tail, elem } => Pattern::Cons {
                head: Box::new(head.strip_as()),
                tail: Box::new(tail.strip_as()),
                elem: elem.clone(),
            },
            Pattern::Tuple { elems, tys } => Pattern::Tuple {
                elems: elems.iter().map(|e| e.strip_as()).collect(),
                tys: tys.clone(),
            },
            Pattern::Named { name, inner } => Pattern::Named {
                name: name.clone(),
                inner: Box::new(inner.strip_as()),
            },
            other => other.clone(),
        }
    }

    /// Expand `A | B` and nested or in payloads into or-free patterns.
    /// Nested payload or uses a cartesian product (`Some(0 | 1)` → `Some(0)`, `Some(1)`).
    pub fn flatten_or(&self) -> Vec<Pattern> {
        match self {
            Pattern::Or(alts) => alts.iter().flat_map(|a| a.flatten_or()).collect(),
            Pattern::As { name, inner } => inner
                .flatten_or()
                .into_iter()
                .map(|p| Pattern::As {
                    name: name.clone(),
                    inner: Box::new(p),
                })
                .collect(),
            Pattern::Adt {
                enum_name,
                case_name,
                binds,
                type_args,
            } => {
                let parts: Vec<Vec<Pattern>> = binds.iter().map(|b| b.flatten_or()).collect();
                cartesian_patterns(&parts)
                    .into_iter()
                    .map(|binds| Pattern::Adt {
                        enum_name: enum_name.clone(),
                        case_name: case_name.clone(),
                        binds,
                        type_args: type_args.clone(),
                    })
                    .collect()
            }
            Pattern::Cons { head, tail, elem } => {
                let parts = [head.flatten_or(), tail.flatten_or()];
                cartesian_patterns(&parts)
                    .into_iter()
                    .map(|mut row| {
                        let t = row.pop().unwrap();
                        let h = row.pop().unwrap();
                        Pattern::Cons {
                            head: Box::new(h),
                            tail: Box::new(t),
                            elem: elem.clone(),
                        }
                    })
                    .collect()
            }
            Pattern::Tuple { elems, tys } => {
                let parts: Vec<Vec<Pattern>> = elems.iter().map(|e| e.flatten_or()).collect();
                cartesian_patterns(&parts)
                    .into_iter()
                    .map(|elems| Pattern::Tuple {
                        elems,
                        tys: tys.clone(),
                    })
                    .collect()
            }
            Pattern::Named { name, inner } => inner
                .flatten_or()
                .into_iter()
                .map(|p| Pattern::Named {
                    name: name.clone(),
                    inner: Box::new(p),
                })
                .collect(),
            other => vec![other.clone()],
        }
    }
}

fn cartesian_patterns(parts: &[Vec<Pattern>]) -> Vec<Vec<Pattern>> {
    parts.iter().fold(vec![vec![]], |acc, part| {
        let mut next = Vec::with_capacity(acc.len().saturating_mul(part.len().max(1)));
        for prefix in &acc {
            for p in part {
                let mut row = prefix.clone();
                row.push(p.clone());
                next.push(row);
            }
        }
        next
    })
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Type {
    Unit,
    Int,
    Float,
    String,
    Bool,
    /// Homogeneous cons list. Runtime is untyped pointers; the argument is a check-time element type.
    List(Box<Type>),
    /// Tuple `(A, B)` / `(A, B, C)`. Runtime is right-nested `SzPair`.
    Tuple(Vec<Type>),
    /// Single-parameter function type (`T => U`) for kit lambdas.
    Fun(Box<Type>, Box<Type>),
    Io(Box<Type>),
    /// Nominal enum type
    Adt(String),
    /// Applied generic enum (`Opt[Int]`, `Either[L, R]`) — eliminated by monomorphization.
    App(String, Vec<Type>),
    /// Type parameter (`T` in `def id[T](x: T): T`)
    Var(String),
    /// Untyped/opaque
    Opaque(String),
}

impl std::fmt::Display for Type {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Type::Unit => write!(f, "Unit"),
            Type::Int => write!(f, "Int"),
            Type::Float => write!(f, "Float"),
            Type::String => write!(f, "String"),
            Type::Bool => write!(f, "Bool"),
            Type::List(t) => write!(f, "List[{t}]"),
            Type::Tuple(xs) => {
                let inner: Vec<String> = xs.iter().map(|t| t.to_string()).collect();
                write!(f, "({})", inner.join(", "))
            }
            Type::Fun(a, b) => write!(f, "{a} => {b}"),
            Type::Io(t) => write!(f, "IO[{t}]"),
            Type::Adt(n) | Type::Var(n) | Type::Opaque(n) => write!(f, "{n}"),
            Type::App(n, args) => {
                let inner: Vec<String> = args.iter().map(|a| a.to_string()).collect();
                write!(f, "{n}[{}]", inner.join(", "))
            }
        }
    }
}

/// Source form for a `Float` literal. Always includes a decimal point.
pub fn format_float_bits(bits: u64) -> String {
    let x = f64::from_bits(bits);
    if !x.is_finite() {
        return format!("{x}");
    }
    let s = format!("{x}");
    if s.contains('.') || s.contains('e') || s.contains('E') {
        s
    } else {
        format!("{s}.0")
    }
}

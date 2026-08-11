//! Typed-enough AST for the Stage-0 kernel dialect.

use crate::span::Span;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dotted package path, e.g. `["scuzz", "compiler"]`.
    pub package: Vec<String>,
    pub enums: Vec<EnumDef>,
    pub defs: Vec<FunDef>,
    pub main: MainDef,
    /// Law def names residualized under TestRuntime (empty for live builds).
    pub law_names: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumDef {
    pub name: String,
    pub cases: Vec<EnumCase>,
}

/// One `case Name` / `case Name(x: T)` in an enum.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumCase {
    pub name: String,
    /// Empty = nullary. Stage 0 codegen supports at most one field (`Int` or `String`).
    pub fields: Vec<(String, Type)>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunDef {
    pub name: String,
    pub params: Vec<Param>,
    pub ret: Type,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Param {
    pub name: String,
    pub ty: Type,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MainDef {
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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExprKind {
    /// `IO.println(expr)`
    IoPrintln(Box<Expr>),
    /// `IO.delay` — unit delay
    IoDelayUnit,
    /// `IO.sleep(expr)`
    IoSleep(Box<Expr>),
    /// `IO.fail(expr)`
    IoFail(Box<Expr>),
    /// `IO.pure(expr)`
    IoPure(Box<Expr>),
    /// `Effects.runKit()`
    EffectsRunKit,
    /// `left.flatMap(param => right)` — param None means `_`
    FlatMap {
        inner: Box<Expr>,
        param: Option<String>,
        body: Box<Expr>,
    },
    /// `io.handleErrorWith(_ => body)`
    HandleErrorWith {
        inner: Box<Expr>,
        body: Box<Expr>,
    },
    /// `io.attempt`
    Attempt {
        inner: Box<Expr>,
    },
    /// `IO.race(a, b)`
    IoRace {
        left: Box<Expr>,
        right: Box<Expr>,
    },
    /// `IO.both(a, b)`
    IoBoth {
        left: Box<Expr>,
        right: Box<Expr>,
    },
    /// `val name = value; body`
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
    /// `Color.Red` / `Opt.Some(x)` ADT case construct
    AdtConstruct {
        enum_name: String,
        case_name: String,
        args: Vec<Expr>,
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
    /// String literal
    StrLit(String),
    /// List literal `[a, b, c]`
    ListLit {
        elems: Vec<Expr>,
    },
    /// `s"...$x..."` / `s"...${expr}..."` — typed concat (Int holes via `Str.fromInt`).
    Interpolate {
        parts: Vec<InterpPart>,
    },
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
    /// Builtin or user call: `Str.concat(a,b)`, `foo(x)`, `Fs.read(p)`
    Call {
        callee: String,
        args: Vec<Expr>,
    },
    /// `_ => expr` or `x => expr` — single-param lambda literal (tap callbacks).
    Lambda {
        param: Option<String>,
        body: Box<Expr>,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum InterpPart {
    Lit(String),
    Expr(Expr),
}

/// Binder inside `for { … }`: `x = e` (pure) or `x <- e` (effect).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ForBinder {
    /// `name = value`
    Eq { name: String, value: Expr },
    /// `name <- value` (`name` may be `"_"`)
    Draw { name: String, value: Expr },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pattern {
    /// `Color.Red` / `Opt.Some(n)` — `bind` is the payload name when present
    Adt {
        enum_name: String,
        case_name: String,
        bind: Option<String>,
    },
    /// `_`
    Wildcard,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Type {
    Unit,
    Int,
    String,
    Bool,
    List,
    Io(Box<Type>),
    /// Nominal enum type
    Adt(String),
    /// Untyped/opaque
    Opaque(String),
}

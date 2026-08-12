//! Typed-enough AST for the Stage-0 kernel dialect.

use crate::span::Span;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dotted package path, e.g. `["scuzz", "compiler"]`.
    pub package: Vec<String>,
    pub enums: Vec<EnumDef>,
    pub traits: Vec<TraitDef>,
    pub impls: Vec<ImplDef>,
    pub defs: Vec<FunDef>,
    pub main: MainDef,
    /// `import Module.name` — bare `name` in `in_module` resolves to `from_module.name`.
    pub imports: Vec<Import>,
    /// Law def names residualized under TestRuntime (empty for live builds).
    pub law_names: Vec<String>,
}

/// `trait Show: def show(): String`
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TraitDef {
    pub module: String,
    pub name: String,
    pub methods: Vec<TraitMethod>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TraitMethod {
    pub name: String,
    pub params: Vec<Param>,
    pub ret: Type,
}

/// `impl Show for Point: def show(): String = …` (`self` is implicit in the body).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ImplDef {
    pub module: String,
    pub trait_name: String,
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

/// Top-level `import FromModule.name` in file-stem module `in_module`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Import {
    pub in_module: String,
    pub from_module: String,
    pub name: String,
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
}

/// One `case Name` / `case Name(x: T)` in an enum.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumCase {
    pub name: String,
    /// Empty = nullary. Multi-field payloads pack as `List` in `sz_adt_payload`.
    pub fields: Vec<(String, Type)>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FunDef {
    /// File-stem module id (`Foo.scuzz` → `Foo`). Empty when parsed without a path.
    pub module: String,
    pub name: String,
    /// `private def` — visible only within `module`. Default public.
    pub is_private: bool,
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
    Field {
        base: Box<Expr>,
        field: String,
    },
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
    /// `Color.Red` / `Opt.Some(n)` / `Pair.Pair(x, y)` — `binds` are payload names (empty = nullary)
    Adt {
        enum_name: String,
        case_name: String,
        binds: Vec<String>,
        /// Instantiation args for generic enums, filled by elaboration; empty otherwise.
        type_args: Vec<Type>,
    },
    /// `_`
    Wildcard,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Type {
    Unit,
    Int,
    String,
    Bool,
    List,
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

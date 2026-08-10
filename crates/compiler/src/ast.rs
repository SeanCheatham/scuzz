//! Typed-enough AST for the Stage-0 / Phase 4 kernel dialect.

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dotted package path, e.g. `["scalui", "compiler"]`.
    pub package: Vec<String>,
    pub enums: Vec<EnumDef>,
    pub defs: Vec<FunDef>,
    pub main: MainDef,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumDef {
    pub name: String,
    pub cases: Vec<String>,
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Expr {
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
    /// `Ui.runHeadless(expr)`
    UiRunHeadless(Box<Expr>),
    /// `Ui.runCounter`
    UiRunCounter,
    /// `Ui.runLive` — pump until quit when Window embedder is present
    UiRunLive,
    /// `Ui.runTodo`
    UiRunTodo,
    /// `Effects.runKit`
    EffectsRunKit,
    /// `Lexer.classify(expr)` → enum Tok
    LexerClassify(Box<Expr>),
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
    /// Local binding / parameter reference
    Var(String),
    /// `Color.Red` nullary ADT case
    AdtConstruct {
        enum_name: String,
        case_name: String,
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pattern {
    /// `Color.Red`
    Adt {
        enum_name: String,
        case_name: String,
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

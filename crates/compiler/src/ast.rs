//! Typed-enough AST for the Stage-0 / Phase 3 kernel dialect.

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    /// Dotted package path, e.g. `["scalui", "parser"]`.
    pub package: Vec<String>,
    pub enums: Vec<EnumDef>,
    pub main: MainDef,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EnumDef {
    pub name: String,
    pub cases: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MainDef {
    pub name: String,
    pub body: Expr,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Expr {
    /// `IO.println("...")`
    IoPrintln(String),
    /// `IO.delay` — unit delay
    IoDelayUnit,
    /// `IO.sleep(ms)`
    IoSleep(i64),
    /// `IO.fail("...")`
    IoFail(String),
    /// `Ui.runHeadless("...")`
    UiRunHeadless(String),
    /// `Ui.runCounter`
    UiRunCounter,
    /// `Ui.runTodo`
    UiRunTodo,
    /// `Effects.runKit` — blessed kit demo
    EffectsRunKit,
    /// `Lexer.classify("...")` → enum Tok (SuAdt*)
    LexerClassify(String),
    /// `left.flatMap(_ => right)`
    FlatMap {
        inner: Box<Expr>,
        body: Box<Expr>,
    },
    /// `io.handleErrorWith(_ => body)`
    HandleErrorWith {
        inner: Box<Expr>,
        body: Box<Expr>,
    },
    /// `io.attempt` → IO[Either]
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
    /// `val name = value; body` (semicolon / newline sequenced)
    Let {
        name: String,
        value: Box<Expr>,
        body: Box<Expr>,
    },
    /// Local binding reference
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
    Io(Box<Type>),
    /// Nominal enum type
    Adt(String),
    /// Untyped/opaque (Ref, etc. in later slices)
    Opaque(String),
}

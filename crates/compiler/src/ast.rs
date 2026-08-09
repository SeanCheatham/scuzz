//! Typed-enough AST for the Stage-0 kernel dialect.

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Program {
    pub main: MainDef,
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
    /// `IO.delay` — thunk is opaque in Phase 0 language; lowered as pure unit delay placeholder
    IoDelayUnit,
    /// `left.flatMap(_ => right)` or `left.flatMap(() => right)`
    FlatMap {
        inner: Box<Expr>,
        body: Box<Expr>,
    },
    /// `()`
    Unit,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Type {
    Unit,
    Io(Box<Type>),
}

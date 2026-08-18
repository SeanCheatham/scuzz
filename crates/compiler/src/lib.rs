//! Scuzz Lang compiler: parse kernel dialect → LLVM IR → native link.

pub(crate) mod ast;
pub(crate) mod check;
pub(crate) mod codegen;
pub(crate) mod complete;
pub(crate) mod definition;
pub mod driver;
pub(crate) mod fold;
pub mod format;
pub mod fuzz;
pub(crate) mod highlight;
pub(crate) mod hover;
pub(crate) mod inlay;
pub(crate) mod lexer;
pub(crate) mod lower;
pub(crate) mod lsp;
pub mod manifest;
pub mod mutate;
pub(crate) mod overlay;
pub(crate) mod parser;
pub(crate) mod references;
pub(crate) mod rename;
pub(crate) mod resolve;
pub(crate) mod select;
pub(crate) mod signature;
pub(crate) mod span;
pub(crate) mod symbols;
pub(crate) mod tokens;
pub(crate) mod typ;

pub use check::{check_project, format_diagnostics};
pub use driver::{compile_prepared_program, compile_project, CompileOutput};
pub use lsp::run_lsp;
pub use overlay::collect_fmt_sources;

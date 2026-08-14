//! Scuzz Lang compiler: parse kernel dialect → LLVM IR → native link.

pub mod ast;
pub mod check;
pub mod codegen;
pub mod driver;
pub mod format;
pub mod fuzz;
pub mod hover;
pub mod lexer;
pub mod lower;
pub mod lsp;
pub mod manifest;
pub mod mutate;
pub mod overlay;
pub mod parser;
pub mod resolve;
pub mod span;
pub mod typ;

pub use check::{check_project, check_project_with, format_diagnostics, hover_project, Diagnostic};
pub use driver::{
    compile_prepared_program, compile_project, load_verify_program, CompileOptions, CompileOutput,
};
pub use lsp::run_lsp;
pub use span::{line_col_to_offset, offset_to_line_col, Span};

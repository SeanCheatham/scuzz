//! ScalUI Stage-0 compiler: parse kernel dialect → LLVM IR → native link.

pub mod ast;
pub mod codegen;
pub mod driver;
pub mod format;
pub mod lexer;
pub mod lower;
pub mod manifest;
pub mod parser;
pub mod typ;

pub use driver::{compile_project, CompileOptions, CompileOutput};

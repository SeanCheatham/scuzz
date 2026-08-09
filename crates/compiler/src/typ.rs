use crate::ast::{Expr, Program, Type};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum TypeError {
    #[error("type error: {0}")]
    Msg(String),
}

/// Minimal structural check: every expr in @main is IO[Unit]-shaped.
pub fn typecheck(program: &Program) -> Result<(), TypeError> {
    let ty = infer(&program.main.body)?;
    match ty {
        Type::Io(inner) if matches!(*inner, Type::Unit) => Ok(()),
        other => Err(TypeError::Msg(format!(
            "@main body must be IO[Unit], got {other:?}"
        ))),
    }
}

fn infer(expr: &Expr) -> Result<Type, TypeError> {
    match expr {
        Expr::Unit => Ok(Type::Unit),
        Expr::IoPrintln(_)
        | Expr::IoDelayUnit
        | Expr::UiRunHeadless(_)
        | Expr::UiRunCounter
        | Expr::UiRunTodo => Ok(Type::Io(Box::new(Type::Unit))),
        Expr::FlatMap { inner, body } => {
            let it = infer(inner)?;
            if !matches!(it, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "flatMap receiver must be IO[_]".into(),
                ));
            }
            let bt = infer(body)?;
            if !matches!(bt, Type::Io(_)) {
                return Err(TypeError::Msg(
                    "flatMap body must return IO[_]".into(),
                ));
            }
            Ok(bt)
        }
    }
}

//! Minimal ScalUI formatter: parse → pretty-print (Phase 3 kernel dialect).

use crate::ast::{EnumDef, Expr, MatchArm, Pattern, Program};
use crate::parser::{parse, ParseError};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum FormatError {
    #[error(transparent)]
    Parse(#[from] ParseError),
}

/// Format source text by round-tripping through the parser.
pub fn format_source(source: &str) -> Result<String, FormatError> {
    let prog = parse(source)?;
    Ok(pretty_program(&prog))
}

fn pretty_program(p: &Program) -> String {
    let mut out = String::new();
    if !p.package.is_empty() {
        out.push_str("package ");
        out.push_str(&p.package.join("."));
        out.push_str("\n\n");
    }
    for e in &p.enums {
        out.push_str(&pretty_enum(e));
        out.push('\n');
    }
    if !p.main.name.is_empty() {
        out.push_str("@main def ");
        out.push_str(&p.main.name);
        out.push_str(": IO[Unit] =\n");
        out.push_str(&pretty_expr(&p.main.body, 1));
        out.push('\n');
    }
    out
}

fn pretty_enum(e: &EnumDef) -> String {
    let mut out = String::new();
    out.push_str("enum ");
    out.push_str(&e.name);
    out.push_str(":\n");
    for c in &e.cases {
        out.push_str("  case ");
        out.push_str(c);
        out.push('\n');
    }
    out
}

fn pretty_expr(expr: &Expr, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    match expr {
        Expr::Unit => format!("{pad}()"),
        Expr::IoPrintln(s) => format!("{pad}IO.println(\"{}\")", escape(s)),
        Expr::IoDelayUnit => format!("{pad}IO.delay(() => ())"),
        Expr::IoSleep(ms) => format!("{pad}IO.sleep({ms})"),
        Expr::IoFail(s) => format!("{pad}IO.fail(\"{}\")", escape(s)),
        Expr::UiRunHeadless(s) => format!("{pad}Ui.runHeadless(\"{}\")", escape(s)),
        Expr::UiRunCounter => format!("{pad}Ui.runCounter"),
        Expr::UiRunTodo => format!("{pad}Ui.runTodo"),
        Expr::EffectsRunKit => format!("{pad}Effects.runKit"),
        Expr::LexerClassify(s) => format!("{pad}Lexer.classify(\"{}\")", escape(s)),
        Expr::Var(n) => format!("{pad}{n}"),
        Expr::AdtConstruct {
            enum_name,
            case_name,
        } => format!("{pad}{enum_name}.{case_name}"),
        Expr::FlatMap { inner, body } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            let right = pretty_expr(body, indent + 1);
            // Keep simple one-line bodies compact.
            if !matches!(body.as_ref(), Expr::Let { .. } | Expr::Match { .. } | Expr::FlatMap { .. })
                && !right.contains('\n')
            {
                format!("{pad}{left}.flatMap(_ => {})", right.trim())
            } else {
                format!("{pad}{left}.flatMap(_ =>\n{right}\n{pad})")
            }
        }
        Expr::HandleErrorWith { inner, body } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            let right = pretty_expr(body, indent + 1);
            format!("{pad}{left}.handleErrorWith(_ =>\n{right}\n{pad})")
        }
        Expr::Attempt { inner } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            format!("{pad}{left}.attempt")
        }
        Expr::IoRace { left, right } => format!(
            "{pad}IO.race({}, {})",
            pretty_expr(left, 0).trim(),
            pretty_expr(right, 0).trim()
        ),
        Expr::IoBoth { left, right } => format!(
            "{pad}IO.both({}, {})",
            pretty_expr(left, 0).trim(),
            pretty_expr(right, 0).trim()
        ),
        Expr::Let { name, value, body } => {
            let v = pretty_expr(value, 0).trim().to_string();
            let b = pretty_expr(body, indent);
            format!("{pad}val {name} = {v}\n{b}")
        }
        Expr::Match { scrutinee, arms } => {
            let s = pretty_expr(scrutinee, 0).trim().to_string();
            let mut out = format!("{pad}{s} match {{\n");
            for arm in arms {
                out.push_str(&pretty_arm(arm, indent + 1));
                out.push('\n');
            }
            out.push_str(&pad);
            out.push('}');
            out
        }
    }
}

fn pretty_arm(arm: &MatchArm, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    let pat = match &arm.pattern {
        Pattern::Wildcard => "_".into(),
        Pattern::Adt {
            enum_name,
            case_name,
        } => format!("{enum_name}.{case_name}"),
    };
    let body = pretty_expr(&arm.body, 0).trim().to_string();
    format!("{pad}case {pat} => {body}")
}

fn escape(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_hello() {
        let src = r#"@main def main: IO[Unit] = IO.println("Hi")"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("@main def main: IO[Unit] ="));
        assert!(out.contains("IO.println(\"Hi\")"));
        assert!(out.ends_with('\n'));
    }

    #[test]
    fn formats_enum_match() {
        let src = r#"
package demo.color
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  val c = Color.Red
  c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("package demo.color"));
        assert!(out.contains("val c = Color.Red"));
        assert!(out.contains("case Color.Red =>"));
        // Round-trip
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }
}

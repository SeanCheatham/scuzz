//! Minimal Scuzz Lang formatter: parse → pretty-print (kernel dialect).

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, ForBinder, FunDef, MatchArm, Pattern, Program, Type};
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
    for im in &p.imports {
        out.push_str("import ");
        out.push_str(&im.from_module);
        out.push('.');
        out.push_str(&im.name);
        out.push('\n');
    }
    if !p.imports.is_empty() && (!p.defs.is_empty() || !p.main.name.is_empty()) {
        out.push('\n');
    }
    for d in &p.defs {
        out.push_str(&pretty_def(d));
        out.push_str("\n\n");
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
        out.push_str(&c.name);
        if !c.fields.is_empty() {
            let parts: Vec<String> = c
                .fields
                .iter()
                .map(|(n, t)| format!("{n}: {}", pretty_type(t)))
                .collect();
            out.push('(');
            out.push_str(&parts.join(", "));
            out.push(')');
        }
        out.push('\n');
    }
    out
}

fn pretty_type(t: &Type) -> String {
    match t {
        Type::Unit => "Unit".into(),
        Type::Int => "Int".into(),
        Type::String => "String".into(),
        Type::Bool => "Bool".into(),
        Type::List => "List".into(),
        Type::Io(inner) => format!("IO[{}]", pretty_type(inner)),
        Type::Adt(n) | Type::Opaque(n) => n.clone(),
    }
}

fn pretty_def(d: &FunDef) -> String {
    let params: Vec<String> = d
        .params
        .iter()
        .map(|p| format!("{}: {}", p.name, pretty_type(&p.ty)))
        .collect();
    let vis = if d.is_private { "private " } else { "" };
    format!(
        "{}def {}({}): {} =\n{}",
        vis,
        d.name,
        params.join(", "),
        pretty_type(&d.ret),
        pretty_expr(&d.body, 1)
    )
}

fn pretty_expr(expr: &Expr, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    match &expr.kind {
        ExprKind::Unit => format!("{pad}()"),
        ExprKind::IntLit(n) => format!("{pad}{n}"),
        ExprKind::StrLit(s) => format!("{pad}\"{}\"", escape(s)),
        ExprKind::ListLit { elems } => {
            let a: Vec<_> = elems
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!("{pad}[{}]", a.join(", "))
        }
        ExprKind::Interpolate { parts } => {
            let mut body = String::from("s\"");
            for part in parts {
                match part {
                    crate::ast::InterpPart::Lit(s) => body.push_str(&escape_interp_lit(s)),
                    crate::ast::InterpPart::Expr(e) => match &e.kind {
                        ExprKind::Var(n) => {
                            body.push('$');
                            body.push_str(n);
                        }
                        _ => {
                            body.push_str("${");
                            body.push_str(pretty_expr(e, 0).trim());
                            body.push('}');
                        }
                    },
                }
            }
            body.push('"');
            format!("{pad}{body}")
        }
        ExprKind::IoPrintln(e) => format!("{pad}IO.println({})", pretty_expr(e, 0).trim()),
        ExprKind::IoDelayUnit => format!("{pad}IO.delay(() => ())"),
        ExprKind::IoSleep(e) => format!("{pad}IO.sleep({})", pretty_expr(e, 0).trim()),
        ExprKind::IoFail(e) => format!("{pad}IO.fail({})", pretty_expr(e, 0).trim()),
        ExprKind::IoPure(e) => format!("{pad}IO.pure({})", pretty_expr(e, 0).trim()),
        ExprKind::Var(n) => format!("{pad}{n}"),
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
        } => {
            if args.is_empty() {
                format!("{pad}{enum_name}.{case_name}")
            } else {
                let a: Vec<_> = args
                    .iter()
                    .map(|e| pretty_expr(e, 0).trim().to_string())
                    .collect();
                format!("{pad}{enum_name}.{case_name}({})", a.join(", "))
            }
        },
        ExprKind::Lambda { param, body } => {
            let p = param.as_deref().unwrap_or("_");
            format!("{pad}{p} => {}", pretty_expr(body, 0).trim())
        }
        ExprKind::Call { callee, args } => {
            let a: Vec<_> = args
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!("{pad}{callee}({})", a.join(", "))
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => format!(
            "{pad}if ({}) {} else {}",
            pretty_expr(cond, 0).trim(),
            pretty_expr(then_branch, 0).trim(),
            pretty_expr(else_branch, 0).trim()
        ),
        ExprKind::Binary { op, left, right } => format!(
            "{pad}{} {} {}",
            pretty_expr(left, 0).trim(),
            binop_str(*op),
            pretty_expr(right, 0).trim()
        ),
        ExprKind::FlatMap { inner, param, body } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            let right = pretty_expr(body, indent + 1);
            let p = param.as_deref().unwrap_or("_");
            if !matches!(
                &body.kind,
                ExprKind::Let { .. } | ExprKind::Match { .. } | ExprKind::FlatMap { .. }
            ) && !right.contains('\n')
            {
                format!("{pad}{left}.flatMap({p} => {})", right.trim())
            } else {
                format!("{pad}{left}.flatMap({p} =>\n{right}\n{pad})")
            }
        }
        ExprKind::HandleErrorWith { inner, body } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            let right = pretty_expr(body, indent + 1);
            format!("{pad}{left}.handleErrorWith(_ =>\n{right}\n{pad})")
        }
        ExprKind::Attempt { inner } => {
            let left = pretty_expr(inner, 0).trim().to_string();
            format!("{pad}{left}.attempt")
        }
        ExprKind::IoRace { left, right } => format!(
            "{pad}IO.race({}, {})",
            pretty_expr(left, 0).trim(),
            pretty_expr(right, 0).trim()
        ),
        ExprKind::IoBoth { left, right } => format!(
            "{pad}IO.both({}, {})",
            pretty_expr(left, 0).trim(),
            pretty_expr(right, 0).trim()
        ),
        ExprKind::Let { name, value, body } => {
            // Core `Let` (post-lower): reprint as a one-binder `for`.
            pretty_expr(
                &Expr::dummy(ExprKind::For {
                    binders: vec![ForBinder::Eq {
                        name: name.clone(),
                        value: *value.clone(),
                    }],
                    body: body.clone(),
                }),
                indent,
            )
        }
        ExprKind::For { binders, body } => {
            let mut out = format!("{pad}for {{\n");
            let inner = "  ".repeat(indent + 1);
            for b in binders {
                match b {
                    ForBinder::Eq { name, value } => {
                        out.push_str(&inner);
                        out.push_str(name);
                        out.push_str(" = ");
                        out.push_str(pretty_expr(value, 0).trim());
                        out.push('\n');
                    }
                    ForBinder::Draw { name, value } => {
                        out.push_str(&inner);
                        out.push_str(name);
                        out.push_str(" <- ");
                        out.push_str(pretty_expr(value, 0).trim());
                        out.push('\n');
                    }
                }
            }
            out.push_str(&pad);
            out.push_str("} yield ");
            out.push_str(pretty_expr(body, 0).trim());
            out
        }
        ExprKind::Match { scrutinee, arms } => {
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

fn binop_str(op: BinOp) -> &'static str {
    match op {
        BinOp::Add => "+",
        BinOp::Sub => "-",
        BinOp::Mul => "*",
        BinOp::Div => "/",
        BinOp::Mod => "%",
        BinOp::Eq => "==",
        BinOp::Ne => "!=",
        BinOp::Lt => "<",
        BinOp::Le => "<=",
        BinOp::Gt => ">",
        BinOp::Ge => ">=",
        BinOp::And => "&&",
        BinOp::Or => "||",
    }
}

fn pretty_arm(arm: &MatchArm, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    let pat = match &arm.pattern {
        Pattern::Wildcard => "_".into(),
        Pattern::Adt {
            enum_name,
            case_name,
            binds,
        } => {
            if binds.is_empty() {
                format!("{enum_name}.{case_name}")
            } else {
                format!("{enum_name}.{case_name}({})", binds.join(", "))
            }
        }
    };
    let body = pretty_expr(&arm.body, 0).trim().to_string();
    format!("{pad}case {pat} => {body}")
}

fn escape(s: &str) -> String {
    let mut out = String::new();
    for ch in s.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            '\r' => out.push_str("\\r"),
            other => out.push(other),
        }
    }
    out
}

fn escape_interp_lit(s: &str) -> String {
    escape(s).replace('$', "\\$")
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
    fn formats_private_def() {
        let src = r#"
private def helper(): String = "x"
def tag(): String = helper()
@main def main: IO[Unit] = IO.println(tag())
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("private def helper(): String ="));
        assert!(out.contains("def tag(): String ="));
        assert!(!out.contains("private def tag"));
    }

    #[test]
    fn formats_enum_match() {
        let src = r#"
package demo.color
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  for {
    c = Color.Red
  } yield c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("package demo.color"));
        assert!(out.contains("c = Color.Red"));
        assert!(out.contains("case Color.Red =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_payload_enum() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Some(x: Int)"));
        assert!(out.contains("case None"));
        assert!(out.contains("Opt.Some(1)"));
        assert!(out.contains("case Opt.Some(n) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_multi_field_payload_enum() {
        let src = r#"
enum Pair:
  case Pair(a: Int, b: String)
@main def main: IO[Unit] =
  Pair.Pair(1, "x") match {
    case Pair.Pair(x, y) => IO.println(y)
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Pair(a: Int, b: String)"));
        assert!(out.contains("Pair.Pair(1, \"x\")"));
        assert!(out.contains("case Pair.Pair(x, y) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_for_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    _ <- Ui.run(count)
  } yield IO.pure(())
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("for {"));
        assert!(out.contains("count = Signal.int(0)"));
        assert!(out.contains("_ <- Ui.run(count)"));
        assert!(out.contains("yield IO.pure(())"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_ref_queue_deferred_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of("x")
    _ <- Ref.set(r, "ok")
    v <- Ref.get(r)
    q <- Queue.unbounded()
    _ <- Queue.offer(q, "a")
    t <- Queue.take(q)
    d <- Deferred.empty()
    _ <- Deferred.complete(d, "go")
    g <- Deferred.get(d)
    _ <- IO.println(v)
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Ref.of("));
        assert!(out.contains("Queue.unbounded()"));
        assert!(out.contains("Deferred.empty()"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_import_roundtrip() {
        let src = "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n";
        let out = format_source(src).unwrap();
        assert!(out.contains("import A.tag"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }
}

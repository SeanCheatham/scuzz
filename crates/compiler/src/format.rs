//! Minimal Scuzz Lang formatter: parse → pretty-print (kernel dialect).

use crate::ast::{
    BinOp, EnumDef, Expr, ExprKind, ForBinder, FunDef, ImplDef, MatchArm, Pattern, Program,
    TraitDef, Type,
};
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
        if e.methods.is_empty() {
            out.push_str(&pretty_enum(e));
            out.push('\n');
        }
    }
    for t in &p.traits {
        out.push_str(&pretty_trait(t));
        out.push('\n');
    }
    for im in &p.impls {
        out.push_str(&pretty_impl(im));
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
    for e in &p.enums {
        if !e.methods.is_empty() {
            out.push_str(&pretty_enum(e));
            out.push('\n');
        }
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
    let tparams = if e.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", e.type_params.join(", "))
    };
    if e.is_record {
        let c = &e.cases[0];
        let parts: Vec<String> = c
            .fields
            .iter()
            .enumerate()
            .map(|(i, (n, t))| pretty_binding(n, t, c.field_rfn(i)))
            .collect();
        let mut out = format!("record {}{tparams}({})", e.name, parts.join(", "));
        if e.methods.is_empty() {
            out.push('\n');
            return out;
        }
        out.push_str(":\n");
        for m in &e.methods {
            let params: Vec<String> = m
                .params
                .iter()
                .map(|p| pretty_binding(&p.name, &p.ty, p.rfn.as_ref()))
                .collect();
            out.push_str(&format!(
                "  def {}({}): {} =\n{}",
                m.name,
                params.join(", "),
                pretty_type(&m.ret),
                pretty_expr(&m.body, 2)
            ));
            out.push('\n');
        }
        return out;
    }
    let mut out = String::new();
    out.push_str("enum ");
    out.push_str(&e.name);
    out.push_str(&tparams);
    out.push_str(":\n");
    for c in &e.cases {
        out.push_str("  case ");
        out.push_str(&c.name);
        if !c.fields.is_empty() {
            let parts: Vec<String> = c
                .fields
                .iter()
                .enumerate()
                .map(|(i, (n, t))| pretty_binding(n, t, c.field_rfn(i)))
                .collect();
            out.push('(');
            out.push_str(&parts.join(", "));
            out.push(')');
        }
        out.push('\n');
    }
    for m in &e.methods {
        let params: Vec<String> = m
            .params
            .iter()
            .map(|p| pretty_binding(&p.name, &p.ty, p.rfn.as_ref()))
            .collect();
        out.push_str(&format!(
            "  def {}({}): {} =\n{}",
            m.name,
            params.join(", "),
            pretty_type(&m.ret),
            pretty_expr(&m.body, 2)
        ));
        out.push('\n');
    }
    out
}

fn pretty_trait(t: &TraitDef) -> String {
    let mut out = String::new();
    out.push_str("trait ");
    out.push_str(&t.name);
    if !t.type_params.is_empty() {
        out.push('[');
        out.push_str(&t.type_params.join(", "));
        out.push(']');
    }
    out.push_str(":\n");
    for m in &t.methods {
        let params: Vec<String> = m
            .params
            .iter()
            .map(|p| pretty_binding(&p.name, &p.ty, p.rfn.as_ref()))
            .collect();
        out.push_str(&format!(
            "  def {}({}): {}\n",
            m.name,
            params.join(", "),
            pretty_type(&m.ret)
        ));
    }
    out
}

fn pretty_impl(im: &ImplDef) -> String {
    let mut out = String::new();
    out.push_str("impl ");
    out.push_str(&im.trait_name);
    if !im.trait_args.is_empty() {
        out.push('[');
        out.push_str(
            &im.trait_args
                .iter()
                .map(pretty_type)
                .collect::<Vec<_>>()
                .join(", "),
        );
        out.push(']');
    }
    out.push_str(" for ");
    out.push_str(&im.for_type);
    out.push_str(":\n");
    for m in &im.methods {
        let params: Vec<String> = m
            .params
            .iter()
            .map(|p| pretty_binding(&p.name, &p.ty, p.rfn.as_ref()))
            .collect();
        out.push_str(&format!(
            "  def {}({}): {} =\n{}",
            m.name,
            params.join(", "),
            pretty_type(&m.ret),
            pretty_expr(&m.body, 2)
        ));
        out.push('\n');
    }
    out
}

fn pretty_binding(name: &str, ty: &Type, rfn: Option<&Expr>) -> String {
    match rfn {
        Some(e) => format!(
            "{}: {} where {}",
            name,
            pretty_type(ty),
            pretty_expr(e, 0).trim()
        ),
        None => format!("{}: {}", name, pretty_type(ty)),
    }
}

fn pretty_type(t: &Type) -> String {
    match t {
        Type::Unit => "Unit".into(),
        Type::Int => "Int".into(),
        Type::String => "String".into(),
        Type::Bool => "Bool".into(),
        Type::List => "List".into(),
        Type::Io(inner) => format!("IO[{}]", pretty_type(inner)),
        Type::App(n, args) => format!(
            "{}[{}]",
            n,
            args.iter().map(pretty_type).collect::<Vec<_>>().join(", ")
        ),
        Type::Adt(n) | Type::Opaque(n) | Type::Var(n) => n.clone(),
    }
}

fn pretty_def(d: &FunDef) -> String {
    if d.is_law {
        return format!(
            "law {}: {} =\n{}",
            d.name,
            pretty_type(&d.ret),
            pretty_expr(&d.body, 1)
        );
    }
    let params: Vec<String> = d
        .params
        .iter()
        .map(|p| pretty_binding(&p.name, &p.ty, p.rfn.as_ref()))
        .collect();
    let vis = if d.is_private { "private " } else { "" };
    let tparams = if d.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", d.type_params.join(", "))
    };
    format!(
        "{}def {}{}({}): {} =\n{}",
        vis,
        d.name,
        tparams,
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
        ExprKind::IoSleep(e) => format!("{pad}IO.sleep({})", pretty_expr(e, 0).trim()),
        ExprKind::IoFail(e) => format!("{pad}IO.fail({})", pretty_expr(e, 0).trim()),
        ExprKind::IoPure(e) => format!("{pad}IO.pure({})", pretty_expr(e, 0).trim()),
        ExprKind::Var(n) => format!("{pad}{n}"),
        ExprKind::Field { base, field } => {
            format!("{pad}{}.{}", pretty_expr(base, 0).trim(), field)
        }
        ExprKind::MethodCall {
            receiver,
            method,
            args,
        } => {
            let a: Vec<String> = args
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!(
                "{pad}{}.{}({})",
                pretty_expr(receiver, 0).trim(),
                method,
                a.join(", ")
            )
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            ..
        } => {
            let bare = crate::resolve::enum_bare_name(enum_name);
            if bare == case_name.as_str() {
                if args.is_empty() {
                    format!("{pad}{bare}")
                } else {
                    let a: Vec<_> = args
                        .iter()
                        .map(|e| pretty_expr(e, 0).trim().to_string())
                        .collect();
                    format!("{pad}{bare}({})", a.join(", "))
                }
            } else if args.is_empty() {
                format!("{pad}{bare}.{case_name}")
            } else {
                let a: Vec<_> = args
                    .iter()
                    .map(|e| pretty_expr(e, 0).trim().to_string())
                    .collect();
                format!("{pad}{bare}.{case_name}({})", a.join(", "))
            }
        }
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
        ExprKind::IoEnsure { inner, finalizer } => format!(
            "{pad}IO.ensure({}, {})",
            pretty_expr(inner, 0).trim(),
            pretty_expr(finalizer, 0).trim()
        ),
        ExprKind::IoTimeout { ms, inner } => format!(
            "{pad}IO.timeout({}, {})",
            pretty_expr(ms, 0).trim(),
            pretty_expr(inner, 0).trim()
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
            ..
        } => {
            let bare = crate::resolve::enum_bare_name(enum_name);
            if bare == case_name.as_str() {
                if binds.is_empty() {
                    bare.to_string()
                } else {
                    format!("{bare}({})", binds.join(", "))
                }
            } else if binds.is_empty() {
                format!("{bare}.{case_name}")
            } else {
                format!("{bare}.{case_name}({})", binds.join(", "))
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
    fn formats_law() {
        let src = r#"
law always: Bool = 1 == 1
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("law always: Bool ="));
        assert!(!out.contains("def always"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_where() {
        let src = r#"
def note(n: Int where n >= 0): Unit = ()
record Point(x: Int where x >= 0, y: Int)
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("n: Int where n >= 0"));
        assert!(out.contains("x: Int where x >= 0"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
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
        assert!(out.contains("case Pair(x, y) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_record_roundtrip() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  Point(1, 2) match {
    case Point(a, b) => IO.println(Str.fromInt(a))
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("record Point(x: Int, y: Int)"));
        assert!(out.contains("Point(1, 2)"));
        assert!(out.contains("case Point(a, b) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_generic_record_method_roundtrip() {
        let src = r#"
def wrap[T](x: T): Box[T] =
  Box(x)
record Box[T](x: T):
  def get(): T =
    self.x
@main def main: IO[Unit] =
  IO.println(Str.fromInt(wrap(4).get()))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("record Box[T](x: T):"));
        assert!(out.contains("def get(): T ="));
        assert!(out.contains("def wrap[T](x: T): Box[T] ="));
        assert!(out.find("def wrap").unwrap() < out.find("record Box").unwrap());
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
    f <- Fiber.fork(IO.pure("ok"))
    _ <- Fiber.join(f)
    _ <- Fiber.interrupt(f)
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Ref.of("));
        assert!(out.contains("Queue.unbounded()"));
        assert!(out.contains("Deferred.empty()"));
        assert!(out.contains("Fiber.fork("));
        assert!(out.contains("Fiber.join("));
        assert!(out.contains("Fiber.interrupt("));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_io_forever_repeat_retry_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n <- IO.repeatN(2, IO.pure("ok"))
    t <- IO.retryN(1, IO.pure("ok"))
    h <- Fiber.fork(IO.forever(IO.sleep(1)))
    _ <- Fiber.interrupt(h)
    _ <- IO.println(n)
    _ <- IO.println(t)
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("IO.repeatN("));
        assert!(out.contains("IO.retryN("));
        assert!(out.contains("IO.forever("));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_resource_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Resource.make("));
        assert!(out.contains("Resource.use("));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_stream_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.concat(Stream.emit("a"), Stream.eval(IO.pure("b")))
    xs <- Stream.compileToList(s)
    _ <- Stream.drain(Stream.evalMap(s, x => IO.println(x)))
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Stream.concat("));
        assert!(out.contains("Stream.compileToList("));
        assert!(out.contains("Stream.evalMap("));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_net_serve_once_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.pure(path))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Net.serveOnce("));
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

    #[test]
    fn formats_generic_enum_and_record() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
enum Either[L, R]:
  case Left(x: L)
  case Right(y: R)
record Box[T](x: T)
def getOrElse[T](o: Opt[T], default: T): T = o match {
  case Opt.Some(x) => x
  case Opt.None => default
}
@main def main: IO[Unit] = IO.println(Str.fromInt(getOrElse(Opt.Some(1), 0)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("enum Opt[T]:"), "missing enum tparams: {out}");
        assert!(
            out.contains("enum Either[L, R]:"),
            "missing Either tparams: {out}"
        );
        assert!(
            out.contains("record Box[T](x: T)"),
            "missing record tparams: {out}"
        );
        assert!(out.contains("case Some(x: T)"));
        assert!(out.contains("def getOrElse[T](o: Opt[T], default: T): T ="));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_generic_enum_method() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] = IO.println(Str.fromInt(Opt.Some(1).getOrElse(0)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("  def getOrElse(default: T): T ="));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_generic_trait() {
        let src = r#"
trait Get[T]:
  def getOrElse(default: T): T
@main def main: IO[Unit] = IO.println("x")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("trait Get[T]:"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_impl_trait_args() {
        let src = r#"
record Point(x: Int)
trait Get[T]:
  def getOrElse(default: T): T
impl Get[Int] for Point:
  def getOrElse(default: Int): Int = self.x
@main def main: IO[Unit] = IO.println("x")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("impl Get[Int] for Point:"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_impl_trait_args_on_generic() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
trait Get[T]:
  def getOrElse(default: T): T
impl Get[T] for Opt:
  def getOrElse(default: T): T =
    self match {
      case Opt.Some(x) => x
      case Opt.None => default
    }
@main def main: IO[Unit] = IO.println("x")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("impl Get[T] for Opt:"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }
}

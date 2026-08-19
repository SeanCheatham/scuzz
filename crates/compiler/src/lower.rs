//! Desugar `for` to `Let` / `FlatMap`. Resolve `Enum.Case` to `AdtConstruct`.

use crate::ast::{Expr, ExprKind, ForBinder, MatchArm, Pattern, Program};
use crate::resolve::{enum_id, split_dotted, EnumIndex};
use crate::span::Span;

/// Lower surface sugar in a program.
pub fn lower_program(mut program: Program) -> Program {
    let enums_snap = program.enums.clone();
    let enums = EnumIndex::build(&enums_snap, &program.imports)
        .expect("duplicate enums should be rejected at parse");
    for d in &mut program.defs {
        let module = d.module.clone();
        for p in &mut d.params {
            if let Some(rfn) = p.rfn.take() {
                p.rfn = Some(lower_expr(rfn, &enums, &module));
            }
            if let Some(dflt) = p.default.take() {
                p.default = Some(lower_expr(dflt, &enums, &module));
            }
        }
        d.body = lower_expr(
            std::mem::replace(&mut d.body, Expr::dummy(ExprKind::Unit)),
            &enums,
            &module,
        );
    }
    let main_mod = program.main.module.clone();
    program.main.body = lower_expr(
        std::mem::replace(&mut program.main.body, Expr::dummy(ExprKind::Unit)),
        &enums,
        &main_mod,
    );
    for en in &mut program.enums {
        let module = en.module.clone();
        for m in &mut en.methods {
            m.body = lower_expr(
                std::mem::replace(&mut m.body, Expr::dummy(ExprKind::Unit)),
                &enums,
                &module,
            );
        }
    }
    program
}

fn lower_pattern(pat: Pattern, enums: &EnumIndex<'_>, current_module: &str) -> Pattern {
    match pat {
        Pattern::Wildcard => Pattern::Wildcard,
        Pattern::Bind(name) => Pattern::Bind(name),
        Pattern::Int(n) => Pattern::Int(n),
        Pattern::Float(bits) => Pattern::Float(bits),
        Pattern::Bool(b) => Pattern::Bool(b),
        Pattern::Str(s) => Pattern::Str(s),
        Pattern::Or(alts) => Pattern::Or(
            alts.into_iter()
                .map(|a| lower_pattern(a, enums, current_module))
                .collect(),
        ),
        Pattern::As { name, inner } => Pattern::As {
            name,
            inner: Box::new(lower_pattern(*inner, enums, current_module)),
        },
        Pattern::Nil => Pattern::Nil,
        Pattern::Cons { head, tail, elem } => Pattern::Cons {
            head: Box::new(lower_pattern(*head, enums, current_module)),
            tail: Box::new(lower_pattern(*tail, enums, current_module)),
            elem,
        },
        Pattern::Named { name, inner } => Pattern::Named {
            name,
            inner: Box::new(lower_pattern(*inner, enums, current_module)),
        },
        Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => {
            let id =
                resolve_ctor(enums, &enum_name, &case_name, current_module).unwrap_or(enum_name);
            let binds: Vec<Pattern> = binds
                .into_iter()
                .map(|b| lower_pattern(b, enums, current_module))
                .collect();
            let binds = match enums
                .resolve(&id, current_module)
                .or_else(|_| enums.resolve(crate::resolve::enum_bare_name(&id), current_module))
            {
                Ok(e) => {
                    if let Some(case) = e.cases.iter().find(|c| c.name == case_name) {
                        let names: Vec<String> =
                            case.fields.iter().map(|(n, _)| n.clone()).collect();
                        let ctor = if e.name == case_name {
                            e.name.clone()
                        } else {
                            format!("{}.{}", e.name, case_name)
                        };
                        crate::ast::rewrite_named_payload(&ctor, binds.clone(), &names)
                            .unwrap_or(binds)
                    } else {
                        binds
                    }
                }
                Err(_) => binds,
            };
            Pattern::Adt {
                enum_name: id,
                case_name,
                binds,
                type_args,
            }
        }
    }
}

fn resolve_ctor(
    enums: &EnumIndex<'_>,
    enum_name: &str,
    case_name: &str,
    current_module: &str,
) -> Option<String> {
    let e = enums.resolve(enum_name, current_module).ok()?;
    if e.cases.iter().any(|c| c.name == case_name) {
        Some(enum_id(&e.module, &e.name))
    } else {
        None
    }
}

fn resolve_record_ctor(
    enums: &EnumIndex<'_>,
    name: &str,
    arity: usize,
    current_module: &str,
) -> Option<String> {
    let e = enums.resolve(name, current_module).ok()?;
    if !e.is_record {
        return None;
    }
    let c = e.cases.first()?;
    if c.name == e.name && c.fields.len() == arity {
        Some(enum_id(&e.module, &e.name))
    } else {
        None
    }
}

fn lower_expr(expr: Expr, enums: &EnumIndex<'_>, current_module: &str) -> Expr {
    let span = expr.span.clone();
    match expr.kind {
        ExprKind::For { binders, body } => {
            let body = lower_expr(*body, enums, current_module);
            desugar_for(binders, body, span, enums, current_module)
        }
        ExprKind::Call { callee, args } => {
            let args: Vec<Expr> = args
                .into_iter()
                .map(|e| lower_expr(e, enums, current_module))
                .collect();
            if let Some((enum_name, case_name)) = split_dotted(&callee) {
                if let Some(id) = resolve_ctor(enums, enum_name, case_name, current_module) {
                    return Expr::new(
                        ExprKind::AdtConstruct {
                            enum_name: id,
                            case_name: case_name.to_string(),
                            args,
                            type_args: Vec::new(),
                        },
                        span,
                    );
                }
            } else if let Some(id) = resolve_record_ctor(enums, &callee, args.len(), current_module)
            {
                return Expr::new(
                    ExprKind::AdtConstruct {
                        enum_name: id,
                        case_name: callee.clone(),
                        args,
                        type_args: Vec::new(),
                    },
                    span,
                );
            }
            Expr::new(ExprKind::Call { callee, args }, span)
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            type_args,
        } => {
            let args: Vec<Expr> = args
                .into_iter()
                .map(|e| lower_expr(e, enums, current_module))
                .collect();
            if let Some(id) = resolve_ctor(enums, &enum_name, &case_name, current_module) {
                return Expr::new(
                    ExprKind::AdtConstruct {
                        enum_name: id,
                        case_name,
                        args,
                        type_args,
                    },
                    span,
                );
            }
            // Nullary `Ident.Ident` is AdtConstruct at parse. If it is not an enum case,
            // treat it as a zero-arg module call (`A.tag`).
            if args.is_empty() {
                return Expr::new(
                    ExprKind::Call {
                        callee: format!("{enum_name}.{case_name}"),
                        args,
                    },
                    span,
                );
            }
            Expr::new(
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args,
                    type_args,
                },
                span,
            )
        }
        ExprKind::Match { scrutinee, arms } => {
            let mut out = Vec::new();
            for a in arms {
                let pattern = lower_pattern(a.pattern, enums, current_module);
                let guard = a.guard.map(|g| lower_expr(g, enums, current_module));
                let body = lower_expr(a.body, enums, current_module);
                for pat in pattern.flatten_or() {
                    out.push(MatchArm {
                        pattern: pat,
                        guard: guard.clone(),
                        body: body.clone(),
                    });
                }
            }
            Expr::new(
                ExprKind::Match {
                    scrutinee: Box::new(lower_expr(*scrutinee, enums, current_module)),
                    arms: out,
                },
                span,
            )
        }
        kind => Expr { kind, span }.map_children(|c| lower_expr(c, enums, current_module)),
    }
}

fn desugar_for(
    binders: Vec<ForBinder>,
    body: Expr,
    span: Span,
    enums: &EnumIndex<'_>,
    current_module: &str,
) -> Expr {
    let has_draw = binders.iter().any(|b| matches!(b, ForBinder::Draw { .. }));
    let body = if has_draw {
        // `<-` desugars like monadic `map` on the final yield: wrap pure results.
        // Effects belong in `<-` binders (`_ <- Ui.run(_ => root); yield ()`).
        Expr::new(ExprKind::IoPure(Box::new(body)), span.clone())
    } else {
        body
    };
    binders
        .into_iter()
        .rev()
        .fold(body, |body, binder| match binder {
            ForBinder::Eq { name, value, .. } => {
                let sp = value.span.clone().cover(&body.span);
                Expr::new(
                    ExprKind::Let {
                        name,
                        value: Box::new(lower_expr(value, enums, current_module)),
                        body: Box::new(body),
                    },
                    sp,
                )
            }
            ForBinder::Draw { name, value, .. } => {
                let param = if name == "_" { None } else { Some(name) };
                let sp = value.span.clone().cover(&body.span);
                Expr::new(
                    ExprKind::FlatMap {
                        inner: Box::new(lower_expr(value, enums, current_module)),
                        param,
                        body: Box::new(body),
                    },
                    sp,
                )
            }
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::ExprKind;
    use crate::parser::parse;

    #[test]
    fn lowers_eq_binders_to_lets() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    a = 1
    b = 2
  } yield IO.println("x")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Let { name, body, .. } => {
                assert_eq!(name, "a");
                assert!(matches!(&body.kind, ExprKind::Let { name, .. } if name == "b"));
            }
            other => panic!("expected nested Let, got {other:?}"),
        }
    }

    #[test]
    fn lowers_draw_to_flatmap() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    s <- Fs.read("x")
  } yield IO.println(s)
"#;
        let p = lower_program(parse(src).unwrap());
        assert!(matches!(
            &p.main.body.kind,
            ExprKind::FlatMap {
                param: Some(n),
                ..
            } if n == "s"
        ));
    }

    #[test]
    fn resolves_payload_ctor_call_to_adt() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] = Opt.Some(1) match {
  case Opt.Some(n) => IO.println("x")
  case Opt.None => IO.println("n")
}
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, .. } => match &scrutinee.kind {
                ExprKind::AdtConstruct {
                    enum_name,
                    case_name,
                    args,
                    ..
                } => {
                    assert_eq!(enum_name, "Opt");
                    assert_eq!(case_name, "Some");
                    assert_eq!(args.len(), 1);
                }
                other => panic!("expected AdtConstruct, got {other:?}"),
            },
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn leaves_color_rgb_as_call() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  for {
    c = Color.rgb(1, 2, 3)
  } yield IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        // Color.rgb is not an enum case → stays Call (builtin).
        fn find_call(e: &Expr) -> bool {
            match &e.kind {
                ExprKind::Call { callee, .. } if callee == "Color.rgb" => true,
                ExprKind::Let { value, body, .. } => find_call(value) || find_call(body),
                ExprKind::AdtConstruct { .. } => false,
                _ => false,
            }
        }
        assert!(find_call(&p.main.body));
    }

    #[test]
    fn leaves_color_rgba_as_call() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  for {
    c = Color.rgba(1, 2, 3, 4)
  } yield IO.println("x")
"#;
        let p = lower_program(parse(src).unwrap());
        fn find_call(e: &Expr) -> bool {
            match &e.kind {
                ExprKind::Call { callee, .. } if callee == "Color.rgba" => true,
                ExprKind::Let { value, body, .. } => find_call(value) || find_call(body),
                ExprKind::AdtConstruct { .. } => false,
                _ => false,
            }
        }
        assert!(find_call(&p.main.body));
    }

    #[test]
    fn qualifies_enum_id_when_module_present() {
        use crate::parser::parse_sources;
        let p = lower_program(
            parse_sources(&[
                (
                    "A.scuzz".into(),
                    "enum Msg:\n  case Hi\n  case Bye\n".into(),
                ),
                (
                    "Main.scuzz".into(),
                    "import A.Msg\n@main def main: IO[Unit] = Msg.Hi match {\n  case Msg.Hi => IO.println(\"h\")\n  case Msg.Bye => IO.println(\"b\")\n}\n".into(),
                ),
            ])
            .unwrap(),
        );
        match &p.main.body.kind {
            ExprKind::Match { scrutinee, .. } => match &scrutinee.kind {
                ExprKind::AdtConstruct { enum_name, .. } => {
                    assert_eq!(enum_name, "A.Msg");
                }
                other => panic!("expected AdtConstruct, got {other:?}"),
            },
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn expands_or_pattern_into_separate_arms() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert_eq!(arms.len(), 2, "{arms:?}");
                assert!(
                    matches!(
                        &arms[0].pattern,
                        Pattern::Adt { case_name, .. } if case_name == "Red"
                    ),
                    "{:?}",
                    arms[0].pattern
                );
                assert!(
                    matches!(
                        &arms[1].pattern,
                        Pattern::Adt { case_name, .. } if case_name == "Blue"
                    ),
                    "{:?}",
                    arms[1].pattern
                );
            }
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn expands_nested_or_payload_into_separate_arms() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(0) match {
    case Opt.Some(0 | 1) => IO.println("s")
    case Opt.Some(_) => IO.println("o")
    case Opt.None => IO.println("n")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert_eq!(arms.len(), 4, "{arms:?}");
                match &arms[0].pattern {
                    Pattern::Adt { binds, .. } => {
                        assert!(matches!(&binds[..], [Pattern::Int(0)]), "{binds:?}");
                    }
                    other => panic!("expected Some(0), got {other:?}"),
                }
                match &arms[1].pattern {
                    Pattern::Adt { binds, .. } => {
                        assert!(matches!(&binds[..], [Pattern::Int(1)]), "{binds:?}");
                    }
                    other => panic!("expected Some(1), got {other:?}"),
                }
            }
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn expands_as_or_pattern_keeping_the_bind() {
        let src = r#"
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  Color.Red match {
    case p @ Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => {
                assert_eq!(arms.len(), 2, "{arms:?}");
                for (arm, want) in arms.iter().zip(["Red", "Blue"]) {
                    match &arm.pattern {
                        Pattern::As { name, inner } => {
                            assert_eq!(name, "p");
                            assert!(
                                matches!(
                                    inner.as_ref(),
                                    Pattern::Adt { case_name, .. } if case_name == want
                                ),
                                "{inner:?}"
                            );
                        }
                        other => panic!("expected as-pattern, got {other:?}"),
                    }
                }
            }
            other => panic!("expected Match, got {other:?}"),
        }
    }

    #[test]
    fn rewrites_named_field_pattern_to_positional() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  Point(3, 5) match {
    case Point(x = n) => IO.println(Str.fromInt(n))
  }
"#;
        let p = lower_program(parse(src).unwrap());
        match &p.main.body.kind {
            ExprKind::Match { arms, .. } => match &arms[0].pattern {
                Pattern::Adt { binds, .. } => {
                    assert_eq!(binds.len(), 2, "{binds:?}");
                    assert!(matches!(&binds[0], Pattern::Bind(n) if n == "n"));
                    assert!(matches!(&binds[1], Pattern::Wildcard));
                }
                other => panic!("expected Point Adt, got {other:?}"),
            },
            other => panic!("expected Match, got {other:?}"),
        }
    }
}

//! Desugar `for` to `Let` / `FlatMap`. Resolve `Enum.Case` to `AdtConstruct`.

use crate::ast::{Expr, ExprKind, ForBinder, MatchArm, Pattern, Program};
use crate::resolve::{enum_id, EnumIndex};
use crate::span::Span;

/// Lower surface sugar in a program.
pub fn lower_program(mut program: Program) -> Program {
    let enums_snap = program.enums.clone();
    let enums = EnumIndex::build(&enums_snap, &program.imports)
        .expect("duplicate enums should be rejected at parse");
    for d in &mut program.defs {
        let module = d.module.clone();
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
        Pattern::Adt {
            enum_name,
            case_name,
            binds,
            type_args,
        } => {
            let id =
                resolve_ctor(enums, &enum_name, &case_name, current_module).unwrap_or(enum_name);
            Pattern::Adt {
                enum_name: id,
                case_name,
                binds: binds
                    .into_iter()
                    .map(|b| lower_pattern(b, enums, current_module))
                    .collect(),
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
                if let Some(id) = resolve_ctor(enums, &enum_name, &case_name, current_module) {
                    return Expr::new(
                        ExprKind::AdtConstruct {
                            enum_name: id,
                            case_name,
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
            // Nullary `Ident.Ident` is AdtConstruct at parse; if not an enum case, treat as
            // zero-arg module call (`A.tag`).
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
        ExprKind::Match { scrutinee, arms } => Expr::new(
            ExprKind::Match {
                scrutinee: Box::new(lower_expr(*scrutinee, enums, current_module)),
                arms: arms
                    .into_iter()
                    .map(|a| MatchArm {
                        pattern: lower_pattern(a.pattern, enums, current_module),
                        body: lower_expr(a.body, enums, current_module),
                    })
                    .collect(),
            },
            span,
        ),
        kind => Expr { kind, span }.map_children(|c| lower_expr(c, enums, current_module)),
    }
}

fn split_dotted(callee: &str) -> Option<(String, String)> {
    crate::resolve::split_dotted(callee).map(|(a, b)| (a.to_string(), b.to_string()))
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
        // Effects belong in `<-` binders (`_ <- Ui.run(root); yield ()`).
        Expr::new(ExprKind::IoPure(Box::new(body)), span.clone())
    } else {
        body
    };
    binders
        .into_iter()
        .rev()
        .fold(body, |body, binder| match binder {
            ForBinder::Eq { name, value } => {
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
            ForBinder::Draw { name, value } => {
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
}

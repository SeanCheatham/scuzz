//! Stem-paired `*.scuzz_intent` files. Closed STE/FRETish grammar. Re-parsed
//! on every check / fuzz compile into in-memory thunks. No generated `.scuzz`.

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, ForBinder, FunDef, Param, Program, Type};
use crate::overlay::{OverlayError, OverlayKind, OverlaySource};
use crate::resolve::enum_id;
use crate::span::Span;
use std::collections::HashMap;

/// Compiler module for intent thunks and forall drives.
pub const INTENT_MODULE: &str = "__intent";

#[derive(Debug, Clone, PartialEq, Eq)]
enum SessionClaim {
    Visible { needle: String, eventually: bool },
    Stays { signal: String, n: i64 },
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum ForClaim {
    Plus(i64),
    Minus(i64),
    Identity,
    Swap,
    ProductFields,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct ParsedIntent {
    session: Vec<SessionClaim>,
    forall: Vec<(String, ForClaim)>,
}

/// True when `module` is the compiler intent module.
pub fn is_intent_module(module: &str) -> bool {
    module == INTENT_MODULE
}

/// Parse and lower stem-paired intent files onto `program`. Missing files are
/// a no-op (not present in `overlays`). A present empty file fails.
pub fn apply_intents(
    program: &mut Program,
    overlays: &[OverlaySource],
) -> Result<(), OverlayError> {
    let mut always = Vec::new();
    let mut eventually = Vec::new();
    let mut thunk_i = 0usize;
    let signals = collect_signal_ids(program);
    for ov in overlays {
        if ov.kind != OverlayKind::Intent {
            continue;
        }
        let parsed = parse_intent(&ov.text, &ov.label)?;
        for claim in parsed.session {
            match claim {
                SessionClaim::Visible {
                    needle,
                    eventually: ev,
                } => {
                    let name = if ev {
                        format!("eventually_{thunk_i}")
                    } else {
                        format!("always_{thunk_i}")
                    };
                    thunk_i += 1;
                    let body = a11y_has(&needle, Span::dummy());
                    program.defs.push(bool_thunk(&name, body));
                    if ev {
                        eventually.push(name);
                    } else {
                        always.push(name);
                    }
                }
                SessionClaim::Stays { signal, n } => {
                    let Some((id, kind)) = signals.get(&signal) else {
                        return Err(OverlayError::Msg(format!(
                            "{}: unbound signal `{signal}`",
                            ov.label
                        )));
                    };
                    if *kind != SigKind::Int {
                        return Err(OverlayError::Msg(format!(
                            "{}: signal `{signal}` is not an Int signal",
                            ov.label
                        )));
                    }
                    let name = format!("always_{thunk_i}");
                    thunk_i += 1;
                    let body = stays_pred(*id, n, Span::dummy());
                    program.defs.push(bool_thunk(&name, body));
                    always.push(name);
                }
            }
        }
        for (def_name, claim) in parsed.forall {
            let live = find_live_def(program, &def_name, &ov.stem)
                .ok_or_else(|| {
                    OverlayError::Msg(format!("{}: unbound def `{def_name}`", ov.label))
                })?
                .clone();
            let driver = lower_for_claim(&ov.label, program, &live, &claim)?;
            if program
                .defs
                .iter()
                .any(|d| d.is_driver && d.name == driver.name)
            {
                return Err(OverlayError::Msg(format!(
                    "{}: For `{def_name}` collides with a drive name",
                    ov.label
                )));
            }
            program.defs.push(driver);
        }
    }
    program.intent_always = always;
    program.intent_eventually = eventually;
    Ok(())
}

fn parse_intent(text: &str, label: &str) -> Result<ParsedIntent, OverlayError> {
    let lines = intent_lines(text);
    if lines.is_empty() {
        return Err(OverlayError::Msg(format!(
            "{label}: intent file is empty; write a claim or delete the file"
        )));
    }
    let mut session = Vec::new();
    let mut forall = Vec::new();
    let mut i = 0;
    while i < lines.len() {
        let line = &lines[i];
        if let Some(def) = parse_for_header(line) {
            i += 1;
            if i >= lines.len() {
                return Err(OverlayError::Msg(format!(
                    "{label}: For `{def}` needs a claim sentence"
                )));
            }
            let claim = parse_for_claim(&lines[i]).ok_or_else(|| {
                OverlayError::Msg(format!("{label}: unknown intent form: {}", lines[i]))
            })?;
            forall.push((def, claim));
            i += 1;
            continue;
        }
        if let Some(claim) = parse_session_claim(line) {
            session.push(claim);
            i += 1;
            continue;
        }
        return Err(OverlayError::Msg(format!(
            "{label}: unknown intent form: {line}"
        )));
    }
    Ok(ParsedIntent { session, forall })
}

fn intent_lines(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    for raw in text.lines() {
        let stripped = strip_hash_comment(raw);
        let t = stripped.trim();
        if !t.is_empty() {
            out.push(t.to_string());
        }
    }
    out
}

fn strip_hash_comment(line: &str) -> String {
    match line.find('#') {
        Some(i) => line[..i].to_string(),
        None => line.to_string(),
    }
}

fn parse_for_header(line: &str) -> Option<String> {
    let t = line.trim();
    if !t.starts_with("For ") || !t.ends_with(':') {
        return None;
    }
    let name = t["For ".len()..t.len() - 1].trim();
    if is_ident(name) {
        Some(name.to_string())
    } else {
        None
    }
}

fn parse_session_claim(line: &str) -> Option<SessionClaim> {
    if let Some(needle) = strip_quoted(line, "The \"", "\" control is visible.") {
        return Some(SessionClaim::Visible {
            needle,
            eventually: false,
        });
    }
    if let Some(needle) = strip_quoted(line, "Eventually the \"", "\" control is visible.") {
        return Some(SessionClaim::Visible {
            needle,
            eventually: true,
        });
    }
    parse_stays(line)
}

fn parse_stays(line: &str) -> Option<SessionClaim> {
    let prefix = "The ";
    let mid = " stays at ";
    let suffix = " or more.";
    if !line.starts_with(prefix) || !line.ends_with(suffix) {
        return None;
    }
    let inner = &line[prefix.len()..line.len() - suffix.len()];
    let at = inner.find(mid)?;
    let signal = inner[..at].trim();
    let n = inner[at + mid.len()..].trim().parse::<i64>().ok()?;
    if !is_ident(signal) {
        return None;
    }
    Some(SessionClaim::Stays {
        signal: signal.to_string(),
        n,
    })
}

fn parse_for_claim(line: &str) -> Option<ForClaim> {
    if line == "The result is the input." {
        return Some(ForClaim::Identity);
    }
    if line == "Swapping the inputs does not change the result." {
        return Some(ForClaim::Swap);
    }
    if line == "The result is the product of the fields." {
        return Some(ForClaim::ProductFields);
    }
    if let Some(n) = strip_prefix_suffix(line, "The result is the input plus ", ".") {
        return n.parse().ok().map(ForClaim::Plus);
    }
    if let Some(n) = strip_prefix_suffix(line, "The result is the input minus ", ".") {
        return n.parse().ok().map(ForClaim::Minus);
    }
    None
}

fn strip_quoted(line: &str, prefix: &str, suffix: &str) -> Option<String> {
    if !line.starts_with(prefix) || !line.ends_with(suffix) {
        return None;
    }
    let inner = &line[prefix.len()..line.len() - suffix.len()];
    if inner.contains('"') {
        return None;
    }
    Some(inner.to_string())
}

fn strip_prefix_suffix<'a>(line: &'a str, prefix: &str, suffix: &str) -> Option<&'a str> {
    if !line.starts_with(prefix) || !line.ends_with(suffix) {
        return None;
    }
    Some(&line[prefix.len()..line.len() - suffix.len()])
}

fn is_ident(s: &str) -> bool {
    let mut chars = s.chars();
    match chars.next() {
        Some(c) if c.is_ascii_alphabetic() || c == '_' => {}
        _ => return false,
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SigKind {
    Int,
    Other,
}

fn collect_signal_ids(program: &Program) -> HashMap<String, (i64, SigKind)> {
    let mut map = HashMap::new();
    let mut next = 0i64;
    walk_signals(&program.main.body, &mut map, &mut next);
    map
}

fn walk_signals(e: &Expr, map: &mut HashMap<String, (i64, SigKind)>, next: &mut i64) {
    match &e.kind {
        ExprKind::Let { name, value, body } => {
            bind_signal(name, value, map, next);
            walk_signals(body, map, next);
        }
        ExprKind::For { binders, body } => {
            for b in binders {
                match b {
                    ForBinder::Eq { name, value, .. } | ForBinder::Draw { name, value, .. } => {
                        bind_signal(name, value, map, next);
                    }
                    ForBinder::Guard { pred, .. } => walk_signals(pred, map, next),
                }
            }
            walk_signals(body, map, next);
        }
        _ => {
            if take_signal_kind(e).is_some() {
                *next += 1;
            }
            e.for_each_child(|c| walk_signals(c, map, next));
        }
    }
}

fn bind_signal(
    name: &str,
    value: &Expr,
    map: &mut HashMap<String, (i64, SigKind)>,
    next: &mut i64,
) {
    if let Some(kind) = take_signal_kind(value) {
        if name != "_" {
            map.insert(name.to_string(), (*next, kind));
        }
        *next += 1;
        value.for_each_child(|c| walk_signals(c, map, next));
        return;
    }
    walk_signals(value, map, next);
}

fn take_signal_kind(e: &Expr) -> Option<SigKind> {
    match &e.kind {
        ExprKind::Call { callee, .. } => match callee.as_str() {
            "Signal.int" => Some(SigKind::Int),
            "Signal.str" | "Signal.list" | "Signal.map" => Some(SigKind::Other),
            _ => None,
        },
        _ => None,
    }
}

fn find_live_def<'a>(program: &'a Program, name: &str, stem: &str) -> Option<&'a FunDef> {
    program
        .defs
        .iter()
        .find(|d| {
            !d.is_driver && !is_intent_module(&d.module) && d.name == name && d.module == stem
        })
        .or_else(|| {
            let hits: Vec<&FunDef> = program
                .defs
                .iter()
                .filter(|d| !d.is_driver && !is_intent_module(&d.module) && d.name == name)
                .collect();
            if hits.len() == 1 {
                Some(hits[0])
            } else {
                None
            }
        })
}

fn lower_for_claim(
    label: &str,
    program: &Program,
    live: &FunDef,
    claim: &ForClaim,
) -> Result<FunDef, OverlayError> {
    let span = live.name_span.clone();
    if live.params.len() > 3 {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` takes at most three generator-friendly params",
            live.name
        )));
    }
    if live.is_private {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` cannot bind a private def",
            live.name
        )));
    }
    for p in &live.params {
        if !crate::overlay::type_is_generator_friendly(&p.ty, program, &live.module, 0) {
            return Err(OverlayError::Msg(format!(
                "{label}: For `{}` param `{}` must be Int, String, Bool, List, or a record/enum of those",
                live.name, p.name
            )));
        }
    }
    let pred = match claim {
        ForClaim::Plus(n) => arith_pred(live, BinOp::Add, *n, span.clone(), label)?,
        ForClaim::Minus(n) => arith_pred(live, BinOp::Sub, *n, span.clone(), label)?,
        ForClaim::Identity => identity_pred(live, span.clone(), label)?,
        ForClaim::Swap => swap_pred(live, span.clone(), label)?,
        ForClaim::ProductFields => product_pred(program, live, span.clone(), label)?,
    };
    Ok(forall_driver(&live.name, &live.params, pred, span))
}

fn unary_live<'a>(live: &'a FunDef, label: &str) -> Result<&'a FunDef, OverlayError> {
    if live.params.len() != 1 {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` needs one input",
            live.name
        )));
    }
    Ok(live)
}

fn arith_pred(
    live: &FunDef,
    op: BinOp,
    n: i64,
    span: Span,
    label: &str,
) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label)?;
    if !matches!(live.params[0].ty, Type::Int) || !matches!(live.ret, Type::Int) {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` plus/minus needs an Int input and Int result",
            live.name
        )));
    }
    let call = live_call(live, span.clone());
    let rhs = Expr::new(
        ExprKind::Binary {
            op,
            left: Box::new(Expr::new(
                ExprKind::Var(live.params[0].name.clone()),
                span.clone(),
            )),
            right: Box::new(Expr::new(ExprKind::IntLit(n), span.clone())),
        },
        span.clone(),
    );
    Ok(eq_expr(call, rhs, span))
}

fn identity_pred(live: &FunDef, span: Span, label: &str) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label)?;
    if live.params[0].ty != live.ret {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` identity needs the result to match the input type",
            live.name
        )));
    }
    let call = live_call(live, span.clone());
    let rhs = Expr::new(ExprKind::Var(live.params[0].name.clone()), span.clone());
    Ok(eq_expr(call, rhs, span))
}

fn swap_pred(live: &FunDef, span: Span, label: &str) -> Result<Expr, OverlayError> {
    if live.params.len() != 2 {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` swap needs two inputs",
            live.name
        )));
    }
    if live.params[0].ty != live.params[1].ty {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` swap needs two inputs of the same type",
            live.name
        )));
    }
    let left = live_call(live, span.clone());
    let right = Expr::new(
        ExprKind::Call {
            callee: format!("{}.{}", live.module, live.name),
            args: vec![
                Expr::new(ExprKind::Var(live.params[1].name.clone()), span.clone()),
                Expr::new(ExprKind::Var(live.params[0].name.clone()), span.clone()),
            ],
        },
        span.clone(),
    );
    Ok(eq_expr(left, right, span))
}

fn product_pred(
    program: &Program,
    live: &FunDef,
    span: Span,
    label: &str,
) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label)?;
    if !matches!(live.ret, Type::Int) {
        return Err(OverlayError::Msg(format!(
            "{label}: For `{}` product needs an Int result",
            live.name
        )));
    }
    let fields =
        fill_product_fields(program, &live.params[0].ty, &live.module).ok_or_else(|| {
            OverlayError::Msg(format!(
                "{label}: For `{}` product needs a record of Int fields",
                live.name
            ))
        })?;
    let base = Expr::new(ExprKind::Var(live.params[0].name.clone()), span.clone());
    let mut prod: Option<Expr> = None;
    for f in fields {
        let field = Expr::new(
            ExprKind::Field {
                base: Box::new(base.clone()),
                field: f,
            },
            span.clone(),
        );
        prod = Some(match prod {
            None => field,
            Some(left) => Expr::new(
                ExprKind::Binary {
                    op: BinOp::Mul,
                    left: Box::new(left),
                    right: Box::new(field),
                },
                span.clone(),
            ),
        });
    }
    let call = live_call(live, span.clone());
    Ok(eq_expr(call, prod.expect("non-empty fields"), span))
}

fn live_call(live: &FunDef, span: Span) -> Expr {
    Expr::new(
        ExprKind::Call {
            callee: format!("{}.{}", live.module, live.name),
            args: live
                .params
                .iter()
                .map(|p| Expr::new(ExprKind::Var(p.name.clone()), span.clone()))
                .collect(),
        },
        span,
    )
}

fn eq_expr(left: Expr, right: Expr, span: Span) -> Expr {
    Expr::new(
        ExprKind::Binary {
            op: BinOp::Eq,
            left: Box::new(left),
            right: Box::new(right),
        },
        span,
    )
}

fn a11y_has(needle: &str, span: Span) -> Expr {
    Expr::new(
        ExprKind::Call {
            callee: "Property.a11yHas".into(),
            args: vec![Expr::new(
                ExprKind::StrLit(needle.to_string()),
                span.clone(),
            )],
        },
        span,
    )
}

fn stays_pred(id: i64, n: i64, span: Span) -> Expr {
    Expr::new(
        ExprKind::Binary {
            op: BinOp::Ge,
            left: Box::new(Expr::new(
                ExprKind::Call {
                    callee: "Property.signalInt".into(),
                    args: vec![Expr::new(ExprKind::IntLit(id), span.clone())],
                },
                span.clone(),
            )),
            right: Box::new(Expr::new(ExprKind::IntLit(n), span.clone())),
        },
        span,
    )
}

fn bool_thunk(name: &str, body: Expr) -> FunDef {
    FunDef {
        module: INTENT_MODULE.into(),
        name: name.to_string(),
        name_span: Span::dummy(),
        is_private: false,
        is_driver: false,
        type_params: Vec::new(),
        params: Vec::new(),
        ret: Type::Bool,
        body,
    }
}

fn forall_driver(name: &str, params: &[Param], pred: Expr, span: Span) -> FunDef {
    let assert = Expr::new(
        ExprKind::Call {
            callee: "Property.assert".into(),
            args: vec![
                Expr::new(ExprKind::StrLit(name.to_string()), span.clone()),
                pred,
            ],
        },
        span.clone(),
    );
    FunDef {
        module: INTENT_MODULE.into(),
        name: name.to_string(),
        name_span: span,
        is_private: false,
        is_driver: true,
        type_params: Vec::new(),
        params: params.to_vec(),
        ret: Type::Io(Box::new(Type::Unit)),
        body: assert,
    }
}

fn fill_product_fields(program: &Program, ty: &Type, module: &str) -> Option<Vec<String>> {
    let id = match ty {
        Type::Adt(id) | Type::App(id, _) | Type::Opaque(id) => id.as_str(),
        _ => return None,
    };
    let en = lookup_enum(program, id, module)?;
    if !en.is_record {
        return None;
    }
    let case = en.cases.first()?;
    if case.fields.is_empty() || !case.fields.iter().all(|(_, t)| matches!(t, Type::Int)) {
        return None;
    }
    Some(case.fields.iter().map(|(n, _)| n.clone()).collect())
}

fn lookup_enum<'a>(program: &'a Program, id: &str, module: &str) -> Option<&'a EnumDef> {
    program
        .enums
        .iter()
        .find(|e| enum_id(&e.module, &e.name) == id)
        .or_else(|| {
            program
                .enums
                .iter()
                .find(|e| e.module == module && e.name == id)
        })
        .or_else(|| {
            let hits: Vec<&EnumDef> = program.enums.iter().filter(|e| e.name == id).collect();
            if hits.len() == 1 {
                Some(hits[0])
            } else {
                None
            }
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_sources;

    fn live(src: &str) -> Program {
        parse_sources(&[(
            "Main.scuzz".into(),
            format!("{src}\n@main def main: IO[Unit] = IO.println(\"x\")\n"),
        )])
        .unwrap()
    }

    fn intent_ov(text: &str) -> OverlaySource {
        OverlaySource {
            stem: "Main".into(),
            kind: OverlayKind::Intent,
            label: "Main.scuzz_intent".into(),
            text: text.into(),
            path: std::path::PathBuf::new(),
        }
    }

    #[test]
    fn missing_intent_is_noop() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        apply_intents(&mut p, &[]).unwrap();
        assert!(p.intent_always.is_empty());
        assert!(p.defs.iter().all(|d| d.module != INTENT_MODULE));
    }

    #[test]
    fn empty_intent_fails() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply_intents(&mut p, &[intent_ov("# just a comment\n\n")]).unwrap_err();
        assert!(err.to_string().contains("empty"), "{err}");
    }

    #[test]
    fn parses_visible_and_eventually() {
        let parsed = parse_intent(
            "The \"button:+1\" control is visible.\nEventually the \"text:done\" control is visible.\n",
            "Main.scuzz_intent",
        )
        .unwrap();
        assert_eq!(parsed.session.len(), 2);
        match &parsed.session[0] {
            SessionClaim::Visible { needle, eventually } => {
                assert_eq!(needle, "button:+1");
                assert!(!*eventually);
            }
            _ => panic!("expected visible"),
        }
        match &parsed.session[1] {
            SessionClaim::Visible { needle, eventually } => {
                assert_eq!(needle, "text:done");
                assert!(*eventually);
            }
            _ => panic!("expected eventually"),
        }
    }

    #[test]
    fn parses_stays_and_for_plus() {
        let parsed = parse_intent(
            "The count stays at 0 or more.\nFor bump:\nThe result is the input plus 1.\n",
            "Main.scuzz_intent",
        )
        .unwrap();
        assert_eq!(
            parsed.session[0],
            SessionClaim::Stays {
                signal: "count".into(),
                n: 0
            }
        );
        assert_eq!(parsed.forall[0].0, "bump");
        assert_eq!(parsed.forall[0].1, ForClaim::Plus(1));
    }

    #[test]
    fn parses_swap_and_product() {
        let parsed = parse_intent(
            "For add:\nSwapping the inputs does not change the result.\nFor area:\nThe result is the product of the fields.\n",
            "Main.scuzz_intent",
        )
        .unwrap();
        assert_eq!(parsed.forall[0].1, ForClaim::Swap);
        assert_eq!(parsed.forall[1].1, ForClaim::ProductFields);
    }

    #[test]
    fn unknown_form_fails() {
        let err = parse_intent("The button should show.\n", "Main.scuzz_intent").unwrap_err();
        assert!(err.to_string().contains("unknown intent form"), "{err}");
    }

    #[test]
    fn unbound_def_fails() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply_intents(
            &mut p,
            &[intent_ov("For missing:\nThe result is the input plus 1.\n")],
        )
        .unwrap_err();
        assert!(err.to_string().contains("unbound def"), "{err}");
    }

    #[test]
    fn lowers_for_plus_drive() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        apply_intents(
            &mut p,
            &[intent_ov("For bump:\nThe result is the input plus 1.\n")],
        )
        .unwrap();
        let d = p
            .defs
            .iter()
            .find(|d| d.is_driver && d.name == "bump")
            .unwrap();
        assert_eq!(d.module, INTENT_MODULE);
        assert!(matches!(d.ret, Type::Io(_)));
    }

    #[test]
    fn lowers_visible_always_thunk() {
        let mut p = live("");
        apply_intents(
            &mut p,
            &[intent_ov("The \"button:+1\" control is visible.\n")],
        )
        .unwrap();
        assert_eq!(p.intent_always.len(), 1);
        let d = p
            .defs
            .iter()
            .find(|d| d.module == INTENT_MODULE && d.name == p.intent_always[0])
            .unwrap();
        assert!(!d.is_driver);
        assert!(matches!(d.ret, Type::Bool));
    }

    #[test]
    fn unbound_signal_fails() {
        let mut p = live("");
        let err =
            apply_intents(&mut p, &[intent_ov("The count stays at 0 or more.\n")]).unwrap_err();
        assert!(err.to_string().contains("unbound signal"), "{err}");
    }

    #[test]
    fn binds_signal_from_main() {
        let mut p = parse_sources(&[(
            "Main.scuzz".into(),
            "\
@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    _ <- IO.println(\"x\")
  } yield ()
"
            .into(),
        )])
        .unwrap();
        apply_intents(&mut p, &[intent_ov("The count stays at 0 or more.\n")]).unwrap();
        assert_eq!(p.intent_always.len(), 1);
    }
}

//! Hierarchical `intent.scuzz_intent` files. Closed STE/FRETish grammar.
//! Re-parsed on every check / fuzz compile into in-memory thunks. No generated
//! `.scuzz`.

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, ForBinder, FunDef, Param, Program, Type};
use crate::overlay::OverlayError;
use crate::resolve::enum_id;
use crate::span::{offset_to_line_col, Span};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Compiler module for intent thunks and forall drives.
pub const INTENT_MODULE: &str = "__intent";

/// Exact intent file name at a package root or under `src/`.
pub const INTENT_FILE_NAME: &str = "intent.scuzz_intent";

/// True when `module` is the compiler intent module.
pub fn is_intent_module(module: &str) -> bool {
    module == INTENT_MODULE
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntentScopeKind {
    Package,
    Directory,
}

/// One discovered intent file.
#[derive(Debug, Clone)]
pub struct IntentSource {
    pub package: String,
    pub scope: IntentScopeKind,
    /// Directory relative to the package root (`src/billing`). Empty for package scope.
    pub dir_rel: String,
    pub label: String,
    pub text: String,
    pub path: PathBuf,
}

/// Live source ownership used to bind `Module.def` inside an intent scope.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SourceUnit {
    pub package: String,
    pub module: String,
    /// Path relative to the package root (`src/Main.scuzz`).
    pub rel: String,
}

impl SourceUnit {
    pub fn from_label(label: &str) -> Self {
        let (package, rel) = match label.split_once('/') {
            Some((pkg, rest)) => (pkg.to_string(), rest.to_string()),
            None => (String::new(), label.to_string()),
        };
        let module = std::path::Path::new(&rel)
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_string();
        Self {
            package,
            module,
            rel,
        }
    }
}

/// Placement of an `intent.scuzz_intent` file inside a package.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IntentPlacement {
    pub scope: IntentScopeKind,
    pub dir_rel: String,
    pub rel: String,
}

/// Classify `path` inside `pkg_dir`. `Ok(None)` means the file is not an intent file.
/// A stem-paired `*.scuzz_intent` name is an error.
pub fn place_intent_file(pkg_dir: &Path, path: &Path) -> Result<Option<IntentPlacement>, String> {
    let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
    if name.ends_with(".scuzz_intent") && name != INTENT_FILE_NAME {
        return Err(format!(
            "rename to {INTENT_FILE_NAME}; stem-paired intent files are not valid"
        ));
    }
    if name != INTENT_FILE_NAME {
        return Ok(None);
    }
    let rel = path
        .strip_prefix(pkg_dir)
        .map(|p| p.to_string_lossy().replace('\\', "/"))
        .map_err(|_| format!("{INTENT_FILE_NAME} must sit beside scuzz.toml or under src/"))?;
    if rel == INTENT_FILE_NAME {
        return Ok(Some(IntentPlacement {
            scope: IntentScopeKind::Package,
            dir_rel: String::new(),
            rel,
        }));
    }
    if let Some(dir) = rel.strip_suffix(&format!("/{INTENT_FILE_NAME}")) {
        if dir == "src" || dir.starts_with("src/") {
            return Ok(Some(IntentPlacement {
                scope: IntentScopeKind::Directory,
                dir_rel: dir.to_string(),
                rel,
            }));
        }
    }
    Err(format!(
        "{INTENT_FILE_NAME} must sit beside scuzz.toml or under src/"
    ))
}

pub fn intent_source_from_parts(
    pkg_name: &str,
    placement: IntentPlacement,
    path: PathBuf,
    text: String,
) -> IntentSource {
    let label = format!("{pkg_name}/{}", placement.rel);
    IntentSource {
        package: pkg_name.to_string(),
        scope: placement.scope,
        dir_rel: placement.dir_rel,
        label,
        text,
        path,
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum SessionClaim {
    Visible { needle: String, eventually: bool },
    Stays { signal: String, n: i64 },
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
enum ForClaim {
    Plus(i64),
    Minus(i64),
    Identity,
    Swap,
    ProductFields,
}

#[derive(Debug)]
struct ParsedIntent {
    session: Vec<(SessionClaim, Span)>,
    forall: Vec<(String, String, ForClaim, Span)>,
}

struct BoundFor {
    module: String,
    def: String,
    claim: ForClaim,
    span: Span,
    label: String,
    line: u32,
}

/// Parse and lower hierarchical intent files onto `program`. Missing files are
/// a no-op (not present in `intents`). A present empty file fails.
pub fn apply_intents(
    program: &mut Program,
    intents: &[IntentSource],
    units: &[SourceUnit],
) -> Result<(), OverlayError> {
    let mut always = Vec::new();
    let mut eventually = Vec::new();
    let mut thunk_i = 0usize;
    let signals = collect_signal_ids(program);
    let mut seen: HashMap<String, (String, Span)> = HashMap::new();
    let mut bound_for = Vec::new();
    for ov in intents {
        let parsed = parse_intent(&ov.text, &ov.label)?;
        if ov.scope != IntentScopeKind::Package && !parsed.session.is_empty() {
            let span = parsed.session[0].1.clone();
            return Err(at(
                span,
                format!(
                    "{}: session claims belong in the package intent file",
                    ov.label
                ),
            ));
        }
        for (claim, span) in parsed.session {
            let key = session_key(&claim);
            if let Some((prev, _)) = seen.get(&key) {
                return Err(at(
                    span,
                    format!("{}: duplicate claim; already stated at {prev}", ov.label),
                ));
            }
            seen.insert(key, (ov.label.clone(), span.clone()));
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
                    let body = a11y_has(&needle, span);
                    program.defs.push(bool_thunk(&name, body));
                    if ev {
                        eventually.push(name);
                    } else {
                        always.push(name);
                    }
                }
                SessionClaim::Stays { signal, n } => {
                    let Some((id, kind)) = signals.get(&signal) else {
                        return Err(at(span, format!("{}: unbound signal `{signal}`", ov.label)));
                    };
                    if *kind != SigKind::Int {
                        return Err(at(
                            span,
                            format!("{}: signal `{signal}` is not an Int signal", ov.label),
                        ));
                    }
                    let name = format!("always_{thunk_i}");
                    thunk_i += 1;
                    let body = stays_pred(*id, n, span);
                    program.defs.push(bool_thunk(&name, body));
                    always.push(name);
                }
            }
        }
        for (module, def_name, claim, span) in parsed.forall {
            let key = for_key(&module, &def_name, &claim);
            if let Some((prev, _)) = seen.get(&key) {
                return Err(at(
                    span,
                    format!("{}: duplicate claim; already stated at {prev}", ov.label),
                ));
            }
            seen.insert(key, (ov.label.clone(), span.clone()));
            find_live_def(program, &module, &def_name).ok_or_else(|| {
                at(
                    span.clone(),
                    format!("{}: unbound def `{module}.{def_name}`", ov.label),
                )
            })?;
            let unit = units
                .iter()
                .find(|u| u.module == module && (ov.package.is_empty() || u.package == ov.package));
            let Some(unit) = unit else {
                return Err(at(
                    span,
                    format!(
                        "{}: For `{module}.{def_name}` is outside this intent scope",
                        ov.label
                    ),
                ));
            };
            if !in_scope(ov, unit) {
                return Err(at(
                    span,
                    format!(
                        "{}: For `{module}.{def_name}` is outside this intent scope",
                        ov.label
                    ),
                ));
            }
            let (line, _) = offset_to_line_col(&ov.text, span.start);
            bound_for.push(BoundFor {
                module,
                def: def_name,
                claim,
                span,
                label: ov.label.clone(),
                line,
            });
        }
    }
    bound_for.sort_by(|a, b| {
        a.module
            .cmp(&b.module)
            .then(a.def.cmp(&b.def))
            .then(a.span.start.cmp(&b.span.start))
            .then(a.label.cmp(&b.label))
    });
    let mut groups: Vec<Vec<BoundFor>> = Vec::new();
    for item in bound_for {
        match groups.last_mut() {
            Some(g) if g[0].module == item.module && g[0].def == item.def => g.push(item),
            _ => groups.push(vec![item]),
        }
    }
    for group in groups {
        let first = &group[0];
        let live = find_live_def(program, &first.module, &first.def)
            .ok_or_else(|| {
                at(
                    first.span.clone(),
                    format!(
                        "{}: unbound def `{}.{}`",
                        first.label, first.module, first.def
                    ),
                )
            })?
            .clone();
        let mut asserts = Vec::new();
        for item in &group {
            let pred = lower_for_claim(&item.label, program, &live, &item.claim, &item.span)?;
            let name = format!("{} ({}:{})", live.name, item.label, item.line);
            asserts.push(assert_expr(&name, pred, item.span.clone()));
        }
        let span = live.name_span.clone();
        let body = seq_io(asserts, span.clone());
        let driver = forall_driver(&live.name, &live.params, body, span);
        if program
            .defs
            .iter()
            .any(|d| d.is_driver && d.name == driver.name)
        {
            return Err(OverlayError::Msg(format!(
                "{}: For `{}.{}` collides with a drive name",
                first.label, first.module, first.def
            )));
        }
        program.defs.push(driver);
    }
    program.intent_always = always;
    program.intent_eventually = eventually;
    Ok(())
}

fn in_scope(intent: &IntentSource, unit: &SourceUnit) -> bool {
    if !intent.package.is_empty() && unit.package != intent.package {
        return false;
    }
    match intent.scope {
        IntentScopeKind::Package => true,
        IntentScopeKind::Directory => {
            let prefix = format!("{}/", intent.dir_rel);
            unit.rel.starts_with(&prefix)
        }
    }
}

fn session_key(claim: &SessionClaim) -> String {
    match claim {
        SessionClaim::Visible { needle, eventually } => {
            format!("visible:{eventually}:{needle}")
        }
        SessionClaim::Stays { signal, n } => format!("stays:{signal}:{n}"),
    }
}

fn for_key(module: &str, def: &str, claim: &ForClaim) -> String {
    format!("for:{module}.{def}:{claim:?}")
}

fn at(span: Span, msg: String) -> OverlayError {
    OverlayError::At { msg, span }
}

fn parse_intent(text: &str, label: &str) -> Result<ParsedIntent, OverlayError> {
    let lines = intent_lines(text, label);
    if lines.is_empty() {
        return Err(at(
            Span::new(label, 0, 0),
            format!("{label}: intent file is empty; write a claim or delete the file"),
        ));
    }
    let mut session = Vec::new();
    let mut forall = Vec::new();
    let mut i = 0;
    while i < lines.len() {
        let (line, span) = &lines[i];
        if line.starts_with("For ") && line.ends_with(':') {
            match parse_for_header(line) {
                Some((module, def)) => {
                    i += 1;
                    if i >= lines.len() {
                        return Err(at(
                            span.clone(),
                            format!("{label}: For `{module}.{def}` needs a claim sentence"),
                        ));
                    }
                    let (claim_line, claim_span) = &lines[i];
                    let claim = parse_for_claim(claim_line).ok_or_else(|| {
                        at(
                            claim_span.clone(),
                            format!("{label}: unknown intent form: {claim_line}"),
                        )
                    })?;
                    forall.push((module, def, claim, claim_span.clone()));
                    i += 1;
                    continue;
                }
                None => {
                    return Err(at(
                        span.clone(),
                        format!("{label}: For claims must name Module.def"),
                    ));
                }
            }
        }
        if let Some(claim) = parse_session_claim(line) {
            session.push((claim, span.clone()));
            i += 1;
            continue;
        }
        return Err(at(
            span.clone(),
            format!("{label}: unknown intent form: {line}"),
        ));
    }
    Ok(ParsedIntent { session, forall })
}

fn intent_lines(text: &str, file: &str) -> Vec<(String, Span)> {
    let mut out = Vec::new();
    let mut offset = 0usize;
    for raw in text.split_inclusive('\n') {
        let line_start = offset;
        offset += raw.len();
        let without_nl = raw.strip_suffix('\n').unwrap_or(raw);
        let without_nl = without_nl.strip_suffix('\r').unwrap_or(without_nl);
        let stripped = match without_nl.find('#') {
            Some(i) => &without_nl[..i],
            None => without_nl,
        };
        let t = stripped.trim();
        if t.is_empty() {
            continue;
        }
        let rel = stripped.find(t).unwrap_or(0);
        let start = line_start + rel;
        let end = start + t.len();
        out.push((t.to_string(), Span::new(file, start, end)));
    }
    out
}

fn parse_for_header(line: &str) -> Option<(String, String)> {
    let t = line.trim();
    if !t.starts_with("For ") || !t.ends_with(':') {
        return None;
    }
    let name = t["For ".len()..t.len() - 1].trim();
    let (module, def) = name.split_once('.')?;
    if is_ident(module) && is_ident(def) {
        Some((module.to_string(), def.to_string()))
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

fn find_live_def<'a>(program: &'a Program, module: &str, name: &str) -> Option<&'a FunDef> {
    program.defs.iter().find(|d| {
        !d.is_driver && !is_intent_module(&d.module) && d.module == module && d.name == name
    })
}

fn lower_for_claim(
    label: &str,
    program: &Program,
    live: &FunDef,
    claim: &ForClaim,
    span: &Span,
) -> Result<Expr, OverlayError> {
    let err_span = span.clone();
    if live.params.len() > 3 {
        return Err(at(
            err_span,
            format!(
                "{label}: For `{}.{}` takes at most three generator-friendly params",
                live.module, live.name
            ),
        ));
    }
    if live.is_private {
        return Err(at(
            err_span,
            format!(
                "{label}: For `{}.{}` cannot bind a private def",
                live.module, live.name
            ),
        ));
    }
    for p in &live.params {
        if !crate::overlay::type_is_generator_friendly(&p.ty, program, &live.module, 0) {
            return Err(at(
                span.clone(),
                format!(
                    "{label}: For `{}.{}` param `{}` must be Int, String, Bool, List, or a record/enum of those",
                    live.module, live.name, p.name
                ),
            ));
        }
    }
    let pred_span = live.name_span.clone();
    match claim {
        ForClaim::Plus(n) => arith_pred(live, BinOp::Add, *n, pred_span, label, span),
        ForClaim::Minus(n) => arith_pred(live, BinOp::Sub, *n, pred_span, label, span),
        ForClaim::Identity => identity_pred(live, pred_span, label, span),
        ForClaim::Swap => swap_pred(live, pred_span, label, span),
        ForClaim::ProductFields => product_pred(program, live, pred_span, label, span),
    }
}

fn unary_live<'a>(live: &'a FunDef, label: &str, span: &Span) -> Result<&'a FunDef, OverlayError> {
    if live.params.len() != 1 {
        return Err(at(
            span.clone(),
            format!(
                "{label}: For `{}.{}` needs one input",
                live.module, live.name
            ),
        ));
    }
    Ok(live)
}

fn arith_pred(
    live: &FunDef,
    op: BinOp,
    n: i64,
    span: Span,
    label: &str,
    err_span: &Span,
) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label, err_span)?;
    if !matches!(live.params[0].ty, Type::Int) || !matches!(live.ret, Type::Int) {
        return Err(at(
            err_span.clone(),
            format!(
                "{label}: For `{}.{}` plus/minus needs an Int input and Int result",
                live.module, live.name
            ),
        ));
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

fn identity_pred(
    live: &FunDef,
    span: Span,
    label: &str,
    err_span: &Span,
) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label, err_span)?;
    if live.params[0].ty != live.ret {
        return Err(at(
            err_span.clone(),
            format!(
                "{label}: For `{}.{}` identity needs the result to match the input type",
                live.module, live.name
            ),
        ));
    }
    let call = live_call(live, span.clone());
    let rhs = Expr::new(ExprKind::Var(live.params[0].name.clone()), span.clone());
    Ok(eq_expr(call, rhs, span))
}

fn swap_pred(
    live: &FunDef,
    span: Span,
    label: &str,
    err_span: &Span,
) -> Result<Expr, OverlayError> {
    if live.params.len() != 2 {
        return Err(at(
            err_span.clone(),
            format!(
                "{label}: For `{}.{}` swap needs two inputs",
                live.module, live.name
            ),
        ));
    }
    if live.params[0].ty != live.params[1].ty {
        return Err(at(
            err_span.clone(),
            format!(
                "{label}: For `{}.{}` swap needs two inputs of the same type",
                live.module, live.name
            ),
        ));
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
    err_span: &Span,
) -> Result<Expr, OverlayError> {
    let live = unary_live(live, label, err_span)?;
    if !matches!(live.ret, Type::Int) {
        return Err(at(
            err_span.clone(),
            format!(
                "{label}: For `{}.{}` product needs an Int result",
                live.module, live.name
            ),
        ));
    }
    let fields =
        fill_product_fields(program, &live.params[0].ty, &live.module).ok_or_else(|| {
            at(
                err_span.clone(),
                format!(
                    "{label}: For `{}.{}` product needs a record of Int fields",
                    live.module, live.name
                ),
            )
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

fn assert_expr(name: &str, pred: Expr, span: Span) -> Expr {
    Expr::new(
        ExprKind::Call {
            callee: "Property.assert".into(),
            args: vec![
                Expr::new(ExprKind::StrLit(name.to_string()), span.clone()),
                pred,
            ],
        },
        span,
    )
}

fn seq_io(exprs: Vec<Expr>, span: Span) -> Expr {
    let mut iter = exprs.into_iter();
    let first = iter.next().expect("non-empty asserts");
    iter.fold(first, |acc, next| {
        Expr::new(
            ExprKind::FlatMap {
                inner: Box::new(acc),
                param: None,
                body: Box::new(next),
            },
            span.clone(),
        )
    })
}

fn forall_driver(name: &str, params: &[Param], body: Expr, span: Span) -> FunDef {
    FunDef {
        module: INTENT_MODULE.into(),
        name: name.to_string(),
        name_span: span,
        is_private: false,
        is_driver: true,
        type_params: Vec::new(),
        params: params.to_vec(),
        ret: Type::Io(Box::new(Type::Unit)),
        body,
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

    fn pkg_intent(text: &str) -> IntentSource {
        IntentSource {
            package: "pkg".into(),
            scope: IntentScopeKind::Package,
            dir_rel: String::new(),
            label: "pkg/intent.scuzz_intent".into(),
            text: text.into(),
            path: PathBuf::new(),
        }
    }

    fn dir_intent(dir_rel: &str, text: &str) -> IntentSource {
        IntentSource {
            package: "pkg".into(),
            scope: IntentScopeKind::Directory,
            dir_rel: dir_rel.into(),
            label: format!("pkg/{dir_rel}/{INTENT_FILE_NAME}"),
            text: text.into(),
            path: PathBuf::new(),
        }
    }

    fn main_unit() -> SourceUnit {
        SourceUnit {
            package: "pkg".into(),
            module: "Main".into(),
            rel: "src/Main.scuzz".into(),
        }
    }

    fn apply(p: &mut Program, text: &str) -> Result<(), OverlayError> {
        apply_intents(p, &[pkg_intent(text)], &[main_unit()])
    }

    #[test]
    fn missing_intent_is_noop() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        apply_intents(&mut p, &[], &[]).unwrap();
        assert!(p.intent_always.is_empty());
        assert!(p.defs.iter().all(|d| d.module != INTENT_MODULE));
    }

    #[test]
    fn empty_intent_fails() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply(&mut p, "# just a comment\n\n").unwrap_err();
        assert!(err.to_string().contains("empty"), "{err}");
        match err {
            OverlayError::At { span, .. } => {
                assert_eq!(span.file, "pkg/intent.scuzz_intent");
                assert_eq!(span.start, 0);
            }
            other => panic!("expected At, got {other}"),
        }
    }

    #[test]
    fn parses_visible_and_eventually() {
        let parsed = parse_intent(
            "The \"button:+1\" control is visible.\nEventually the \"text:done\" control is visible.\n",
            "pkg/intent.scuzz_intent",
        )
        .unwrap();
        assert_eq!(parsed.session.len(), 2);
        match &parsed.session[0].0 {
            SessionClaim::Visible { needle, eventually } => {
                assert_eq!(needle, "button:+1");
                assert!(!*eventually);
            }
            _ => panic!("expected visible"),
        }
        assert_eq!(parsed.session[0].1.start, 0);
        assert_eq!(
            parsed.session[0].1.end,
            "The \"button:+1\" control is visible.".len()
        );
        match &parsed.session[1].0 {
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
            "The count stays at 0 or more.\nFor Main.bump:\nThe result is the input plus 1.\n",
            "pkg/intent.scuzz_intent",
        )
        .unwrap();
        assert_eq!(
            parsed.session[0].0,
            SessionClaim::Stays {
                signal: "count".into(),
                n: 0
            }
        );
        assert_eq!(parsed.forall[0].0, "Main");
        assert_eq!(parsed.forall[0].1, "bump");
        assert_eq!(parsed.forall[0].2, ForClaim::Plus(1));
    }

    #[test]
    fn parses_swap_and_product() {
        let parsed = parse_intent(
            "For Main.add:\nSwapping the inputs does not change the result.\nFor Main.area:\nThe result is the product of the fields.\n",
            "pkg/intent.scuzz_intent",
        )
        .unwrap();
        assert_eq!(parsed.forall[0].2, ForClaim::Swap);
        assert_eq!(parsed.forall[1].2, ForClaim::ProductFields);
    }

    #[test]
    fn unknown_form_fails_with_span() {
        let err = parse_intent("The button should show.\n", "pkg/intent.scuzz_intent").unwrap_err();
        assert!(err.to_string().contains("unknown intent form"), "{err}");
        match err {
            OverlayError::At { span, .. } => {
                assert_eq!(span.start, 0);
                assert_eq!(span.end, "The button should show.".len());
            }
            other => panic!("expected At, got {other}"),
        }
    }

    #[test]
    fn bare_for_name_fails() {
        let err = parse_intent(
            "For bump:\nThe result is the input plus 1.\n",
            "pkg/intent.scuzz_intent",
        )
        .unwrap_err();
        assert!(err.to_string().contains("Module.def"), "{err}");
    }

    #[test]
    fn unbound_def_fails() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply(
            &mut p,
            "For Main.missing:\nThe result is the input plus 1.\n",
        )
        .unwrap_err();
        assert!(err.to_string().contains("unbound def"), "{err}");
    }

    #[test]
    fn unique_bare_name_does_not_bind_other_module() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply(&mut p, "For Other.bump:\nThe result is the input plus 1.\n").unwrap_err();
        assert!(err.to_string().contains("unbound def"), "{err}");
    }

    #[test]
    fn lowers_for_plus_drive() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        apply(&mut p, "For Main.bump:\nThe result is the input plus 1.\n").unwrap();
        let d = p
            .defs
            .iter()
            .find(|d| d.is_driver && d.name == "bump")
            .unwrap();
        assert_eq!(d.module, INTENT_MODULE);
        assert!(matches!(d.ret, Type::Io(_)));
        let dumped = format!("{:?}", d.body.kind);
        assert!(dumped.contains("pkg/intent.scuzz_intent:2"), "{dumped}");
    }

    #[test]
    fn aggregates_two_claims_into_one_drive() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        apply_intents(
            &mut p,
            &[
                pkg_intent("For Main.bump:\nThe result is the input plus 1.\n"),
                dir_intent("src", "For Main.bump:\nThe result is the input minus 0.\n"),
            ],
            &[main_unit()],
        )
        .unwrap();
        let drivers: Vec<_> = p.defs.iter().filter(|d| d.is_driver).collect();
        assert_eq!(drivers.len(), 1);
        assert_eq!(drivers[0].name, "bump");
        let dumped = format!("{:?}", drivers[0].body.kind);
        assert!(dumped.contains("FlatMap"), "{dumped}");
        assert!(dumped.contains("pkg/intent.scuzz_intent:2"), "{dumped}");
        assert!(
            dumped.contains(&format!("pkg/src/{INTENT_FILE_NAME}:2")),
            "{dumped}"
        );
    }

    #[test]
    fn duplicate_claim_fails() {
        let mut p = live("def bump(n: Int): Int = n + 1\n");
        let err = apply_intents(
            &mut p,
            &[
                pkg_intent("For Main.bump:\nThe result is the input plus 1.\n"),
                dir_intent("src", "For Main.bump:\nThe result is the input plus 1.\n"),
            ],
            &[main_unit()],
        )
        .unwrap_err();
        assert!(err.to_string().contains("duplicate claim"), "{err}");
        assert!(err.to_string().contains("pkg/intent.scuzz_intent"), "{err}");
    }

    #[test]
    fn directory_intent_rejects_session_claim() {
        let mut p = live("");
        let err = apply_intents(
            &mut p,
            &[dir_intent("src", "The \"button:+1\" control is visible.\n")],
            &[main_unit()],
        )
        .unwrap_err();
        assert!(err.to_string().contains("session claims"), "{err}");
    }

    #[test]
    fn directory_intent_rejects_out_of_scope_def() {
        let mut p = parse_sources(&[
            (
                "Billing.scuzz".into(),
                "def total(n: Int): Int = n\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "def bump(n: Int): Int = n + 1\n@main def main: IO[Unit] = IO.println(\"x\")\n"
                    .into(),
            ),
        ])
        .unwrap();
        let units = vec![
            SourceUnit {
                package: "pkg".into(),
                module: "Billing".into(),
                rel: "src/billing/Billing.scuzz".into(),
            },
            main_unit(),
        ];
        let err = apply_intents(
            &mut p,
            &[dir_intent(
                "src/billing",
                "For Main.bump:\nThe result is the input plus 1.\n",
            )],
            &units,
        )
        .unwrap_err();
        assert!(
            err.to_string().contains("outside this intent scope"),
            "{err}"
        );
    }

    #[test]
    fn directory_intent_binds_in_subtree() {
        let mut p = parse_sources(&[
            (
                "Billing.scuzz".into(),
                "def total(n: Int): Int = n\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
            ),
        ])
        .unwrap();
        let units = vec![
            SourceUnit {
                package: "pkg".into(),
                module: "Billing".into(),
                rel: "src/billing/Billing.scuzz".into(),
            },
            main_unit(),
        ];
        apply_intents(
            &mut p,
            &[dir_intent(
                "src/billing",
                "For Billing.total:\nThe result is the input.\n",
            )],
            &units,
        )
        .unwrap();
        assert!(p.defs.iter().any(|d| d.is_driver && d.name == "total"));
    }

    #[test]
    fn dependency_def_is_out_of_scope() {
        let mut p = parse_sources(&[
            (
                "Shared.scuzz".into(),
                "def bump(n: Int): Int = n + 1\n".into(),
            ),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(\"x\")\n".into(),
            ),
        ])
        .unwrap();
        let units = vec![
            SourceUnit {
                package: "shared".into(),
                module: "Shared".into(),
                rel: "src/Shared.scuzz".into(),
            },
            main_unit(),
        ];
        let err = apply_intents(
            &mut p,
            &[pkg_intent(
                "For Shared.bump:\nThe result is the input plus 1.\n",
            )],
            &units,
        )
        .unwrap_err();
        assert!(
            err.to_string().contains("outside this intent scope"),
            "{err}"
        );
    }

    #[test]
    fn lowers_visible_always_thunk() {
        let mut p = live("");
        apply(&mut p, "The \"button:+1\" control is visible.\n").unwrap();
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
        let err = apply(&mut p, "The count stays at 0 or more.\n").unwrap_err();
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
        apply(&mut p, "The count stays at 0 or more.\n").unwrap();
        assert_eq!(p.intent_always.len(), 1);
    }

    #[test]
    fn place_intent_package_and_directory() {
        let pkg = Path::new("/app");
        let place = place_intent_file(pkg, Path::new("/app/intent.scuzz_intent"))
            .unwrap()
            .unwrap();
        assert_eq!(place.scope, IntentScopeKind::Package);
        assert_eq!(place.rel, INTENT_FILE_NAME);
        let place = place_intent_file(pkg, Path::new("/app/src/billing/intent.scuzz_intent"))
            .unwrap()
            .unwrap();
        assert_eq!(place.scope, IntentScopeKind::Directory);
        assert_eq!(place.dir_rel, "src/billing");
    }

    #[test]
    fn place_intent_rejects_legacy_stem() {
        let err = place_intent_file(Path::new("/app"), Path::new("/app/src/Main.scuzz_intent"))
            .unwrap_err();
        assert!(err.contains("rename to intent.scuzz_intent"), "{err}");
    }
}

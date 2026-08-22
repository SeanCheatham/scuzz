//! Minimal Scuzz Lang formatter: parse → pretty-print (kernel dialect).

use crate::ast::{
    case_lambda_match_arms, BinOp, EnumDef, Expr, ExprKind, ForBinder, FunDef, ImplDef, MatchArm,
    Pattern, Program, TraitDef, Type, TypeAlias,
};
use crate::parser::{parse, ParseError};
use crate::span::Span;
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
    for a in &p.aliases {
        out.push_str(&pretty_alias(a));
        out.push('\n');
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
        if im.is_wildcard() {
            out.push('*');
        } else {
            out.push_str(&im.name);
            if let Some(alias) = &im.alias {
                out.push_str(" as ");
                out.push_str(alias);
            }
        }
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

fn pretty_alias(a: &TypeAlias) -> String {
    let tparams = if a.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", a.type_params.join(", "))
    };
    format!("type {}{tparams} = {}", a.name, pretty_type(&a.target))
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

fn pretty_param(p: &crate::ast::Param) -> String {
    let mut s = pretty_binding(&p.name, &p.ty, p.rfn.as_ref());
    if let Some(d) = &p.default {
        s.push_str(" = ");
        s.push_str(pretty_expr(d, 0).trim());
    }
    s
}

fn pretty_type(t: &Type) -> String {
    match t {
        Type::Unit => "Unit".into(),
        Type::Int => "Int".into(),
        Type::Float => "Float".into(),
        Type::String => "String".into(),
        Type::Bool => "Bool".into(),
        Type::List(inner) => format!("List[{}]", pretty_type(inner)),
        Type::Tuple(xs) => format!(
            "({})",
            xs.iter().map(pretty_type).collect::<Vec<_>>().join(", ")
        ),
        Type::Fun(a, b) => {
            let left = pretty_type(a);
            if matches!(a.as_ref(), Type::Fun(_, _)) {
                format!("({left}) => {}", pretty_type(b))
            } else {
                format!("{left} => {}", pretty_type(b))
            }
        }
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
        let params: Vec<String> = d.params.iter().map(pretty_param).collect();
        let sig = if params.is_empty() {
            d.name.clone()
        } else {
            format!("{}({})", d.name, params.join(", "))
        };
        return format!(
            "law {}: {} =\n{}",
            sig,
            pretty_type(&d.ret),
            pretty_expr(&d.body, 1)
        );
    }
    let params: Vec<String> = d.params.iter().map(pretty_param).collect();
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

/// `==` and looser. A `::` operand at this prec or below needs parens.
const PREC_CMP: i8 = 6;
/// Right-associative `::` (`List.cons`).
const PREC_CONS: i8 = 7;

/// Precedence of a binary operator. A larger value binds more tightly.
/// The order matches the parser: mul, add, shift, cons, cmp, `&`, `^`, `|`, `&&`, `||`.
fn bin_prec(op: BinOp) -> i8 {
    match op {
        BinOp::Mul | BinOp::Div | BinOp::Mod => 10,
        BinOp::Add | BinOp::Sub => 9,
        BinOp::Shl | BinOp::Shr => 8,
        BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => PREC_CMP,
        BinOp::BitAnd => 5,
        BinOp::BitXor => 4,
        BinOp::BitOr => 3,
        BinOp::And => 2,
        BinOp::Or => 1,
    }
}

fn is_cons_call(e: &Expr) -> bool {
    matches!(
        &e.kind,
        ExprKind::Call { callee, args } if callee == "List.cons" && args.len() == 2
    )
}

fn infix_prec(e: &Expr) -> Option<i8> {
    match &e.kind {
        ExprKind::Binary { op, .. } => Some(bin_prec(*op)),
        ExprKind::Call { callee, args } if callee == "List.cons" && args.len() == 2 => {
            Some(PREC_CONS)
        }
        _ => None,
    }
}

/// Print context that decides when a child needs parens.
#[derive(Clone, Copy)]
enum WrapCtx {
    /// Left or right operand of a binary operator.
    Infix { parent: i8, right: bool },
    /// Head or tail of `::`.
    Cons { right: bool },
    /// Operand of prefix `-`, `!`, or `~`.
    Unary,
    /// Receiver of `.field`, `.method`, `match`, `.attempt`, `.map`, `.flatMap`, or `.handleErrorWith`.
    Postfix,
}

/// Return true when this child needs parens in `ctx`.
fn needs_paren_in(e: &Expr, ctx: WrapCtx) -> bool {
    let loose_form = matches!(
        e.kind,
        ExprKind::If { .. }
            | ExprKind::For { .. }
            | ExprKind::Lambda { .. }
            | ExprKind::Ascribe { .. }
    );
    match ctx {
        WrapCtx::Infix { parent, right } => {
            if loose_form {
                return true;
            }
            match infix_prec(e) {
                Some(child) => child < parent || (right && child == parent),
                None => false,
            }
        }
        WrapCtx::Cons { right } => {
            if matches!(
                e.kind,
                ExprKind::If { .. }
                    | ExprKind::Match { .. }
                    | ExprKind::For { .. }
                    | ExprKind::Lambda { .. }
                    | ExprKind::Ascribe { .. }
            ) {
                return true;
            }
            if is_cons_call(e) {
                return !right;
            }
            if let ExprKind::Binary { op, .. } = &e.kind {
                return bin_prec(*op) <= PREC_CMP;
            }
            false
        }
        WrapCtx::Unary => {
            matches!(
                e.kind,
                ExprKind::Binary { .. }
                    | ExprKind::If { .. }
                    | ExprKind::Match { .. }
                    | ExprKind::For { .. }
                    | ExprKind::Ascribe { .. }
            ) || is_cons_call(e)
        }
        WrapCtx::Postfix => {
            matches!(
                e.kind,
                ExprKind::Binary { .. }
                    | ExprKind::Unary { .. }
                    | ExprKind::Ascribe { .. }
                    | ExprKind::If { .. }
                    | ExprKind::For { .. }
                    | ExprKind::Lambda { .. }
            ) || is_cons_call(e)
        }
    }
}

/// Print `e` and add parens when `needs_paren_in` is true.
fn pretty_in(e: &Expr, ctx: WrapCtx) -> String {
    let s = pretty_expr(e, 0).trim().to_string();
    if needs_paren_in(e, ctx) {
        format!("({s})")
    } else {
        s
    }
}

fn pretty_expr(expr: &Expr, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    match &expr.kind {
        ExprKind::Unit => format!("{pad}()"),
        ExprKind::IntLit(n) => format!("{pad}{n}"),
        ExprKind::FloatLit(bits) => format!("{pad}{}", crate::ast::format_float_bits(*bits)),
        ExprKind::BoolLit(true) => format!("{pad}true"),
        ExprKind::BoolLit(false) => format!("{pad}false"),
        ExprKind::StrLit(s) => format!("{pad}{}", quote_string(s)),
        ExprKind::ListLit { elems } => {
            let a: Vec<_> = elems
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!("{pad}[{}]", a.join(", "))
        }
        ExprKind::Tuple { elems } => {
            let inner: Vec<String> = elems
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!("{pad}({})", inner.join(", "))
        }
        ExprKind::Interpolate { parts } => {
            format!("{pad}{}", quote_interpolate(parts))
        }
        ExprKind::IoPrintln(e) => format!("{pad}IO.println({})", pretty_expr(e, 0).trim()),
        ExprKind::IoSleep(e) => format!("{pad}IO.sleep({})", pretty_expr(e, 0).trim()),
        ExprKind::IoFail(e) => format!("{pad}IO.fail({})", pretty_expr(e, 0).trim()),
        ExprKind::IoPure(e) => format!("{pad}IO.pure({})", pretty_expr(e, 0).trim()),
        ExprKind::Var(n) => format!("{pad}{n}"),
        ExprKind::Placeholder => format!("{pad}_"),
        ExprKind::Field { base, field } => {
            format!("{pad}{}.{}", pretty_in(base, WrapCtx::Postfix), field)
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
                pretty_in(receiver, WrapCtx::Postfix),
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
        ExprKind::Lambda {
            param,
            param_ty,
            pat,
            body,
        } => {
            if let Some(arms) = case_lambda_match_arms(param.as_deref(), body) {
                return pretty_case_lambda(arms, indent);
            }
            let body = pretty_expr(body, 0).trim().to_string();
            if let Some(p) = pat {
                let binder = pretty_pattern(p.as_ref());
                let binder = match p.as_ref() {
                    Pattern::Tuple { .. } => binder,
                    _ => format!("({binder})"),
                };
                return format!("{pad}{binder} => {body}");
            }
            match (param.as_deref(), param_ty) {
                (None, None) => format!("{pad}_ => {body}"),
                (None, Some(ty)) => format!("{pad}(_: {}) => {body}", pretty_type(ty)),
                (Some(p), None) => format!("{pad}{p} => {body}"),
                (Some(p), Some(ty)) => format!("{pad}({p}: {}) => {body}", pretty_type(ty)),
            }
        }
        ExprKind::Call { callee, args } => {
            if callee == "List.cons" && args.len() == 2 {
                return format!(
                    "{pad}{} :: {}",
                    pretty_in(&args[0], WrapCtx::Cons { right: false }),
                    pretty_in(&args[1], WrapCtx::Cons { right: true })
                );
            }
            let a: Vec<_> = args
                .iter()
                .map(|e| pretty_expr(e, 0).trim().to_string())
                .collect();
            format!("{pad}{callee}({})", a.join(", "))
        }
        ExprKind::NamedArg { name, value } => {
            format!("{pad}{name} = {}", pretty_expr(value, 0).trim())
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
            pretty_in(
                left,
                WrapCtx::Infix {
                    parent: bin_prec(*op),
                    right: false
                }
            ),
            binop_str(*op),
            pretty_in(
                right,
                WrapCtx::Infix {
                    parent: bin_prec(*op),
                    right: true
                }
            )
        ),
        ExprKind::Unary { op, expr } => {
            let inner = pretty_in(expr, WrapCtx::Unary);
            let pfx = match op {
                crate::ast::UnOp::Neg => "-",
                crate::ast::UnOp::Not => "!",
                crate::ast::UnOp::BitNot => "~",
            };
            format!("{pad}{pfx}{inner}")
        }
        ExprKind::Ascribe { expr, ty } => {
            let inner = pretty_expr(expr, 0).trim().to_string();
            let wrapped = match &expr.kind {
                ExprKind::Binary { .. }
                | ExprKind::If { .. }
                | ExprKind::Match { .. }
                | ExprKind::For { .. }
                | ExprKind::Lambda { .. } => format!("({inner})"),
                _ => inner,
            };
            format!("{pad}{wrapped}: {}", pretty_type(ty))
        }
        ExprKind::FlatMap { inner, param, body } => {
            let left = pretty_in(inner, WrapCtx::Postfix);
            if let Some(arms) = case_lambda_match_arms(param.as_deref(), body) {
                return format!(
                    "{pad}{left}.flatMap({})",
                    pretty_case_lambda(arms, 0).trim()
                );
            }
            let right = pretty_expr(body, indent + 1);
            let p = param.as_deref().unwrap_or("_");
            if !matches!(
                &body.kind,
                ExprKind::Let { .. }
                    | ExprKind::Match { .. }
                    | ExprKind::FlatMap { .. }
                    | ExprKind::IoMap { .. }
            ) && !right.contains('\n')
            {
                format!("{pad}{left}.flatMap({p} => {})", right.trim())
            } else {
                format!("{pad}{left}.flatMap({p} =>\n{right}\n{pad})")
            }
        }
        ExprKind::IoMap { inner, param, body } => {
            let left = pretty_in(inner, WrapCtx::Postfix);
            if let Some(arms) = case_lambda_match_arms(param.as_deref(), body) {
                return format!("{pad}{left}.map({})", pretty_case_lambda(arms, 0).trim());
            }
            let right = pretty_expr(body, indent + 1);
            let p = param.as_deref().unwrap_or("_");
            if !matches!(
                &body.kind,
                ExprKind::Let { .. } | ExprKind::Match { .. } | ExprKind::FlatMap { .. }
            ) && !right.contains('\n')
            {
                format!("{pad}{left}.map({p} => {})", right.trim())
            } else {
                format!("{pad}{left}.map({p} =>\n{right}\n{pad})")
            }
        }
        ExprKind::HandleErrorWith { inner, param, body } => {
            let left = pretty_in(inner, WrapCtx::Postfix);
            let right = pretty_expr(body, indent + 1);
            let p = param.as_deref().unwrap_or("_");
            format!("{pad}{left}.handleErrorWith({p} =>\n{right}\n{pad})")
        }
        ExprKind::Attempt { inner } => {
            let left = pretty_in(inner, WrapCtx::Postfix);
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
                        span: Span::dummy(),
                        value: *value.clone(),
                        pat: None,
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
                    ForBinder::Eq {
                        name, value, pat, ..
                    } => {
                        out.push_str(&inner);
                        if let Some(p) = pat {
                            out.push_str(&pretty_pattern(p));
                        } else {
                            out.push_str(name);
                        }
                        out.push_str(" = ");
                        out.push_str(pretty_expr(value, 0).trim());
                        out.push('\n');
                    }
                    ForBinder::Draw {
                        name, value, pat, ..
                    } => {
                        out.push_str(&inner);
                        if let Some(p) = pat {
                            out.push_str(&pretty_pattern(p));
                        } else {
                            out.push_str(name);
                        }
                        out.push_str(" <- ");
                        out.push_str(pretty_expr(value, 0).trim());
                        out.push('\n');
                    }
                    ForBinder::Guard { pred, .. } => {
                        out.push_str(&inner);
                        out.push_str("if ");
                        out.push_str(pretty_expr(pred, 0).trim());
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
            let s = pretty_in(scrutinee, WrapCtx::Postfix);
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
        BinOp::BitAnd => "&",
        BinOp::BitOr => "|",
        BinOp::BitXor => "^",
        BinOp::Shl => "<<",
        BinOp::Shr => ">>",
    }
}

fn pretty_case_lambda(arms: &[MatchArm], indent: usize) -> String {
    let pad = "  ".repeat(indent);
    let mut out = format!("{pad}{{\n");
    for arm in arms {
        out.push_str(&pretty_arm(arm, indent + 1));
        out.push('\n');
    }
    out.push_str(&pad);
    out.push('}');
    out
}

fn pretty_arm(arm: &MatchArm, indent: usize) -> String {
    let pad = "  ".repeat(indent);
    let body = pretty_expr(&arm.body, 0).trim().to_string();
    let pat = pretty_pattern(&arm.pattern);
    match &arm.guard {
        Some(g) => {
            let pred = pretty_expr(g, 0).trim().to_string();
            format!("{pad}case {pat} if {pred} => {body}")
        }
        None => format!("{pad}case {pat} => {body}"),
    }
}

fn pretty_pattern(pat: &Pattern) -> String {
    match pat {
        Pattern::Wildcard => "_".into(),
        Pattern::Bind(name) => name.clone(),
        Pattern::Int(n) => format!("{n}"),
        Pattern::Float(bits) => crate::ast::format_float_bits(*bits),
        Pattern::Bool(true) => "true".into(),
        Pattern::Bool(false) => "false".into(),
        Pattern::Str(s) => quote_string(s),
        Pattern::Or(alts) => alts
            .iter()
            .map(pretty_pattern)
            .collect::<Vec<_>>()
            .join(" | "),
        Pattern::As { name, inner } => format!("{name} @ {}", pretty_pattern(inner)),
        Pattern::Nil => "[]".into(),
        Pattern::Cons { head, tail, .. } => pretty_cons_pattern(head, tail),
        Pattern::Tuple { elems, .. } => {
            let inner: Vec<String> = elems.iter().map(pretty_pattern).collect();
            format!("({})", inner.join(", "))
        }
        Pattern::Named { name, inner } => format!("{name} = {}", pretty_pattern(inner)),
        Pattern::Adt {
            enum_name,
            case_name,
            binds,
            ..
        } => {
            let bare = crate::resolve::enum_bare_name(enum_name);
            let inner: Vec<String> = binds.iter().map(pretty_pattern).collect();
            if bare == case_name.as_str() {
                if binds.is_empty() {
                    bare.to_string()
                } else {
                    format!("{bare}({})", inner.join(", "))
                }
            } else if binds.is_empty() {
                format!("{bare}.{case_name}")
            } else {
                format!("{bare}.{case_name}({})", inner.join(", "))
            }
        }
    }
}

fn pretty_cons_pattern(head: &Pattern, tail: &Pattern) -> String {
    if let Some(elems) = collect_nil_chain(head, tail) {
        return format!("[{}]", elems.join(", "));
    }
    let h = match head {
        Pattern::Or(_) | Pattern::Cons { .. } => format!("({})", pretty_pattern(head)),
        _ => pretty_pattern(head),
    };
    format!("{h} :: {}", pretty_pattern(tail))
}

fn collect_nil_chain(head: &Pattern, tail: &Pattern) -> Option<Vec<String>> {
    let mut elems = vec![pretty_pattern(head)];
    let mut cur = tail;
    loop {
        match cur {
            Pattern::Nil => return Some(elems),
            Pattern::Cons { head, tail, .. } => {
                elems.push(pretty_pattern(head));
                cur = tail;
            }
            _ => return None,
        }
    }
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

fn use_triple_quotes(s: &str) -> bool {
    s.contains('\n') && !s.contains('"')
}

fn quote_string(s: &str) -> String {
    if use_triple_quotes(s) {
        format!("\"\"\"{s}\"\"\"")
    } else {
        format!("\"{}\"", escape(s))
    }
}

fn interp_has_newline(parts: &[crate::ast::InterpPart]) -> bool {
    parts.iter().any(|p| match p {
        crate::ast::InterpPart::Lit(s) => s.contains('\n'),
        crate::ast::InterpPart::Expr(_) => false,
    })
}

fn interp_has_quote(parts: &[crate::ast::InterpPart]) -> bool {
    parts.iter().any(|p| match p {
        crate::ast::InterpPart::Lit(s) => s.contains('"'),
        crate::ast::InterpPart::Expr(_) => false,
    })
}

fn escape_triple_interp_lit(s: &str) -> String {
    let mut out = String::new();
    for ch in s.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '$' => out.push_str("\\$"),
            other => out.push(other),
        }
    }
    out
}

fn write_interp_body(parts: &[crate::ast::InterpPart], triple: bool) -> String {
    let mut body = String::new();
    for (i, part) in parts.iter().enumerate() {
        match part {
            crate::ast::InterpPart::Lit(s) => {
                if triple {
                    body.push_str(&escape_triple_interp_lit(s));
                } else {
                    body.push_str(&escape_interp_lit(s));
                }
            }
            crate::ast::InterpPart::Expr(e) => match &e.kind {
                ExprKind::Var(n) => {
                    let glue = match parts.get(i + 1) {
                        Some(crate::ast::InterpPart::Lit(s)) => s
                            .chars()
                            .next()
                            .is_some_and(|c| c.is_ascii_alphanumeric() || c == '_'),
                        _ => false,
                    };
                    if glue {
                        body.push_str("${");
                        body.push_str(n);
                        body.push('}');
                    } else {
                        body.push('$');
                        body.push_str(n);
                    }
                }
                _ => {
                    body.push_str("${");
                    body.push_str(pretty_expr(e, 0).trim());
                    body.push('}');
                }
            },
        }
    }
    body
}

fn quote_interpolate(parts: &[crate::ast::InterpPart]) -> String {
    if interp_has_newline(parts) && !interp_has_quote(parts) {
        format!("s\"\"\"{}\"\"\"", write_interp_body(parts, true))
    } else {
        format!("s\"{}\"", write_interp_body(parts, false))
    }
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
    fn formats_float_literal() {
        let src = r#"
def scale(x: Float): Float = x * 2.0
@main def main: IO[Unit] = IO.println(s"${scale(1.5)}")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("x: Float"));
        assert!(out.contains("2.0"));
        assert!(out.contains("1.5"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
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
    fn formats_match_guard() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(1) match {
    case Opt.Some(n) if n > 0 => IO.println("pos")
    case Opt.Some(n) => IO.println("nonpos")
    case Opt.None => IO.println("none")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Opt.Some(n) if n > 0 =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_literal_patterns() {
        let src = r#"
@main def main: IO[Unit] =
  n match {
    case 0 => IO.println("z")
    case -1 => IO.println("n")
    case true => IO.println("t")
    case "ok" => IO.println("s")
    case x => IO.println("b")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case 0 =>"));
        assert!(out.contains("case -1 =>"));
        assert!(out.contains("case true =>"));
        assert!(out.contains("case \"ok\" =>"));
        assert!(out.contains("case x =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_or_patterns() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  c match {
    case Color.Red | Color.Blue => IO.println("p")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Color.Red | Color.Blue =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_nested_or_pattern() {
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
        let out = format_source(src).unwrap();
        assert!(out.contains("case Opt.Some(0 | 1) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_as_pattern() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
enum Color:
  case Red
  case Blue
@main def main: IO[Unit] =
  o match {
    case s @ Opt.Some(n @ 0) => IO.println("z")
    case p @ Color.Red | Color.Blue => IO.println("p")
    case Opt.None => IO.println("n")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case s @ Opt.Some(n @ 0) =>"));
        assert!(out.contains("case p @ Color.Red | Color.Blue =>"));
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
    fn formats_tuple_for_binder_and_lambda() {
        let src = r#"@main def main: IO[Unit] =
  for {
    (n, s) = (1, "x")
    (a, b) <- IO.both(IO.pure(2), IO.pure("y"))
  } yield IO.println(List.join(List.map([(3, "z")], (i, t) => t), ","))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(n, s) = (1, \"x\")"), "{out}");
        assert!(out.contains("(a, b) <- IO.both"), "{out}");
        assert!(out.contains("(i, t) => t"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_ctor_for_binder_and_lambda() {
        let src = r#"@main def main: IO[Unit] =
  for {
    Point(x, y) = Point(1, 2)
    Opt.Some(n) = Opt.Some(3)
    h :: _t = [4]
  } yield IO.println(Str.fromInt(List.head(List.map([Opt.Some(5)], (Opt.Some(k)) => k))))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Point(x, y) = Point(1, 2)"), "{out}");
        assert!(out.contains("Opt.Some(n) = Opt.Some(3)"), "{out}");
        assert!(out.contains("h :: _t = [4]"), "{out}");
        assert!(out.contains("(Opt.Some(k)) => k"), "{out}");
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
    _ <- Ui.run(_ => View.text("x"))
  } yield IO.pure(())
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("for {"));
        assert!(out.contains("count = Signal.int(0)"));
        assert!(out.contains("_ <- Ui.run(_ => View.text(\"x\"))"));
        assert!(out.contains("yield IO.pure(())"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_for_if_guard() {
        let src = r#"@main def main: IO[Unit] =
  for {
    x <- IO.pure(1)
    if x > 0
  } yield x
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("if x > 0"), "{out}");
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
    _ <- IO.foreach(["a"], x => IO.println(x))
    _ <- IO.when(true, IO.println("y"))
    _ <- IO.println(n)
    _ <- IO.println(t)
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("IO.repeatN("));
        assert!(out.contains("IO.retryN("));
        assert!(out.contains("IO.forever("));
        assert!(out.contains("IO.foreach("));
        assert!(out.contains("IO.when("));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_ref_update_roundtrip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of(1)
    _ <- Ref.update(r, n => n + 1)
    n <- Ref.updateAndGet(r, n => n + 1)
    _ <- IO.println(Str.fromInt(n))
  } yield ()
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Ref.update("));
        assert!(out.contains("Ref.updateAndGet("));
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
    fn formats_nested_adt_pattern() {
        let src = r#"
enum Color:
  case Red
  case Blue
enum Wrap:
  case Box(c: Color)
  case Empty
@main def main: IO[Unit] =
  Wrap.Box(Color.Red) match {
    case Wrap.Box(Color.Red) => IO.println("red")
    case Wrap.Box(_) => IO.println("other")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Wrap.Box(Color.Red) =>"));
        assert!(out.contains("case Wrap.Box(_) =>"));
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_tuple_roundtrip() {
        let src = r#"
def swap(p: (Int, String)): (String, Int) =
  p match {
    case (n, s) => (s, n)
  }
@main def main: IO[Unit] =
  IO.println(swap((1, "x"))._1)
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("def swap(p: (Int, String)): (String, Int) ="));
        assert!(out.contains("case (n, s) =>"));
        assert!(out.contains("(s, n)"));
        assert!(out.contains("swap((1, \"x\"))._1"));
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
    fn formats_import_alias_and_wildcard() {
        let src = "import A.tag as fromA\nimport B.*\n@main def main: IO[Unit] =\n  IO.println(fromA())\n";
        let out = format_source(src).unwrap();
        assert!(out.contains("import A.tag as fromA"), "{out}");
        assert!(out.contains("import B.*"), "{out}");
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
    fn formats_list_patterns() {
        let src = r#"
def describe(xs: List[String]): String =
  xs match {
    case [] => "empty"
    case x :: xs => x
  }
@main def main: IO[Unit] = IO.println(describe(["a"]))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case [] =>"), "{out}");
        assert!(out.contains("case x :: xs =>"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_list_literal_pattern() {
        let src = r#"
def isPair(xs: List[String]): String =
  xs match {
    case ["a", "b"] => "ab"
    case _ => "no"
  }
@main def main: IO[Unit] = IO.println(isPair(["a"]))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case [\"a\", \"b\"] =>"), "{out}");
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

    #[test]
    fn formats_unary_and_bitwise() {
        let src = r#"
def bits(n: Int, b: Bool): Int = if (!b) -n else n & 0xF | 1 << 2
@main def main: IO[Unit] = IO.println("x")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("!b"), "{out}");
        assert!(out.contains("-n"), "{out}");
        assert!(out.contains("n & 15") || out.contains("n & 0xF"), "{out}");
        assert!(out.contains("<<"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_named_args() {
        let src = r#"
def add(n: Int, m: Int): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(m = 2, n = 1)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("add(m = 2, n = 1)"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_param_default() {
        let src = r#"
def add(n: Int, m: Int = 1): Int = n + m
@main def main: IO[Unit] = IO.println(Str.fromInt(add(3)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("m: Int = 1"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_record_copy() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy(y = 9).x))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Point(3, 5).copy(y = 9)"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_type_ascription() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  for {
    x = Opt.None: Opt[Int]
    n = (1: Int) + 2
  } yield IO.println(Str.fromInt(n))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("Opt.None: Opt[Int]"), "{out}");
        assert!(out.contains("(1: Int)"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_named_field_pattern() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] =
  Point(3, 5) match {
    case Point(x = n) => IO.println(Str.fromInt(n))
  }
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("case Point(x = n)"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_cons_expr() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join("a" :: "b" :: List.empty(), ","))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("\"a\" :: \"b\" :: List.empty()"), "{out}");
        assert!(!out.contains("List.cons"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_placeholder_lambda() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map([1], _ + 1), ","))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("_ + 1"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_io_map() {
        let src = r#"@main def main: IO[Unit] =
  IO.pure(1).map(n => n + 1).flatMap(n => IO.println(Str.fromInt(n)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains(".map(n => n + 1)"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_typed_lambda_param() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map([1], (n: Int) => Str.fromInt(n)), ","))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(n: Int) =>"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_case_lambda() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.println(List.join(List.map([Opt.Some(1)], { case Opt.Some(n) => Str.fromInt(n) case Opt.None => "n" }), ","))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("{"), "{out}");
        assert!(out.contains("case Opt.Some(n)"), "{out}");
        assert!(out.contains("case Opt.None"), "{out}");
        assert!(!out.contains("__case"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_case_lambda_on_io_map() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  IO.pure(Opt.Some(1)).map({ case Opt.Some(n) => n case Opt.None => 0 }).flatMap(n => IO.println(Str.fromInt(n)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains(".map({"), "{out}");
        assert!(out.contains("case Opt.Some(n)"), "{out}");
        assert!(!out.contains("__case"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_triple_quoted_multiline() {
        let src = "@main def main: IO[Unit] =\n  IO.println(\"\"\"a\nb\"\"\")\n";
        let out = format_source(src).unwrap();
        assert!(out.contains("\"\"\"a\nb\"\"\""), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_scientific_and_separated_numbers() {
        let src =
            r#"@main def main: IO[Unit] = IO.println(Str.fromInt(1_000 + Float.toInt(1.5e1)))"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("1000"), "{out}");
        assert!(
            out.contains("15.0") || out.contains("1.5e1") || out.contains("15"),
            "{out}"
        );
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_type_alias() {
        let src = r#"
type UserId = Int
type BoxList[T] = List[T]
def idOf(n: UserId): UserId = n
@main def main: IO[Unit] = IO.println(Str.fromInt(idOf(1)))
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("type UserId = Int"), "{out}");
        assert!(out.contains("type BoxList[T] = List[T]"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_keeps_infix_grouping() {
        let src = r#"
def mul(): Int = (1 + 2) * 3
def sub(a: Int, b: Int, c: Int): Int = a - (b - c)
def and(a: Bool, b: Bool, c: Bool): Bool = (a || b) && c
def cons(a: Int, b: Int, xs: List[Int]): List[Int] = (a == b) :: xs
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(1 + 2) * 3"), "{out}");
        assert!(out.contains("a - (b - c)"), "{out}");
        assert!(out.contains("(a || b) && c"), "{out}");
        assert!(out.contains("(a == b) ::"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_keeps_cons_and_unary_grouping() {
        let src = r#"
def add(a: Int, b: List[Int], c: Int): Int = (a :: b) + c
def neg(a: Int, b: List[Int]): Int = -(a :: b)
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(a :: b) + c"), "{out}");
        assert!(!out.contains("a :: b +"), "{out}");
        assert!(
            out.contains("-(a :: b)") || out.contains("-((a :: b))"),
            "{out}"
        );
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_keeps_postfix_and_if_grouping() {
        let src = r#"
record Point(x: Int, y: Int)
def copy(a: Point, b: Point): Point = (a + b).copy(x = 1)
def addIf(c: Bool, a: Int, b: Int): Int = (if (c) a else b) + 1
def matchAdd(a: Int, b: Int): Int = (a + b) match {
  case x => x
}
def addLam(): Int = (x => x) + 1
def field(n: Int): Int = (-n).x
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(a + b).copy"), "{out}");
        assert!(out.contains("(if"), "{out}");
        assert!(out.contains("(a + b) match"), "{out}");
        assert!(out.contains("(x => x) + 1"), "{out}");
        assert!(out.contains("(-n).x"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_keeps_interp_ident_glue() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    n = 1
  } yield IO.println(s"${n}foo")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("${n}foo"), "{out}");
        assert!(!out.contains("$nfoo"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_keeps_fun_type_parens() {
        let src = r#"
def apply(f: (Int => String) => Bool, g: Int => String): Bool = f(g)
@main def main: IO[Unit] = IO.println("ok")
"#;
        let out = format_source(src).unwrap();
        assert!(out.contains("(Int => String) => Bool"), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }

    #[test]
    fn formats_multiline_string_with_quote() {
        let src = "@main def main: IO[Unit] =\n  IO.println(\"hello\\nworld\\\"\")\n";
        let out = format_source(src).unwrap();
        assert!(out.contains("\"hello\\nworld\\\"\""), "{out}");
        assert!(!out.contains("\"\"\""), "{out}");
        let again = format_source(&out).unwrap();
        assert_eq!(out, again);
    }
}

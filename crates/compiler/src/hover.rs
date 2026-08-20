//! Signature hover from the same parse as `check`. No second typer.

use crate::ast::{EnumDef, Expr, ExprKind, FunDef, Param, Program, Type, TypeAlias};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use crate::typ::kit_lambda_param_ty;

const COPY_HOVER: &str = "record.copy(field = value, …): T";

/// Hover text at a byte offset in `source` (`file` is the parse label).
pub fn hover_in_source(
    program: &Program,
    file: &str,
    source: &str,
    offset: usize,
) -> Option<String> {
    let (qual, name) = ident_at(source, offset)?;
    let module = module_id_from_label(file);
    if let Some(q) = qual {
        let callee = format!("{q}.{name}");
        if let Some(sig) = kit_sig(&callee) {
            return Some(sig.to_string());
        }
        if let Some(d) = def_named(program, &q, &name) {
            return Some(show_def(d));
        }
        if let Some(en) =
            enum_named(program, &q, &name).or_else(|| enum_named(program, &module, &name))
        {
            return Some(show_enum(en));
        }
        if name == "copy" {
            return Some(COPY_HOVER.into());
        }
        if let Some(a) = alias_named(program, &q, &name) {
            return Some(show_alias(a));
        }
        return None;
    }
    if matches!(
        name.as_str(),
        "Int" | "Float" | "String" | "Bool" | "Unit" | "List" | "Map" | "Set" | "IO"
    ) {
        return Some(name);
    }
    if let Some(a) = alias_named(program, &module, &name)
        .or_else(|| imported_alias(program, &module, &name))
        .or_else(|| unique_alias(program, &name))
    {
        return Some(show_alias(a));
    }
    if let Some(d) = def_named(program, &module, &name)
        .or_else(|| imported_def(program, &module, &name))
        .or_else(|| unique_def(program, &name))
    {
        return Some(show_def(d));
    }
    if let Some(en) = enum_named(program, &module, &name)
        .or_else(|| imported_enum(program, &module, &name))
        .or_else(|| unique_enum(program, &name))
    {
        return Some(show_enum(en));
    }
    if let Some(p) = param_in_module(program, &module, &name, offset) {
        return Some(show_param(p));
    }
    if name == "copy" {
        return Some(COPY_HOVER.into());
    }
    binder_in_program(program, file, &name, offset)
}

fn ident_at(source: &str, offset: usize) -> Option<(Option<String>, String)> {
    ident_at_opts(source, offset, true)
}

/// Ident at `offset`. When `lookahead` is set, `IO` in `IO.println` resolves as `IO.println`.
pub(crate) fn ident_at_opts(
    source: &str,
    offset: usize,
    lookahead: bool,
) -> Option<(Option<String>, String)> {
    let toks = lex(source).ok()?;
    let mut hit = None;
    for (i, t) in toks.iter().enumerate() {
        if t.span.start <= offset && offset < t.span.end {
            hit = Some(i);
            break;
        }
        if offset == t.span.end && matches!(t.token, Token::Ident(_)) {
            hit = Some(i);
        }
    }
    let i = hit?;
    let Token::Ident(name) = &toks[i].token else {
        return None;
    };
    if i >= 2 && matches!(toks[i - 1].token, Token::Dot) {
        if let Token::Ident(q) = &toks[i - 2].token {
            return Some((Some(q.clone()), name.clone()));
        }
    }
    if lookahead && i + 2 < toks.len() && matches!(toks[i + 1].token, Token::Dot) {
        if let Token::Ident(m) = &toks[i + 2].token {
            return Some((Some(name.clone()), m.clone()));
        }
    }
    Some((None, name.clone()))
}

pub(crate) fn def_named<'a>(program: &'a Program, module: &str, name: &str) -> Option<&'a FunDef> {
    program
        .defs
        .iter()
        .find(|d| d.module == module && d.name == name)
}

pub(crate) fn imported_def<'a>(
    program: &'a Program,
    module: &str,
    name: &str,
) -> Option<&'a FunDef> {
    let (from, src) = crate::resolve::bind_import(
        &program.imports,
        &program.defs,
        &program.enums,
        &program.aliases,
        module,
        name,
    )?;
    def_named(program, from, &src)
}

pub(crate) fn imported_enum<'a>(
    program: &'a Program,
    module: &str,
    name: &str,
) -> Option<&'a EnumDef> {
    let (from, src) = crate::resolve::bind_import(
        &program.imports,
        &program.defs,
        &program.enums,
        &program.aliases,
        module,
        name,
    )?;
    enum_named(program, from, &src)
}

pub(crate) fn unique_def<'a>(program: &'a Program, name: &str) -> Option<&'a FunDef> {
    let hits: Vec<_> = program.defs.iter().filter(|d| d.name == name).collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

pub(crate) fn enum_named<'a>(
    program: &'a Program,
    module: &str,
    name: &str,
) -> Option<&'a EnumDef> {
    program
        .enums
        .iter()
        .find(|e| e.module == module && e.name == name)
}

pub(crate) fn unique_enum<'a>(program: &'a Program, name: &str) -> Option<&'a EnumDef> {
    let hits: Vec<_> = program.enums.iter().filter(|e| e.name == name).collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

fn alias_named<'a>(program: &'a Program, module: &str, name: &str) -> Option<&'a TypeAlias> {
    program
        .aliases
        .iter()
        .find(|a| a.module == module && a.name == name)
}

fn imported_alias<'a>(program: &'a Program, module: &str, name: &str) -> Option<&'a TypeAlias> {
    let (from, src) = crate::resolve::bind_import(
        &program.imports,
        &program.defs,
        &program.enums,
        &program.aliases,
        module,
        name,
    )?;
    alias_named(program, from, &src)
}

fn unique_alias<'a>(program: &'a Program, name: &str) -> Option<&'a TypeAlias> {
    let hits: Vec<_> = program.aliases.iter().filter(|a| a.name == name).collect();
    if hits.len() == 1 {
        Some(hits[0])
    } else {
        None
    }
}

fn param_in_module<'a>(
    program: &'a Program,
    module: &str,
    name: &str,
    offset: usize,
) -> Option<&'a Param> {
    let mut hits: Vec<&Param> = Vec::new();
    let mut containing: Option<&Param> = None;
    for d in program.defs.iter().filter(|d| d.module == module) {
        for p in &d.params {
            if p.name != name {
                continue;
            }
            hits.push(p);
            if d.body.span.start <= offset && offset <= d.body.span.end {
                containing = Some(p);
            }
        }
    }
    containing.or_else(|| if hits.len() == 1 { Some(hits[0]) } else { None })
}

fn binder_in_program(program: &Program, file: &str, name: &str, offset: usize) -> Option<String> {
    for d in &program.defs {
        if d.body.span.file == file {
            if let Some(s) = binder_in_expr(&d.body, name, offset) {
                return Some(s);
            }
        }
    }
    if program.main.body.span.file == file {
        return binder_in_expr(&program.main.body, name, offset);
    }
    None
}

fn binder_in_expr(expr: &Expr, name: &str, offset: usize) -> Option<String> {
    let locals = kit_lambda_locals(expr, offset);
    for (n, ty) in locals.iter().rev() {
        if n == name {
            return Some(format!("{n}: {ty}"));
        }
    }
    let mut best: Option<(usize, String)> = None;
    walk_binders(expr, name, offset, &mut best);
    best.map(|(_, s)| s)
}

/// Kit lambda params whose span covers `offset` (outer first). Skip `_`.
pub(crate) fn kit_lambda_locals(expr: &Expr, offset: usize) -> Vec<(String, Type)> {
    let mut out = Vec::new();
    collect_kit_locals(expr, offset, &mut out);
    out
}

fn collect_kit_locals(expr: &Expr, offset: usize, out: &mut Vec<(String, Type)>) {
    match &expr.kind {
        ExprKind::Call { callee, args } => {
            let n = args.len();
            for (i, a) in args.iter().enumerate() {
                if let Some(pty) = kit_lambda_param_ty(callee, i, n) {
                    if let ExprKind::Lambda {
                        param: Some(p),
                        body,
                        ..
                    } = &a.kind
                    {
                        if a.span.start <= offset && offset <= a.span.end && p != "_" {
                            out.push((p.clone(), pty));
                        }
                        collect_kit_locals(body, offset, out);
                        continue;
                    }
                }
                collect_kit_locals(a, offset, out);
            }
        }
        _ => expr.for_each_child(|c| collect_kit_locals(c, offset, out)),
    }
}

fn walk_binders(expr: &Expr, name: &str, offset: usize, best: &mut Option<(usize, String)>) {
    let span_len = expr.span.end.saturating_sub(expr.span.start);
    let covers = expr.span.start <= offset && offset <= expr.span.end;
    match &expr.kind {
        ExprKind::Let { name: n, body, .. } if n == name && covers => {
            consider(best, span_len, n);
            walk_binders(body, name, offset, best);
        }
        ExprKind::FlatMap {
            param: Some(n),
            body,
            ..
        } if n == name && covers => {
            consider(best, span_len, n);
            walk_binders(body, name, offset, best);
        }
        ExprKind::Lambda {
            param: Some(n),
            body,
            ..
        } if n == name && covers => {
            consider(best, span_len, n);
            walk_binders(body, name, offset, best);
        }
        _ => expr.for_each_child(|c| walk_binders(c, name, offset, best)),
    }
}

fn consider(best: &mut Option<(usize, String)>, span_len: usize, name: &str) {
    let text = name.to_string();
    match best {
        None => *best = Some((span_len, text)),
        Some((len, _)) if span_len < *len => *best = Some((span_len, text)),
        _ => {}
    }
}

pub(crate) fn show_def(d: &FunDef) -> String {
    let vis = if d.is_private { "private " } else { "" };
    let tps = if d.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", d.type_params.join(", "))
    };
    let params: Vec<String> = d.params.iter().map(show_param).collect();
    if d.is_law {
        format!("law {}: {}", d.name, d.ret)
    } else {
        format!("{vis}def {}{tps}({}): {}", d.name, params.join(", "), d.ret)
    }
}

pub(crate) fn show_param(p: &Param) -> String {
    let mut s = if p.rfn.is_some() {
        format!("{}: {} where ...", p.name, p.ty)
    } else {
        format!("{}: {}", p.name, p.ty)
    };
    if let Some(d) = &p.default {
        s.push_str(" = ");
        s.push_str(&show_default(d));
    }
    s
}

fn show_default(e: &Expr) -> String {
    match &e.kind {
        ExprKind::IntLit(n) => n.to_string(),
        ExprKind::BoolLit(true) => "true".into(),
        ExprKind::BoolLit(false) => "false".into(),
        ExprKind::StrLit(s) => format!("\"{s}\""),
        ExprKind::FloatLit(bits) => crate::ast::format_float_bits(*bits),
        ExprKind::Unit => "()".into(),
        _ => "…".into(),
    }
}

pub(crate) fn show_alias(a: &TypeAlias) -> String {
    let tps = if a.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", a.type_params.join(", "))
    };
    format!("type {}{tps} = {}", a.name, a.target)
}

pub(crate) fn show_enum(en: &EnumDef) -> String {
    let tps = if en.type_params.is_empty() {
        String::new()
    } else {
        format!("[{}]", en.type_params.join(", "))
    };
    if en.is_record {
        let fields = en
            .cases
            .first()
            .map(|c| {
                c.fields
                    .iter()
                    .map(|(n, t)| format!("{n}: {t}"))
                    .collect::<Vec<_>>()
                    .join(", ")
            })
            .unwrap_or_default();
        format!("record {}{tps}({fields})", en.name)
    } else {
        let cases: Vec<String> = en
            .cases
            .iter()
            .map(|c| {
                if c.fields.is_empty() {
                    c.name.clone()
                } else {
                    let fs: Vec<String> =
                        c.fields.iter().map(|(n, t)| format!("{n}: {t}")).collect();
                    format!("{}({})", c.name, fs.join(", "))
                }
            })
            .collect();
        format!("enum {}{tps}: {}", en.name, cases.join(" | "))
    }
}

pub(crate) const KIT_SIGS: &[(&str, &str)] = &[
    ("IO.println", "IO.println(s: String): IO[Unit]"),
    ("IO.sleep", "IO.sleep(ms: Int): IO[Unit]"),
    ("IO.fail", "IO.fail(s: String): IO[Unit]"),
    ("IO.pure", "IO.pure(x: T): IO[T]"),
    ("IO.timeout", "IO.timeout(ms: Int, inner: IO[T]): IO[T]"),
    ("IO.forever", "IO.forever(inner: IO[T]): IO[Unit]"),
    ("IO.repeatN", "IO.repeatN(n: Int, inner: IO[T]): IO[T]"),
    ("IO.retryN", "IO.retryN(n: Int, inner: IO[T]): IO[T]"),
    ("IO.race", "IO.race(a: IO[T], b: IO[T]): IO[T]"),
    ("IO.both", "IO.both(a: IO[A], b: IO[B]): IO[(A, B)]"),
    (
        "IO.ensure",
        "IO.ensure(inner: IO[T], finalizer: IO[Unit]): IO[T]",
    ),
    ("Fiber.fork", "Fiber.fork(inner: IO[T]): IO[Fiber[T]]"),
    ("Fiber.join", "Fiber.join(f: Fiber[T]): IO[T]"),
    ("Fiber.interrupt", "Fiber.interrupt(f: Fiber[T]): IO[Unit]"),
    ("Str.fromInt", "Str.fromInt(n: Int): String"),
    ("Float.fromInt", "Float.fromInt(n: Int): Float"),
    ("Float.toInt", "Float.toInt(x: Float): Int"),
    (
        "Str.startsWith",
        "Str.startsWith(s: String, prefix: String): Bool",
    ),
    (
        "Str.contains",
        "Str.contains(s: String, needle: String): Bool",
    ),
    (
        "Str.endsWith",
        "Str.endsWith(s: String, suffix: String): Bool",
    ),
    ("Str.toInt", "Str.toInt(s: String, default: Int): Int"),
    (
        "Str.replace",
        "Str.replace(s: String, old: String, new: String): String",
    ),
    (
        "Str.split",
        "Str.split(s: String, sep: String): List[String]",
    ),
    ("Str.trim", "Str.trim(s: String): String"),
    ("Str.isEmpty", "Str.isEmpty(s: String): Bool"),
    ("Str.nonEmpty", "Str.nonEmpty(s: String): Bool"),
    ("Str.toLower", "Str.toLower(s: String): String"),
    ("Str.toUpper", "Str.toUpper(s: String): String"),
    ("Str.capitalize", "Str.capitalize(s: String): String"),
    ("Str.repeat", "Str.repeat(s: String, n: Int): String"),
    (
        "Str.stripPrefix",
        "Str.stripPrefix(s: String, prefix: String): String",
    ),
    (
        "Str.stripSuffix",
        "Str.stripSuffix(s: String, suffix: String): String",
    ),
    (
        "Str.padLeft",
        "Str.padLeft(s: String, n: Int, pad: String): String",
    ),
    (
        "Str.padRight",
        "Str.padRight(s: String, n: Int, pad: String): String",
    ),
    ("Str.isBlank", "Str.isBlank(s: String): Bool"),
    (
        "Str.lastIndexOf",
        "Str.lastIndexOf(s: String, needle: String): Int",
    ),
    ("Str.take", "Str.take(s: String, n: Int): String"),
    ("Str.drop", "Str.drop(s: String, n: Int): String"),
    ("Str.takeRight", "Str.takeRight(s: String, n: Int): String"),
    ("Str.dropRight", "Str.dropRight(s: String, n: Int): String"),
    ("Str.reverse", "Str.reverse(s: String): String"),
    ("Str.len", "Str.len(s: String): Int"),
    ("Str.charAt", "Str.charAt(s: String, i: Int): Int"),
    ("Str.indexOf", "Str.indexOf(s: String, needle: String): Int"),
    (
        "Str.slice",
        "Str.slice(s: String, start: Int, end: Int): String",
    ),
    ("Str.lines", "Str.lines(s: String): List[String]"),
    ("Signal.int", "Signal.int(n: Int): SignalInt"),
    ("Signal.get", "Signal.get(s: SignalInt): Int"),
    ("Signal.set", "Signal.set(s: SignalInt, n: Int): Unit"),
    ("Signal.str", "Signal.str(s: String): SignalStr"),
    (
        "Signal.map",
        "Signal.map(s: SignalInt, f: Int => String): SignalStr",
    ),
    ("Signal.list", "Signal.list(xs: List[T]): SignalList"),
    ("Signal.getStr", "Signal.getStr(s: SignalStr): String"),
    (
        "Signal.setStr",
        "Signal.setStr(s: SignalStr, v: String): Unit",
    ),
    (
        "Signal.getList",
        "Signal.getList(s: SignalList): List[String]",
    ),
    (
        "Signal.setList",
        "Signal.setList(s: SignalList, xs: List[String]): Unit",
    ),
    ("List.empty", "List.empty(): List[T]"),
    ("List.isEmpty", "List.isEmpty(xs: List[T]): Bool"),
    ("List.nonEmpty", "List.nonEmpty(xs: List[T]): Bool"),
    ("List.head", "List.head(xs: List[T]): T"),
    ("List.tail", "List.tail(xs: List[T]): List[T]"),
    ("List.len", "List.len(xs: List[T]): Int"),
    ("List.at", "List.at(xs: List[T], i: Int): T"),
    (
        "List.join",
        "List.join(xs: List[String], sep: String): String",
    ),
    ("Str.eq", "Str.eq(a: String, b: String): Bool"),
    (
        "List.filter",
        "List.filter(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.filterNot",
        "List.filterNot(xs: List[T], pred: T => Bool): List[T]",
    ),
    ("List.map", "List.map(xs: List[T], f: T => U): List[U]"),
    (
        "List.flatMap",
        "List.flatMap(xs: List[T], f: T => List[U]): List[U]",
    ),
    (
        "List.setAt",
        "List.setAt(xs: List[T], i: Int, v: T): List[T]",
    ),
    ("List.take", "List.take(xs: List[T], n: Int): List[T]"),
    ("List.drop", "List.drop(xs: List[T], n: Int): List[T]"),
    (
        "List.takeRight",
        "List.takeRight(xs: List[T], n: Int): List[T]",
    ),
    (
        "List.dropRight",
        "List.dropRight(xs: List[T], n: Int): List[T]",
    ),
    ("List.init", "List.init(xs: List[T]): List[T]"),
    ("List.last", "List.last(xs: List[T]): List[T]"),
    (
        "List.getOrElse",
        "List.getOrElse(xs: List[T], i: Int, default: T): T",
    ),
    ("List.fill", "List.fill(n: Int, x: T): List[T]"),
    ("List.range", "List.range(from: Int, until: Int): List[Int]"),
    (
        "List.tabulate",
        "List.tabulate(n: Int, f: Int => T): List[T]",
    ),
    (
        "List.intersperse",
        "List.intersperse(xs: List[T], x: T): List[T]",
    ),
    (
        "List.grouped",
        "List.grouped(xs: List[T], n: Int): List[List[T]]",
    ),
    (
        "List.sliding",
        "List.sliding(xs: List[T], n: Int): List[List[T]]",
    ),
    (
        "List.slice",
        "List.slice(xs: List[T], from: Int, until: Int): List[T]",
    ),
    ("List.indices", "List.indices(xs: List[T]): List[Int]"),
    (
        "List.padTo",
        "List.padTo(xs: List[T], n: Int, x: T): List[T]",
    ),
    ("List.reverse", "List.reverse(xs: List[T]): List[T]"),
    (
        "List.concat",
        "List.concat(xs: List[T], ys: List[T]): List[T]",
    ),
    ("List.flatten", "List.flatten(xss: List[List[T]]): List[T]"),
    (
        "List.find",
        "List.find(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.exists",
        "List.exists(xs: List[T], pred: T => Bool): Bool",
    ),
    (
        "List.indexWhere",
        "List.indexWhere(xs: List[T], pred: T => Bool): Int",
    ),
    (
        "List.lastIndexWhere",
        "List.lastIndexWhere(xs: List[T], pred: T => Bool): Int",
    ),
    (
        "List.count",
        "List.count(xs: List[T], pred: T => Bool): Int",
    ),
    (
        "List.takeWhile",
        "List.takeWhile(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.dropWhile",
        "List.dropWhile(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.span",
        "List.span(xs: List[T], pred: T => Bool): List[List[T]]",
    ),
    (
        "List.partition",
        "List.partition(xs: List[T], pred: T => Bool): List[List[T]]",
    ),
    (
        "List.splitAt",
        "List.splitAt(xs: List[T], n: Int): List[List[T]]",
    ),
    ("List.inits", "List.inits(xs: List[T]): List[List[T]]"),
    ("List.tails", "List.tails(xs: List[T]): List[List[T]]"),
    (
        "List.zip",
        "List.zip(xs: List[T], ys: List[T]): List[List[T]]",
    ),
    (
        "List.zipAll",
        "List.zipAll(xs: List[T], ys: List[T], x: T, y: T): List[List[T]]",
    ),
    (
        "List.unzip",
        "List.unzip(pairs: List[List[T]]): List[List[T]]",
    ),
    (
        "List.transpose",
        "List.transpose(xss: List[List[T]]): List[List[T]]",
    ),
    ("List.contains", "List.contains(xs: List[T], x: T): Bool"),
    ("List.indexOf", "List.indexOf(xs: List[T], x: T): Int"),
    (
        "List.lastIndexOf",
        "List.lastIndexOf(xs: List[T], x: T): Int",
    ),
    ("List.distinct", "List.distinct(xs: List[T]): List[T]"),
    ("List.diff", "List.diff(xs: List[T], ys: List[T]): List[T]"),
    (
        "List.intersect",
        "List.intersect(xs: List[T], ys: List[T]): List[T]",
    ),
    (
        "List.startsWith",
        "List.startsWith(xs: List[T], prefix: List[T]): Bool",
    ),
    (
        "List.endsWith",
        "List.endsWith(xs: List[T], suffix: List[T]): Bool",
    ),
    (
        "List.sameElements",
        "List.sameElements(xs: List[T], ys: List[T]): Bool",
    ),
    (
        "List.patch",
        "List.patch(xs: List[T], from: Int, other: List[T], replaced: Int): List[T]",
    ),
    (
        "List.findLast",
        "List.findLast(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.prefixLength",
        "List.prefixLength(xs: List[T], pred: T => Bool): Int",
    ),
    (
        "List.indexOfSlice",
        "List.indexOfSlice(xs: List[T], slice: List[T]): Int",
    ),
    (
        "List.lastIndexOfSlice",
        "List.lastIndexOfSlice(xs: List[T], slice: List[T]): Int",
    ),
    (
        "List.segmentLength",
        "List.segmentLength(xs: List[T], pred: T => Bool, from: Int): Int",
    ),
    (
        "List.isDefinedAt",
        "List.isDefinedAt(xs: List[T], i: Int): Bool",
    ),
    (
        "List.lengthCompare",
        "List.lengthCompare(xs: List[T], n: Int): Int",
    ),
    ("List.sort", "List.sort(xs: List[T]): List[T]"),
    (
        "List.sortBy",
        "List.sortBy(xs: List[T], f: T => Int): List[T]",
    ),
    ("List.max", "List.max(xs: List[T]): T"),
    ("List.min", "List.min(xs: List[T]): T"),
    ("List.maxBy", "List.maxBy(xs: List[T], f: T => Int): T"),
    ("List.minBy", "List.minBy(xs: List[T], f: T => Int): T"),
    (
        "List.groupBy",
        "List.groupBy(xs: List[T], f: T => K): Map[K, List[T]]",
    ),
    ("List.sum", "List.sum(xs: List[Int]): Int"),
    ("List.product", "List.product(xs: List[Int]): Int"),
    (
        "List.forall",
        "List.forall(xs: List[T], pred: T => Bool): Bool",
    ),
    ("View.text", "View.text(s: String): View"),
    ("View.bindText", "View.bindText(s: SignalStr): View"),
    (
        "View.button",
        "View.button(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.iconButton",
        "View.iconButton(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.fab",
        "View.fab(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.outlinedButton",
        "View.outlinedButton(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.textButton",
        "View.textButton(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.tooltip",
        "View.tooltip(message: String, child: View): View",
    ),
    ("View.placeholder", "View.placeholder(child: View): View"),
    (
        "View.semantics",
        "View.semantics(label: String, child: View): View",
    ),
    (
        "View.mergeSemantics",
        "View.mergeSemantics(label: String, child: View): View",
    ),
    (
        "View.inkWell",
        "View.inkWell(label: String, onTap: _ => Unit, child: View): View",
    ),
    (
        "View.visibility",
        "View.visibility(sig: SignalInt, child: View): View",
    ),
    (
        "View.offstage",
        "View.offstage(sig: SignalInt, child: View): View",
    ),
    (
        "View.unconstrainedBox",
        "View.unconstrainedBox(child: View): View",
    ),
    (
        "View.checkbox",
        "View.checkbox(sig: SignalInt, label: String): View",
    ),
    (
        "View.radio",
        "View.radio(sig: SignalInt, value: Int, label: String): View",
    ),
    ("View.slider", "View.slider(sig: SignalInt): View"),
    ("View.progress", "View.progress(sig: SignalInt): View"),
    (
        "View.circularProgress",
        "View.circularProgress(sig: SignalInt): View",
    ),
    ("View.avatar", "View.avatar(label: String): View"),
    (
        "View.switch",
        "View.switch(sig: SignalInt, label: String): View",
    ),
    (
        "View.chip",
        "View.chip(sig: SignalInt, label: String): View",
    ),
    (
        "View.filterChip",
        "View.filterChip(sig: SignalInt, label: String): View",
    ),
    (
        "View.choiceChip",
        "View.choiceChip(sig: SignalInt, value: Int, label: String): View",
    ),
    (
        "View.actionChip",
        "View.actionChip(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.inputChip",
        "View.inputChip(sig: SignalInt, label: String): View",
    ),
    (
        "View.listTile",
        "View.listTile(title: String, trailing: View): View",
    ),
    (
        "View.checkboxListTile",
        "View.checkboxListTile(sig: SignalInt, title: String): View",
    ),
    (
        "View.switchListTile",
        "View.switchListTile(sig: SignalInt, title: String): View",
    ),
    (
        "View.radioListTile",
        "View.radioListTile(sig: SignalInt, value: Int, title: String): View",
    ),
    (
        "View.segmented",
        "View.segmented(sig: SignalInt, left: String, right: String): View",
    ),
    (
        "View.badge",
        "View.badge(sig: SignalInt, child: View): View",
    ),
    ("View.card", "View.card(child: View): View"),
    ("View.divider", "View.divider(): View"),
    ("View.verticalDivider", "View.verticalDivider(): View"),
    (
        "View.expansionTile",
        "View.expansionTile(sig: SignalInt, title: String, child: View): View",
    ),
    ("View.scroll", "View.scroll(child: View): View"),
    ("View.scrollH", "View.scrollH(child: View): View"),
    ("View.column", "View.column(...): View"),
    ("View.row", "View.row(...): View"),
    ("View.wrap", "View.wrap(...): View"),
    ("View.grid", "View.grid(n: Int, ...): View"),
    ("View.stack", "View.stack(...): View"),
    (
        "View.each",
        "View.each(items: SignalList, row: String => View): View",
    ),
    ("View.expanded", "View.expanded(child: View): View"),
    (
        "View.sized",
        "View.sized(w: Int, h: Int, child: View): View",
    ),
    (
        "View.align",
        "View.align(ax: Int, ay: Int, child: View): View",
    ),
    (
        "View.positioned",
        "View.positioned(x: Int, y: Int, child: View): View",
    ),
    (
        "View.aspectRatio",
        "View.aspectRatio(rw: Int, rh: Int, child: View): View",
    ),
    (
        "View.fraction",
        "View.fraction(wpct: Int, hpct: Int, child: View): View",
    ),
    ("View.padding", "View.padding(n: Int, child: View): View"),
    (
        "View.background",
        "View.background(color: Int, child: View): View",
    ),
    ("View.icon", "View.icon(code: Int, color: Int): View"),
    (
        "View.image",
        "View.image(w: Int, h: Int, color: Int, caption: String): View",
    ),
    (
        "View.showWhen",
        "View.showWhen(sig: SignalInt, value: Int, child: View): View",
    ),
    (
        "View.textField",
        "View.textField(sig: SignalStr, hint: String): View",
    ),
    ("View.stretch", "View.stretch(child: View): View"),
    ("View.clip", "View.clip(child: View): View"),
    ("View.opacity", "View.opacity(pct: Int, child: View): View"),
    ("View.center", "View.center(child: View): View"),
    (
        "View.minSize",
        "View.minSize(w: Int, h: Int, child: View): View",
    ),
    (
        "View.maxSize",
        "View.maxSize(w: Int, h: Int, child: View): View",
    ),
    ("View.maxLines", "View.maxLines(n: Int, child: View): View"),
    ("View.ellipsis", "View.ellipsis(child: View): View"),
    (
        "View.textColor",
        "View.textColor(color: Int, child: View): View",
    ),
    ("View.gap", "View.gap(n: Int, child: View): View"),
    ("View.fontSize", "View.fontSize(n: Int, child: View): View"),
    (
        "View.border",
        "View.border(n: Int, color: Int, child: View): View",
    ),
    ("View.radius", "View.radius(n: Int, child: View): View"),
    ("Color.rgb", "Color.rgb(r: Int, g: Int, b: Int): Int"),
    (
        "Color.rgba",
        "Color.rgba(r: Int, g: Int, b: Int, a: Int): Int",
    ),
    ("Theme.accent", "Theme.accent(): Int"),
    ("Theme.primary", "Theme.primary(): Int"),
    ("Theme.muted", "Theme.muted(): Int"),
    ("Theme.foreground", "Theme.foreground(): Int"),
    (
        "View.ignorePointer",
        "View.ignorePointer(child: View): View",
    ),
    (
        "View.absorbPointer",
        "View.absorbPointer(child: View): View",
    ),
    (
        "View.excludeSemantics",
        "View.excludeSemantics(child: View): View",
    ),
    ("Ui.run", "Ui.run(_ => View): IO[Unit]"),
    (
        "Law.check",
        "Law.check(name: String, ok: Bool, value: T): T",
    ),
    ("Law.sometimes", "Law.sometimes(name: String): Unit"),
    ("Law.force", "Law.force(inner: IO[Bool]): Bool"),
    ("Law.assert", "Law.assert(name: String, ok: Bool): IO[Unit]"),
    ("Law.signalInt", "Law.signalInt(id: Int): Int"),
    ("Law.signalStr", "Law.signalStr(id: Int): String"),
    ("Law.signalListLen", "Law.signalListLen(id: Int): Int"),
    (
        "Law.signalListAt",
        "Law.signalListAt(id: Int, i: Int): String",
    ),
    ("Law.a11yHas", "Law.a11yHas(needle: String): Bool"),
    ("Ref.of", "Ref.of(s: String): IO[Ref[String]]"),
    ("Ref.get", "Ref.get(r: Ref[String]): IO[String]"),
    ("Ref.set", "Ref.set(r: Ref[String], s: String): IO[Unit]"),
    ("Queue.unbounded", "Queue.unbounded(): IO[Queue[String]]"),
    (
        "Queue.offer",
        "Queue.offer(q: Queue[String], s: String): IO[Unit]",
    ),
    ("Queue.take", "Queue.take(q: Queue[String]): IO[String]"),
    ("Deferred.empty", "Deferred.empty(): IO[Deferred[String]]"),
    (
        "Deferred.complete",
        "Deferred.complete(d: Deferred[String], s: String): IO[Unit]",
    ),
    (
        "Deferred.get",
        "Deferred.get(d: Deferred[String]): IO[String]",
    ),
    ("Str.concat", "Str.concat(a: String, b: String): String"),
    (
        "List.cons",
        "List.cons(x: T, xs: List[T]): List[T]  (`x :: xs`)",
    ),
    ("List.append", "List.append(xs: List[T], x: T): List[T]"),
    ("Map.empty", "Map.empty(): Map[K, V]"),
    ("Map.set", "Map.set(m: Map[K, V], k: K, v: V): Map[K, V]"),
    ("Map.get", "Map.get(m: Map[K, V], k: K): List[V]"),
    (
        "Map.getOrElse",
        "Map.getOrElse(m: Map[K, V], k: K, default: V): V",
    ),
    ("Map.contains", "Map.contains(m: Map[K, V], k: K): Bool"),
    ("Map.remove", "Map.remove(m: Map[K, V], k: K): Map[K, V]"),
    ("Map.keys", "Map.keys(m: Map[K, V]): List[K]"),
    ("Map.values", "Map.values(m: Map[K, V]): List[V]"),
    ("Map.size", "Map.size(m: Map[K, V]): Int"),
    ("Map.isEmpty", "Map.isEmpty(m: Map[K, V]): Bool"),
    ("Map.nonEmpty", "Map.nonEmpty(m: Map[K, V]): Bool"),
    (
        "Map.union",
        "Map.union(a: Map[K, V], b: Map[K, V]): Map[K, V]",
    ),
    (
        "Map.intersect",
        "Map.intersect(a: Map[K, V], b: Map[K, V]): Map[K, V]",
    ),
    (
        "Map.diff",
        "Map.diff(a: Map[K, V], b: Map[K, V]): Map[K, V]",
    ),
    (
        "Map.filter",
        "Map.filter(m: Map[K, V], pred: V => Bool): Map[K, V]",
    ),
    (
        "Map.mapValues",
        "Map.mapValues(m: Map[K, V], f: V => W): Map[K, W]",
    ),
    (
        "Map.exists",
        "Map.exists(m: Map[K, V], pred: V => Bool): Bool",
    ),
    (
        "Map.forall",
        "Map.forall(m: Map[K, V], pred: V => Bool): Bool",
    ),
    ("Set.empty", "Set.empty(): Set[T]"),
    ("Set.add", "Set.add(s: Set[T], x: T): Set[T]"),
    ("Set.contains", "Set.contains(s: Set[T], x: T): Bool"),
    ("Set.remove", "Set.remove(s: Set[T], x: T): Set[T]"),
    ("Set.toList", "Set.toList(s: Set[T]): List[T]"),
    ("Set.size", "Set.size(s: Set[T]): Int"),
    ("Set.isEmpty", "Set.isEmpty(s: Set[T]): Bool"),
    ("Set.nonEmpty", "Set.nonEmpty(s: Set[T]): Bool"),
    ("Set.union", "Set.union(a: Set[T], b: Set[T]): Set[T]"),
    (
        "Set.intersect",
        "Set.intersect(a: Set[T], b: Set[T]): Set[T]",
    ),
    ("Set.diff", "Set.diff(a: Set[T], b: Set[T]): Set[T]"),
    ("Set.isSubset", "Set.isSubset(a: Set[T], b: Set[T]): Bool"),
    (
        "Set.isDisjoint",
        "Set.isDisjoint(a: Set[T], b: Set[T]): Bool",
    ),
    (
        "Set.filter",
        "Set.filter(s: Set[T], pred: T => Bool): Set[T]",
    ),
    ("Set.map", "Set.map(s: Set[T], f: T => K): Set[K]"),
    ("Set.exists", "Set.exists(s: Set[T], pred: T => Bool): Bool"),
    ("Set.forall", "Set.forall(s: Set[T], pred: T => Bool): Bool"),
    ("Fs.read", "Fs.read(path: String): IO[String]"),
    ("Fs.write", "Fs.write(path: String, body: String): IO[Unit]"),
    ("Fs.list", "Fs.list(path: String): IO[List[String]]"),
    ("Fs.mkdirs", "Fs.mkdirs(path: String): IO[Unit]"),
    (
        "Fs.canonicalize",
        "Fs.canonicalize(path: String): IO[String]",
    ),
    ("Sys.args", "Sys.args(): IO[List[String]]"),
    ("Sys.readLine", "Sys.readLine(): IO[String]"),
    ("Sys.read", "Sys.read(n: Int): IO[String]"),
    ("Sys.write", "Sys.write(s: String): IO[Unit]"),
    ("Sys.exec", "Sys.exec(cmd: String): IO[Int]"),
    ("Sys.spawn", "Sys.spawn(cmd: String): IO[Int]"),
    ("Sys.alive", "Sys.alive(pid: Int): IO[Int]"),
    ("Sys.kill", "Sys.kill(pid: Int): IO[Unit]"),
    ("Sys.getenv", "Sys.getenv(name: String): IO[String]"),
    ("Clock.realTime", "Clock.realTime(): IO[Int]"),
    ("Clock.monotonic", "Clock.monotonic(): IO[Int]"),
    ("Random.nextInt", "Random.nextInt(bound: Int): IO[Int]"),
    ("Net.httpGet", "Net.httpGet(url: String): IO[String]"),
    (
        "Net.serveOnce",
        "Net.serveOnce(port: Int, handle: String => IO[String]): IO[Unit]",
    ),
    (
        "Net.serve",
        "Net.serve(port: Int, handle: String => IO[String]): IO[Unit]",
    ),
    ("Impurity.runKit", "Impurity.runKit(): IO[Unit]"),
    (
        "Resource.make",
        "Resource.make(acquire: IO[String], release: String => IO[Unit]): Resource[String]",
    ),
    (
        "Resource.use",
        "Resource.use(r: Resource[String], f: String => IO[T]): IO[T]",
    ),
    ("Stream.emit", "Stream.emit(s: String): Stream[String]"),
    (
        "Stream.emits",
        "Stream.emits(xs: List[String]): Stream[String]",
    ),
    ("Stream.eval", "Stream.eval(io: IO[String]): Stream[String]"),
    (
        "Stream.concat",
        "Stream.concat(a: Stream[String], b: Stream[String]): Stream[String]",
    ),
    (
        "Stream.map",
        "Stream.map(s: Stream[String], f: String => String): Stream[String]",
    ),
    (
        "Stream.evalMap",
        "Stream.evalMap(s: Stream[String], f: String => IO[String]): Stream[String]",
    ),
    (
        "Stream.filter",
        "Stream.filter(s: Stream[String], pred: String => Bool): Stream[String]",
    ),
    (
        "Stream.take",
        "Stream.take(s: Stream[String], n: Int): Stream[String]",
    ),
    (
        "Stream.takeWhile",
        "Stream.takeWhile(s: Stream[String], pred: String => Bool): Stream[String]",
    ),
    (
        "Stream.drop",
        "Stream.drop(s: Stream[String], n: Int): Stream[String]",
    ),
    (
        "Stream.dropWhile",
        "Stream.dropWhile(s: Stream[String], pred: String => Bool): Stream[String]",
    ),
    (
        "Stream.find",
        "Stream.find(s: Stream[String], pred: String => Bool): Stream[String]",
    ),
    (
        "Stream.exists",
        "Stream.exists(s: Stream[String], pred: String => Bool): IO[Bool]",
    ),
    ("Stream.drain", "Stream.drain(s: Stream[String]): IO[Unit]"),
    (
        "Stream.compileToList",
        "Stream.compileToList(s: Stream[String]): IO[List[String]]",
    ),
];

pub(crate) fn kit_sig(callee: &str) -> Option<&'static str> {
    KIT_SIGS.iter().find(|(k, _)| *k == callee).map(|(_, s)| *s)
}

#[cfg(test)]
const TYPED_KIT_CALLEES: &[&str] = &[
    "Str.startsWith",
    "Str.eq",
    "List.isEmpty",
    "Law.force",
    "Law.signalInt",
    "Law.signalStr",
    "Law.signalListLen",
    "Law.signalListAt",
    "Law.a11yHas",
    "Fiber.fork",
    "Fiber.join",
    "Ref.of",
    "Queue.unbounded",
    "Deferred.empty",
    "Resource.make",
    "Stream.emit",
    "Stream.emits",
    "Stream.eval",
    "Stream.concat",
    "Stream.map",
    "Stream.evalMap",
    "Stream.filter",
    "Stream.take",
    "Stream.takeWhile",
    "Stream.drop",
    "Stream.dropWhile",
    "Stream.find",
    "Stream.exists",
    "Stream.drain",
    "View.bindText",
    "View.sized",
    "View.align",
    "View.positioned",
    "View.aspectRatio",
    "View.fraction",
    "View.padding",
    "View.background",
    "View.icon",
    "View.image",
    "View.showWhen",
    "View.textField",
    "Signal.get",
    "Signal.str",
    "Signal.getStr",
    "Signal.setStr",
    "Signal.getList",
    "Signal.setList",
    "Theme.accent",
    "Theme.primary",
    "Theme.muted",
    "Theme.foreground",
    "Fs.list",
    "Fs.mkdirs",
    "Fs.canonicalize",
    "Sys.read",
    "Sys.write",
    "Sys.exec",
    "Sys.spawn",
    "Sys.alive",
    "Sys.kill",
    "Sys.getenv",
    "Random.nextInt",
    "Net.serveOnce",
    "Impurity.runKit",
];

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn hover_src(src: &str, needle: &str) -> String {
        let program = parse_file(src, "Main.scuzz").unwrap();
        let offset = src.find(needle).expect(needle);
        hover_in_source(&program, "Main.scuzz", src, offset)
            .unwrap_or_else(|| panic!("no hover at {needle:?}"))
    }

    #[test]
    fn hovers_def_signature() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let h = hover_src(src, "add");
        assert!(h.contains("def add(n: Int): Int"), "{h}");
    }

    #[test]
    fn hovers_type_alias() {
        let src = r#"
type UserId = Int
def idOf(n: UserId): UserId = n
@main def main: IO[Unit] = IO.println(Str.fromInt(idOf(1)))
"#;
        let h = hover_src(src, "UserId");
        assert!(h.contains("type UserId = Int"), "{h}");
    }

    #[test]
    fn hovers_def_default_arg() {
        let src =
            "def add(n: Int, m: Int = 1): Int = n + m\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let h = hover_src(src, "add");
        assert!(h.contains("m: Int = 1"), "{h}");
    }

    #[test]
    fn hovers_imported_alias() {
        let sources = vec![
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".to_string()),
            (
                "Main.scuzz".into(),
                "import A.tag as fromA\n@main def main: IO[Unit] =\n  IO.println(fromA())\n"
                    .to_string(),
            ),
        ];
        let program = crate::parser::parse_sources(&sources).unwrap();
        let main = &sources[1].1;
        let offset = main.rfind("fromA").unwrap();
        let h = hover_in_source(&program, "Main.scuzz", main, offset).expect("hover alias");
        assert!(h.contains("def tag(): String"), "{h}");
    }

    #[test]
    fn hovers_record_copy() {
        let src = r#"
record Point(x: Int, y: Int)
@main def main: IO[Unit] = IO.println(Str.fromInt(Point(3, 5).copy(y = 9).x))
"#;
        let h = hover_src(src, "copy");
        assert!(h.contains("record.copy"), "{h}");
    }

    #[test]
    fn hovers_param_in_body() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let offset = src.find("= n").unwrap() + 2;
        let program = parse_file(src, "Main.scuzz").unwrap();
        let h = hover_in_source(&program, "Main.scuzz", src, offset).unwrap();
        assert!(h.contains("n: Int"), "{h}");
    }

    #[test]
    fn hovers_io_println() {
        let src = "@main def main: IO[Unit] = IO.println(\"x\")\n";
        let h = hover_src(src, "println");
        assert!(h.contains("IO.println(s: String): IO[Unit]"), "{h}");
        let offset = src.find("IO.println").unwrap();
        let program = parse_file(src, "Main.scuzz").unwrap();
        let h = hover_in_source(&program, "Main.scuzz", src, offset).unwrap();
        assert!(h.contains("IO.println"), "{h}");
    }

    #[test]
    fn hovers_ui_run() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.text(\"x\"))\n";
        let h = hover_src(src, "Ui.run");
        assert!(h.contains("Ui.run(_ => View): IO[Unit]"), "{h}");
    }

    #[test]
    fn hovers_enum() {
        let src =
            "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let h = hover_src(src, "Color");
        assert!(h.contains("enum Color:"), "{h}");
        assert!(h.contains("Red"), "{h}");
    }

    #[test]
    fn hovers_view_kit_sigs() {
        let calls = [
            ("View.stretch", "View.stretch(View.text(\"x\"))"),
            ("View.wrap", "View.wrap(View.text(\"a\"), View.text(\"b\"))"),
            (
                "View.grid",
                "View.grid(2, View.text(\"a\"), View.text(\"b\"))",
            ),
            ("View.scroll", "View.scroll(View.text(\"x\"))"),
            ("View.scrollH", "View.scrollH(View.text(\"x\"))"),
            ("View.maxSize", "View.maxSize(40, 30, View.text(\"x\"))"),
            ("View.clip", "View.clip(View.text(\"x\"))"),
            ("View.opacity", "View.opacity(50, View.text(\"x\"))"),
            ("View.maxLines", "View.maxLines(2, View.text(\"x\"))"),
            ("View.ellipsis", "View.ellipsis(View.text(\"x\"))"),
            ("View.textColor", "View.textColor(1, View.text(\"x\"))"),
            ("View.gap", "View.gap(0, View.text(\"x\"))"),
            ("View.fontSize", "View.fontSize(16, View.text(\"x\"))"),
            (
                "View.border",
                "View.border(2, Color.rgb(255, 0, 0), View.text(\"x\"))",
            ),
            ("View.radius", "View.radius(8, View.text(\"x\"))"),
            ("View.ignorePointer", "View.ignorePointer(View.text(\"x\"))"),
            ("View.absorbPointer", "View.absorbPointer(View.text(\"x\"))"),
            (
                "View.excludeSemantics",
                "View.excludeSemantics(View.text(\"x\"))",
            ),
            (
                "View.unconstrainedBox",
                "View.unconstrainedBox(View.text(\"x\"))",
            ),
        ];
        for (callee, call) in calls {
            let needle = callee.rsplit('.').next().unwrap();
            let src = format!("@main def main: IO[Unit] =\n  Ui.run(_ => {call})\n");
            let h = hover_src(&src, needle);
            let sig = kit_sig(callee).expect(callee);
            assert!(h.contains(sig), "{callee}: {h}");
        }
    }

    #[test]
    fn hovers_added_kit_sigs() {
        let view_calls = [
            ("View.sized", "View.sized(40, 30, View.text(\"x\"))"),
            ("View.align", "View.align(0, 1, View.text(\"x\"))"),
            ("View.positioned", "View.positioned(4, 8, View.text(\"x\"))"),
            (
                "View.aspectRatio",
                "View.aspectRatio(16, 9, View.text(\"x\"))",
            ),
            ("View.fraction", "View.fraction(50, 0, View.text(\"x\"))"),
            ("View.padding", "View.padding(8, View.text(\"x\"))"),
            (
                "View.background",
                "View.background(Color.rgb(1, 2, 3), View.text(\"x\"))",
            ),
            ("View.icon", "View.icon(43, Theme.accent())"),
            (
                "View.image",
                "View.image(24, 24, Color.rgb(1, 2, 3), \"a\")",
            ),
            (
                "View.textField",
                "View.textField(Signal.str(\"\"), \"hint\")",
            ),
        ];
        for (callee, call) in view_calls {
            let needle = callee.rsplit('.').next().unwrap();
            let src = format!("@main def main: IO[Unit] =\n  Ui.run(_ => {call})\n");
            let h = hover_src(&src, needle);
            let sig = kit_sig(callee).expect(callee);
            assert!(h.contains(sig), "{callee}: {h}");
        }
        let plain_calls = [
            ("Theme.primary", "Theme.primary()"),
            ("Theme.muted", "Theme.muted()"),
            ("Theme.foreground", "Theme.foreground()"),
            ("Fs.list", "Fs.list(\".\")"),
            ("Fs.mkdirs", "Fs.mkdirs(\"a\")"),
            ("Fs.canonicalize", "Fs.canonicalize(\".\")"),
            ("Sys.read", "Sys.read(4)"),
            ("Sys.write", "Sys.write(\"x\")"),
            ("Sys.exec", "Sys.exec(\"ls\")"),
            ("Sys.spawn", "Sys.spawn(\"ls\")"),
            ("Sys.alive", "Sys.alive(1)"),
            ("Sys.kill", "Sys.kill(1)"),
            ("Sys.getenv", "Sys.getenv(\"HOME\")"),
            ("Random.nextInt", "Random.nextInt(10)"),
            ("Net.serveOnce", "Net.serveOnce(8080, s => IO.pure(s))"),
            ("Impurity.runKit", "Impurity.runKit()"),
            ("Signal.getStr", "Signal.getStr(Signal.str(\"x\"))"),
            ("Signal.setStr", "Signal.setStr(Signal.str(\"x\"), \"y\")"),
            ("Signal.getList", "Signal.getList(Signal.list([\"a\"]))"),
            (
                "Signal.setList",
                "Signal.setList(Signal.list([\"a\"]), [\"b\"])",
            ),
            ("Law.signalInt", "Law.signalInt(0)"),
            ("Law.signalStr", "Law.signalStr(0)"),
            ("Law.signalListLen", "Law.signalListLen(0)"),
            ("Law.signalListAt", "Law.signalListAt(0, 0)"),
            ("Law.a11yHas", "Law.a11yHas(\"x\")"),
            ("Stream.emits", "Stream.emits([\"a\"])"),
            ("Stream.eval", "Stream.eval(IO.pure(\"a\"))"),
            (
                "Stream.concat",
                "Stream.concat(Stream.emit(\"a\"), Stream.emit(\"b\"))",
            ),
            ("Stream.map", "Stream.map(Stream.emit(\"a\"), s => s)"),
            (
                "Stream.evalMap",
                "Stream.evalMap(Stream.emit(\"a\"), s => IO.pure(s))",
            ),
            (
                "Stream.filter",
                "Stream.filter(Stream.emit(\"a\"), s => true)",
            ),
            ("Stream.take", "Stream.take(Stream.emit(\"a\"), 1)"),
            (
                "Stream.takeWhile",
                "Stream.takeWhile(Stream.emit(\"a\"), s => true)",
            ),
            ("Stream.drop", "Stream.drop(Stream.emit(\"a\"), 1)"),
            (
                "Stream.dropWhile",
                "Stream.dropWhile(Stream.emit(\"a\"), s => true)",
            ),
            ("Stream.find", "Stream.find(Stream.emit(\"a\"), s => true)"),
            (
                "Stream.exists",
                "Stream.exists(Stream.emit(\"a\"), s => true)",
            ),
            ("Stream.drain", "Stream.drain(Stream.emit(\"a\"))"),
        ];
        for (callee, call) in plain_calls {
            let needle = callee.rsplit('.').next().unwrap();
            let src = format!("@main def main: IO[Unit] = {call}\n");
            let h = hover_src(&src, needle);
            let sig = kit_sig(callee).expect(callee);
            assert!(h.contains(sig), "{callee}: {h}");
        }
    }

    #[test]
    fn hovers_view_checkbox() {
        let src = r#"@main def main: IO[Unit] =
  for {
    c = Signal.int(0)
    _ <- Ui.run(_ => View.checkbox(c, "Done"))
  } yield ()
"#;
        let h = hover_src(src, "checkbox");
        assert!(
            h.contains("View.checkbox(sig: SignalInt, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_radio() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radio(n, 1, "On"))
  } yield ()
"#;
        let h = hover_src(src, "radio");
        assert!(
            h.contains("View.radio(sig: SignalInt, value: Int, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_slider() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.slider(n))
  } yield ()
"#;
        let h = hover_src(src, "slider");
        assert!(h.contains("View.slider(sig: SignalInt): View"), "{h}");
    }

    #[test]
    fn hovers_view_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.progress(n))
  } yield ()
"#;
        let h = hover_src(src, "progress");
        assert!(h.contains("View.progress(sig: SignalInt): View"), "{h}");
    }

    #[test]
    fn hovers_view_circular_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.circularProgress(n))
  } yield ()
"#;
        let h = hover_src(src, "circularProgress");
        assert!(
            h.contains("View.circularProgress(sig: SignalInt): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_avatar() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.avatar("S"))
"#;
        let h = hover_src(src, "avatar");
        assert!(h.contains("View.avatar(label: String): View"), "{h}");
    }

    #[test]
    fn hovers_view_switch() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switch(n, "On"))
  } yield ()
"#;
        let h = hover_src(src, "switch");
        assert!(
            h.contains("View.switch(sig: SignalInt, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.chip(n, "Pin"))
  } yield ()
"#;
        let h = hover_src(src, "chip");
        assert!(
            h.contains("View.chip(sig: SignalInt, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_filter_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.filterChip(n, "Tag"))
  } yield ()
"#;
        let h = hover_src(src, "filterChip");
        assert!(
            h.contains("View.filterChip(sig: SignalInt, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_choice_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.choiceChip(n, 0, "Day"))
  } yield ()
"#;
        let h = hover_src(src, "choiceChip");
        assert!(
            h.contains("View.choiceChip(sig: SignalInt, value: Int, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_action_chip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.actionChip("Go", _ => ()))
"#;
        let h = hover_src(src, "actionChip");
        assert!(
            h.contains("View.actionChip(label: String, onTap: _ => Unit): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_input_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.inputChip(n, "In"))
  } yield ()
"#;
        let h = hover_src(src, "inputChip");
        assert!(
            h.contains("View.inputChip(sig: SignalInt, label: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("milk", View.button("Del", _ => ())))
"#;
        let h = hover_src(src, "listTile");
        assert!(
            h.contains("View.listTile(title: String, trailing: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_checkbox_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.checkboxListTile(n, "Star"))
  } yield ()
"#;
        let h = hover_src(src, "checkboxListTile");
        assert!(
            h.contains("View.checkboxListTile(sig: SignalInt, title: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_switch_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switchListTile(n, "Quiet"))
  } yield ()
"#;
        let h = hover_src(src, "switchListTile");
        assert!(
            h.contains("View.switchListTile(sig: SignalInt, title: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_radio_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radioListTile(n, 1, "Night"))
  } yield ()
"#;
        let h = hover_src(src, "radioListTile");
        assert!(
            h.contains("View.radioListTile(sig: SignalInt, value: Int, title: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_segmented() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.segmented(n, "List", "Grid"))
  } yield ()
"#;
        let h = hover_src(src, "segmented");
        assert!(
            h.contains("View.segmented(sig: SignalInt, left: String, right: String): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_fab() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fab("+", _ => ()))
"#;
        let h = hover_src(src, "fab");
        assert!(
            h.contains("View.fab(label: String, onTap: _ => Unit): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_outlined_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.outlinedButton("Edit", _ => ()))
"#;
        let h = hover_src(src, "outlinedButton");
        assert!(
            h.contains("View.outlinedButton(label: String, onTap: _ => Unit): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_text_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textButton("Open", _ => ()))
"#;
        let h = hover_src(src, "textButton");
        assert!(
            h.contains("View.textButton(label: String, onTap: _ => Unit): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_tooltip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.tooltip("Sean", View.avatar("S")))
"#;
        let h = hover_src(src, "tooltip");
        assert!(
            h.contains("View.tooltip(message: String, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_placeholder() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.placeholder(View.avatar("S")))
"#;
        let h = hover_src(src, "placeholder");
        assert!(h.contains("View.placeholder(child: View): View"), "{h}");
    }

    #[test]
    fn hovers_view_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.semantics("mark", View.avatar("S")))
"#;
        let h = hover_src(src, "semantics");
        assert!(
            h.contains("View.semantics(label: String, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_merge_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.mergeSemantics("logo", View.avatar("S")))
"#;
        let h = hover_src(src, "mergeSemantics");
        assert!(
            h.contains("View.mergeSemantics(label: String, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_ink_well() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.inkWell("face", _ => (), View.avatar("S")))
"#;
        let h = hover_src(src, "inkWell");
        assert!(
            h.contains("View.inkWell(label: String, onTap: _ => Unit, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_visibility() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.visibility(n, View.avatar("S")))
  } yield ()
"#;
        let h = hover_src(src, "visibility");
        assert!(
            h.contains("View.visibility(sig: SignalInt, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_offstage() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.offstage(n, View.avatar("S")))
  } yield ()
"#;
        let h = hover_src(src, "offstage");
        assert!(
            h.contains("View.offstage(sig: SignalInt, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_unconstrained_box() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.unconstrainedBox(View.avatar("S")))
"#;
        let h = hover_src(src, "unconstrainedBox");
        assert!(
            h.contains("View.unconstrainedBox(child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_badge() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(3)
    _ <- Ui.run(_ => View.badge(n, View.text("x")))
  } yield ()
"#;
        let h = hover_src(src, "badge");
        assert!(
            h.contains("View.badge(sig: SignalInt, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_card() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.card(View.text("x")))
"#;
        let h = hover_src(src, "card");
        assert!(h.contains("View.card(child: View): View"), "{h}");
    }

    #[test]
    fn hovers_view_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.divider())
"#;
        let h = hover_src(src, "divider");
        assert!(h.contains("View.divider(): View"), "{h}");
    }

    #[test]
    fn hovers_view_vertical_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.verticalDivider())
"#;
        let h = hover_src(src, "verticalDivider");
        assert!(h.contains("View.verticalDivider(): View"), "{h}");
    }

    #[test]
    fn hovers_view_expansion_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.expansionTile(n, "More", View.text("x")))
  } yield ()
"#;
        let h = hover_src(src, "expansionTile");
        assert!(
            h.contains("View.expansionTile(sig: SignalInt, title: String, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_icon_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.iconButton("i", _ => ()))
"#;
        let h = hover_src(src, "iconButton");
        assert!(
            h.contains("View.iconButton(label: String, onTap: _ => Unit): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_each() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => View.text(s)))
  } yield ()
"#;
        let h = hover_src(src, "each");
        assert!(
            h.contains("View.each(items: SignalList, row: String => View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_filter() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.filter(["a", "b"], x => x != "b"), ","))
"#;
        let h = hover_src(src, "filter");
        assert!(
            h.contains("List.filter(xs: List[T], pred: T => Bool): List[T]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_filter_not() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.filterNot(["a", "b"], x => x == "b"), ","))
"#;
        let h = hover_src(src, "filterNot");
        assert!(
            h.contains("List.filterNot(xs: List[T], pred: T => Bool): List[T]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_map() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(["a"], x => Str.concat(x, "!")), ","))
"#;
        let h = hover_src(src, "map");
        assert!(
            h.contains("List.map(xs: List[T], f: T => U): List[U]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_flat_map() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.flatMap(["a"], x => [x, x]), ","))
"#;
        let h = hover_src(src, "flatMap");
        assert!(
            h.contains("List.flatMap(xs: List[T], f: T => List[U]): List[U]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_range_tabulate_intersperse() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(List.range(1, 4))))
"#;
        let h = hover_src(src, "range");
        assert!(
            h.contains("List.range(from: Int, until: Int): List[Int]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.tabulate(2, i => Str.fromInt(i)), ","))
"#;
        let h = hover_src(src, "tabulate");
        assert!(
            h.contains("List.tabulate(n: Int, f: Int => T): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.intersperse(["a", "b"], "|"), ","))
"#;
        let h = hover_src(src, "intersperse");
        assert!(
            h.contains("List.intersperse(xs: List[T], x: T): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.grouped(["a", "b"], 1), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "grouped");
        assert!(
            h.contains("List.grouped(xs: List[T], n: Int): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.sliding(["a", "b"], 1), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "sliding");
        assert!(
            h.contains("List.sliding(xs: List[T], n: Int): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.slice(["a", "b", "c"], 1, 3), ","))
"#;
        let h = hover_src(src, "slice");
        assert!(
            h.contains("List.slice(xs: List[T], from: Int, until: Int): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.indices(["a", "b"]), n => Str.fromInt(n)), ","))
"#;
        let h = hover_src(src, "indices");
        assert!(h.contains("List.indices(xs: List[T]): List[Int]"), "{h}");
    }

    #[test]
    fn hovers_list_set_at() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.setAt(["a"], 0, "b"), ","))
"#;
        let h = hover_src(src, "setAt");
        assert!(
            h.contains("List.setAt(xs: List[T], i: Int, v: T): List[T]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_take() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.take(["a", "b"], 1), ","))
"#;
        let h = hover_src(src, "take");
        assert!(h.contains("List.take(xs: List[T], n: Int): List[T]"), "{h}");
    }

    #[test]
    fn hovers_list_take_right_init_last() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.takeRight(["a", "b"], 1), ","))
"#;
        let h = hover_src(src, "takeRight");
        assert!(
            h.contains("List.takeRight(xs: List[T], n: Int): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.dropRight(["a", "b"], 1), ","))
"#;
        let h = hover_src(src, "dropRight");
        assert!(
            h.contains("List.dropRight(xs: List[T], n: Int): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.init(["a", "b"]), ","))
"#;
        let h = hover_src(src, "init");
        assert!(h.contains("List.init(xs: List[T]): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.last(["a", "b"]), ","))
"#;
        let h = hover_src(src, "last");
        assert!(h.contains("List.last(xs: List[T]): List[T]"), "{h}");
    }

    #[test]
    fn hovers_list_get_or_else_fill() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.getOrElse(["a", "b"], 0, "z"))
"#;
        let h = hover_src(src, "getOrElse");
        assert!(
            h.contains("List.getOrElse(xs: List[T], i: Int, default: T): T"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.fill(2, "a"), ","))
"#;
        let h = hover_src(src, "fill");
        assert!(h.contains("List.fill(n: Int, x: T): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.padTo(["a"], 2, "z"), ","))
"#;
        let h = hover_src(src, "padTo");
        assert!(
            h.contains("List.padTo(xs: List[T], n: Int, x: T): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.reverse(["a", "b"]), ","))
"#;
        let h = hover_src(src, "reverse");
        assert!(h.contains("List.reverse(xs: List[T]): List[T]"), "{h}");
    }

    #[test]
    fn hovers_list_exists() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.exists(["a"], x => true)) "y" else "n")
"#;
        let h = hover_src(src, "exists");
        assert!(
            h.contains("List.exists(xs: List[T], pred: T => Bool): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.indexWhere(["a", "b"], x => x == "b")))
"#;
        let h = hover_src(src, "indexWhere");
        assert!(
            h.contains("List.indexWhere(xs: List[T], pred: T => Bool): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.lastIndexWhere(["a", "b"], x => x == "b")))
"#;
        let h = hover_src(src, "lastIndexWhere");
        assert!(
            h.contains("List.lastIndexWhere(xs: List[T], pred: T => Bool): Int"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_count() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.count(["a", "b"], x => x != "b")))
"#;
        let h = hover_src(src, "count");
        assert!(
            h.contains("List.count(xs: List[T], pred: T => Bool): Int"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_take_while() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.takeWhile(["a"], x => true), ","))
"#;
        let h = hover_src(src, "takeWhile");
        assert!(
            h.contains("List.takeWhile(xs: List[T], pred: T => Bool): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.span(["a", "b"], x => x != "b"), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "span");
        assert!(
            h.contains("List.span(xs: List[T], pred: T => Bool): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.partition(["a", "b"], x => x == "a"), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "partition");
        assert!(
            h.contains("List.partition(xs: List[T], pred: T => Bool): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.splitAt(["a", "b"], 1), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "splitAt");
        assert!(
            h.contains("List.splitAt(xs: List[T], n: Int): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.inits(["a", "b"]), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "inits");
        assert!(h.contains("List.inits(xs: List[T]): List[List[T]]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.tails(["a", "b"]), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "tails");
        assert!(h.contains("List.tails(xs: List[T]): List[List[T]]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.zip(["a"], ["1"]), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "zip");
        assert!(
            h.contains("List.zip(xs: List[T], ys: List[T]): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.zipAll(["a"], ["1"], "z", "9"), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "zipAll");
        assert!(
            h.contains("List.zipAll(xs: List[T], ys: List[T], x: T, y: T): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.unzip(List.zip(["a"], ["1"])), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "unzip");
        assert!(
            h.contains("List.unzip(pairs: List[List[T]]): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(List.transpose([["a", "b"], ["c", "d"]]), g => List.join(g, ",")), "|"))
"#;
        let h = hover_src(src, "transpose");
        assert!(
            h.contains("List.transpose(xss: List[List[T]]): List[List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.contains(["a", "b"], "b")) "y" else "n")
"#;
        let h = hover_src(src, "contains");
        assert!(h.contains("List.contains(xs: List[T], x: T): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.indexOf(["a", "b"], "b")))
"#;
        let h = hover_src(src, "indexOf");
        assert!(h.contains("List.indexOf(xs: List[T], x: T): Int"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.lastIndexOf(["a", "a"], "a")))
"#;
        let h = hover_src(src, "lastIndexOf");
        assert!(
            h.contains("List.lastIndexOf(xs: List[T], x: T): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.distinct(["a", "a"]), ","))
"#;
        let h = hover_src(src, "distinct");
        assert!(h.contains("List.distinct(xs: List[T]): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.diff(["a", "b"], ["b"]), ","))
"#;
        let h = hover_src(src, "diff");
        assert!(
            h.contains("List.diff(xs: List[T], ys: List[T]): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.intersect(["a", "b"], ["b"]), ","))
"#;
        let h = hover_src(src, "intersect");
        assert!(
            h.contains("List.intersect(xs: List[T], ys: List[T]): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.startsWith(["a", "b"], ["a"])) "y" else "n")
"#;
        let h = hover_src(src, "startsWith");
        assert!(
            h.contains("List.startsWith(xs: List[T], prefix: List[T]): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.endsWith(["a", "b"], ["b"])) "y" else "n")
"#;
        let h = hover_src(src, "endsWith");
        assert!(
            h.contains("List.endsWith(xs: List[T], suffix: List[T]): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.sameElements(["a"], ["a"])) "y" else "n")
"#;
        let h = hover_src(src, "sameElements");
        assert!(
            h.contains("List.sameElements(xs: List[T], ys: List[T]): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.patch(["a", "b", "c"], 1, ["x"], 1), ","))
"#;
        let h = hover_src(src, "patch");
        assert!(
            h.contains(
                "List.patch(xs: List[T], from: Int, other: List[T], replaced: Int): List[T]"
            ),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.findLast(["a", "b", "a"], x => x == "a"), ","))
"#;
        let h = hover_src(src, "findLast");
        assert!(
            h.contains("List.findLast(xs: List[T], pred: T => Bool): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.prefixLength(["a", "a", "b"], x => x == "a")))
"#;
        let h = hover_src(src, "prefixLength");
        assert!(
            h.contains("List.prefixLength(xs: List[T], pred: T => Bool): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.indexOfSlice(["a", "b"], ["b"])))
"#;
        let h = hover_src(src, "indexOfSlice");
        assert!(
            h.contains("List.indexOfSlice(xs: List[T], slice: List[T]): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.lastIndexOfSlice(["a", "b", "a", "b"], ["a", "b"])))
"#;
        let h = hover_src(src, "lastIndexOfSlice");
        assert!(
            h.contains("List.lastIndexOfSlice(xs: List[T], slice: List[T]): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.segmentLength(["a", "a", "b"], x => x == "a", 0)))
"#;
        let h = hover_src(src, "segmentLength");
        assert!(
            h.contains("List.segmentLength(xs: List[T], pred: T => Bool, from: Int): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.isDefinedAt(["a"], 0)) "y" else "n")
"#;
        let h = hover_src(src, "isDefinedAt");
        assert!(
            h.contains("List.isDefinedAt(xs: List[T], i: Int): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.lengthCompare(["a", "b"], 2)))
"#;
        let h = hover_src(src, "lengthCompare");
        assert!(
            h.contains("List.lengthCompare(xs: List[T], n: Int): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.sort(["b", "a"]), ","))
"#;
        let h = hover_src(src, "sort");
        assert!(h.contains("List.sort(xs: List[T]): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.sortBy(["bb", "a"], x => Str.len(x)), ","))
"#;
        let h = hover_src(src, "sortBy");
        assert!(
            h.contains("List.sortBy(xs: List[T], f: T => Int): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.max(["a", "c"]))
"#;
        let h = hover_src(src, "max");
        assert!(h.contains("List.max(xs: List[T]): T"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.maxBy(["bb", "a"], x => Str.len(x)))
"#;
        let h = hover_src(src, "maxBy");
        assert!(h.contains("List.maxBy(xs: List[T], f: T => Int): T"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.map(Map.keys(List.groupBy(["aa", "b"], x => Str.len(x))), n => Str.fromInt(n)), ","))
"#;
        let h = hover_src(src, "groupBy");
        assert!(
            h.contains("List.groupBy(xs: List[T], f: T => K): Map[K, List[T]]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.sum([1, 2])))
"#;
        let h = hover_src(src, "sum");
        assert!(h.contains("List.sum(xs: List[Int]): Int"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.product([2, 3])))
"#;
        let h = hover_src(src, "product");
        assert!(h.contains("List.product(xs: List[Int]): Int"), "{h}");
    }

    #[test]
    fn hovers_list_forall() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.forall(["a"], x => true)) "y" else "n")
"#;
        let h = hover_src(src, "forall");
        assert!(
            h.contains("List.forall(xs: List[T], pred: T => Bool): Bool"),
            "{h}"
        );
    }

    #[test]
    fn hovers_str_starts_with() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.startsWith("ab", "a")) "y" else "n")
"#;
        let h = hover_src(src, "startsWith");
        assert!(
            h.contains("Str.startsWith(s: String, prefix: String): Bool"),
            "{h}"
        );
    }

    #[test]
    fn hovers_str_contains() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.contains("ab", "b")) "y" else "n")
"#;
        let h = hover_src(src, "contains");
        assert!(
            h.contains("Str.contains(s: String, needle: String): Bool"),
            "{h}"
        );
    }

    #[test]
    fn hovers_str_to_int() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Str.toInt("7", 0)))
"#;
        let h = hover_src(src, "toInt");
        assert!(h.contains("Str.toInt(s: String, default: Int): Int"), "{h}");
    }

    #[test]
    fn hovers_str_trim() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.trim("  x  "))
"#;
        let h = hover_src(src, "trim");
        assert!(h.contains("Str.trim(s: String): String"), "{h}");
    }

    #[test]
    fn hovers_str_split() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Str.split("a,b", ","), ":"))
"#;
        let h = hover_src(src, "split");
        assert!(
            h.contains("Str.split(s: String, sep: String): List[String]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_str_is_empty_case_repeat() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.isEmpty("")) "y" else "n")
"#;
        let h = hover_src(src, "isEmpty");
        assert!(h.contains("Str.isEmpty(s: String): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.nonEmpty("a")) "y" else "n")
"#;
        let h = hover_src(src, "nonEmpty");
        assert!(h.contains("Str.nonEmpty(s: String): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.toLower("Ab"))
"#;
        let h = hover_src(src, "toLower");
        assert!(h.contains("Str.toLower(s: String): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.toUpper("Ab"))
"#;
        let h = hover_src(src, "toUpper");
        assert!(h.contains("Str.toUpper(s: String): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.repeat("a", 3))
"#;
        let h = hover_src(src, "repeat");
        assert!(h.contains("Str.repeat(s: String, n: Int): String"), "{h}");
    }

    #[test]
    fn hovers_str_strip_pad_blank() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.stripPrefix("abc", "a"))
"#;
        let h = hover_src(src, "stripPrefix");
        assert!(
            h.contains("Str.stripPrefix(s: String, prefix: String): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.stripSuffix("abc", "c"))
"#;
        let h = hover_src(src, "stripSuffix");
        assert!(
            h.contains("Str.stripSuffix(s: String, suffix: String): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.padLeft("a", 3, "x"))
"#;
        let h = hover_src(src, "padLeft");
        assert!(
            h.contains("Str.padLeft(s: String, n: Int, pad: String): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.padRight("a", 3, "x"))
"#;
        let h = hover_src(src, "padRight");
        assert!(
            h.contains("Str.padRight(s: String, n: Int, pad: String): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.isBlank(" ")) "y" else "n")
"#;
        let h = hover_src(src, "isBlank");
        assert!(h.contains("Str.isBlank(s: String): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Str.lastIndexOf("ababa", "ba")))
"#;
        let h = hover_src(src, "lastIndexOf");
        assert!(
            h.contains("Str.lastIndexOf(s: String, needle: String): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.take("abc", 2))
"#;
        let h = hover_src(src, "take");
        assert!(h.contains("Str.take(s: String, n: Int): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.drop("abc", 1))
"#;
        let h = hover_src(src, "drop");
        assert!(h.contains("Str.drop(s: String, n: Int): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.takeRight("abc", 2))
"#;
        let h = hover_src(src, "takeRight");
        assert!(
            h.contains("Str.takeRight(s: String, n: Int): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.dropRight("abc", 1))
"#;
        let h = hover_src(src, "dropRight");
        assert!(
            h.contains("Str.dropRight(s: String, n: Int): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.reverse("abc"))
"#;
        let h = hover_src(src, "reverse");
        assert!(h.contains("Str.reverse(s: String): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.capitalize("hello"))
"#;
        let h = hover_src(src, "capitalize");
        assert!(h.contains("Str.capitalize(s: String): String"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Str.len("abc")))
"#;
        let h = hover_src(src, "len");
        assert!(h.contains("Str.len(s: String): Int"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Str.charAt("abc", 1)))
"#;
        let h = hover_src(src, "charAt");
        assert!(h.contains("Str.charAt(s: String, i: Int): Int"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(Str.indexOf("ababa", "ba")))
"#;
        let h = hover_src(src, "indexOf");
        assert!(
            h.contains("Str.indexOf(s: String, needle: String): Int"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.slice("abc", 1, 3))
"#;
        let h = hover_src(src, "slice");
        assert!(
            h.contains("Str.slice(s: String, start: Int, end: Int): String"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Str.lines("a\nb"), ","))
"#;
        let h = hover_src(src, "lines");
        assert!(h.contains("Str.lines(s: String): List[String]"), "{h}");
    }

    #[test]
    fn hovers_list_empty_head_tail_len_at_join() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(List.empty())))
"#;
        let h = hover_src(src, "empty");
        assert!(h.contains("List.empty(): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.nonEmpty(["a"])) "y" else "n")
"#;
        let h = hover_src(src, "nonEmpty");
        assert!(h.contains("List.nonEmpty(xs: List[T]): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.head(["a"]))
"#;
        let h = hover_src(src, "head");
        assert!(h.contains("List.head(xs: List[T]): T"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.tail(["a", "b"]), ","))
"#;
        let h = hover_src(src, "tail");
        assert!(h.contains("List.tail(xs: List[T]): List[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(["a"])))
"#;
        let h = hover_src(src, "len");
        assert!(h.contains("List.len(xs: List[T]): Int"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.at(["a", "b"], 1))
"#;
        let h = hover_src(src, "at");
        assert!(h.contains("List.at(xs: List[T], i: Int): T"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(["a", "b"], ","))
"#;
        let h = hover_src(src, "join");
        assert!(
            h.contains("List.join(xs: List[String], sep: String): String"),
            "{h}"
        );
    }

    #[test]
    fn hovers_map_values() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.set(Map.empty(), "a", "1")), ","))
"#;
        let h = hover_src(src, "values");
        assert!(h.contains("Map.values(m: Map[K, V]): List[V]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.get(Map.set(Map.empty(), "a", "1"), "a"), ","))
"#;
        let h = hover_src(src, "get");
        assert!(h.contains("Map.get(m: Map[K, V], k: K): List[V]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Map.isEmpty(Map.empty())) "y" else "n")
"#;
        let h = hover_src(src, "isEmpty");
        assert!(h.contains("Map.isEmpty(m: Map[K, V]): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Set.isEmpty(Set.empty())) "y" else "n")
"#;
        let h = hover_src(src, "isEmpty");
        assert!(h.contains("Set.isEmpty(s: Set[T]): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Map.nonEmpty(Map.set(Map.empty(), "a", "1"))) "y" else "n")
"#;
        let h = hover_src(src, "nonEmpty");
        assert!(h.contains("Map.nonEmpty(m: Map[K, V]): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Set.nonEmpty(Set.add(Set.empty(), "x"))) "y" else "n")
"#;
        let h = hover_src(src, "nonEmpty");
        assert!(h.contains("Set.nonEmpty(s: Set[T]): Bool"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Set.toList(Set.union(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "y"))), ","))
"#;
        let h = hover_src(src, "union");
        assert!(h.contains("Set.union(a: Set[T], b: Set[T]): Set[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Set.toList(Set.intersect(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "x"))), ","))
"#;
        let h = hover_src(src, "intersect");
        assert!(
            h.contains("Set.intersect(a: Set[T], b: Set[T]): Set[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Set.toList(Set.diff(Set.add(Set.empty(), "x"), Set.empty())), ","))
"#;
        let h = hover_src(src, "diff");
        assert!(h.contains("Set.diff(a: Set[T], b: Set[T]): Set[T]"), "{h}");
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.union(Map.set(Map.empty(), "a", "1"), Map.set(Map.empty(), "b", "2"))), ","))
"#;
        let h = hover_src(src, "union");
        assert!(
            h.contains("Map.union(a: Map[K, V], b: Map[K, V]): Map[K, V]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.intersect(Map.set(Map.empty(), "a", "1"), Map.set(Map.empty(), "a", "9"))), ","))
"#;
        let h = hover_src(src, "intersect");
        assert!(
            h.contains("Map.intersect(a: Map[K, V], b: Map[K, V]): Map[K, V]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.diff(Map.set(Map.empty(), "a", "1"), Map.empty())), ","))
"#;
        let h = hover_src(src, "diff");
        assert!(
            h.contains("Map.diff(a: Map[K, V], b: Map[K, V]): Map[K, V]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.filter(Map.set(Map.empty(), "a", "1"), v => v != "z")), ","))
"#;
        let h = hover_src(src, "filter");
        assert!(
            h.contains("Map.filter(m: Map[K, V], pred: V => Bool): Map[K, V]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.mapValues(Map.set(Map.empty(), "a", "1"), v => v)), ","))
"#;
        let h = hover_src(src, "mapValues");
        assert!(
            h.contains("Map.mapValues(m: Map[K, V], f: V => W): Map[K, W]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Map.exists(Map.set(Map.empty(), "a", "1"), v => v == "1")) "y" else "n")
"#;
        let h = hover_src(src, "exists");
        assert!(
            h.contains("Map.exists(m: Map[K, V], pred: V => Bool): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Map.forall(Map.set(Map.empty(), "a", "1"), v => v != "z")) "y" else "n")
"#;
        let h = hover_src(src, "forall");
        assert!(
            h.contains("Map.forall(m: Map[K, V], pred: V => Bool): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Set.isSubset(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "x"))) "y" else "n")
"#;
        let h = hover_src(src, "isSubset");
        assert!(
            h.contains("Set.isSubset(a: Set[T], b: Set[T]): Bool"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Set.isDisjoint(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "y"))) "y" else "n")
"#;
        let h = hover_src(src, "isDisjoint");
        assert!(
            h.contains("Set.isDisjoint(a: Set[T], b: Set[T]): Bool"),
            "{h}"
        );
    }

    #[test]
    fn hovers_list_concat_and_flatten() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.concat(["a"], ["b"]), ","))
"#;
        let h = hover_src(src, "concat");
        assert!(
            h.contains("List.concat(xs: List[T], ys: List[T]): List[T]"),
            "{h}"
        );
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.flatten(List.append(List.empty(), ["a"])), ","))
"#;
        let h = hover_src(src, "flatten");
        assert!(
            h.contains("List.flatten(xss: List[List[T]]): List[T]"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_each_row_param() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, row => View.text(row)))
  } yield ()
"#;
        let offset = src.find("View.text(row)").unwrap() + "View.text(".len();
        let program = parse_file(src, "Main.scuzz").unwrap();
        let h = hover_in_source(&program, "Main.scuzz", src, offset).unwrap();
        assert!(h.contains("row: String"), "{h}");
    }

    #[test]
    fn hovers_signal_map_param() {
        let src = r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => Str.fromInt(n))
    _ <- Ui.run(_ => View.bindText(label))
  } yield ()
"#;
        let offset = src.find("Str.fromInt(n)").unwrap() + "Str.fromInt(".len();
        let program = parse_file(src, "Main.scuzz").unwrap();
        let h = hover_in_source(&program, "Main.scuzz", src, offset).unwrap();
        assert!(h.contains("n: Int"), "{h}");
    }

    #[test]
    fn hovers_color_rgba() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.background(Color.rgba(1, 2, 3, 4), View.text(\"x\")))\n";
        let h = hover_src(src, "rgba");
        assert!(
            h.contains("Color.rgba(r: Int, g: Int, b: Int, a: Int): Int"),
            "{h}"
        );
    }

    #[test]
    fn kit_sigs_are_unique_and_cover_typed_kits() {
        let mut seen = std::collections::BTreeSet::new();
        for (k, _) in KIT_SIGS {
            assert!(seen.insert(*k), "duplicate kit sig {k}");
        }
        for name in TYPED_KIT_CALLEES {
            assert!(kit_sig(name).is_some(), "missing kit sig for {name}");
        }
        assert_eq!(
            kit_sig("View.bindText"),
            Some("View.bindText(s: SignalStr): View")
        );
    }
}

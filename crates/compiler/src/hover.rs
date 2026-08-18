//! Signature hover from the same parse as `check`. No second typer.

use crate::ast::{EnumDef, Expr, ExprKind, FunDef, Param, Program, Type};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use crate::typ::kit_lambda_param_ty;

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
        return None;
    }
    if matches!(
        name.as_str(),
        "Int" | "Float" | "String" | "Bool" | "Unit" | "List" | "Map" | "Set" | "IO"
    ) {
        return Some(name);
    }
    if let Some(d) = def_named(program, &module, &name).or_else(|| unique_def(program, &name)) {
        return Some(show_def(d));
    }
    if let Some(en) = enum_named(program, &module, &name).or_else(|| unique_enum(program, &name)) {
        return Some(show_enum(en));
    }
    if let Some(p) = param_in_module(program, &module, &name, offset) {
        return Some(show_param(p));
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
        } if n == name && covers => {
            consider(best, span_len, n);
            walk_binders(body, name, offset, best);
        }
        _ => expr.for_each_child(|c| walk_binders(c, name, offset, best)),
    }
}

fn consider(best: &mut Option<(usize, String)>, span_len: usize, name: &str) {
    let text = format!("{name}");
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
    if p.rfn.is_some() {
        format!("{}: {} where ...", p.name, p.ty)
    } else {
        format!("{}: {}", p.name, p.ty)
    }
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
    ("IO.both", "IO.both(a: IO[T], b: IO[T]): IO[(T, T)]"),
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
    ("Str.trim", "Str.trim(s: String): String"),
    ("Signal.int", "Signal.int(n: Int): SignalInt"),
    ("Signal.get", "Signal.get(s: SignalInt): Int"),
    ("Signal.set", "Signal.set(s: SignalInt, n: Int): Unit"),
    ("Signal.str", "Signal.str(s: String): SignalStr"),
    (
        "Signal.map",
        "Signal.map(s: SignalInt, f: Int => String): SignalStr",
    ),
    ("Signal.list", "Signal.list(xs: List[T]): SignalList"),
    ("List.isEmpty", "List.isEmpty(xs: List[T]): Bool"),
    ("Str.eq", "Str.eq(a: String, b: String): Bool"),
    (
        "List.filter",
        "List.filter(xs: List[T], pred: T => Bool): List[T]",
    ),
    ("List.map", "List.map(xs: List[T], f: T => U): List[U]"),
    (
        "List.setAt",
        "List.setAt(xs: List[T], i: Int, v: T): List[T]",
    ),
    ("List.take", "List.take(xs: List[T], n: Int): List[T]"),
    ("List.drop", "List.drop(xs: List[T], n: Int): List[T]"),
    (
        "List.find",
        "List.find(xs: List[T], pred: T => Bool): List[T]",
    ),
    (
        "List.exists",
        "List.exists(xs: List[T], pred: T => Bool): Bool",
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
    ("List.cons", "List.cons(x: T, xs: List[T]): List[T]"),
    ("List.append", "List.append(xs: List[T], x: T): List[T]"),
    ("Map.empty", "Map.empty(): Map[K, V]"),
    ("Map.set", "Map.set(m: Map[K, V], k: K, v: V): Map[K, V]"),
    (
        "Map.getOrElse",
        "Map.getOrElse(m: Map[K, V], k: K, default: V): V",
    ),
    ("Map.contains", "Map.contains(m: Map[K, V], k: K): Bool"),
    ("Map.remove", "Map.remove(m: Map[K, V], k: K): Map[K, V]"),
    ("Map.keys", "Map.keys(m: Map[K, V]): List[K]"),
    ("Map.size", "Map.size(m: Map[K, V]): Int"),
    ("Set.empty", "Set.empty(): Set[T]"),
    ("Set.add", "Set.add(s: Set[T], x: T): Set[T]"),
    ("Set.contains", "Set.contains(s: Set[T], x: T): Bool"),
    ("Set.remove", "Set.remove(s: Set[T], x: T): Set[T]"),
    ("Set.toList", "Set.toList(s: Set[T]): List[T]"),
    ("Set.size", "Set.size(s: Set[T]): Int"),
    ("Fs.read", "Fs.read(path: String): IO[String]"),
    ("Fs.write", "Fs.write(path: String, body: String): IO[Unit]"),
    ("Sys.args", "Sys.args(): IO[List]"),
    ("Sys.readLine", "Sys.readLine(): IO[String]"),
    ("Clock.realTime", "Clock.realTime(): IO[Int]"),
    ("Clock.monotonic", "Clock.monotonic(): IO[Int]"),
    ("Net.httpGet", "Net.httpGet(url: String): IO[String]"),
    (
        "Net.serve",
        "Net.serve(port: Int, handle: String => IO[Unit]): IO[Unit]",
    ),
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
    "Fiber.fork",
    "Fiber.join",
    "Ref.of",
    "Queue.unbounded",
    "Deferred.empty",
    "Resource.make",
    "Stream.emit",
    "View.bindText",
    "Signal.get",
    "Signal.str",
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
    fn hovers_list_exists() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (List.exists(["a"], x => true)) "y" else "n")
"#;
        let h = hover_src(src, "exists");
        assert!(
            h.contains("List.exists(xs: List[T], pred: T => Bool): Bool"),
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
    fn hovers_str_trim() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.trim("  x  "))
"#;
        let h = hover_src(src, "trim");
        assert!(h.contains("Str.trim(s: String): String"), "{h}");
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

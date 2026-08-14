//! Signature hover from the same parse as `check`. No second typer.

use crate::ast::{EnumDef, Expr, ExprKind, FunDef, Param, Program};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;

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
        "Int" | "String" | "Bool" | "Unit" | "List" | "IO"
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
    let mut best: Option<(usize, String)> = None;
    walk_binders(expr, name, offset, &mut best);
    best.map(|(_, s)| s)
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
        format!("{}: {} where …", p.name, p.ty)
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
    ("Fiber.fork", "Fiber.fork(inner: IO[T]): IO[Fiber]"),
    ("Fiber.join", "Fiber.join(f: Fiber): IO[T]"),
    ("Fiber.interrupt", "Fiber.interrupt(f: Fiber): IO[Unit]"),
    ("Str.fromInt", "Str.fromInt(n: Int): String"),
    ("Signal.int", "Signal.int(n: Int): Signal"),
    ("Signal.get", "Signal.get(s: Signal): Int"),
    ("Signal.set", "Signal.set(s: Signal, n: Int): Unit"),
    ("Signal.str", "Signal.str(s: String): Signal"),
    (
        "Signal.map",
        "Signal.map(s: Signal, f: Int => String): Signal",
    ),
    ("Signal.list", "Signal.list(xs: List): Signal"),
    ("View.text", "View.text(s: String): View"),
    ("View.bindText", "View.bindText(s: Signal): View"),
    (
        "View.button",
        "View.button(label: String, onTap: _ => Unit): View",
    ),
    ("View.column", "View.column(...): View"),
    ("View.row", "View.row(...): View"),
    ("View.stack", "View.stack(...): View"),
    ("View.each", "View.each(items: Signal): View"),
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
    ("Ui.run", "Ui.run(view: View): IO[Unit]"),
    (
        "Law.check",
        "Law.check(name: String, ok: Bool, value: T): T",
    ),
    ("Law.sometimes", "Law.sometimes(name: String): Unit"),
    ("Fs.read", "Fs.read(path: String): IO[String]"),
    ("Fs.write", "Fs.write(path: String, body: String): IO[Unit]"),
    ("Sys.args", "Sys.args(): IO[List]"),
    ("Sys.readLine", "Sys.readLine(): IO[String]"),
    ("Clock.nowMillis", "Clock.nowMillis(): IO[Int]"),
    ("Net.httpGet", "Net.httpGet(url: String): IO[String]"),
    (
        "Net.serve",
        "Net.serve(port: Int, handle: String => String): IO[Unit]",
    ),
    (
        "Resource.make",
        "Resource.make(acquire: IO[String], release: String => IO[Unit]): Resource",
    ),
    (
        "Resource.use",
        "Resource.use(r: Resource, f: String => IO[T]): IO[T]",
    ),
    ("Stream.emit", "Stream.emit(s: String): Stream"),
    (
        "Stream.compileToList",
        "Stream.compileToList(s: Stream): IO[List]",
    ),
];

pub(crate) fn kit_sig(callee: &str) -> Option<&'static str> {
    KIT_SIGS.iter().find(|(k, _)| *k == callee).map(|(_, s)| *s)
}

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
    fn hovers_enum() {
        let src =
            "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let h = hover_src(src, "Color");
        assert!(h.contains("enum Color:"), "{h}");
        assert!(h.contains("Red"), "{h}");
    }

    #[test]
    fn hovers_view_stretch() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.stretch(View.text(\"x\")))\n";
        let h = hover_src(src, "stretch");
        assert!(h.contains("View.stretch(child: View): View"), "{h}");
    }

    #[test]
    fn hovers_view_max_size() {
        let src =
            "@main def main: IO[Unit] =\n  Ui.run(_ => View.maxSize(40, 30, View.text(\"x\")))\n";
        let h = hover_src(src, "maxSize");
        assert!(
            h.contains("View.maxSize(w: Int, h: Int, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_clip() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.clip(View.text(\"x\")))\n";
        let h = hover_src(src, "clip");
        assert!(h.contains("View.clip(child: View): View"), "{h}");
    }

    #[test]
    fn hovers_view_opacity() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.opacity(50, View.text(\"x\")))\n";
        let h = hover_src(src, "opacity");
        assert!(
            h.contains("View.opacity(pct: Int, child: View): View"),
            "{h}"
        );
    }

    #[test]
    fn hovers_view_max_lines() {
        let src = "@main def main: IO[Unit] =\n  Ui.run(_ => View.maxLines(2, View.text(\"x\")))\n";
        let h = hover_src(src, "maxLines");
        assert!(
            h.contains("View.maxLines(n: Int, child: View): View"),
            "{h}"
        );
    }
}

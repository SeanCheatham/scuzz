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
    (
        "Str.startsWith",
        "Str.startsWith(s: String, prefix: String): Int",
    ),
    ("Str.trim", "Str.trim(s: String): String"),
    ("Signal.int", "Signal.int(n: Int): Signal"),
    ("Signal.get", "Signal.get(s: Signal): Int"),
    ("Signal.set", "Signal.set(s: Signal, n: Int): Unit"),
    ("Signal.str", "Signal.str(s: String): Signal"),
    (
        "Signal.map",
        "Signal.map(s: Signal, f: Int => String): Signal",
    ),
    ("Signal.list", "Signal.list(xs: List): Signal"),
    (
        "List.filter",
        "List.filter(xs: List, pred: String => Bool): List",
    ),
    ("List.map", "List.map(xs: List, f: String => String): List"),
    (
        "List.setAt",
        "List.setAt(xs: List, i: Int, v: String): List",
    ),
    ("View.text", "View.text(s: String): View"),
    ("View.bindText", "View.bindText(s: Signal): View"),
    (
        "View.button",
        "View.button(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.iconButton",
        "View.iconButton(label: String, onTap: _ => Unit): View",
    ),
    (
        "View.checkbox",
        "View.checkbox(sig: Signal, label: String): View",
    ),
    (
        "View.radio",
        "View.radio(sig: Signal, value: Int, label: String): View",
    ),
    ("View.slider", "View.slider(sig: Signal): View"),
    ("View.progress", "View.progress(sig: Signal): View"),
    (
        "View.circularProgress",
        "View.circularProgress(sig: Signal): View",
    ),
    (
        "View.switch",
        "View.switch(sig: Signal, label: String): View",
    ),
    ("View.chip", "View.chip(sig: Signal, label: String): View"),
    (
        "View.listTile",
        "View.listTile(title: String, trailing: View): View",
    ),
    ("View.badge", "View.badge(sig: Signal, child: View): View"),
    ("View.card", "View.card(child: View): View"),
    ("View.divider", "View.divider(): View"),
    ("View.verticalDivider", "View.verticalDivider(): View"),
    (
        "View.expansionTile",
        "View.expansionTile(sig: Signal, title: String, child: View): View",
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
        "View.each(items: Signal, row: String => View): View",
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
            h.contains("View.checkbox(sig: Signal, label: String): View"),
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
            h.contains("View.radio(sig: Signal, value: Int, label: String): View"),
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
        assert!(h.contains("View.slider(sig: Signal): View"), "{h}");
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
        assert!(h.contains("View.progress(sig: Signal): View"), "{h}");
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
            h.contains("View.circularProgress(sig: Signal): View"),
            "{h}"
        );
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
            h.contains("View.switch(sig: Signal, label: String): View"),
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
            h.contains("View.chip(sig: Signal, label: String): View"),
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
    fn hovers_view_badge() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(3)
    _ <- Ui.run(_ => View.badge(n, View.text("x")))
  } yield ()
"#;
        let h = hover_src(src, "badge");
        assert!(
            h.contains("View.badge(sig: Signal, child: View): View"),
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
            h.contains("View.expansionTile(sig: Signal, title: String, child: View): View"),
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
            h.contains("View.each(items: Signal, row: String => View): View"),
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
            h.contains("List.filter(xs: List, pred: String => Bool): List"),
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
            h.contains("List.map(xs: List, f: String => String): List"),
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
            h.contains("List.setAt(xs: List, i: Int, v: String): List"),
            "{h}"
        );
    }

    #[test]
    fn hovers_str_starts_with() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.startsWith("ab", "a") == 1) "y" else "n")
"#;
        let h = hover_src(src, "startsWith");
        assert!(
            h.contains("Str.startsWith(s: String, prefix: String): Int"),
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
}

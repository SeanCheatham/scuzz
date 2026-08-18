//! Call hierarchy from the same parse as `check`. No second typer.

use crate::ast::{Expr, ExprKind, Program};
use crate::definition::{decl_kw_name, definition_in_sources, source_for_module, DeclKind};
use std::collections::BTreeMap;

pub const KIND_FN: u8 = 12;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HierarchyItem {
    pub file: String,
    pub name: String,
    pub range_start: usize,
    pub range_end: usize,
    pub sel_start: usize,
    pub sel_end: usize,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IncomingCall {
    pub from: HierarchyItem,
    pub from_ranges: Vec<(usize, usize)>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OutgoingCall {
    pub to: HierarchyItem,
    pub from_ranges: Vec<(usize, usize)>,
}

/// Function item at `offset`. Kits and params have no item.
pub fn prepare_call_hierarchy(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Option<HierarchyItem> {
    let fns = fns(program, sources);
    if let Some(loc) = definition_in_sources(program, sources, current_file, current_source, offset)
    {
        if let Some(item) = fns
            .iter()
            .find(|f| f.file == loc.file && f.sel_start == loc.start)
        {
            return Some(item.clone());
        }
    }
    fns.into_iter()
        .find(|f| f.file == current_file && f.sel_start <= offset && offset < f.sel_end)
}

/// Callers of the function at `offset`.
pub fn incoming_calls(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<IncomingCall> {
    let Some(target) =
        prepare_call_hierarchy(program, sources, current_file, current_source, offset)
    else {
        return Vec::new();
    };
    let mut by_from: BTreeMap<(String, usize), IncomingCall> = BTreeMap::new();
    for (from, body, text) in fn_bodies(program, sources) {
        walk_calls(body, &mut |callee, start, end| {
            let Some((rs, re)) = callee_ident_span(text, start, end, callee) else {
                return;
            };
            let Some(hit) = resolve_fn(program, sources, &from.file, text, rs) else {
                return;
            };
            if hit.file != target.file || hit.sel_start != target.sel_start {
                return;
            }
            let key = (from.file.clone(), from.sel_start);
            by_from
                .entry(key)
                .or_insert_with(|| IncomingCall {
                    from: from.clone(),
                    from_ranges: Vec::new(),
                })
                .from_ranges
                .push((rs, re));
        });
    }
    finish_incoming(by_from)
}

/// Callees of the function at `offset`. Kits are omitted.
pub fn outgoing_calls(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
    current_source: &str,
    offset: usize,
) -> Vec<OutgoingCall> {
    let Some(from) = prepare_call_hierarchy(program, sources, current_file, current_source, offset)
    else {
        return Vec::new();
    };
    let Some((_, body, text)) = fn_bodies(program, sources)
        .into_iter()
        .find(|(f, _, _)| f.file == from.file && f.sel_start == from.sel_start)
    else {
        return Vec::new();
    };
    let mut by_to: BTreeMap<(String, usize), OutgoingCall> = BTreeMap::new();
    walk_calls(body, &mut |callee, start, end| {
        let Some((rs, re)) = callee_ident_span(text, start, end, callee) else {
            return;
        };
        let Some(to) = resolve_fn(program, sources, &from.file, text, rs) else {
            return;
        };
        let key = (to.file.clone(), to.sel_start);
        by_to
            .entry(key)
            .or_insert_with(|| OutgoingCall {
                to,
                from_ranges: Vec::new(),
            })
            .from_ranges
            .push((rs, re));
    });
    finish_outgoing(by_to)
}

fn resolve_fn(
    program: &Program,
    sources: &[(String, String)],
    file: &str,
    text: &str,
    offset: usize,
) -> Option<HierarchyItem> {
    prepare_call_hierarchy(program, sources, file, text, offset)
}

fn fns(program: &Program, sources: &[(String, String)]) -> Vec<HierarchyItem> {
    let mut out = Vec::new();
    for d in &program.defs {
        if let Some(item) = item_for_def(sources, &d.module, &d.name, d.body.span.end) {
            out.push(item);
        }
    }
    if !program.main.name.is_empty() {
        if let Some(item) = item_for_def(
            sources,
            &program.main.module,
            &program.main.name,
            program.main.body.span.end,
        ) {
            out.push(item);
        }
    }
    out
}

fn item_for_def(
    sources: &[(String, String)],
    module: &str,
    name: &str,
    body_end: usize,
) -> Option<HierarchyItem> {
    let (file, text) = source_for_module(sources, module)?;
    let (kw, sel_start, sel_end) = decl_kw_name(text, DeclKind::Def, name)?;
    Some(HierarchyItem {
        file: file.to_string(),
        name: name.to_string(),
        range_start: kw,
        range_end: body_end.max(sel_end),
        sel_start,
        sel_end,
    })
}

fn fn_bodies<'a>(
    program: &'a Program,
    sources: &'a [(String, String)],
) -> Vec<(HierarchyItem, &'a Expr, &'a str)> {
    let mut out = Vec::new();
    for d in &program.defs {
        let Some(item) = item_for_def(sources, &d.module, &d.name, d.body.span.end) else {
            continue;
        };
        let Some((_, text)) = source_for_module(sources, &d.module) else {
            continue;
        };
        out.push((item, &d.body, text));
    }
    if !program.main.name.is_empty() {
        if let Some(item) = item_for_def(
            sources,
            &program.main.module,
            &program.main.name,
            program.main.body.span.end,
        ) {
            if let Some((_, text)) = source_for_module(sources, &program.main.module) {
                out.push((item, &program.main.body, text));
            }
        }
    }
    out
}

fn walk_calls(e: &Expr, f: &mut impl FnMut(&str, usize, usize)) {
    if let ExprKind::Call { callee, .. } = &e.kind {
        f(callee, e.span.start, e.span.end);
    }
    e.for_each_child(|c| walk_calls(c, f));
}

fn callee_ident_span(
    source: &str,
    start: usize,
    end: usize,
    callee: &str,
) -> Option<(usize, usize)> {
    let region = source.get(start..end)?;
    let i = region.find(callee)?;
    let method = callee.rsplit('.').next().unwrap_or(callee);
    let ident_start = start + i + callee.len() - method.len();
    Some((ident_start, ident_start + method.len()))
}

fn finish_incoming(map: BTreeMap<(String, usize), IncomingCall>) -> Vec<IncomingCall> {
    let mut out: Vec<IncomingCall> = map.into_values().collect();
    for c in &mut out {
        c.from_ranges.sort();
        c.from_ranges.dedup();
    }
    out.sort_by(|a, b| {
        a.from
            .file
            .cmp(&b.from.file)
            .then(a.from.sel_start.cmp(&b.from.sel_start))
    });
    out
}

fn finish_outgoing(map: BTreeMap<(String, usize), OutgoingCall>) -> Vec<OutgoingCall> {
    let mut out: Vec<OutgoingCall> = map.into_values().collect();
    for c in &mut out {
        c.from_ranges.sort();
        c.from_ranges.dedup();
    }
    out.sort_by(|a, b| {
        a.to.file
            .cmp(&b.to.file)
            .then(a.to.sel_start.cmp(&b.to.sel_start))
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn src() -> &'static str {
        r#"def add(n: Int): Int =
  n

def twice(n: Int): Int =
  add(add(n))

@main def main: IO[Unit] =
  IO.println(Str.fromInt(twice(1)))
"#
    }

    fn program() -> (Program, Vec<(String, String)>, String) {
        let s = src().to_string();
        let p = parse_file(&s, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), s.clone())];
        (p, sources, s)
    }

    #[test]
    fn prepare_at_call_returns_callee() {
        let (p, sources, s) = program();
        let at = s.rfind("twice").unwrap();
        let item = prepare_call_hierarchy(&p, &sources, "Main.scuzz", &s, at).expect("twice");
        assert_eq!(item.name, "twice");
        assert_eq!(item.sel_start, s.find("twice").unwrap());
    }

    #[test]
    fn prepare_at_def_name() {
        let (p, sources, s) = program();
        let at = s.find("add").unwrap();
        let item = prepare_call_hierarchy(&p, &sources, "Main.scuzz", &s, at).expect("add");
        assert_eq!(item.name, "add");
        assert_eq!(item.sel_start, at);
    }

    #[test]
    fn prepare_skips_kits() {
        let (p, sources, s) = program();
        let at = s.find("IO.println").unwrap() + 3;
        assert!(prepare_call_hierarchy(&p, &sources, "Main.scuzz", &s, at).is_none());
    }

    #[test]
    fn prepare_at_main_def() {
        let (p, sources, s) = program();
        let at = s.find("def main").unwrap() + 4;
        let item = prepare_call_hierarchy(&p, &sources, "Main.scuzz", &s, at).expect("main");
        assert_eq!(item.name, "main");
    }

    #[test]
    fn incoming_groups_callers() {
        let (p, sources, s) = program();
        let at = s.find("add").unwrap();
        let inc = incoming_calls(&p, &sources, "Main.scuzz", &s, at);
        assert_eq!(inc.len(), 1, "{inc:?}");
        assert_eq!(inc[0].from.name, "twice");
        assert_eq!(inc[0].from_ranges.len(), 2, "{inc:?}");
    }

    #[test]
    fn outgoing_skips_kits_and_lists_user_callees() {
        let (p, sources, s) = program();
        let twice = s.find("twice").unwrap();
        let out = outgoing_calls(&p, &sources, "Main.scuzz", &s, twice);
        assert_eq!(out.len(), 1, "{out:?}");
        assert_eq!(out[0].to.name, "add");
        assert_eq!(out[0].from_ranges.len(), 2, "{out:?}");
        let main = s.find("def main").unwrap() + 4;
        let from_main = outgoing_calls(&p, &sources, "Main.scuzz", &s, main);
        assert_eq!(from_main.len(), 1, "{from_main:?}");
        assert_eq!(from_main[0].to.name, "twice");
    }

    #[test]
    fn incoming_from_main() {
        let (p, sources, s) = program();
        let twice = s.find("twice").unwrap();
        let inc = incoming_calls(&p, &sources, "Main.scuzz", &s, twice);
        assert_eq!(inc.len(), 1, "{inc:?}");
        assert_eq!(inc[0].from.name, "main");
        assert_eq!(inc[0].from_ranges.len(), 1);
    }
}

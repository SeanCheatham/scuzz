//! Completions from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::hover::{kit_lambda_locals, show_def, show_enum, show_param, KIT_SIGS};
use crate::lexer::{lex, Token};
use crate::resolve::module_id_from_label;
use std::collections::BTreeSet;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Completion {
    pub label: String,
    pub kind: u8,
    pub detail: String,
    pub insert_text: String,
}

pub const KIND_FN: u8 = 3;
pub const KIND_VAR: u8 = 6;
pub const KIND_ENUM: u8 = 13;
pub const KIND_KEYWORD: u8 = 14;
pub const KIND_MODULE: u8 = 9;
pub const KIND_TYPE: u8 = 22;
pub const KIND_MEMBER: u8 = 20;

/// Completions at a byte offset. `program` may be missing when parse fails; kits still appear.
pub fn complete_in_source(
    program: Option<&Program>,
    file: &str,
    source: &str,
    offset: usize,
) -> Vec<Completion> {
    let (qual, prefix) = prefix_at(source, offset);
    let module = module_id_from_label(file);
    let mut out = Vec::new();
    let mut seen = BTreeSet::new();
    let mut push = |c: Completion| {
        if seen.insert(c.label.clone()) {
            out.push(c);
        }
    };
    if let Some(q) = &qual {
        for (callee, sig) in KIT_SIGS {
            if let Some(method) = callee.strip_prefix(&format!("{q}.")) {
                if method.starts_with(&prefix) {
                    push(Completion {
                        label: (*callee).to_string(),
                        kind: KIND_FN,
                        detail: (*sig).to_string(),
                        insert_text: method.to_string(),
                    });
                }
            }
        }
        if let Some(p) = program {
            for d in &p.defs {
                if d.module == *q && d.name.starts_with(&prefix) && !d.is_private {
                    push(Completion {
                        label: format!("{q}.{}", d.name),
                        kind: KIND_FN,
                        detail: show_def(d),
                        insert_text: d.name.clone(),
                    });
                }
            }
            for en in &p.enums {
                if en.name == *q || (en.module == module && en.name == *q) {
                    for c in &en.cases {
                        if c.name.starts_with(&prefix) {
                            push(Completion {
                                label: format!("{}.{}", en.name, c.name),
                                kind: KIND_MEMBER,
                                detail: show_enum(en),
                                insert_text: c.name.clone(),
                            });
                        }
                    }
                }
            }
        }
        out.sort_by(|a, b| a.label.cmp(&b.label));
        return out;
    }
    for (callee, sig) in KIT_SIGS {
        let method = callee.split('.').nth(1).unwrap_or(callee);
        let kit = callee.split('.').next().unwrap_or(callee);
        if callee.starts_with(&prefix) || method.starts_with(&prefix) {
            push(Completion {
                label: (*callee).to_string(),
                kind: KIND_FN,
                detail: (*sig).to_string(),
                insert_text: (*callee).to_string(),
            });
        }
        if kit.starts_with(&prefix) {
            push(Completion {
                label: kit.to_string(),
                kind: KIND_MODULE,
                detail: format!("{kit} kit"),
                insert_text: kit.to_string(),
            });
        }
    }
    for ty in [
        "Int", "Float", "String", "Bool", "Unit", "List", "Map", "Set", "IO",
    ] {
        if ty.starts_with(&prefix) {
            push(Completion {
                label: ty.to_string(),
                kind: KIND_TYPE,
                detail: ty.to_string(),
                insert_text: ty.to_string(),
            });
        }
    }
    for kw in [
        "def", "law", "match", "for", "yield", "case", "import", "enum", "record", "trait", "impl",
    ] {
        if kw.starts_with(&prefix) {
            push(Completion {
                label: kw.to_string(),
                kind: KIND_KEYWORD,
                detail: kw.to_string(),
                insert_text: kw.to_string(),
            });
        }
    }
    if let Some(p) = program {
        for d in &p.defs {
            if !d.name.starts_with(&prefix) {
                continue;
            }
            if d.module == module || !d.is_private {
                push(Completion {
                    label: d.name.clone(),
                    kind: KIND_FN,
                    detail: show_def(d),
                    insert_text: d.name.clone(),
                });
            }
        }
        for en in &p.enums {
            if en.name.starts_with(&prefix) {
                push(Completion {
                    label: en.name.clone(),
                    kind: KIND_ENUM,
                    detail: show_enum(en),
                    insert_text: en.name.clone(),
                });
            }
        }
        for d in p.defs.iter().filter(|d| d.module == module) {
            if !(d.body.span.start <= offset && offset <= d.body.span.end) {
                continue;
            }
            for param in &d.params {
                if param.name.starts_with(&prefix) {
                    push(Completion {
                        label: param.name.clone(),
                        kind: KIND_VAR,
                        detail: show_param(param),
                        insert_text: param.name.clone(),
                    });
                }
            }
        }
        let mut bodies: Vec<&crate::ast::Expr> = p.defs.iter().map(|d| &d.body).collect();
        bodies.push(&p.main.body);
        for body in bodies {
            for (name, ty) in kit_lambda_locals(body, offset) {
                if name.starts_with(&prefix) {
                    push(Completion {
                        label: name.clone(),
                        kind: KIND_VAR,
                        detail: format!("{name}: {ty}"),
                        insert_text: name,
                    });
                }
            }
        }
    }
    out.sort_by(|a, b| a.label.cmp(&b.label));
    out
}

fn prefix_at(source: &str, offset: usize) -> (Option<String>, String) {
    let Ok(toks) = lex(source) else {
        return (None, String::new());
    };
    let mut last = None;
    for (i, t) in toks.iter().enumerate() {
        if t.span.start < offset {
            last = Some(i);
        }
    }
    let Some(i) = last else {
        return (None, String::new());
    };
    let t = &toks[i];
    let qual_before_ident = |i: usize| -> Option<String> {
        if i >= 2 && matches!(toks[i - 1].token, Token::Dot) {
            if let Token::Ident(q) = &toks[i - 2].token {
                return Some(q.clone());
            }
        }
        None
    };
    if matches!(t.token, Token::Ident(_)) && t.span.start < offset && offset <= t.span.end {
        let prefix = source.get(t.span.start..offset).unwrap_or("").to_string();
        return (qual_before_ident(i), prefix);
    }
    if matches!(t.token, Token::Dot) && t.span.end <= offset {
        if i >= 1 {
            if let Token::Ident(q) = &toks[i - 1].token {
                return (Some(q.clone()), String::new());
            }
        }
    }
    (None, String::new())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse_file;

    fn labels_at(src: &str, at: &str) -> Vec<String> {
        let program = parse_file(src, "Main.scuzz").ok();
        let offset = src.find(at).expect(at) + at.len();
        complete_in_source(program.as_ref(), "Main.scuzz", src, offset)
            .into_iter()
            .map(|c| c.label)
            .collect()
    }

    #[test]
    fn completes_io_methods_after_dot() {
        let src = "@main def main: IO[Unit] = IO.\n";
        let labels = labels_at(src, "IO.");
        assert!(labels.iter().any(|l| l == "IO.println"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "IO.sleep"), "{labels:?}");
    }

    #[test]
    fn completes_def_by_prefix() {
        let src = "def add(n: Int): Int = n\n@main def main: IO[Unit] = ad\n";
        let labels = labels_at(src, " ad");
        assert!(labels.iter().any(|l| l == "add"), "{labels:?}");
    }

    #[test]
    fn completes_enum_cases() {
        let live =
            "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        let program = parse_file(live, "Main.scuzz").unwrap();
        let src = "enum Color:\n  case Red\n  case Blue\n@main def main: IO[Unit] = Color.\n";
        let offset = src.find("Color.").unwrap() + 6;
        let labels: Vec<String> = complete_in_source(Some(&program), "Main.scuzz", src, offset)
            .into_iter()
            .map(|c| c.label)
            .collect();
        assert!(labels.iter().any(|l| l.contains("Red")), "{labels:?}");
        assert!(labels.iter().any(|l| l.contains("Blue")), "{labels:?}");
    }

    #[test]
    fn completes_view_stretch_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.st\n";
        let labels = labels_at(src, "View.st");
        assert!(labels.iter().any(|l| l == "View.stretch"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "View.stack"), "{labels:?}");
    }

    #[test]
    fn completes_view_wrap_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.wr\n";
        let labels = labels_at(src, "View.wr");
        assert!(labels.iter().any(|l| l == "View.wrap"), "{labels:?}");
    }

    #[test]
    fn completes_view_grid_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.gr\n";
        let labels = labels_at(src, "View.gr");
        assert!(labels.iter().any(|l| l == "View.grid"), "{labels:?}");
    }

    #[test]
    fn completes_view_max_size_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.max\n";
        let labels = labels_at(src, "View.max");
        assert!(labels.iter().any(|l| l == "View.maxSize"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "View.maxLines"), "{labels:?}");
    }

    #[test]
    fn completes_view_border_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.bo\n";
        let labels = labels_at(src, "View.bo");
        assert!(labels.iter().any(|l| l == "View.border"), "{labels:?}");
    }

    #[test]
    fn completes_view_radius_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.ra\n";
        let labels = labels_at(src, "View.ra");
        assert!(labels.iter().any(|l| l == "View.radius"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "View.radio"), "{labels:?}");
    }

    #[test]
    fn completes_view_radio_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.radio\n";
        let labels = labels_at(src, "View.radio");
        assert!(labels.iter().any(|l| l == "View.radio"), "{labels:?}");
    }

    #[test]
    fn completes_view_checkbox_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.ch\n";
        let labels = labels_at(src, "View.ch");
        assert!(labels.iter().any(|l| l == "View.checkbox"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "View.chip"), "{labels:?}");
    }

    #[test]
    fn completes_view_chip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.chi\n";
        let labels = labels_at(src, "View.chi");
        assert!(labels.iter().any(|l| l == "View.chip"), "{labels:?}");
    }

    #[test]
    fn completes_view_filter_chip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.filt\n";
        let labels = labels_at(src, "View.filt");
        assert!(labels.iter().any(|l| l == "View.filterChip"), "{labels:?}");
    }

    #[test]
    fn completes_view_choice_chip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.choi\n";
        let labels = labels_at(src, "View.choi");
        assert!(labels.iter().any(|l| l == "View.choiceChip"), "{labels:?}");
    }

    #[test]
    fn completes_view_action_chip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.acti\n";
        let labels = labels_at(src, "View.acti");
        assert!(labels.iter().any(|l| l == "View.actionChip"), "{labels:?}");
    }

    #[test]
    fn completes_view_input_chip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.inp\n";
        let labels = labels_at(src, "View.inp");
        assert!(labels.iter().any(|l| l == "View.inputChip"), "{labels:?}");
    }

    #[test]
    fn completes_view_list_tile_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.li\n";
        let labels = labels_at(src, "View.li");
        assert!(labels.iter().any(|l| l == "View.listTile"), "{labels:?}");
    }

    #[test]
    fn completes_view_checkbox_list_tile_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.checkboxL\n";
        let labels = labels_at(src, "View.checkboxL");
        assert!(
            labels.iter().any(|l| l == "View.checkboxListTile"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_switch_list_tile_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.switchL\n";
        let labels = labels_at(src, "View.switchL");
        assert!(
            labels.iter().any(|l| l == "View.switchListTile"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_radio_list_tile_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.radioL\n";
        let labels = labels_at(src, "View.radioL");
        assert!(
            labels.iter().any(|l| l == "View.radioListTile"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_segmented_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.seg\n";
        let labels = labels_at(src, "View.seg");
        assert!(labels.iter().any(|l| l == "View.segmented"), "{labels:?}");
    }

    #[test]
    fn completes_view_fab_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.fa\n";
        let labels = labels_at(src, "View.fa");
        assert!(labels.iter().any(|l| l == "View.fab"), "{labels:?}");
    }

    #[test]
    fn completes_view_outlined_button_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.outl\n";
        let labels = labels_at(src, "View.outl");
        assert!(
            labels.iter().any(|l| l == "View.outlinedButton"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_text_button_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.textB\n";
        let labels = labels_at(src, "View.textB");
        assert!(labels.iter().any(|l| l == "View.textButton"), "{labels:?}");
    }

    #[test]
    fn completes_view_tooltip_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.tool\n";
        let labels = labels_at(src, "View.tool");
        assert!(labels.iter().any(|l| l == "View.tooltip"), "{labels:?}");
    }

    #[test]
    fn completes_view_placeholder_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.place\n";
        let labels = labels_at(src, "View.place");
        assert!(labels.iter().any(|l| l == "View.placeholder"), "{labels:?}");
    }

    #[test]
    fn completes_view_semantics_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.sema\n";
        let labels = labels_at(src, "View.sema");
        assert!(labels.iter().any(|l| l == "View.semantics"), "{labels:?}");
    }

    #[test]
    fn completes_view_merge_semantics_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.merg\n";
        let labels = labels_at(src, "View.merg");
        assert!(
            labels.iter().any(|l| l == "View.mergeSemantics"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_ink_well_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.ink\n";
        let labels = labels_at(src, "View.ink");
        assert!(labels.iter().any(|l| l == "View.inkWell"), "{labels:?}");
    }

    #[test]
    fn completes_view_visibility_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.vis\n";
        let labels = labels_at(src, "View.vis");
        assert!(labels.iter().any(|l| l == "View.visibility"), "{labels:?}");
    }

    #[test]
    fn completes_view_offstage_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.offs\n";
        let labels = labels_at(src, "View.offs");
        assert!(labels.iter().any(|l| l == "View.offstage"), "{labels:?}");
    }

    #[test]
    fn completes_view_unconstrained_box_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.unc\n";
        let labels = labels_at(src, "View.unc");
        assert!(
            labels.iter().any(|l| l == "View.unconstrainedBox"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_badge_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.ba\n";
        let labels = labels_at(src, "View.ba");
        assert!(labels.iter().any(|l| l == "View.badge"), "{labels:?}");
    }

    #[test]
    fn completes_view_card_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.car\n";
        let labels = labels_at(src, "View.car");
        assert!(labels.iter().any(|l| l == "View.card"), "{labels:?}");
    }

    #[test]
    fn completes_view_divider_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.di\n";
        let labels = labels_at(src, "View.di");
        assert!(labels.iter().any(|l| l == "View.divider"), "{labels:?}");
    }

    #[test]
    fn completes_view_vertical_divider_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.vert\n";
        let labels = labels_at(src, "View.vert");
        assert!(
            labels.iter().any(|l| l == "View.verticalDivider"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_expansion_tile_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.expan\n";
        let labels = labels_at(src, "View.expan");
        assert!(
            labels.iter().any(|l| l == "View.expansionTile"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_icon_button_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.iconB\n";
        let labels = labels_at(src, "View.iconB");
        assert!(labels.iter().any(|l| l == "View.iconButton"), "{labels:?}");
    }

    #[test]
    fn completes_view_slider_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.sl\n";
        let labels = labels_at(src, "View.sl");
        assert!(labels.iter().any(|l| l == "View.slider"), "{labels:?}");
    }

    #[test]
    fn completes_view_progress_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.pr\n";
        let labels = labels_at(src, "View.pr");
        assert!(labels.iter().any(|l| l == "View.progress"), "{labels:?}");
    }

    #[test]
    fn completes_view_circular_progress_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.circ\n";
        let labels = labels_at(src, "View.circ");
        assert!(
            labels.iter().any(|l| l == "View.circularProgress"),
            "{labels:?}"
        );
    }

    #[test]
    fn completes_view_avatar_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.av\n";
        let labels = labels_at(src, "View.av");
        assert!(labels.iter().any(|l| l == "View.avatar"), "{labels:?}");
    }

    #[test]
    fn completes_view_switch_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.sw\n";
        let labels = labels_at(src, "View.sw");
        assert!(labels.iter().any(|l| l == "View.switch"), "{labels:?}");
    }

    #[test]
    fn completes_view_scroll_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.sc\n";
        let labels = labels_at(src, "View.sc");
        assert!(labels.iter().any(|l| l == "View.scroll"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "View.scrollH"), "{labels:?}");
    }

    #[test]
    fn completes_view_each_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.ea\n";
        let labels = labels_at(src, "View.ea");
        assert!(labels.iter().any(|l| l == "View.each"), "{labels:?}");
    }

    #[test]
    fn completes_list_filter_after_dot() {
        let src = "@main def main: IO[Unit] = List.fi\n";
        let labels = labels_at(src, "List.fi");
        assert!(labels.iter().any(|l| l == "List.filter"), "{labels:?}");
    }

    #[test]
    fn completes_list_map_after_dot() {
        let src = "@main def main: IO[Unit] = List.ma\n";
        let labels = labels_at(src, "List.ma");
        assert!(labels.iter().any(|l| l == "List.map"), "{labels:?}");
    }

    #[test]
    fn completes_list_set_at_after_dot() {
        let src = "@main def main: IO[Unit] = List.se\n";
        let labels = labels_at(src, "List.se");
        assert!(labels.iter().any(|l| l == "List.setAt"), "{labels:?}");
    }

    #[test]
    fn completes_list_take_after_dot() {
        let src = "@main def main: IO[Unit] = List.tak\n";
        let labels = labels_at(src, "List.tak");
        assert!(labels.iter().any(|l| l == "List.take"), "{labels:?}");
    }

    #[test]
    fn completes_list_exists_after_dot() {
        let src = "@main def main: IO[Unit] = List.ex\n";
        let labels = labels_at(src, "List.ex");
        assert!(labels.iter().any(|l| l == "List.exists"), "{labels:?}");
    }

    #[test]
    fn completes_list_take_while_after_dot() {
        let src = "@main def main: IO[Unit] = List.takeW\n";
        let labels = labels_at(src, "List.takeW");
        assert!(labels.iter().any(|l| l == "List.takeWhile"), "{labels:?}");
    }

    #[test]
    fn completes_list_forall_after_dot() {
        let src = "@main def main: IO[Unit] = List.fo\n";
        let labels = labels_at(src, "List.fo");
        assert!(labels.iter().any(|l| l == "List.forall"), "{labels:?}");
    }

    #[test]
    fn completes_map_set_after_dot() {
        let src = "@main def main: IO[Unit] = Map.se\n";
        let labels = labels_at(src, "Map.se");
        assert!(labels.iter().any(|l| l == "Map.set"), "{labels:?}");
    }

    #[test]
    fn completes_map_remove_after_dot() {
        let src = "@main def main: IO[Unit] = Map.re\n";
        let labels = labels_at(src, "Map.re");
        assert!(labels.iter().any(|l| l == "Map.remove"), "{labels:?}");
    }

    #[test]
    fn completes_set_to_list_after_dot() {
        let src = "@main def main: IO[Unit] = Set.to\n";
        let labels = labels_at(src, "Set.to");
        assert!(labels.iter().any(|l| l == "Set.toList"), "{labels:?}");
    }

    #[test]
    fn completes_str_starts_with_after_dot() {
        let src = "@main def main: IO[Unit] = Str.st\n";
        let labels = labels_at(src, "Str.st");
        assert!(labels.iter().any(|l| l == "Str.startsWith"), "{labels:?}");
    }

    #[test]
    fn completes_str_trim_after_dot() {
        let src = "@main def main: IO[Unit] = Str.tr\n";
        let labels = labels_at(src, "Str.tr");
        assert!(labels.iter().any(|l| l == "Str.trim"), "{labels:?}");
    }

    #[test]
    fn completes_view_each_row_param() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, row => View.text(row)))
  } yield ()
"#;
        let program = parse_file(src, "Main.scuzz").unwrap();
        let at = "View.text(ro";
        let offset = src.find(at).unwrap() + at.len();
        let hits: Vec<_> = complete_in_source(Some(&program), "Main.scuzz", src, offset);
        assert!(
            hits.iter()
                .any(|c| c.label == "row" && c.detail.contains("row: String")),
            "{hits:?}"
        );
    }

    #[test]
    fn completes_view_kit_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => View.\n";
        let labels = labels_at(src, "View.");
        for (callee, _) in KIT_SIGS {
            if callee.starts_with("View.") {
                assert!(
                    labels.iter().any(|l| l == *callee),
                    "missing {callee} in {labels:?}"
                );
            }
        }
    }

    #[test]
    fn completes_color_rgba_after_dot() {
        let src = "@main def main: IO[Unit] = Ui.run(_ => Color.rgb\n";
        let labels = labels_at(src, "Color.rgb");
        assert!(labels.iter().any(|l| l == "Color.rgb"), "{labels:?}");
        assert!(labels.iter().any(|l| l == "Color.rgba"), "{labels:?}");
    }
}

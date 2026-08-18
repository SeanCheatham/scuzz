use crate::ast::{BinOp, EnumDef, Expr, ExprKind, FunDef, MatchArm, Pattern, Program, Type};
use crate::resolve::{user_symbol, FunIndex};
use std::collections::HashMap;
use std::fmt::Write as _;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Kind {
    Int,
    Float,
    Ptr,
    Io,
}

#[derive(Clone)]
struct Local {
    value: String,
    kind: Kind,
    owned: bool,
}

impl Local {
    fn borrow(value: impl Into<String>, kind: Kind) -> Self {
        Local {
            value: value.into(),
            kind,
            owned: false,
        }
    }

    fn owned(value: impl Into<String>, kind: Kind) -> Self {
        Local {
            value: value.into(),
            kind,
            owned: true,
        }
    }
}

/// Emit LLVM IR. Links against `libscuzz_rt`.
pub fn emit_llvm(program: &Program) -> String {
    let mut strs: Vec<String> = Vec::new();
    for d in &program.defs {
        collect_strings(&d.body, &mut strs);
    }
    collect_strings(&program.main.body, &mut strs);
    for d in &program.defs {
        if d.is_driver {
            if !strs.contains(&d.name) {
                strs.push(d.name.clone());
            }
        } else if d.is_law && !d.params.is_empty() && !strs.contains(&d.name) {
            strs.push(d.name.clone());
        }
    }

    let enum_tags = build_enum_tags(&program.enums);
    let enum_payloads = build_enum_payloads(&program.enums);
    let funs = FunIndex::build(&program.defs, &program.imports, &program.enums)
        .expect("duplicate defs should be rejected earlier");

    let mut out = String::new();
    writeln!(out, "; Scuzz Lang generated LLVM IR").unwrap();
    // Omit target triple / datalayout: clang uses the host defaults (macOS arm64, Linux x86_64, …).
    writeln!(out).unwrap();

    writeln!(out, "declare ptr @sz_string_from_cstr(ptr)").unwrap();
    writeln!(out, "declare void @sz_retain(ptr)").unwrap();
    writeln!(out, "declare void @sz_release(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_concat(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_len(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_slice(ptr, i64, i64)").unwrap();
    writeln!(out, "declare i32 @sz_string_eq(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_char_at(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_from_int(i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_from_float(double)").unwrap();
    writeln!(out, "declare i64 @sz_string_index_of(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_last_index_of(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_take(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_drop(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_take_right(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_drop_right(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_reverse(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_starts_with(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_contains(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_ends_with(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_to_int(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_replace(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_trim(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_is_empty(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_non_empty(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_to_lower(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_to_upper(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_capitalize(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_repeat(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_strip_prefix(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_strip_suffix(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_pad_left(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_pad_right(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_is_blank(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_lines(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_split(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_println(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_pure(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_flatmap(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_error_new(i32, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_fail(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_sleep_ms(i64)").unwrap();
    writeln!(out, "declare ptr @sz_io_handle_error_with(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_attempt_as_result(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_error_message(ptr)").unwrap();
    writeln!(out, "declare void @sz_panic(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_race(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_both(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_ensure(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_timeout(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_forever(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_repeat_n(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_retry_n(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fiber_fork(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fiber_join(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fiber_interrupt(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_unsafe_run_or_die(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_adt_new(i32, ptr)").unwrap();
    writeln!(out, "declare i32 @sz_adt_tag(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_adt_payload(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_nil()").unwrap();
    writeln!(out, "declare i32 @sz_list_is_empty(ptr)").unwrap();
    writeln!(out, "declare i32 @sz_list_non_empty(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_cons(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_head(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_tail(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_len(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_at(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_reverse(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_join(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_map_empty()").unwrap();
    writeln!(out, "declare ptr @sz_map_set(ptr, ptr, ptr, i32)").unwrap();
    writeln!(out, "declare ptr @sz_map_get_or(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_map_get(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_map_contains(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_map_remove(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_map_keys(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_map_values(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_map_size(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_set_union(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_set_intersect(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_set_diff(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_append(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_set_at(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_filter(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_filter_not(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_take(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_drop(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_take_right(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_drop_right(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_init(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_last(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_get_or(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_fill(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_find(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_exists(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_count(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_takewhile(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_dropwhile(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_forall(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_concat(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_flatten(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_map(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_flat_map(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_pad_to(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_range(i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_tabulate(i64, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_intersperse(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_grouped(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_sliding(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_slice(ptr, i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_indices(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_split_at(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_span(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_partition(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_index_where(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_last_index_where(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fs_read(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fs_write(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fs_list(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fs_mkdirs(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_fs_canonicalize(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_sys_args()").unwrap();
    writeln!(out, "declare ptr @sz_sys_read_line()").unwrap();
    writeln!(out, "declare ptr @sz_sys_read(i64)").unwrap();
    writeln!(out, "declare ptr @sz_sys_write(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_sys_exec(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_sys_spawn(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_sys_alive(i64)").unwrap();
    writeln!(out, "declare ptr @sz_sys_kill(i64)").unwrap();
    writeln!(out, "declare ptr @sz_sys_getenv(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_clock_real_time()").unwrap();
    writeln!(out, "declare ptr @sz_clock_monotonic()").unwrap();
    writeln!(out, "declare ptr @sz_random_next_int(i64)").unwrap();
    writeln!(out, "declare ptr @sz_net_http_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_net_serve_once(i64, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_net_serve(i64, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_impurity_run_kit()").unwrap();
    writeln!(out, "declare ptr @sz_ref_of(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_ref_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_ref_set(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_queue_unbounded()").unwrap();
    writeln!(out, "declare ptr @sz_queue_offer(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_queue_take(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_deferred_empty()").unwrap();
    writeln!(out, "declare ptr @sz_deferred_complete(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_deferred_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_resource_make(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_resource_use(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_emit(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_emits(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_eval(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_concat(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_take(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_stream_drop(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_stream_evalmap(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_filter(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_map(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_takewhile(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_dropwhile(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_find(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_exists(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_compile_to_list(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_stream_drain(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_int(i64)").unwrap();
    writeln!(out, "declare i64 @sz_lang_signal_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_set(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_str(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_str_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_str_set(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_list(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_list_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_list_set(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_signal_map(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_text(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_bind_text(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_button(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_icon_button(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_fab(ptr, ptr, ptr)").unwrap();
    writeln!(
        out,
        "declare ptr @sz_lang_view_outlined_button(ptr, ptr, ptr)"
    )
    .unwrap();
    writeln!(out, "declare ptr @sz_lang_view_text_button(ptr, ptr, ptr)").unwrap();
    writeln!(
        out,
        "declare ptr @sz_lang_view_ink_well(ptr, ptr, ptr, ptr)"
    )
    .unwrap();
    writeln!(out, "declare ptr @sz_lang_view_checkbox(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_radio(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_slider(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_progress(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_circular_progress(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_avatar(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_switch(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_chip(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_filter_chip(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_choice_chip(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_action_chip(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_input_chip(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_list_tile(ptr, ptr)").unwrap();
    writeln!(
        out,
        "declare ptr @sz_lang_view_checkbox_list_tile(ptr, ptr)"
    )
    .unwrap();
    writeln!(out, "declare ptr @sz_lang_view_switch_list_tile(ptr, ptr)").unwrap();
    writeln!(
        out,
        "declare ptr @sz_lang_view_radio_list_tile(ptr, i64, ptr)"
    )
    .unwrap();
    writeln!(out, "declare ptr @sz_lang_view_segmented(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_badge(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_visibility(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_offstage(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_unconstrained_box(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_card(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_tooltip(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_placeholder(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_semantics(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_merge_semantics(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_divider()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_vertical_divider()").unwrap();
    writeln!(
        out,
        "declare ptr @sz_lang_view_expansion_tile(ptr, ptr, ptr)"
    )
    .unwrap();
    writeln!(out, "declare i64 @sz_theme_accent()").unwrap();
    writeln!(out, "declare i64 @sz_theme_primary()").unwrap();
    writeln!(out, "declare i64 @sz_theme_muted()").unwrap();
    writeln!(out, "declare i64 @sz_theme_foreground()").unwrap();
    writeln!(out, "declare i64 @sz_color_rgb(i64, i64, i64)").unwrap();
    writeln!(out, "declare i64 @sz_color_rgba(i64, i64, i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_column()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_row()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_wrap()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_grid(i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_stack()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_each(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_each_map(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_scroll(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_scroll_h(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_expanded(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_stretch(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_center(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_align(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_positioned(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_padding(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_sized(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_min_size(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_max_size(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_clip(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_opacity(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_max_lines(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_ellipsis(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_text_color(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_gap(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_font_size(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_border(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_radius(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_ignore_pointer(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_absorb_pointer(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_exclude_semantics(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_background(i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_aspect_ratio(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_fraction(i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_text_field(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_icon(i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_image(i64, i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_add_child(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_show_when(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_ui_run_rebuild(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_law_signal_int(i64)").unwrap();
    writeln!(out, "declare ptr @sz_law_signal_str(i64)").unwrap();
    writeln!(out, "declare i64 @sz_law_signal_list_len(i64)").unwrap();
    writeln!(out, "declare ptr @sz_law_signal_list_at(i64, i64)").unwrap();
    writeln!(out, "declare i64 @sz_law_a11y_has(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_law_assert(ptr, i64)").unwrap();
    writeln!(out, "declare void @sz_law_check(ptr, i64)").unwrap();
    writeln!(out, "declare void @sz_law_sometimes(ptr)").unwrap();
    writeln!(out, "declare void @sz_driver_register(ptr, i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_box_i64(i64)").unwrap();
    writeln!(out, "declare i64 @sz_unbox_i64(ptr)").unwrap();
    writeln!(out, "declare i32 @sz_runtime_main_args(ptr, i32, ptr)").unwrap();
    writeln!(out).unwrap();

    for (i, s) in strs.iter().enumerate() {
        let escaped = llvm_escape(s);
        let len = s.len() + 1;
        writeln!(
            out,
            "@.str{i} = private unnamed_addr constant [{len} x i8] c\"{escaped}\\00\", align 1"
        )
        .unwrap();
    }
    writeln!(
        out,
        "@.div0 = private unnamed_addr constant [17 x i8] c\"division by zero\\00\", align 1"
    )
    .unwrap();
    writeln!(out).unwrap();

    // User defs are emitted below. LLVM allows call sites before the defining
    // `define` in the same module. A separate `declare` errors.

    let mut cont_id = 0usize;
    let mut conts = String::new();
    let mut reload_fn: Option<String> = None;
    let mut ctx = EmitCtx {
        strs: &strs,
        enum_tags: &enum_tags,
        enum_payloads: &enum_payloads,
        funs: &funs,
        current_module: "",
        cont_id: &mut cont_id,
        conts: &mut conts,
        reload_fn: &mut reload_fn,
    };

    let mut fundef_ir = String::new();
    for d in &program.defs {
        ctx.current_module = d.module.as_str();
        emit_fundef(d, &mut ctx, &mut fundef_ir);
    }

    ctx.current_module = program.main.module.as_str();
    let mut locals: HashMap<String, Local> = HashMap::new();
    let body_expr = emit_expr(&program.main.body, &mut ctx, &mut locals, "build");
    let reload_name = ctx.reload_fn.clone();

    out.push_str(&conts);
    out.push_str(&fundef_ir);
    emit_law_drive_tramps(&mut out, program, &strs);

    writeln!(out, "define i32 @main(i32 %argc, ptr %argv) {{").unwrap();
    writeln!(out, "entry:").unwrap();
    emit_driver_registers(&mut out, program, &strs);
    out.push_str(&body_expr.code);
    let io_val = ensure_io(
        &mut out,
        body_expr.kind,
        &body_expr.value,
        "wrapped",
        body_expr.owned,
    );
    writeln!(
        out,
        "  %rc = call i32 @sz_runtime_main_args(ptr {io_val}, i32 %argc, ptr %argv)"
    )
    .unwrap();
    writeln!(out, "  ret i32 %rc").unwrap();
    writeln!(out, "}}").unwrap();

    if let Some(name) = reload_name {
        writeln!(out).unwrap();
        writeln!(out, "define ptr @sz_ui_reload_rebuild(ptr %env) {{").unwrap();
        writeln!(out, "entry:").unwrap();
        writeln!(out, "  %v = call ptr @{name}(ptr %env)").unwrap();
        writeln!(out, "  ret ptr %v").unwrap();
        writeln!(out, "}}").unwrap();
    }

    out
}

struct EmitCtx<'a> {
    strs: &'a [String],
    enum_tags: &'a HashMap<(String, String), i32>,
    /// Payload field type for unary cases (absent / Unit-like = nullary).
    enum_payloads: &'a HashMap<(String, String), Vec<Type>>,
    funs: &'a FunIndex<'a>,
    current_module: &'a str,
    cont_id: &'a mut usize,
    conts: &'a mut String,
    reload_fn: &'a mut Option<String>,
}

struct Emitted {
    code: String,
    value: String,
    kind: Kind,
    /// When `kind == Io`, the kind of the successful payload (Int for Sys.exec).
    payload: Kind,
    /// When `kind == Io` and the payload is a ptr, the run result holds a distinct RC.
    payload_owned: bool,
    /// Compiler owns a +1 on this ptr (string / list / map / ADT temps). Release after last use.
    owned: bool,
}

fn io_emitted(code: String, value: String, payload: Kind) -> Emitted {
    io_emitted_payload(code, value, payload, false)
}

fn io_emitted_payload(code: String, value: String, payload: Kind, payload_owned: bool) -> Emitted {
    Emitted {
        code,
        value,
        kind: Kind::Io,
        payload,
        payload_owned,
        owned: false,
    }
}

fn val_emitted(code: String, value: String, kind: Kind) -> Emitted {
    Emitted {
        code,
        value,
        kind,
        payload: Kind::Ptr,
        payload_owned: false,
        owned: false,
    }
}

fn owned_ptr(code: String, value: String) -> Emitted {
    Emitted {
        code,
        value,
        kind: Kind::Ptr,
        payload: Kind::Ptr,
        payload_owned: false,
        owned: true,
    }
}

fn drop_owned_ptr(code: &mut String, e: &Emitted) {
    if e.owned && e.kind == Kind::Ptr {
        writeln!(code, "  call void @sz_release(ptr {})", e.value).unwrap();
    }
}

fn drop_owned_ptrs(code: &mut String, args: &[Emitted]) {
    for a in args {
        drop_owned_ptr(code, a);
    }
}

/// Keep `result` when `src` was the last owner (retain, then drop `src`).
fn take_owned_ptr(code: &mut String, src: &Emitted, result: &str) {
    if src.owned && src.kind == Kind::Ptr {
        writeln!(code, "  call void @sz_retain(ptr {result})").unwrap();
        drop_owned_ptr(code, src);
    }
}

fn ptr_owned_if(code: String, value: String, owned: bool) -> Emitted {
    if owned {
        owned_ptr(code, value)
    } else {
        val_emitted(code, value, Kind::Ptr)
    }
}

fn build_enum_tags(enums: &[EnumDef]) -> HashMap<(String, String), i32> {
    let mut m = HashMap::new();
    for e in enums {
        let id = crate::resolve::enum_id(&e.module, &e.name);
        for (i, c) in e.cases.iter().enumerate() {
            m.insert((id.clone(), c.name.clone()), i as i32);
        }
    }
    m
}

fn build_enum_payloads(enums: &[EnumDef]) -> HashMap<(String, String), Vec<Type>> {
    let mut m = HashMap::new();
    for e in enums {
        let id = crate::resolve::enum_id(&e.module, &e.name);
        for c in &e.cases {
            if !c.fields.is_empty() {
                m.insert(
                    (id.clone(), c.name.clone()),
                    c.fields.iter().map(|(_, ty)| ty.clone()).collect(),
                );
            }
        }
    }
    m
}

fn llvm_type(ty: &Type) -> &'static str {
    match ty {
        Type::Int | Type::Bool => "i64",
        Type::Float => "double",
        _ => "ptr",
    }
}

fn kind_of_type(ty: &Type) -> Kind {
    match ty {
        Type::Int | Type::Bool => Kind::Int,
        Type::Float => Kind::Float,
        Type::Io(_) => Kind::Io,
        _ => Kind::Ptr,
    }
}

fn retain_borrowed_ret(ty: &Type) -> bool {
    match ty {
        Type::String | Type::List(_) | Type::Adt(_) => true,
        Type::App(n, _) => !matches!(
            n.as_str(),
            "Fiber" | "Ref" | "Queue" | "Deferred" | "Resource" | "Stream"
        ),
        _ => false,
    }
}

fn llvm_kind_ty(kind: Kind) -> &'static str {
    match kind {
        Kind::Int => "i64",
        Kind::Float => "double",
        Kind::Ptr | Kind::Io => "ptr",
    }
}

fn llvm_double_const(bits: u64) -> String {
    format!("0x{:016X}", bits)
}

fn box_numeric(code: &mut String, kind: Kind, value: &str, tmp: &str) -> String {
    match kind {
        Kind::Int => {
            writeln!(code, "  %{tmp} = call ptr @sz_box_i64(i64 {value})").unwrap();
            format!("%{tmp}")
        }
        Kind::Float => {
            writeln!(code, "  %{tmp}_bits = bitcast double {value} to i64").unwrap();
            writeln!(code, "  %{tmp} = call ptr @sz_box_i64(i64 %{tmp}_bits)").unwrap();
            format!("%{tmp}")
        }
        Kind::Ptr | Kind::Io => value.to_string(),
    }
}

fn unbox_numeric(code: &mut String, kind: Kind, ptr: &str, tmp: &str) -> String {
    match kind {
        Kind::Int => {
            writeln!(code, "  %{tmp} = call i64 @sz_unbox_i64(ptr {ptr})").unwrap();
            format!("%{tmp}")
        }
        Kind::Float => {
            writeln!(code, "  %{tmp}_bits = call i64 @sz_unbox_i64(ptr {ptr})").unwrap();
            writeln!(code, "  %{tmp} = bitcast i64 %{tmp}_bits to double").unwrap();
            format!("%{tmp}")
        }
        Kind::Ptr | Kind::Io => ptr.to_string(),
    }
}

fn as_f64(code: &mut String, kind: Kind, value: &str, tmp: &str) -> String {
    if kind == Kind::Float {
        return value.to_string();
    }
    unbox_numeric(code, Kind::Float, value, tmp)
}

fn stringify_scalar(code: &mut String, kind: Kind, value: &str, tmp: &str) -> String {
    match kind {
        Kind::Float => {
            writeln!(
                code,
                "  %{tmp} = call ptr @sz_string_from_float(double {value})"
            )
            .unwrap();
            format!("%{tmp}")
        }
        Kind::Int => {
            writeln!(code, "  %{tmp} = call ptr @sz_string_from_int(i64 {value})").unwrap();
            format!("%{tmp}")
        }
        Kind::Ptr | Kind::Io => value.to_string(),
    }
}

fn payload_of_type(ty: &Type) -> Kind {
    match ty {
        Type::Io(inner) => kind_of_type(inner),
        _ => Kind::Ptr,
    }
}

fn collect_strings(expr: &Expr, out: &mut Vec<String>) {
    match &expr.kind {
        ExprKind::StrLit(s) => {
            if !out.contains(s) {
                out.push(s.clone());
            }
        }
        ExprKind::Interpolate { parts } => {
            for part in parts {
                match part {
                    crate::ast::InterpPart::Lit(s) => {
                        if !out.contains(s) {
                            out.push(s.clone());
                        }
                    }
                    crate::ast::InterpPart::Expr(e) => collect_strings(e, out),
                }
            }
        }
        ExprKind::IoPrintln(e)
        | ExprKind::IoSleep(e)
        | ExprKind::IoFail(e)
        | ExprKind::IoPure(e)
        | ExprKind::Attempt { inner: e } => collect_strings(e, out),
        ExprKind::Lambda { body, .. } => collect_strings(body, out),
        ExprKind::FlatMap { inner, body, .. }
        | ExprKind::HandleErrorWith { inner, body, .. }
        | ExprKind::Let {
            value: inner, body, ..
        }
        | ExprKind::IoRace {
            left: inner,
            right: body,
        }
        | ExprKind::IoBoth {
            left: inner,
            right: body,
        }
        | ExprKind::IoEnsure {
            inner,
            finalizer: body,
        }
        | ExprKind::IoTimeout {
            ms: inner,
            inner: body,
        }
        | ExprKind::Binary {
            left: inner,
            right: body,
            ..
        } => {
            collect_strings(inner, out);
            collect_strings(body, out);
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            collect_strings(cond, out);
            collect_strings(then_branch, out);
            collect_strings(else_branch, out);
        }
        ExprKind::Match { scrutinee, arms } => {
            collect_strings(scrutinee, out);
            for a in arms {
                collect_strings(&a.body, out);
            }
        }
        ExprKind::Call { args, .. } => {
            for a in args {
                collect_strings(a, out);
            }
        }
        ExprKind::ListLit { elems } => {
            for e in elems {
                collect_strings(e, out);
            }
        }
        ExprKind::Unit
        | ExprKind::Var(_)
        | ExprKind::IntLit(_)
        | ExprKind::FloatLit(_)
        | ExprKind::BoolLit(_) => {}
        ExprKind::Field { base, .. } => collect_strings(base, out),
        ExprKind::MethodCall { receiver, args, .. } => {
            collect_strings(receiver, out);
            for a in args {
                collect_strings(a, out);
            }
        }
        ExprKind::AdtConstruct { args, .. } => {
            for a in args {
                collect_strings(a, out);
            }
        }
        ExprKind::For { binders, body } => {
            for b in binders {
                match b {
                    crate::ast::ForBinder::Eq { value, .. }
                    | crate::ast::ForBinder::Draw { value, .. } => collect_strings(value, out),
                }
            }
            collect_strings(body, out);
        }
    }
}

fn str_index(strs: &[String], s: &str) -> usize {
    strs.iter().position(|x| x == s).expect("string collected")
}

fn llvm_escape(s: &str) -> String {
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'\n' => out.push_str("\\0A"),
            b'\t' => out.push_str("\\09"),
            b'\\' => out.push_str("\\5C"),
            b'"' => out.push_str("\\22"),
            32..=126 => out.push(b as char),
            other => out.push_str(&format!("\\{other:02X}")),
        }
    }
    out
}

fn emit_fundef(def: &FunDef, ctx: &mut EmitCtx<'_>, out: &mut String) {
    let ret = llvm_type(&def.ret);
    let sym = user_symbol(&def.module, &def.name);
    write!(out, "define internal {ret} @{sym}(").unwrap();
    for (i, p) in def.params.iter().enumerate() {
        if i > 0 {
            write!(out, ", ").unwrap();
        }
        write!(out, "{} %{}", llvm_type(&p.ty), p.name).unwrap();
    }
    writeln!(out, ") {{").unwrap();
    writeln!(out, "entry:").unwrap();

    let mut locals: HashMap<String, Local> = HashMap::new();
    for p in &def.params {
        locals.insert(
            p.name.clone(),
            Local::borrow(format!("%{}", p.name), kind_of_type(&p.ty)),
        );
    }

    let body = emit_expr(&def.body, ctx, &mut locals, "body");
    out.push_str(&body.code);

    let ret_kind = kind_of_type(&def.ret);
    match ret_kind {
        Kind::Io => {
            let io = ensure_io(out, body.kind, &body.value, "ret_wrap", body.owned);
            writeln!(out, "  ret ptr {io}").unwrap();
        }
        Kind::Int => {
            let v = if body.kind == Kind::Int {
                body.value
            } else {
                writeln!(out, "  %ret_coerce = add i64 0, 0").unwrap();
                "%ret_coerce".into()
            };
            writeln!(out, "  ret i64 {v}").unwrap();
        }
        Kind::Float => {
            let v = if body.kind == Kind::Float {
                body.value
            } else {
                writeln!(out, "  %ret_fcoerce = bitcast i64 0 to double").unwrap();
                "%ret_fcoerce".into()
            };
            writeln!(out, "  ret double {v}").unwrap();
        }
        Kind::Ptr => {
            let v = if body.kind == Kind::Ptr || body.kind == Kind::Io {
                body.value
            } else if body.kind == Kind::Int || body.kind == Kind::Float {
                box_numeric(out, body.kind, &body.value, "ret_box")
            } else {
                "null".into()
            };
            if retain_borrowed_ret(&def.ret) && !body.owned {
                writeln!(out, "  call void @sz_retain(ptr {v})").unwrap();
            }
            writeln!(out, "  ret ptr {v}").unwrap();
        }
    }
    writeln!(out, "}}").unwrap();
    writeln!(out).unwrap();
}

fn pack_param_kinds(params: &[crate::ast::Param]) -> i64 {
    let mut kind = 0i64;
    let mut place = 1i64;
    for p in params {
        let k = match p.ty {
            Type::String => 1,
            Type::Bool => 2,
            _ => 0,
        };
        kind += k * place;
        place *= 4;
    }
    kind
}

fn emit_driver_registers(out: &mut String, program: &Program, strs: &[String]) {
    let mut i = 0usize;
    for d in program.defs.iter().filter(|d| d.is_driver) {
        emit_one_driver_register(out, d, strs, i, &user_symbol(&d.module, &d.name));
        i += 1;
    }
    for d in program
        .defs
        .iter()
        .filter(|d| d.is_law && !d.params.is_empty())
    {
        let tramp = format!("{}__drv", user_symbol(&d.module, &d.name));
        emit_one_driver_register(out, d, strs, i, &tramp);
        i += 1;
    }
}

fn emit_law_drive_tramps(out: &mut String, program: &Program, strs: &[String]) {
    for d in program
        .defs
        .iter()
        .filter(|d| d.is_law && !d.params.is_empty())
    {
        let tramp = format!("{}__drv", user_symbol(&d.module, &d.name));
        emit_law_drive_tramp(out, d, strs, &tramp);
    }
}

fn emit_one_driver_register(out: &mut String, d: &FunDef, strs: &[String], i: usize, sym: &str) {
    let idx = strs
        .iter()
        .position(|s| s == &d.name)
        .expect("driver name interned");
    let len = d.name.len() + 1;
    let nargs = d.params.len() as i64;
    let kind = pack_param_kinds(&d.params);
    writeln!(
        out,
        "  %drv{i}_gep = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
    )
    .unwrap();
    writeln!(
        out,
        "  %drv{i}_ss = call ptr @sz_string_from_cstr(ptr %drv{i}_gep)"
    )
    .unwrap();
    writeln!(
        out,
        "  call void @sz_driver_register(ptr %drv{i}_ss, i64 {nargs}, i64 {kind}, ptr @{sym})"
    )
    .unwrap();
}

fn emit_law_drive_tramp(out: &mut String, d: &FunDef, strs: &[String], tramp: &str) {
    let law = user_symbol(&d.module, &d.name);
    let idx = strs
        .iter()
        .position(|s| s == &d.name)
        .expect("law name interned");
    let len = d.name.len() + 1;
    if d.params.len() <= 1 {
        let (arg_ty, arg_name) = match d.params.first().map(|p| &p.ty) {
            Some(Type::String) => ("ptr", "%a"),
            Some(_) | None => ("i64", "%a"),
        };
        let params = if d.params.is_empty() {
            String::new()
        } else {
            format!("{arg_ty} {arg_name}")
        };
        writeln!(out, "define internal ptr @{tramp}({params}) {{").unwrap();
        writeln!(out, "entry:").unwrap();
        if d.params.is_empty() {
            writeln!(out, "  %ok = call i64 @{law}()").unwrap();
        } else {
            writeln!(out, "  %ok = call i64 @{law}({arg_ty} {arg_name})").unwrap();
        }
    } else {
        writeln!(out, "define internal ptr @{tramp}(ptr %args) {{").unwrap();
        writeln!(out, "entry:").unwrap();
        let mut cur = "%args".to_string();
        let mut call_args = Vec::new();
        for (i, p) in d.params.iter().enumerate() {
            writeln!(out, "  %h{i} = call ptr @sz_list_head(ptr {cur})").unwrap();
            writeln!(out, "  %t{i} = call ptr @sz_list_tail(ptr {cur})").unwrap();
            match p.ty {
                Type::String => {
                    call_args.push(format!("ptr %h{i}"));
                }
                _ => {
                    writeln!(out, "  %v{i} = call i64 @sz_unbox_i64(ptr %h{i})").unwrap();
                    call_args.push(format!("i64 %v{i}"));
                }
            }
            cur = format!("%t{i}");
        }
        writeln!(out, "  %ok = call i64 @{law}({})", call_args.join(", ")).unwrap();
    }
    writeln!(
        out,
        "  %nm_gep = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
    )
    .unwrap();
    writeln!(out, "  %nm = call ptr @sz_string_from_cstr(ptr %nm_gep)").unwrap();
    writeln!(out, "  %io = call ptr @sz_law_assert(ptr %nm, i64 %ok)").unwrap();
    writeln!(out, "  ret ptr %io").unwrap();
    writeln!(out, "}}").unwrap();
}

fn ensure_io(code: &mut String, kind: Kind, value: &str, tmp: &str, owned: bool) -> String {
    if kind == Kind::Io {
        return value.to_string();
    }
    let ptr = if kind == Kind::Int || kind == Kind::Float {
        box_numeric(code, kind, value, &format!("{tmp}_box"))
    } else {
        value.to_string()
    };
    writeln!(code, "  %{tmp} = call ptr @sz_io_pure(ptr {ptr})").unwrap();
    if kind == Kind::Int || kind == Kind::Float {
        writeln!(code, "  call void @sz_release(ptr {ptr})").unwrap();
    } else if owned && kind == Kind::Ptr {
        writeln!(code, "  call void @sz_release(ptr {value})").unwrap();
    }
    format!("%{tmp}")
}

/// Stable capture order for packing/unpacking flatMap/handleErrorWith env lists.
fn capture_name_order(locals: &HashMap<String, Local>) -> Vec<String> {
    let mut names: Vec<String> = locals.keys().cloned().collect();
    names.sort();
    names
}

/// Pack enclosing locals into a `SzList` (head = first capture name). Ints are boxed.
fn pack_env(
    code: &mut String,
    locals: &HashMap<String, Local>,
    names: &[String],
    prefix: &str,
) -> String {
    if names.is_empty() {
        return "null".into();
    }
    writeln!(code, "  %{prefix}_0 = call ptr @sz_list_nil()").unwrap();
    let mut cur = format!("%{prefix}_0");
    for (i, name) in names.iter().enumerate().rev() {
        let loc = locals.get(name).expect("capture name");
        let ptr = if loc.kind == Kind::Int || loc.kind == Kind::Float {
            box_numeric(code, loc.kind, &loc.value, &format!("{prefix}_b{i}"))
        } else {
            loc.value.clone()
        };
        writeln!(
            code,
            "  %{prefix}_{} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})",
            i + 1
        )
        .unwrap();
        writeln!(code, "  call void @sz_release(ptr {cur})").unwrap();
        cur = format!("%{prefix}_{}", i + 1);
    }
    cur
}

fn emit_list_lit(
    elems: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let mut code = String::new();
    writeln!(code, "  %{prefix}_0 = call ptr @sz_list_nil()").unwrap();
    let mut cur = format!("%{prefix}_0");
    for (i, elem) in elems.iter().enumerate().rev() {
        let ee = emit_expr(elem, ctx, locals, &format!("{prefix}_e{i}"));
        code.push_str(&ee.code);
        let ptr = if ee.kind == Kind::Int || ee.kind == Kind::Float {
            box_numeric(&mut code, ee.kind, &ee.value, &format!("{prefix}_b{i}"))
        } else {
            ee.value.clone()
        };
        let next = format!("%{prefix}_{}", i + 1);
        writeln!(
            code,
            "  {next} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})"
        )
        .unwrap();
        writeln!(code, "  call void @sz_release(ptr {cur})").unwrap();
        if ee.kind == Kind::Int || ee.kind == Kind::Float {
            writeln!(code, "  call void @sz_release(ptr {ptr})").unwrap();
        } else {
            drop_owned_ptr(&mut code, &ee);
        }
        cur = next;
    }
    owned_ptr(code, cur)
}

/// Unpack `%env` list into `body_locals` (mirrors [`pack_env`] order).
fn unpack_env_preamble(
    pre: &mut String,
    body_locals: &mut HashMap<String, Local>,
    outer: &HashMap<String, Local>,
    names: &[String],
    prefix: &str,
) {
    if names.is_empty() {
        return;
    }
    let mut cur = "%env".to_string();
    for (i, name) in names.iter().enumerate() {
        let kind = outer.get(name).map(|l| l.kind).unwrap_or(Kind::Ptr);
        writeln!(pre, "  %{prefix}_h{i} = call ptr @sz_list_head(ptr {cur})").unwrap();
        writeln!(pre, "  %{prefix}_t{i} = call ptr @sz_list_tail(ptr {cur})").unwrap();
        match kind {
            Kind::Int | Kind::Float => {
                let v = unbox_numeric(pre, kind, &format!("%{prefix}_h{i}"), name);
                body_locals.insert(name.clone(), Local::borrow(v, kind));
            }
            Kind::Ptr | Kind::Io => {
                body_locals.insert(name.clone(), Local::borrow(format!("%{prefix}_h{i}"), kind));
            }
        }
        cur = format!("%{prefix}_t{i}");
    }
}

fn emit_sz_string(
    code: &mut String,
    strs: &[String],
    expr: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> (String, bool) {
    if let ExprKind::StrLit(s) = &expr.kind {
        let idx = str_index(strs, s);
        let len = s.len() + 1;
        writeln!(
            code,
            "  %{prefix}_gep = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
        )
        .unwrap();
        writeln!(
            code,
            "  %{prefix}_ss = call ptr @sz_string_from_cstr(ptr %{prefix}_gep)"
        )
        .unwrap();
        return (format!("%{prefix}_ss"), true);
    }
    let se = emit_expr(expr, ctx, locals, &format!("{prefix}_e"));
    code.push_str(&se.code);
    (se.value, se.owned)
}

fn emit_match(
    scrutinee: &Expr,
    arms: &[MatchArm],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let se = emit_expr(scrutinee, ctx, locals, &format!("{prefix}_sc"));
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let mut code = se.code;
    let merge = format!("{prefix}_merge_{id}");
    let default_label = format!("{prefix}_default_{id}");
    let first = if arms.is_empty() {
        default_label.clone()
    } else {
        format!("{prefix}_try_{id}_0")
    };
    writeln!(code, "  br label %{first}").unwrap();

    let mut arm_emits: Vec<(String, String, Emitted)> = Vec::new();
    let mut result_kind = Kind::Io;
    let mut result_payload = Kind::Ptr;
    let mut result_payload_owned = !arms.is_empty();
    let mut any_arm_owned = false;
    let mut all_arms_owned = !arms.is_empty();

    for (i, arm) in arms.iter().enumerate() {
        let try_l = format!("{prefix}_try_{id}_{i}");
        let next_l = if i + 1 < arms.len() {
            format!("{prefix}_try_{id}_{}", i + 1)
        } else {
            default_label.clone()
        };
        let ok_l = format!("{prefix}_ok_{id}_{i}");
        let join_l = format!("{prefix}_aj_{id}_{i}");
        writeln!(code, "{try_l}:").unwrap();
        let mut bound_names: Vec<String> = Vec::new();
        emit_pat(
            &arm.pattern,
            &se.value,
            Kind::Ptr,
            ctx,
            locals,
            &format!("{prefix}_p{id}_{i}"),
            &ok_l,
            &next_l,
            &mut code,
            &mut bound_names,
        );
        let ae = emit_expr(&arm.body, ctx, locals, &format!("{prefix}_a{id}_{i}"));
        for b in &bound_names {
            locals.remove(b);
        }
        if arm_emits.is_empty() {
            result_kind = ae.kind;
            result_payload = ae.payload;
        }
        result_payload_owned = result_payload_owned && ae.payload_owned;
        if ae.owned && ae.kind == Kind::Ptr {
            any_arm_owned = true;
        } else {
            all_arms_owned = false;
        }
        arm_emits.push((ok_l, join_l, ae));
    }

    writeln!(code, "{default_label}:").unwrap();
    let dflt = match result_kind {
        Kind::Int => {
            writeln!(code, "  %{prefix}_dflt = add i64 0, 0").unwrap();
            format!("%{prefix}_dflt")
        }
        Kind::Float => {
            writeln!(code, "  %{prefix}_dflt = bitcast i64 0 to double").unwrap();
            format!("%{prefix}_dflt")
        }
        Kind::Ptr => "null".into(),
        Kind::Io => {
            writeln!(code, "  %{prefix}_dflt = call ptr @sz_io_pure(ptr null)").unwrap();
            format!("%{prefix}_dflt")
        }
    };
    writeln!(code, "  br label %{merge}").unwrap();

    let mixed_ptr = result_kind == Kind::Ptr && any_arm_owned && !all_arms_owned;
    let arms_provide = result_kind == Kind::Ptr && any_arm_owned;
    let mut phi_parts: Vec<(String, String)> = Vec::new();
    for (ok_l, join_l, ae) in &arm_emits {
        writeln!(code, "{ok_l}:").unwrap();
        code.push_str(&ae.code);
        if mixed_ptr && !ae.owned {
            writeln!(code, "  call void @sz_retain(ptr {})", ae.value).unwrap();
        }
        writeln!(code, "  br label %{join_l}").unwrap();
        writeln!(code, "{join_l}:").unwrap();
        writeln!(code, "  br label %{merge}").unwrap();
        phi_parts.push((ae.value.clone(), join_l.clone()));
    }
    phi_parts.push((dflt, default_label));

    let ty = llvm_kind_ty(result_kind);
    writeln!(code, "{merge}:").unwrap();
    write!(code, "  %{prefix}_phi = phi {ty}").unwrap();
    for (i, (val, lab)) in phi_parts.iter().enumerate() {
        if i > 0 {
            write!(code, ",").unwrap();
        }
        write!(code, " [ {val}, %{lab} ]").unwrap();
    }
    writeln!(code).unwrap();
    if se.owned {
        if result_kind == Kind::Ptr && !arms_provide {
            writeln!(code, "  call void @sz_retain(ptr %{prefix}_phi)").unwrap();
        }
        writeln!(code, "  call void @sz_release(ptr {})", se.value).unwrap();
    }
    Emitted {
        code,
        value: format!("%{prefix}_phi"),
        kind: result_kind,
        payload: result_payload,
        payload_owned: result_kind == Kind::Io && result_payload_owned,
        owned: result_kind == Kind::Ptr && (arms_provide || se.owned),
    }
}

fn emit_pat(
    pat: &Pattern,
    value: &str,
    kind: Kind,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
    ok_label: &str,
    fail_label: &str,
    code: &mut String,
    bound_names: &mut Vec<String>,
) {
    match pat {
        Pattern::Wildcard => {
            writeln!(code, "  br label %{ok_label}").unwrap();
        }
        Pattern::Bind(name) => {
            locals.insert(name.clone(), Local::borrow(value, kind));
            bound_names.push(name.clone());
            writeln!(code, "  br label %{ok_label}").unwrap();
        }
        Pattern::Adt {
            enum_name,
            case_name,
            binds,
            ..
        } => {
            let sid = *ctx.cont_id;
            *ctx.cont_id += 1;
            let Some(&expected) = ctx.enum_tags.get(&(enum_name.clone(), case_name.clone())) else {
                writeln!(code, "  br label %{fail_label}").unwrap();
                return;
            };
            let tag = format!("{prefix}_tg{sid}");
            let cmp = format!("{prefix}_eq{sid}");
            let matched = format!("{prefix}_m{sid}");
            writeln!(code, "  %{tag} = call i32 @sz_adt_tag(ptr {value})").unwrap();
            writeln!(code, "  %{cmp} = icmp eq i32 %{tag}, {expected}").unwrap();
            writeln!(
                code,
                "  br i1 %{cmp}, label %{matched}, label %{fail_label}"
            )
            .unwrap();
            writeln!(code, "{matched}:").unwrap();
            if binds.is_empty() {
                writeln!(code, "  br label %{ok_label}").unwrap();
                return;
            }
            let fields = emit_payload_fields(
                enum_name,
                case_name,
                binds.len(),
                value,
                ctx,
                &format!("{prefix}_f{sid}"),
                code,
            );
            for (fi, nested) in binds.iter().enumerate() {
                let (fval, fkind) = fields.get(fi).cloned().unwrap_or((value.to_string(), kind));
                let next_ok = if fi + 1 == binds.len() {
                    ok_label.to_string()
                } else {
                    format!("{prefix}_n{sid}_{fi}")
                };
                emit_pat(
                    nested,
                    &fval,
                    fkind,
                    ctx,
                    locals,
                    &format!("{prefix}_n{sid}_{fi}"),
                    &next_ok,
                    fail_label,
                    code,
                    bound_names,
                );
                if fi + 1 != binds.len() {
                    writeln!(code, "{next_ok}:").unwrap();
                }
            }
        }
    }
}

fn emit_payload_fields(
    enum_name: &str,
    case_name: &str,
    nbinds: usize,
    adt_value: &str,
    ctx: &EmitCtx<'_>,
    prefix: &str,
    code: &mut String,
) -> Vec<(String, Kind)> {
    let field_tys = ctx
        .enum_payloads
        .get(&(enum_name.to_string(), case_name.to_string()))
        .cloned()
        .unwrap_or_default();
    writeln!(
        code,
        "  %{prefix}_pl = call ptr @sz_adt_payload(ptr {adt_value})"
    )
    .unwrap();
    let mut out = Vec::with_capacity(nbinds);
    if nbinds == 1 {
        match field_tys.first() {
            Some(Type::Int) | Some(Type::Bool) | Some(Type::Float) => {
                let k = if matches!(field_tys.first(), Some(Type::Float)) {
                    Kind::Float
                } else {
                    Kind::Int
                };
                let v = unbox_numeric(code, k, &format!("%{prefix}_pl"), &format!("{prefix}_b0"));
                out.push((v, k));
            }
            _ => out.push((format!("%{prefix}_pl"), Kind::Ptr)),
        }
    } else {
        for fi in 0..nbinds {
            let cell = format!("{prefix}_c{fi}");
            writeln!(
                code,
                "  %{cell} = call ptr @sz_list_at(ptr %{prefix}_pl, i64 {fi})"
            )
            .unwrap();
            match field_tys.get(fi) {
                Some(Type::Int) | Some(Type::Bool) | Some(Type::Float) => {
                    let k = if matches!(field_tys.get(fi), Some(Type::Float)) {
                        Kind::Float
                    } else {
                        Kind::Int
                    };
                    let v = unbox_numeric(code, k, &format!("%{cell}"), &format!("{prefix}_b{fi}"));
                    out.push((v, k));
                }
                _ => out.push((format!("%{cell}"), Kind::Ptr)),
            }
        }
    }
    out
}

fn emit_expr(
    expr: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    match &expr.kind {
        ExprKind::Unit => val_emitted(String::new(), "null".into(), Kind::Ptr),
        ExprKind::IntLit(n) => val_emitted(String::new(), format!("{n}"), Kind::Int),
        ExprKind::FloatLit(bits) => {
            val_emitted(String::new(), llvm_double_const(*bits), Kind::Float)
        }
        ExprKind::BoolLit(b) => val_emitted(
            String::new(),
            if *b { "1".into() } else { "0".into() },
            Kind::Int,
        ),
        ExprKind::StrLit(s) => {
            let idx = str_index(ctx.strs, s);
            let len = s.len() + 1;
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_gep = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_s = call ptr @sz_string_from_cstr(ptr %{prefix}_gep)"
            )
            .unwrap();
            owned_ptr(code, format!("%{prefix}_s"))
        }
        ExprKind::ListLit { elems } => emit_list_lit(elems, ctx, locals, prefix),
        ExprKind::Interpolate { parts } => emit_interpolate(parts, ctx, locals, prefix),
        ExprKind::IoSleep(ms) => {
            let me = emit_expr(ms, ctx, locals, &format!("{prefix}_ms"));
            let mut code = me.code;
            let ms_val = if me.kind == Kind::Int {
                me.value
            } else {
                writeln!(code, "  %{prefix}_ms0 = add i64 0, 0").unwrap();
                format!("%{prefix}_ms0")
            };
            writeln!(
                code,
                "  %{prefix}_sleep = call ptr @sz_io_sleep_ms(i64 {ms_val})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_sleep"), Kind::Ptr)
        }
        ExprKind::IoFail(msg) => {
            let mut code = String::new();
            let (s, owned) = emit_sz_string(&mut code, ctx.strs, msg, ctx, locals, prefix);
            writeln!(code, "  %{prefix}_cstr = call ptr @sz_string_cstr(ptr {s})").unwrap();
            writeln!(
                code,
                "  %{prefix}_err = call ptr @sz_error_new(i32 1, ptr %{prefix}_cstr)"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @sz_io_fail(ptr %{prefix}_err)"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr %{prefix}_err)").unwrap();
            if owned {
                writeln!(code, "  call void @sz_release(ptr {s})").unwrap();
            }
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        ExprKind::IoPrintln(arg) => {
            let mut code = String::new();
            let (s, owned) = emit_sz_string(&mut code, ctx.strs, arg, ctx, locals, prefix);
            writeln!(code, "  %{prefix}_io = call ptr @sz_io_println(ptr {s})").unwrap();
            if owned {
                writeln!(code, "  call void @sz_release(ptr {s})").unwrap();
            }
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        ExprKind::IoPure(inner) => {
            let ie = emit_expr(inner, ctx, locals, &format!("{prefix}_p"));
            let mut code = ie.code;
            let ptr = if ie.kind == Kind::Int || ie.kind == Kind::Float {
                box_numeric(&mut code, ie.kind, &ie.value, &format!("{prefix}_box"))
            } else {
                ie.value.clone()
            };
            writeln!(code, "  %{prefix}_io = call ptr @sz_io_pure(ptr {ptr})").unwrap();
            if ie.kind == Kind::Int || ie.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {ptr})").unwrap();
            } else if ie.owned && ie.kind == Kind::Ptr {
                writeln!(code, "  call void @sz_release(ptr {})", ie.value).unwrap();
            }
            let payload = if ie.kind == Kind::Io {
                ie.payload
            } else {
                ie.kind
            };
            io_emitted_payload(code, format!("%{prefix}_io"), payload, payload == Kind::Ptr)
        }
        ExprKind::AdtConstruct {
            enum_name,
            case_name,
            args,
            ..
        } => {
            let tag = ctx
                .enum_tags
                .get(&(enum_name.clone(), case_name.clone()))
                .copied()
                .unwrap_or(0);
            let mut code = String::new();
            let mut extra_box: Option<String> = None;
            let mut extra_pack: Option<String> = None;
            let mut extra_owned: Option<Emitted> = None;
            let payload_ptr = if args.is_empty() {
                "null".to_string()
            } else {
                let field_tys = ctx
                    .enum_payloads
                    .get(&(enum_name.clone(), case_name.clone()))
                    .cloned()
                    .unwrap_or_default();
                if args.len() == 1 {
                    let ae = emit_expr(&args[0], ctx, locals, &format!("{prefix}_ap"));
                    code.push_str(&ae.code);
                    match field_tys.first() {
                        Some(Type::Int) | Some(Type::Bool) | Some(Type::Float) => {
                            let want = kind_of_type(field_tys.first().unwrap());
                            let v = if ae.kind == want {
                                ae.value.clone()
                            } else if want == Kind::Float {
                                writeln!(code, "  %{prefix}_ap0 = bitcast i64 0 to double")
                                    .unwrap();
                                format!("%{prefix}_ap0")
                            } else {
                                writeln!(code, "  %{prefix}_ap0 = add i64 0, 0").unwrap();
                                format!("%{prefix}_ap0")
                            };
                            let boxed = box_numeric(&mut code, want, &v, &format!("{prefix}_box"));
                            extra_box = Some(boxed.clone());
                            boxed
                        }
                        _ => {
                            let p = ae.value.clone();
                            extra_owned = Some(ae);
                            p
                        }
                    }
                } else {
                    // N>=2: pack field values into a List (Ints boxed).
                    writeln!(code, "  %{prefix}_pl0 = call ptr @sz_list_nil()").unwrap();
                    let mut cur = format!("%{prefix}_pl0");
                    for (i, arg) in args.iter().enumerate().rev() {
                        let ae = emit_expr(arg, ctx, locals, &format!("{prefix}_ap{i}"));
                        code.push_str(&ae.code);
                        let numeric = matches!(
                            field_tys.get(i),
                            Some(Type::Int) | Some(Type::Bool) | Some(Type::Float)
                        );
                        let ptr = if numeric {
                            let want = kind_of_type(field_tys.get(i).unwrap());
                            let v = if ae.kind == want {
                                ae.value.clone()
                            } else if want == Kind::Float {
                                writeln!(code, "  %{prefix}_ap{i}i = bitcast i64 0 to double")
                                    .unwrap();
                                format!("%{prefix}_ap{i}i")
                            } else {
                                writeln!(code, "  %{prefix}_ap{i}i = add i64 0, 0").unwrap();
                                format!("%{prefix}_ap{i}i")
                            };
                            box_numeric(&mut code, want, &v, &format!("{prefix}_bx{i}"))
                        } else {
                            ae.value.clone()
                        };
                        let next = format!("%{prefix}_pl{}", args.len() - i);
                        writeln!(
                            code,
                            "  {next} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})"
                        )
                        .unwrap();
                        writeln!(code, "  call void @sz_release(ptr {cur})").unwrap();
                        if numeric {
                            writeln!(code, "  call void @sz_release(ptr {ptr})").unwrap();
                        } else {
                            drop_owned_ptr(&mut code, &ae);
                        }
                        cur = next;
                    }
                    extra_pack = Some(cur.clone());
                    cur
                }
            };
            writeln!(
                code,
                "  %{prefix}_adt = call ptr @sz_adt_new(i32 {tag}, ptr {payload_ptr})"
            )
            .unwrap();
            if let Some(b) = extra_box {
                writeln!(code, "  call void @sz_release(ptr {b})").unwrap();
            }
            if let Some(e) = extra_owned {
                drop_owned_ptr(&mut code, &e);
            }
            if let Some(p) = extra_pack {
                writeln!(code, "  call void @sz_release(ptr {p})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_adt"))
        }
        ExprKind::Var(name) => {
            let loc = locals
                .get(name)
                .cloned()
                .unwrap_or_else(|| Local::borrow("null", Kind::Ptr));
            val_emitted(String::new(), loc.value, loc.kind)
        }
        ExprKind::Field { .. } => panic!("internal: unresolved field access in codegen"),
        ExprKind::MethodCall { .. } => panic!("internal: unresolved method call in codegen"),
        ExprKind::Let { name, value, body } => {
            // Nested vals must not reuse the same LLVM name prefix.
            let ve = emit_expr(value, ctx, locals, &format!("{prefix}_lv_{name}"));
            let mut code = ve.code;
            locals.insert(
                name.clone(),
                Local {
                    value: ve.value.clone(),
                    kind: ve.kind,
                    owned: ve.owned,
                },
            );
            let mut be = emit_expr(body, ctx, locals, &format!("{prefix}_l_{name}"));
            let bound = locals.remove(name);
            code.push_str(&be.code);
            if let Some(bound) = bound {
                if bound.owned && bound.kind == Kind::Ptr {
                    if be.value == bound.value {
                        be.owned = true;
                    } else {
                        if be.kind == Kind::Ptr && !be.owned {
                            writeln!(code, "  call void @sz_retain(ptr {})", be.value).unwrap();
                            be.owned = true;
                        }
                        writeln!(code, "  call void @sz_release(ptr {})", bound.value).unwrap();
                    }
                }
            }
            Emitted {
                code,
                value: be.value,
                kind: be.kind,
                payload: be.payload,
                payload_owned: be.payload_owned,
                owned: be.owned,
            }
        }
        ExprKind::If {
            cond,
            then_branch,
            else_branch,
        } => {
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let ce = emit_expr(cond, ctx, locals, &format!("{prefix}_ic"));
            let mut code = ce.code;
            let cond_i64 = as_i64(&mut code, ce.kind, &ce.value, &format!("{prefix}_c0"));
            let then_l = format!("{prefix}_then_{id}");
            let else_l = format!("{prefix}_else_{id}");
            // Join blocks so nested if/match inside a branch are valid PHI preds.
            let then_join = format!("{prefix}_tj_{id}");
            let else_join = format!("{prefix}_ej_{id}");
            let merge = format!("{prefix}_merge_{id}");
            writeln!(code, "  %{prefix}_cmp = icmp ne i64 {cond_i64}, 0").unwrap();
            writeln!(
                code,
                "  br i1 %{prefix}_cmp, label %{then_l}, label %{else_l}"
            )
            .unwrap();

            let te = emit_expr(then_branch, ctx, locals, &format!("{prefix}_t{id}"));
            let ee = emit_expr(else_branch, ctx, locals, &format!("{prefix}_e{id}"));
            let kind = te.kind;
            let mixed_ptr = kind == Kind::Ptr && te.owned != ee.owned;

            writeln!(code, "{then_l}:").unwrap();
            code.push_str(&te.code);
            if mixed_ptr && !te.owned {
                writeln!(code, "  call void @sz_retain(ptr {})", te.value).unwrap();
            }
            writeln!(code, "  br label %{then_join}").unwrap();
            writeln!(code, "{then_join}:").unwrap();
            writeln!(code, "  br label %{merge}").unwrap();

            writeln!(code, "{else_l}:").unwrap();
            code.push_str(&ee.code);
            if mixed_ptr && !ee.owned {
                writeln!(code, "  call void @sz_retain(ptr {})", ee.value).unwrap();
            }
            writeln!(code, "  br label %{else_join}").unwrap();
            writeln!(code, "{else_join}:").unwrap();
            writeln!(code, "  br label %{merge}").unwrap();

            let payload = te.payload;
            let ty = llvm_kind_ty(kind);
            writeln!(code, "{merge}:").unwrap();
            writeln!(
                code,
                "  %{prefix}_phi = phi {ty} [ {}, %{then_join} ], [ {}, %{else_join} ]",
                te.value, ee.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_phi"),
                kind,
                payload,
                payload_owned: te.payload_owned && ee.payload_owned,
                owned: kind == Kind::Ptr && (te.owned || ee.owned),
            }
        }
        ExprKind::Lambda { param, body } => emit_lambda(param, body, ctx, locals, prefix),
        ExprKind::Binary { op, left, right } => emit_binary(op, left, right, ctx, locals, prefix),
        ExprKind::Call { callee, args } => emit_call(callee, args, ctx, locals, prefix),
        ExprKind::For { .. } => panic!("internal: unlowered `for` in codegen"),
        ExprKind::Match { scrutinee, arms } => emit_match(scrutinee, arms, ctx, locals, prefix),
        ExprKind::FlatMap { inner, param, body } => {
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let cont_name = format!("sz_cont_{id}");

            let inner_emitted = emit_expr(inner, ctx, locals, &format!("{prefix}_in"));
            let payload_kind = inner_emitted.payload;
            let payload_owned = inner_emitted.payload_owned;

            // Cont is a separate LLVM function. Capture enclosing locals with %env list.
            let capture_names = capture_name_order(locals);
            let mut pre = String::new();
            let mut body_locals: HashMap<String, Local> = HashMap::new();
            unpack_env_preamble(
                &mut pre,
                &mut body_locals,
                locals,
                &capture_names,
                &format!("c{id}"),
            );

            if let Some(p) = param {
                if payload_kind == Kind::Int || payload_kind == Kind::Float {
                    let v = unbox_numeric(&mut pre, payload_kind, "%value", p);
                    body_locals.insert(p.clone(), Local::borrow(v, payload_kind));
                } else if payload_owned {
                    body_locals.insert(p.clone(), Local::owned("%value", Kind::Ptr));
                } else {
                    body_locals.insert(p.clone(), Local::borrow("%value", Kind::Ptr));
                }
            }

            let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("c{id}"));

            writeln!(
                ctx.conts,
                "define internal ptr @{cont_name}(ptr %value, ptr %env) {{"
            )
            .unwrap();
            writeln!(ctx.conts, "entry:").unwrap();
            ctx.conts.push_str(&pre);
            ctx.conts.push_str(&body_emitted.code);
            let ret = ensure_io(
                ctx.conts,
                body_emitted.kind,
                &body_emitted.value,
                &format!("c{id}_wrap"),
                body_emitted.owned,
            );
            if let Some(p) = param {
                if let Some(bound) = body_locals.get(p) {
                    if bound.owned && bound.kind == Kind::Ptr && ret != bound.value {
                        writeln!(ctx.conts, "  call void @sz_release(ptr {})", bound.value)
                            .unwrap();
                    }
                }
            } else if payload_owned {
                writeln!(ctx.conts, "  call void @sz_release(ptr %value)").unwrap();
            }
            writeln!(ctx.conts, "  ret ptr {ret}").unwrap();
            writeln!(ctx.conts, "}}").unwrap();
            writeln!(ctx.conts).unwrap();

            let mut code = inner_emitted.code;
            let inner_io = ensure_io(
                &mut code,
                inner_emitted.kind,
                &inner_emitted.value,
                &format!("{prefix}_inio"),
                inner_emitted.owned,
            );
            let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
            if env_ptr != "null" {
                writeln!(code, "  call void @sz_retain(ptr {env_ptr})").unwrap();
            }
            writeln!(
                code,
                "  %{prefix}_fm = call ptr @sz_io_flatmap(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {inner_io})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_fm"),
                body_emitted.payload,
                body_emitted.payload_owned,
            )
        }
        ExprKind::HandleErrorWith { inner, param, body } => {
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let cont_name = format!("sz_err_{id}");
            let capture_names = capture_name_order(locals);
            let mut pre = String::new();
            let mut body_locals = HashMap::new();
            unpack_env_preamble(
                &mut pre,
                &mut body_locals,
                locals,
                &capture_names,
                &format!("e{id}"),
            );
            if let Some(p) = param {
                writeln!(pre, "  %{p}_msg = call ptr @sz_error_message(ptr %err)").unwrap();
                body_locals.insert(p.clone(), Local::owned(format!("%{p}_msg"), Kind::Ptr));
            }
            let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("e{id}"));
            writeln!(
                ctx.conts,
                "define internal ptr @{cont_name}(ptr %err, ptr %env) {{"
            )
            .unwrap();
            writeln!(ctx.conts, "entry:").unwrap();
            ctx.conts.push_str(&pre);
            ctx.conts.push_str(&body_emitted.code);
            let ret = ensure_io(
                ctx.conts,
                body_emitted.kind,
                &body_emitted.value,
                &format!("e{id}_wrap"),
                body_emitted.owned,
            );
            if let Some(p) = param {
                if let Some(bound) = body_locals.get(p) {
                    if bound.owned && bound.kind == Kind::Ptr && ret != bound.value {
                        writeln!(ctx.conts, "  call void @sz_release(ptr {})", bound.value)
                            .unwrap();
                    }
                }
            }
            writeln!(ctx.conts, "  ret ptr {ret}").unwrap();
            writeln!(ctx.conts, "}}").unwrap();
            writeln!(ctx.conts).unwrap();

            let inner_emitted = emit_expr(inner, ctx, locals, &format!("{prefix}_he"));
            let mut code = inner_emitted.code;
            let inner_io = ensure_io(
                &mut code,
                inner_emitted.kind,
                &inner_emitted.value,
                &format!("{prefix}_heio"),
                inner_emitted.owned,
            );
            let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_ecap"));
            if env_ptr != "null" {
                writeln!(code, "  call void @sz_retain(ptr {env_ptr})").unwrap();
            }
            writeln!(
                code,
                "  %{prefix}_h = call ptr @sz_io_handle_error_with(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {inner_io})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_h"),
                body_emitted.payload,
                body_emitted.payload_owned,
            )
        }
        ExprKind::Attempt { inner } => {
            let inner_emitted = emit_expr(inner, ctx, locals, &format!("{prefix}_at"));
            let mut code = inner_emitted.code;
            let inner_io = ensure_io(
                &mut code,
                inner_emitted.kind,
                &inner_emitted.value,
                &format!("{prefix}_atio"),
                inner_emitted.owned,
            );
            writeln!(
                code,
                "  %{prefix}_attempt = call ptr @sz_io_attempt_as_result(ptr {inner_io})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {inner_io})").unwrap();
            io_emitted_payload(code, format!("%{prefix}_attempt"), Kind::Ptr, true)
        }
        ExprKind::IoRace { left, right } => {
            let le = emit_expr(left, ctx, locals, &format!("{prefix}_rl"));
            let re = emit_expr(right, ctx, locals, &format!("{prefix}_rr"));
            let mut code = le.code;
            code.push_str(&re.code);
            let lv = ensure_io(
                &mut code,
                le.kind,
                &le.value,
                &format!("{prefix}_rlio"),
                le.owned,
            );
            let rv = ensure_io(
                &mut code,
                re.kind,
                &re.value,
                &format!("{prefix}_rrio"),
                re.owned,
            );
            writeln!(
                code,
                "  %{prefix}_race = call ptr @sz_io_race(ptr {lv}, ptr {rv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {lv})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {rv})").unwrap();
            io_emitted(code, format!("%{prefix}_race"), Kind::Ptr)
        }
        ExprKind::IoBoth { left, right } => {
            let le = emit_expr(left, ctx, locals, &format!("{prefix}_bl"));
            let re = emit_expr(right, ctx, locals, &format!("{prefix}_br"));
            let mut code = le.code;
            code.push_str(&re.code);
            let lv = ensure_io(
                &mut code,
                le.kind,
                &le.value,
                &format!("{prefix}_blio"),
                le.owned,
            );
            let rv = ensure_io(
                &mut code,
                re.kind,
                &re.value,
                &format!("{prefix}_brio"),
                re.owned,
            );
            writeln!(
                code,
                "  %{prefix}_both = call ptr @sz_io_both(ptr {lv}, ptr {rv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {lv})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {rv})").unwrap();
            io_emitted_payload(code, format!("%{prefix}_both"), Kind::Ptr, true)
        }
        ExprKind::IoEnsure { inner, finalizer } => {
            let ie = emit_expr(inner, ctx, locals, &format!("{prefix}_ei"));
            let fe = emit_expr(finalizer, ctx, locals, &format!("{prefix}_ef"));
            let mut code = ie.code;
            code.push_str(&fe.code);
            let iv = ensure_io(
                &mut code,
                ie.kind,
                &ie.value,
                &format!("{prefix}_eiio"),
                ie.owned,
            );
            let fv = ensure_io(
                &mut code,
                fe.kind,
                &fe.value,
                &format!("{prefix}_efio"),
                fe.owned,
            );
            writeln!(
                code,
                "  %{prefix}_ensure = call ptr @sz_io_ensure(ptr {iv}, ptr {fv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {fv})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_ensure"),
                ie.payload,
                ie.payload_owned,
            )
        }
        ExprKind::IoTimeout { ms, inner } => {
            let me = emit_expr(ms, ctx, locals, &format!("{prefix}_ms"));
            let ie = emit_expr(inner, ctx, locals, &format!("{prefix}_ti"));
            let mut code = me.code;
            code.push_str(&ie.code);
            let ms_val = if me.kind == Kind::Int {
                me.value
            } else {
                writeln!(code, "  %{prefix}_ms0 = add i64 0, 0").unwrap();
                format!("%{prefix}_ms0")
            };
            let iv = ensure_io(
                &mut code,
                ie.kind,
                &ie.value,
                &format!("{prefix}_tiio"),
                ie.owned,
            );
            writeln!(
                code,
                "  %{prefix}_timeout = call ptr @sz_io_timeout(i64 {ms_val}, ptr {iv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_timeout"),
                ie.payload,
                ie.payload_owned,
            )
        }
    }
}

/// Emit `s"..."` as left-fold `sz_string_concat`. Coerce Int / Float holes.
fn emit_interpolate(
    parts: &[crate::ast::InterpPart],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    if parts.is_empty() {
        return emit_expr(
            &Expr::dummy(ExprKind::StrLit(String::new())),
            ctx,
            locals,
            prefix,
        );
    }
    if parts.len() == 1 {
        match &parts[0] {
            crate::ast::InterpPart::Lit(s) => {
                return emit_expr(
                    &Expr::dummy(ExprKind::StrLit(s.clone())),
                    ctx,
                    locals,
                    prefix,
                );
            }
            crate::ast::InterpPart::Expr(e) => {
                let ee = emit_expr(e, ctx, locals, &format!("{prefix}_p0"));
                if ee.kind == Kind::Ptr {
                    return ee;
                }
                let mut code = ee.code;
                let s = stringify_scalar(&mut code, ee.kind, &ee.value, &format!("{prefix}_s"));
                return owned_ptr(code, s);
            }
        }
    }

    let mut code = String::new();
    let mut acc: Option<(String, bool)> = None;
    for (i, part) in parts.iter().enumerate() {
        let (piece, piece_owned) = match part {
            crate::ast::InterpPart::Lit(s) => {
                let e = emit_expr(
                    &Expr::dummy(ExprKind::StrLit(s.clone())),
                    ctx,
                    locals,
                    &format!("{prefix}_l{i}"),
                );
                code.push_str(&e.code);
                (e.value, e.owned)
            }
            crate::ast::InterpPart::Expr(e) => {
                let ee = emit_expr(e, ctx, locals, &format!("{prefix}_e{i}"));
                code.push_str(&ee.code);
                if ee.kind == Kind::Ptr {
                    (ee.value, ee.owned)
                } else {
                    let s =
                        stringify_scalar(&mut code, ee.kind, &ee.value, &format!("{prefix}_s{i}"));
                    (s, true)
                }
            }
        };
        acc = Some(match acc {
            None => (piece, piece_owned),
            Some((prev, prev_owned)) => {
                writeln!(
                    code,
                    "  %{prefix}_c{i} = call ptr @sz_string_concat(ptr {prev}, ptr {piece})"
                )
                .unwrap();
                if prev_owned {
                    writeln!(code, "  call void @sz_release(ptr {prev})").unwrap();
                }
                if piece_owned {
                    writeln!(code, "  call void @sz_release(ptr {piece})").unwrap();
                }
                (format!("%{prefix}_c{i}"), true)
            }
        });
    }
    let (val, owned) = acc.unwrap();
    if owned {
        owned_ptr(code, val)
    } else {
        val_emitted(code, val, Kind::Ptr)
    }
}

/// Emit a `_ => body` / `x => body` lambda literal as a closure value: a
/// 2-element `SzList` `cons(fn_ptr, cons(env_ptr, nil))`. `fn_ptr` matches the
/// C `SzViewTapFn` signature `void (*)(SzView *self, void *env)`. `env_ptr` is
/// the captured-locals list (same packing scheme as `flatMap` continuations).
/// Consumers (`View.button`, `View.iconButton`, `View.fab`, `View.outlinedButton`, `View.textButton`, `View.actionChip`, `View.inkWell`) unpack the pair.
fn emit_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_tap_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("t{id}"),
    );
    if let Some(p) = param {
        body_locals.insert(p.clone(), Local::borrow("%self", Kind::Ptr));
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("t{id}"));

    writeln!(
        ctx.conts,
        "define internal void @{fn_name}(ptr %self, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    if body_emitted.kind == Kind::Io {
        writeln!(
            ctx.conts,
            "  %t{id}_ur = call ptr @sz_io_unsafe_run_or_die(ptr {})",
            body_emitted.value
        )
        .unwrap();
    }
    writeln!(ctx.conts, "  ret void").unwrap();
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

/// `n => body` for `Signal.map`: `ptr (*)(i64, ptr)` returning a SzString.
fn emit_map_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_map_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("m{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%n", Kind::Int));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("m{id}"));

    writeln!(
        ctx.conts,
        "define internal ptr @{fn_name}(i64 %n, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    if body_emitted.kind == Kind::Io {
        writeln!(
            ctx.conts,
            "  %m{id}_ok = call ptr @sz_io_unsafe_run_or_die(ptr {})",
            body_emitted.value
        )
        .unwrap();
        writeln!(ctx.conts, "  ret ptr %m{id}_ok").unwrap();
    } else if body_emitted.kind == Kind::Int || body_emitted.kind == Kind::Float {
        let s = stringify_scalar(
            ctx.conts,
            body_emitted.kind,
            &body_emitted.value,
            &format!("m{id}_s"),
        );
        writeln!(ctx.conts, "  ret ptr {s}").unwrap();
    } else {
        writeln!(ctx.conts, "  ret ptr {}", body_emitted.value).unwrap();
    }
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

/// `s => view` for `View.each`: `ptr (*)(ptr item, ptr env)` returning a SzView.
fn emit_each_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_each_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("e{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%item", Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("e{id}"));

    writeln!(
        ctx.conts,
        "define internal ptr @{fn_name}(ptr %item, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    writeln!(ctx.conts, "  ret ptr {}", body_emitted.value).unwrap();
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

fn emit_view_each(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let src = emit_expr(&args[0], ctx, locals, &format!("{prefix}_src"));
    if args.len() == 1 {
        let mut code = src.code;
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_lang_view_each(ptr {})",
            src.value
        )
        .unwrap();
        return val_emitted(code, format!("%{prefix}_v"), Kind::Ptr);
    }
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("View.each mapper must be a lambda");
    };
    let mapper = emit_each_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = src.code;
    code.push_str(&mapper.code);
    unpack_closure(&mut code, &mapper.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_lang_view_each_map(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        src.value
    )
    .unwrap();
    drop_owned_ptr(&mut code, &mapper);
    val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

fn emit_signal_map(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "Signal.map expects 2 args");
    let src = emit_expr(&args[0], ctx, locals, &format!("{prefix}_src"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("Signal.map mapper must be a lambda");
    };
    let mapper = emit_map_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = src.code;
    code.push_str(&mapper.code);
    unpack_closure(&mut code, &mapper.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_lang_signal_map(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        src.value
    )
    .unwrap();
    drop_owned_ptr(&mut code, &mapper);
    val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

/// `t => IO[...]` continuation: `ptr (*)(ptr value, ptr env)` returning `SzIo*`.
fn emit_io_cont_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_rcont_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("r{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%value", Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("r{id}"));

    writeln!(
        ctx.conts,
        "define internal ptr @{fn_name}(ptr %value, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    let ret = ensure_io(
        ctx.conts,
        body_emitted.kind,
        &body_emitted.value,
        &format!("r{id}_wrap"),
        body_emitted.owned,
    );
    writeln!(ctx.conts, "  ret ptr {ret}").unwrap();
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

/// `t => Bool` predicate: `i64 (*)(ptr value, ptr env)`.
fn emit_pred_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_pred_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("p{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%value", Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("p{id}"));
    assert!(
        body_emitted.kind == Kind::Int,
        "predicate lambda must return Bool/Int"
    );

    writeln!(
        ctx.conts,
        "define internal i64 @{fn_name}(ptr %value, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    writeln!(ctx.conts, "  ret i64 {}", body_emitted.value).unwrap();
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

/// `t => ptr` mapper: `ptr (*)(ptr value, ptr env)`.
/// Stream.map stringifies Int bodies. List.map boxes Int bodies.
fn emit_smap_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
    box_int: bool,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_smap_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("sm{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%value", Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("sm{id}"));

    writeln!(
        ctx.conts,
        "define internal ptr @{fn_name}(ptr %value, ptr %env) {{"
    )
    .unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    if body_emitted.kind == Kind::Int || body_emitted.kind == Kind::Float {
        if box_int {
            let boxed = box_numeric(
                ctx.conts,
                body_emitted.kind,
                &body_emitted.value,
                &format!("sm{id}_box"),
            );
            writeln!(ctx.conts, "  ret ptr {boxed}").unwrap();
        } else {
            let s = stringify_scalar(
                ctx.conts,
                body_emitted.kind,
                &body_emitted.value,
                &format!("sm{id}_istr"),
            );
            writeln!(ctx.conts, "  ret ptr {s}").unwrap();
        }
    } else {
        assert!(
            body_emitted.kind == Kind::Ptr,
            "map mapper must be a pointer or numeric"
        );
        if !body_emitted.owned {
            writeln!(
                ctx.conts,
                "  call void @sz_retain(ptr {})",
                body_emitted.value
            )
            .unwrap();
        }
        writeln!(ctx.conts, "  ret ptr {}", body_emitted.value).unwrap();
    }
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

fn unpack_closure(code: &mut String, closure: &str, prefix: &str) {
    writeln!(
        code,
        "  %{prefix}_fnp = call ptr @sz_list_head(ptr {closure})"
    )
    .unwrap();
    writeln!(
        code,
        "  %{prefix}_fnt = call ptr @sz_list_tail(ptr {closure})"
    )
    .unwrap();
    writeln!(
        code,
        "  %{prefix}_envp = call ptr @sz_list_head(ptr %{prefix}_fnt)"
    )
    .unwrap();
}

fn emit_resource(
    callee: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let first = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} callback must be a lambda");
    };
    let lam = emit_io_cont_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = String::new();
    code.push_str(&first.code);
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    if callee == "Resource.make" {
        let acq = ensure_io(
            &mut code,
            first.kind,
            &first.value,
            &format!("{prefix}_acq"),
            first.owned,
        );
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_lang_resource_make(ptr {acq}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
        )
        .unwrap();
        writeln!(code, "  call void @sz_release(ptr {acq})").unwrap();
        drop_owned_ptr(&mut code, &lam);
        owned_ptr(code, format!("%{prefix}_v"))
    } else {
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_lang_resource_use(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
            first.value
        )
        .unwrap();
        drop_owned_ptr(&mut code, &lam);
        drop_owned_ptr(&mut code, &first);
        io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
    }
}

fn emit_stream_evalmap(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "Stream.evalMap expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("Stream.evalMap callback must be a lambda");
    };
    let lam = emit_io_cont_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = String::new();
    code.push_str(&inner.code);
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_stream_evalmap(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        inner.value
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    drop_owned_ptr(&mut code, &inner);
    owned_ptr(code, format!("%{prefix}_v"))
}

fn emit_list_pred_i64(
    callee: &str,
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} predicate must be a lambda");
    };
    let lam = emit_pred_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let inner_owned = inner.owned;
    let inner_kind = inner.kind;
    let inner_value = inner.value.clone();
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call i64 @{rt}(ptr {inner_value}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    if inner_owned && inner_kind == Kind::Ptr {
        writeln!(code, "  call void @sz_release(ptr {inner_value})").unwrap();
    }
    val_emitted(code, format!("%{prefix}_v"), Kind::Int)
}

fn emit_stream_pred(
    callee: &str,
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
    as_io: bool,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} predicate must be a lambda");
    };
    let lam = emit_pred_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let inner_owned = inner.owned;
    let inner_kind = inner.kind;
    let inner_value = inner.value.clone();
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @{rt}(ptr {inner_value}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    if inner_owned && inner_kind == Kind::Ptr {
        writeln!(code, "  call void @sz_release(ptr {inner_value})").unwrap();
    }
    if as_io {
        io_emitted(code, format!("%{prefix}_v"), Kind::Int)
    } else {
        owned_ptr(code, format!("%{prefix}_v"))
    }
}

fn emit_stream_filter(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    emit_stream_pred(
        "Stream.filter",
        "sz_stream_filter",
        args,
        ctx,
        locals,
        prefix,
        false,
    )
}

fn emit_stream_map(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    emit_ptr_map(
        "Stream.map",
        "sz_stream_map",
        args,
        ctx,
        locals,
        prefix,
        false,
    )
}

fn emit_ptr_map(
    callee: &str,
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
    box_int: bool,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} mapper must be a lambda");
    };
    let lam = emit_smap_lambda(param, body, ctx, locals, &format!("{prefix}_fn"), box_int);
    let inner_owned = inner.owned;
    let inner_kind = inner.kind;
    let inner_value = inner.value.clone();
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @{rt}(ptr {inner_value}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    if inner_owned && inner_kind == Kind::Ptr {
        writeln!(code, "  call void @sz_release(ptr {inner_value})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_v"))
}

fn emit_list_tabulate(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "List.tabulate expects 2 args");
    let n = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("List.tabulate mapper must be a lambda");
    };
    let lam = emit_smap_lambda(param, body, ctx, locals, &format!("{prefix}_fn"), true);
    let mut code = n.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    let n_i = as_i64(&mut code, n.kind, &n.value, &format!("{prefix}_n"));
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_list_tabulate(i64 {n_i}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    owned_ptr(code, format!("%{prefix}_v"))
}

fn emit_net_serve(
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "{rt} expects 2 args");
    let port = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{rt} callback must be a lambda");
    };
    let lam = emit_io_cont_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = port.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @{rt}(i64 {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        port.value
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

/// `_ => View…` factory for `Ui.run`: `ptr (*)(ptr env)` returning `SzView*`.
fn emit_rebuild_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_uibuild_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, Local> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("u{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), Local::borrow("%env", Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("u{id}"));

    writeln!(ctx.conts, "define internal ptr @{fn_name}(ptr %env) {{").unwrap();
    writeln!(ctx.conts, "entry:").unwrap();
    ctx.conts.push_str(&pre);
    ctx.conts.push_str(&body_emitted.code);
    writeln!(ctx.conts, "  ret ptr {}", body_emitted.value).unwrap();
    writeln!(ctx.conts, "}}").unwrap();
    writeln!(ctx.conts).unwrap();
    *ctx.reload_fn = Some(fn_name.clone());

    let mut code = String::new();
    let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
    writeln!(code, "  %{prefix}_cl0 = call ptr @sz_list_nil()").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl1 = call ptr @sz_list_cons(ptr {env_ptr}, ptr %{prefix}_cl0)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl0)").unwrap();
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    writeln!(code, "  call void @sz_release(ptr %{prefix}_cl1)").unwrap();
    if env_ptr != "null" {
        writeln!(code, "  call void @sz_release(ptr {env_ptr})").unwrap();
    }
    owned_ptr(code, format!("%{prefix}_cl2"))
}

fn emit_ui_run(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 1, "Ui.run expects 1 arg");
    let ExprKind::Lambda { param, body } = &args[0].kind else {
        panic!("Ui.run expects _ => View");
    };
    let lam = emit_rebuild_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = String::new();
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_ui_run_rebuild(ptr %{prefix}_fnp, ptr %{prefix}_envp)"
    )
    .unwrap();
    drop_owned_ptr(&mut code, &lam);
    io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

fn as_i64(code: &mut String, kind: Kind, value: &str, tmp: &str) -> String {
    if kind == Kind::Int {
        return value.to_string();
    }
    writeln!(code, "  %{tmp} = call i64 @sz_unbox_i64(ptr {value})").unwrap();
    format!("%{tmp}")
}

fn emit_binary(
    op: &BinOp,
    left: &Expr,
    right: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    if matches!(op, BinOp::And | BinOp::Or) {
        return emit_short_circuit(*op == BinOp::And, left, right, ctx, locals, prefix);
    }
    let le = emit_expr(left, ctx, locals, &format!("{prefix}_l"));
    let re = emit_expr(right, ctx, locals, &format!("{prefix}_r"));
    let mut code = le.code;
    code.push_str(&re.code);

    if *op == BinOp::Add && le.kind == Kind::Ptr && re.kind == Kind::Ptr {
        writeln!(
            code,
            "  %{prefix}_add = call ptr @sz_string_concat(ptr {}, ptr {})",
            le.value, re.value
        )
        .unwrap();
        if le.owned {
            writeln!(code, "  call void @sz_release(ptr {})", le.value).unwrap();
        }
        drop_owned_ptr(&mut code, &re);
        return owned_ptr(code, format!("%{prefix}_add"));
    }

    if matches!(op, BinOp::Eq | BinOp::Ne) && le.kind == Kind::Ptr && re.kind == Kind::Ptr {
        writeln!(
            code,
            "  %{prefix}_eqi = call i32 @sz_string_eq(ptr {}, ptr {})",
            le.value, re.value
        )
        .unwrap();
        writeln!(code, "  %{prefix}_eq = zext i32 %{prefix}_eqi to i64").unwrap();
        if le.owned {
            writeln!(code, "  call void @sz_release(ptr {})", le.value).unwrap();
        }
        drop_owned_ptr(&mut code, &re);
        if *op == BinOp::Eq {
            return val_emitted(code, format!("%{prefix}_eq"), Kind::Int);
        }
        writeln!(code, "  %{prefix}_ne = icmp eq i64 %{prefix}_eq, 0").unwrap();
        writeln!(code, "  %{prefix}_nev = zext i1 %{prefix}_ne to i64").unwrap();
        return val_emitted(code, format!("%{prefix}_nev"), Kind::Int);
    }

    if le.kind == Kind::Float || re.kind == Kind::Float {
        let lv = as_f64(&mut code, le.kind, &le.value, &format!("{prefix}_l0"));
        let rv = as_f64(&mut code, re.kind, &re.value, &format!("{prefix}_r0"));
        match op {
            BinOp::Add => {
                writeln!(code, "  %{prefix}_v = fadd double {lv}, {rv}").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Float);
            }
            BinOp::Sub => {
                writeln!(code, "  %{prefix}_v = fsub double {lv}, {rv}").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Float);
            }
            BinOp::Mul => {
                writeln!(code, "  %{prefix}_v = fmul double {lv}, {rv}").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Float);
            }
            BinOp::Div => {
                writeln!(code, "  %{prefix}_v = fdiv double {lv}, {rv}").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Float);
            }
            BinOp::Mod => {
                writeln!(code, "  %{prefix}_v = frem double {lv}, {rv}").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Float);
            }
            BinOp::And | BinOp::Or => unreachable!("short-circuit ops emit separately"),
            BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
                let pred = match op {
                    BinOp::Eq => "oeq",
                    BinOp::Ne => "one",
                    BinOp::Lt => "olt",
                    BinOp::Le => "ole",
                    BinOp::Gt => "ogt",
                    BinOp::Ge => "oge",
                    _ => unreachable!(),
                };
                writeln!(code, "  %{prefix}_cmp = fcmp {pred} double {lv}, {rv}").unwrap();
                writeln!(code, "  %{prefix}_v = zext i1 %{prefix}_cmp to i64").unwrap();
                return val_emitted(code, format!("%{prefix}_v"), Kind::Int);
            }
        }
    }

    let lv = as_i64(&mut code, le.kind, &le.value, &format!("{prefix}_l0"));
    let rv = as_i64(&mut code, re.kind, &re.value, &format!("{prefix}_r0"));

    match op {
        BinOp::Add => {
            writeln!(code, "  %{prefix}_v = add i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::Sub => {
            writeln!(code, "  %{prefix}_v = sub i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::Mul => {
            writeln!(code, "  %{prefix}_v = mul i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::Div | BinOp::Mod => {
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let zero_l = format!("{prefix}_d0_{id}");
            let ok_l = format!("{prefix}_dok_{id}");
            writeln!(code, "  %{prefix}_z{id} = icmp eq i64 {rv}, 0").unwrap();
            writeln!(
                code,
                "  br i1 %{prefix}_z{id}, label %{zero_l}, label %{ok_l}"
            )
            .unwrap();
            writeln!(code, "{zero_l}:").unwrap();
            writeln!(
                code,
                "  call void @sz_panic(ptr getelementptr inbounds ([17 x i8], ptr @.div0, i64 0, i64 0))"
            )
            .unwrap();
            writeln!(code, "  unreachable").unwrap();
            writeln!(code, "{ok_l}:").unwrap();
            if *op == BinOp::Div {
                writeln!(code, "  %{prefix}_v = sdiv i64 {lv}, {rv}").unwrap();
            } else {
                writeln!(code, "  %{prefix}_v = srem i64 {lv}, {rv}").unwrap();
            }
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::And | BinOp::Or => unreachable!("short-circuit ops emit separately"),
        BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Le | BinOp::Gt | BinOp::Ge => {
            let pred = match op {
                BinOp::Eq => "eq",
                BinOp::Ne => "ne",
                BinOp::Lt => "slt",
                BinOp::Le => "sle",
                BinOp::Gt => "sgt",
                BinOp::Ge => "sge",
                _ => unreachable!(),
            };
            writeln!(code, "  %{prefix}_cmp = icmp {pred} i64 {lv}, {rv}").unwrap();
            writeln!(code, "  %{prefix}_v = zext i1 %{prefix}_cmp to i64").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
    }
}

fn emit_short_circuit(
    is_and: bool,
    left: &Expr,
    right: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let after_l = format!("{prefix}_al_{id}");
    let rhs_l = format!("{prefix}_rhs_{id}");
    let after_r = format!("{prefix}_ar_{id}");
    let done_l = format!("{prefix}_sc_{id}");
    let le = emit_expr(left, ctx, locals, &format!("{prefix}_l"));
    let mut code = le.code;
    let lv = as_i64(&mut code, le.kind, &le.value, &format!("{prefix}_l0"));
    writeln!(code, "  br label %{after_l}").unwrap();
    writeln!(code, "{after_l}:").unwrap();
    writeln!(code, "  %{prefix}_nz{id} = icmp ne i64 {lv}, 0").unwrap();
    if is_and {
        writeln!(
            code,
            "  br i1 %{prefix}_nz{id}, label %{rhs_l}, label %{done_l}"
        )
        .unwrap();
    } else {
        writeln!(
            code,
            "  br i1 %{prefix}_nz{id}, label %{done_l}, label %{rhs_l}"
        )
        .unwrap();
    }
    writeln!(code, "{rhs_l}:").unwrap();
    let re = emit_expr(right, ctx, locals, &format!("{prefix}_r"));
    code.push_str(&re.code);
    let rv = as_i64(&mut code, re.kind, &re.value, &format!("{prefix}_r0"));
    writeln!(code, "  br label %{after_r}").unwrap();
    writeln!(code, "{after_r}:").unwrap();
    writeln!(code, "  %{prefix}_rnz{id} = icmp ne i64 {rv}, 0").unwrap();
    writeln!(
        code,
        "  %{prefix}_rb{id} = zext i1 %{prefix}_rnz{id} to i64"
    )
    .unwrap();
    writeln!(code, "  br label %{done_l}").unwrap();
    writeln!(code, "{done_l}:").unwrap();
    let early = if is_and { "0" } else { "1" };
    writeln!(
        code,
        "  %{prefix}_v = phi i64 [ {early}, %{after_l} ], [ %{prefix}_rb{id}, %{after_r} ]"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_v"), Kind::Int)
}

fn emit_view_box(
    create_fn: &str,
    code: &mut String,
    emitted_args: &[Emitted],
    prefix: &str,
) -> Emitted {
    writeln!(code, "  %{prefix}_v = call ptr @{create_fn}()").unwrap();
    for (i, child) in emitted_args.iter().enumerate() {
        writeln!(
            code,
            "  %{prefix}_ac{i} = call ptr @sz_lang_view_add_child(ptr %{prefix}_v, ptr {})",
            child.value
        )
        .unwrap();
    }
    val_emitted(std::mem::take(code), format!("%{prefix}_v"), Kind::Ptr)
}

fn emit_view_grid(code: &mut String, emitted_args: &[Emitted], prefix: &str) -> Emitted {
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_lang_view_grid(i64 {})",
        emitted_args[0].value
    )
    .unwrap();
    for (i, child) in emitted_args.iter().skip(1).enumerate() {
        writeln!(
            code,
            "  %{prefix}_ac{i} = call ptr @sz_lang_view_add_child(ptr %{prefix}_v, ptr {})",
            child.value
        )
        .unwrap();
    }
    val_emitted(std::mem::take(code), format!("%{prefix}_v"), Kind::Ptr)
}

#[inline(never)]
fn emit_call(
    callee: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, Local>,
    prefix: &str,
) -> Emitted {
    if callee == "Signal.map" {
        return emit_signal_map(args, ctx, locals, prefix);
    }
    if callee == "View.each" {
        return emit_view_each(args, ctx, locals, prefix);
    }
    if callee == "List.filter" {
        return emit_stream_pred(
            "List.filter",
            "sz_list_filter",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.filterNot" {
        return emit_stream_pred(
            "List.filterNot",
            "sz_list_filter_not",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.find" {
        return emit_stream_pred(
            "List.find",
            "sz_list_find",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.exists" {
        return emit_list_pred_i64("List.exists", "sz_list_exists", args, ctx, locals, prefix);
    }
    if callee == "List.indexWhere" {
        return emit_list_pred_i64(
            "List.indexWhere",
            "sz_list_index_where",
            args,
            ctx,
            locals,
            prefix,
        );
    }
    if callee == "List.lastIndexWhere" {
        return emit_list_pred_i64(
            "List.lastIndexWhere",
            "sz_list_last_index_where",
            args,
            ctx,
            locals,
            prefix,
        );
    }
    if callee == "List.count" {
        return emit_list_pred_i64("List.count", "sz_list_count", args, ctx, locals, prefix);
    }
    if callee == "List.takeWhile" {
        return emit_stream_pred(
            "List.takeWhile",
            "sz_list_takewhile",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.span" {
        return emit_stream_pred(
            "List.span",
            "sz_list_span",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.partition" {
        return emit_stream_pred(
            "List.partition",
            "sz_list_partition",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.dropWhile" {
        return emit_stream_pred(
            "List.dropWhile",
            "sz_list_dropwhile",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "List.forall" {
        return emit_list_pred_i64("List.forall", "sz_list_forall", args, ctx, locals, prefix);
    }
    if callee == "List.map" {
        return emit_ptr_map("List.map", "sz_list_map", args, ctx, locals, prefix, true);
    }
    if callee == "List.flatMap" {
        return emit_ptr_map(
            "List.flatMap",
            "sz_list_flat_map",
            args,
            ctx,
            locals,
            prefix,
            true,
        );
    }
    if callee == "List.tabulate" {
        return emit_list_tabulate(args, ctx, locals, prefix);
    }
    if callee == "Resource.make" || callee == "Resource.use" {
        return emit_resource(callee, args, ctx, locals, prefix);
    }
    if callee == "Stream.evalMap" {
        return emit_stream_evalmap(args, ctx, locals, prefix);
    }
    if callee == "Stream.filter" {
        return emit_stream_filter(args, ctx, locals, prefix);
    }
    if callee == "Stream.takeWhile" {
        return emit_stream_pred(
            "Stream.takeWhile",
            "sz_stream_takewhile",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "Stream.dropWhile" {
        return emit_stream_pred(
            "Stream.dropWhile",
            "sz_stream_dropwhile",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "Stream.find" {
        return emit_stream_pred(
            "Stream.find",
            "sz_stream_find",
            args,
            ctx,
            locals,
            prefix,
            false,
        );
    }
    if callee == "Stream.exists" {
        return emit_stream_pred(
            "Stream.exists",
            "sz_stream_exists",
            args,
            ctx,
            locals,
            prefix,
            true,
        );
    }
    if callee == "Stream.map" {
        return emit_stream_map(args, ctx, locals, prefix);
    }
    if callee == "Net.serveOnce" {
        return emit_net_serve("sz_net_serve_once", args, ctx, locals, prefix);
    }
    if callee == "Net.serve" {
        return emit_net_serve("sz_net_serve", args, ctx, locals, prefix);
    }
    if callee == "Ui.run" {
        return emit_ui_run(args, ctx, locals, prefix);
    }
    let mut emitted_args = Vec::new();
    for (i, a) in args.iter().enumerate() {
        emitted_args.push(emit_expr(a, ctx, locals, &format!("{prefix}_arg{i}")));
    }
    let mut code = String::new();
    for a in &emitted_args {
        code.push_str(&a.code);
    }

    match callee {
        "Str.concat" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_concat(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.len" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_len(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.slice" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_slice(ptr {}, i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.eq" => {
            writeln!(
                code,
                "  %{prefix}_eqi = call i32 @sz_string_eq(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_v = zext i32 %{prefix}_eqi to i64").unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.charAt" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_char_at(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.fromInt" => {
            let n = as_i64(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_n"),
            );
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_from_int(i64 {n})"
            )
            .unwrap();
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Float.fromInt" => {
            let n = as_i64(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_n"),
            );
            writeln!(code, "  %{prefix}_v = sitofp i64 {n} to double").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Float)
        }
        "Float.toInt" => {
            let x = as_f64(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_x"),
            );
            writeln!(code, "  %{prefix}_v = fptosi double {x} to i64").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.indexOf" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_index_of(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.lastIndexOf" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_last_index_of(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_take(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.drop" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_drop(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.takeRight" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_take_right(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.dropRight" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_drop_right(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.reverse" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_reverse(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.startsWith" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_starts_with(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.contains" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_contains(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.endsWith" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_ends_with(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.toInt" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_to_int(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.replace" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_replace(ptr {}, ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.split" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_split(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.lines" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_lines(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.trim" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_trim(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.isEmpty" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_is_empty(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.nonEmpty" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_non_empty(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.toLower" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_to_lower(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.toUpper" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_to_upper(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.capitalize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_capitalize(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.repeat" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_repeat(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.stripPrefix" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_strip_prefix(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.stripSuffix" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_strip_suffix(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.padLeft" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_pad_left(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[2]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.padRight" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_pad_right(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[2]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Str.isBlank" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_is_blank(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.empty" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_list_nil()").unwrap();
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.cons" => {
            let head = if emitted_args[0].kind == Kind::Int || emitted_args[0].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[0].kind,
                    &emitted_args[0].value,
                    &format!("{prefix}_hd"),
                )
            } else {
                emitted_args[0].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_cons(ptr {head}, ptr {})",
                emitted_args[1].value
            )
            .unwrap();
            if emitted_args[0].kind == Kind::Int || emitted_args[0].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {head})").unwrap();
            }
            drop_owned_ptrs(&mut code, &emitted_args);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.isEmpty" => {
            writeln!(
                code,
                "  %{prefix}_i = call i32 @sz_list_is_empty(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_v = zext i32 %{prefix}_i to i64").unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.nonEmpty" => {
            writeln!(
                code,
                "  %{prefix}_i = call i32 @sz_list_non_empty(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_v = zext i32 %{prefix}_i to i64").unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.head" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_head(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            take_owned_ptr(&mut code, &emitted_args[0], &format!("%{prefix}_v"));
            ptr_owned_if(code, format!("%{prefix}_v"), emitted_args[0].owned)
        }
        "List.tail" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_tail(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            take_owned_ptr(&mut code, &emitted_args[0], &format!("%{prefix}_v"));
            ptr_owned_if(code, format!("%{prefix}_v"), emitted_args[0].owned)
        }
        "List.len" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_list_len(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.at" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_at(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            take_owned_ptr(&mut code, &emitted_args[0], &format!("%{prefix}_v"));
            ptr_owned_if(code, format!("%{prefix}_v"), emitted_args[0].owned)
        }
        "List.reverse" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_reverse(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.join" => {
            writeln!(
                code,
                "  %{prefix}_sep = call ptr @sz_string_cstr(ptr {})",
                emitted_args[1].value
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_join(ptr {}, ptr %{prefix}_sep)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_take(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.splitAt" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_split_at(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.drop" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_drop(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.takeRight" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_take_right(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.dropRight" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_drop_right(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.init" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_init(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.last" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_last(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.getOrElse" => {
            let dflt = if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &emitted_args[2].value,
                    &format!("{prefix}_d"),
                )
            } else {
                emitted_args[2].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_p = call ptr @sz_list_get_or(ptr {}, i64 {}, ptr {dflt})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            take_owned_ptr(&mut code, &emitted_args[0], &format!("%{prefix}_p"));
            let dflt_is_num =
                emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float;
            let dflt_owned = dflt_is_num || emitted_args[2].owned;
            if emitted_args[0].owned {
                if dflt_is_num {
                    writeln!(code, "  call void @sz_release(ptr {dflt})").unwrap();
                } else {
                    drop_owned_ptr(&mut code, &emitted_args[2]);
                }
            } else if dflt_owned {
                writeln!(code, "  call void @sz_retain(ptr %{prefix}_p)").unwrap();
                if dflt_is_num {
                    writeln!(code, "  call void @sz_release(ptr {dflt})").unwrap();
                } else {
                    drop_owned_ptr(&mut code, &emitted_args[2]);
                }
            }
            let result_owned = emitted_args[0].owned || dflt_owned;
            if dflt_is_num {
                let v = unbox_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &format!("%{prefix}_p"),
                    &format!("{prefix}_u"),
                );
                if result_owned {
                    writeln!(code, "  call void @sz_release(ptr %{prefix}_p)").unwrap();
                }
                val_emitted(code, v, emitted_args[2].kind)
            } else {
                ptr_owned_if(code, format!("%{prefix}_p"), result_owned)
            }
        }
        "List.fill" => {
            let elem = if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[1].kind,
                    &emitted_args[1].value,
                    &format!("{prefix}_x"),
                )
            } else {
                emitted_args[1].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_fill(i64 {}, ptr {elem})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {elem})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.padTo" => {
            let elem = if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &emitted_args[2].value,
                    &format!("{prefix}_x"),
                )
            } else {
                emitted_args[2].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_pad_to(ptr {}, i64 {}, ptr {elem})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[2]);
            if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {elem})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.range" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_range(i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.intersperse" => {
            let elem = if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[1].kind,
                    &emitted_args[1].value,
                    &format!("{prefix}_x"),
                )
            } else {
                emitted_args[1].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_intersperse(ptr {}, ptr {elem})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {elem})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.grouped" | "List.sliding" => {
            let rt = if callee == "List.grouped" {
                "sz_list_grouped"
            } else {
                "sz_list_sliding"
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @{rt}(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.slice" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_slice(ptr {}, i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.indices" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_indices(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.concat" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_concat(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.flatten" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_flatten(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.append" => {
            let elem = if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[1].kind,
                    &emitted_args[1].value,
                    &format!("{prefix}_el"),
                )
            } else {
                emitted_args[1].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_append(ptr {}, ptr {elem})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            if emitted_args[1].kind == Kind::Int || emitted_args[1].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {elem})").unwrap();
            } else {
                drop_owned_ptr(&mut code, &emitted_args[1]);
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "List.setAt" => {
            let elem = if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &emitted_args[2].value,
                    &format!("{prefix}_el"),
                )
            } else {
                emitted_args[2].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_set_at(ptr {}, i64 {}, ptr {elem})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {elem})").unwrap();
            } else {
                drop_owned_ptr(&mut code, &emitted_args[2]);
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.empty" | "Set.empty" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_map_empty()").unwrap();
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.set" | "Set.add" => {
            let key_src = &emitted_args[1];
            let key = if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                box_numeric(
                    &mut code,
                    key_src.kind,
                    &key_src.value,
                    &format!("{prefix}_k"),
                )
            } else {
                key_src.value.clone()
            };
            let (val, val_owned_box) = if callee == "Set.add" {
                ("null".to_string(), false)
            } else if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                (
                    box_numeric(
                        &mut code,
                        emitted_args[2].kind,
                        &emitted_args[2].value,
                        &format!("{prefix}_v0"),
                    ),
                    true,
                )
            } else {
                (emitted_args[2].value.clone(), false)
            };
            let kind = if key_src.kind == Kind::Int { 0 } else { 1 };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_map_set(ptr {}, ptr {key}, ptr {val}, i32 {kind})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, key_src);
            if callee == "Map.set" {
                drop_owned_ptr(&mut code, &emitted_args[2]);
            }
            if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {key})").unwrap();
            }
            if val_owned_box {
                writeln!(code, "  call void @sz_release(ptr {val})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.get" => {
            let key_src = &emitted_args[1];
            let key = if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                box_numeric(
                    &mut code,
                    key_src.kind,
                    &key_src.value,
                    &format!("{prefix}_k"),
                )
            } else {
                key_src.value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_map_get(ptr {}, ptr {key})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, key_src);
            if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {key})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.getOrElse" => {
            let key_src = &emitted_args[1];
            let key = if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                box_numeric(
                    &mut code,
                    key_src.kind,
                    &key_src.value,
                    &format!("{prefix}_k"),
                )
            } else {
                key_src.value.clone()
            };
            let dflt = if emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float {
                box_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &emitted_args[2].value,
                    &format!("{prefix}_d"),
                )
            } else {
                emitted_args[2].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_p = call ptr @sz_map_get_or(ptr {}, ptr {key}, ptr {dflt})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, key_src);
            if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {key})").unwrap();
            }
            take_owned_ptr(&mut code, &emitted_args[0], &format!("%{prefix}_p"));
            let dflt_is_num =
                emitted_args[2].kind == Kind::Int || emitted_args[2].kind == Kind::Float;
            let dflt_owned = dflt_is_num || emitted_args[2].owned;
            if emitted_args[0].owned {
                if dflt_is_num {
                    writeln!(code, "  call void @sz_release(ptr {dflt})").unwrap();
                } else {
                    drop_owned_ptr(&mut code, &emitted_args[2]);
                }
            } else if dflt_owned {
                writeln!(code, "  call void @sz_retain(ptr %{prefix}_p)").unwrap();
                if dflt_is_num {
                    writeln!(code, "  call void @sz_release(ptr {dflt})").unwrap();
                } else {
                    drop_owned_ptr(&mut code, &emitted_args[2]);
                }
            }
            let result_owned = emitted_args[0].owned || dflt_owned;
            if dflt_is_num {
                let v = unbox_numeric(
                    &mut code,
                    emitted_args[2].kind,
                    &format!("%{prefix}_p"),
                    &format!("{prefix}_u"),
                );
                if result_owned {
                    writeln!(code, "  call void @sz_release(ptr %{prefix}_p)").unwrap();
                }
                val_emitted(code, v, emitted_args[2].kind)
            } else {
                ptr_owned_if(code, format!("%{prefix}_p"), result_owned)
            }
        }
        "Map.contains" | "Set.contains" => {
            let key_src = &emitted_args[1];
            let key = if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                box_numeric(
                    &mut code,
                    key_src.kind,
                    &key_src.value,
                    &format!("{prefix}_k"),
                )
            } else {
                key_src.value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_map_contains(ptr {}, ptr {key})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, key_src);
            if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {key})").unwrap();
            }
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Map.remove" | "Set.remove" => {
            let key_src = &emitted_args[1];
            let key = if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                box_numeric(
                    &mut code,
                    key_src.kind,
                    &key_src.value,
                    &format!("{prefix}_k"),
                )
            } else {
                key_src.value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_map_remove(ptr {}, ptr {key})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, key_src);
            if key_src.kind == Kind::Int || key_src.kind == Kind::Float {
                writeln!(code, "  call void @sz_release(ptr {key})").unwrap();
            }
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Set.union" | "Set.intersect" | "Set.diff" => {
            let rt = match callee {
                "Set.union" => "sz_set_union",
                "Set.intersect" => "sz_set_intersect",
                _ => "sz_set_diff",
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @{rt}(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.keys" | "Set.toList" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_map_keys(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.values" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_map_values(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Map.size" | "Set.size" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_map_size(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Map.isEmpty" | "Set.isEmpty" => {
            writeln!(
                code,
                "  %{prefix}_n = call i64 @sz_map_size(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_b = icmp eq i64 %{prefix}_n, 0").unwrap();
            writeln!(code, "  %{prefix}_v = zext i1 %{prefix}_b to i64").unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Map.nonEmpty" | "Set.nonEmpty" => {
            writeln!(
                code,
                "  %{prefix}_n = call i64 @sz_map_size(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_b = icmp ne i64 %{prefix}_n, 0").unwrap();
            writeln!(code, "  %{prefix}_v = zext i1 %{prefix}_b to i64").unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Fs.read" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_read(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.write" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_write(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.list" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.mkdirs" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_mkdirs(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.canonicalize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_canonicalize(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.args" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_sys_args()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.readLine" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_sys_read_line()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.read" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_read(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.write" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_write(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.exec" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_exec(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Sys.spawn" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_spawn(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Sys.alive" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_alive(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Sys.kill" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_kill(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.getenv" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_getenv(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Clock.realTime" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_clock_real_time()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Clock.monotonic" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_clock_monotonic()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Random.nextInt" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_random_next_int(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Net.httpGet" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_net_http_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Impurity.runKit" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_impurity_run_kit()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Ref.of" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ref_of(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Ref.get" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ref_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Ref.set" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ref_set(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Queue.unbounded" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_queue_unbounded()").unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Queue.offer" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_queue_offer(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Queue.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_queue_take(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Deferred.empty" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_deferred_empty()").unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Deferred.complete" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_deferred_complete(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Deferred.get" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_deferred_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Fiber.fork" => {
            let iv = ensure_io(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_fio"),
                emitted_args[0].owned,
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_fiber_fork(ptr {iv})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            io_emitted(code, format!("%{prefix}_v"), emitted_args[0].payload)
        }
        "Fiber.join" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fiber_join(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted_payload(code, format!("%{prefix}_v"), Kind::Ptr, true)
        }
        "Fiber.interrupt" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fiber_interrupt(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "IO.forever" => {
            let iv = ensure_io(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_fio"),
                emitted_args[0].owned,
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_io_forever(ptr {iv})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "IO.repeatN" => {
            let n = if emitted_args[0].kind == Kind::Int {
                emitted_args[0].value.clone()
            } else {
                writeln!(code, "  %{prefix}_n0 = add i64 0, 0").unwrap();
                format!("%{prefix}_n0")
            };
            let iv = ensure_io(
                &mut code,
                emitted_args[1].kind,
                &emitted_args[1].value,
                &format!("{prefix}_rio"),
                emitted_args[1].owned,
            );
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_io_repeat_n(i64 {n}, ptr {iv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_v"),
                emitted_args[1].payload,
                emitted_args[1].payload_owned,
            )
        }
        "IO.retryN" => {
            let n = if emitted_args[0].kind == Kind::Int {
                emitted_args[0].value.clone()
            } else {
                writeln!(code, "  %{prefix}_n0 = add i64 0, 0").unwrap();
                format!("%{prefix}_n0")
            };
            let iv = ensure_io(
                &mut code,
                emitted_args[1].kind,
                &emitted_args[1].value,
                &format!("{prefix}_tio"),
                emitted_args[1].owned,
            );
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_io_retry_n(i64 {n}, ptr {iv})"
            )
            .unwrap();
            writeln!(code, "  call void @sz_release(ptr {iv})").unwrap();
            io_emitted_payload(
                code,
                format!("%{prefix}_v"),
                emitted_args[1].payload,
                emitted_args[1].payload_owned,
            )
        }
        "Stream.emit" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_emit(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.emits" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_emits(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.eval" => {
            let io = ensure_io(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_eval"),
                emitted_args[0].owned,
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_stream_eval(ptr {io})").unwrap();
            writeln!(code, "  call void @sz_release(ptr {io})").unwrap();
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.concat" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_concat(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_take(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.drop" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_drop(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            owned_ptr(code, format!("%{prefix}_v"))
        }
        "Stream.compileToList" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_compile_to_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.drain" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_drain(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.int" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_int(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.get" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_lang_signal_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Signal.set" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_set(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.str" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_str(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.getStr" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_str_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.setStr" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_str_set(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.list" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.getList" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_list_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.setList" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_list_set(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Law.signalInt" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_law_signal_int(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Law.signalStr" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_law_signal_str(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Law.signalListLen" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_law_signal_list_len(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Law.signalListAt" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_law_signal_list_at(i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Law.a11yHas" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_law_a11y_has(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Law.assert" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_law_assert(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Law.check" => {
            writeln!(
                code,
                "  call void @sz_law_check(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, emitted_args[2].value.clone(), emitted_args[2].kind)
        }
        "Law.force" => {
            // IO[Bool/Int] → Int with unsafe_run + unbox (verify residual only).
            writeln!(
                code,
                "  %{prefix}_fr = call ptr @sz_io_unsafe_run_or_die(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_unbox_i64(ptr %{prefix}_fr)"
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Law.sometimes" => {
            writeln!(
                code,
                "  call void @sz_law_sometimes(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, "null".into(), Kind::Ptr)
        }
        "View.text" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_text(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.bindText" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_bind_text(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.button" => {
            // args[1] is a closure value: cons(fn_ptr, cons(env_ptr, nil)).
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_button(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.iconButton" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_icon_button(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.fab" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_fab(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.outlinedButton" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_outlined_button(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.textButton" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_text_button(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.actionChip" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_action_chip(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.checkbox" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_checkbox(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.radio" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_radio(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[2]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.slider" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_slider(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.progress" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_progress(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.circularProgress" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_circular_progress(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.avatar" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_avatar(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.switch" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_switch(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.chip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_chip(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.filterChip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_filter_chip(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.inputChip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_input_chip(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.choiceChip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_choice_chip(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[2]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.listTile" => {
            if emitted_args.len() == 1 {
                writeln!(
                    code,
                    "  %{prefix}_v = call ptr @sz_lang_view_list_tile(ptr {}, ptr null)",
                    emitted_args[0].value
                )
                .unwrap();
            } else {
                writeln!(
                    code,
                    "  %{prefix}_v = call ptr @sz_lang_view_list_tile(ptr {}, ptr {})",
                    emitted_args[0].value, emitted_args[1].value
                )
                .unwrap();
            }
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.checkboxListTile" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_checkbox_list_tile(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.switchListTile" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_switch_list_tile(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.radioListTile" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_radio_list_tile(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[2]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.segmented" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_segmented(ptr {}, ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            drop_owned_ptr(&mut code, &emitted_args[2]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.badge" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_badge(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.card" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_card(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.tooltip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_tooltip(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.placeholder" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_placeholder(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.semantics" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_semantics(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.mergeSemantics" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_merge_semantics(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.inkWell" => {
            unpack_closure(&mut code, &emitted_args[1].value, prefix);
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_ink_well(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp, ptr {})",
                emitted_args[0].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[0]);
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.visibility" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_visibility(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.offstage" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_offstage(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.unconstrainedBox" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_unconstrained_box(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.divider" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_lang_view_divider()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.verticalDivider" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_vertical_divider()"
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.expansionTile" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_expansion_tile(ptr {}, ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Theme.accent" => {
            writeln!(code, "  %{prefix}_v = call i64 @sz_theme_accent()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Theme.primary" => {
            writeln!(code, "  %{prefix}_v = call i64 @sz_theme_primary()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Theme.muted" => {
            writeln!(code, "  %{prefix}_v = call i64 @sz_theme_muted()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Theme.foreground" => {
            writeln!(code, "  %{prefix}_v = call i64 @sz_theme_foreground()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Color.rgb" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_color_rgb(i64 {}, i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Color.rgba" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_color_rgba(i64 {}, i64 {}, i64 {}, i64 {})",
                emitted_args[0].value,
                emitted_args[1].value,
                emitted_args[2].value,
                emitted_args[3].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "View.column" => emit_view_box("sz_lang_view_column", &mut code, &emitted_args, prefix),
        "View.row" => emit_view_box("sz_lang_view_row", &mut code, &emitted_args, prefix),
        "View.wrap" => emit_view_box("sz_lang_view_wrap", &mut code, &emitted_args, prefix),
        "View.grid" => emit_view_grid(&mut code, &emitted_args, prefix),
        "View.stack" => emit_view_box("sz_lang_view_stack", &mut code, &emitted_args, prefix),
        "View.scroll" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_scroll(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.scrollH" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_scroll_h(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.expanded" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_expanded(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.stretch" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_stretch(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.center" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_center(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.align" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_align(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.positioned" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_positioned(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.padding" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_padding(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.sized" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_sized(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.minSize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_min_size(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.maxSize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_max_size(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.clip" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_clip(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.opacity" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_opacity(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.maxLines" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_max_lines(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.ellipsis" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_ellipsis(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.textColor" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_text_color(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.gap" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_gap(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.fontSize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_font_size(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.border" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_border(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.radius" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_radius(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.ignorePointer" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_ignore_pointer(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.absorbPointer" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_absorb_pointer(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.excludeSemantics" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_exclude_semantics(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.background" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_background(i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.aspectRatio" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_aspect_ratio(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.fraction" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_fraction(i64 {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.textField" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_text_field(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[1]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.icon" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_icon(i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.image" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_image(i64 {}, i64 {}, i64 {}, ptr {})",
                emitted_args[0].value,
                emitted_args[1].value,
                emitted_args[2].value,
                emitted_args[3].value
            )
            .unwrap();
            drop_owned_ptr(&mut code, &emitted_args[3]);
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.showWhen" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_show_when(ptr {}, i64 {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        other => {
            let f = ctx.funs.resolve(other, ctx.current_module).ok();
            let (ret_ty, ret_kind, payload, sym) = if let Some(f) = f {
                (
                    llvm_type(&f.ret),
                    kind_of_type(&f.ret),
                    payload_of_type(&f.ret),
                    user_symbol(&f.module, &f.name),
                )
            } else {
                ("ptr", Kind::Ptr, Kind::Ptr, format!("sz_user_{other}"))
            };
            let mut arg_parts = Vec::new();
            if let Some(f) = f {
                for (i, p) in f.params.iter().enumerate() {
                    let a = &emitted_args[i];
                    let want = kind_of_type(&p.ty);
                    let (lval, lty) = match (want, a.kind) {
                        (Kind::Int, Kind::Int) => (a.value.clone(), "i64"),
                        (Kind::Float, Kind::Float) => (a.value.clone(), "double"),
                        (Kind::Int, _) => {
                            let v = as_i64(&mut code, a.kind, &a.value, &format!("{prefix}_a{i}"));
                            (v, "i64")
                        }
                        (Kind::Float, _) => {
                            let v = as_f64(&mut code, a.kind, &a.value, &format!("{prefix}_a{i}"));
                            (v, "double")
                        }
                        (_, Kind::Int) | (_, Kind::Float) => {
                            let boxed =
                                box_numeric(&mut code, a.kind, &a.value, &format!("{prefix}_a{i}"));
                            (boxed, "ptr")
                        }
                        _ => (a.value.clone(), "ptr"),
                    };
                    arg_parts.push(format!("{lty} {lval}"));
                }
            } else {
                for (i, a) in emitted_args.iter().enumerate() {
                    let (lval, lty) = if a.kind == Kind::Int {
                        (a.value.clone(), "i64")
                    } else if a.kind == Kind::Float {
                        (a.value.clone(), "double")
                    } else {
                        (a.value.clone(), "ptr")
                    };
                    let _ = i;
                    arg_parts.push(format!("{lty} {lval}"));
                }
            }
            writeln!(
                code,
                "  %{prefix}_v = call {ret_ty} @{sym}({})",
                arg_parts.join(", ")
            )
            .unwrap();
            match ret_kind {
                Kind::Io => io_emitted(code, format!("%{prefix}_v"), payload),
                other_k => val_emitted(code, format!("%{prefix}_v"), other_k),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    fn assert_contains_releases_map_arg(ir: &str) {
        let needle = "call i64 @sz_map_contains(ptr ";
        let at = ir.find(needle).expect("expected sz_map_contains");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of {name} after contains:\n{ir}"
        );
    }

    #[test]
    fn emit_contains_main_and_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hi")"#).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("define i32 @main(i32 %argc, ptr %argv)"));
        assert!(ir.contains("sz_io_println"));
        assert!(ir.contains("sz_runtime_main_args"));
        assert!(ir.contains("sz_release"));
    }

    #[test]
    fn emit_string_retain_on_borrowed_return() {
        let src = r#"
def id(s: String): String = s
@main def main: IO[Unit] = IO.println(id("x"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_retain"));
    }

    #[test]
    fn emit_list_temp_release_after_len() {
        let src = r#"
@main def main: IO[Unit] = IO.println(Str.fromInt(List.len([1, 2])))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let at = ir
            .find("call i64 @sz_list_len")
            .expect("expected sz_list_len");
        assert!(
            ir[at..].contains("sz_release"),
            "expected last-use release of list temp:\n{ir}"
        );
    }

    #[test]
    fn emit_let_binder_release_after_len() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    xs = [1, 2]
    n = List.len(xs)
    _ <- IO.println(Str.fromInt(n))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call i64 @sz_list_len(ptr ";
        let at = ir.find(needle).expect("expected sz_list_len");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected let-binder release of {name} after last use:\n{ir}"
        );
    }

    #[test]
    fn emit_let_binder_release_through_if_phi() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(for {
    xs = [1]
    ys = [2]
  } yield if (true) xs else ys)))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let roots: Vec<&str> = ir
            .lines()
            .filter_map(|line| {
                let line = line.trim();
                if line.contains("_cl") {
                    return None;
                }
                let (name, rest) = line.split_once(" = call ptr @sz_list_cons(")?;
                if rest.contains("@") {
                    return None;
                }
                Some(name.trim())
            })
            .collect();
        assert!(roots.len() >= 2, "expected two list-literal roots:\n{ir}");
        for name in &roots {
            assert!(
                ir.contains(&format!("call void @sz_release(ptr {name})")),
                "expected release of unused/last-use list {name}:\n{ir}"
            );
        }
        assert!(
            ir.contains("sz_retain"),
            "expected retain of if-phi before dropping binders:\n{ir}"
        );
    }

    #[test]
    fn emit_if_arm_temp_release_after_len() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(if (true) [1] else [2])))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call i64 @sz_list_len(ptr ";
        let at = ir.find(needle).expect("expected sz_list_len");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of if-arm list {name}:\n{ir}"
        );
    }

    #[test]
    fn emit_if_mixed_arm_retain_then_release() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(for {
    xs = [1]
  } yield if (false) xs else [2])))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let roots: Vec<&str> = ir
            .lines()
            .filter_map(|line| {
                let line = line.trim();
                if line.contains("_cl") {
                    return None;
                }
                let (name, rest) = line.split_once(" = call ptr @sz_list_cons(")?;
                if rest.contains("@") {
                    return None;
                }
                Some(name.trim())
            })
            .collect();
        assert!(
            roots.len() >= 2,
            "expected xs and else-arm list roots:\n{ir}"
        );
        assert!(
            ir.contains(&format!("call void @sz_retain(ptr {})", roots[0])),
            "expected retain of borrowed xs {}:\n{ir}",
            roots[0]
        );
        let needle = "call i64 @sz_list_len(ptr ";
        let at = ir.find(needle).expect("expected sz_list_len");
        let phi = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {phi})")),
            "expected last-use release of mixed if-phi {phi}:\n{ir}"
        );
    }

    #[test]
    fn emit_match_arm_temp_release_after_len() {
        let src = r#"
enum Color { case Red, case Blue }
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(Color.Red match {
    case Color.Red => [1]
    case Color.Blue => [2]
  })))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call i64 @sz_list_len(ptr ";
        let at = ir.find(needle).expect("expected sz_list_len");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of match-arm list {name}:\n{ir}"
        );
    }

    #[test]
    fn emit_match_mixed_arm_retain_then_release() {
        let src = r#"
enum Color { case Red, case Blue }
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(for {
    xs = [1]
  } yield Color.Red match {
    case Color.Red => xs
    case Color.Blue => [2]
  })))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let roots: Vec<&str> = ir
            .lines()
            .filter_map(|line| {
                let line = line.trim();
                if line.contains("_cl") {
                    return None;
                }
                let (name, rest) = line.split_once(" = call ptr @sz_list_cons(")?;
                if rest.contains("@") {
                    return None;
                }
                Some(name.trim())
            })
            .collect();
        assert!(
            roots.len() >= 2,
            "expected xs and match-arm list roots:\n{ir}"
        );
        assert!(
            ir.contains(&format!("call void @sz_retain(ptr {})", roots[0])),
            "expected retain of borrowed xs {}:\n{ir}",
            roots[0]
        );
        let needle = "call i64 @sz_list_len(ptr ";
        let at = ir.find(needle).expect("expected sz_list_len");
        let phi = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {phi})")),
            "expected last-use release of mixed match-phi {phi}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_retain_on_borrowed_return() {
        let src = r#"
def id(xs: List[Int]): List[Int] = xs
@main def main: IO[Unit] = IO.println(Str.fromInt(List.len(id([1]))))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_retain"));
    }

    #[test]
    fn emit_map_temp_release_after_contains() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(if (Map.contains(Map.set(Map.empty(), "a", "1"), "a")) "y" else "n")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert_contains_releases_map_arg(&ir);
    }

    #[test]
    fn emit_map_temp_release_after_get_or_else() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Map.getOrElse(Map.set(Map.empty(), "a", "1"), "a", "?"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_map_get_or(ptr ";
        let at = ir.find(needle).expect("expected sz_map_get_or");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after getOrElse:\n{ir}"
        );
        let rest = ir[at + needle.len()..].split(')').next().unwrap();
        let dflt = rest
            .split(',')
            .nth(2)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ");
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {dflt})")),
            "expected last-use release of default {dflt} after getOrElse:\n{ir}"
        );
    }

    #[test]
    fn emit_map_get_or_else_drops_default_when_map_borrowed() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(for {
    m = Map.set(Map.empty(), "a", "1")
  } yield Map.getOrElse(m, "a", "?"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_map_get_or(ptr ";
        let at = ir.find(needle).expect("expected sz_map_get_or");
        let rest = ir[at + needle.len()..].split(')').next().unwrap();
        let dflt = rest
            .split(',')
            .nth(2)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ");
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {dflt})")),
            "expected last-use release of borrowed-map default {dflt}:\n{ir}"
        );
        assert!(
            ir[at..].contains("sz_retain"),
            "expected retain of getOrElse result before dropping default:\n{ir}"
        );
    }

    #[test]
    fn emit_set_temp_release_after_contains() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(if (Set.contains(Set.add(Set.empty(), "x"), "x")) "y" else "n")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert_contains_releases_map_arg(&ir);
    }

    #[test]
    fn emit_map_retain_on_borrowed_return() {
        let src = r#"
def id(m: Map[String, String]): Map[String, String] = m
@main def main: IO[Unit] =
  IO.println(Map.getOrElse(id(Map.set(Map.empty(), "a", "1")), "a", "?"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_retain"));
    }

    #[test]
    fn emit_resource_make_use() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_lang_resource_make"));
        assert!(ir.contains("sz_lang_resource_use"));
        assert!(ir.contains("sz_rcont_"));
    }

    #[test]
    fn emit_resource_make_releases_acquire() {
        let src = r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_resource_make(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_resource_make");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of acquire {name} after Resource.make:\n{ir}"
        );
    }

    #[test]
    fn emit_resource_use_releases_resource() {
        let src = r#"@main def main: IO[Unit] =
  Resource.use(Resource.make(IO.pure("tok"), t => IO.println(t)), t => IO.println(t))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_resource_use(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_resource_use");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of resource {name} after Resource.use:\n{ir}"
        );
    }

    #[test]
    fn emit_io_forever_repeat_retry() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n <- IO.repeatN(2, IO.pure("ok"))
    _ <- IO.println(n)
    t <- IO.retryN(1, IO.pure("ok"))
    _ <- IO.println(t)
    h <- Fiber.fork(IO.forever(IO.sleep(1)))
    _ <- Fiber.interrupt(h)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_io_forever"));
        assert!(ir.contains("sz_io_repeat_n"));
        assert!(ir.contains("sz_io_retry_n"));
    }

    #[test]
    fn emit_io_forever_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    h <- Fiber.fork(IO.forever(IO.sleep(1)))
    _ <- Fiber.interrupt(h)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_forever(ptr ";
        let at = ir.find(needle).expect("expected sz_io_forever");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after IO.forever:\n{ir}"
        );
    }

    #[test]
    fn emit_fiber_fork_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    h <- Fiber.fork(IO.sleep(1))
    _ <- Fiber.interrupt(h)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_fiber_fork(ptr ";
        let at = ir.find(needle).expect("expected sz_fiber_fork");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after Fiber.fork:\n{ir}"
        );
    }

    #[test]
    fn emit_fiber_join_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    f <- Fiber.fork(IO.pure("ok"))
    v <- Fiber.join(f)
    _ <- IO.println(v)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_fiber_join(ptr "),
            "expected sz_fiber_join:\n{ir}"
        );
        assert!(
            ir.contains("call ptr @sz_io_println(ptr %value)"),
            "expected println of Fiber.join binder:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Fiber.join binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_io_timeout_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    got <- IO.timeout(50, IO.pure("ok"))
    _ <- IO.println(got)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_timeout(i64 50, ptr ";
        let at = ir.find(needle).expect("expected sz_io_timeout");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after IO.timeout:\n{ir}"
        );
    }

    #[test]
    fn emit_io_ensure_releases_inner_and_finalizer() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.ensure(IO.pure("ok"), IO.println("fin"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_ensure(ptr ";
        let at = ir.find(needle).expect("expected sz_io_ensure");
        let rest = &ir[at + needle.len()..];
        let inner = rest.split(',').next().unwrap().trim();
        let fin = rest
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .split(')')
            .next()
            .unwrap()
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {inner})")),
            "expected last-use release of inner {inner} after IO.ensure:\n{ir}"
        );
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {fin})")),
            "expected last-use release of finalizer {fin} after IO.ensure:\n{ir}"
        );
    }

    #[test]
    fn emit_io_race_releases_arms() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.race(IO.sleep(1), IO.pure("ok"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_race(ptr ";
        let at = ir.find(needle).expect("expected sz_io_race");
        let rest = &ir[at + needle.len()..];
        let left = rest.split(',').next().unwrap().trim();
        let right = rest
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .split(')')
            .next()
            .unwrap()
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {left})")),
            "expected last-use release of left {left} after IO.race:\n{ir}"
        );
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {right})")),
            "expected last-use release of right {right} after IO.race:\n{ir}"
        );
    }

    #[test]
    fn emit_io_both_releases_arms() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.both(IO.pure("a"), IO.pure("b"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_both(ptr ";
        let at = ir.find(needle).expect("expected sz_io_both");
        let rest = &ir[at + needle.len()..];
        let left = rest.split(',').next().unwrap().trim();
        let right = rest
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .split(')')
            .next()
            .unwrap()
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {left})")),
            "expected last-use release of left {left} after IO.both:\n{ir}"
        );
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {right})")),
            "expected last-use release of right {right} after IO.both:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of IO.both pair binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_handle_error_with_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_handle_error_with(ptr ";
        let at = ir.find(needle).expect("expected sz_io_handle_error_with");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after handleErrorWith:\n{ir}"
        );
    }

    #[test]
    fn emit_handle_error_with_releases_capture_pack() {
        let src = r#"@main def main: IO[Unit] =
  IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_handle_error_with(ptr ";
        let at = ir.find(needle).expect("expected sz_io_handle_error_with");
        let close = ir[at..]
            .find(')')
            .expect("expected handleErrorWith call close");
        let env = ir[at..at + close].rsplit("ptr ").next().unwrap().trim();
        assert!(
            ir[at + close..].contains(&format!("call void @sz_release(ptr {env})")),
            "expected last-use release of capture pack {env} after handleErrorWith:\n{ir}"
        );
    }

    #[test]
    fn emit_handle_error_with_releases_error_message() {
        let src = r#"@main def main: IO[Unit] =
  IO.fail("boom").handleErrorWith(e => IO.println(e))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = " = call ptr @sz_error_message(ptr ";
        let at = ir.find(needle).expect("expected sz_error_message");
        let line_start = ir[..at].rfind('\n').map(|i| i + 1).unwrap_or(0);
        let name = ir[line_start..at].trim();
        assert!(
            name.starts_with('%'),
            "expected SSA name for error message, got {name:?}:\n{ir}"
        );
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of error message {name} after handleErrorWith body:\n{ir}"
        );
    }

    #[test]
    fn emit_ref_set_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of("a")
    _ <- Ref.set(r, "b")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_ref_set(ptr ";
        let at = ir.find(needle).expect("expected sz_ref_set");
        let close = ir[at..].find(')').expect("expected sz_ref_set call close");
        let value = ir[at..at + close].rsplit("ptr ").next().unwrap().trim();
        assert_ne!(value, "null", "expected a value, not null:\n{ir}");
        assert!(
            ir[at + close..].contains(&format!("call void @sz_release(ptr {value})")),
            "expected last-use release of value {value} after Ref.set:\n{ir}"
        );
    }

    #[test]
    fn emit_queue_offer_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- Queue.offer(q, "a")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_queue_offer(ptr ";
        let at = ir.find(needle).expect("expected sz_queue_offer");
        let close = ir[at..]
            .find(')')
            .expect("expected sz_queue_offer call close");
        let value = ir[at..at + close].rsplit("ptr ").next().unwrap().trim();
        assert_ne!(value, "null", "expected a value, not null:\n{ir}");
        assert!(
            ir[at + close..].contains(&format!("call void @sz_release(ptr {value})")),
            "expected last-use release of value {value} after Queue.offer:\n{ir}"
        );
    }

    #[test]
    fn emit_deferred_complete_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    d <- Deferred.empty()
    _ <- Deferred.complete(d, "a")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_deferred_complete(ptr ";
        let at = ir.find(needle).expect("expected sz_deferred_complete");
        let close = ir[at..]
            .find(')')
            .expect("expected sz_deferred_complete call close");
        let value = ir[at..at + close].rsplit("ptr ").next().unwrap().trim();
        assert_ne!(value, "null", "expected a value, not null:\n{ir}");
        assert!(
            ir[at + close..].contains(&format!("call void @sz_release(ptr {value})")),
            "expected last-use release of value {value} after Deferred.complete:\n{ir}"
        );
    }

    fn assert_owned_ptr_args_released(src: &str, decl: &str) {
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = format!("call ptr @{decl}(");
        let at = ir
            .find(&needle)
            .unwrap_or_else(|| panic!("expected {decl}:\n{ir}"));
        let close = ir[at..]
            .find(')')
            .unwrap_or_else(|| panic!("expected {decl} call close:\n{ir}"));
        let args = &ir[at + needle.len()..at + close];
        let after = &ir[at + close..];
        let mut saw_ptr = false;
        for part in args.split(", ") {
            let part = part.trim();
            if let Some(v) = part.strip_prefix("ptr ") {
                if v == "null" {
                    continue;
                }
                saw_ptr = true;
                assert!(
                    after.contains(&format!("call void @sz_release(ptr {v})")),
                    "expected last-use release of {v} after {decl}:\n{ir}"
                );
            }
        }
        assert!(saw_ptr, "expected a ptr arg for {decl}:\n{ir}");
    }

    #[test]
    fn emit_kit_string_args_release_after_call() {
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Fs.read("x").flatMap(_ => IO.pure(()))"#,
            "sz_fs_read",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Fs.write("x", "y")"#,
            "sz_fs_write",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Fs.list("x").flatMap(_ => IO.pure(()))"#,
            "sz_fs_list",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Fs.mkdirs("x")"#,
            "sz_fs_mkdirs",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Fs.canonicalize("x").flatMap(_ => IO.pure(()))"#,
            "sz_fs_canonicalize",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Sys.write("x")"#,
            "sz_sys_write",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Sys.exec("true").flatMap(_ => IO.pure(()))"#,
            "sz_sys_exec",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Sys.spawn("true").flatMap(_ => IO.pure(()))"#,
            "sz_sys_spawn",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Sys.getenv("PATH").flatMap(_ => IO.pure(()))"#,
            "sz_sys_getenv",
        );
        assert_owned_ptr_args_released(
            r#"@main def main: IO[Unit] = Net.httpGet("http://example.test/").flatMap(_ => IO.pure(()))"#,
            "sz_net_http_get",
        );
    }

    #[test]
    fn emit_ref_get_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of("a")
    v <- Ref.get(r)
    _ <- IO.println(v)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_io_println(ptr %value)"),
            "expected println of Ref.get binder:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Ref.get binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_ref_of_releases_handle() {
        let src = r#"@main def main: IO[Unit] =
  for {
    r <- Ref.of("a")
    _ <- Ref.set(r, "b")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_ref_of(ptr "),
            "expected sz_ref_of:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Ref.of binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_queue_unbounded_releases_handle() {
        let src = r#"@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- Queue.offer(q, "a")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_queue_unbounded()"),
            "expected sz_queue_unbounded:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Queue.unbounded binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_deferred_empty_releases_handle() {
        let src = r#"@main def main: IO[Unit] =
  for {
    d <- Deferred.empty()
    _ <- Deferred.complete(d, "a")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_deferred_empty()"),
            "expected sz_deferred_empty:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Deferred.empty binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_queue_take_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- Queue.offer(q, "a")
    v <- Queue.take(q)
    _ <- IO.println(v)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_io_println(ptr %value)"),
            "expected println of Queue.take binder:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Queue.take binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_deferred_get_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    d <- Deferred.empty()
    _ <- Deferred.complete(d, "a")
    v <- Deferred.get(d)
    _ <- IO.println(v)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_io_println(ptr %value)"),
            "expected println of Deferred.get binder:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of Deferred.get binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_flatmap_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  IO.pure("ok").flatMap(_ => IO.println("ok"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_flatmap(ptr ";
        let at = ir.find(needle).expect("expected sz_io_flatmap");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after flatMap:\n{ir}"
        );
    }

    #[test]
    fn emit_flatmap_releases_capture_pack() {
        let src = r#"@main def main: IO[Unit] =
  IO.pure("ok").flatMap(_ => IO.println("ok"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_flatmap(ptr ";
        let at = ir.find(needle).expect("expected sz_io_flatmap");
        let close = ir[at..].find(')').expect("expected flatMap call close");
        let env = ir[at..at + close].rsplit("ptr ").next().unwrap().trim();
        assert!(
            ir[at + close..].contains(&format!("call void @sz_release(ptr {env})")),
            "expected last-use release of capture pack {env} after flatMap:\n{ir}"
        );
    }

    #[test]
    fn emit_io_fail_releases_error() {
        let src = r#"@main def main: IO[Unit] =
  IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_fail(ptr ";
        let at = ir.find(needle).expect("expected sz_io_fail");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert_ne!(name, "null", "expected an error, not null:\n{ir}");
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of error {name} after IO.fail:\n{ir}"
        );
    }

    #[test]
    fn emit_io_pure_releases_payload() {
        let src = r#"@main def main: IO[Unit] =
  IO.pure("ok").flatMap(_ => IO.println("ok"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_pure(ptr ";
        let at = ir.find(needle).expect("expected sz_io_pure");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert_ne!(name, "null", "expected a payload, not null:\n{ir}");
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of payload {name} after IO.pure:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of IO.pure binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_io_pure_for_binder_releases_value() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s <- IO.pure("ok")
    _ <- IO.println(s)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_io_println(ptr %value)"),
            "expected println of IO.pure binder:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of IO.pure binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_io_attempt_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.fail("boom").attempt
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_attempt_as_result(ptr ";
        let at = ir.find(needle).expect("expected sz_io_attempt_as_result");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after IO.attempt:\n{ir}"
        );
        assert!(
            ir.contains("call void @sz_release(ptr %value)"),
            "expected last-use release of attempt Result binder %value:\n{ir}"
        );
    }

    #[test]
    fn emit_io_repeat_n_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n <- IO.repeatN(2, IO.pure("ok"))
    _ <- IO.println(n)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_repeat_n(i64 2, ptr ";
        let at = ir.find(needle).expect("expected sz_io_repeat_n");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after IO.repeatN:\n{ir}"
        );
    }

    #[test]
    fn emit_io_retry_n_releases_inner() {
        let src = r#"@main def main: IO[Unit] =
  for {
    t <- IO.retryN(1, IO.pure("ok"))
    _ <- IO.println(t)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_io_retry_n(i64 1, ptr ";
        let at = ir.find(needle).expect("expected sz_io_retry_n");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of inner {name} after IO.retryN:\n{ir}"
        );
    }

    #[test]
    fn emit_stream_evalmap_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.evalMap(Stream.emit("a"), x => IO.pure(x))
    xs <- Stream.compileToList(s)
    _ <- Stream.drain(Stream.emit("b"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_evalmap"));
        assert!(ir.contains("sz_stream_compile_to_list"));
        assert!(ir.contains("sz_stream_drain"));
        assert!(ir.contains("sz_rcont_"));
    }

    #[test]
    fn emit_stream_filter_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.filter(Stream.emits(["a", "", "b"]), x => Str.len(x) > 0)
    xs <- Stream.compileToList(s)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_filter"));
        assert!(ir.contains("sz_pred_"));
    }

    #[test]
    fn emit_stream_emits_releases_list() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.emits(["a"]))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_stream_emits(ptr ";
        let at = ir.find(needle).expect("expected sz_stream_emits");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after Stream.emits:\n{ir}"
        );
    }

    #[test]
    fn emit_stream_eval_releases_io() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    xs <- Stream.compileToList(Stream.eval(IO.pure("a")))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_stream_eval(ptr ";
        let at = ir.find(needle).expect("expected sz_stream_eval");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of IO {name} after Stream.eval:\n{ir}"
        );
    }

    #[test]
    fn emit_stream_emit_releases_value() {
        let src = r#"
@main def main: IO[Unit] =
  Stream.drain(Stream.emit("a"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_stream_emit(ptr ";
        let at = ir.find(needle).expect("expected sz_stream_emit");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of value {name} after Stream.emit:\n{ir}"
        );
    }

    #[test]
    fn emit_stream_drain_releases_stream() {
        let src = r#"@main def main: IO[Unit] =
  Stream.drain(Stream.emit("a"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_stream_drain(ptr ";
        let at = ir.find(needle).expect("expected sz_stream_drain");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of stream {name} after Stream.drain:\n{ir}"
        );
    }

    #[test]
    fn emit_list_filter_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.filter(["a", "b", "a"], x => x != "b")
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_filter"));
        assert!(ir.contains("sz_pred_"));
    }

    fn last_cl2_before(ir: &str, at: usize) -> &str {
        let before = &ir[..at];
        let idx = before
            .rfind("_cl2 = call ptr @sz_list_cons")
            .expect("expected closure pack cons");
        let line_start = before[..idx].rfind('\n').map(|i| i + 1).unwrap_or(0);
        before[line_start..idx + 4].trim()
    }

    #[test]
    fn emit_list_filter_releases_closure_pack() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(List.filter([1], x => true))))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let at = ir
            .find("call ptr @sz_list_filter")
            .expect("expected sz_list_filter");
        let pack = last_cl2_before(&ir, at);
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {pack})")),
            "expected last-use release of filter closure {pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_map_releases_closure_pack() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(List.map([1], x => x))))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let at = ir
            .find("call ptr @sz_list_map")
            .expect("expected sz_list_map");
        let pack = last_cl2_before(&ir, at);
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {pack})")),
            "expected last-use release of map closure {pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_map_retains_borrowed_element() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(Str.fromInt(List.len(List.map(["a"], x => x))))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let fn_at = ir
            .find("define internal ptr @sz_smap_")
            .expect("expected sz_smap_");
        let fn_end = ir[fn_at..].find("\n}").expect("expected smap body end");
        let body = &ir[fn_at..fn_at + fn_end];
        assert!(
            body.contains("call void @sz_retain(ptr %value)"),
            "expected retain of borrowed map element:\n{body}"
        );
    }

    #[test]
    fn emit_list_map_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.map(["a", "b"], x => Str.concat(x, "!"))
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_map"));
        assert!(ir.contains("sz_smap_"));
    }

    #[test]
    fn emit_list_append_drops_owned_elem() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(List.join(List.append(["milk"], Str.trim(" f ")), ","))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let at = ir
            .find("call ptr @sz_list_append")
            .expect("expected sz_list_append");
        assert!(
            ir[at..].contains("sz_release"),
            "expected last-use release after List.append:\n{ir}"
        );
    }

    #[test]
    fn emit_list_append_binder_release() {
        let src = r#"@main def main: IO[Unit] =
  for {
    d = Str.trim(" f ")
    xs = List.append(["milk"], d)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_list_append(ptr ";
        let at = ir.find(needle).expect("expected sz_list_append");
        let rest = ir[at + needle.len()..].split(')').next().unwrap();
        let elem = rest
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {elem})")),
            "expected binder release of append element {elem}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_set_at_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    xs = List.setAt(["a", "b"], 0, "c")
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_set_at"));
    }

    #[test]
    fn emit_list_take_drop_find_exists() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.take(["a", "b", "c"], 2), ","))
    _ <- IO.println(List.join(List.drop(["a", "b", "c"], 1), ","))
    _ <- IO.println(List.join(List.find(["a", "b"], x => x == "b"), ","))
    _ <- IO.println(if (List.exists(["a", "c"], x => x == "c")) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_take"));
        assert!(ir.contains("sz_list_drop"));
        assert!(ir.contains("sz_list_find"));
        assert!(ir.contains("sz_list_exists"));
        assert!(ir.contains("sz_pred_"));
        let take_needle = "call ptr @sz_list_take(ptr ";
        let take_at = ir.find(take_needle).expect("take");
        let take_list = ir[take_at + take_needle.len()..]
            .split(',')
            .next()
            .unwrap()
            .trim();
        assert!(
            ir[take_at..].contains(&format!("call void @sz_release(ptr {take_list})")),
            "expected last-use release of list {take_list} after List.take:\n{ir}"
        );
        let drop_needle = "call ptr @sz_list_drop(ptr ";
        let drop_at = ir.find(drop_needle).expect("drop");
        let drop_list = ir[drop_at + drop_needle.len()..]
            .split(',')
            .next()
            .unwrap()
            .trim();
        assert!(
            ir[drop_at..].contains(&format!("call void @sz_release(ptr {drop_list})")),
            "expected last-use release of list {drop_list} after List.drop:\n{ir}"
        );
        let find_at = ir.find("call ptr @sz_list_find").expect("find");
        let pack = last_cl2_before(&ir, find_at);
        assert!(
            ir[find_at..].contains(&format!("call void @sz_release(ptr {pack})")),
            "expected last-use release of find closure {pack}:\n{ir}"
        );
        let exists_at = ir.find("call i64 @sz_list_exists").expect("exists");
        let exists_pack = last_cl2_before(&ir, exists_at);
        assert!(
            ir[exists_at..].contains(&format!("call void @sz_release(ptr {exists_pack})")),
            "expected last-use release of exists closure {exists_pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_take_right_drop_right_init_last() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.takeRight(["a", "b", "c"], 2), ","))
    _ <- IO.println(List.join(List.dropRight(["a", "b", "c"], 1), ","))
    _ <- IO.println(List.join(List.init(["a", "b", "c"]), ","))
    _ <- IO.println(List.join(List.last(["a", "b", "c"]), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_take_right"));
        assert!(ir.contains("sz_list_drop_right"));
        assert!(ir.contains("sz_list_init"));
        assert!(ir.contains("sz_list_last"));
        let needle = "call ptr @sz_list_take_right(ptr ";
        let at = ir.find(needle).expect("takeRight");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after takeRight:\n{ir}"
        );
        let needle = "call ptr @sz_list_init(ptr ";
        let at = ir.find(needle).expect("init");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after init:\n{ir}"
        );
        let needle = "call ptr @sz_list_drop_right(ptr ";
        let at = ir.find(needle).expect("dropRight");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after dropRight:\n{ir}"
        );
        let needle = "call ptr @sz_list_last(ptr ";
        let at = ir.find(needle).expect("last");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after last:\n{ir}"
        );
    }

    #[test]
    fn emit_list_get_or_else_fill_map_is_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.getOrElse(["a", "b"], 0, "z"))
    _ <- IO.println(List.join(List.fill(2, "a"), ","))
    _ <- IO.println(if (Map.isEmpty(Map.empty())) "y" else "n")
    _ <- IO.println(if (Set.isEmpty(Set.empty())) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_get_or"));
        assert!(ir.contains("sz_list_fill"));
        assert!(ir.contains("sz_map_size"));
        let needle = "call ptr @sz_list_get_or(ptr ";
        let at = ir.find(needle).expect("getOrElse");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after getOrElse:\n{ir}"
        );
        let needle = "call ptr @sz_list_fill(i64 ";
        let at = ir.find(needle).expect("fill");
        assert!(
            ir[at..].contains("call void @sz_release(ptr "),
            "expected last-use release after List.fill:\n{ir}"
        );
        let empty_at = ir.find("icmp eq i64 ").expect("isEmpty");
        assert!(
            ir[empty_at.saturating_sub(120)..empty_at].contains("sz_map_size"),
            "expected Map/Set.isEmpty via sz_map_size:\n{ir}"
        );
    }

    #[test]
    fn emit_list_take_while_drop_while_forall() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.takeWhile(["a", "b"], x => x != "b"), ","))
    _ <- IO.println(List.join(List.dropWhile(["a", "b"], x => x != "b"), ","))
    _ <- IO.println(if (List.forall(["a"], x => true)) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_takewhile"));
        assert!(ir.contains("sz_list_dropwhile"));
        assert!(ir.contains("sz_list_forall"));
        let tw_at = ir.find("call ptr @sz_list_takewhile").expect("takeWhile");
        let tw_pack = last_cl2_before(&ir, tw_at);
        assert!(
            ir[tw_at..].contains(&format!("call void @sz_release(ptr {tw_pack})")),
            "expected last-use release of takeWhile closure {tw_pack}:\n{ir}"
        );
        let dw_at = ir.find("call ptr @sz_list_dropwhile").expect("dropWhile");
        let dw_pack = last_cl2_before(&ir, dw_at);
        assert!(
            ir[dw_at..].contains(&format!("call void @sz_release(ptr {dw_pack})")),
            "expected last-use release of dropWhile closure {dw_pack}:\n{ir}"
        );
        let fa_at = ir.find("call i64 @sz_list_forall").expect("forall");
        let fa_pack = last_cl2_before(&ir, fa_at);
        assert!(
            ir[fa_at..].contains(&format!("call void @sz_release(ptr {fa_pack})")),
            "expected last-use release of forall closure {fa_pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_split_at_span_partition() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.map(List.splitAt(["a", "b"], 1), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.span(["a", "b"], x => x != "b"), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.partition(["a", "b"], x => x == "a"), g => List.join(g, ",")), "|"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_split_at"));
        assert!(ir.contains("sz_list_span"));
        assert!(ir.contains("sz_list_partition"));
        let needle = "call ptr @sz_list_split_at(ptr ";
        let at = ir.find(needle).expect("splitAt");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.splitAt:\n{ir}"
        );
        let sp_at = ir.find("call ptr @sz_list_span").expect("span");
        let sp_pack = last_cl2_before(&ir, sp_at);
        assert!(
            ir[sp_at..].contains(&format!("call void @sz_release(ptr {sp_pack})")),
            "expected last-use release of span closure {sp_pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_list_count_filter_not_str_reverse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(List.count(["a", "b", "c"], x => x != "b")))
    _ <- IO.println(List.join(List.filterNot(["a", "b", "c"], x => x == "b"), ","))
    _ <- IO.println(Str.reverse("abc"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_count"));
        assert!(ir.contains("sz_list_filter_not"));
        assert!(ir.contains("sz_string_reverse"));
        let count_at = ir.find("call i64 @sz_list_count").expect("count");
        let count_pack = last_cl2_before(&ir, count_at);
        assert!(
            ir[count_at..].contains(&format!("call void @sz_release(ptr {count_pack})")),
            "expected last-use release of count closure {count_pack}:\n{ir}"
        );
        let fn_at = ir.find("call ptr @sz_list_filter_not").expect("filterNot");
        let fn_pack = last_cl2_before(&ir, fn_at);
        assert!(
            ir[fn_at..].contains(&format!("call void @sz_release(ptr {fn_pack})")),
            "expected last-use release of filterNot closure {fn_pack}:\n{ir}"
        );
        let needle = "call ptr @sz_string_reverse(ptr ";
        let at = ir.find(needle).expect("reverse");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Str.reverse:\n{ir}"
        );
    }

    #[test]
    fn emit_list_flat_map_pad_to_non_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.flatMap(["a", "b"], x => [x, x]), ","))
    _ <- IO.println(List.join(List.padTo(["a"], 3, "z"), ","))
    _ <- IO.println(if (List.nonEmpty(["a"])) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_flat_map"));
        assert!(ir.contains("sz_list_pad_to"));
        assert!(ir.contains("sz_list_non_empty"));
        let fm_at = ir.find("call ptr @sz_list_flat_map").expect("flatMap");
        let fm_pack = last_cl2_before(&ir, fm_at);
        assert!(
            ir[fm_at..].contains(&format!("call void @sz_release(ptr {fm_pack})")),
            "expected last-use release of flatMap closure {fm_pack}:\n{ir}"
        );
        let needle = "call ptr @sz_list_pad_to(ptr ";
        let at = ir.find(needle).expect("padTo");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.padTo:\n{ir}"
        );
    }

    #[test]
    fn emit_list_range_tabulate_intersperse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.map(List.range(1, 4), n => Str.fromInt(n)), ","))
    _ <- IO.println(List.join(List.tabulate(3, i => Str.fromInt(i)), ","))
    _ <- IO.println(List.join(List.intersperse(["a", "b"], "|"), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_range"));
        assert!(ir.contains("sz_list_tabulate"));
        assert!(ir.contains("sz_list_intersperse"));
        let tab_at = ir.find("call ptr @sz_list_tabulate").expect("tabulate");
        let tab_pack = last_cl2_before(&ir, tab_at);
        assert!(
            ir[tab_at..].contains(&format!("call void @sz_release(ptr {tab_pack})")),
            "expected last-use release of tabulate closure {tab_pack}:\n{ir}"
        );
        let needle = "call ptr @sz_list_intersperse(ptr ";
        let at = ir.find(needle).expect("intersperse");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.intersperse:\n{ir}"
        );
    }

    #[test]
    fn emit_list_grouped_sliding_non_empty() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.map(List.grouped(["a", "b", "c"], 2), g => List.join(g, ",")), "|"))
    _ <- IO.println(List.join(List.map(List.sliding(["a", "b", "c"], 2), g => List.join(g, ",")), "|"))
    _ <- IO.println(if (Str.nonEmpty("a")) "y" else "n")
    _ <- IO.println(if (Map.nonEmpty(Map.set(Map.empty(), "a", "1"))) "y" else "n")
    _ <- IO.println(if (Set.nonEmpty(Set.add(Set.empty(), "x"))) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_grouped"));
        assert!(ir.contains("sz_list_sliding"));
        assert!(ir.contains("sz_string_non_empty"));
        assert!(ir.contains("icmp ne i64"));
        let needle = "call ptr @sz_list_grouped(ptr ";
        let at = ir.find(needle).expect("grouped");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.grouped:\n{ir}"
        );
        let needle = "call ptr @sz_list_sliding(ptr ";
        let at = ir.find(needle).expect("sliding");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.sliding:\n{ir}"
        );
        let needle = "call i64 @sz_string_non_empty(ptr ";
        let at = ir.find(needle).expect("nonEmpty");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Str.nonEmpty:\n{ir}"
        );
    }

    #[test]
    fn emit_list_slice_index_where() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(List.slice(["a", "b", "c"], 1, 3), ","))
    _ <- IO.println(Str.fromInt(List.indexWhere(["a", "b", "c"], x => x == "b")))
    _ <- IO.println(Str.fromInt(List.lastIndexWhere(["a", "b", "c"], x => x != "z")))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_slice"));
        assert!(ir.contains("sz_list_index_where"));
        assert!(ir.contains("sz_list_last_index_where"));
        let needle = "call ptr @sz_list_slice(ptr ";
        let at = ir.find(needle).expect("slice");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.slice:\n{ir}"
        );
        let ix_at = ir
            .find("call i64 @sz_list_index_where")
            .expect("indexWhere");
        let ix_pack = last_cl2_before(&ir, ix_at);
        assert!(
            ir[ix_at..].contains(&format!("call void @sz_release(ptr {ix_pack})")),
            "expected last-use release of indexWhere closure {ix_pack}:\n{ir}"
        );
        let lix_at = ir
            .find("call i64 @sz_list_last_index_where")
            .expect("lastIndexWhere");
        let lix_pack = last_cl2_before(&ir, lix_at);
        assert!(
            ir[lix_at..].contains(&format!("call void @sz_release(ptr {lix_pack})")),
            "expected last-use release of lastIndexWhere closure {lix_pack}:\n{ir}"
        );
    }

    #[test]
    fn emit_map_get_str_capitalize_list_indices() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(Map.get(Map.set(Map.empty(), "a", "1"), "a"), ","))
    _ <- IO.println(Str.capitalize("hello"))
    _ <- IO.println(List.join(List.map(List.indices(["a", "b"]), n => Str.fromInt(n)), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_map_get"));
        assert!(ir.contains("sz_string_capitalize"));
        assert!(ir.contains("sz_list_indices"));
        let needle = "call ptr @sz_map_get(ptr ";
        let at = ir.find(needle).expect("map get");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after Map.get:\n{ir}"
        );
        let needle = "call ptr @sz_string_capitalize(ptr ";
        let at = ir.find(needle).expect("capitalize");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Str.capitalize:\n{ir}"
        );
        let needle = "call ptr @sz_list_indices(ptr ";
        let at = ir.find(needle).expect("indices");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.indices:\n{ir}"
        );
    }

    #[test]
    fn emit_set_union_intersect_diff() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(Set.toList(Set.union(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "y"))), ","))
    _ <- IO.println(List.join(Set.toList(Set.intersect(Set.add(Set.empty(), "x"), Set.add(Set.empty(), "x"))), ","))
    _ <- IO.println(List.join(Set.toList(Set.diff(Set.add(Set.empty(), "x"), Set.empty())), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_set_union"));
        assert!(ir.contains("sz_set_intersect"));
        assert!(ir.contains("sz_set_diff"));
        let needle = "call ptr @sz_set_union(ptr ";
        let at = ir.find(needle).expect("union");
        let left = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {left})")),
            "expected last-use release of set {left} after Set.union:\n{ir}"
        );
    }

    #[test]
    fn emit_list_temp_release_after_set_at() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(List.setAt(["a", "b"], 0, "c"), ","))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_list_set_at(ptr ";
        let at = ir.find(needle).expect("expected sz_list_set_at");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after setAt:\n{ir}"
        );
    }

    #[test]
    fn emit_map_and_set_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    m = Map.set(Map.empty(), "a", "1")
    s = Set.add(Set.empty(), "x")
    gone = Map.remove(m, "a")
    dropped = Set.remove(s, "x")
    _ <- IO.println(Map.getOrElse(m, "a", "?"))
    _ <- IO.println(if (Set.contains(s, "x")) "y" else "n")
    _ <- IO.println(List.join(Map.keys(gone), ","))
    _ <- IO.println(List.join(Map.values(gone), ","))
    _ <- IO.println(s"${Map.size(gone)}")
    _ <- IO.println(List.join(Set.toList(dropped), ","))
    _ <- IO.println(s"${Set.size(dropped)}")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_map_set"));
        assert!(ir.contains("sz_map_get_or"));
        assert!(ir.contains("sz_map_contains"));
        assert!(ir.contains("sz_map_remove"));
        assert!(ir.contains("sz_map_keys"));
        assert!(ir.contains("sz_map_values"));
        assert!(ir.contains("sz_map_size"));
    }

    #[test]
    fn emit_map_temp_release_after_remove() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(if (Map.contains(Map.remove(Map.set(Map.empty(), "a", "1"), "a"), "a")) "y" else "n")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_map_remove(ptr ";
        let at = ir.find(needle).expect("expected sz_map_remove");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after remove:\n{ir}"
        );
    }

    #[test]
    fn emit_map_temp_release_after_keys() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(Map.keys(Map.set(Map.empty(), "a", "1")), ","))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_map_keys(ptr ";
        let at = ir.find(needle).expect("expected sz_map_keys");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after keys:\n{ir}"
        );
    }

    #[test]
    fn emit_map_temp_release_after_values() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(List.join(Map.values(Map.set(Map.empty(), "a", "1")), ","))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_map_values(ptr ";
        let at = ir.find(needle).expect("expected sz_map_values");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after values:\n{ir}"
        );
    }

    #[test]
    fn emit_map_temp_release_after_size() {
        let src = r#"
@main def main: IO[Unit] =
  IO.println(s"${Map.size(Map.set(Map.empty(), "a", "1"))}")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call i64 @sz_map_size(ptr ";
        let at = ir.find(needle).expect("expected sz_map_size");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of map {name} after size:\n{ir}"
        );
    }

    #[test]
    fn emit_str_starts_with_compile() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.startsWith("ab", "a")) "yes" else "no")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_starts_with"));
    }

    #[test]
    fn emit_str_contains_ends_toint_replace() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(if (Str.contains("ab", "b")) "y" else "n")
    _ <- IO.println(if (Str.endsWith("ab", "b")) "y" else "n")
    _ <- IO.println(s"${Str.toInt("7", 0)}")
    _ <- IO.println(Str.replace("a-b", "-", ":"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_contains"));
        assert!(ir.contains("sz_string_ends_with"));
        assert!(ir.contains("sz_string_to_int"));
        assert!(ir.contains("sz_string_replace"));
        let needle = "call ptr @sz_string_replace(ptr ";
        let at = ir.find(needle).expect("expected sz_string_replace");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after replace:\n{ir}"
        );
    }

    #[test]
    fn emit_str_split_list_concat_flatten() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(List.join(Str.split("a,b", ","), ":"))
    _ <- IO.println(List.join(List.concat(["a"], ["b", "c"]), ","))
    xss = List.append(List.append(List.empty(), ["a"]), ["b", "c"])
    _ <- IO.println(List.join(List.flatten(xss), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_split"));
        assert!(ir.contains("sz_list_concat"));
        assert!(ir.contains("sz_list_flatten"));
        let needle = "call ptr @sz_string_split(ptr ";
        let at = ir.find(needle).expect("expected sz_string_split");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after split:\n{ir}"
        );
    }

    #[test]
    fn emit_float_arith_and_convert() {
        let src = r#"
def scale(x: Float): Float = x * 2.0
@main def main: IO[Unit] =
  IO.println(s"${scale(1.5) + Float.fromInt(1)} ${Float.toInt(2.9)}")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("fmul double"));
        assert!(ir.contains("fadd double"));
        assert!(ir.contains("sitofp i64"));
        assert!(ir.contains("fptosi double"));
        assert!(ir.contains("sz_string_from_float"));
    }

    #[test]
    fn emit_str_trim_compile() {
        let src = r#"@main def main: IO[Unit] =
  IO.println(Str.trim("  x  "))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_trim"));
    }

    #[test]
    fn emit_str_is_empty_case_repeat() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(if (Str.isEmpty("")) "y" else "n")
    _ <- IO.println(Str.toLower("Ab"))
    _ <- IO.println(Str.toUpper("Ab"))
    _ <- IO.println(Str.repeat("a", 3))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_is_empty"));
        assert!(ir.contains("sz_string_to_lower"));
        assert!(ir.contains("sz_string_to_upper"));
        assert!(ir.contains("sz_string_repeat"));
        let needle = "call ptr @sz_string_to_lower(ptr ";
        let at = ir.find(needle).expect("expected sz_string_to_lower");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after toLower:\n{ir}"
        );
        let needle = "call ptr @sz_string_repeat(ptr ";
        let at = ir.find(needle).expect("expected sz_string_repeat");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after repeat:\n{ir}"
        );
    }

    #[test]
    fn emit_str_strip_pad_blank() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.stripPrefix("abc", "a"))
    _ <- IO.println(Str.stripSuffix("abc", "c"))
    _ <- IO.println(Str.padLeft("a", 3, "x"))
    _ <- IO.println(Str.padRight("a", 3, "x"))
    _ <- IO.println(if (Str.isBlank(" ")) "y" else "n")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_strip_prefix"));
        assert!(ir.contains("sz_string_strip_suffix"));
        assert!(ir.contains("sz_string_pad_left"));
        assert!(ir.contains("sz_string_pad_right"));
        assert!(ir.contains("sz_string_is_blank"));
        let needle = "call ptr @sz_string_strip_prefix(ptr ";
        let at = ir.find(needle).expect("stripPrefix");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after stripPrefix:\n{ir}"
        );
        let needle = "call ptr @sz_string_pad_left(ptr ";
        let at = ir.find(needle).expect("padLeft");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after padLeft:\n{ir}"
        );
    }

    #[test]
    fn emit_str_last_index_take_drop_list_reverse() {
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(Str.lastIndexOf("ababa", "ba")))
    _ <- IO.println(Str.take("abc", 2))
    _ <- IO.println(Str.drop("abc", 1))
    _ <- IO.println(Str.takeRight("abc", 2))
    _ <- IO.println(Str.dropRight("abc", 1))
    _ <- IO.println(List.join(List.reverse(["a", "b", "c"]), ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_string_last_index_of"));
        assert!(ir.contains("sz_string_take"));
        assert!(ir.contains("sz_string_drop"));
        assert!(ir.contains("sz_string_take_right"));
        assert!(ir.contains("sz_string_drop_right"));
        assert!(ir.contains("sz_list_reverse"));
        let needle = "call ptr @sz_string_take(ptr ";
        let at = ir.find(needle).expect("take");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Str.take:\n{ir}"
        );
        let needle = "call ptr @sz_list_reverse(ptr ";
        let at = ir.find(needle).expect("reverse");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after List.reverse:\n{ir}"
        );
    }

    #[test]
    fn emit_stream_map_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.map(Stream.emits(["a", "b"]), x => Str.concat(x, "!"))
    xs <- Stream.compileToList(s)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_map"));
        assert!(ir.contains("sz_smap_"));
    }

    #[test]
    fn emit_stream_takewhile_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.takeWhile(Stream.emits(["a", "b", "", "c"]), x => Str.len(x) > 0)
    xs <- Stream.compileToList(s)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_takewhile"));
        assert!(ir.contains("sz_pred_"));
    }

    #[test]
    fn emit_stream_find_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    s = Stream.find(Stream.emits(["", "a", "b"]), x => Str.len(x) > 0)
    xs <- Stream.compileToList(s)
    _ <- IO.println(List.join(xs, ","))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_find"));
        assert!(ir.contains("sz_pred_"));
    }

    #[test]
    fn emit_stream_exists_compile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    hit <- Stream.exists(Stream.emits(["", "a", "b"]), x => Str.len(x) > 0)
    _ <- IO.println(if (hit) "1" else "0")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_stream_exists"));
        assert!(ir.contains("sz_pred_"));
    }

    #[test]
    fn emit_net_serve_once() {
        let src = r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.pure(path))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_net_serve_once"));
        assert!(ir.contains("sz_rcont_"));
    }

    #[test]
    fn emit_net_serve_call() {
        let src = r#"@main def main: IO[Unit] =
  Net.serve(8080, path => IO.pure(path))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("call ptr @sz_net_serve("));
        assert!(ir.contains("sz_rcont_"));
        assert!(!ir.contains("call ptr @sz_net_serve_once("));
    }

    #[test]
    fn emit_sys_spawn_alive() {
        let src = r#"@main def main: IO[Unit] =
  Sys.spawn("true").flatMap(pid => Sys.alive(pid).flatMap(_ => IO.pure(())))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_sys_spawn"));
        assert!(ir.contains("sz_sys_alive"));
    }

    #[test]
    fn emit_sys_kill() {
        let src = r#"@main def main: IO[Unit] =
  Sys.spawn("true").flatMap(pid => Sys.kill(pid))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_sys_kill"));
    }

    #[test]
    fn emit_sys_read_write() {
        let src = r#"@main def main: IO[Unit] =
  Sys.read(4).flatMap(s => Sys.write(s))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_sys_read"));
        assert!(ir.contains("sz_sys_write"));
    }

    #[test]
    fn emit_require_residual_law_check_and_assert() {
        let src = r#"
law always: Bool = 1 == 1
@main def main: IO[Unit] =
  for {
    n = 1.require("nonNeg", n => n >= 0)
    _ <- IO.println(Str.fromInt(n)).require(always)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let p = crate::typ::resolve_field_access(p).expect("resolve require");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_law_check"),
            "expected Law.check in IR:\n{ir}"
        );
        assert!(
            ir.contains("sz_law_assert"),
            "expected Law.assert in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_ui_run_rebuild_lambda() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.text("x"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_ui_run_rebuild"));
        assert!(ir.contains("sz_uibuild_"));
        assert!(ir.contains("sz_ui_reload_rebuild"));
    }

    #[test]
    fn emit_session_packs_release_owned() {
        let cases: &[(&str, &str, &str)] = &[
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.text("a"))
"#,
                "call ptr @sz_ui_run_rebuild(",
                "Ui.run",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => View.text(s)))
  } yield ()
"#,
                "call ptr @sz_lang_view_each_map(",
                "View.each",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    s = Signal.map(n, x => "a")
    _ <- IO.println(Signal.getStr(s))
  } yield ()
"#,
                "call ptr @sz_lang_signal_map(",
                "Signal.map",
            ),
        ];
        for (src, needle, label) in cases {
            let p = crate::lower::lower_program(parse(src).unwrap());
            crate::typ::typecheck(&p).unwrap_or_else(|e| panic!("typecheck {label}: {e}"));
            let ir = emit_llvm(&p);
            let at = ir
                .find(needle)
                .unwrap_or_else(|| panic!("expected {needle} in IR for {label}:\n{ir}"));
            let pack = last_cl2_before(&ir, at);
            assert!(
                ir[at..].contains(&format!("call void @sz_release(ptr {pack})")),
                "expected last-use release of pack {pack} after {label}:\n{ir}"
            );
        }
    }

    #[test]
    fn emit_kit_packs_release_owned() {
        let cases: &[(&str, &str, &str)] = &[
            (
                r#"@main def main: IO[Unit] =
  Stream.drain(Stream.filter(Stream.emit("a"), x => true))
"#,
                "call ptr @sz_stream_filter(",
                "Stream.filter",
            ),
            (
                r#"@main def main: IO[Unit] =
  Stream.drain(Stream.map(Stream.emit("a"), x => x))
"#,
                "call ptr @sz_stream_map(",
                "Stream.map",
            ),
            (
                r#"@main def main: IO[Unit] =
  Stream.drain(Stream.evalMap(Stream.emit("a"), x => IO.pure(x)))
"#,
                "call ptr @sz_stream_evalmap(",
                "Stream.evalMap",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#,
                "call ptr @sz_lang_resource_make(",
                "Resource.make",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#,
                "call ptr @sz_lang_resource_use(",
                "Resource.use",
            ),
            (
                r#"@main def main: IO[Unit] =
  Net.serveOnce(8080, path => IO.pure(path))
"#,
                "call ptr @sz_net_serve_once(",
                "Net.serveOnce",
            ),
            (
                r#"@main def main: IO[Unit] =
  Net.serve(8080, path => IO.pure(path))
"#,
                "call ptr @sz_net_serve(",
                "Net.serve",
            ),
        ];
        for (src, needle, label) in cases {
            let p = crate::lower::lower_program(parse(src).unwrap());
            crate::typ::typecheck(&p).unwrap_or_else(|e| panic!("typecheck {label}: {e}"));
            let ir = emit_llvm(&p);
            let at = ir
                .find(needle)
                .unwrap_or_else(|| panic!("expected {needle} in IR for {label}:\n{ir}"));
            let pack = last_cl2_before(&ir, at);
            assert!(
                ir[at..].contains(&format!("call void @sz_release(ptr {pack})")),
                "expected last-use release of pack {pack} after {label}:\n{ir}"
            );
        }
    }

    #[test]
    fn emit_view_text_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.text("a"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_text(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_text");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.text:\n{ir}"
        );
    }

    #[test]
    fn emit_view_button_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.button("a", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_button(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_button");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.button:\n{ir}"
        );
    }

    #[test]
    fn emit_view_icon_button_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.iconButton("i", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_icon_button(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_icon_button");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.iconButton:\n{ir}"
        );
    }

    #[test]
    fn emit_view_fab_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fab("+", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_fab(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_fab");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.fab:\n{ir}"
        );
    }

    #[test]
    fn emit_view_outlined_button_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.outlinedButton("a", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_outlined_button(ptr ";
        let at = ir
            .find(needle)
            .expect("expected sz_lang_view_outlined_button");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.outlinedButton:\n{ir}"
        );
    }

    #[test]
    fn emit_view_text_button_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textButton("a", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_text_button(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_text_button");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.textButton:\n{ir}"
        );
    }

    #[test]
    fn emit_view_action_chip_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.actionChip("a", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_action_chip(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_action_chip");
        let name = ir[at + needle.len()..].split(',').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.actionChip:\n{ir}"
        );
    }

    #[test]
    fn emit_view_chip_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.chip(n, "a"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_chip(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_chip");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.chip:\n{ir}"
        );
    }

    #[test]
    fn emit_view_filter_chip_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.filterChip(n, "a"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_filter_chip(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_filter_chip");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.filterChip:\n{ir}"
        );
    }

    #[test]
    fn emit_view_input_chip_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.inputChip(n, "a"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_input_chip(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_input_chip");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.inputChip:\n{ir}"
        );
    }

    #[test]
    fn emit_view_choice_chip_releases_label() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.choiceChip(n, 0, "a"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_view_choice_chip(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_view_choice_chip");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(2)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after View.choiceChip:\n{ir}"
        );
    }

    #[test]
    fn emit_view_string_copies_release_owned() {
        let cases: &[(&str, &str, &[usize], &str)] = &[
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("a"))
"#,
                "call ptr @sz_lang_view_list_tile(ptr ",
                &[0],
                "View.listTile",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("a", View.button("b", _ => ())))
"#,
                "call ptr @sz_lang_view_list_tile(ptr ",
                &[0],
                "View.listTile trailing",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.checkboxListTile(n, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_checkbox_list_tile(ptr ",
                &[1],
                "View.checkboxListTile",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switchListTile(n, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_switch_list_tile(ptr ",
                &[1],
                "View.switchListTile",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radioListTile(n, 1, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_radio_list_tile(ptr ",
                &[2],
                "View.radioListTile",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.checkbox(n, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_checkbox(ptr ",
                &[1],
                "View.checkbox",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radio(n, 1, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_radio(ptr ",
                &[2],
                "View.radio",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switch(n, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_switch(ptr ",
                &[1],
                "View.switch",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.avatar("a"))
"#,
                "call ptr @sz_lang_view_avatar(ptr ",
                &[0],
                "View.avatar",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.segmented(n, "a", "b"))
  } yield ()
"#,
                "call ptr @sz_lang_view_segmented(ptr ",
                &[1, 2],
                "View.segmented",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.tooltip("a", View.avatar("b")))
"#,
                "call ptr @sz_lang_view_tooltip(ptr ",
                &[0],
                "View.tooltip",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.semantics("a", View.avatar("b")))
"#,
                "call ptr @sz_lang_view_semantics(ptr ",
                &[0],
                "View.semantics",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.mergeSemantics("a", View.avatar("b")))
"#,
                "call ptr @sz_lang_view_merge_semantics(ptr ",
                &[0],
                "View.mergeSemantics",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.inkWell("a", _ => (), View.avatar("b")))
"#,
                "call ptr @sz_lang_view_ink_well(ptr ",
                &[0],
                "View.inkWell",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.expansionTile(n, "a", View.text("b")))
  } yield ()
"#,
                "call ptr @sz_lang_view_expansion_tile(ptr ",
                &[1],
                "View.expansionTile",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    s = Signal.str("x")
    _ <- Ui.run(_ => View.textField(s, "a"))
  } yield ()
"#,
                "call ptr @sz_lang_view_text_field(ptr ",
                &[1],
                "View.textField",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.image(24, 24, 1, "a"))
"#,
                "call ptr @sz_lang_view_image(",
                &[3],
                "View.image",
            ),
        ];
        for (src, needle, idxs, label) in cases {
            let p = crate::lower::lower_program(parse(src).unwrap());
            crate::typ::typecheck(&p).unwrap_or_else(|e| panic!("typecheck {label}: {e}"));
            let ir = emit_llvm(&p);
            let at = ir
                .find(needle)
                .unwrap_or_else(|| panic!("expected {needle} in IR for {label}:\n{ir}"));
            let rest = &ir[at + needle.len()..];
            let args = rest.split(')').next().unwrap();
            let parts: Vec<&str> = args.split(',').collect();
            for &idx in *idxs {
                let name = parts
                    .get(idx)
                    .unwrap_or_else(|| {
                        panic!("missing arg {idx} after {needle} for {label}:\n{ir}")
                    })
                    .trim()
                    .trim_start_matches("ptr ")
                    .trim();
                assert!(
                    ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
                    "expected last-use release of string {name} after {label}:\n{ir}"
                );
            }
        }
    }

    #[test]
    fn emit_view_tap_packs_release_owned() {
        let cases: &[(&str, &str, &str)] = &[
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.button("a", _ => ()))
"#,
                "call ptr @sz_lang_view_button(",
                "View.button",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.iconButton("i", _ => ()))
"#,
                "call ptr @sz_lang_view_icon_button(",
                "View.iconButton",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fab("+", _ => ()))
"#,
                "call ptr @sz_lang_view_fab(",
                "View.fab",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.outlinedButton("a", _ => ()))
"#,
                "call ptr @sz_lang_view_outlined_button(",
                "View.outlinedButton",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textButton("a", _ => ()))
"#,
                "call ptr @sz_lang_view_text_button(",
                "View.textButton",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.actionChip("a", _ => ()))
"#,
                "call ptr @sz_lang_view_action_chip(",
                "View.actionChip",
            ),
            (
                r#"@main def main: IO[Unit] =
  Ui.run(_ => View.inkWell("a", _ => (), View.avatar("b")))
"#,
                "call ptr @sz_lang_view_ink_well(",
                "View.inkWell",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.button("a", _ => Signal.set(n, 1)))
  } yield ()
"#,
                "call ptr @sz_lang_view_button(",
                "View.button capture",
            ),
        ];
        for (src, needle, label) in cases {
            let p = crate::lower::lower_program(parse(src).unwrap());
            crate::typ::typecheck(&p).unwrap_or_else(|e| panic!("typecheck {label}: {e}"));
            let ir = emit_llvm(&p);
            let at = ir
                .find(needle)
                .unwrap_or_else(|| panic!("expected {needle} in IR for {label}:\n{ir}"));
            let pack = last_cl2_before(&ir, at);
            assert!(
                ir[at..].contains(&format!("call void @sz_release(ptr {pack})")),
                "expected last-use release of tap pack {pack} after {label}:\n{ir}"
            );
        }
    }

    #[test]
    fn emit_view_tap_capture_env_released_after_pack() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.button("a", _ => Signal.set(n, 1)))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "_cap_1 = call ptr @sz_list_cons(";
        let cap_at = ir.find(needle).expect("expected capture list cons");
        let start = ir[..cap_at]
            .rfind('%')
            .expect("expected % before capture SSA");
        let name = &ir[start..cap_at + "_cap_1".len()];
        assert!(
            ir[cap_at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected construction release of tap capture {name}:\n{ir}"
        );
    }

    #[test]
    fn emit_session_capture_env_released_after_pack() {
        let cases: &[(&str, &str)] = &[
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.button("a", _ => Signal.set(n, 1)))
  } yield ()
"#,
                "Ui.run",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    items = Signal.list(["a"])
    _ <- Ui.run(_ => View.each(items, s => View.button(s, _ => Signal.set(n, 1))))
  } yield ()
"#,
                "View.each",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    tag = "k"
    n = Signal.int(0)
                s = Signal.map(n, x => tag)
    _ <- IO.println(Signal.getStr(s))
  } yield ()
"#,
                "Signal.map",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    tag = "k"
    res = Resource.make(IO.pure("tok"), t => IO.println(tag))
    _ <- Resource.use(res, t => IO.println(t))
  } yield ()
"#,
                "Resource.make",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    tag = "k"
    res = Resource.make(IO.pure("tok"), t => IO.println(t))
    _ <- Resource.use(res, t => IO.println(tag))
  } yield ()
"#,
                "Resource.use",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    tag = "k"
                _ <- Net.serveOnce(8080, path => IO.pure(tag))
  } yield ()
"#,
                "Net.serveOnce",
            ),
            (
                r#"@main def main: IO[Unit] =
  for {
    tag = "k"
    _ <- Stream.drain(Stream.filter(Stream.emit("a"), x => Str.len(tag) > 0))
  } yield ()
"#,
                "Stream.filter",
            ),
        ];
        for (src, label) in cases {
            let p = crate::lower::lower_program(parse(src).unwrap());
            crate::typ::typecheck(&p).unwrap_or_else(|e| panic!("typecheck {label}: {e}"));
            let ir = emit_llvm(&p);
            let needle = "_cap_1 = call ptr @sz_list_cons(";
            let cap_at = ir
                .find(needle)
                .unwrap_or_else(|| panic!("expected capture list cons for {label}:\n{ir}"));
            let start = ir[..cap_at]
                .rfind('%')
                .expect("expected % before capture SSA");
            let name = &ir[start..cap_at + "_cap_1".len()];
            assert!(
                ir[cap_at..].contains(&format!("call void @sz_release(ptr {name})")),
                "expected construction release of {label} capture {name}:\n{ir}"
            );
        }
    }

    #[test]
    fn emit_view_wrappers() {
        let cases = [
            ("View.stretch(View.text(\"x\"))", "sz_lang_view_stretch"),
            (
                "View.wrap(View.text(\"a\"), View.text(\"b\"))",
                "sz_lang_view_wrap",
            ),
            (
                "View.grid(2, View.text(\"a\"), View.text(\"b\"))",
                "sz_lang_view_grid",
            ),
            ("View.scrollH(View.text(\"x\"))", "sz_lang_view_scroll_h"),
            (
                "View.maxSize(40, 30, View.text(\"x\"))",
                "sz_lang_view_max_size",
            ),
            ("View.clip(View.text(\"x\"))", "sz_lang_view_clip"),
            ("View.card(View.text(\"x\"))", "sz_lang_view_card"),
            ("View.divider()", "sz_lang_view_divider"),
            ("View.verticalDivider()", "sz_lang_view_vertical_divider"),
            ("View.opacity(50, View.text(\"x\"))", "sz_lang_view_opacity"),
            (
                "View.maxLines(2, View.text(\"x\"))",
                "sz_lang_view_max_lines",
            ),
            ("View.ellipsis(View.text(\"x\"))", "sz_lang_view_ellipsis"),
            (
                "View.textColor(Color.rgb(255, 0, 0), View.text(\"x\"))",
                "sz_lang_view_text_color",
            ),
            ("View.gap(0, View.text(\"x\"))", "sz_lang_view_gap"),
            (
                "View.fontSize(16, View.text(\"x\"))",
                "sz_lang_view_font_size",
            ),
            (
                "View.border(2, Color.rgb(255, 0, 0), View.text(\"x\"))",
                "sz_lang_view_border",
            ),
            ("View.radius(8, View.text(\"x\"))", "sz_lang_view_radius"),
            (
                "View.ignorePointer(View.text(\"x\"))",
                "sz_lang_view_ignore_pointer",
            ),
            (
                "View.absorbPointer(View.text(\"x\"))",
                "sz_lang_view_absorb_pointer",
            ),
            (
                "View.excludeSemantics(View.text(\"x\"))",
                "sz_lang_view_exclude_semantics",
            ),
            (
                "View.unconstrainedBox(View.text(\"x\"))",
                "sz_lang_view_unconstrained_box",
            ),
        ];
        for (call, sym) in cases {
            let src = format!("@main def main: IO[Unit] =\n  Ui.run(_ => {call})\n");
            let p = crate::lower::lower_program(parse(&src).unwrap());
            crate::typ::typecheck(&p).expect("typecheck");
            let ir = emit_llvm(&p);
            assert!(ir.contains(sym), "expected {sym} in IR for {call}:\n{ir}");
        }
    }

    #[test]
    fn emit_view_checkbox() {
        let src = r#"@main def main: IO[Unit] =
  for {
    c = Signal.int(0)
    _ <- Ui.run(_ => View.checkbox(c, "Done"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_checkbox"),
            "expected sz_lang_view_checkbox in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_radio() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radio(n, 1, "On"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_radio"),
            "expected sz_lang_view_radio in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_slider() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.slider(n))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_slider"),
            "expected sz_lang_view_slider in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.progress(n))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_progress"),
            "expected sz_lang_view_progress in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_circular_progress() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(40)
    _ <- Ui.run(_ => View.circularProgress(n))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_circular_progress"),
            "expected sz_lang_view_circular_progress in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_avatar() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.avatar("S"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_avatar"),
            "expected sz_lang_view_avatar in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_switch() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switch(n, "On"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_switch"),
            "expected sz_lang_view_switch in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.chip(n, "Pin"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_chip"),
            "expected sz_lang_view_chip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_filter_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.filterChip(n, "Tag"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_filter_chip"),
            "expected sz_lang_view_filter_chip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_choice_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.choiceChip(n, 0, "Day"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_choice_chip"),
            "expected sz_lang_view_choice_chip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_action_chip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.actionChip("Go", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_action_chip"),
            "expected sz_lang_view_action_chip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_input_chip() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.inputChip(n, "In"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_input_chip"),
            "expected sz_lang_view_input_chip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("milk"))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_list_tile"),
            "expected sz_lang_view_list_tile in IR:\n{ir}"
        );
        assert!(
            ir.contains("ptr null"),
            "one-arg View.listTile must pass null trailing:\n{ir}"
        );
    }

    #[test]
    fn emit_view_list_tile_trailing() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.listTile("milk", View.button("Del", _ => ())))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_list_tile"),
            "expected sz_lang_view_list_tile in IR:\n{ir}"
        );
        assert!(
            ir.contains("sz_lang_view_button"),
            "two-arg View.listTile should emit trailing:\n{ir}"
        );
    }

    #[test]
    fn emit_view_checkbox_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.checkboxListTile(n, "Star"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_checkbox_list_tile"),
            "expected sz_lang_view_checkbox_list_tile in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_switch_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.switchListTile(n, "Quiet"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_switch_list_tile"),
            "expected sz_lang_view_switch_list_tile in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_radio_list_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.radioListTile(n, 1, "Night"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_radio_list_tile"),
            "expected sz_lang_view_radio_list_tile in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_segmented() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.segmented(n, "List", "Grid"))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_segmented"),
            "expected sz_lang_view_segmented in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_fab() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fab("+", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_fab"),
            "expected sz_lang_view_fab in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_outlined_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.outlinedButton("Edit", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_outlined_button"),
            "expected sz_lang_view_outlined_button in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_text_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textButton("Open", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_text_button"),
            "expected sz_lang_view_text_button in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_tooltip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.tooltip("Sean", View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_tooltip"),
            "expected sz_lang_view_tooltip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_placeholder() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.placeholder(View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_placeholder"),
            "expected sz_lang_view_placeholder in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.semantics("mark", View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_semantics"),
            "expected sz_lang_view_semantics in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_merge_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.mergeSemantics("logo", View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_merge_semantics"),
            "expected sz_lang_view_merge_semantics in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_ink_well() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.inkWell("face", _ => (), View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_ink_well"),
            "expected sz_lang_view_ink_well in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_visibility() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.visibility(n, View.avatar("S")))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_visibility"),
            "expected sz_lang_view_visibility in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_offstage() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(1)
    _ <- Ui.run(_ => View.offstage(n, View.avatar("S")))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_offstage"),
            "expected sz_lang_view_offstage in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_unconstrained_box() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.unconstrainedBox(View.avatar("S")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_unconstrained_box"),
            "expected sz_lang_view_unconstrained_box in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_badge() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(3)
    _ <- Ui.run(_ => View.badge(n, View.text("x")))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_badge"),
            "expected sz_lang_view_badge in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_card() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.card(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_card"),
            "expected sz_lang_view_card in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.divider())
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_divider"),
            "expected sz_lang_view_divider in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_vertical_divider() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.verticalDivider())
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_vertical_divider"),
            "expected sz_lang_view_vertical_divider in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_expansion_tile() {
        let src = r#"@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    _ <- Ui.run(_ => View.expansionTile(n, "More", View.text("x")))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_expansion_tile"),
            "expected sz_lang_view_expansion_tile in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_icon_button() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.iconButton("i", _ => ()))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_icon_button"),
            "expected sz_lang_view_icon_button in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_signal_list_releases_input() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    items = Signal.list(["a"])
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_signal_list(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_signal_list");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after Signal.list:\n{ir}"
        );
    }

    #[test]
    fn emit_signal_str_releases_input() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    s = Signal.str("a")
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_signal_str(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_signal_str");
        let name = ir[at + needle.len()..].split(')').next().unwrap().trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Signal.str:\n{ir}"
        );
    }

    #[test]
    fn emit_signal_set_str_releases_input() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    s = Signal.str("")
    _ = Signal.setStr(s, "a")
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_signal_str_set(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_signal_str_set");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of string {name} after Signal.setStr:\n{ir}"
        );
    }

    #[test]
    fn emit_signal_set_list_releases_input() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    items = Signal.list([])
    _ = Signal.setList(items, ["a"])
    _ <- IO.println("ok")
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = "call ptr @sz_lang_signal_list_set(ptr ";
        let at = ir.find(needle).expect("expected sz_lang_signal_list_set");
        let rest = &ir[at + needle.len()..];
        let args = rest.split(')').next().unwrap();
        let name = args
            .split(',')
            .nth(1)
            .unwrap()
            .trim()
            .trim_start_matches("ptr ")
            .trim();
        assert!(
            ir[at..].contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of list {name} after Signal.setList:\n{ir}"
        );
    }

    #[test]
    fn emit_view_each() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("call ptr @sz_lang_view_each("),
            "expected sz_lang_view_each call in IR:\n{ir}"
        );
        assert!(
            !ir.contains("call ptr @sz_lang_view_each_map("),
            "one-arg View.each must not emit mapper:\n{ir}"
        );
    }

    #[test]
    fn emit_view_each_mapper() {
        let src = r#"@main def main: IO[Unit] =
  for {
    items = Signal.list(["milk"])
    _ <- Ui.run(_ => View.each(items, s => View.text(s)))
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_each_map"),
            "expected sz_lang_view_each_map in IR:\n{ir}"
        );
        assert!(
            ir.contains("sz_each_"),
            "expected sz_each_ mapper in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_color_rgba() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.background(Color.rgba(1, 2, 3, 4), View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_color_rgba"),
            "expected Color.rgba in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_enum_match() {
        let src = r#"
enum Color { case Red, case Blue }
@main def main: IO[Unit] =
  for {
    c = Color.Red
  } yield c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_adt_new"));
        assert!(ir.contains("sz_adt_tag"));
        assert!(ir.contains("icmp eq i32"));
    }

    #[test]
    fn emit_adt_temp_release_after_match() {
        let src = r#"
enum Color { case Red, case Blue }
@main def main: IO[Unit] =
  IO.println(Str.fromInt(Color.Red match {
    case Color.Red => 1
    case Color.Blue => 0
  }))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        let needle = " = call ptr @sz_adt_new";
        let at = ir.find(needle).expect("expected sz_adt_new");
        let line_start = ir[..at].rfind('\n').map(|i| i + 1).unwrap_or(0);
        let name = ir[line_start..at].trim();
        assert!(
            ir.contains(&format!("call void @sz_release(ptr {name})")),
            "expected last-use release of ADT temp {name}:\n{ir}"
        );
    }

    #[test]
    fn emit_adt_retain_on_borrowed_return() {
        let src = r#"
enum Color { case Red, case Blue }
def id(c: Color): Color = c
@main def main: IO[Unit] =
  IO.println(Str.fromInt(id(Color.Red) match {
    case Color.Red => 1
    case Color.Blue => 0
  }))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_retain"));
    }

    #[test]
    fn emit_payload_adt_int() {
        let src = r#"
enum Opt:
  case Some(x: Int)
  case None
@main def main: IO[Unit] =
  Opt.Some(7) match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_box_i64"));
        assert!(ir.contains("sz_adt_new"));
        assert!(ir.contains("sz_adt_payload"));
        assert!(ir.contains("sz_unbox_i64"));
        // Some is tag 0, None tag 1 — payload non-null for Some.
        assert!(ir.contains("call ptr @sz_adt_new(i32 0, ptr %"));
        assert!(ir.contains("call ptr @sz_adt_new(i32 1, ptr null)") || !ir.contains("Opt.None("));
    }

    #[test]
    fn emit_nested_adt_pattern() {
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
    case Wrap.Box(Color.Blue) => IO.println("blue")
    case Wrap.Empty => IO.println("empty")
  }
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_adt_tag"));
        assert!(ir.contains("sz_adt_payload"));
        let tag_calls = ir.matches("call i32 @sz_adt_tag").count();
        assert!(
            tag_calls >= 2,
            "expected nested tag tests, got {tag_calls}:\n{ir}"
        );
    }

    #[test]
    fn emit_payload_adt_string() {
        let src = r#"
enum Msg:
  case Hello(s: String)
  case Empty
@main def main: IO[Unit] =
  Msg.Hello("hi") match {
    case Msg.Hello(t) => IO.println(t)
    case Msg.Empty => IO.println("empty")
  }
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_adt_payload"));
        assert!(ir.contains("sz_adt_new"));
        // String payload is passed through (no box_i64 for the ctor arg).
        assert!(ir.contains("call ptr @sz_adt_new(i32 0, ptr %"));
    }

    #[test]
    fn emit_multi_field_payload_adt() {
        let src = r#"
enum Pair:
  case Pair(a: Int, b: String)
@main def main: IO[Unit] =
  Pair.Pair(7, "hi") match {
    case Pair.Pair(x, y) => IO.println(Str.concat(Str.fromInt(x), y))
  }
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_nil"));
        assert!(ir.contains("sz_list_cons"));
        assert!(ir.contains("sz_box_i64"));
        assert!(ir.contains("sz_list_at"));
        assert!(ir.contains("sz_unbox_i64"));
        assert!(ir.contains("sz_adt_new"));
        assert!(ir.contains("sz_adt_payload"));
    }

    #[test]
    fn emit_def_and_if() {
        let src = r#"
def add1(n: Int): Int = n + 1
@main def main: IO[Unit] =
  if (add1(0) == 1) IO.println("ok") else IO.println("bad")
"#;
        let p = parse(src).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("@sz_user_add1"));
        assert!(ir.contains("icmp"));
        assert!(ir.contains("sz_runtime_main_args"));
    }

    #[test]
    fn emit_namespaced_module_defs() {
        let p = crate::parser::parse_sources(&[
            ("A.scuzz".into(), "def tag(): String = \"a\"\n".into()),
            ("B.scuzz".into(), "def tag(): String = \"b\"\n".into()),
            (
                "Main.scuzz".into(),
                "@main def main: IO[Unit] = IO.println(Str.concat(A.tag(), B.tag()))\n".into(),
            ),
        ])
        .unwrap();
        let p = crate::lower::lower_program(p);
        crate::typ::typecheck(&p).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("@sz_user_A_tag"));
        assert!(ir.contains("@sz_user_B_tag"));
        assert!(ir.contains("call ptr @sz_user_A_tag()"));
        assert!(ir.contains("call ptr @sz_user_B_tag()"));
    }

    #[test]
    fn emit_list_literals() {
        let src = r#"
@main def main: IO[Unit] =
  for {
    empty = []
    pair = ["a", "b"]
  } yield IO.println("ok")
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        let ir = emit_llvm(&p);
        assert!(ir.contains("sz_list_nil"));
        assert!(ir.contains("sz_list_cons"));
    }

    fn gen_ir(src: &str) -> String {
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let p = crate::typ::elaborate_generics(p).expect("elaborate");
        let p = crate::typ::monomorphize(p).expect("monomorphize");
        emit_llvm(&p)
    }

    #[test]
    fn emit_generic_enum_boxes_int_payload() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
def noneInt(): Opt[Int] = Opt.None
@main def main: IO[Unit] =
  Opt.Some(7) match {
    case Opt.Some(n) => IO.println(Str.fromInt(n))
    case Opt.None => IO.println("none")
  }
"#;
        let ir = gen_ir(src);
        assert!(
            ir.contains("call ptr @sz_box_i64"),
            "Int payload must be boxed"
        );
        assert!(ir.contains("call ptr @sz_adt_new(i32 0, ptr %"));
        assert!(
            ir.contains("call i64 @sz_unbox_i64"),
            "Int bind must be unboxed"
        );
        assert!(ir.contains("call ptr @sz_adt_new(i32 1, ptr null)"));
    }

    #[test]
    fn emit_generic_enum_string_payload_not_boxed() {
        let src = r#"
enum Opt[T]:
  case Some(x: T)
  case None
@main def main: IO[Unit] =
  Opt.Some("s") match {
    case Opt.Some(x) => IO.println(x)
    case Opt.None => IO.println("none")
  }
"#;
        let ir = gen_ir(src);
        assert!(
            !ir.contains("call ptr @sz_box_i64"),
            "String payload must pass through"
        );
        assert!(ir.contains("sz_adt_new"));
        assert!(ir.contains("sz_adt_payload"));
    }

    #[test]
    fn emit_multi_param_either_constructs() {
        let src = r#"
enum Either[L, R]:
  case Left(x: L)
  case Right(y: R)
def describe(e: Either[Int, String]): String = e match {
  case Either.Left(n) => Str.fromInt(n)
  case Either.Right(s) => s
}
@main def main: IO[Unit] = IO.println(describe(Either.Right("r")))
"#;
        let ir = gen_ir(src);
        assert!(ir.contains("sz_adt_new"));
        assert!(ir.contains("icmp eq i32"));
        // Right is tag 1, String payload passes through unboxed.
        assert!(ir.contains("call ptr @sz_adt_new(i32 1, ptr %"));
    }

    #[test]
    fn emit_generic_record_mixed_payload_pack() {
        let src = r#"
record Pair[A, B](a: A, b: B)
@main def main: IO[Unit] =
  Pair(7, "x") match {
    case Pair.Pair(n, s) => IO.println(Str.concat(Str.fromInt(n), s))
  }
"#;
        let ir = gen_ir(src);
        assert!(
            ir.contains("call ptr @sz_box_i64"),
            "Int field must be boxed in the pack"
        );
        assert!(
            ir.contains("sz_list_cons"),
            "multi-field payload packs as list"
        );
        assert!(
            ir.contains("call i64 @sz_unbox_i64"),
            "Int field bind must be unboxed"
        );
    }
}

use crate::ast::{BinOp, EnumDef, Expr, ExprKind, FunDef, MatchArm, Pattern, Program, Type};
use crate::resolve::{user_symbol, FunIndex};
use std::collections::HashMap;
use std::fmt::Write as _;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Kind {
    Int,
    Ptr,
    Io,
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
    writeln!(out, "declare ptr @sz_string_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_concat(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_len(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_slice(ptr, i64, i64)").unwrap();
    writeln!(out, "declare i32 @sz_string_eq(ptr, ptr)").unwrap();
    writeln!(out, "declare i64 @sz_string_char_at(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_string_from_int(i64)").unwrap();
    writeln!(out, "declare i64 @sz_string_index_of(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_string_lines(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_println(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_pure(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_delay(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_flatmap(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_fail_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_sleep_ms(i64)").unwrap();
    writeln!(out, "declare ptr @sz_io_handle_error_with(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_io_attempt(ptr)").unwrap();
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
    writeln!(out, "declare ptr @sz_list_cons(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_head(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_tail(ptr)").unwrap();
    writeln!(out, "declare i64 @sz_list_len(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_at(ptr, i64)").unwrap();
    writeln!(out, "declare ptr @sz_list_reverse(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_join(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_list_append(ptr, ptr)").unwrap();
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
    writeln!(out, "declare i64 @sz_theme_accent()").unwrap();
    writeln!(out, "declare i64 @sz_theme_primary()").unwrap();
    writeln!(out, "declare i64 @sz_theme_muted()").unwrap();
    writeln!(out, "declare i64 @sz_theme_foreground()").unwrap();
    writeln!(out, "declare i64 @sz_color_rgb(i64, i64, i64)").unwrap();
    writeln!(out, "declare i64 @sz_color_rgba(i64, i64, i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_column()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_row()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_stack()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_each(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_scroll(ptr)").unwrap();
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
    writeln!(out, "declare ptr @sz_ui_run_view(ptr)").unwrap();
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
    let mut locals: HashMap<String, (String, Kind)> = HashMap::new();
    let body_expr = emit_expr(&program.main.body, &mut ctx, &mut locals, "build");
    let reload_name = ctx.reload_fn.clone();

    out.push_str(&conts);
    out.push_str(&fundef_ir);

    writeln!(out, "define i32 @main(i32 %argc, ptr %argv) {{").unwrap();
    writeln!(out, "entry:").unwrap();
    emit_driver_registers(&mut out, program, &strs);
    out.push_str(&body_expr.code);
    let io_val = ensure_io(&mut out, body_expr.kind, &body_expr.value, "wrapped");
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
}

fn io_emitted(code: String, value: String, payload: Kind) -> Emitted {
    Emitted {
        code,
        value,
        kind: Kind::Io,
        payload,
    }
}

fn val_emitted(code: String, value: String, kind: Kind) -> Emitted {
    Emitted {
        code,
        value,
        kind,
        payload: Kind::Ptr,
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
        _ => "ptr",
    }
}

fn kind_of_type(ty: &Type) -> Kind {
    match ty {
        Type::Int | Type::Bool => Kind::Int,
        Type::Io(_) => Kind::Io,
        _ => Kind::Ptr,
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
        | ExprKind::HandleErrorWith { inner, body }
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
        ExprKind::Unit | ExprKind::Var(_) | ExprKind::IntLit(_) => {}
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

    let mut locals: HashMap<String, (String, Kind)> = HashMap::new();
    for p in &def.params {
        locals.insert(
            p.name.clone(),
            (format!("%{}", p.name), kind_of_type(&p.ty)),
        );
    }

    let body = emit_expr(&def.body, ctx, &mut locals, "body");
    out.push_str(&body.code);

    let ret_kind = kind_of_type(&def.ret);
    match ret_kind {
        Kind::Io => {
            let io = ensure_io(out, body.kind, &body.value, "ret_wrap");
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
        Kind::Ptr => {
            let v = if body.kind == Kind::Ptr || body.kind == Kind::Io {
                body.value
            } else if body.kind == Kind::Int {
                writeln!(out, "  %ret_box = call ptr @sz_box_i64(i64 {})", body.value).unwrap();
                "%ret_box".into()
            } else {
                "null".into()
            };
            writeln!(out, "  ret ptr {v}").unwrap();
        }
    }
    writeln!(out, "}}").unwrap();
    writeln!(out).unwrap();
}

fn emit_driver_registers(out: &mut String, program: &Program, strs: &[String]) {
    for (i, d) in program.defs.iter().filter(|d| d.is_driver).enumerate() {
        let idx = strs
            .iter()
            .position(|s| s == &d.name)
            .expect("driver name interned");
        let len = d.name.len() + 1;
        let nargs = d.params.len() as i64;
        let kind = match d.params.first().map(|p| &p.ty) {
            Some(Type::String) => 1,
            Some(Type::Bool) => 2,
            _ => 0,
        };
        let sym = user_symbol(&d.module, &d.name);
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
}

fn ensure_io(code: &mut String, kind: Kind, value: &str, tmp: &str) -> String {
    if kind == Kind::Io {
        return value.to_string();
    }
    let ptr = if kind == Kind::Int {
        writeln!(code, "  %{tmp}_box = call ptr @sz_box_i64(i64 {value})").unwrap();
        format!("%{tmp}_box")
    } else {
        value.to_string()
    };
    writeln!(code, "  %{tmp} = call ptr @sz_io_pure(ptr {ptr})").unwrap();
    format!("%{tmp}")
}

/// Stable capture order for packing/unpacking flatMap/handleErrorWith env lists.
fn capture_name_order(locals: &HashMap<String, (String, Kind)>) -> Vec<String> {
    let mut names: Vec<String> = locals.keys().cloned().collect();
    names.sort();
    names
}

/// Pack enclosing locals into a `SzList` (head = first capture name). Ints are boxed.
fn pack_env(
    code: &mut String,
    locals: &HashMap<String, (String, Kind)>,
    names: &[String],
    prefix: &str,
) -> String {
    if names.is_empty() {
        return "null".into();
    }
    writeln!(code, "  %{prefix}_0 = call ptr @sz_list_nil()").unwrap();
    let mut cur = format!("%{prefix}_0");
    for (i, name) in names.iter().enumerate().rev() {
        let (val, kind) = locals.get(name).expect("capture name");
        let ptr = if *kind == Kind::Int {
            writeln!(code, "  %{prefix}_b{i} = call ptr @sz_box_i64(i64 {val})").unwrap();
            format!("%{prefix}_b{i}")
        } else {
            val.clone()
        };
        writeln!(
            code,
            "  %{prefix}_{} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})",
            i + 1
        )
        .unwrap();
        cur = format!("%{prefix}_{}", i + 1);
    }
    cur
}

fn emit_list_lit(
    elems: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let mut code = String::new();
    writeln!(code, "  %{prefix}_0 = call ptr @sz_list_nil()").unwrap();
    let mut cur = format!("%{prefix}_0");
    for (i, elem) in elems.iter().enumerate().rev() {
        let ee = emit_expr(elem, ctx, locals, &format!("{prefix}_e{i}"));
        code.push_str(&ee.code);
        let ptr = if ee.kind == Kind::Int {
            writeln!(
                code,
                "  %{prefix}_b{i} = call ptr @sz_box_i64(i64 {})",
                ee.value
            )
            .unwrap();
            format!("%{prefix}_b{i}")
        } else {
            ee.value.clone()
        };
        let next = format!("%{prefix}_{}", i + 1);
        writeln!(
            code,
            "  {next} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})"
        )
        .unwrap();
        cur = next;
    }
    val_emitted(code, cur, Kind::Ptr)
}

/// Unpack `%env` list into `body_locals` (mirrors [`pack_env`] order).
fn unpack_env_preamble(
    pre: &mut String,
    body_locals: &mut HashMap<String, (String, Kind)>,
    outer: &HashMap<String, (String, Kind)>,
    names: &[String],
    prefix: &str,
) {
    if names.is_empty() {
        return;
    }
    let mut cur = "%env".to_string();
    for (i, name) in names.iter().enumerate() {
        let kind = outer.get(name).map(|(_, k)| *k).unwrap_or(Kind::Ptr);
        writeln!(pre, "  %{prefix}_h{i} = call ptr @sz_list_head(ptr {cur})").unwrap();
        writeln!(pre, "  %{prefix}_t{i} = call ptr @sz_list_tail(ptr {cur})").unwrap();
        match kind {
            Kind::Int => {
                writeln!(
                    pre,
                    "  %{name} = call i64 @sz_unbox_i64(ptr %{prefix}_h{i})"
                )
                .unwrap();
                body_locals.insert(name.clone(), (format!("%{name}"), Kind::Int));
            }
            Kind::Ptr | Kind::Io => {
                body_locals.insert(name.clone(), (format!("%{prefix}_h{i}"), kind));
            }
        }
        cur = format!("%{prefix}_t{i}");
    }
}

fn emit_cstr_from_string(
    code: &mut String,
    strs: &[String],
    expr: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> String {
    if let ExprKind::StrLit(s) = &expr.kind {
        let idx = str_index(strs, s);
        let len = s.len() + 1;
        writeln!(
            code,
            "  %{prefix}_cstr = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
        )
        .unwrap();
        return format!("%{prefix}_cstr");
    }
    let se = emit_expr(expr, ctx, locals, &format!("{prefix}_s"));
    code.push_str(&se.code);
    writeln!(
        code,
        "  %{prefix}_cstr = call ptr @sz_string_cstr(ptr {})",
        se.value
    )
    .unwrap();
    format!("%{prefix}_cstr")
}

fn emit_sz_string(
    code: &mut String,
    strs: &[String],
    expr: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> String {
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
        return format!("%{prefix}_ss");
    }
    let se = emit_expr(expr, ctx, locals, &format!("{prefix}_e"));
    code.push_str(&se.code);
    se.value
}

fn emit_match(
    scrutinee: &Expr,
    arms: &[MatchArm],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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

    let mut phi_parts: Vec<(String, String)> = Vec::new();
    let mut result_kind = Kind::Io;
    let mut result_payload = Kind::Ptr;

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
        writeln!(code, "{ok_l}:").unwrap();
        let ae = emit_expr(&arm.body, ctx, locals, &format!("{prefix}_a{id}_{i}"));
        for b in &bound_names {
            locals.remove(b);
        }
        code.push_str(&ae.code);
        if phi_parts.is_empty() {
            result_kind = ae.kind;
            result_payload = ae.payload;
        }
        writeln!(code, "  br label %{join_l}").unwrap();
        writeln!(code, "{join_l}:").unwrap();
        writeln!(code, "  br label %{merge}").unwrap();
        phi_parts.push((ae.value, join_l));
    }

    writeln!(code, "{default_label}:").unwrap();
    let dflt = match result_kind {
        Kind::Int => {
            writeln!(code, "  %{prefix}_dflt = add i64 0, 0").unwrap();
            format!("%{prefix}_dflt")
        }
        Kind::Ptr => "null".into(),
        Kind::Io => {
            writeln!(code, "  %{prefix}_dflt = call ptr @sz_io_pure(ptr null)").unwrap();
            format!("%{prefix}_dflt")
        }
    };
    writeln!(code, "  br label %{merge}").unwrap();
    phi_parts.push((dflt, default_label));

    let ty = match result_kind {
        Kind::Int => "i64",
        Kind::Ptr | Kind::Io => "ptr",
    };
    writeln!(code, "{merge}:").unwrap();
    write!(code, "  %{prefix}_phi = phi {ty}").unwrap();
    for (i, (val, lab)) in phi_parts.iter().enumerate() {
        if i > 0 {
            write!(code, ",").unwrap();
        }
        write!(code, " [ {val}, %{lab} ]").unwrap();
    }
    writeln!(code).unwrap();
    Emitted {
        code,
        value: format!("%{prefix}_phi"),
        kind: result_kind,
        payload: result_payload,
    }
}

fn emit_pat(
    pat: &Pattern,
    value: &str,
    kind: Kind,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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
            locals.insert(name.clone(), (value.to_string(), kind));
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
            Some(Type::Int) | Some(Type::Bool) => {
                writeln!(
                    code,
                    "  %{prefix}_b0 = call i64 @sz_unbox_i64(ptr %{prefix}_pl)"
                )
                .unwrap();
                out.push((format!("%{prefix}_b0"), Kind::Int));
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
                Some(Type::Int) | Some(Type::Bool) => {
                    let bn = format!("{prefix}_b{fi}");
                    writeln!(code, "  %{bn} = call i64 @sz_unbox_i64(ptr %{cell})").unwrap();
                    out.push((format!("%{bn}"), Kind::Int));
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
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    match &expr.kind {
        ExprKind::Unit => val_emitted(String::new(), "null".into(), Kind::Ptr),
        ExprKind::IntLit(n) => val_emitted(String::new(), format!("{n}"), Kind::Int),
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
            val_emitted(code, format!("%{prefix}_s"), Kind::Ptr)
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
            let cstr = emit_cstr_from_string(&mut code, ctx.strs, msg, ctx, locals, prefix);
            writeln!(
                code,
                "  %{prefix}_io = call ptr @sz_io_fail_cstr(ptr {cstr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        ExprKind::IoPrintln(arg) => {
            let mut code = String::new();
            let s = emit_sz_string(&mut code, ctx.strs, arg, ctx, locals, prefix);
            writeln!(code, "  %{prefix}_io = call ptr @sz_io_println(ptr {s})").unwrap();
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        ExprKind::IoPure(inner) => {
            let ie = emit_expr(inner, ctx, locals, &format!("{prefix}_p"));
            let mut code = ie.code;
            let ptr = if ie.kind == Kind::Int {
                writeln!(
                    code,
                    "  %{prefix}_box = call ptr @sz_box_i64(i64 {})",
                    ie.value
                )
                .unwrap();
                format!("%{prefix}_box")
            } else if ie.kind == Kind::Io {
                ie.value
            } else {
                ie.value
            };
            writeln!(code, "  %{prefix}_io = call ptr @sz_io_pure(ptr {ptr})").unwrap();
            let payload = if ie.kind == Kind::Io {
                ie.payload
            } else {
                ie.kind
            };
            io_emitted(code, format!("%{prefix}_io"), payload)
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
                        Some(Type::Int) => {
                            let v = if ae.kind == Kind::Int {
                                ae.value
                            } else {
                                writeln!(code, "  %{prefix}_ap0 = add i64 0, 0").unwrap();
                                format!("%{prefix}_ap0")
                            };
                            writeln!(code, "  %{prefix}_box = call ptr @sz_box_i64(i64 {v})")
                                .unwrap();
                            format!("%{prefix}_box")
                        }
                        _ => ae.value,
                    }
                } else {
                    // N>=2: pack field values into a List (Ints boxed).
                    writeln!(code, "  %{prefix}_pl0 = call ptr @sz_list_nil()").unwrap();
                    let mut cur = format!("%{prefix}_pl0");
                    for (i, arg) in args.iter().enumerate().rev() {
                        let ae = emit_expr(arg, ctx, locals, &format!("{prefix}_ap{i}"));
                        code.push_str(&ae.code);
                        let ptr = match field_tys.get(i) {
                            Some(Type::Int) => {
                                let v = if ae.kind == Kind::Int {
                                    ae.value.clone()
                                } else {
                                    writeln!(code, "  %{prefix}_ap{i}i = add i64 0, 0").unwrap();
                                    format!("%{prefix}_ap{i}i")
                                };
                                writeln!(code, "  %{prefix}_bx{i} = call ptr @sz_box_i64(i64 {v})")
                                    .unwrap();
                                format!("%{prefix}_bx{i}")
                            }
                            _ => ae.value.clone(),
                        };
                        let next = format!("%{prefix}_pl{}", args.len() - i);
                        writeln!(
                            code,
                            "  {next} = call ptr @sz_list_cons(ptr {ptr}, ptr {cur})"
                        )
                        .unwrap();
                        cur = next;
                    }
                    cur
                }
            };
            writeln!(
                code,
                "  %{prefix}_adt = call ptr @sz_adt_new(i32 {tag}, ptr {payload_ptr})"
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_adt"), Kind::Ptr)
        }
        ExprKind::Var(name) => {
            let (val, kind) = locals
                .get(name)
                .cloned()
                .unwrap_or_else(|| ("null".into(), Kind::Ptr));
            val_emitted(String::new(), val, kind)
        }
        ExprKind::Field { .. } => panic!("internal: unresolved field access in codegen"),
        ExprKind::MethodCall { .. } => panic!("internal: unresolved method call in codegen"),
        ExprKind::Let { name, value, body } => {
            // Nested vals must not reuse the same LLVM name prefix.
            let ve = emit_expr(value, ctx, locals, &format!("{prefix}_lv_{name}"));
            let mut code = ve.code;
            locals.insert(name.clone(), (ve.value.clone(), ve.kind));
            let be = emit_expr(body, ctx, locals, &format!("{prefix}_l_{name}"));
            locals.remove(name);
            code.push_str(&be.code);
            Emitted {
                code,
                value: be.value,
                kind: be.kind,
                payload: be.payload,
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
            let cond_i64 = if ce.kind == Kind::Int {
                ce.value
            } else {
                writeln!(code, "  %{prefix}_c0 = add i64 0, 0").unwrap();
                format!("%{prefix}_c0")
            };
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

            writeln!(code, "{then_l}:").unwrap();
            let te = emit_expr(then_branch, ctx, locals, &format!("{prefix}_t{id}"));
            code.push_str(&te.code);
            writeln!(code, "  br label %{then_join}").unwrap();
            writeln!(code, "{then_join}:").unwrap();
            writeln!(code, "  br label %{merge}").unwrap();

            writeln!(code, "{else_l}:").unwrap();
            let ee = emit_expr(else_branch, ctx, locals, &format!("{prefix}_e{id}"));
            code.push_str(&ee.code);
            writeln!(code, "  br label %{else_join}").unwrap();
            writeln!(code, "{else_join}:").unwrap();
            writeln!(code, "  br label %{merge}").unwrap();

            let kind = te.kind;
            let payload = te.payload;
            let ty = match kind {
                Kind::Int => "i64",
                Kind::Ptr | Kind::Io => "ptr",
            };
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

            // Cont is a separate LLVM function. Capture enclosing locals with %env list.
            let capture_names = capture_name_order(locals);
            let mut pre = String::new();
            let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
            unpack_env_preamble(
                &mut pre,
                &mut body_locals,
                locals,
                &capture_names,
                &format!("c{id}"),
            );

            if let Some(p) = param {
                if payload_kind == Kind::Int {
                    writeln!(pre, "  %{p} = call i64 @sz_unbox_i64(ptr %value)").unwrap();
                    body_locals.insert(p.clone(), (format!("%{p}"), Kind::Int));
                } else {
                    body_locals.insert(p.clone(), ("%value".into(), Kind::Ptr));
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
            );
            writeln!(ctx.conts, "  ret ptr {ret}").unwrap();
            writeln!(ctx.conts, "}}").unwrap();
            writeln!(ctx.conts).unwrap();

            let mut code = inner_emitted.code;
            let inner_io = ensure_io(
                &mut code,
                inner_emitted.kind,
                &inner_emitted.value,
                &format!("{prefix}_inio"),
            );
            let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_cap"));
            writeln!(
                code,
                "  %{prefix}_fm = call ptr @sz_io_flatmap(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_fm"), body_emitted.payload)
        }
        ExprKind::HandleErrorWith { inner, body } => {
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
            );
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
            );
            let env_ptr = pack_env(&mut code, locals, &capture_names, &format!("{prefix}_ecap"));
            writeln!(
                code,
                "  %{prefix}_h = call ptr @sz_io_handle_error_with(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_h"), body_emitted.payload)
        }
        ExprKind::Attempt { inner } => {
            let inner_emitted = emit_expr(inner, ctx, locals, &format!("{prefix}_at"));
            let mut code = inner_emitted.code;
            let inner_io = ensure_io(
                &mut code,
                inner_emitted.kind,
                &inner_emitted.value,
                &format!("{prefix}_atio"),
            );
            writeln!(
                code,
                "  %{prefix}_attempt = call ptr @sz_io_attempt(ptr {inner_io})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_attempt"), Kind::Ptr)
        }
        ExprKind::IoRace { left, right } => {
            let le = emit_expr(left, ctx, locals, &format!("{prefix}_rl"));
            let re = emit_expr(right, ctx, locals, &format!("{prefix}_rr"));
            let mut code = le.code;
            code.push_str(&re.code);
            let lv = ensure_io(&mut code, le.kind, &le.value, &format!("{prefix}_rlio"));
            let rv = ensure_io(&mut code, re.kind, &re.value, &format!("{prefix}_rrio"));
            writeln!(
                code,
                "  %{prefix}_race = call ptr @sz_io_race(ptr {lv}, ptr {rv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_race"), Kind::Ptr)
        }
        ExprKind::IoBoth { left, right } => {
            let le = emit_expr(left, ctx, locals, &format!("{prefix}_bl"));
            let re = emit_expr(right, ctx, locals, &format!("{prefix}_br"));
            let mut code = le.code;
            code.push_str(&re.code);
            let lv = ensure_io(&mut code, le.kind, &le.value, &format!("{prefix}_blio"));
            let rv = ensure_io(&mut code, re.kind, &re.value, &format!("{prefix}_brio"));
            writeln!(
                code,
                "  %{prefix}_both = call ptr @sz_io_both(ptr {lv}, ptr {rv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_both"), Kind::Ptr)
        }
        ExprKind::IoEnsure { inner, finalizer } => {
            let ie = emit_expr(inner, ctx, locals, &format!("{prefix}_ei"));
            let fe = emit_expr(finalizer, ctx, locals, &format!("{prefix}_ef"));
            let mut code = ie.code;
            code.push_str(&fe.code);
            let iv = ensure_io(&mut code, ie.kind, &ie.value, &format!("{prefix}_eiio"));
            let fv = ensure_io(&mut code, fe.kind, &fe.value, &format!("{prefix}_efio"));
            writeln!(
                code,
                "  %{prefix}_ensure = call ptr @sz_io_ensure(ptr {iv}, ptr {fv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_ensure"), ie.payload)
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
            let iv = ensure_io(&mut code, ie.kind, &ie.value, &format!("{prefix}_tiio"));
            writeln!(
                code,
                "  %{prefix}_timeout = call ptr @sz_io_timeout(i64 {ms_val}, ptr {iv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_timeout"), ie.payload)
        }
    }
}

/// Emit `s"..."` as left-fold `sz_string_concat`. Coerce Int holes with `sz_string_from_int`.
fn emit_interpolate(
    parts: &[crate::ast::InterpPart],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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
                writeln!(
                    code,
                    "  %{prefix}_s = call ptr @sz_string_from_int(i64 {})",
                    ee.value
                )
                .unwrap();
                return val_emitted(code, format!("%{prefix}_s"), Kind::Ptr);
            }
        }
    }

    let mut code = String::new();
    let mut acc: Option<String> = None;
    for (i, part) in parts.iter().enumerate() {
        let piece = match part {
            crate::ast::InterpPart::Lit(s) => {
                let e = emit_expr(
                    &Expr::dummy(ExprKind::StrLit(s.clone())),
                    ctx,
                    locals,
                    &format!("{prefix}_l{i}"),
                );
                code.push_str(&e.code);
                e.value
            }
            crate::ast::InterpPart::Expr(e) => {
                let ee = emit_expr(e, ctx, locals, &format!("{prefix}_e{i}"));
                code.push_str(&ee.code);
                if ee.kind == Kind::Ptr {
                    ee.value
                } else {
                    writeln!(
                        code,
                        "  %{prefix}_s{i} = call ptr @sz_string_from_int(i64 {})",
                        ee.value
                    )
                    .unwrap();
                    format!("%{prefix}_s{i}")
                }
            }
        };
        acc = Some(match acc {
            None => piece,
            Some(prev) => {
                writeln!(
                    code,
                    "  %{prefix}_c{i} = call ptr @sz_string_concat(ptr {prev}, ptr {piece})"
                )
                .unwrap();
                format!("%{prefix}_c{i}")
            }
        });
    }
    val_emitted(code, acc.unwrap(), Kind::Ptr)
}

/// Emit a `_ => body` / `x => body` lambda literal as a closure value: a
/// 2-element `SzList` `cons(fn_ptr, cons(env_ptr, nil))`. `fn_ptr` matches the
/// C `SzViewTapFn` signature `void (*)(SzView *self, void *env)`. `env_ptr` is
/// the captured-locals list (same packing scheme as `flatMap` continuations).
/// Consumers (only `View.button` now) unpack the pair.
fn emit_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_tap_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("t{id}"),
    );
    if let Some(p) = param {
        body_locals.insert(p.clone(), ("%self".into(), Kind::Ptr));
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
}

/// `n => body` for `Signal.map`: `ptr (*)(i64, ptr)` returning a SzString.
fn emit_map_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_map_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("m{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), ("%n".into(), Kind::Int));
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
    } else if body_emitted.kind == Kind::Int {
        writeln!(
            ctx.conts,
            "  %m{id}_s = call ptr @sz_string_from_int(i64 {})",
            body_emitted.value
        )
        .unwrap();
        writeln!(ctx.conts, "  ret ptr %m{id}_s").unwrap();
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
}

fn emit_signal_map(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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
    writeln!(
        code,
        "  %{prefix}_fnp = call ptr @sz_list_head(ptr {})",
        mapper.value
    )
    .unwrap();
    writeln!(
        code,
        "  %{prefix}_fnt = call ptr @sz_list_tail(ptr {})",
        mapper.value
    )
    .unwrap();
    writeln!(
        code,
        "  %{prefix}_envp = call ptr @sz_list_head(ptr %{prefix}_fnt)"
    )
    .unwrap();
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_lang_signal_map(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        src.value
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

/// `t => IO[...]` continuation: `ptr (*)(ptr value, ptr env)` returning `SzIo*`.
fn emit_io_cont_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_rcont_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("r{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), ("%value".into(), Kind::Ptr));
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
}

/// `t => Bool` predicate: `i64 (*)(ptr value, ptr env)`.
fn emit_pred_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_pred_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("p{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), ("%value".into(), Kind::Ptr));
        }
    }

    let body_emitted = emit_expr(body, ctx, &mut body_locals, &format!("p{id}"));
    assert!(
        body_emitted.kind == Kind::Int,
        "Stream.filter predicate must be Bool/Int"
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
}

/// `t => String` mapper: `ptr (*)(ptr value, ptr env)`.
fn emit_smap_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_smap_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("sm{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), ("%value".into(), Kind::Ptr));
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
    if body_emitted.kind == Kind::Int {
        writeln!(
            ctx.conts,
            "  %sm{id}_istr = call ptr @sz_string_from_int(i64 {})",
            body_emitted.value
        )
        .unwrap();
        writeln!(ctx.conts, "  ret ptr %sm{id}_istr").unwrap();
    } else {
        assert!(
            body_emitted.kind == Kind::Ptr,
            "Stream.map mapper must be String/Int"
        );
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
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
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let first = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} callback must be a lambda");
    };
    let lam = emit_io_cont_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = first.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    if callee == "Resource.make" {
        let acq = ensure_io(
            &mut code,
            first.kind,
            &first.value,
            &format!("{prefix}_acq"),
        );
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_lang_resource_make(ptr {acq}, ptr %{prefix}_fnp, ptr %{prefix}_envp)"
        )
        .unwrap();
        val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
    } else {
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_lang_resource_use(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
            first.value
        )
        .unwrap();
        io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
    }
}

fn emit_stream_evalmap(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "Stream.evalMap expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("Stream.evalMap callback must be a lambda");
    };
    let lam = emit_io_cont_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_stream_evalmap(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        inner.value
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

fn emit_stream_pred(
    callee: &str,
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
    as_io: bool,
) -> Emitted {
    assert!(args.len() == 2, "{callee} expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("{callee} predicate must be a lambda");
    };
    let lam = emit_pred_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @{rt}(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        inner.value
    )
    .unwrap();
    if as_io {
        io_emitted(code, format!("%{prefix}_v"), Kind::Int)
    } else {
        val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
    }
}

fn emit_stream_filter(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 2, "Stream.map expects 2 args");
    let inner = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let ExprKind::Lambda { param, body } = &args[1].kind else {
        panic!("Stream.map mapper must be a lambda");
    };
    let lam = emit_smap_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
    let mut code = inner.code;
    code.push_str(&lam.code);
    unpack_closure(&mut code, &lam.value, prefix);
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_stream_map(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
        inner.value
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

fn emit_net_serve(
    rt: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
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
    io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

/// `_ => View…` factory for `Ui.run`: `ptr (*)(ptr env)` returning `SzView*`.
fn emit_rebuild_lambda(
    param: &Option<String>,
    body: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    let id = *ctx.cont_id;
    *ctx.cont_id += 1;
    let fn_name = format!("sz_uibuild_{id}");

    let capture_names = capture_name_order(locals);
    let mut pre = String::new();
    let mut body_locals: HashMap<String, (String, Kind)> = HashMap::new();
    unpack_env_preamble(
        &mut pre,
        &mut body_locals,
        locals,
        &capture_names,
        &format!("u{id}"),
    );
    if let Some(p) = param {
        if p != "_" {
            body_locals.insert(p.clone(), ("%env".into(), Kind::Ptr));
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
    writeln!(
        code,
        "  %{prefix}_cl2 = call ptr @sz_list_cons(ptr @{fn_name}, ptr %{prefix}_cl1)"
    )
    .unwrap();
    val_emitted(code, format!("%{prefix}_cl2"), Kind::Ptr)
}

fn emit_ui_run(
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    assert!(args.len() == 1, "Ui.run expects 1 arg");
    if let ExprKind::Lambda { param, body } = &args[0].kind {
        let lam = emit_rebuild_lambda(param, body, ctx, locals, &format!("{prefix}_fn"));
        let mut code = lam.code;
        unpack_closure(&mut code, &lam.value, prefix);
        writeln!(
            code,
            "  %{prefix}_v = call ptr @sz_ui_run_rebuild(ptr %{prefix}_fnp, ptr %{prefix}_envp)"
        )
        .unwrap();
        return io_emitted(code, format!("%{prefix}_v"), Kind::Ptr);
    }
    let root = emit_expr(&args[0], ctx, locals, &format!("{prefix}_a0"));
    let mut code = root.code;
    writeln!(
        code,
        "  %{prefix}_v = call ptr @sz_ui_run_view(ptr {})",
        root.value
    )
    .unwrap();
    io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
}

fn emit_binary(
    op: &BinOp,
    left: &Expr,
    right: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
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
        return val_emitted(code, format!("%{prefix}_add"), Kind::Ptr);
    }

    if matches!(op, BinOp::Eq | BinOp::Ne) && le.kind == Kind::Ptr && re.kind == Kind::Ptr {
        writeln!(
            code,
            "  %{prefix}_eqi = call i32 @sz_string_eq(ptr {}, ptr {})",
            le.value, re.value
        )
        .unwrap();
        writeln!(code, "  %{prefix}_eq = zext i32 %{prefix}_eqi to i64").unwrap();
        if *op == BinOp::Eq {
            return val_emitted(code, format!("%{prefix}_eq"), Kind::Int);
        }
        writeln!(code, "  %{prefix}_ne = icmp eq i64 %{prefix}_eq, 0").unwrap();
        writeln!(code, "  %{prefix}_nev = zext i1 %{prefix}_ne to i64").unwrap();
        return val_emitted(code, format!("%{prefix}_nev"), Kind::Int);
    }

    let lv = if le.kind == Kind::Int {
        le.value
    } else {
        writeln!(code, "  %{prefix}_l0 = add i64 0, 0").unwrap();
        format!("%{prefix}_l0")
    };
    let rv = if re.kind == Kind::Int {
        re.value
    } else {
        writeln!(code, "  %{prefix}_r0 = add i64 0, 0").unwrap();
        format!("%{prefix}_r0")
    };

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
        BinOp::Div => {
            writeln!(code, "  %{prefix}_v = sdiv i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::Mod => {
            writeln!(code, "  %{prefix}_v = srem i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::And => {
            writeln!(code, "  %{prefix}_v = and i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        BinOp::Or => {
            writeln!(code, "  %{prefix}_v = or i64 {lv}, {rv}").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
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

fn emit_call(
    callee: &str,
    args: &[Expr],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    if callee == "Signal.map" {
        return emit_signal_map(args, ctx, locals, prefix);
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
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Str.len" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_len(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.slice" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_slice(ptr {}, i64 {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value, emitted_args[2].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Str.eq" => {
            writeln!(
                code,
                "  %{prefix}_eqi = call i32 @sz_string_eq(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_v = zext i32 %{prefix}_eqi to i64").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.charAt" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_char_at(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.fromInt" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_from_int(i64 {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Str.indexOf" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_string_index_of(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Str.lines" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_string_lines(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.empty" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_list_nil()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.cons" => {
            let head = if emitted_args[0].kind == Kind::Int {
                writeln!(
                    code,
                    "  %{prefix}_hd = call ptr @sz_box_i64(i64 {})",
                    emitted_args[0].value
                )
                .unwrap();
                format!("%{prefix}_hd")
            } else {
                emitted_args[0].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_cons(ptr {head}, ptr {})",
                emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.isEmpty" => {
            writeln!(
                code,
                "  %{prefix}_i = call i32 @sz_list_is_empty(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            writeln!(code, "  %{prefix}_v = zext i32 %{prefix}_i to i64").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.head" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_head(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.tail" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_tail(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.len" => {
            writeln!(
                code,
                "  %{prefix}_v = call i64 @sz_list_len(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "List.at" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_at(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.reverse" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_reverse(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
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
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "List.append" => {
            let elem = if emitted_args[1].kind == Kind::Int {
                writeln!(
                    code,
                    "  %{prefix}_el = call ptr @sz_box_i64(i64 {})",
                    emitted_args[1].value
                )
                .unwrap();
                format!("%{prefix}_el")
            } else {
                emitted_args[1].value.clone()
            };
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_list_append(ptr {}, ptr {elem})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.read" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_read(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.write" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_write(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.list" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.mkdirs" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_mkdirs(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fs.canonicalize" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fs_canonicalize(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
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
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Sys.exec" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_exec(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
        }
        "Sys.spawn" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_spawn(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
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
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Ref.get" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ref_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Ref.set" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ref_set(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Queue.unbounded" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_queue_unbounded()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Queue.offer" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_queue_offer(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Queue.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_queue_take(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Deferred.empty" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_deferred_empty()").unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Deferred.complete" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_deferred_complete(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Deferred.get" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_deferred_get(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Fiber.fork" => {
            let iv = ensure_io(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_fio"),
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_fiber_fork(ptr {iv})").unwrap();
            io_emitted(code, format!("%{prefix}_v"), emitted_args[0].payload)
        }
        "Fiber.join" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_fiber_join(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
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
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_io_forever(ptr {iv})").unwrap();
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
            );
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_io_repeat_n(i64 {n}, ptr {iv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), emitted_args[1].payload)
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
            );
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_io_retry_n(i64 {n}, ptr {iv})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), emitted_args[1].payload)
        }
        "Stream.emit" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_emit(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.emits" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_emits(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.eval" => {
            let io = ensure_io(
                &mut code,
                emitted_args[0].kind,
                &emitted_args[0].value,
                &format!("{prefix}_eval"),
            );
            writeln!(code, "  %{prefix}_v = call ptr @sz_stream_eval(ptr {io})").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.concat" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_concat(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.take" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_take(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.drop" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_drop(ptr {}, i64 {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.compileToList" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_compile_to_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Stream.drain" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_stream_drain(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
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
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "Signal.list" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_signal_list(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
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
            writeln!(
                code,
                "  %{prefix}_fnp = call ptr @sz_list_head(ptr {})",
                emitted_args[1].value
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_fnt = call ptr @sz_list_tail(ptr {})",
                emitted_args[1].value
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_envp = call ptr @sz_list_head(ptr %{prefix}_fnt)"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_button(ptr {}, ptr %{prefix}_fnp, ptr %{prefix}_envp)",
                emitted_args[0].value
            )
            .unwrap();
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
        "View.stack" => emit_view_box("sz_lang_view_stack", &mut code, &emitted_args, prefix),
        "View.each" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_each(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.scroll" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_scroll(ptr {})",
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
        "Ui.run" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_ui_run_view(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
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
                        (Kind::Int, _) => {
                            writeln!(code, "  %{prefix}_a{i} = add i64 0, 0").unwrap();
                            (format!("%{prefix}_a{i}"), "i64")
                        }
                        (_, Kind::Int) => {
                            writeln!(
                                code,
                                "  %{prefix}_a{i} = call ptr @sz_box_i64(i64 {})",
                                a.value
                            )
                            .unwrap();
                            (format!("%{prefix}_a{i}"), "ptr")
                        }
                        _ => (a.value.clone(), "ptr"),
                    };
                    arg_parts.push(format!("{lty} {lval}"));
                }
            } else {
                for (i, a) in emitted_args.iter().enumerate() {
                    let (lval, lty) = if a.kind == Kind::Int {
                        (a.value.clone(), "i64")
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

    #[test]
    fn emit_contains_main_and_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hi")"#).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("define i32 @main(i32 %argc, ptr %argv)"));
        assert!(ir.contains("sz_io_println"));
        assert!(ir.contains("sz_runtime_main_args"));
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
    _ <- IO.println(Str.fromInt(hit))
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
        assert!(!ir.contains("call ptr @sz_ui_run_view"));
    }

    #[test]
    fn emit_view_stretch() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.stretch(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_stretch"),
            "expected View.stretch in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_max_size() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.maxSize(40, 30, View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_max_size"),
            "expected View.maxSize in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_clip() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.clip(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_clip"),
            "expected View.clip in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_opacity() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.opacity(50, View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_opacity"),
            "expected View.opacity in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_max_lines() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.maxLines(2, View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_max_lines"),
            "expected View.maxLines in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_ellipsis() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.ellipsis(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_ellipsis"),
            "expected View.ellipsis in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_text_color() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.textColor(Color.rgb(255, 0, 0), View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_text_color"),
            "expected View.textColor in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_gap() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.gap(0, View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_gap"),
            "expected View.gap in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_font_size() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.fontSize(16, View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_font_size"),
            "expected View.fontSize in IR:\n{ir}"
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
    fn emit_view_ignore_pointer() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.ignorePointer(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_ignore_pointer"),
            "expected View.ignorePointer in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_absorb_pointer() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.absorbPointer(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_absorb_pointer"),
            "expected View.absorbPointer in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_view_exclude_semantics() {
        let src = r#"@main def main: IO[Unit] =
  Ui.run(_ => View.excludeSemantics(View.text("x")))
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(
            ir.contains("sz_lang_view_exclude_semantics"),
            "expected View.excludeSemantics in IR:\n{ir}"
        );
    }

    #[test]
    fn emit_ui_run_view_still_emits() {
        let src = r#"@main def main: IO[Unit] =
  for {
    root = View.text("x")
    _ <- Ui.run(root)
  } yield ()
"#;
        let p = crate::lower::lower_program(parse(src).unwrap());
        crate::typ::typecheck(&p).expect("typecheck");
        let ir = emit_llvm(&p);
        assert!(ir.contains("call ptr @sz_ui_run_view"));
        assert!(!ir.contains("sz_ui_reload_rebuild"));
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

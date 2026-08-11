use crate::ast::{BinOp, EnumDef, Expr, FunDef, Pattern, Program, Type};
use std::collections::HashMap;
use std::fmt::Write as _;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Kind {
    Int,
    Ptr,
    Io,
}

/// Emit LLVM IR text for a Stage-0 program. Links against `libscuzz_rt`.
pub fn emit_llvm(program: &Program) -> String {
    let mut strs: Vec<String> = Vec::new();
    for d in &program.defs {
        collect_strings(&d.body, &mut strs);
    }
    collect_strings(&program.main.body, &mut strs);

    let enum_tags = build_enum_tags(&program.enums);
    let funs: HashMap<String, &FunDef> = program.defs.iter().map(|d| (d.name.clone(), d)).collect();

    let mut out = String::new();
    writeln!(out, "; Scuzz Lang Stage-0 generated LLVM IR").unwrap();
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
    writeln!(out, "declare {{ i32, ptr, ptr }} @sz_io_unsafe_run(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_adt_new(i32, ptr)").unwrap();
    writeln!(out, "declare i32 @sz_adt_tag(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_effects_run_kit()").unwrap();
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
    writeln!(out, "declare ptr @sz_sys_exec(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_sys_getenv(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_clock_real_time()").unwrap();
    writeln!(out, "declare ptr @sz_clock_monotonic()").unwrap();
    writeln!(out, "declare ptr @sz_random_next_int(i64)").unwrap();
    writeln!(out, "declare ptr @sz_net_http_get(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_impurity_run_kit()").unwrap();
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
    writeln!(out, "declare ptr @sz_lang_view_column()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_row()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_list()").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_each(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_scroll(ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_text_field(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_icon(i64, i64)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_image(i64, i64, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_add_child(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_add_texts(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_lang_view_show_when(ptr, i64, ptr)").unwrap();
    writeln!(out, "declare ptr @sz_ui_run_view(ptr)").unwrap();
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

    writeln!(out, "define internal ptr @sz_delay_unit_thunk(ptr %env) {{").unwrap();
    writeln!(out, "entry:").unwrap();
    writeln!(out, "  ret ptr null").unwrap();
    writeln!(out, "}}").unwrap();
    writeln!(out).unwrap();

    // User defs are emitted below; LLVM allows call sites before the defining
    // `define` in the same module (no separate `declare` — that would error).

    let mut cont_id = 0usize;
    let mut conts = String::new();
    let mut ctx = EmitCtx {
        strs: &strs,
        enum_tags: &enum_tags,
        funs: &funs,
        cont_id: &mut cont_id,
        conts: &mut conts,
    };

    let mut fundef_ir = String::new();
    for d in &program.defs {
        emit_fundef(d, &mut ctx, &mut fundef_ir);
    }

    let mut locals: HashMap<String, (String, Kind)> = HashMap::new();
    let body_expr = emit_expr(&program.main.body, &mut ctx, &mut locals, "build");

    out.push_str(&conts);
    out.push_str(&fundef_ir);

    writeln!(out, "define i32 @main(i32 %argc, ptr %argv) {{").unwrap();
    writeln!(out, "entry:").unwrap();
    out.push_str(&body_expr.code);
    let io_val = ensure_io(&mut out, body_expr.kind, &body_expr.value, "wrapped");
    writeln!(
        out,
        "  %rc = call i32 @sz_runtime_main_args(ptr {io_val}, i32 %argc, ptr %argv)"
    )
    .unwrap();
    writeln!(out, "  ret i32 %rc").unwrap();
    writeln!(out, "}}").unwrap();

    out
}

struct EmitCtx<'a> {
    strs: &'a [String],
    enum_tags: &'a HashMap<(String, String), i32>,
    funs: &'a HashMap<String, &'a FunDef>,
    cont_id: &'a mut usize,
    conts: &'a mut String,
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
        for (i, c) in e.cases.iter().enumerate() {
            m.insert((e.name.clone(), c.clone()), i as i32);
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
    match expr {
        Expr::StrLit(s) => {
            if !out.contains(s) {
                out.push(s.clone());
            }
        }
        Expr::Interpolate { parts } => {
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
        Expr::IoPrintln(e)
        | Expr::IoSleep(e)
        | Expr::IoFail(e)
        | Expr::IoPure(e)
        | Expr::Attempt { inner: e } => collect_strings(e, out),
        Expr::Lambda { body, .. } => collect_strings(body, out),
        Expr::FlatMap { inner, body, .. }
        | Expr::HandleErrorWith { inner, body }
        | Expr::Let {
            value: inner,
            body,
            ..
        }
        | Expr::IoRace {
            left: inner,
            right: body,
        }
        | Expr::IoBoth {
            left: inner,
            right: body,
        }
        | Expr::Binary {
            left: inner,
            right: body,
            ..
        } => {
            collect_strings(inner, out);
            collect_strings(body, out);
        }
        Expr::If {
            cond,
            then_branch,
            else_branch,
        } => {
            collect_strings(cond, out);
            collect_strings(then_branch, out);
            collect_strings(else_branch, out);
        }
        Expr::Match { scrutinee, arms } => {
            collect_strings(scrutinee, out);
            for a in arms {
                collect_strings(&a.body, out);
            }
        }
        Expr::Call { args, .. } => {
            for a in args {
                collect_strings(a, out);
            }
        }
        Expr::ListLit { elems } => {
            for e in elems {
                collect_strings(e, out);
            }
        }
        Expr::IoDelayUnit
        | Expr::Unit
        | Expr::EffectsRunKit
        | Expr::Var(_)
        | Expr::IntLit(_)
        | Expr::AdtConstruct { .. } => {}
        Expr::For { binders, body } => {
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
    write!(out, "define internal {ret} @sz_user_{}(", def.name).unwrap();
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
                writeln!(
                    out,
                    "  %ret_box = call ptr @sz_box_i64(i64 {})",
                    body.value
                )
                .unwrap();
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
            writeln!(
                code,
                "  %{prefix}_b{i} = call ptr @sz_box_i64(i64 {val})"
            )
            .unwrap();
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
        writeln!(
            pre,
            "  %{prefix}_h{i} = call ptr @sz_list_head(ptr {cur})"
        )
        .unwrap();
        writeln!(
            pre,
            "  %{prefix}_t{i} = call ptr @sz_list_tail(ptr {cur})"
        )
        .unwrap();
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
    if let Expr::StrLit(s) = expr {
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
    if let Expr::StrLit(s) = expr {
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

fn emit_expr(
    expr: &Expr,
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    match expr {
        Expr::Unit => val_emitted(String::new(), "null".into(), Kind::Ptr),
        Expr::IntLit(n) => val_emitted(String::new(), format!("{n}"), Kind::Int),
        Expr::StrLit(s) => {
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
        Expr::ListLit { elems } => emit_list_lit(elems, ctx, locals, prefix),
        Expr::Interpolate { parts } => emit_interpolate(parts, ctx, locals, prefix),
        Expr::IoDelayUnit => {
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_delay = call ptr @sz_io_delay(ptr @sz_delay_unit_thunk, ptr null)"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_delay"), Kind::Ptr)
        }
        Expr::IoSleep(ms) => {
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
        Expr::IoFail(msg) => {
            let mut code = String::new();
            let cstr = emit_cstr_from_string(&mut code, ctx.strs, msg, ctx, locals, prefix);
            writeln!(
                code,
                "  %{prefix}_io = call ptr @sz_io_fail_cstr(ptr {cstr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        Expr::IoPrintln(arg) => {
            let mut code = String::new();
            let s = emit_sz_string(&mut code, ctx.strs, arg, ctx, locals, prefix);
            writeln!(
                code,
                "  %{prefix}_io = call ptr @sz_io_println(ptr {s})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        Expr::IoPure(inner) => {
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
            writeln!(
                code,
                "  %{prefix}_io = call ptr @sz_io_pure(ptr {ptr})"
            )
            .unwrap();
            let payload = if ie.kind == Kind::Io {
                ie.payload
            } else {
                ie.kind
            };
            io_emitted(code, format!("%{prefix}_io"), payload)
        }
        Expr::EffectsRunKit => {
            let mut code = String::new();
            writeln!(code, "  %{prefix}_io = call ptr @sz_effects_run_kit()").unwrap();
            io_emitted(code, format!("%{prefix}_io"), Kind::Ptr)
        }
        Expr::AdtConstruct {
            enum_name,
            case_name,
        } => {
            let tag = ctx
                .enum_tags
                .get(&(enum_name.clone(), case_name.clone()))
                .copied()
                .unwrap_or(0);
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_adt = call ptr @sz_adt_new(i32 {tag}, ptr null)"
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_adt"), Kind::Ptr)
        }
        Expr::Var(name) => {
            let (val, kind) = locals
                .get(name)
                .cloned()
                .unwrap_or_else(|| ("null".into(), Kind::Ptr));
            val_emitted(String::new(), val, kind)
        }
        Expr::Let { name, value, body } => {
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
        Expr::If {
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
            writeln!(
                code,
                "  %{prefix}_cmp = icmp ne i64 {cond_i64}, 0"
            )
            .unwrap();
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
        Expr::Lambda { param, body } => emit_lambda(param, body, ctx, locals, prefix),
        Expr::Binary { op, left, right } => emit_binary(op, left, right, ctx, locals, prefix),
        Expr::Call { callee, args } => emit_call(callee, args, ctx, locals, prefix),
        Expr::For { .. } => panic!("internal: unlowered `for` in codegen"),
        Expr::Match { scrutinee, arms } => {
            let se = emit_expr(scrutinee, ctx, locals, &format!("{prefix}_sc"));
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let mut code = se.code;
            writeln!(
                code,
                "  %{prefix}_tag = call i32 @sz_adt_tag(ptr {})",
                se.value
            )
            .unwrap();

            let merge = format!("{prefix}_merge_{id}");
            let default_label = format!("{prefix}_default_{id}");

            write!(
                code,
                "  switch i32 %{prefix}_tag, label %{default_label} ["
            )
            .unwrap();
            for (i, arm) in arms.iter().enumerate() {
                if let Pattern::Adt {
                    enum_name,
                    case_name,
                } = &arm.pattern
                {
                    if let Some(tag) = ctx.enum_tags.get(&(enum_name.clone(), case_name.clone())) {
                        write!(code, " i32 {tag}, label %{prefix}_arm_{id}_{i}").unwrap();
                    }
                }
            }
            writeln!(code, " ]").unwrap();

            let mut phi_parts: Vec<(String, String)> = Vec::new();
            let mut result_kind = Kind::Io;
            let mut result_payload = Kind::Ptr;

            for (i, arm) in arms.iter().enumerate() {
                if matches!(arm.pattern, Pattern::Wildcard) {
                    continue;
                }
                let label = format!("{prefix}_arm_{id}_{i}");
                writeln!(code, "{label}:").unwrap();
                let ae = emit_expr(
                    &arm.body,
                    ctx,
                    locals,
                    &format!("{prefix}_a{id}_{i}"),
                );
                code.push_str(&ae.code);
                if phi_parts.is_empty() {
                    result_kind = ae.kind;
                    result_payload = ae.payload;
                }
                writeln!(code, "  br label %{merge}").unwrap();
                phi_parts.push((ae.value, label));
            }

            writeln!(code, "{default_label}:").unwrap();
            if let Some(arm) = arms
                .iter()
                .find(|a| matches!(a.pattern, Pattern::Wildcard))
            {
                let ae = emit_expr(
                    &arm.body,
                    ctx,
                    locals,
                    &format!("{prefix}_aw{id}"),
                );
                code.push_str(&ae.code);
                if phi_parts.is_empty() {
                    result_kind = ae.kind;
                    result_payload = ae.payload;
                }
                writeln!(code, "  br label %{merge}").unwrap();
                phi_parts.push((ae.value, default_label));
            } else {
                let dflt = match result_kind {
                    Kind::Int => {
                        writeln!(code, "  %{prefix}_dflt = add i64 0, 0").unwrap();
                        format!("%{prefix}_dflt")
                    }
                    Kind::Ptr => "null".into(),
                    Kind::Io => {
                        writeln!(
                            code,
                            "  %{prefix}_dflt = call ptr @sz_io_pure(ptr null)"
                        )
                        .unwrap();
                        format!("%{prefix}_dflt")
                    }
                };
                writeln!(code, "  br label %{merge}").unwrap();
                phi_parts.push((dflt, default_label));
            }

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
        Expr::FlatMap { inner, param, body } => {
            let id = *ctx.cont_id;
            *ctx.cont_id += 1;
            let cont_name = format!("sz_cont_{id}");

            let inner_emitted = emit_expr(inner, ctx, locals, &format!("{prefix}_in"));
            let payload_kind = inner_emitted.payload;

            // Cont is a separate LLVM function; capture enclosing locals via %env list.
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
                    writeln!(
                        pre,
                        "  %{p} = call i64 @sz_unbox_i64(ptr %value)"
                    )
                    .unwrap();
                    body_locals.insert(p.clone(), (format!("%{p}"), Kind::Int));
                } else {
                    body_locals.insert(p.clone(), ("%value".into(), Kind::Ptr));
                }
            }

            let body_emitted = emit_expr(
                body,
                ctx,
                &mut body_locals,
                &format!("c{id}"),
            );

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
            let env_ptr = pack_env(
                &mut code,
                locals,
                &capture_names,
                &format!("{prefix}_cap"),
            );
            writeln!(
                code,
                "  %{prefix}_fm = call ptr @sz_io_flatmap(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_fm"), body_emitted.payload)
        }
        Expr::HandleErrorWith { inner, body } => {
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
            let env_ptr = pack_env(
                &mut code,
                locals,
                &capture_names,
                &format!("{prefix}_ecap"),
            );
            writeln!(
                code,
                "  %{prefix}_h = call ptr @sz_io_handle_error_with(ptr {inner_io}, ptr @{cont_name}, ptr {env_ptr})"
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_h"), body_emitted.payload)
        }
        Expr::Attempt { inner } => {
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
        Expr::IoRace { left, right } => {
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
        Expr::IoBoth { left, right } => {
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
    }
}

/// Emit `s"..."` as left-fold `sz_string_concat`, coercing Int holes via `sz_string_from_int`.
fn emit_interpolate(
    parts: &[crate::ast::InterpPart],
    ctx: &mut EmitCtx<'_>,
    locals: &mut HashMap<String, (String, Kind)>,
    prefix: &str,
) -> Emitted {
    if parts.is_empty() {
        return emit_expr(&Expr::StrLit(String::new()), ctx, locals, prefix);
    }
    if parts.len() == 1 {
        match &parts[0] {
            crate::ast::InterpPart::Lit(s) => {
                return emit_expr(&Expr::StrLit(s.clone()), ctx, locals, prefix);
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
                let e = emit_expr(&Expr::StrLit(s.clone()), ctx, locals, &format!("{prefix}_l{i}"));
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
/// C `SzViewTapFn` signature `void (*)(SzView *self, void *env)`; `env_ptr` is
/// the captured-locals list (same packing scheme as `flatMap` continuations).
/// Consumers (currently only `View.button`) unpack the pair back out.
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
            "  %t{id}_ur = call {{ i32, ptr, ptr }} @sz_io_unsafe_run(ptr {})",
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
            "  %m{id}_ur = call {{ i32, ptr, ptr }} @sz_io_unsafe_run(ptr {})",
            body_emitted.value
        )
        .unwrap();
        writeln!(
            ctx.conts,
            "  %m{id}_ok = extractvalue {{ i32, ptr, ptr }} %m{id}_ur, 1"
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
    let Expr::Lambda { param, body } = &args[1] else {
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
        writeln!(
            code,
            "  %{prefix}_eq = zext i32 %{prefix}_eqi to i64"
        )
        .unwrap();
        if *op == BinOp::Eq {
            return val_emitted(code, format!("%{prefix}_eq"), Kind::Int);
        }
        writeln!(
            code,
            "  %{prefix}_ne = icmp eq i64 %{prefix}_eq, 0"
        )
        .unwrap();
        writeln!(
            code,
            "  %{prefix}_nev = zext i1 %{prefix}_ne to i64"
        )
        .unwrap();
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
            writeln!(
                code,
                "  %{prefix}_cmp = icmp {pred} i64 {lv}, {rv}"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_v = zext i1 %{prefix}_cmp to i64"
            )
            .unwrap();
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
    let mut emitted_args = Vec::new();
    for (i, a) in args.iter().enumerate() {
        emitted_args.push(emit_expr(
            a,
            ctx,
            locals,
            &format!("{prefix}_arg{i}"),
        ));
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
            writeln!(
                code,
                "  %{prefix}_v = zext i32 %{prefix}_eqi to i64"
            )
            .unwrap();
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
            writeln!(
                code,
                "  %{prefix}_v = zext i32 %{prefix}_i to i64"
            )
            .unwrap();
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
        "Sys.exec" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_sys_exec(ptr {})",
                emitted_args[0].value
            )
            .unwrap();
            io_emitted(code, format!("%{prefix}_v"), Kind::Int)
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
        "View.column" => emit_view_box(
            "sz_lang_view_column",
            &mut code,
            &emitted_args,
            prefix,
        ),
        "View.row" => emit_view_box("sz_lang_view_row", &mut code, &emitted_args, prefix),
        "View.list" => {
            writeln!(code, "  %{prefix}_v = call ptr @sz_lang_view_list()").unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
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
        "View.addChild" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_add_child(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
            )
            .unwrap();
            val_emitted(code, format!("%{prefix}_v"), Kind::Ptr)
        }
        "View.addTexts" => {
            writeln!(
                code,
                "  %{prefix}_v = call ptr @sz_lang_view_add_texts(ptr {}, ptr {})",
                emitted_args[0].value, emitted_args[1].value
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
            let f = ctx.funs.get(other).copied();
            let (ret_ty, ret_kind, payload) = if let Some(f) = f {
                (
                    llvm_type(&f.ret),
                    kind_of_type(&f.ret),
                    payload_of_type(&f.ret),
                )
            } else {
                ("ptr", Kind::Ptr, Kind::Ptr)
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
                "  %{prefix}_v = call {ret_ty} @sz_user_{other}({})",
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
        assert!(ir.contains("switch i32"));
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
}

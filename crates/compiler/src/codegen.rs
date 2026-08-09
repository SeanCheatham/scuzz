use crate::ast::{EnumDef, Expr, Pattern, Program};
use std::collections::HashMap;
use std::fmt::Write as _;

/// Emit LLVM IR text for a Stage-0 / Phase 3 program. Links against `libscalui_rt`.
pub fn emit_llvm(program: &Program) -> String {
    let mut out = String::new();
    let mut strs: Vec<String> = Vec::new();
    collect_strings(&program.main.body, &mut strs);

    let enum_tags = build_enum_tags(&program.enums);

    writeln!(out, "; ScalUI Stage-0 generated LLVM IR").unwrap();
    writeln!(out, "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"").unwrap();
    writeln!(out, "target triple = \"x86_64-pc-linux-gnu\"").unwrap();
    writeln!(out).unwrap();

    writeln!(out, "declare ptr @su_string_from_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_println(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_pure(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_delay(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_flatmap(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_fail_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_sleep_ms(i64)").unwrap();
    writeln!(out, "declare ptr @su_io_handle_error_with(ptr, ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_attempt(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_race(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_both(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_adt_new(i32, ptr)").unwrap();
    writeln!(out, "declare i32 @su_adt_tag(ptr)").unwrap();
    writeln!(out, "declare ptr @su_lexer_classify(ptr)").unwrap();
    writeln!(out, "declare ptr @su_effects_run_kit()").unwrap();
    writeln!(out, "declare ptr @su_ui_run_headless_label(ptr, i32, i32)").unwrap();
    writeln!(out, "declare ptr @su_ui_run_counter(i32, i32)").unwrap();
    writeln!(out, "declare ptr @su_ui_run_todo(i32, i32)").unwrap();
    writeln!(out, "declare i32 @su_runtime_main(ptr)").unwrap();
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

    writeln!(out, "define internal ptr @su_delay_unit_thunk(ptr %env) {{").unwrap();
    writeln!(out, "entry:").unwrap();
    writeln!(out, "  ret ptr null").unwrap();
    writeln!(out, "}}").unwrap();
    writeln!(out).unwrap();

    // Error handler trampoline: SuErrorHandler(err, env) → calls cont(err, env) style body.
    // We lower handleErrorWith bodies as SuCont-shaped functions that ignore the error ptr.
    // Runtime signature: SuIo* (*)(SuError*, void*)
    // Emit wrapper per handler that ignores err and runs the body.

    let mut cont_id = 0usize;
    let mut conts = String::new();
    let mut locals: HashMap<String, String> = HashMap::new();
    let body_expr = emit_expr(
        &program.main.body,
        &strs,
        &enum_tags,
        &mut cont_id,
        &mut conts,
        &mut locals,
        "build",
    );

    out.push_str(&conts);

    writeln!(out, "define i32 @main() {{").unwrap();
    writeln!(out, "entry:").unwrap();
    out.push_str(&body_expr.code);
    // If body is a pure ADT/unit (shouldn't happen for @main after typecheck), wrap.
    let io_val = if body_expr.is_io {
        body_expr.value
    } else {
        writeln!(
            out,
            "  %wrapped = call ptr @su_io_pure(ptr {})",
            body_expr.value
        )
        .unwrap();
        "%wrapped".into()
    };
    writeln!(out, "  %rc = call i32 @su_runtime_main(ptr {io_val})").unwrap();
    writeln!(out, "  ret i32 %rc").unwrap();
    writeln!(out, "}}").unwrap();

    out
}

struct Emitted {
    code: String,
    value: String,
    is_io: bool,
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

fn collect_strings(expr: &Expr, out: &mut Vec<String>) {
    match expr {
        Expr::IoPrintln(s)
        | Expr::UiRunHeadless(s)
        | Expr::IoFail(s)
        | Expr::LexerClassify(s) => {
            if !out.contains(s) {
                out.push(s.clone());
            }
        }
        Expr::FlatMap { inner, body }
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
        } => {
            collect_strings(inner, out);
            collect_strings(body, out);
        }
        Expr::Attempt { inner } => collect_strings(inner, out),
        Expr::Match { scrutinee, arms } => {
            collect_strings(scrutinee, out);
            for a in arms {
                collect_strings(&a.body, out);
            }
        }
        Expr::IoDelayUnit
        | Expr::Unit
        | Expr::UiRunCounter
        | Expr::UiRunTodo
        | Expr::EffectsRunKit
        | Expr::IoSleep(_)
        | Expr::Var(_)
        | Expr::AdtConstruct { .. } => {}
    }
}

fn str_index(strs: &[String], s: &str) -> usize {
    strs.iter().position(|x| x == s).expect("string collected")
}

fn emit_expr(
    expr: &Expr,
    strs: &[String],
    enum_tags: &HashMap<(String, String), i32>,
    cont_id: &mut usize,
    conts: &mut String,
    locals: &mut HashMap<String, String>,
    prefix: &str,
) -> Emitted {
    match expr {
        Expr::Unit => {
            let mut code = String::new();
            writeln!(code, "  %{prefix}_unit = call ptr @su_io_pure(ptr null)").unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_unit"),
                is_io: true,
            }
        }
        Expr::IoDelayUnit => {
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_delay = call ptr @su_io_delay(ptr @su_delay_unit_thunk, ptr null)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_delay"),
                is_io: true,
            }
        }
        Expr::IoSleep(ms) => {
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_sleep = call ptr @su_io_sleep_ms(i64 {ms})"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_sleep"),
                is_io: true,
            }
        }
        Expr::IoFail(s) => {
            let idx = str_index(strs, s);
            let len = s.len() + 1;
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_sptr = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_io_fail_cstr(ptr %{prefix}_sptr)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::IoPrintln(s) => {
            let idx = str_index(strs, s);
            let len = s.len() + 1;
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_sptr = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_s = call ptr @su_string_from_cstr(ptr %{prefix}_sptr)"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_io_println(ptr %{prefix}_s)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::UiRunHeadless(s) => {
            let idx = str_index(strs, s);
            let len = s.len() + 1;
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_sptr = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_ui_run_headless_label(ptr %{prefix}_sptr, i32 0, i32 0)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::UiRunCounter => {
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_ui_run_counter(i32 0, i32 0)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::UiRunTodo => {
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_ui_run_todo(i32 0, i32 0)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::EffectsRunKit => {
            let mut code = String::new();
            writeln!(code, "  %{prefix}_io = call ptr @su_effects_run_kit()").unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
                is_io: true,
            }
        }
        Expr::LexerClassify(s) => {
            let idx = str_index(strs, s);
            let len = s.len() + 1;
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_sptr = getelementptr inbounds [{len} x i8], ptr @.str{idx}, i64 0, i64 0"
            )
            .unwrap();
            writeln!(
                code,
                "  %{prefix}_adt = call ptr @su_lexer_classify(ptr %{prefix}_sptr)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_adt"),
                is_io: false,
            }
        }
        Expr::AdtConstruct {
            enum_name,
            case_name,
        } => {
            let tag = enum_tags
                .get(&(enum_name.clone(), case_name.clone()))
                .copied()
                .unwrap_or(0);
            let mut code = String::new();
            writeln!(
                code,
                "  %{prefix}_adt = call ptr @su_adt_new(i32 {tag}, ptr null)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_adt"),
                is_io: false,
            }
        }
        Expr::Var(name) => {
            let val = locals
                .get(name)
                .cloned()
                .unwrap_or_else(|| "null".into());
            Emitted {
                code: String::new(),
                value: val,
                is_io: false,
            }
        }
        Expr::Let { name, value, body } => {
            let ve = emit_expr(value, strs, enum_tags, cont_id, conts, locals, &format!("{prefix}_lv"));
            let mut code = ve.code;
            locals.insert(name.clone(), ve.value.clone());
            let be = emit_expr(body, strs, enum_tags, cont_id, conts, locals, prefix);
            locals.remove(name);
            code.push_str(&be.code);
            Emitted {
                code,
                value: be.value,
                is_io: be.is_io,
            }
        }
        Expr::Match { scrutinee, arms } => {
            let se = emit_expr(
                scrutinee,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_sc"),
            );
            let id = *cont_id;
            *cont_id += 1;
            let mut code = se.code;
            writeln!(
                code,
                "  %{prefix}_tag = call i32 @su_adt_tag(ptr {})",
                se.value
            )
            .unwrap();

            // Emit each arm into its own block; phi the resulting IO ptr.
            let merge = format!("{prefix}_merge_{id}");
            let mut arm_blocks = Vec::new();
            for (i, arm) in arms.iter().enumerate() {
                let label = format!("{prefix}_arm_{id}_{i}");
                arm_blocks.push((label.clone(), arm));
                let _ = i;
            }
            let default_label = format!("{prefix}_default_{id}");

            // switch
            write!(code, "  switch i32 %{prefix}_tag, label %{default_label} [").unwrap();
            for (i, arm) in arms.iter().enumerate() {
                if let Pattern::Adt {
                    enum_name,
                    case_name,
                } = &arm.pattern
                {
                    if let Some(tag) = enum_tags.get(&(enum_name.clone(), case_name.clone())) {
                        write!(code, " i32 {tag}, label %{prefix}_arm_{id}_{i}").unwrap();
                    }
                }
            }
            writeln!(code, " ]").unwrap();

            let mut phi_parts = Vec::new();
            for (i, arm) in arms.iter().enumerate() {
                if matches!(arm.pattern, Pattern::Wildcard) {
                    continue;
                }
                let label = format!("{prefix}_arm_{id}_{i}");
                writeln!(code, "{label}:").unwrap();
                let ae = emit_expr(
                    &arm.body,
                    strs,
                    enum_tags,
                    cont_id,
                    conts,
                    locals,
                    &format!("{prefix}_a{id}_{i}"),
                );
                code.push_str(&ae.code);
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
                    strs,
                    enum_tags,
                    cont_id,
                    conts,
                    locals,
                    &format!("{prefix}_aw{id}"),
                );
                code.push_str(&ae.code);
                writeln!(code, "  br label %{merge}").unwrap();
                phi_parts.push((ae.value, default_label));
            } else {
                writeln!(
                    code,
                    "  %{prefix}_dflt = call ptr @su_io_pure(ptr null)"
                )
                .unwrap();
                writeln!(code, "  br label %{merge}").unwrap();
                phi_parts.push((format!("%{prefix}_dflt"), default_label));
            }

            writeln!(code, "{merge}:").unwrap();
            write!(code, "  %{prefix}_phi = phi ptr").unwrap();
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
                is_io: true,
            }
        }
        Expr::FlatMap { inner, body } => {
            let id = *cont_id;
            *cont_id += 1;
            let cont_name = format!("su_cont_{id}");

            let mut body_locals = HashMap::new();
            let body_emitted = emit_expr(
                body,
                strs,
                enum_tags,
                cont_id,
                conts,
                &mut body_locals,
                &format!("c{id}"),
            );
            writeln!(
                conts,
                "define internal ptr @{cont_name}(ptr %value, ptr %env) {{"
            )
            .unwrap();
            writeln!(conts, "entry:").unwrap();
            conts.push_str(&body_emitted.code);
            let ret = if body_emitted.is_io {
                body_emitted.value
            } else {
                writeln!(
                    conts,
                    "  %c{id}_wrap = call ptr @su_io_pure(ptr {})",
                    body_emitted.value
                )
                .unwrap();
                format!("%c{id}_wrap")
            };
            writeln!(conts, "  ret ptr {ret}").unwrap();
            writeln!(conts, "}}").unwrap();
            writeln!(conts).unwrap();

            let inner_emitted = emit_expr(
                inner,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_in"),
            );
            let mut code = inner_emitted.code;
            writeln!(
                code,
                "  %{prefix}_fm = call ptr @su_io_flatmap(ptr {}, ptr @{cont_name}, ptr null)",
                inner_emitted.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_fm"),
                is_io: true,
            }
        }
        Expr::HandleErrorWith { inner, body } => {
            let id = *cont_id;
            *cont_id += 1;
            let cont_name = format!("su_err_{id}");
            let mut body_locals = HashMap::new();
            let body_emitted = emit_expr(
                body,
                strs,
                enum_tags,
                cont_id,
                conts,
                &mut body_locals,
                &format!("e{id}"),
            );
            // SuErrorHandler: ptr (ptr err, ptr env)
            writeln!(
                conts,
                "define internal ptr @{cont_name}(ptr %err, ptr %env) {{"
            )
            .unwrap();
            writeln!(conts, "entry:").unwrap();
            conts.push_str(&body_emitted.code);
            let ret = if body_emitted.is_io {
                body_emitted.value
            } else {
                writeln!(
                    conts,
                    "  %e{id}_wrap = call ptr @su_io_pure(ptr {})",
                    body_emitted.value
                )
                .unwrap();
                format!("%e{id}_wrap")
            };
            writeln!(conts, "  ret ptr {ret}").unwrap();
            writeln!(conts, "}}").unwrap();
            writeln!(conts).unwrap();

            let inner_emitted = emit_expr(
                inner,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_he"),
            );
            let mut code = inner_emitted.code;
            writeln!(
                code,
                "  %{prefix}_h = call ptr @su_io_handle_error_with(ptr {}, ptr @{cont_name}, ptr null)",
                inner_emitted.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_h"),
                is_io: true,
            }
        }
        Expr::Attempt { inner } => {
            let inner_emitted = emit_expr(
                inner,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_at"),
            );
            let mut code = inner_emitted.code;
            writeln!(
                code,
                "  %{prefix}_attempt = call ptr @su_io_attempt(ptr {})",
                inner_emitted.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_attempt"),
                is_io: true,
            }
        }
        Expr::IoRace { left, right } => {
            let le = emit_expr(
                left,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_rl"),
            );
            let re = emit_expr(
                right,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_rr"),
            );
            let mut code = le.code;
            code.push_str(&re.code);
            writeln!(
                code,
                "  %{prefix}_race = call ptr @su_io_race(ptr {}, ptr {})",
                le.value, re.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_race"),
                is_io: true,
            }
        }
        Expr::IoBoth { left, right } => {
            let le = emit_expr(
                left,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_bl"),
            );
            let re = emit_expr(
                right,
                strs,
                enum_tags,
                cont_id,
                conts,
                locals,
                &format!("{prefix}_br"),
            );
            let mut code = le.code;
            code.push_str(&re.code);
            writeln!(
                code,
                "  %{prefix}_both = call ptr @su_io_both(ptr {}, ptr {})",
                le.value, re.value
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_both"),
                is_io: true,
            }
        }
    }
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    #[test]
    fn emit_contains_main_and_println() {
        let p = parse(r#"@main def main: IO[Unit] = IO.println("Hi")"#).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("define i32 @main()"));
        assert!(ir.contains("su_io_println"));
        assert!(ir.contains("su_runtime_main"));
    }

    #[test]
    fn emit_enum_match() {
        let src = r#"
enum Color { case Red, case Blue }
@main def main: IO[Unit] =
  val c = Color.Red
  c match {
    case Color.Red => IO.println("red")
    case Color.Blue => IO.println("blue")
  }
"#;
        let p = parse(src).unwrap();
        let ir = emit_llvm(&p);
        assert!(ir.contains("su_adt_new"));
        assert!(ir.contains("su_adt_tag"));
        assert!(ir.contains("switch i32"));
    }
}

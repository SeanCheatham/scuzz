use crate::ast::{Expr, Program};
use std::fmt::Write as _;

/// Emit LLVM IR text for a Stage-0 program. Links against `libscalui_rt`.
/// Uses opaque pointers (LLVM 15+ / Clang 18).
pub fn emit_llvm(program: &Program) -> String {
    let mut out = String::new();
    let mut strs: Vec<String> = Vec::new();
    collect_strings(&program.main.body, &mut strs);

    writeln!(out, "; ScalUI Stage-0 generated LLVM IR").unwrap();
    writeln!(out, "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"").unwrap();
    writeln!(out, "target triple = \"x86_64-pc-linux-gnu\"").unwrap();
    writeln!(out).unwrap();

    writeln!(out, "declare ptr @su_string_from_cstr(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_println(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_pure(ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_delay(ptr, ptr)").unwrap();
    writeln!(out, "declare ptr @su_io_flatmap(ptr, ptr, ptr)").unwrap();
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

    let mut cont_id = 0usize;
    let mut conts = String::new();
    let body_expr = emit_expr(
        &program.main.body,
        &strs,
        &mut cont_id,
        &mut conts,
        "build",
    );

    out.push_str(&conts);

    writeln!(out, "define i32 @main() {{").unwrap();
    writeln!(out, "entry:").unwrap();
    out.push_str(&body_expr.code);
    writeln!(
        out,
        "  %rc = call i32 @su_runtime_main(ptr {})",
        body_expr.value
    )
    .unwrap();
    writeln!(out, "  ret i32 %rc").unwrap();
    writeln!(out, "}}").unwrap();

    out
}

struct Emitted {
    code: String,
    value: String,
}

fn collect_strings(expr: &Expr, out: &mut Vec<String>) {
    match expr {
        Expr::IoPrintln(s) | Expr::UiRunHeadless(s) => {
            if !out.contains(s) {
                out.push(s.clone());
            }
        }
        Expr::FlatMap { inner, body } => {
            collect_strings(inner, out);
            collect_strings(body, out);
        }
        Expr::IoDelayUnit | Expr::Unit | Expr::UiRunCounter | Expr::UiRunTodo => {}
    }
}

fn str_index(strs: &[String], s: &str) -> usize {
    strs.iter().position(|x| x == s).expect("string collected")
}

fn emit_expr(
    expr: &Expr,
    strs: &[String],
    cont_id: &mut usize,
    conts: &mut String,
    prefix: &str,
) -> Emitted {
    match expr {
        Expr::Unit => {
            let mut code = String::new();
            writeln!(code, "  %{prefix}_unit = call ptr @su_io_pure(ptr null)").unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_unit"),
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
            // Width/height come from SCALUI_UI_WIDTH / HEIGHT env at runtime defaults in C;
            // Stage-0 passes 0,0 to mean "use defaults / env".
            writeln!(
                code,
                "  %{prefix}_io = call ptr @su_ui_run_headless_label(ptr %{prefix}_sptr, i32 0, i32 0)"
            )
            .unwrap();
            Emitted {
                code,
                value: format!("%{prefix}_io"),
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
            }
        }
        Expr::FlatMap { inner, body } => {
            let id = *cont_id;
            *cont_id += 1;
            let cont_name = format!("su_cont_{id}");

            let body_emitted = emit_expr(body, strs, cont_id, conts, &format!("c{id}"));
            writeln!(
                conts,
                "define internal ptr @{cont_name}(ptr %value, ptr %env) {{"
            )
            .unwrap();
            writeln!(conts, "entry:").unwrap();
            conts.push_str(&body_emitted.code);
            writeln!(conts, "  ret ptr {}", body_emitted.value).unwrap();
            writeln!(conts, "}}").unwrap();
            writeln!(conts).unwrap();

            let inner_emitted = emit_expr(inner, strs, cont_id, conts, &format!("{prefix}_in"));
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
}

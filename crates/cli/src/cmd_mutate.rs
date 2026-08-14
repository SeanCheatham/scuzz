use crate::support::{compile_opts, resolve_dir};
use anyhow::{bail, Context, Result};
use scuzz_compiler::compile_prepared_program;
use scuzz_compiler::driver::load_verify_program;
use scuzz_compiler::fuzz::{count_prefix_lines, fuzz_script, lines_nonempty};
use scuzz_compiler::mutate::{mutate_apply, mutate_count};
use std::path::Path;
use std::process::{Command, ExitCode};

pub fn cmd_mutate(path: &Path, limit: i64, iters: i64, seed: i64) -> Result<ExitCode> {
    if limit <= 0 {
        bail!("mutate --limit N requires N > 0");
    }
    if iters < 0 {
        bail!("mutate --iters N requires N >= 0");
    }
    let project_dir = resolve_dir(path)?;
    let (prog, manifest) = load_verify_program(&project_dir)?;
    let total = mutate_count(&prog) as i64;
    if total == 0 {
        println!("scuzz mutate: no residual Law.check / Law.assert / .require sites");
        return Ok(ExitCode::SUCCESS);
    }
    let take = total.min(limit);
    println!(
        "scuzz mutate: {take} of {total} residual oracle sites (negate/flip/0-1/arith/drop); idle + {iters} fuzz iters"
    );
    let mut killed = 0i64;
    let mut survived = 0i64;
    for i in 0..take {
        let out_dir = project_dir.join("build").join("mutate").join(i.to_string());
        let mutant = mutate_apply(prog.clone(), i as i32);
        let opts = compile_opts(&project_dir, &out_dir, false, true)?;
        let compiled = compile_prepared_program(&opts, mutant)?;
        let w = manifest.ui.as_ref().map(|u| u.width()).unwrap_or(200);
        let h = manifest.ui.as_ref().map(|u| u.height()).unwrap_or(120);
        let with_ui = manifest.ui.is_some();
        let code = mutate_exec(
            &compiled.executable,
            &out_dir,
            with_ui,
            w,
            h,
            i,
            iters,
            seed,
        )?;
        if code == 0 {
            println!("  mutant {i}: survived");
            survived += 1;
        } else {
            println!("  mutant {i}: killed");
            killed += 1;
        }
    }
    println!("scuzz mutate: {killed} killed, {survived} survived ({take} ran)");
    if survived == 0 {
        println!("scuzz mutate ok");
        Ok(ExitCode::SUCCESS)
    } else {
        println!(
            "surviving mutants mean weak or unreachable residual oracles; rerun: scuzz mutate --limit {take} --iters {iters}"
        );
        bail!("mutate survivors");
    }
}

fn mutate_iter_seed(seed: i64, mutant_index: i64, iter: i64) -> i64 {
    seed + mutant_index * 32 + iter
}

fn mutate_exec(
    exe: &Path,
    out_dir: &Path,
    with_ui: bool,
    w: i32,
    h: i32,
    mutant_index: i64,
    iters: i64,
    seed: i64,
) -> Result<i32> {
    if with_ui {
        mutate_exec_ui(exe, out_dir, w, h, mutant_index, iters, seed)
    } else {
        mutate_exec_io(exe, out_dir, mutant_index, iters, seed)
    }
}

fn mutate_exec_ui(
    exe: &Path,
    out_dir: &Path,
    w: i32,
    h: i32,
    mutant_index: i64,
    iters: i64,
    seed: i64,
) -> Result<i32> {
    let code = mutate_exec_ui_events(exe, out_dir, w, h, &[], "")?;
    if code != 0 {
        return Ok(code);
    }
    if iters <= 0 {
        return Ok(0);
    }
    let dump = std::fs::read_to_string(out_dir.join("dump.txt")).unwrap_or_default();
    let n_buttons = count_prefix_lines(&dump, "button:");
    let n_fields = count_prefix_lines(&dump, "textfield:");
    let n_scrolls = count_prefix_lines(&dump, "scroll:");
    if n_buttons + n_fields + n_scrolls == 0 {
        return Ok(0);
    }
    let drivers =
        lines_nonempty(&std::fs::read_to_string(out_dir.join("drivers.txt")).unwrap_or_default());
    for iter in 0..iters {
        let s = mutate_iter_seed(seed, mutant_index, iter);
        let events = fuzz_script(s, n_buttons, n_fields > 0, n_scrolls > 0, &drivers);
        let code = mutate_exec_ui_events(exe, out_dir, w, h, &events, &s.to_string())?;
        if code != 0 {
            return Ok(code);
        }
    }
    Ok(0)
}

fn mutate_exec_ui_events(
    exe: &Path,
    out_dir: &Path,
    w: i32,
    h: i32,
    events: &[String],
    schedule_seed: &str,
) -> Result<i32> {
    let script = out_dir.join("script.txt");
    let dump = out_dir.join("dump.txt");
    let reached = out_dir.join("sometimes.reached");
    let text = if events.is_empty() {
        String::new()
    } else {
        let mut s = events.join("\n");
        s.push('\n');
        s
    };
    std::fs::write(&script, text)?;
    std::fs::write(&dump, "")?;
    std::fs::write(&reached, "")?;
    let mut cmd = Command::new(exe);
    cmd.env("SCUZZ_UI_RUNTIME", "headless")
        .env("SCUZZ_TESTRT", "1")
        .env("SCUZZ_UI_SCRIPT", &script)
        .env("SCUZZ_FUZZ_DUMP", &dump)
        .env("SCUZZ_UI_WIDTH", w.to_string())
        .env("SCUZZ_UI_HEIGHT", h.to_string())
        .env("SCUZZ_SOMETIMES_DUMP", &reached);
    if !schedule_seed.is_empty() {
        cmd.env("SCUZZ_SCHED_SEED", schedule_seed);
    }
    let status = cmd
        .status()
        .with_context(|| format!("running {}", exe.display()))?;
    Ok(status.code().unwrap_or(1))
}

fn mutate_exec_io(
    exe: &Path,
    out_dir: &Path,
    mutant_index: i64,
    iters: i64,
    seed: i64,
) -> Result<i32> {
    let code = mutate_exec_io_at(exe, out_dir, "")?;
    if code != 0 {
        return Ok(code);
    }
    for iter in 0..iters {
        let s = mutate_iter_seed(seed, mutant_index, iter);
        let code = mutate_exec_io_at(exe, out_dir, &s.to_string())?;
        if code != 0 {
            return Ok(code);
        }
    }
    Ok(0)
}

fn mutate_exec_io_at(exe: &Path, out_dir: &Path, schedule_seed: &str) -> Result<i32> {
    let reached = out_dir.join("sometimes.reached");
    std::fs::write(&reached, "")?;
    let mut cmd = Command::new(exe);
    cmd.env("SCUZZ_TESTRT", "1")
        .env("SCUZZ_SOMETIMES_DUMP", &reached);
    if !schedule_seed.is_empty() {
        cmd.env("SCUZZ_SCHED_SEED", schedule_seed);
    }
    let status = cmd
        .status()
        .with_context(|| format!("running {}", exe.display()))?;
    Ok(status.code().unwrap_or(1))
}

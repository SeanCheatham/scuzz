use crate::support::{compile_opts, resolve_dir, run_testrt, TestrtUi};
use anyhow::{bail, Result};
use scuzz_compiler::compile_prepared_program;
use scuzz_compiler::driver::load_verify_program;
use scuzz_compiler::fuzz::{
    count_dump_section, drive_script_lines, fuzz_script, lines_nonempty, script_text,
};
use scuzz_compiler::mutate::{mutate_apply_mode, mutate_count_mode, MutateMode};
use std::path::Path;
use std::process::ExitCode;

pub fn cmd_mutate(
    path: &Path,
    limit: i64,
    iters: i64,
    seed: i64,
    oracles: bool,
) -> Result<ExitCode> {
    if limit <= 0 {
        bail!("mutate --limit N requires N > 0");
    }
    if iters < 0 {
        bail!("mutate --iters N requires N >= 0");
    }
    let project_dir = resolve_dir(path)?;
    let (prog, manifest) = load_verify_program(&project_dir)?;
    let mode = if oracles {
        MutateMode::Oracles
    } else {
        MutateMode::Program
    };
    let total = mutate_count_mode(&prog, mode) as i64;
    if total == 0 {
        if oracles {
            println!("scuzz mutate: no residual Law.check / Law.assert / .require sites");
        } else {
            println!("scuzz mutate: no live-code mutation sites");
        }
        return Ok(ExitCode::SUCCESS);
    }
    let take = total.min(limit);
    let kind = if oracles {
        "residual oracle sites (negate/flip/0-1/arith/drop)"
    } else {
        "live-code sites (flip/if/0-1/sibling)"
    };
    println!("scuzz mutate: {take} of {total} {kind}; idle + {iters} fuzz iters");
    let mut killed = 0i64;
    let mut survived = 0i64;
    for i in 0..take {
        let out_dir = project_dir.join("build").join("mutate").join(i.to_string());
        let mutant = mutate_apply_mode(prog.clone(), i as i32, mode);
        let opts = compile_opts(&project_dir, &out_dir, false, true)?;
        let compiled = compile_prepared_program(&opts, mutant)?;
        let ui = manifest.ui.as_ref().map(|u| (u.width(), u.height()));
        let code = mutate_exec(&compiled.executable, &out_dir, ui, i, iters, seed)?;
        if code == 0 {
            println!("  mutant {i}: survived");
            survived += 1;
        } else {
            println!("  mutant {i}: killed");
            killed += 1;
        }
    }
    println!("scuzz mutate: {killed} killed, {survived} survived ({take} ran)");
    std::fs::create_dir_all(project_dir.join("build").join("mutate"))?;
    std::fs::write(
        project_dir.join("build").join("mutate").join("summary.toml"),
        format!(
            "[mutate]\nkilled = {killed}\nsurvived = {survived}\nran = {take}\nseed = {seed}\niters = {iters}\noracles = {oracles}\n"
        ),
    )?;
    if survived == 0 {
        println!("scuzz mutate ok");
        Ok(ExitCode::SUCCESS)
    } else {
        let flag = if oracles { " --oracles" } else { "" };
        println!(
            "surviving mutants mean weak or unreachable residual oracles; rerun: scuzz mutate{flag} --limit {take} --iters {iters}"
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
    ui: Option<(i32, i32)>,
    mutant_index: i64,
    iters: i64,
    seed: i64,
) -> Result<i32> {
    match ui {
        Some((w, h)) => mutate_exec_ui(exe, out_dir, w, h, mutant_index, iters, seed),
        None => mutate_exec_io(exe, out_dir, mutant_index, iters, seed),
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
    let n_taps = count_dump_section(&dump, "[taps]");
    let n_fields = count_dump_section(&dump, "[fields]");
    let n_scrolls = count_dump_section(&dump, "[scrolls]");
    let drivers =
        lines_nonempty(&std::fs::read_to_string(out_dir.join("drivers.txt")).unwrap_or_default());
    if n_taps + n_fields + n_scrolls == 0 && drivers.is_empty() {
        return Ok(0);
    }
    for iter in 0..iters {
        let s = mutate_iter_seed(seed, mutant_index, iter);
        let events = fuzz_script(s, n_taps, n_fields > 0, n_scrolls > 0, &drivers);
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
    let text = script_text(events);
    std::fs::write(&script, text)?;
    std::fs::write(&dump, "")?;
    std::fs::write(&reached, "")?;
    run_testrt(
        exe,
        &reached,
        schedule_seed,
        Some(TestrtUi {
            script: &script,
            dump: &dump,
            width: w,
            height: h,
        }),
        None,
    )
}

fn mutate_exec_io(
    exe: &Path,
    out_dir: &Path,
    mutant_index: i64,
    iters: i64,
    seed: i64,
) -> Result<i32> {
    let code = mutate_exec_io_at(exe, out_dir, "", &[])?;
    if code != 0 {
        return Ok(code);
    }
    let drivers =
        lines_nonempty(&std::fs::read_to_string(out_dir.join("drivers.txt")).unwrap_or_default());
    for iter in 0..iters {
        let s = mutate_iter_seed(seed, mutant_index, iter);
        let drives = drive_script_lines(s, &drivers);
        let code = mutate_exec_io_at(exe, out_dir, &s.to_string(), &drives)?;
        if code != 0 {
            return Ok(code);
        }
    }
    Ok(0)
}

fn mutate_exec_io_at(
    exe: &Path,
    out_dir: &Path,
    schedule_seed: &str,
    drives: &[String],
) -> Result<i32> {
    let reached = out_dir.join("sometimes.reached");
    let drive_path = out_dir.join("drive.txt");
    std::fs::write(&reached, "")?;
    std::fs::write(&drive_path, script_text(drives))?;
    let drive = if drives.is_empty() {
        None
    } else {
        Some(drive_path.as_path())
    };
    crate::support::run_testrt(exe, &reached, schedule_seed, None, drive)
}

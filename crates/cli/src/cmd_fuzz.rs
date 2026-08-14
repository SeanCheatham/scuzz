use crate::support::{compile_opts, resolve_dir};
use anyhow::{bail, Context, Result};
use scuzz_compiler::compile_project;
use scuzz_compiler::fuzz::{
    corpus_keep, corpus_push, count_prefix_lines, dump_push, exhaust_alphabet, fuzz_pick_sched,
    fuzz_pick_script, lines_nonempty, missing_from, parse_repro, repro_text, sched_push,
    script_text,
};
use scuzz_compiler::manifest::load_manifest;
use std::path::Path;
use std::process::{Command, ExitCode};

pub fn cmd_fuzz(
    path: &Path,
    replay: Option<&Path>,
    iters: i64,
    seed: i64,
    exhaust: bool,
    depth: Option<i64>,
) -> Result<ExitCode> {
    if let Some(replay) = replay {
        return fuzz_replay(path, replay);
    }
    if exhaust {
        let depth = depth.ok_or_else(|| anyhow::anyhow!("fuzz --exhaust requires --depth N"))?;
        if depth <= 0 {
            bail!("fuzz --exhaust --depth N requires N > 0");
        }
        return fuzz_run(path, iters, seed, true, depth);
    }
    if depth.is_some() {
        bail!("fuzz --depth requires --exhaust");
    }
    fuzz_run(path, iters, seed, false, 0)
}

fn fuzz_run(path: &Path, iters: i64, seed: i64, exhaust: bool, depth: i64) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    let is_ui = manifest.ui.is_some();
    if exhaust && !is_ui {
        bail!("fuzz --exhaust requires a [ui] project (event alphabet)");
    }
    let out = compile_project(&compile_opts(&project_dir, Path::new("build"), true, true)?)?;
    let fuzz_dir = project_dir.join("build").join("fuzz");
    std::fs::create_dir_all(&fuzz_dir)?;
    std::fs::write(fuzz_dir.join("sometimes.campaign"), "")?;
    let w = manifest.ui.as_ref().map(|u| u.width()).unwrap_or(200);
    let h = manifest.ui.as_ref().map(|u| u.height()).unwrap_or(120);
    if is_ui {
        fuzz_probe(
            &out.executable,
            &fuzz_dir,
            &project_dir,
            w,
            h,
            seed,
            iters,
            exhaust,
            depth,
        )
    } else {
        fuzz_io_loop(&out.executable, &fuzz_dir, &project_dir, seed, iters)
    }
}

fn fuzz_replay(path: &Path, replay_path: &Path) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    let is_ui = manifest.ui.is_some();
    let out = compile_project(&compile_opts(&project_dir, Path::new("build"), true, true)?)?;
    let fuzz_dir = project_dir.join("build").join("fuzz");
    std::fs::create_dir_all(&fuzz_dir)?;
    std::fs::write(fuzz_dir.join("sometimes.campaign"), "")?;
    let w = manifest.ui.as_ref().map(|u| u.width()).unwrap_or(200);
    let h = manifest.ui.as_ref().map(|u| u.height()).unwrap_or(120);
    let text = std::fs::read_to_string(replay_path)
        .with_context(|| format!("reading {}", replay_path.display()))?;
    let repro = parse_repro(&text);
    let sched_note = match &repro.schedule_seed {
        Some(s) => format!(", schedule_seed {s})"),
        None => ")".into(),
    };
    println!(
        "scuzz fuzz --replay {} ({} events, seed {}{}",
        replay_path.display(),
        repro.events.len(),
        repro.seed,
        sched_note
    );
    let sched = repro.schedule_seed.clone().unwrap_or_default();
    let code = if is_ui {
        fuzz_exec(&out.executable, &fuzz_dir, w, h, &repro.events, &sched)?
    } else {
        fuzz_exec_io(&out.executable, &fuzz_dir, &sched)?
    };
    if code == 0 {
        println!("fuzz replay ok (no failure)");
        Ok(ExitCode::SUCCESS)
    } else {
        println!("fuzz replay reproduced a failure");
        bail!("fuzz replay failure");
    }
}

fn fuzz_probe(
    exe: &Path,
    fuzz_dir: &Path,
    project_dir: &Path,
    w: i32,
    h: i32,
    seed: i64,
    iters: i64,
    exhaust: bool,
    depth: i64,
) -> Result<ExitCode> {
    let code = fuzz_exec(exe, fuzz_dir, w, h, &[], "")?;
    if code != 0 {
        bail!("fuzz probe failed: app fails under TestRuntime before any event");
    }
    let dump = std::fs::read_to_string(fuzz_dir.join("dump.txt")).unwrap_or_default();
    let n_buttons = count_prefix_lines(&dump, "button:");
    let n_fields = count_prefix_lines(&dump, "textfield:");
    let n_scrolls = count_prefix_lines(&dump, "scroll:");
    let drivers = read_drivers(project_dir);
    if n_buttons + n_fields + n_scrolls == 0 {
        check_sometimes_campaign(project_dir, fuzz_dir)?;
        println!("scuzz fuzz ok (no buttons, text fields, or scrolls; probe only)");
        return Ok(ExitCode::SUCCESS);
    }
    if exhaust {
        fuzz_exhaust(
            exe,
            fuzz_dir,
            project_dir,
            w,
            h,
            n_buttons,
            n_fields > 0,
            n_scrolls > 0,
            &drivers,
            depth,
        )
    } else {
        fuzz_loop(
            exe,
            fuzz_dir,
            project_dir,
            w,
            h,
            n_buttons,
            n_fields > 0,
            n_scrolls > 0,
            &drivers,
            seed,
            iters,
            vec![],
            vec![dump],
        )
    }
}

fn fuzz_loop(
    exe: &Path,
    fuzz_dir: &Path,
    project_dir: &Path,
    w: i32,
    h: i32,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
    seed: i64,
    iters: i64,
    mut corpus: Vec<Vec<String>>,
    mut seen: Vec<String>,
) -> Result<ExitCode> {
    for iter in 0..iters {
        let events = fuzz_pick_script(
            seed + iter,
            n_buttons,
            has_text,
            has_scroll,
            drivers,
            &corpus,
        );
        let sched = seed + iter;
        let old_camp = lines_nonempty(
            &std::fs::read_to_string(fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
        );
        let code = fuzz_exec(exe, fuzz_dir, w, h, &events, &sched.to_string())?;
        if code != 0 {
            return fuzz_fail(project_dir, fuzz_dir, seed + iter, sched, iter, &events);
        }
        let reached = lines_nonempty(
            &std::fs::read_to_string(fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
        );
        let dump = std::fs::read_to_string(fuzz_dir.join("dump.txt")).unwrap_or_default();
        if corpus_keep(&reached, &old_camp, &dump, &seen) {
            dump_push(&mut seen, dump);
            corpus_push(&mut corpus, events);
        }
    }
    check_sometimes_campaign(project_dir, fuzz_dir)?;
    println!(
        "scuzz fuzz ok ({} scripts, seed {}, {} corpus prefixes)",
        iters,
        seed,
        corpus.len()
    );
    Ok(ExitCode::SUCCESS)
}

fn fuzz_io_loop(
    exe: &Path,
    fuzz_dir: &Path,
    project_dir: &Path,
    seed: i64,
    iters: i64,
) -> Result<ExitCode> {
    let mut corpus: Vec<String> = Vec::new();
    for iter in 0..iters {
        let sched = fuzz_pick_sched(seed, iter, &corpus);
        let old_camp = lines_nonempty(
            &std::fs::read_to_string(fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
        );
        let code = fuzz_exec_io(exe, fuzz_dir, &sched)?;
        if code != 0 {
            let sched_n: i64 = sched.parse().unwrap_or(0);
            return fuzz_fail(project_dir, fuzz_dir, seed + iter, sched_n, iter, &[]);
        }
        let reached = lines_nonempty(
            &std::fs::read_to_string(fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
        );
        if !missing_from(&reached, &old_camp).is_empty() {
            sched_push(&mut corpus, sched);
        }
    }
    check_sometimes_campaign(project_dir, fuzz_dir)?;
    println!(
        "scuzz fuzz ok ({} schedules, seed {}, {} corpus seeds)",
        iters,
        seed,
        corpus.len()
    );
    Ok(ExitCode::SUCCESS)
}

fn fuzz_exhaust(
    exe: &Path,
    fuzz_dir: &Path,
    project_dir: &Path,
    w: i32,
    h: i32,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
    max_depth: i64,
) -> Result<ExitCode> {
    let alphabet = exhaust_alphabet(n_buttons, has_text, has_scroll, drivers);
    let mut script_index: i64 = 0;
    for depth in 1..=max_depth {
        script_index = exhaust_extend(
            exe,
            fuzz_dir,
            project_dir,
            w,
            h,
            max_depth,
            depth,
            &alphabet,
            &[],
            script_index,
        )?;
    }
    check_sometimes_campaign(project_dir, fuzz_dir)?;
    println!(
        "scuzz fuzz --exhaust ok (depth {}, {} scripts)",
        max_depth, script_index
    );
    Ok(ExitCode::SUCCESS)
}

fn exhaust_extend(
    exe: &Path,
    fuzz_dir: &Path,
    project_dir: &Path,
    w: i32,
    h: i32,
    max_depth: i64,
    target_len: i64,
    alphabet: &[String],
    prefix: &[String],
    script_index: i64,
) -> Result<i64> {
    if prefix.len() as i64 == target_len {
        let code = fuzz_exec(exe, fuzz_dir, w, h, prefix, "")?;
        if code == 0 {
            return Ok(script_index + 1);
        }
        fuzz_exhaust_fail(project_dir, fuzz_dir, max_depth, script_index, prefix)?;
        return Ok(script_index);
    }
    let mut idx = script_index;
    for ev in alphabet {
        let mut next = prefix.to_vec();
        next.push(ev.clone());
        idx = exhaust_extend(
            exe,
            fuzz_dir,
            project_dir,
            w,
            h,
            max_depth,
            target_len,
            alphabet,
            &next,
            idx,
        )?;
    }
    Ok(idx)
}

fn fuzz_fail(
    project_dir: &Path,
    fuzz_dir: &Path,
    script_seed: i64,
    schedule_seed: i64,
    iter: i64,
    events: &[String],
) -> Result<ExitCode> {
    let repro = fuzz_dir.join("repro.toml");
    std::fs::write(
        &repro,
        repro_text(script_seed, &schedule_seed.to_string(), events),
    )?;
    println!(
        "fuzz failure at script {iter} (seed {script_seed}); wrote {}",
        repro.display()
    );
    println!(
        "replay: scuzz fuzz {} --replay {}",
        project_dir.display(),
        repro.display()
    );
    bail!("fuzz failure");
}

fn fuzz_exhaust_fail(
    project_dir: &Path,
    fuzz_dir: &Path,
    depth: i64,
    script_index: i64,
    events: &[String],
) -> Result<()> {
    let repro = fuzz_dir.join("repro.toml");
    std::fs::write(&repro, repro_text(depth, "", events))?;
    println!(
        "fuzz --exhaust failure at script {script_index} (depth {depth}); wrote {}",
        repro.display()
    );
    println!(
        "replay: scuzz fuzz {} --replay {}",
        project_dir.display(),
        repro.display()
    );
    bail!("fuzz exhaust failure");
}

fn check_sometimes_campaign(project_dir: &Path, fuzz_dir: &Path) -> Result<()> {
    let decl = lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("sometimes.declared"))
            .unwrap_or_default(),
    );
    let camp = lines_nonempty(
        &std::fs::read_to_string(fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
    );
    let missing: Vec<&String> = missing_from(&decl, &camp);
    if missing.is_empty() {
        Ok(())
    } else {
        let names: Vec<&str> = missing.iter().map(|s| s.as_str()).collect();
        bail!("Law.sometimes never reached: {}", names.join(", "));
    }
}

fn merge_sometimes(reached_path: &Path, campaign_path: &Path) -> Result<()> {
    let reached = lines_nonempty(&std::fs::read_to_string(reached_path).unwrap_or_default());
    let mut camp = lines_nonempty(&std::fs::read_to_string(campaign_path).unwrap_or_default());
    for n in reached {
        if !camp.iter().any(|c| c == &n) {
            camp.push(n);
        }
    }
    let text = if camp.is_empty() {
        String::new()
    } else {
        let mut s = camp.join("\n");
        s.push('\n');
        s
    };
    std::fs::write(campaign_path, text)?;
    Ok(())
}

fn fuzz_exec(
    exe: &Path,
    fuzz_dir: &Path,
    w: i32,
    h: i32,
    events: &[String],
    schedule_seed: &str,
) -> Result<i32> {
    let script = fuzz_dir.join("script.txt");
    let dump = fuzz_dir.join("dump.txt");
    let reached = fuzz_dir.join("sometimes.reached");
    std::fs::write(&script, script_text(events))?;
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
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    Ok(status.code().unwrap_or(1))
}

fn fuzz_exec_io(exe: &Path, fuzz_dir: &Path, schedule_seed: &str) -> Result<i32> {
    let reached = fuzz_dir.join("sometimes.reached");
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
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    Ok(status.code().unwrap_or(1))
}

fn read_drivers(project_dir: &Path) -> Vec<String> {
    lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("drivers.txt")).unwrap_or_default(),
    )
}

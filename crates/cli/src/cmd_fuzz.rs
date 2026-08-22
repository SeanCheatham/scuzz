use crate::support::{compile_opts, resolve_dir, run_testrt, TestrtUi};
use anyhow::{bail, Context, Result};
use scuzz_compiler::compile_prepared_program;
use scuzz_compiler::compile_project;
use scuzz_compiler::driver::load_verify_program;
use scuzz_compiler::fuzz::{
    corpus_entry_name, corpus_keep, corpus_push, corpus_sorted_names, count_dump_section,
    drive_script_lines, dump_push, exhaust_alphabet, fuzz_mutate_sites, fuzz_pick_sched,
    fuzz_pick_script, lines_nonempty, missing_from, parse_repro, repro_text, script_text, Repro,
};
use scuzz_compiler::manifest::load_manifest;
use scuzz_compiler::mutate::{mutate_apply_mode, mutate_count_mode, MutateMode};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

/// Shared fuzz target: executable, output directory, project directory, UI size.
struct FuzzCtx<'a> {
    exe: &'a Path,
    fuzz_dir: &'a Path,
    project_dir: &'a Path,
    w: i32,
    h: i32,
    is_ui: bool,
}

/// UI elements found by the probe. Drives the event alphabet.
struct FuzzTargets {
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    drivers: Vec<String>,
}

struct MutateStats {
    killed: i64,
    survived: i64,
    ran: i64,
    sites: i64,
    oracles: bool,
}

impl MutateStats {
    fn skipped(oracles: bool, sites: i64) -> Self {
        Self {
            killed: 0,
            survived: 0,
            ran: 0,
            sites,
            oracles,
        }
    }
}

/// Kept IO search: schedule seed plus the drive lines that ran with it.
struct IoKeep {
    sched: String,
    drives: Vec<String>,
}

/// Kept UI search: schedule seed plus the event prefix that ran with it.
struct UiKeep {
    sched: String,
    events: Vec<String>,
}

#[derive(Default)]
struct CorpusReport {
    entries: i64,
    failures: i64,
    reached: Vec<String>,
    promoted: i64,
}

/// Result fields written to summary.toml.
struct FuzzSummary<'a> {
    seed: i64,
    iterations: i64,
    search: i64,
    search_failures: i64,
    corpus: i64,
    ok: bool,
    drivers: &'a [String],
    events: &'a [String],
    declared: &'a [String],
    reached: &'a [String],
    missing_budget: &'a [String],
    missing_corpus: &'a [String],
    stored: &'a CorpusReport,
    repro: Option<&'a Path>,
    mutate: &'a MutateStats,
}

struct SearchRepro {
    path: PathBuf,
    events: Vec<String>,
}

struct Campaign {
    seed: i64,
    iterations: i64,
    oracles: bool,
    search_used: i64,
    mutate_slots: i64,
    fail_fast: bool,
    search_failures: i64,
    repro: Option<SearchRepro>,
    stored: CorpusReport,
    corpus_reached: Vec<String>,
    coverage_promoted: i64,
    declared_count: i64,
}

struct UiSearch {
    prefixes: Vec<UiKeep>,
    seen: Vec<String>,
}

pub fn cmd_fuzz(
    path: &Path,
    replay: Option<&Path>,
    iterations: i64,
    seed: i64,
    oracles: bool,
    no_fail_fast: bool,
) -> Result<ExitCode> {
    if let Some(replay) = replay {
        return fuzz_replay(path, replay);
    }
    if iterations < 0 {
        bail!("fuzz --iterations N requires N >= 0");
    }
    fuzz_run(path, iterations, seed, oracles, !no_fail_fast)
}

fn budget_split(n: i64) -> (i64, i64) {
    if n <= 0 {
        (0, 0)
    } else {
        let search = (2 * n / 3).max(1).min(n);
        (search, n - search)
    }
}

fn fuzz_run(
    path: &Path,
    iterations: i64,
    seed: i64,
    oracles: bool,
    fail_fast: bool,
) -> Result<ExitCode> {
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
    let ctx = FuzzCtx {
        exe: &out.executable,
        fuzz_dir: &fuzz_dir,
        project_dir: &project_dir,
        w: if is_ui { w } else { 0 },
        h: if is_ui { h } else { 0 },
        is_ui,
    };
    let (search_budget, mutate_slots) = budget_split(iterations);
    let declared_count = declared_names(&project_dir).len() as i64;
    let mut camp = Campaign {
        seed,
        iterations,
        oracles,
        search_used: 0,
        mutate_slots,
        fail_fast,
        search_failures: 0,
        repro: None,
        stored: CorpusReport::default(),
        corpus_reached: Vec::new(),
        coverage_promoted: 0,
        declared_count,
    };
    if iterations == 0 {
        println!("scuzz fuzz: corpus-only (--iterations 0)");
    }
    if is_ui {
        fuzz_ui_campaign(&ctx, &mut camp, search_budget)
    } else {
        fuzz_io_campaign(&ctx, &mut camp, search_budget)
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
    let repro = parse_repro(&text).map_err(|e| anyhow::anyhow!("repro.toml: {e}"))?;
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
        fuzz_exec_io(&out.executable, &fuzz_dir, &sched, &repro.events)?
    };
    if code == 0 {
        println!("fuzz replay ok (no failure)");
        Ok(ExitCode::SUCCESS)
    } else {
        println!("fuzz replay reproduced a failure");
        bail!("fuzz replay failure");
    }
}

fn fuzz_ui_campaign(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    search_budget: i64,
) -> Result<ExitCode> {
    let code = fuzz_exec(
        ctx.exe,
        ctx.fuzz_dir,
        ctx.w,
        ctx.h,
        &[],
        &camp.seed.to_string(),
    )?;
    if code != 0 {
        write_fail_summary(ctx, camp, 0)?;
        bail!("fuzz probe failed: app fails under TestRuntime before any event");
    }
    let dump = std::fs::read_to_string(ctx.fuzz_dir.join("dump.txt")).unwrap_or_default();
    let n_taps = count_dump_section(&dump, "[taps]");
    let n_fields = count_dump_section(&dump, "[fields]");
    let n_scrolls = count_dump_section(&dump, "[scrolls]");
    let drivers = read_drivers(ctx.project_dir);
    let targets = FuzzTargets {
        n_buttons: n_taps,
        n_fields,
        n_scrolls,
        drivers,
    };
    let mut search = UiSearch {
        prefixes: Vec::new(),
        seen: vec![dump],
    };
    replay_stored_corpus(ctx, camp, Some(&mut search), None)?;
    let mut remaining = search_budget;
    let alphabet = exhaust_alphabet(
        targets.n_buttons,
        targets.n_fields,
        targets.n_scrolls,
        &targets.drivers,
    );
    if !alphabet.is_empty() {
        let mut depth = 1i64;
        while let Some(need) = exhaust_need(alphabet.len(), depth) {
            if need == 0 || need > remaining {
                break;
            }
            exhaust_extend(ctx, camp, depth, &alphabet, &[], &mut search)?;
            remaining = search_budget - camp.search_used;
            depth += 1;
        }
    }
    if remaining > 0 {
        fuzz_loop(ctx, camp, &targets, remaining, &mut search)?;
        remaining = 0;
    }
    camp.mutate_slots += remaining;
    let corpus_len = search.prefixes.len() as i64;
    finish_campaign(ctx, camp, corpus_len, &search.prefixes, &[])
}

fn fuzz_io_campaign(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    search_budget: i64,
) -> Result<ExitCode> {
    let mut corpus: Vec<IoKeep> = Vec::new();
    let drivers = read_drivers(ctx.project_dir);
    replay_stored_corpus(ctx, camp, None, Some(&mut corpus))?;
    for iter in 0..search_budget {
        let sched_corpus: Vec<String> = corpus.iter().map(|k| k.sched.clone()).collect();
        let sched = fuzz_pick_sched(camp.seed, iter, &sched_corpus);
        let drives = drive_script_lines(camp.seed + iter, &drivers);
        let old_camp = lines_nonempty(
            &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
        );
        let code = fuzz_exec_io(ctx.exe, ctx.fuzz_dir, &sched, &drives)?;
        if code != 0 {
            camp.search_used = iter + 1;
            let sched_n: i64 = sched.parse().unwrap_or(0);
            note_search_fail(
                ctx,
                camp,
                camp.seed + iter,
                sched_n,
                iter,
                &drives,
                corpus.len() as i64,
            )?;
            continue;
        }
        let reached = lines_nonempty(
            &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
        );
        if !missing_from(&reached, &old_camp).is_empty() && !corpus.iter().any(|k| k.sched == sched)
        {
            corpus.insert(
                0,
                IoKeep {
                    sched: sched.clone(),
                    drives,
                },
            );
            corpus.truncate(32);
            maybe_promote_coverage(
                ctx,
                camp,
                camp.seed + iter,
                &sched,
                &corpus[0].drives,
                &reached,
            )?;
        }
    }
    camp.search_used = search_budget;
    let corpus_len = corpus.len() as i64;
    finish_campaign(ctx, camp, corpus_len, &[], &corpus)
}

fn fuzz_loop(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    targets: &FuzzTargets,
    remaining: i64,
    search: &mut UiSearch,
) -> Result<()> {
    let search_base = camp.search_used;
    for i in 0..remaining {
        let iter = search_base + i;
        let event_corpus: Vec<Vec<String>> =
            search.prefixes.iter().map(|k| k.events.clone()).collect();
        let events = fuzz_pick_script(
            camp.seed + iter,
            targets.n_buttons,
            targets.n_fields,
            targets.n_scrolls,
            &targets.drivers,
            &event_corpus,
        );
        let sched = camp.seed + iter;
        consider_ui_script(ctx, camp, iter, sched, &events, search)?;
    }
    Ok(())
}

fn exhaust_need(alpha: usize, depth: i64) -> Option<i64> {
    if depth <= 0 || alpha == 0 {
        return Some(0);
    }
    (alpha as i64).checked_pow(depth as u32)
}

fn exhaust_extend(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    target_len: i64,
    alphabet: &[String],
    prefix: &[String],
    search: &mut UiSearch,
) -> Result<()> {
    if prefix.len() as i64 == target_len {
        let iter = camp.search_used;
        let sched = camp.seed + iter;
        consider_ui_script(ctx, camp, iter, sched, prefix, search)?;
        return Ok(());
    }
    for ev in alphabet {
        let mut next = prefix.to_vec();
        next.push(ev.clone());
        exhaust_extend(ctx, camp, target_len, alphabet, &next, search)?;
    }
    Ok(())
}

fn consider_ui_script(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    iter: i64,
    sched: i64,
    events: &[String],
    search: &mut UiSearch,
) -> Result<()> {
    let old_camp = lines_nonempty(
        &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
    );
    let code = fuzz_exec(
        ctx.exe,
        ctx.fuzz_dir,
        ctx.w,
        ctx.h,
        events,
        &sched.to_string(),
    )?;
    camp.search_used += 1;
    if code != 0 {
        note_search_fail(
            ctx,
            camp,
            camp.seed + iter,
            sched,
            iter,
            events,
            search.prefixes.len() as i64,
        )?;
        return Ok(());
    }
    let reached = lines_nonempty(
        &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
    );
    let dump = std::fs::read_to_string(ctx.fuzz_dir.join("dump.txt")).unwrap_or_default();
    if corpus_keep(&reached, &old_camp, &dump, &search.seen) {
        let dump2_code = fuzz_exec(
            ctx.exe,
            ctx.fuzz_dir,
            ctx.w,
            ctx.h,
            events,
            &sched.to_string(),
        )?;
        if dump2_code != 0 {
            note_search_fail(
                ctx,
                camp,
                camp.seed + iter,
                sched,
                iter,
                events,
                search.prefixes.len() as i64,
            )?;
            return Ok(());
        }
        let dump2 = std::fs::read_to_string(ctx.fuzz_dir.join("dump.txt")).unwrap_or_default();
        if dump2 != dump {
            write_fail_summary(ctx, camp, search.prefixes.len() as i64)?;
            bail!("fuzz dump mismatch on replay (nondeterministic Headless dump)");
        }
        dump_push(&mut search.seen, dump);
        corpus_push(
            &mut search.prefixes,
            UiKeep {
                sched: sched.to_string(),
                events: events.to_vec(),
            },
        );
        maybe_promote_coverage(
            ctx,
            camp,
            camp.seed + iter,
            &sched.to_string(),
            events,
            &reached,
        )?;
    }
    Ok(())
}

fn finish_campaign(
    ctx: &FuzzCtx<'_>,
    camp: &Campaign,
    corpus: i64,
    corpus_ui: &[UiKeep],
    corpus_io: &[IoKeep],
) -> Result<ExitCode> {
    let declared = declared_names(ctx.project_dir);
    let reached = reached_names(ctx.fuzz_dir);
    let missing_budget = sometimes_missing_from(&declared, &reached);
    if camp.fail_fast && !missing_budget.is_empty() {
        return report_sometimes_fail(ctx, camp, corpus, &declared, &reached, &missing_budget);
    }
    mutate_then_finish(
        ctx,
        camp,
        corpus,
        corpus_ui,
        corpus_io,
        &declared,
        &reached,
        &missing_budget,
    )
}

fn sometimes_missing_from(declared: &[String], have: &[String]) -> Vec<String> {
    missing_from(declared, have).into_iter().cloned().collect()
}

fn report_sometimes_fail(
    ctx: &FuzzCtx<'_>,
    camp: &Campaign,
    corpus: i64,
    declared: &[String],
    reached: &[String],
    missing_budget: &[String],
) -> Result<ExitCode> {
    let mutate = mutate_stats_skip(ctx.project_dir, camp.oracles);
    let missing_corpus = sometimes_missing_from(declared, &camp.stored.reached);
    let repro = camp.repro.as_ref().map(|r| r.path.as_path());
    let events: &[String] = camp
        .repro
        .as_ref()
        .map(|r| r.events.as_slice())
        .unwrap_or(&[]);
    write_and_print(
        ctx,
        &FuzzSummary {
            seed: camp.seed,
            iterations: camp.iterations,
            search: camp.search_used,
            search_failures: camp.search_failures,
            corpus,
            ok: false,
            drivers: &read_drivers(ctx.project_dir),
            events,
            declared,
            reached,
            missing_budget,
            missing_corpus: &missing_corpus,
            stored: &camp.stored,
            repro,
            mutate: &mutate,
        },
    )?;
    bail_sometimes(missing_budget, &missing_corpus)
}

fn bail_sometimes(missing_budget: &[String], missing_corpus: &[String]) -> Result<ExitCode> {
    if !missing_corpus.is_empty() {
        bail!(
            "Property.sometimes reached by no stored corpus entry: {}",
            missing_corpus.join(", ")
        )
    }
    bail!(
        "Property.sometimes not reached in this budget: {}",
        missing_budget.join(", ")
    )
}

fn mutate_then_finish(
    ctx: &FuzzCtx<'_>,
    camp: &Campaign,
    corpus: i64,
    corpus_ui: &[UiKeep],
    corpus_io: &[IoKeep],
    declared: &[String],
    reached: &[String],
    missing_budget: &[String],
) -> Result<ExitCode> {
    let mutate = mutate_phase(
        ctx,
        camp.seed,
        camp.mutate_slots,
        camp.oracles,
        corpus_ui,
        corpus_io,
    )?;
    let missing_corpus = sometimes_missing_from(declared, &camp.stored.reached);
    let repro = camp.repro.as_ref().map(|r| r.path.as_path());
    let events: &[String] = camp
        .repro
        .as_ref()
        .map(|r| r.events.as_slice())
        .unwrap_or(&[]);
    let ok = camp.search_failures == 0 && missing_budget.is_empty() && mutate.survived == 0;
    write_and_print(
        ctx,
        &FuzzSummary {
            seed: camp.seed,
            iterations: camp.iterations,
            search: camp.search_used,
            search_failures: camp.search_failures,
            corpus,
            ok,
            drivers: &read_drivers(ctx.project_dir),
            events,
            declared,
            reached,
            missing_budget,
            missing_corpus: &missing_corpus,
            stored: &camp.stored,
            repro,
            mutate: &mutate,
        },
    )?;
    if ok {
        return Ok(ExitCode::SUCCESS);
    }
    if camp.search_failures > 0 {
        if let Some(repro) = &camp.repro {
            println!(
                "replay: scuzz fuzz {} --replay {}",
                ctx.project_dir.display(),
                repro.path.display()
            );
        }
    }
    if mutate.survived > 0 {
        let flag = if camp.oracles { " --oracles" } else { "" };
        println!(
            "surviving mutants mean weak or unreachable residual oracles; rerun: scuzz fuzz{flag} --iterations {}",
            camp.iterations
        );
    }
    if camp.search_failures > 0 {
        bail!("fuzz failure");
    }
    if !missing_budget.is_empty() {
        return bail_sometimes(missing_budget, &missing_corpus);
    }
    bail!("mutate survivors");
}

fn print_sometimes_missing(missing_budget: &[String], missing_corpus: &[String]) {
    if !missing_budget.is_empty() {
        println!(
            "Property.sometimes not reached in this budget: {}",
            missing_budget.join(", ")
        );
    }
    if !missing_corpus.is_empty() {
        println!(
            "Property.sometimes reached by no stored corpus entry: {}",
            missing_corpus.join(", ")
        );
    }
}

fn mutate_stats_skip(project_dir: &Path, oracles: bool) -> MutateStats {
    let sites = match load_verify_program(project_dir) {
        Ok((prog, _)) => {
            let mode = if oracles {
                MutateMode::Oracles
            } else {
                MutateMode::Program
            };
            mutate_count_mode(&prog, mode) as i64
        }
        Err(_) => 0,
    };
    MutateStats::skipped(oracles, sites)
}

fn mutate_phase(
    ctx: &FuzzCtx<'_>,
    seed: i64,
    slots: i64,
    oracles: bool,
    corpus_ui: &[UiKeep],
    corpus_io: &[IoKeep],
) -> Result<MutateStats> {
    let (prog, _manifest) = load_verify_program(ctx.project_dir)?;
    let mode = if oracles {
        MutateMode::Oracles
    } else {
        MutateMode::Program
    };
    let sites = mutate_count_mode(&prog, mode) as i64;
    if sites == 0 {
        if oracles {
            println!("scuzz fuzz: no residual Property.check / Property.assert / .require sites");
        } else {
            println!("scuzz fuzz: no live-code mutation sites");
        }
        return Ok(MutateStats::skipped(oracles, 0));
    }
    if slots <= 0 {
        return Ok(MutateStats::skipped(oracles, sites));
    }
    let take = sites.min(slots);
    let kind = if oracles {
        "residual oracle sites (negate/flip/0-1/arith/drop)"
    } else {
        "live-code sites (flip/if/0-1/sibling)"
    };
    println!("scuzz fuzz mutate: {take} of {sites} {kind}; idle + corpus replay");
    let idle_sched = seed.to_string();
    let site_idxs = fuzz_mutate_sites(seed, take, sites);
    let mut killed = 0i64;
    let mut survived = 0i64;
    for site in &site_idxs {
        let out_dir = ctx
            .project_dir
            .join("build")
            .join("fuzz")
            .join("mutate")
            .join(site.to_string());
        let mutant = mutate_apply_mode(prog.clone(), *site as i32, mode);
        let opts = compile_opts(ctx.project_dir, &out_dir, false, true)?;
        let compiled = compile_prepared_program(&opts, mutant)?;
        let code = if ctx.is_ui {
            mutate_exec_ui(
                &compiled.executable,
                &out_dir,
                ctx.w,
                ctx.h,
                corpus_ui,
                &idle_sched,
            )?
        } else {
            mutate_exec_io(&compiled.executable, &out_dir, corpus_io, &idle_sched)?
        };
        if code == 0 {
            println!("  mutant {site}: survived");
            survived += 1;
        } else {
            println!("  mutant {site}: killed");
            killed += 1;
        }
    }
    println!("scuzz fuzz mutate: {killed} killed, {survived} survived ({take} ran)");
    Ok(MutateStats {
        killed,
        survived,
        ran: take,
        sites,
        oracles,
    })
}

fn mutate_exec_ui(
    exe: &Path,
    out_dir: &Path,
    w: i32,
    h: i32,
    corpus: &[UiKeep],
    idle_sched: &str,
) -> Result<i32> {
    let code = mutate_exec_ui_events(exe, out_dir, w, h, &[], idle_sched)?;
    if code != 0 {
        return Ok(code);
    }
    for keep in corpus {
        let code = mutate_exec_ui_events(exe, out_dir, w, h, &keep.events, &keep.sched)?;
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
    std::fs::write(&script, script_text(events))?;
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

fn mutate_exec_io(exe: &Path, out_dir: &Path, corpus: &[IoKeep], idle_sched: &str) -> Result<i32> {
    let code = mutate_exec_io_at(exe, out_dir, idle_sched, &[])?;
    if code != 0 {
        return Ok(code);
    }
    for keep in corpus {
        let code = mutate_exec_io_at(exe, out_dir, &keep.sched, &keep.drives)?;
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
    run_testrt(exe, &reached, schedule_seed, None, drive)
}

fn shrink_events(
    exe: &Path,
    fuzz_dir: &Path,
    is_ui: bool,
    w: i32,
    h: i32,
    schedule_seed: &str,
    events: &[String],
) -> Result<Vec<String>> {
    let mut cur = events.to_vec();
    loop {
        let mut progressed = false;
        let mut i = 0;
        while i < cur.len() {
            let mut cand = cur.clone();
            cand.remove(i);
            let code = if is_ui {
                fuzz_exec(exe, fuzz_dir, w, h, &cand, schedule_seed)?
            } else {
                fuzz_exec_io(exe, fuzz_dir, schedule_seed, &cand)?
            };
            if code != 0 {
                cur = cand;
                progressed = true;
            } else {
                i += 1;
            }
        }
        if !progressed {
            break;
        }
    }
    Ok(cur)
}

fn write_fail_summary(ctx: &FuzzCtx<'_>, camp: &Campaign, corpus: i64) -> Result<()> {
    let mutate = mutate_stats_skip(ctx.project_dir, camp.oracles);
    let declared = declared_names(ctx.project_dir);
    let reached = reached_names(ctx.fuzz_dir);
    let missing_budget = sometimes_missing_from(&declared, &reached);
    let missing_corpus = sometimes_missing_from(&declared, &camp.stored.reached);
    let repro = camp.repro.as_ref().map(|r| r.path.as_path());
    let events: &[String] = camp
        .repro
        .as_ref()
        .map(|r| r.events.as_slice())
        .unwrap_or(&[]);
    write_and_print(
        ctx,
        &FuzzSummary {
            seed: camp.seed,
            iterations: camp.iterations,
            search: camp.search_used,
            search_failures: camp.search_failures,
            corpus,
            ok: false,
            drivers: &read_drivers(ctx.project_dir),
            events,
            declared: &declared,
            reached: &reached,
            missing_budget: &missing_budget,
            missing_corpus: &missing_corpus,
            stored: &camp.stored,
            repro,
            mutate: &mutate,
        },
    )
}

fn note_search_fail(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    script_seed: i64,
    schedule_seed: i64,
    iter: i64,
    events: &[String],
    corpus: i64,
) -> Result<()> {
    let dump_src = ctx.fuzz_dir.join("dump.txt");
    let dump_fail = ctx.fuzz_dir.join("dump.fail");
    if dump_src.exists() {
        std::fs::copy(&dump_src, &dump_fail)?;
    }
    let shrink_dir = ctx.fuzz_dir.join("shrink");
    std::fs::create_dir_all(&shrink_dir)?;
    std::fs::write(shrink_dir.join("sometimes.campaign"), "")?;
    let shrunk = shrink_events(
        ctx.exe,
        &shrink_dir,
        ctx.is_ui,
        ctx.w,
        ctx.h,
        &schedule_seed.to_string(),
        events,
    )?;
    camp.search_failures += 1;
    let first = camp.repro.is_none();
    if first {
        let path = ctx.fuzz_dir.join("repro.toml");
        std::fs::write(
            &path,
            repro_text(script_seed, &schedule_seed.to_string(), &shrunk),
        )?;
        println!(
            "fuzz failure at script {iter} (seed {script_seed}); wrote {} ({} events, shrunk from {})",
            path.display(),
            shrunk.len(),
            events.len()
        );
        if !shrunk.is_empty() {
            println!("shrunk events:");
            for ev in &shrunk {
                println!("  {ev}");
            }
        }
        println!(
            "replay: scuzz fuzz {} --replay {}",
            ctx.project_dir.display(),
            path.display()
        );
        let dump = std::fs::read_to_string(&dump_fail).unwrap_or_default();
        if !dump.is_empty() {
            println!("last dump:");
            for line in dump.lines().take(80) {
                println!("  {line}");
            }
        }
        if promote_to_corpus(
            ctx.project_dir,
            script_seed,
            &schedule_seed.to_string(),
            &shrunk,
        )? {
            camp.stored.promoted += 1;
        }
        camp.repro = Some(SearchRepro {
            path,
            events: shrunk,
        });
    } else {
        println!("fuzz failure at script {iter} (seed {script_seed}); counted (repro kept)");
    }
    if camp.fail_fast {
        write_fail_summary(ctx, camp, corpus)?;
        bail!("fuzz failure");
    }
    if first {
        println!("scuzz fuzz: --no-fail-fast; continue search then mutation");
    }
    Ok(())
}

fn load_corpus(project_dir: &Path) -> Result<Vec<(PathBuf, Repro)>> {
    let dir = project_dir.join("corpus");
    if !dir.is_dir() {
        return Ok(Vec::new());
    }
    let mut names = Vec::new();
    for ent in std::fs::read_dir(&dir)? {
        let ent = ent?;
        names.push(ent.file_name().to_string_lossy().into_owned());
    }
    let mut out = Vec::new();
    for name in corpus_sorted_names(names) {
        let path = dir.join(&name);
        if !path.is_file() {
            continue;
        }
        let text = std::fs::read_to_string(&path)
            .with_context(|| format!("reading {}", path.display()))?;
        let repro = parse_repro(&text).map_err(|e| anyhow::anyhow!("{}: {e}", path.display()))?;
        out.push((path, repro));
    }
    Ok(out)
}

fn replay_stored_corpus(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    mut ui: Option<&mut UiSearch>,
    mut io: Option<&mut Vec<IoKeep>>,
) -> Result<()> {
    let entries = load_corpus(ctx.project_dir)?;
    camp.stored.entries = entries.len() as i64;
    if !entries.is_empty() {
        println!(
            "scuzz fuzz: replay {} corpus {}",
            entries.len(),
            if entries.len() == 1 {
                "entry"
            } else {
                "entries"
            }
        );
    }
    for (path, repro) in entries {
        let sched = repro.schedule_seed.clone().unwrap_or_default();
        let code = if ctx.is_ui {
            fuzz_exec(ctx.exe, ctx.fuzz_dir, ctx.w, ctx.h, &repro.events, &sched)?
        } else {
            fuzz_exec_io(ctx.exe, ctx.fuzz_dir, &sched, &repro.events)?
        };
        let this_reached = lines_nonempty(
            &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
        );
        for n in this_reached {
            if !camp.corpus_reached.iter().any(|c| c == &n) {
                camp.corpus_reached.push(n);
            }
        }
        if code != 0 {
            camp.stored.failures += 1;
            camp.search_failures += 1;
            if camp.repro.is_none() {
                let dump_src = ctx.fuzz_dir.join("dump.txt");
                let dump_fail = ctx.fuzz_dir.join("dump.fail");
                if dump_src.exists() {
                    std::fs::copy(&dump_src, &dump_fail)?;
                }
                std::fs::copy(&path, ctx.fuzz_dir.join("repro.toml"))?;
                println!(
                    "fuzz failure on corpus {} ({} events)",
                    path.display(),
                    repro.events.len()
                );
                println!(
                    "replay: scuzz fuzz {} --replay {}",
                    ctx.project_dir.display(),
                    path.display()
                );
                camp.repro = Some(SearchRepro {
                    path: path.clone(),
                    events: repro.events.clone(),
                });
            } else {
                println!(
                    "fuzz failure on corpus {}; counted (repro kept)",
                    path.display()
                );
            }
            if camp.fail_fast {
                let corpus_len = ui
                    .as_ref()
                    .map(|s| s.prefixes.len() as i64)
                    .or_else(|| io.as_ref().map(|c| c.len() as i64))
                    .unwrap_or(0);
                camp.stored.reached = camp.corpus_reached.clone();
                write_fail_summary(ctx, camp, corpus_len)?;
                bail!("fuzz failure");
            }
            continue;
        }
        if let Some(search) = ui.as_mut() {
            let dump = std::fs::read_to_string(ctx.fuzz_dir.join("dump.txt")).unwrap_or_default();
            dump_push(&mut search.seen, dump);
            corpus_push(
                &mut search.prefixes,
                UiKeep {
                    sched: sched.clone(),
                    events: repro.events,
                },
            );
        } else if let Some(corpus) = io.as_mut() {
            corpus_push(
                corpus,
                IoKeep {
                    sched,
                    drives: repro.events,
                },
            );
        }
    }
    camp.stored.reached = camp.corpus_reached.clone();
    Ok(())
}

fn promote_to_corpus(
    project_dir: &Path,
    seed: i64,
    schedule_seed: &str,
    events: &[String],
) -> Result<bool> {
    let dir = project_dir.join("corpus");
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(format!("{}.toml", corpus_entry_name(schedule_seed, events)));
    if path.exists() {
        return Ok(false);
    }
    std::fs::write(&path, repro_text(seed, schedule_seed, events))?;
    println!("scuzz fuzz: promoted {}", path.display());
    Ok(true)
}

fn maybe_promote_coverage(
    ctx: &FuzzCtx<'_>,
    camp: &mut Campaign,
    script_seed: i64,
    schedule_seed: &str,
    events: &[String],
    reached: &[String],
) -> Result<()> {
    let novel: Vec<String> = missing_from(reached, &camp.corpus_reached)
        .into_iter()
        .cloned()
        .collect();
    if novel.is_empty() {
        return Ok(());
    }
    if camp.declared_count <= 0 || camp.coverage_promoted >= camp.declared_count {
        return Ok(());
    }
    if !promote_to_corpus(ctx.project_dir, script_seed, schedule_seed, events)? {
        return Ok(());
    }
    camp.stored.promoted += 1;
    camp.coverage_promoted += 1;
    for n in novel {
        if !camp.corpus_reached.iter().any(|c| c == &n) {
            camp.corpus_reached.push(n);
        }
    }
    camp.stored.reached = camp.corpus_reached.clone();
    Ok(())
}

fn declared_names(project_dir: &Path) -> Vec<String> {
    lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("sometimes.declared"))
            .unwrap_or_default(),
    )
}

fn reached_names(fuzz_dir: &Path) -> Vec<String> {
    lines_nonempty(
        &std::fs::read_to_string(fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
    )
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

fn toml_str_array(items: &[String]) -> String {
    items
        .iter()
        .map(|s| format!("\"{}\"", s.replace('\\', "\\\\").replace('"', "\\\"")))
        .collect::<Vec<_>>()
        .join(", ")
}

fn write_and_print(ctx: &FuzzCtx<'_>, s: &FuzzSummary<'_>) -> Result<()> {
    write_fuzz_summary(ctx, s)?;
    print_report(s);
    Ok(())
}

fn print_report(s: &FuzzSummary<'_>) {
    let status = if s.ok { "ok" } else { "fail" };
    println!(
        "scuzz fuzz {status} ({} search, {} search failures, seed {}, {} corpus)",
        s.search, s.search_failures, s.seed, s.corpus
    );
    println!(
        "corpus: {} stored, {} failed, {} promoted",
        s.stored.entries, s.stored.failures, s.stored.promoted
    );
    println!(
        "coverage: {}",
        if s.reached.is_empty() {
            "(none)".into()
        } else {
            s.reached.join(", ")
        }
    );
    println!(
        "declared: {}",
        if s.declared.is_empty() {
            "(none)".into()
        } else {
            s.declared.join(", ")
        }
    );
    print_sometimes_missing(s.missing_budget, s.missing_corpus);
    println!(
        "mutate: {} killed, {} survived ({} of {} sites)",
        s.mutate.killed, s.mutate.survived, s.mutate.ran, s.mutate.sites
    );
}

fn write_fuzz_summary(ctx: &FuzzCtx<'_>, s: &FuzzSummary<'_>) -> Result<()> {
    let repro_path = s.repro.map(|p| p.display().to_string()).unwrap_or_default();
    let replay = match s.repro {
        Some(p) => format!(
            "scuzz fuzz {} --replay {}",
            ctx.project_dir.display(),
            p.display()
        ),
        None => String::new(),
    };
    let text = format!(
        "[fuzz]\nok = {ok}\nseed = {seed}\niterations = {iterations}\nsearch = {search}\nsearch_failures = {search_failures}\ncorpus = {corpus}\ndrivers = [{drivers}]\nevents = [{events}]\ndeclared = [{declared}]\nreachability = [{reached}]\nmissing_budget = [{missing_budget}]\nmissing_corpus = [{missing_corpus}]\nrepro = \"{repro}\"\nreplay = \"{replay}\"\n\n[corpus]\nentries = {entries}\nfailures = {failures}\nreached = [{corpus_reached}]\npromoted = {promoted}\n\n[mutate]\nkilled = {killed}\nsurvived = {survived}\nran = {ran}\nsites = {sites}\noracles = {oracles}\n",
        ok = if s.ok { "true" } else { "false" },
        seed = s.seed,
        iterations = s.iterations,
        search = s.search,
        search_failures = s.search_failures,
        corpus = s.corpus,
        drivers = toml_str_array(s.drivers),
        events = toml_str_array(s.events),
        declared = toml_str_array(s.declared),
        reached = toml_str_array(s.reached),
        missing_budget = toml_str_array(s.missing_budget),
        missing_corpus = toml_str_array(s.missing_corpus),
        repro = repro_path.replace('\\', "\\\\").replace('"', "\\\""),
        replay = replay.replace('\\', "\\\\").replace('"', "\\\""),
        entries = s.stored.entries,
        failures = s.stored.failures,
        corpus_reached = toml_str_array(&s.stored.reached),
        promoted = s.stored.promoted,
        killed = s.mutate.killed,
        survived = s.mutate.survived,
        ran = s.mutate.ran,
        sites = s.mutate.sites,
        oracles = s.mutate.oracles,
    );
    std::fs::write(ctx.fuzz_dir.join("summary.toml"), text)?;
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
    let code = run_testrt(
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
    )?;
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    Ok(code)
}

fn fuzz_exec_io(
    exe: &Path,
    fuzz_dir: &Path,
    schedule_seed: &str,
    drives: &[String],
) -> Result<i32> {
    let reached = fuzz_dir.join("sometimes.reached");
    let drive_path = fuzz_dir.join("drive.txt");
    std::fs::write(&reached, "")?;
    std::fs::write(&drive_path, script_text(drives))?;
    let drive = if drives.is_empty() {
        None
    } else {
        Some(drive_path.as_path())
    };
    let code = run_testrt(exe, &reached, schedule_seed, None, drive)?;
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    Ok(code)
}

fn read_drivers(project_dir: &Path) -> Vec<String> {
    lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("drivers.txt")).unwrap_or_default(),
    )
}

use crate::support::{compile_opts, resolve_dir, run_testrt, TestrtUi};
use anyhow::{bail, Context, Result};
use scuzz_compiler::compile_prepared_program;
use scuzz_compiler::compile_project;
use scuzz_compiler::driver::load_verify_program;
use scuzz_compiler::fuzz::{
    corpus_entry_name_fault, corpus_keep, corpus_push, corpus_sorted_names, count_dump_section,
    decode_fault_seed, decode_sched_seed, drive_line_shrinks, drive_script_lines, dump_push,
    encode_fault_plan, encode_sched_plan, exhaust_alphabet, fault_seed_key, fuzz_mutate_sites,
    fuzz_pick_fault, fuzz_pick_sched, fuzz_pick_script, lines_nonempty, missing_from, parse_repro,
    repro_text, script_text, FaultMode, Repro, PCT_D_MIN, PCT_K_MAX,
};
use scuzz_compiler::manifest::load_manifest;
use scuzz_compiler::mutate::{
    collect_oracle_sites, mutate_apply_mode, mutate_count_mode, mutate_describe, nearest_oracle,
    MutantDesc, MutateMode, OracleSite,
};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

/// Shared fuzz target: executable, output directory, project directory, UI size.
struct FuzzCtx {
    exe: PathBuf,
    fuzz_dir: PathBuf,
    project_dir: PathBuf,
    w: i32,
    h: i32,
    is_ui: bool,
}

/// UI elements found by the probe. Drives the event alphabet.
struct FuzzTargets {
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: Vec<String>,
}

struct MutantSurvivor {
    site: i64,
    def: String,
    location: String,
    label: String,
    oracle: String,
}

struct MutateStats {
    killed: i64,
    survived: i64,
    ran: i64,
    sites: i64,
    oracles: bool,
    survivors: Vec<MutantSurvivor>,
}

impl MutateStats {
    fn skipped(oracles: bool, sites: i64) -> Self {
        Self {
            killed: 0,
            survived: 0,
            ran: 0,
            sites,
            oracles,
            survivors: Vec::new(),
        }
    }
}

/// Kept IO search: schedule seed plus the drive lines that ran with it.
struct IoKeep {
    sched: String,
    fault: String,
    drives: Vec<String>,
}

/// Kept UI search: schedule seed plus the event prefix that ran with it.
struct UiKeep {
    sched: String,
    fault: String,
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
    classify: &'a [(String, i64, i64)],
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

fn prepare_fuzz(path: &Path) -> Result<FuzzCtx> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    let is_ui = manifest.ui.is_some();
    let out = compile_project(&compile_opts(&project_dir, Path::new("build"), true, true)?)?;
    let fuzz_dir = project_dir.join("build").join("fuzz");
    std::fs::create_dir_all(&fuzz_dir)?;
    std::fs::write(fuzz_dir.join("sometimes.campaign"), "")?;
    std::fs::write(fuzz_dir.join("classify.campaign"), "")?;
    let w = manifest.ui.as_ref().map(|u| u.width()).unwrap_or(200);
    let h = manifest.ui.as_ref().map(|u| u.height()).unwrap_or(120);
    Ok(FuzzCtx {
        exe: out.executable,
        fuzz_dir,
        project_dir,
        w: if is_ui { w } else { 0 },
        h: if is_ui { h } else { 0 },
        is_ui,
    })
}

fn fuzz_run(
    path: &Path,
    iterations: i64,
    seed: i64,
    oracles: bool,
    fail_fast: bool,
) -> Result<ExitCode> {
    let ctx = prepare_fuzz(path)?;
    let (search_budget, mutate_slots) = budget_split(iterations);
    let declared_count = declared_names(&ctx.project_dir).len() as i64;
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
        coverage_promoted: 0,
        declared_count,
    };
    if iterations == 0 {
        println!("scuzz fuzz: corpus-only (--iterations 0)");
    }
    if ctx.is_ui {
        fuzz_ui_campaign(&ctx, &mut camp, search_budget)
    } else {
        fuzz_io_campaign(&ctx, &mut camp, search_budget)
    }
}

fn fuzz_replay(path: &Path, replay_path: &Path) -> Result<ExitCode> {
    let ctx = prepare_fuzz(path)?;
    let text = std::fs::read_to_string(replay_path)
        .with_context(|| format!("reading {}", replay_path.display()))?;
    let repro = parse_repro(&text).map_err(|e| anyhow::anyhow!("repro.toml: {e}"))?;
    let fault_note = match repro_fault(&repro).as_str() {
        "" | "0" => String::new(),
        f => format!(", fault_seed {f}"),
    };
    let pct_note = match (repro.pct_d, repro.pct_k) {
        (Some(d), Some(k)) => format!(", pct_d {d}, pct_k {k}"),
        _ => String::new(),
    };
    let sched_note = match &repro.schedule_seed {
        Some(s) => format!(", schedule_seed {s}{pct_note}{fault_note})"),
        None => {
            if fault_note.is_empty() {
                ")".into()
            } else {
                format!("{fault_note})")
            }
        }
    };
    println!(
        "scuzz fuzz --replay {} ({} events, seed {}{}",
        replay_path.display(),
        repro.events.len(),
        repro.seed,
        sched_note
    );
    let sched = repro.schedule_seed.clone().unwrap_or_default();
    let fault = repro_fault(&repro);
    let code = if ctx.is_ui {
        fuzz_exec(
            &ctx.exe,
            &ctx.fuzz_dir,
            ctx.w,
            ctx.h,
            &repro.events,
            &sched,
            &fault,
        )?
    } else {
        fuzz_exec_io(&ctx.exe, &ctx.fuzz_dir, &sched, &fault, &repro.events)?
    };
    if code == 0 {
        println!("fuzz replay ok (no failure)");
        Ok(ExitCode::SUCCESS)
    } else {
        println!("fuzz replay reproduced a failure");
        bail!("fuzz replay failure");
    }
}

fn fuzz_ui_campaign(ctx: &FuzzCtx, camp: &mut Campaign, search_budget: i64) -> Result<ExitCode> {
    let code = fuzz_exec(
        &ctx.exe,
        &ctx.fuzz_dir,
        ctx.w,
        ctx.h,
        &[],
        &camp.seed.to_string(),
        "0",
    )?;
    if code != 0 {
        write_fail_summary(ctx, camp, 0)?;
        bail!("fuzz probe failed: app fails under TestRuntime before any event");
    }
    let dump = std::fs::read_to_string(ctx.fuzz_dir.join("dump.txt")).unwrap_or_default();
    let n_taps = count_dump_section(&dump, "[taps]");
    let n_fields = count_dump_section(&dump, "[fields]");
    let n_scrolls = count_dump_section(&dump, "[scrolls]");
    let n_editors = count_dump_section(&dump, "[editor]");
    let drivers = read_drivers(&ctx.project_dir);
    let targets = FuzzTargets {
        n_buttons: n_taps,
        n_fields,
        n_scrolls,
        n_editors,
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
        targets.n_editors,
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
    }
    let corpus_len = search.prefixes.len() as i64;
    finish_campaign(ctx, camp, corpus_len, &search.prefixes, &[])
}

fn fuzz_io_campaign(ctx: &FuzzCtx, camp: &mut Campaign, search_budget: i64) -> Result<ExitCode> {
    let mut corpus: Vec<IoKeep> = Vec::new();
    let drivers = read_drivers(&ctx.project_dir);
    replay_stored_corpus(ctx, camp, None, Some(&mut corpus))?;
    for iter in 0..search_budget {
        let sched_corpus: Vec<String> = corpus.iter().map(|k| k.sched.clone()).collect();
        let fault_corpus: Vec<String> = corpus.iter().map(|k| k.fault.clone()).collect();
        let sched = fuzz_pick_sched(camp.seed, iter, &sched_corpus);
        let fault = fuzz_pick_fault(camp.seed, iter, &fault_corpus);
        let drives = drive_script_lines(camp.seed + iter, &drivers);
        let old_camp = lines_nonempty(
            &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
        );
        let code = fuzz_exec_io(&ctx.exe, &ctx.fuzz_dir, &sched, &fault, &drives)?;
        if code != 0 {
            camp.search_used = iter + 1;
            let sched_n: i64 = sched.parse().unwrap_or(0);
            note_search_fail(
                ctx,
                camp,
                camp.seed + iter,
                sched_n,
                &fault,
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
            maybe_promote_coverage(
                ctx,
                camp,
                camp.seed + iter,
                &sched,
                &fault,
                &drives,
                &reached,
            )?;
            corpus_push(
                &mut corpus,
                IoKeep {
                    sched,
                    fault,
                    drives,
                },
            );
        }
    }
    camp.search_used = search_budget;
    let corpus_len = corpus.len() as i64;
    finish_campaign(ctx, camp, corpus_len, &[], &corpus)
}

fn fuzz_loop(
    ctx: &FuzzCtx,
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
            targets.n_editors,
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
    ctx: &FuzzCtx,
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
    ctx: &FuzzCtx,
    camp: &mut Campaign,
    iter: i64,
    sched: i64,
    events: &[String],
    search: &mut UiSearch,
) -> Result<()> {
    let old_camp = lines_nonempty(
        &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.campaign")).unwrap_or_default(),
    );
    let fault_corpus: Vec<String> = search.prefixes.iter().map(|k| k.fault.clone()).collect();
    let fault = fuzz_pick_fault(camp.seed, iter, &fault_corpus);
    let code = fuzz_exec(
        &ctx.exe,
        &ctx.fuzz_dir,
        ctx.w,
        ctx.h,
        events,
        &sched.to_string(),
        &fault,
    )?;
    camp.search_used += 1;
    if code != 0 {
        note_search_fail(
            ctx,
            camp,
            camp.seed + iter,
            sched,
            &fault,
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
            &ctx.exe,
            &ctx.fuzz_dir,
            ctx.w,
            ctx.h,
            events,
            &sched.to_string(),
            &fault,
        )?;
        if dump2_code != 0 {
            note_search_fail(
                ctx,
                camp,
                camp.seed + iter,
                sched,
                &fault,
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
                fault: fault.clone(),
                events: events.to_vec(),
            },
        );
        maybe_promote_coverage(
            ctx,
            camp,
            camp.seed + iter,
            &sched.to_string(),
            &fault,
            events,
            &reached,
        )?;
    }
    Ok(())
}

fn finish_campaign(
    ctx: &FuzzCtx,
    camp: &Campaign,
    corpus: i64,
    corpus_ui: &[UiKeep],
    corpus_io: &[IoKeep],
) -> Result<ExitCode> {
    let declared = declared_names(&ctx.project_dir);
    let reached = reached_names(&ctx.fuzz_dir);
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
    ctx: &FuzzCtx,
    camp: &Campaign,
    corpus: i64,
    declared: &[String],
    reached: &[String],
    missing_budget: &[String],
) -> Result<ExitCode> {
    let mutate = mutate_stats_skip(&ctx.project_dir, camp.oracles);
    write_campaign(
        ctx,
        camp,
        corpus,
        declared,
        reached,
        missing_budget,
        &mutate,
        false,
    )?;
    let missing_corpus = sometimes_missing_from(declared, &camp.stored.reached);
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
    ctx: &FuzzCtx,
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
    let ok = camp.search_failures == 0 && missing_budget.is_empty() && mutate.survived == 0;
    write_campaign(
        ctx,
        camp,
        corpus,
        declared,
        reached,
        missing_budget,
        &mutate,
        ok,
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
        let missing_corpus = sometimes_missing_from(declared, &camp.stored.reached);
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

fn source_text_for_span(project_dir: &Path, file: &str) -> Option<String> {
    if file.is_empty() {
        return None;
    }
    let direct = project_dir.join(file);
    if direct.is_file() {
        return std::fs::read_to_string(direct).ok();
    }
    if let Some((_, rest)) = file.split_once('/') {
        let stripped = project_dir.join(rest);
        if stripped.is_file() {
            return std::fs::read_to_string(stripped).ok();
        }
    }
    None
}

fn print_surviving_mutant(
    project_dir: &Path,
    site: i64,
    desc: Option<&MutantDesc>,
    oracles: &[OracleSite],
) -> MutantSurvivor {
    let Some(desc) = desc else {
        println!("  mutant {site}: survived");
        return MutantSurvivor {
            site,
            def: String::new(),
            location: String::new(),
            label: String::new(),
            oracle: String::new(),
        };
    };
    let source = source_text_for_span(project_dir, &desc.file);
    let location = desc.location(source.as_deref());
    let excerpt = source
        .as_deref()
        .map(|s| desc.excerpt(s))
        .unwrap_or_default();
    let oracle = nearest_oracle(desc, oracles);
    println!("  mutant {site}: survived");
    println!("    {location}  {}", desc.def);
    if excerpt.is_empty() {
        println!("    {}", desc.label);
    } else {
        println!("    {}: {excerpt}", desc.label);
    }
    if oracle.starts_with("no oracle") {
        println!("    {oracle}");
    } else {
        println!("    nearest oracle: {oracle}");
    }
    MutantSurvivor {
        site,
        def: desc.def.clone(),
        location,
        label: desc.label.clone(),
        oracle,
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
    ctx: &FuzzCtx,
    seed: i64,
    slots: i64,
    oracles: bool,
    corpus_ui: &[UiKeep],
    corpus_io: &[IoKeep],
) -> Result<MutateStats> {
    let (prog, _manifest) = load_verify_program(&ctx.project_dir)?;
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
    let oracle_sites = collect_oracle_sites(&prog);
    let mut killed = 0i64;
    let mut survived = 0i64;
    let mut survivors = Vec::new();
    for site in &site_idxs {
        let out_dir = ctx
            .project_dir
            .join("build")
            .join("fuzz")
            .join("mutate")
            .join(site.to_string());
        let mutant = mutate_apply_mode(prog.clone(), *site as i32, mode);
        let opts = compile_opts(&ctx.project_dir, &out_dir, false, true)?;
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
            let desc = mutate_describe(&prog, *site as i32, mode);
            survivors.push(print_surviving_mutant(
                &ctx.project_dir,
                *site,
                desc.as_ref(),
                &oracle_sites,
            ));
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
        survivors,
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
    let code = mutate_exec_ui_events(exe, out_dir, w, h, &[], idle_sched, "0")?;
    if code != 0 {
        return Ok(code);
    }
    for keep in corpus {
        let code =
            mutate_exec_ui_events(exe, out_dir, w, h, &keep.events, &keep.sched, &keep.fault)?;
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
    fault_seed: &str,
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
        fault_seed,
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
    let code = mutate_exec_io_at(exe, out_dir, idle_sched, "0", &[])?;
    if code != 0 {
        return Ok(code);
    }
    for keep in corpus {
        let code = mutate_exec_io_at(exe, out_dir, &keep.sched, &keep.fault, &keep.drives)?;
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
    fault_seed: &str,
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
    run_testrt(exe, &reached, schedule_seed, fault_seed, None, drive)
}

fn shrink_events(
    exe: &Path,
    fuzz_dir: &Path,
    is_ui: bool,
    w: i32,
    h: i32,
    schedule_seed: &str,
    fault_seed: &str,
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
                fuzz_exec(exe, fuzz_dir, w, h, &cand, schedule_seed, fault_seed)?
            } else {
                fuzz_exec_io(exe, fuzz_dir, schedule_seed, fault_seed, &cand)?
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

/// Shrink args inside surviving `drive` lines. Try smaller Int/Bool/String
/// values. Keep a candidate only when the run still fails.
fn shrink_drive_args(
    exe: &Path,
    fuzz_dir: &Path,
    is_ui: bool,
    w: i32,
    h: i32,
    schedule_seed: &str,
    fault_seed: &str,
    events: &[String],
    specs: &[String],
) -> Result<Vec<String>> {
    let mut cur = events.to_vec();
    loop {
        let mut progressed = false;
        let mut i = 0;
        while i < cur.len() {
            let cands = drive_line_shrinks(&cur[i], specs);
            let mut kept = false;
            for cand_line in cands {
                let mut cand = cur.clone();
                cand[i] = cand_line;
                let code = if is_ui {
                    fuzz_exec(exe, fuzz_dir, w, h, &cand, schedule_seed, fault_seed)?
                } else {
                    fuzz_exec_io(exe, fuzz_dir, schedule_seed, fault_seed, &cand)?
                };
                if code != 0 {
                    cur = cand;
                    progressed = true;
                    kept = true;
                    break;
                }
            }
            if !kept {
                i += 1;
            }
        }
        if !progressed {
            break;
        }
    }
    Ok(cur)
}

fn shrink_fault(
    exe: &Path,
    fuzz_dir: &Path,
    is_ui: bool,
    w: i32,
    h: i32,
    schedule_seed: &str,
    fault_seed: &str,
    events: &[String],
) -> Result<String> {
    let cur = fault_seed_key(fault_seed);
    if cur.is_empty() {
        return Ok("0".into());
    }
    let run = |fault: &str| -> Result<i32> {
        if is_ui {
            fuzz_exec(exe, fuzz_dir, w, h, events, schedule_seed, fault)
        } else {
            fuzz_exec_io(exe, fuzz_dir, schedule_seed, fault, events)
        }
    };
    if run("0")? != 0 {
        return Ok("0".into());
    }
    let seed: i64 = cur.parse().unwrap_or(0);
    let plan = decode_fault_seed(seed);
    if plan.n > 1 {
        for n in 1..plan.n {
            let mut p = plan;
            p.n = n;
            let cand = encode_fault_plan(p).to_string();
            if run(&cand)? != 0 {
                return Ok(cand);
            }
        }
    }
    if plan.mode_str() != "fail" {
        let mut p = plan;
        p.mode = FaultMode::Fail;
        let cand = encode_fault_plan(p).to_string();
        if run(&cand)? != 0 {
            return Ok(cand);
        }
    }
    Ok(cur.to_string())
}

fn shrink_sched(
    exe: &Path,
    fuzz_dir: &Path,
    is_ui: bool,
    w: i32,
    h: i32,
    schedule_seed: &str,
    fault_seed: &str,
    events: &[String],
) -> Result<String> {
    if schedule_seed.is_empty() {
        return Ok(String::new());
    }
    let run = |sched: &str| -> Result<i32> {
        if is_ui {
            fuzz_exec(exe, fuzz_dir, w, h, events, sched, fault_seed)
        } else {
            fuzz_exec_io(exe, fuzz_dir, sched, fault_seed, events)
        }
    };
    let seed: i64 = schedule_seed.parse().unwrap_or(0);
    let plan = decode_sched_seed(seed);
    if plan.k > 0 {
        for k in 0..plan.k {
            let mut p = plan;
            p.k = k;
            let cand = encode_sched_plan(p).to_string();
            if run(&cand)? != 0 {
                return Ok(cand);
            }
        }
    }
    if plan.d > PCT_D_MIN {
        for d in PCT_D_MIN..plan.d {
            let mut p = plan;
            p.d = d;
            p.k = plan.k.min(d - 1).clamp(0, PCT_K_MAX);
            let cand = encode_sched_plan(p).to_string();
            if cand != schedule_seed && run(&cand)? != 0 {
                return Ok(cand);
            }
        }
    }
    Ok(schedule_seed.to_string())
}

fn repro_fault(repro: &Repro) -> String {
    repro
        .fault_seed
        .clone()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "0".into())
}

fn write_fail_summary(ctx: &FuzzCtx, camp: &Campaign, corpus: i64) -> Result<()> {
    let mutate = mutate_stats_skip(&ctx.project_dir, camp.oracles);
    let declared = declared_names(&ctx.project_dir);
    let reached = reached_names(&ctx.fuzz_dir);
    let missing_budget = sometimes_missing_from(&declared, &reached);
    write_campaign(
        ctx,
        camp,
        corpus,
        &declared,
        &reached,
        &missing_budget,
        &mutate,
        false,
    )
}

fn note_search_fail(
    ctx: &FuzzCtx,
    camp: &mut Campaign,
    script_seed: i64,
    schedule_seed: i64,
    fault_seed: &str,
    iter: i64,
    events: &[String],
    corpus: i64,
) -> Result<()> {
    stash_dump_fail(&ctx.fuzz_dir)?;
    let shrink_dir = ctx.fuzz_dir.join("shrink");
    std::fs::create_dir_all(&shrink_dir)?;
    std::fs::write(shrink_dir.join("sometimes.campaign"), "")?;
    let sched = schedule_seed.to_string();
    let shrunk = shrink_events(
        &ctx.exe,
        &shrink_dir,
        ctx.is_ui,
        ctx.w,
        ctx.h,
        &sched,
        fault_seed,
        events,
    )?;
    let shrunk = shrink_drive_args(
        &ctx.exe,
        &shrink_dir,
        ctx.is_ui,
        ctx.w,
        ctx.h,
        &sched,
        fault_seed,
        &shrunk,
        &read_drivers(&ctx.project_dir),
    )?;
    let fault = shrink_fault(
        &ctx.exe,
        &shrink_dir,
        ctx.is_ui,
        ctx.w,
        ctx.h,
        &sched,
        fault_seed,
        &shrunk,
    )?;
    let sched = shrink_sched(
        &ctx.exe,
        &shrink_dir,
        ctx.is_ui,
        ctx.w,
        ctx.h,
        &sched,
        &fault,
        &shrunk,
    )?;
    camp.search_failures += 1;
    let first = camp.repro.is_none();
    if first {
        let path = ctx.fuzz_dir.join("repro.toml");
        std::fs::write(&path, repro_text(script_seed, &sched, &fault, &shrunk))?;
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
        if !sched.is_empty() {
            let plan = decode_sched_seed(sched.parse().unwrap_or(0));
            println!("schedule_seed {sched} pct_d {} pct_k {}", plan.d, plan.k);
        }
        if !fault_seed_key(&fault).is_empty() {
            println!("fault_seed {fault}");
        }
        println!(
            "replay: scuzz fuzz {} --replay {}",
            ctx.project_dir.display(),
            path.display()
        );
        let dump = std::fs::read_to_string(ctx.fuzz_dir.join("dump.fail")).unwrap_or_default();
        if !dump.is_empty() {
            println!("last dump:");
            for line in dump.lines().take(80) {
                println!("  {line}");
            }
        }
        if promote_to_corpus(&ctx.project_dir, script_seed, &sched, &fault, &shrunk)? {
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
    ctx: &FuzzCtx,
    camp: &mut Campaign,
    mut ui: Option<&mut UiSearch>,
    mut io: Option<&mut Vec<IoKeep>>,
) -> Result<()> {
    let entries = load_corpus(&ctx.project_dir)?;
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
        let fault = repro_fault(&repro);
        let code = if ctx.is_ui {
            fuzz_exec(
                &ctx.exe,
                &ctx.fuzz_dir,
                ctx.w,
                ctx.h,
                &repro.events,
                &sched,
                &fault,
            )?
        } else {
            fuzz_exec_io(&ctx.exe, &ctx.fuzz_dir, &sched, &fault, &repro.events)?
        };
        let this_reached = lines_nonempty(
            &std::fs::read_to_string(ctx.fuzz_dir.join("sometimes.reached")).unwrap_or_default(),
        );
        for n in this_reached {
            push_name(&mut camp.stored.reached, n);
        }
        if code != 0 {
            camp.stored.failures += 1;
            camp.search_failures += 1;
            if camp.repro.is_none() {
                stash_dump_fail(&ctx.fuzz_dir)?;
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
                    fault: fault.clone(),
                    events: repro.events,
                },
            );
        } else if let Some(corpus) = io.as_mut() {
            corpus_push(
                corpus,
                IoKeep {
                    sched,
                    fault,
                    drives: repro.events,
                },
            );
        }
    }
    Ok(())
}

fn promote_to_corpus(
    project_dir: &Path,
    seed: i64,
    schedule_seed: &str,
    fault_seed: &str,
    events: &[String],
) -> Result<bool> {
    let dir = project_dir.join("corpus");
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(format!(
        "{}.toml",
        corpus_entry_name_fault(schedule_seed, fault_seed, events)
    ));
    if path.exists() {
        return Ok(false);
    }
    std::fs::write(&path, repro_text(seed, schedule_seed, fault_seed, events))?;
    println!("scuzz fuzz: promoted {}", path.display());
    Ok(true)
}

fn maybe_promote_coverage(
    ctx: &FuzzCtx,
    camp: &mut Campaign,
    script_seed: i64,
    schedule_seed: &str,
    fault_seed: &str,
    events: &[String],
    reached: &[String],
) -> Result<()> {
    let novel: Vec<String> = missing_from(reached, &camp.stored.reached)
        .into_iter()
        .cloned()
        .collect();
    if novel.is_empty() {
        return Ok(());
    }
    if camp.declared_count <= 0 || camp.coverage_promoted >= camp.declared_count {
        return Ok(());
    }
    if !promote_to_corpus(
        &ctx.project_dir,
        script_seed,
        schedule_seed,
        fault_seed,
        events,
    )? {
        return Ok(());
    }
    camp.stored.promoted += 1;
    camp.coverage_promoted += 1;
    for n in novel {
        push_name(&mut camp.stored.reached, n);
    }
    Ok(())
}

fn declared_names(project_dir: &Path) -> Vec<String> {
    let mut names = lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("sometimes.declared"))
            .unwrap_or_default(),
    );
    names.extend(lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("response.declared"))
            .unwrap_or_default(),
    ));
    names
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
        push_name(&mut camp, n);
    }
    std::fs::write(campaign_path, script_text(&camp))?;
    Ok(())
}

fn parse_classify_counts(text: &str) -> Vec<(String, i64, i64)> {
    let mut out = Vec::new();
    for line in text.lines() {
        let mut parts = line.split_whitespace();
        let Some(name) = parts.next() else {
            continue;
        };
        if name.is_empty() {
            continue;
        }
        let yes: i64 = parts.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        let no: i64 = parts.next().and_then(|s| s.parse().ok()).unwrap_or(0);
        out.push((name.to_string(), yes, no));
    }
    out
}

fn merge_classify(dump_path: &Path, campaign_path: &Path) -> Result<()> {
    let dump = parse_classify_counts(&std::fs::read_to_string(dump_path).unwrap_or_default());
    let mut camp =
        parse_classify_counts(&std::fs::read_to_string(campaign_path).unwrap_or_default());
    for (name, yes, no) in dump {
        if let Some(row) = camp.iter_mut().find(|(n, _, _)| n == &name) {
            row.1 += yes;
            row.2 += no;
        } else {
            camp.push((name, yes, no));
        }
    }
    let mut text = String::new();
    for (name, yes, no) in &camp {
        text.push_str(&format!("{name} {yes} {no}\n"));
    }
    std::fs::write(campaign_path, text)?;
    Ok(())
}

fn read_classify(project_dir: &Path, fuzz_dir: &Path) -> Vec<(String, i64, i64)> {
    let declared = lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("classify.declared"))
            .unwrap_or_default(),
    );
    let mut counts = parse_classify_counts(
        &std::fs::read_to_string(fuzz_dir.join("classify.campaign")).unwrap_or_default(),
    );
    for name in declared {
        if !counts.iter().any(|(n, _, _)| n == &name) {
            counts.push((name, 0, 0));
        }
    }
    counts
}

fn classify_summary_toml(rows: &[(String, i64, i64)]) -> String {
    if rows.is_empty() {
        return String::new();
    }
    let mut out = String::new();
    for (name, yes, no) in rows {
        let key = name.replace(['.', ' ', '-'], "_");
        out.push_str(&format!("{key}_true = {yes}\n{key}_false = {no}\n"));
    }
    out
}

fn push_name(names: &mut Vec<String>, n: String) {
    if !names.iter().any(|c| c == &n) {
        names.push(n);
    }
}

fn stash_dump_fail(fuzz_dir: &Path) -> Result<()> {
    let src = fuzz_dir.join("dump.txt");
    if src.exists() {
        std::fs::copy(&src, fuzz_dir.join("dump.fail"))?;
    }
    Ok(())
}

fn toml_escape(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"")
}

fn toml_str_array(items: &[String]) -> String {
    items
        .iter()
        .map(|s| format!("\"{}\"", toml_escape(s)))
        .collect::<Vec<_>>()
        .join(", ")
}

fn toml_survivor_array(items: &[MutantSurvivor]) -> String {
    items
        .iter()
        .map(|s| {
            format!(
                "{{ site = {}, def = \"{}\", location = \"{}\", label = \"{}\", oracle = \"{}\" }}",
                s.site,
                toml_escape(&s.def),
                toml_escape(&s.location),
                toml_escape(&s.label),
                toml_escape(&s.oracle),
            )
        })
        .collect::<Vec<_>>()
        .join(", ")
}

fn write_campaign(
    ctx: &FuzzCtx,
    camp: &Campaign,
    corpus: i64,
    declared: &[String],
    reached: &[String],
    missing_budget: &[String],
    mutate: &MutateStats,
    ok: bool,
) -> Result<()> {
    let missing_corpus = sometimes_missing_from(declared, &camp.stored.reached);
    let drivers = read_drivers(&ctx.project_dir);
    let classify = read_classify(&ctx.project_dir, &ctx.fuzz_dir);
    let empty: [String; 0] = [];
    let (repro, events): (Option<&Path>, &[String]) = match &camp.repro {
        Some(r) => (Some(r.path.as_path()), r.events.as_slice()),
        None => (None, &empty),
    };
    write_and_print(
        ctx,
        &FuzzSummary {
            seed: camp.seed,
            iterations: camp.iterations,
            search: camp.search_used,
            search_failures: camp.search_failures,
            corpus,
            ok,
            drivers: &drivers,
            events,
            declared,
            reached,
            missing_budget,
            missing_corpus: &missing_corpus,
            classify: &classify,
            stored: &camp.stored,
            repro,
            mutate,
        },
    )
}

fn write_and_print(ctx: &FuzzCtx, s: &FuzzSummary<'_>) -> Result<()> {
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
    if !s.classify.is_empty() {
        let parts: Vec<String> = s
            .classify
            .iter()
            .map(|(n, yes, no)| format!("{n} {yes}/{}", yes + no))
            .collect();
        println!("classify: {}", parts.join(", "));
    }
    println!(
        "mutate: {} killed, {} survived ({} of {} sites)",
        s.mutate.killed, s.mutate.survived, s.mutate.ran, s.mutate.sites
    );
    for v in &s.mutate.survivors {
        println!(
            "  survivor {}: {}  {}  {}  {}",
            v.site, v.location, v.def, v.label, v.oracle
        );
    }
}

fn write_fuzz_summary(ctx: &FuzzCtx, s: &FuzzSummary<'_>) -> Result<()> {
    let repro_path = s.repro.map(|p| p.display().to_string()).unwrap_or_default();
    let replay = match s.repro {
        Some(p) => format!(
            "scuzz fuzz {} --replay {}",
            ctx.project_dir.display(),
            p.display()
        ),
        None => String::new(),
    };
    let classify_toml = classify_summary_toml(s.classify);
    let text = format!(
        "[fuzz]\nok = {ok}\nseed = {seed}\niterations = {iterations}\nsearch = {search}\nsearch_failures = {search_failures}\ncorpus = {corpus}\ndrivers = [{drivers}]\nevents = [{events}]\ndeclared = [{declared}]\nreachability = [{reached}]\nmissing_budget = [{missing_budget}]\nmissing_corpus = [{missing_corpus}]\nrepro = \"{repro}\"\nreplay = \"{replay}\"\n\n[corpus]\nentries = {entries}\nfailures = {failures}\nreached = [{corpus_reached}]\npromoted = {promoted}\n\n[classify]\n{classify_toml}[mutate]\nkilled = {killed}\nsurvived = {survived}\nran = {ran}\nsites = {sites}\noracles = {oracles}\nsurvivors = [{survivors}]\n",
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
        survivors = toml_survivor_array(&s.mutate.survivors),
        classify_toml = classify_toml,
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
    fault_seed: &str,
) -> Result<i32> {
    let script = fuzz_dir.join("script.txt");
    let dump = fuzz_dir.join("dump.txt");
    let reached = fuzz_dir.join("sometimes.reached");
    std::fs::write(&script, script_text(events))?;
    std::fs::write(&dump, "")?;
    std::fs::write(&reached, "")?;
    std::fs::write(fuzz_dir.join("classify.dump"), "")?;
    let code = run_testrt(
        exe,
        &reached,
        schedule_seed,
        fault_seed,
        Some(TestrtUi {
            script: &script,
            dump: &dump,
            width: w,
            height: h,
        }),
        None,
    )?;
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    merge_classify(
        &fuzz_dir.join("classify.dump"),
        &fuzz_dir.join("classify.campaign"),
    )?;
    Ok(code)
}

fn fuzz_exec_io(
    exe: &Path,
    fuzz_dir: &Path,
    schedule_seed: &str,
    fault_seed: &str,
    drives: &[String],
) -> Result<i32> {
    let reached = fuzz_dir.join("sometimes.reached");
    let drive_path = fuzz_dir.join("drive.txt");
    std::fs::write(&reached, "")?;
    std::fs::write(fuzz_dir.join("classify.dump"), "")?;
    std::fs::write(&drive_path, script_text(drives))?;
    let drive = if drives.is_empty() {
        None
    } else {
        Some(drive_path.as_path())
    };
    let code = run_testrt(exe, &reached, schedule_seed, fault_seed, None, drive)?;
    merge_sometimes(&reached, &fuzz_dir.join("sometimes.campaign"))?;
    merge_classify(
        &fuzz_dir.join("classify.dump"),
        &fuzz_dir.join("classify.campaign"),
    )?;
    Ok(code)
}

fn read_drivers(project_dir: &Path) -> Vec<String> {
    lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("drivers.txt")).unwrap_or_default(),
    )
}

//! `scuzz mine`: mine candidate intent claims from fuzz campaign traces.

use crate::cmd_fuzz;
use crate::support::resolve_dir;
use anyhow::{bail, Result};
use scuzz_compiler::fuzz::{count_dump_section, exhaust_alphabet, fuzz_pick_script, parse_repro};
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

const MAX_CANDIDATES: usize = 20;
const MAX_ROWS_PER_TAP: usize = 3;
const VALIDATE_ITERATIONS: i64 = 8;
const VALIDATE_SEED: i64 = 42;
const PROBE_COUNT: usize = 8;
const PROBE_SEED: i64 = 7;
const LCG_M: i64 = 2_147_483_647;
const LCG_A: i64 = 48_271;
const LCG_SEED_MOD: i64 = 2_147_483_646;

/// One `== pump` block from a campaign trace.
struct Block {
    /// Int signal values at this pump: (signal id, value).
    ints: Vec<(i64, i64)>,
    /// A11y rows under `[views]`.
    views: Vec<String>,
    /// `[last_hit]` desc when a tap landed this pump.
    last_hit: Option<String>,
}

/// One `== run` section of `trace.campaign`.
struct TraceRun {
    blocks: Vec<Block>,
}

/// Observed int-signal stats that feed invariant candidates.
#[derive(Clone, Debug)]
struct InvariantStats {
    min: i64,
    max: i64,
    runs: i64,
    blocks: i64,
    attainment_runs: i64,
    decreasing_at_min: bool,
    decreasing_taps: BTreeSet<String>,
}

/// One mined candidate.
#[derive(Clone)]
struct Candidate {
    /// Stable id: 16 hex chars of the corpus hash over `<kind>|<input>`.
    id: String,
    /// `invariant`, `response`, or `boundary`.
    kind: &'static str,
    /// Sentence for invariant/response. Description for boundary.
    text: String,
    /// Claim sentence for --always/--never. Boundary derives one on --never.
    sentence: Option<String>,
    /// Response qualifying run count. Ranks response candidates.
    runs: i64,
    /// Response tap label. Breaks response rank ties.
    tap: String,
    /// Response views row. Breaks response rank ties.
    row: String,
    /// Invariant signal id. Ranks invariant candidates.
    signal_id: i64,
    min: i64,
    max: i64,
    blocks: i64,
    attainment_runs: i64,
    decreasing_at_min: bool,
    decreasing_taps: BTreeSet<String>,
    kills: i64,
    survivors: i64,
    probes: i64,
}

/// Boundary candidate replay data: the truncated repro script.
struct BoundaryData {
    id: String,
    seed: i64,
    schedule_seed: String,
    fault_seed: String,
    truncated: Vec<String>,
}

/// Claim lines and dismissals already in `intent.scuzz_intent`.
struct IntentState {
    lines: Vec<String>,
    dismissed: Vec<String>,
}

/// Survivor sites from `build/fuzz/summary.toml`.
struct MutateSurvivors {
    sites: Vec<i64>,
    oracles: bool,
}

/// FNV-1a 64 over `input`, formatted as 16 hex chars. Replicates the hash in
/// `corpus_entry_name_fault` (`fnv1a64` is private there). Candidate ids hash
/// the bare input without the corpus seed and 0xff framing.
fn candidate_id(input: &str) -> String {
    let mut h = 0xcbf29ce484222325u64;
    for &b in input.as_bytes() {
        h ^= u64::from(b);
        h = h.wrapping_mul(0x100000001b3);
    }
    format!("{h:016x}")
}

/// Parse `int[<id>] = <v>`. Other signal lines do not feed mining.
fn parse_int_line(line: &str) -> Option<(i64, i64)> {
    let rest = line.strip_prefix("int[")?;
    let (id, rest) = rest.split_once(']')?;
    let v = rest.strip_prefix(" = ")?;
    Some((id.parse().ok()?, v.parse().ok()?))
}

/// Parse `trace.campaign` into runs of pump blocks.
fn parse_campaign(text: &str) -> Vec<TraceRun> {
    let mut runs: Vec<TraceRun> = Vec::new();
    let mut block: Option<Block> = None;
    let mut section = "";
    let close_block = |runs: &mut Vec<TraceRun>, block: &mut Option<Block>| {
        if let Some(b) = block.take() {
            if let Some(run) = runs.last_mut() {
                run.blocks.push(b);
            }
        }
    };
    for line in text.lines() {
        match line {
            "== run" => {
                close_block(&mut runs, &mut block);
                runs.push(TraceRun { blocks: Vec::new() });
                section = "";
            }
            "== pump" => {
                close_block(&mut runs, &mut block);
                if runs.is_empty() {
                    runs.push(TraceRun { blocks: Vec::new() });
                }
                block = Some(Block {
                    ints: Vec::new(),
                    views: Vec::new(),
                    last_hit: None,
                });
                section = "";
            }
            "[signals]" => section = "signals",
            "[views]" => section = "views",
            "[last_hit]" => section = "last_hit",
            _ => {
                let Some(b) = block.as_mut() else {
                    continue;
                };
                match section {
                    "signals" => {
                        if let Some(kv) = parse_int_line(line) {
                            b.ints.push(kv);
                        }
                    }
                    "views" => b.views.push(line.to_string()),
                    "last_hit" if b.last_hit.is_none() => {
                        b.last_hit = Some(line.to_string());
                    }
                    _ => {}
                }
            }
        }
    }
    close_block(&mut runs, &mut block);
    runs
}

/// `build/signals.txt` rows: signal id -> name for `int` signals.
fn signal_names(project_dir: &Path) -> BTreeMap<i64, String> {
    let text =
        std::fs::read_to_string(project_dir.join("build").join("signals.txt")).unwrap_or_default();
    let mut out = BTreeMap::new();
    for line in text.lines() {
        let mut parts = line.split('\t');
        let (Some(id), Some(name), Some(kind)) = (parts.next(), parts.next(), parts.next()) else {
            continue;
        };
        if kind != "int" {
            continue;
        }
        if let Ok(id) = id.parse() {
            out.insert(id, name.to_string());
        }
    }
    out
}

/// Read existing claim lines and dismissed entries from `intent.scuzz_intent`.
fn read_intent(project_dir: &Path) -> IntentState {
    let text = std::fs::read_to_string(project_dir.join("intent.scuzz_intent")).unwrap_or_default();
    let mut lines = Vec::new();
    let mut dismissed = Vec::new();
    for line in text.lines() {
        if let Some(rest) = line.strip_prefix("# dismissed:") {
            dismissed.push(rest.trim().to_string());
        } else if !line.is_empty() {
            lines.push(line.to_string());
        }
    }
    IntentState { lines, dismissed }
}

/// True when a dismissed comment matches this candidate by id or sentence.
fn dismiss_matches(entry: &str, c: &Candidate) -> bool {
    let entry = entry.trim();
    let (first, rest) = match entry.split_once(char::is_whitespace) {
        Some((a, b)) => (a, b.trim()),
        None => (entry, ""),
    };
    if first == c.id {
        return true;
    }
    let sentence = c.sentence.as_deref().unwrap_or(c.text.as_str());
    rest == sentence || entry == sentence
}

fn signal_value(b: &Block, id: i64) -> Option<i64> {
    b.ints.iter().find(|(sid, _)| *sid == id).map(|(_, v)| *v)
}

/// Per-signal min/max/runs/blocks/attainment and decreasing-at-min taps.
fn invariant_stats(runs: &[TraceRun]) -> BTreeMap<i64, InvariantStats> {
    let mut minmax: BTreeMap<i64, (i64, i64, i64)> = BTreeMap::new();
    for run in runs {
        for b in &run.blocks {
            for &(id, v) in &b.ints {
                let e = minmax.entry(id).or_insert((v, v, 0));
                e.0 = e.0.min(v);
                e.1 = e.1.max(v);
                e.2 += 1;
            }
        }
    }
    let mut out = BTreeMap::new();
    for (&id, &(min, max, blocks)) in &minmax {
        let mut run_count = 0i64;
        let mut attain = 0i64;
        let mut decreasing_at_min = false;
        let mut taps = BTreeSet::new();
        for run in runs {
            let mentioned = run.blocks.iter().any(|b| signal_value(b, id).is_some());
            if !mentioned {
                continue;
            }
            run_count += 1;
            let run_min = run
                .blocks
                .iter()
                .filter_map(|b| signal_value(b, id))
                .min()
                .unwrap_or(min);
            if run_min == min {
                attain += 1;
            }
            for pair in run.blocks.windows(2) {
                let (Some(a), Some(b)) = (signal_value(&pair[0], id), signal_value(&pair[1], id))
                else {
                    continue;
                };
                if b < a && b == min {
                    if let Some(hit) = &pair[1].last_hit {
                        decreasing_at_min = true;
                        taps.insert(hit.clone());
                    }
                }
            }
        }
        out.insert(
            id,
            InvariantStats {
                min,
                max,
                runs: run_count,
                blocks,
                attainment_runs: attain,
                decreasing_at_min,
                decreasing_taps: taps,
            },
        );
    }
    out
}

/// Mine `stays at <min> or more` invariants from int signal values.
fn mine_invariants(runs: &[TraceRun], signals: &BTreeMap<i64, String>) -> Vec<Candidate> {
    let stats = invariant_stats(runs);
    let mut out = Vec::new();
    for (id, st) in stats {
        if st.min == i64::MIN {
            continue;
        }
        let Some(name) = signals.get(&id) else {
            continue;
        };
        let sentence = format!("The {name} stays at {} or more.", st.min);
        out.push(Candidate {
            id: candidate_id(&format!("invariant|{sentence}")),
            kind: "invariant",
            text: sentence.clone(),
            sentence: Some(sentence),
            runs: st.runs,
            tap: String::new(),
            row: String::new(),
            signal_id: id,
            min: st.min,
            max: st.max,
            blocks: st.blocks,
            attainment_runs: st.attainment_runs,
            decreasing_at_min: st.decreasing_at_min,
            decreasing_taps: st.decreasing_taps,
            kills: 0,
            survivors: 0,
            probes: 0,
        });
    }
    out
}

/// Rows of one block as a set. Dedup keeps frequency in block units.
fn view_set(b: &Block) -> BTreeSet<&str> {
    b.views.iter().map(String::as_str).collect()
}

fn empty_candidate(
    id: String,
    kind: &'static str,
    text: String,
    sentence: Option<String>,
    runs: i64,
    tap: String,
    row: String,
    signal_id: i64,
    blocks: i64,
) -> Candidate {
    Candidate {
        id,
        kind,
        text,
        sentence,
        runs,
        tap,
        row,
        signal_id,
        min: 0,
        max: 0,
        blocks,
        attainment_runs: 0,
        decreasing_at_min: false,
        decreasing_taps: BTreeSet::new(),
        kills: 0,
        survivors: 0,
        probes: 0,
    }
}

/// Mine `After a tap` correlations between tap labels and views rows.
fn mine_responses(runs: &[TraceRun]) -> Vec<Candidate> {
    let mut row_freq: BTreeMap<String, i64> = BTreeMap::new();
    let mut total_blocks = 0i64;
    let mut taps: BTreeSet<String> = BTreeSet::new();
    for run in runs {
        for b in &run.blocks {
            total_blocks += 1;
            for r in view_set(b) {
                *row_freq.entry(r.to_string()).or_insert(0) += 1;
            }
            if let Some(t) = &b.last_hit {
                taps.insert(t.clone());
            }
        }
    }
    let mut out = Vec::new();
    for t in taps {
        let mut qualifying = 0i64;
        let mut common: Option<BTreeSet<String>> = None;
        for run in runs {
            let Some(first) = run
                .blocks
                .iter()
                .position(|b| b.last_hit.as_deref() == Some(t.as_str()))
            else {
                continue;
            };
            qualifying += 1;
            for b in &run.blocks[first..] {
                let set: BTreeSet<String> = view_set(b).into_iter().map(str::to_string).collect();
                common = Some(match common {
                    None => set,
                    Some(prev) => prev.intersection(&set).cloned().collect(),
                });
            }
        }
        let Some(rows) = common else {
            continue;
        };
        let mut rows: Vec<String> = rows
            .into_iter()
            .filter(|r| row_freq.get(r).copied().unwrap_or(0) < total_blocks)
            .collect();
        rows.sort_by(|a, b| row_freq.get(b).cmp(&row_freq.get(a)).then_with(|| a.cmp(b)));
        rows.truncate(MAX_ROWS_PER_TAP);
        for r in rows {
            let sentence = format!(
                "After a tap on the \"{t}\" control, eventually the \"{r}\" control is visible."
            );
            out.push(empty_candidate(
                candidate_id(&format!("response|{sentence}")),
                "response",
                sentence.clone(),
                Some(sentence),
                qualifying,
                t.clone(),
                r,
                0,
                0,
            ));
        }
    }
    out
}

/// Mine the boundary candidate from `build/fuzz/repro.toml` minus its last event.
fn mine_boundary(project_dir: &Path) -> Option<(Candidate, BoundaryData)> {
    let repro_path = project_dir.join("build").join("fuzz").join("repro.toml");
    let text = std::fs::read_to_string(&repro_path).ok()?;
    let repro = parse_repro(&text).ok()?;
    if repro.events.is_empty() {
        return None;
    }
    let truncated: Vec<String> = repro.events[..repro.events.len() - 1].to_vec();
    let id = candidate_id(&format!("boundary|{}", truncated.join("\n")));
    let n = truncated.len();
    let cand = empty_candidate(
        id.clone(),
        "boundary",
        format!(
            "approve: corpus entry for {n}-event prefix of repro.toml; never: never-visible claim on a state-unique row"
        ),
        None,
        0,
        String::new(),
        String::new(),
        0,
        n as i64,
    );
    let data = BoundaryData {
        id,
        seed: repro.seed,
        schedule_seed: repro.schedule_seed.clone().unwrap_or_default(),
        fault_seed: cmd_fuzz::repro_fault(&repro),
        truncated,
    };
    Some((cand, data))
}

/// Rank: boundary first, then response, then invariant.
fn kind_rank(kind: &str) -> i32 {
    match kind {
        "boundary" => 0,
        "response" => 1,
        _ => 2,
    }
}

fn cmp_candidates(a: &Candidate, b: &Candidate) -> std::cmp::Ordering {
    b.kills.cmp(&a.kills).then_with(|| {
        kind_rank(a.kind).cmp(&kind_rank(b.kind)).then_with(|| {
            if a.kind == "response" {
                b.runs
                    .cmp(&a.runs)
                    .then_with(|| a.tap.cmp(&b.tap))
                    .then_with(|| a.row.cmp(&b.row))
            } else {
                a.signal_id.cmp(&b.signal_id)
            }
        })
    })
}

fn candidate_sentence(c: &Candidate) -> &str {
    c.sentence.as_deref().unwrap_or(c.text.as_str())
}

/// Parse `[mutate].survivors` site values and the oracles flag. Missing text is empty.
fn parse_mutate_survivors(text: &str) -> MutateSurvivors {
    let oracles = text.lines().any(|line| {
        let line = line.trim();
        line == "oracles = true" || line.starts_with("oracles = true")
    });
    let mut sites = Vec::new();
    let mut rest = text;
    while let Some(i) = rest.find("site = ") {
        let after = &rest[i + 7..];
        let n: String = after
            .chars()
            .take_while(|c| c.is_ascii_digit() || *c == '-')
            .collect();
        if let Ok(v) = n.parse::<i64>() {
            sites.push(v);
        }
        rest = &rest[i + 7..];
    }
    MutateSurvivors { sites, oracles }
}

fn load_mutate_survivors(project_dir: &Path) -> MutateSurvivors {
    let path = project_dir.join("build").join("fuzz").join("summary.toml");
    let text = std::fs::read_to_string(path).unwrap_or_default();
    parse_mutate_survivors(&text)
}

/// Parse `[taps]` rows as (index, label). Frame tokens sit at the end.
fn dump_tap_rows(dump: &str) -> Vec<(i64, String)> {
    let mut out = Vec::new();
    let mut in_taps = false;
    for line in dump.lines() {
        if line.starts_with('[') && line.ends_with(']') {
            in_taps = line == "[taps]";
            continue;
        }
        if !in_taps || line.is_empty() {
            continue;
        }
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() < 3 {
            continue;
        }
        let Ok(idx) = parts[0].parse::<i64>() else {
            continue;
        };
        let label = parts[1..parts.len() - 2].join(" ");
        out.push((idx, label));
    }
    out
}

fn tap_matches_hit(last_hit: &str, tap_label: &str) -> bool {
    last_hit == tap_label
        || last_hit.ends_with(&format!(":{tap_label}"))
        || last_hit.contains(tap_label)
}

/// Map decreasing last_hit labels to `tap N` indices via idle `[taps]` rows.
fn map_decreasing_taps(dump: &str, labels: &BTreeSet<String>) -> Vec<i64> {
    let rows = dump_tap_rows(dump);
    let mut idxs = Vec::new();
    for label in labels {
        for (i, tap_label) in &rows {
            if tap_matches_hit(label, tap_label) && !idxs.contains(i) {
                idxs.push(*i);
            }
        }
    }
    idxs.sort_unstable();
    idxs
}

fn lcg_seed(seed: i64) -> i64 {
    seed.rem_euclid(LCG_SEED_MOD) + 1
}

fn lcg_next(s: i64) -> i64 {
    s.wrapping_mul(LCG_A).rem_euclid(LCG_M)
}

fn lcg_below(s: i64, n: i64) -> i64 {
    if n <= 0 {
        0
    } else {
        s.rem_euclid(n)
    }
}

fn biased_script(seed: i64, decreasing: &[i64], alphabet: &[String]) -> Vec<String> {
    let mut s = lcg_next(lcg_seed(seed));
    let len = 1 + lcg_below(s, 12);
    let mut out = Vec::new();
    let mut left = len;
    while left > 0 {
        s = lcg_next(s);
        let ev = if lcg_below(s, 4) < 3 {
            s = lcg_next(s);
            let n = decreasing.len() as i64 + 1;
            let k = lcg_below(s, n);
            if k == 0 {
                "pump 1".to_string()
            } else {
                format!("tap {}", decreasing[(k - 1) as usize])
            }
        } else if alphabet.is_empty() {
            "pump 1".to_string()
        } else {
            s = lcg_next(s);
            alphabet[lcg_below(s, alphabet.len() as i64) as usize].clone()
        };
        out.push(ev);
        left -= 1;
    }
    out
}

fn probe_scripts(dump: &str, decreasing: &[i64], drivers: &[String]) -> Vec<Vec<String>> {
    let n_taps = count_dump_section(dump, "[taps]");
    let n_fields = count_dump_section(dump, "[fields]");
    let n_scrolls = count_dump_section(dump, "[scrolls]");
    let n_editors = count_dump_section(dump, "[editor]");
    let alphabet = exhaust_alphabet(n_taps, n_fields, n_scrolls, n_editors, drivers);
    (0..PROBE_COUNT)
        .map(|i| {
            let seed = PROBE_SEED + i as i64;
            if decreasing.is_empty() {
                fuzz_pick_script(seed, n_taps, n_fields, n_scrolls, n_editors, drivers, &[])
            } else {
                biased_script(seed, decreasing, &alphabet)
            }
        })
        .collect()
}

fn evidence_text(c: &Candidate) -> String {
    match c.kind {
        "invariant" => format!(
            "min={} max={} runs={} attain={} decreasing_at_min={} kills={}/{} probes={}",
            c.min,
            c.max,
            c.runs,
            c.attainment_runs,
            if c.decreasing_at_min { "yes" } else { "no" },
            c.kills,
            c.survivors,
            c.probes
        ),
        "response" => format!("{} runs kills={}/{}", c.runs, c.kills, c.survivors),
        _ => format!("{} events", c.blocks),
    }
}

fn json_escape(s: &str) -> String {
    let mut out = String::new();
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

fn evidence_json(c: &Candidate) -> String {
    match c.kind {
        "invariant" => format!(
            "{{\"min\":{},\"max\":{},\"runs\":{},\"blocks\":{},\"attainment_runs\":{},\"decreasing_at_min\":{},\"kills\":{},\"survivors\":{},\"probes\":{}}}",
            c.min,
            c.max,
            c.runs,
            c.blocks,
            c.attainment_runs,
            if c.decreasing_at_min { "true" } else { "false" },
            c.kills,
            c.survivors,
            c.probes
        ),
        "response" => format!(
            "{{\"runs\":{},\"kills\":{},\"survivors\":{}}}",
            c.runs, c.kills, c.survivors
        ),
        _ => format!(
            "{{\"events\":{},\"kills\":{},\"survivors\":{}}}",
            c.blocks, c.kills, c.survivors
        ),
    }
}

fn candidate_json(c: &Candidate) -> String {
    let sentence = match &c.sentence {
        Some(s) => format!("\"{}\"", json_escape(s)),
        None => "null".to_string(),
    };
    format!(
        "  {{\"id\":\"{}\",\"kind\":\"{}\",\"sentence\":{},\"evidence\":{}}}",
        json_escape(&c.id),
        c.kind,
        sentence,
        evidence_json(c)
    )
}

fn emit_json(cands: &[Candidate]) {
    println!("[");
    for (i, c) in cands.iter().enumerate() {
        if i + 1 == cands.len() {
            println!("{}", candidate_json(c));
        } else {
            println!("{},", candidate_json(c));
        }
    }
    println!("]");
}

/// Mine and filter dismissed / already-accepted candidates. No cap.
fn candidates(project_dir: &Path) -> (Vec<Candidate>, Option<BoundaryData>) {
    let trace = std::fs::read_to_string(
        project_dir
            .join("build")
            .join("fuzz")
            .join("trace.campaign"),
    )
    .unwrap_or_default();
    let runs = parse_campaign(&trace);
    let signals = signal_names(project_dir);
    let intent = read_intent(project_dir);
    let boundary = mine_boundary(project_dir);
    let mut cands: Vec<Candidate> = Vec::new();
    if let Some((cand, _)) = &boundary {
        cands.push(cand.clone());
    }
    cands.extend(mine_responses(&runs));
    cands.extend(mine_invariants(&runs, &signals));
    cands.retain(|c| {
        !intent.dismissed.iter().any(|d| dismiss_matches(d, c))
            && !c
                .sentence
                .as_ref()
                .is_some_and(|s| intent.lines.iter().any(|l| l == s))
    });
    (cands, boundary.map(|(_, data)| data))
}

fn read_drivers(project_dir: &Path) -> Vec<String> {
    scuzz_compiler::fuzz::lines_nonempty(
        &std::fs::read_to_string(project_dir.join("build").join("drivers.txt")).unwrap_or_default(),
    )
}

/// Directed falsification for one invariant. Caller has already appended the claim.
fn probe_invariant(path: &Path, c: &mut Candidate) -> Result<bool> {
    let ctx = cmd_fuzz::compile_fuzz_ctx(path)?;
    if !ctx.is_ui {
        return Ok(false);
    }
    let (idle_code, dump) = cmd_fuzz::run_mine_ui(&ctx, &[])?;
    if idle_code != 0 {
        c.probes = 0;
        return Ok(true);
    }
    let decreasing = map_decreasing_taps(&dump, &c.decreasing_taps);
    let project_dir = resolve_dir(path)?;
    let drivers = read_drivers(&project_dir);
    let scripts = probe_scripts(&dump, &decreasing, &drivers);
    let mut ran = 0i64;
    for events in &scripts {
        ran += 1;
        let (code, _) = cmd_fuzz::run_mine_ui(&ctx, events)?;
        if code != 0 {
            c.probes = ran;
            return Ok(true);
        }
    }
    c.probes = ran;
    Ok(false)
}

fn score_kills(path: &Path, c: &mut Candidate, sites: &[i64], oracles: bool) -> Result<()> {
    if sites.is_empty() {
        c.kills = 0;
        c.survivors = 0;
        return Ok(());
    }
    let (kills, n) = cmd_fuzz::mutate_score_sites(path, sites, oracles)?;
    c.kills = kills;
    c.survivors = n;
    Ok(())
}

fn qualify_list(path: &Path, project_dir: &Path, cands: &mut Vec<Candidate>) -> Result<()> {
    let survivors = load_mutate_survivors(project_dir);
    let mut kept = Vec::new();
    for mut c in cands.drain(..) {
        let Some(sentence) = c.sentence.clone() else {
            kept.push(c);
            continue;
        };
        let (intent_path, old) = append_intent(project_dir, &sentence)?;
        let result: Result<bool> = (|| {
            if c.kind == "invariant" && probe_invariant(path, &mut c)? {
                return Ok(false);
            }
            score_kills(path, &mut c, &survivors.sites, survivors.oracles)?;
            Ok(true)
        })();
        restore_intent(&intent_path, old)?;
        if result? {
            kept.push(c);
        }
    }
    *cands = kept;
    cands.sort_by(cmp_candidates);
    cands.truncate(MAX_CANDIDATES);
    Ok(())
}

/// Append one line to `intent.scuzz_intent`. Create the file when missing.
/// Keep exactly one trailing newline. Returns the path and prior content.
fn append_intent(project_dir: &Path, line: &str) -> Result<(PathBuf, Option<String>)> {
    let path = project_dir.join("intent.scuzz_intent");
    let old = std::fs::read_to_string(&path).ok();
    let mut text = old.clone().unwrap_or_default();
    while text.ends_with('\n') {
        text.pop();
    }
    if !text.is_empty() {
        text.push('\n');
    }
    text.push_str(line);
    text.push('\n');
    std::fs::write(&path, text)?;
    Ok((path, old))
}

/// Restore the intent file after a failed validation.
fn restore_intent(path: &Path, old: Option<String>) -> Result<()> {
    match old {
        Some(text) => std::fs::write(path, text)?,
        None => {
            let _ = std::fs::remove_file(path);
        }
    }
    Ok(())
}

/// Views rows of an a11y dump (`[views]` section of a golden dump).
fn dump_views(dump: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut in_views = false;
    for line in dump.lines() {
        if line.starts_with('[') {
            in_views = line == "[views]";
        } else if in_views && !line.is_empty() {
            out.push(line.to_string());
        }
    }
    out
}

/// Append a claim, then validate with the shared fuzz campaign entry.
/// On failure restore the file and point at the repro.
fn decide_claim(path: &Path, project_dir: &Path, sentence: &str) -> Result<ExitCode> {
    let (intent_path, old) = append_intent(project_dir, sentence)?;
    let result = cmd_fuzz::cmd_fuzz(path, None, VALIDATE_ITERATIONS, VALIDATE_SEED, false, false);
    let ok = matches!(result, Ok(code) if code == ExitCode::SUCCESS);
    if !ok {
        restore_intent(&intent_path, old)?;
        eprintln!(
            "scuzz mine: claim failed validation; repro at {}",
            project_dir
                .join("build")
                .join("fuzz")
                .join("repro.toml")
                .display()
        );
        bail!("scuzz mine: validation failed for: {sentence}");
    }
    println!("{sentence}");
    Ok(ExitCode::SUCCESS)
}

/// Derive a `never visible` sentence for a boundary candidate.
/// Replay the truncated script and pick a dump row absent from every trace block.
fn boundary_never_sentence(
    path: &Path,
    project_dir: &Path,
    runs: &[TraceRun],
    bnd: &BoundaryData,
) -> Result<String> {
    let (code, dump_path) =
        cmd_fuzz::replay_ui_events(path, &bnd.truncated, &bnd.schedule_seed, &bnd.fault_seed)?;
    if code != 0 {
        bail!(
            "scuzz mine: truncated script failed replay; see {}",
            project_dir
                .join("build")
                .join("fuzz")
                .join("repro.toml")
                .display()
        );
    }
    let dump = std::fs::read_to_string(&dump_path).unwrap_or_default();
    let traced: BTreeSet<&str> = runs
        .iter()
        .flat_map(|r| r.blocks.iter())
        .flat_map(|b| b.views.iter().map(String::as_str))
        .collect();
    let mut unique: Vec<String> = dump_views(&dump)
        .into_iter()
        .filter(|r| !traced.contains(r.as_str()))
        .collect();
    unique.sort();
    unique.dedup();
    let Some(row) = unique.into_iter().next() else {
        bail!(
            "scuzz mine: no state-unique row in the boundary dump; only --approve/--dismiss apply to boundary candidate {}",
            bnd.id
        );
    };
    Ok(format!("The \"{row}\" control is never visible."))
}

pub fn run(
    path: &Path,
    always: Option<String>,
    never: Option<String>,
    approve: Option<String>,
    dismiss: Option<String>,
    json: bool,
) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let (mut cands, boundary) = candidates(&project_dir);
    let decision = always
        .as_deref()
        .map(|id| ("always", id))
        .or_else(|| never.as_deref().map(|id| ("never", id)))
        .or_else(|| approve.as_deref().map(|id| ("approve", id)))
        .or_else(|| dismiss.as_deref().map(|id| ("dismiss", id)));
    if json && decision.is_some() {
        bail!("scuzz mine: --json does not combine with a decision flag");
    }
    let Some((flag, id)) = decision else {
        qualify_list(path, &project_dir, &mut cands)?;
        if json {
            emit_json(&cands);
        } else {
            for c in &cands {
                println!("{}  {}  {}  ({})", c.id, c.kind, c.text, evidence_text(c));
            }
        }
        return Ok(ExitCode::SUCCESS);
    };
    let Some(cand) = cands.iter().find(|c| c.id == id) else {
        let known: Vec<String> = cands.iter().map(|c| c.id.clone()).collect();
        bail!("unknown candidate id {id}\nknown ids: {}", known.join(", "));
    };
    match flag {
        "dismiss" => {
            let line = format!("# dismissed: {id} {}", candidate_sentence(cand));
            append_intent(&project_dir, &line)?;
            println!("{line}");
            Ok(ExitCode::SUCCESS)
        }
        "approve" => {
            let Some(bnd) = boundary.filter(|b| b.id == id) else {
                bail!("scuzz mine: --approve applies to boundary candidates only ({id} is not boundary)");
            };
            let (code, _) = cmd_fuzz::replay_ui_events(
                path,
                &bnd.truncated,
                &bnd.schedule_seed,
                &bnd.fault_seed,
            )?;
            if code != 0 {
                bail!(
                    "scuzz mine: truncated script failed replay; see {}",
                    project_dir
                        .join("build")
                        .join("fuzz")
                        .join("repro.toml")
                        .display()
                );
            }
            cmd_fuzz::promote_to_corpus(
                &project_dir,
                bnd.seed,
                &bnd.schedule_seed,
                &bnd.fault_seed,
                &bnd.truncated,
            )?;
            Ok(ExitCode::SUCCESS)
        }
        "always" => {
            if cand.kind == "boundary" {
                bail!("scuzz mine: --always does not apply to boundary candidates; use --never/--approve/--dismiss");
            }
            let sentence = cand.sentence.clone().unwrap_or_default();
            decide_claim(path, &project_dir, &sentence)
        }
        _ => {
            let sentence = if cand.kind == "boundary" {
                let Some(bnd) = boundary.filter(|b| b.id == id) else {
                    bail!("scuzz mine: boundary candidate {id} has no replay data");
                };
                let trace = std::fs::read_to_string(
                    project_dir
                        .join("build")
                        .join("fuzz")
                        .join("trace.campaign"),
                )
                .unwrap_or_default();
                let runs = parse_campaign(&trace);
                boundary_never_sentence(path, &project_dir, &runs, &bnd)?
            } else {
                cand.sentence.clone().unwrap_or_default()
            };
            decide_claim(path, &project_dir, &sentence)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn names(pairs: &[(&str, &str)]) -> BTreeMap<i64, String> {
        let mut m = BTreeMap::new();
        for (id, name) in pairs {
            m.insert(id.parse().unwrap(), (*name).to_string());
        }
        m
    }

    #[test]
    fn parse_int_line_reads_id_and_value() {
        assert_eq!(parse_int_line("int[0] = 3"), Some((0, 3)));
        assert_eq!(parse_int_line("int[12] = -4"), Some((12, -4)));
        assert_eq!(parse_int_line("str[1] = \"x\""), None);
    }

    #[test]
    fn parse_campaign_splits_runs_and_last_hit() {
        let text = "\
== run
== pump
[signals]
int[0] = 1
[views]
button:+1
[last_hit]
button:+1
== pump
[signals]
int[0] = 2
== run
== pump
[signals]
int[0] = 0
";
        let runs = parse_campaign(text);
        assert_eq!(runs.len(), 2);
        assert_eq!(runs[0].blocks.len(), 2);
        assert_eq!(runs[0].blocks[0].ints, vec![(0, 1)]);
        assert_eq!(runs[0].blocks[0].last_hit.as_deref(), Some("button:+1"));
        assert_eq!(runs[0].blocks[1].last_hit, None);
        assert_eq!(runs[1].blocks[0].ints, vec![(0, 0)]);
    }

    #[test]
    fn mine_invariants_skips_i64_min_keeps_floor_zero() {
        let min_trace = format!("== run\n== pump\n[signals]\nint[0] = {}\n", i64::MIN);
        let runs = parse_campaign(&min_trace);
        let signals = names(&[("0", "count")]);
        assert!(mine_invariants(&runs, &signals).is_empty());

        let zero = parse_campaign("== run\n== pump\n[signals]\nint[0] = 0\n");
        let cands = mine_invariants(&zero, &signals);
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].min, 0);
        assert_eq!(cands[0].text, "The count stays at 0 or more.");
    }

    #[test]
    fn mine_invariants_counts_attainment_runs() {
        let text = "\
== run
== pump
[signals]
int[0] = 3
== run
== pump
[signals]
int[0] = 0
== pump
[signals]
int[0] = 2
";
        let cands = mine_invariants(&parse_campaign(text), &names(&[("0", "count")]));
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].min, 0);
        assert_eq!(cands[0].max, 3);
        assert_eq!(cands[0].runs, 2);
        assert_eq!(cands[0].blocks, 3);
        assert_eq!(cands[0].attainment_runs, 1);
        assert!(!cands[0].decreasing_at_min);
    }

    #[test]
    fn mine_invariants_marks_decreasing_at_min() {
        let text = "\
== run
== pump
[signals]
int[0] = 5
== pump
[signals]
int[0] = 0
[last_hit]
button:-1
";
        let cands = mine_invariants(&parse_campaign(text), &names(&[("0", "count")]));
        assert_eq!(cands.len(), 1);
        assert!(cands[0].decreasing_at_min);
        assert!(cands[0].decreasing_taps.contains("button:-1"));
        assert_eq!(cands[0].attainment_runs, 1);
    }

    #[test]
    fn mine_invariants_drop_without_last_hit_is_not_decreasing_at_min() {
        let text = "\
== run
== pump
[signals]
int[0] = 2
== pump
[signals]
int[0] = 0
";
        let cands = mine_invariants(&parse_campaign(text), &names(&[("0", "count")]));
        assert_eq!(cands.len(), 1);
        assert!(!cands[0].decreasing_at_min);
        assert!(cands[0].decreasing_taps.is_empty());
    }

    #[test]
    fn dismiss_matches_id_or_sentence() {
        let c = empty_candidate(
            "abc123def4567890".into(),
            "invariant",
            "The count stays at 0 or more.".into(),
            Some("The count stays at 0 or more.".into()),
            1,
            String::new(),
            String::new(),
            0,
            4,
        );
        assert!(dismiss_matches("abc123def4567890", &c));
        assert!(dismiss_matches(
            "abc123def4567890 The count stays at 0 or more.",
            &c
        ));
        assert!(dismiss_matches(
            "oldhash000000000 The count stays at 0 or more.",
            &c
        ));
        assert!(!dismiss_matches(
            "otherid The count stays at 1 or more.",
            &c
        ));
    }

    #[test]
    fn parse_mutate_survivors_reads_sites_and_oracles() {
        let text = r#"
[mutate]
killed = 2
survived = 1
oracles = false
survivors = [{ site = 3, def = "main", location = "Main.scuzz:10", label = "+ to -", oracle = "count >= 0" }, { site = 9, def = "main", location = "Main.scuzz:11", label = "0 to 1", oracle = "none" }]
"#;
        let s = parse_mutate_survivors(text);
        assert_eq!(s.sites, vec![3, 9]);
        assert!(!s.oracles);
        let empty = parse_mutate_survivors("[mutate]\nsurvivors = []\noracles = true\n");
        assert!(empty.sites.is_empty());
        assert!(empty.oracles);
    }

    #[test]
    fn cmp_candidates_ranks_kills_then_kind() {
        let mut low = empty_candidate(
            "a".into(),
            "boundary",
            "b".into(),
            None,
            0,
            String::new(),
            String::new(),
            0,
            1,
        );
        let mut high = empty_candidate(
            "z".into(),
            "invariant",
            "i".into(),
            Some("i".into()),
            0,
            String::new(),
            String::new(),
            0,
            1,
        );
        high.kills = 2;
        low.kills = 0;
        assert_eq!(cmp_candidates(&high, &low), std::cmp::Ordering::Less);
        high.kills = 0;
        assert_eq!(cmp_candidates(&low, &high), std::cmp::Ordering::Less);
    }

    #[test]
    fn map_decreasing_taps_matches_role_label() {
        let dump = "[taps]\n0 +1 24,68 80x36\n1 -1 120,68 80x36\n";
        let mut labels = BTreeSet::new();
        labels.insert("button:-1".into());
        assert_eq!(map_decreasing_taps(dump, &labels), vec![1]);
    }

    #[test]
    fn evidence_and_json_cover_invariant_fields() {
        let mut c = empty_candidate(
            "deadbeefdeadbeef".into(),
            "invariant",
            "The count stays at 0 or more.".into(),
            Some("The count stays at 0 or more.".into()),
            16,
            String::new(),
            String::new(),
            0,
            40,
        );
        c.min = 0;
        c.max = 5;
        c.attainment_runs = 16;
        c.kills = 0;
        c.survivors = 3;
        c.probes = 8;
        let text = evidence_text(&c);
        assert!(text.contains("min=0"));
        assert!(text.contains("max=5"));
        assert!(text.contains("runs=16"));
        assert!(text.contains("attain=16"));
        assert!(text.contains("decreasing_at_min=no"));
        assert!(text.contains("kills=0/3"));
        assert!(text.contains("probes=8"));
        let js = candidate_json(&c);
        assert!(js.contains("\"id\":\"deadbeefdeadbeef\""));
        assert!(js.contains("\"kind\":\"invariant\""));
        assert!(js.contains("\"sentence\":\"The count stays at 0 or more.\""));
        assert!(js.contains("\"min\":0"));
        assert!(js.contains("\"probes\":8"));
    }
}

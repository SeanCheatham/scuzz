//! `scuzz mine`: mine candidate intent claims from fuzz campaign traces.

use crate::cmd_fuzz;
use crate::support::resolve_dir;
use anyhow::{bail, Result};
use scuzz_compiler::fuzz::parse_repro;
use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

const MAX_CANDIDATES: usize = 20;
const MAX_ROWS_PER_TAP: usize = 3;
const VALIDATE_ITERATIONS: i64 = 8;
const VALIDATE_SEED: i64 = 42;

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

/// One mined candidate.
#[derive(Clone)]
struct Candidate {
    /// Stable id: 16 hex chars of the corpus hash over `<kind>|<input>`.
    id: String,
    /// `invariant`, `response`, or `boundary`.
    kind: &'static str,
    /// Sentence for invariant/response. Description for boundary.
    text: String,
    /// Evidence note for list output.
    evidence: String,
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

/// Read existing claim lines and dismissed ids from `intent.scuzz_intent`.
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

/// Mine `stays at <min> or more` invariants from int signal values.
fn mine_invariants(runs: &[TraceRun], signals: &BTreeMap<i64, String>) -> Vec<Candidate> {
    // (min value, block count) per signal id.
    let mut stats: BTreeMap<i64, (i64, i64)> = BTreeMap::new();
    for run in runs {
        for b in &run.blocks {
            for &(id, v) in &b.ints {
                let e = stats.entry(id).or_insert((v, 0));
                e.0 = e.0.min(v);
                e.1 += 1;
            }
        }
    }
    let mut out = Vec::new();
    for (id, (min, blocks)) in stats {
        // Unnamed signals stay unnamed. Skip ids absent from signals.txt.
        let Some(name) = signals.get(&id) else {
            continue;
        };
        let sentence = format!("The {name} stays at {min} or more.");
        out.push(Candidate {
            id: candidate_id(&format!("invariant|{sentence}")),
            kind: "invariant",
            text: sentence.clone(),
            evidence: format!("{blocks} blocks"),
            sentence: Some(sentence),
            runs: 0,
            tap: String::new(),
            row: String::new(),
            signal_id: id,
        });
    }
    out
}

/// Rows of one block as a set. Dedup keeps frequency in block units.
fn view_set(b: &Block) -> BTreeSet<&str> {
    b.views.iter().map(String::as_str).collect()
}

/// Mine `After a tap` correlations between tap labels and views rows.
fn mine_responses(runs: &[TraceRun]) -> Vec<Candidate> {
    // Global block frequency per row. Rows in every campaign block carry no signal.
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
        // Row intersection across every post-tap block of every qualifying run.
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
            out.push(Candidate {
                id: candidate_id(&format!("response|{sentence}")),
                kind: "response",
                text: sentence.clone(),
                evidence: format!("{qualifying} runs"),
                sentence: Some(sentence),
                runs: qualifying,
                tap: t.clone(),
                row: r,
                signal_id: 0,
            });
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
    let cand = Candidate {
        id: id.clone(),
        kind: "boundary",
        text: format!(
            "approve: corpus entry for {n}-event prefix of repro.toml; never: never-visible claim on a state-unique row"
        ),
        evidence: format!("{n} events"),
        sentence: None,
        runs: 0,
        tap: String::new(),
        row: String::new(),
        signal_id: 0,
    };
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

/// Mine, filter, and rank candidates. Also returns boundary replay data.
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
        !intent.dismissed.iter().any(|d| d == &c.id)
            && !c
                .sentence
                .as_ref()
                .is_some_and(|s| intent.lines.iter().any(|l| l == s))
    });
    cands.sort_by(|a, b| {
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
    });
    cands.truncate(MAX_CANDIDATES);
    (cands, boundary.map(|(_, data)| data))
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
) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let (cands, boundary) = candidates(&project_dir);
    let decision = always
        .as_deref()
        .map(|id| ("always", id))
        .or_else(|| never.as_deref().map(|id| ("never", id)))
        .or_else(|| approve.as_deref().map(|id| ("approve", id)))
        .or_else(|| dismiss.as_deref().map(|id| ("dismiss", id)));
    let Some((flag, id)) = decision else {
        for c in &cands {
            println!("{}  {}  {}  ({})", c.id, c.kind, c.text, c.evidence);
        }
        return Ok(ExitCode::SUCCESS);
    };
    let Some(cand) = cands.iter().find(|c| c.id == id) else {
        let known: Vec<String> = cands.iter().map(|c| c.id.clone()).collect();
        bail!("unknown candidate id {id}\nknown ids: {}", known.join(", "));
    };
    match flag {
        "dismiss" => {
            append_intent(&project_dir, &format!("# dismissed: {id}"))?;
            println!("# dismissed: {id}");
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
            // never
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

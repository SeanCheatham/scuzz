//! Deterministic fuzz alphabet, corpus, and `repro.toml` (CLI search; no runtime machinery).

use crate::ast::{Expr, ExprKind, Program};
use serde::Deserialize;

const LCG_M: i64 = 2_147_483_647;
const LCG_A: i64 = 48_271;
const LCG_SEED_MOD: i64 = 2_147_483_646;
const LETTERS: &[u8] = b"abcdefghijklmnopqrstuvwxyz";

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

fn letter_at(i: i64) -> String {
    let idx = i.rem_euclid(26) as usize;
    (LETTERS[idx] as char).to_string()
}

fn fuzz_kind_count(has_text: bool, has_scroll: bool, has_drive: bool) -> i64 {
    3 + i64::from(has_drive)
        + i64::from(has_text)
        + i64::from(has_text)
        + i64::from(has_text)
        + i64::from(has_scroll)
}

fn fuzz_word(prefix: &str, mut s: i64, n: i64) -> (String, i64) {
    let mut acc = String::new();
    let mut remaining = n;
    while remaining > 0 {
        acc.push_str(&letter_at(lcg_below(s, 26)));
        s = lcg_next(s);
        remaining -= 1;
    }
    (format!("{prefix}{acc}"), s)
}

fn drive_bool(s: i64) -> &'static str {
    if lcg_below(s, 2) == 0 {
        "false"
    } else {
        "true"
    }
}

fn drive_spec_parts(spec: &str) -> (String, Vec<char>) {
    let mut parts = spec.split_whitespace();
    let name = parts.next().unwrap_or("").to_string();
    let kinds: Vec<char> = parts.filter_map(|p| p.chars().next()).collect();
    (name, kinds)
}

fn drive_line(spec: &str, mut s: i64) -> String {
    let (name, kinds) = drive_spec_parts(spec);
    if kinds.is_empty() {
        return format!("drive {name}");
    }
    let mut args = Vec::new();
    for k in kinds {
        match k {
            's' => {
                args.push(letter_at(lcg_below(s, 26)));
                s = lcg_next(s);
            }
            'b' => {
                args.push(drive_bool(s).to_string());
                s = lcg_next(s);
            }
            _ => {
                args.push((1 + lcg_below(s, 3)).to_string());
                s = lcg_next(s);
            }
        }
    }
    format!("drive {name} {}", args.join(" "))
}

/// One `drive` line per driver table spec, with generated args.
pub fn drive_script_lines(seed: i64, drivers: &[String]) -> Vec<String> {
    let mut s = lcg_next(lcg_seed(seed));
    let mut out = Vec::new();
    for spec in drivers {
        out.push(drive_line(spec, s));
        s = lcg_next(s);
    }
    out
}

fn fuzz_event(
    s: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
) -> (String, i64) {
    fuzz_event_at(lcg_next(s), n_buttons, has_text, has_scroll, drivers)
}

fn fuzz_event_at(
    s: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
) -> (String, i64) {
    let k = lcg_below(
        s,
        fuzz_kind_count(has_text, has_scroll, !drivers.is_empty()),
    );
    fuzz_event_kind(s, k, n_buttons, has_scroll, has_text, drivers)
}

fn fuzz_event_kind(
    s: i64,
    k: i64,
    n_buttons: i64,
    has_scroll: bool,
    has_text: bool,
    drivers: &[String],
) -> (String, i64) {
    if k <= 1 {
        if n_buttons > 0 {
            let s = lcg_next(s);
            let n = n_buttons.max(1);
            (format!("tap {}", lcg_below(s, n)), s)
        } else if !drivers.is_empty() {
            let s = lcg_next(s);
            let spec = &drivers[lcg_below(s, drivers.len() as i64) as usize];
            let s = lcg_next(s);
            (drive_line(spec, s), s)
        } else {
            let s = lcg_next(s);
            (format!("pump {}", 1 + lcg_below(s, 3)), s)
        }
    } else if k == 2 {
        let s = lcg_next(s);
        (format!("pump {}", 1 + lcg_below(s, 3)), s)
    } else if !drivers.is_empty() {
        if k == 3 {
            let s = lcg_next(s);
            let spec = &drivers[lcg_below(s, drivers.len() as i64) as usize];
            let s = lcg_next(s);
            (drive_line(spec, s), s)
        } else {
            fuzz_event_extra(lcg_next(s), k - 4, has_scroll, has_text)
        }
    } else {
        fuzz_event_extra(lcg_next(s), k - 3, has_scroll, has_text)
    }
}

fn fuzz_event_extra(s: i64, k: i64, has_scroll: bool, has_text: bool) -> (String, i64) {
    if k == 0 {
        if has_scroll {
            ("scroll 40".into(), s)
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else if k == 1 {
        if has_text {
            ("backspace".into(), s)
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else if k == 2 {
        if has_text {
            fuzz_word("type ", lcg_next(s), 1 + lcg_below(s, 7))
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else {
        fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
    }
}

fn fuzz_script_acc(
    mut s: i64,
    remaining: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
    mut acc: Vec<String>,
) -> Vec<String> {
    let mut left = remaining;
    while left > 0 {
        let (ev, next) = fuzz_event(s, n_buttons, has_text, has_scroll, drivers);
        acc.push(ev);
        s = next;
        left -= 1;
    }
    acc
}

pub fn fuzz_script(
    seed: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
) -> Vec<String> {
    let s = lcg_next(lcg_seed(seed));
    let len = 1 + lcg_below(s, 12);
    fuzz_script_acc(s, len, n_buttons, has_text, has_scroll, drivers, Vec::new())
}

fn fuzz_extend_prefix(
    prefix: &[String],
    s: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
) -> Vec<String> {
    let extra = 1 + lcg_below(s, 4);
    fuzz_script_acc(
        s,
        extra,
        n_buttons,
        has_text,
        has_scroll,
        drivers,
        prefix.to_vec(),
    )
}

pub fn fuzz_pick_script(
    seed: i64,
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
    corpus: &[Vec<String>],
) -> Vec<String> {
    if corpus.is_empty() {
        return fuzz_script(seed, n_buttons, has_text, has_scroll, drivers);
    }
    let s = lcg_next(lcg_seed(seed));
    if lcg_below(s, 2) == 0 {
        fuzz_script(seed, n_buttons, has_text, has_scroll, drivers)
    } else {
        let s = lcg_next(s);
        let base = &corpus[lcg_below(s, corpus.len() as i64) as usize];
        fuzz_extend_prefix(base, lcg_next(s), n_buttons, has_text, has_scroll, drivers)
    }
}

fn fuzz_perturb_sched(base: &str, s: i64) -> String {
    let n: i64 = base.parse().unwrap_or(0);
    (n + 1 + lcg_below(s, 16)).to_string()
}

pub fn fuzz_pick_sched(seed: i64, iter: i64, corpus: &[String]) -> String {
    if corpus.is_empty() {
        return (seed + iter).to_string();
    }
    let s = lcg_next(lcg_seed(seed + iter));
    if lcg_below(s, 2) == 0 {
        (seed + iter).to_string()
    } else {
        let s = lcg_next(s);
        let base = &corpus[lcg_below(s, corpus.len() as i64) as usize];
        fuzz_perturb_sched(base, lcg_next(s))
    }
}

pub fn exhaust_alphabet(
    n_buttons: i64,
    has_text: bool,
    has_scroll: bool,
    drivers: &[String],
) -> Vec<String> {
    let mut out = Vec::new();
    for i in 0..n_buttons {
        out.push(format!("tap {i}"));
    }
    if has_text {
        out.push("text".into());
        out.push("text a".into());
        out.push("backspace".into());
        out.push("type a".into());
    }
    if has_scroll {
        out.push("scroll 40".into());
    }
    for spec in drivers {
        out.push(drive_line(spec, 0));
    }
    out.push("pump 1".into());
    out
}

pub fn corpus_push(corpus: &mut Vec<Vec<String>>, events: Vec<String>) {
    corpus.insert(0, events);
    corpus.truncate(32);
}

pub fn dump_push(seen: &mut Vec<String>, dump: String) {
    if !seen.iter().any(|d| d == &dump) {
        seen.insert(0, dump);
        seen.truncate(64);
    }
}

pub fn sched_push(corpus: &mut Vec<String>, sched: String) {
    if !corpus.iter().any(|s| s == &sched) {
        corpus.insert(0, sched);
        corpus.truncate(32);
    }
}

pub fn missing_from<'a>(have: &'a [String], known: &[String]) -> Vec<&'a String> {
    have.iter()
        .filter(|n| !known.iter().any(|k| k == *n))
        .collect()
}

pub fn corpus_keep(reached: &[String], old_camp: &[String], dump: &str, seen: &[String]) -> bool {
    !missing_from(reached, old_camp).is_empty() || !seen.iter().any(|d| d == dump)
}

pub fn lines_nonempty(text: &str) -> Vec<String> {
    text.lines()
        .filter(|l| !l.is_empty())
        .map(|s| s.to_string())
        .collect()
}

pub fn count_prefix_lines(s: &str, prefix: &str) -> i64 {
    s.lines().filter(|l| l.starts_with(prefix)).count() as i64
}

/// Count nonempty lines under a dump section header (`[taps]`, `[fields]`, `[scrolls]`).
pub fn count_dump_section(s: &str, header: &str) -> i64 {
    let mut in_section = false;
    let mut n = 0i64;
    for line in s.lines() {
        if line.starts_with('[') && line.ends_with(']') {
            in_section = line == header;
            continue;
        }
        if in_section && !line.is_empty() {
            n += 1;
        }
    }
    n
}

pub fn script_text(events: &[String]) -> String {
    if events.is_empty() {
        String::new()
    } else {
        let mut s = events.join("\n");
        s.push('\n');
        s
    }
}

pub fn sometimes_declared_text(program: &Program) -> String {
    let mut names = Vec::new();
    for d in &program.defs {
        collect_sometimes(&d.body, &mut names);
    }
    collect_sometimes(&program.main.body, &mut names);
    if names.is_empty() {
        String::new()
    } else {
        let mut s = names.join("\n");
        s.push('\n');
        s
    }
}

fn collect_sometimes(e: &Expr, acc: &mut Vec<String>) {
    if let ExprKind::Call { callee, args } = &e.kind {
        if callee == "Law.sometimes" {
            if let Some(ExprKind::StrLit(name)) = args.first().map(|a| &a.kind) {
                if !acc.iter().any(|n| n == name) {
                    acc.push(name.clone());
                }
            }
        }
    }
    e.for_each_child(|c| collect_sometimes(c, acc));
}

#[derive(Debug, Clone, Default, Deserialize)]
pub struct Repro {
    #[serde(default)]
    pub seed: i64,
    pub schedule_seed: Option<String>,
    #[serde(default)]
    pub events: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct ReproFile {
    #[serde(default)]
    fuzz: Repro,
}

pub fn parse_repro(text: &str) -> Result<Repro, String> {
    let parsed: ReproFile = toml::from_str(text).map_err(|e| e.to_string())?;
    Ok(parsed.fuzz)
}

pub fn repro_text(seed: i64, schedule_seed: &str, events: &[String]) -> String {
    let mut out = format!("[fuzz]\nseed = {seed}\n");
    if !schedule_seed.is_empty() {
        out.push_str(&format!("schedule_seed = \"{schedule_seed}\"\n"));
    }
    let quoted: Vec<String> = events.iter().map(|e| format!("\"{e}\"")).collect();
    out.push_str(&format!("events = [{}]\n", quoted.join(", ")));
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::parse;

    #[test]
    fn lcg_is_deterministic() {
        assert_eq!(lcg_seed(42), 43);
        assert_eq!(lcg_next(43), 43 * 48_271);
        assert_eq!(lcg_below(10, 3), 1);
    }

    #[test]
    fn script_same_seed_same_events() {
        let a = fuzz_script(42, 2, true, false, &[]);
        let b = fuzz_script(42, 2, true, false, &[]);
        assert_eq!(a, b);
        assert!(!a.is_empty());
    }

    #[test]
    fn exhaust_alphabet_includes_taps_and_pump() {
        let a = exhaust_alphabet(2, false, false, &[]);
        assert_eq!(
            a,
            vec!["tap 0".to_string(), "tap 1".into(), "pump 1".into()]
        );
    }

    #[test]
    fn dump_section_counts_taps_not_view_buttons() {
        let dump = "\
[views]
radio:Home=1
button:+1
button:Add
textfield:item
scroll:scroll

[taps]
0 Home 24,24 25x32
1 +1 44,254 48x32
2 Add 75,416 43x32

[fields]
0* item=\"\"

[scrolls]
0 scroll
";
        assert_eq!(count_prefix_lines(dump, "button:"), 2);
        assert_eq!(count_dump_section(dump, "[taps]"), 3);
        assert_eq!(count_dump_section(dump, "[fields]"), 1);
        assert_eq!(count_dump_section(dump, "[scrolls]"), 1);
    }

    #[test]
    fn repro_roundtrip() {
        let text = repro_text(7, "9", &["tap 0".into(), "pump 1".into()]);
        let r = parse_repro(&text).expect("repro toml");
        assert_eq!(r.seed, 7);
        assert_eq!(r.schedule_seed.as_deref(), Some("9"));
        assert_eq!(r.events, vec!["tap 0", "pump 1"]);
    }

    #[test]
    fn parse_repro_rejects_invalid_toml() {
        assert!(parse_repro("not toml").is_err());
        assert!(parse_repro("[fuzz]\nevents = 1\n").is_err());
    }

    #[test]
    fn drive_line_two_int_params() {
        let a = drive_line("addComm i i", 1);
        let b = drive_line("addComm i i", 1);
        assert_eq!(a, b);
        assert!(a.starts_with("drive addComm "), "{a}");
        assert_eq!(a.split_whitespace().count(), 4);
    }

    #[test]
    fn sometimes_declared_first_seen() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  for {
    _ = Law.sometimes("a")
    _ = Law.sometimes("b")
    _ = Law.sometimes("a")
  } yield ()
"#,
        )
        .unwrap();
        assert_eq!(sometimes_declared_text(&p), "a\nb\n");
    }
}

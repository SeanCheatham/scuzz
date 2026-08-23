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

const INTERESTING_INTS: [i64; 8] = [0, 1, -1, 2, 3, 8, 255, -100];

/// Inclusive Int range from a driver-table bound token (`i>=0` → lo 0).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct IntBound {
    lo: Option<i64>,
    hi: Option<i64>,
}

impl IntBound {
    const NONE: Self = Self { lo: None, hi: None };

    fn contains(self, v: i64) -> bool {
        if let Some(lo) = self.lo {
            if v < lo {
                return false;
            }
        }
        if let Some(hi) = self.hi {
            if v > hi {
                return false;
            }
        }
        true
    }

    fn clamp(self, v: i64) -> i64 {
        let v = match self.lo {
            Some(lo) => v.max(lo),
            None => v,
        };
        match self.hi {
            Some(hi) => v.min(hi),
            None => v,
        }
    }
}

/// One generated argument in a driver-table spec.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DriveArgKind {
    Int(IntBound),
    String,
    Bool,
}

fn drive_int(s: i64, bound: IntBound) -> i64 {
    let v = if lcg_below(s, 2) == 0 {
        INTERESTING_INTS[lcg_below(lcg_next(s), INTERESTING_INTS.len() as i64) as usize]
    } else {
        lcg_below(lcg_next(s), 1000) - 100
    };
    bound.clamp(v)
}

fn letter_at(i: i64) -> String {
    let idx = i.rem_euclid(26) as usize;
    (LETTERS[idx] as char).to_string()
}

fn fuzz_kind_count(n_fields: i64, n_scrolls: i64, has_drive: bool, n_editors: i64) -> i64 {
    3 + i64::from(has_drive)
        + 3 * i64::from(n_fields > 0)
        + i64::from(n_scrolls > 0)
        + 2 * i64::from(n_editors > 0)
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

fn drive_spec_kinds(spec: &str) -> (String, Vec<DriveArgKind>) {
    let mut parts = spec.split_whitespace();
    let name = parts.next().unwrap_or("").to_string();
    let kinds: Vec<DriveArgKind> = parts.map(parse_kind_token).collect();
    (name, kinds)
}

fn parse_kind_token(tok: &str) -> DriveArgKind {
    match tok.chars().next() {
        Some('s') => DriveArgKind::String,
        Some('b') => DriveArgKind::Bool,
        _ => DriveArgKind::Int(parse_int_bound(tok)),
    }
}

/// Parse `i`, `i>=0`, `i>0`, `i<=k`, or `i<k`. Exclusive bounds become inclusive.
fn parse_int_bound(tok: &str) -> IntBound {
    let rest = tok.strip_prefix('i').unwrap_or(tok);
    if rest.is_empty() {
        return IntBound::NONE;
    }
    if let Some(n) = rest.strip_prefix(">=") {
        return IntBound {
            lo: n.parse().ok(),
            hi: None,
        };
    }
    if let Some(n) = rest.strip_prefix("<=") {
        return IntBound {
            lo: None,
            hi: n.parse().ok(),
        };
    }
    if let Some(n) = rest.strip_prefix('>') {
        return IntBound {
            lo: n.parse().ok().and_then(|k: i64| k.checked_add(1)),
            hi: None,
        };
    }
    if let Some(n) = rest.strip_prefix('<') {
        return IntBound {
            lo: None,
            hi: n.parse().ok().and_then(|k: i64| k.checked_sub(1)),
        };
    }
    IntBound::NONE
}

fn drive_line(spec: &str, mut s: i64) -> String {
    let (name, kinds) = drive_spec_kinds(spec);
    if kinds.is_empty() {
        return format!("drive {name}");
    }
    let mut args = Vec::new();
    for k in kinds {
        match k {
            DriveArgKind::String => {
                args.push(letter_at(lcg_below(s, 26)));
                s = lcg_next(s);
            }
            DriveArgKind::Bool => {
                args.push(drive_bool(s).to_string());
                s = lcg_next(s);
            }
            DriveArgKind::Int(bound) => {
                args.push(drive_int(s, bound).to_string());
                s = lcg_next(s);
            }
        }
    }
    format!("drive {name} {}", args.join(" "))
}

fn infer_kind(raw: &str) -> DriveArgKind {
    if raw == "true" || raw == "false" {
        DriveArgKind::Bool
    } else if raw.parse::<i64>().is_ok() {
        DriveArgKind::Int(IntBound::NONE)
    } else {
        DriveArgKind::String
    }
}

fn kinds_for_drive(name: &str, specs: &[String]) -> Vec<DriveArgKind> {
    for spec in specs {
        let (spec_name, kinds) = drive_spec_kinds(spec);
        if spec_name == name {
            return kinds;
        }
    }
    Vec::new()
}

fn shrink_int(n: i64, bound: IntBound) -> Vec<i64> {
    let target = bound.lo.unwrap_or(0);
    let mut out = Vec::new();
    let mut push = |v: i64| {
        if v != n && bound.contains(v) && !out.contains(&v) {
            out.push(v);
        }
    };
    push(target);
    let mid = target.saturating_add(n.saturating_sub(target) / 2);
    push(mid);
    if n > target {
        push(n.saturating_sub(1));
    }
    out
}

fn shrink_arg(kind: DriveArgKind, raw: &str) -> Vec<String> {
    match kind {
        DriveArgKind::Int(bound) => match raw.parse::<i64>() {
            Ok(n) => shrink_int(n, bound)
                .into_iter()
                .map(|v| v.to_string())
                .collect(),
            Err(_) => Vec::new(),
        },
        DriveArgKind::Bool => {
            if raw == "true" {
                vec!["false".into()]
            } else {
                Vec::new()
            }
        }
        DriveArgKind::String => {
            if raw != "a" {
                vec!["a".into()]
            } else {
                Vec::new()
            }
        }
    }
}

fn set_drive_arg(name: &str, args: &[&str], i: usize, val: &str) -> String {
    let mut s = format!("drive {name}");
    for (j, a) in args.iter().enumerate() {
        s.push(' ');
        if j == i {
            s.push_str(val);
        } else {
            s.push_str(a);
        }
    }
    s
}

/// Smaller `drive` lines for one event. Earlier args first. Int: lower bound
/// (0 or published bound), then midpoint, then decrement. Bool: `false`.
/// String: `a`.
pub fn drive_line_shrinks(line: &str, specs: &[String]) -> Vec<String> {
    let mut parts = line.split_whitespace();
    let Some(verb) = parts.next() else {
        return Vec::new();
    };
    if verb != "drive" {
        return Vec::new();
    }
    let Some(name) = parts.next() else {
        return Vec::new();
    };
    let args: Vec<&str> = parts.collect();
    if args.is_empty() {
        return Vec::new();
    }
    let kinds = kinds_for_drive(name, specs);
    let mut out = Vec::new();
    for (i, arg) in args.iter().enumerate() {
        let kind = kinds.get(i).copied().unwrap_or_else(|| infer_kind(arg));
        for val in shrink_arg(kind, arg) {
            out.push(set_drive_arg(name, &args, i, &val));
        }
    }
    out
}

/// One `drive` line per driver table spec, with generated args.
/// Shuffle the lines. Drop a suffix so some iters run a subset. Keep-all is one draw.
pub fn drive_script_lines(seed: i64, drivers: &[String]) -> Vec<String> {
    let mut s = lcg_next(lcg_seed(seed));
    let mut out = Vec::new();
    for spec in drivers {
        out.push(drive_line(spec, s));
        s = lcg_next(s);
    }
    if out.len() <= 1 {
        return out;
    }
    for i in (1..out.len()).rev() {
        let j = lcg_below(s, (i + 1) as i64) as usize;
        s = lcg_next(s);
        out.swap(i, j);
    }
    let keep = 1 + lcg_below(s, out.len() as i64);
    out.truncate(keep as usize);
    out
}

fn fuzz_event(
    s: i64,
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
) -> (String, i64) {
    let s = lcg_next(s);
    let k = lcg_below(
        s,
        fuzz_kind_count(n_fields, n_scrolls, !drivers.is_empty(), n_editors),
    );
    fuzz_event_kind(s, k, n_buttons, n_scrolls, n_fields, n_editors, drivers)
}

fn fuzz_event_kind(
    s: i64,
    k: i64,
    n_buttons: i64,
    n_scrolls: i64,
    n_fields: i64,
    n_editors: i64,
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
            fuzz_event_extra(lcg_next(s), k - 4, n_scrolls, n_fields, n_editors)
        }
    } else {
        fuzz_event_extra(lcg_next(s), k - 3, n_scrolls, n_fields, n_editors)
    }
}

fn fuzz_field_word(verb: &str, s: i64, n_fields: i64) -> (String, i64) {
    let idx = lcg_below(s, n_fields.max(1));
    let s = lcg_next(s);
    fuzz_word(&format!("{verb} {idx} "), lcg_next(s), 1 + lcg_below(s, 7))
}

fn fuzz_editor_key(s: i64) -> (String, i64) {
    let s = lcg_next(s);
    let pick = lcg_below(s, 3);
    if pick == 0 {
        (String::from("key Enter"), s)
    } else if pick == 1 {
        (String::from("key Backspace"), s)
    } else {
        let (w, s) = fuzz_word("", lcg_next(s), 1);
        (format!("key {w} {w}"), s)
    }
}

fn fuzz_event_extra(
    s: i64,
    k: i64,
    n_scrolls: i64,
    n_fields: i64,
    n_editors: i64,
) -> (String, i64) {
    if k == 0 {
        if n_scrolls > 0 {
            let s = lcg_next(s);
            (format!("scroll {} 40", lcg_below(s, n_scrolls.max(1))), s)
        } else if n_fields > 0 {
            fuzz_field_word("text", s, n_fields)
        } else if n_editors > 0 {
            fuzz_editor_key(s)
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else if k == 1 {
        if n_fields > 0 {
            let s = lcg_next(s);
            (format!("backspace {} 1", lcg_below(s, n_fields.max(1))), s)
        } else if n_editors > 0 {
            fuzz_editor_key(s)
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else if k == 2 {
        if n_fields > 0 {
            fuzz_field_word("type", s, n_fields)
        } else if n_editors > 0 {
            fuzz_editor_key(s)
        } else {
            fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
        }
    } else if n_editors > 0 {
        fuzz_editor_key(s)
    } else {
        fuzz_word("text ", lcg_next(s), 1 + lcg_below(s, 7))
    }
}

fn fuzz_script_acc(
    mut s: i64,
    remaining: i64,
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
    mut acc: Vec<String>,
) -> Vec<String> {
    let mut left = remaining;
    while left > 0 {
        let (ev, next) = fuzz_event(s, n_buttons, n_fields, n_scrolls, n_editors, drivers);
        acc.push(ev);
        s = next;
        left -= 1;
    }
    acc
}

fn fuzz_script(
    seed: i64,
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
) -> Vec<String> {
    let s = lcg_next(lcg_seed(seed));
    let len = 1 + lcg_below(s, 12);
    fuzz_script_acc(
        s,
        len,
        n_buttons,
        n_fields,
        n_scrolls,
        n_editors,
        drivers,
        Vec::new(),
    )
}

fn fuzz_extend_prefix(
    prefix: &[String],
    s: i64,
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
) -> Vec<String> {
    let extra = 1 + lcg_below(s, 4);
    fuzz_script_acc(
        s,
        extra,
        n_buttons,
        n_fields,
        n_scrolls,
        n_editors,
        drivers,
        prefix.to_vec(),
    )
}

pub fn fuzz_pick_script(
    seed: i64,
    n_buttons: i64,
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
    corpus: &[Vec<String>],
) -> Vec<String> {
    if corpus.is_empty() {
        return fuzz_script(seed, n_buttons, n_fields, n_scrolls, n_editors, drivers);
    }
    let s = lcg_next(lcg_seed(seed));
    if lcg_below(s, 2) == 0 {
        fuzz_script(seed, n_buttons, n_fields, n_scrolls, n_editors, drivers)
    } else {
        let s = lcg_next(s);
        let base = &corpus[lcg_below(s, corpus.len() as i64) as usize];
        fuzz_extend_prefix(
            base,
            lcg_next(s),
            n_buttons,
            n_fields,
            n_scrolls,
            n_editors,
            drivers,
        )
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
    n_fields: i64,
    n_scrolls: i64,
    n_editors: i64,
    drivers: &[String],
) -> Vec<String> {
    let mut out = Vec::new();
    for i in 0..n_buttons {
        out.push(format!("tap {i}"));
    }
    for i in 0..n_fields {
        out.push(format!("text {i} a"));
        out.push(format!("type {i} a"));
        // Two numbers: field index then chop count. `backspace N` is count on the starred field.
        out.push(format!("backspace {i} 1"));
    }
    if n_editors > 0 {
        out.push("key a a".into());
        out.push("key Enter".into());
        out.push("key Backspace".into());
    }
    for i in 0..n_scrolls {
        out.push(format!("scroll {i} 40"));
    }
    for spec in drivers {
        out.push(drive_line(spec, 0));
    }
    out.push("pump 1".into());
    out
}

pub fn corpus_push<T>(corpus: &mut Vec<T>, item: T) {
    corpus.insert(0, item);
    corpus.truncate(32);
}

pub fn dump_push(seen: &mut Vec<String>, dump: String) {
    if !seen.iter().any(|d| d == &dump) {
        seen.insert(0, dump);
    }
}

/// Distinct live-code site indices for `take` mutation slots.
pub fn fuzz_mutate_sites(seed: i64, take: i64, sites: i64) -> Vec<i64> {
    if sites <= 0 || take <= 0 {
        return Vec::new();
    }
    let take = take.min(sites);
    let mut out = Vec::new();
    for i in 0..take {
        let mut s = lcg_seed(seed + i);
        let mut site = s.rem_euclid(sites);
        let mut tries = 0i64;
        while out.contains(&site) && tries < 8 {
            s = lcg_next(s);
            site = s.rem_euclid(sites);
            tries += 1;
        }
        if out.contains(&site) {
            if let Some(free) = (0..sites).find(|c| !out.contains(c)) {
                site = free;
            }
        }
        out.push(site);
    }
    out
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

#[cfg(test)]
fn count_prefix_lines(s: &str, prefix: &str) -> i64 {
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
        if callee == "Property.sometimes" {
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

/// FNV-1a 64-bit. Stable across rustc versions (unlike `DefaultHasher`).
fn fnv1a64(h: &mut u64, bytes: &[u8]) {
    for &b in bytes {
        *h ^= u64::from(b);
        *h = h.wrapping_mul(0x100000001b3);
    }
}

/// Content hash of `schedule_seed` plus events. Filename stem is idempotent.
pub fn corpus_entry_name(schedule_seed: &str, events: &[String]) -> String {
    let mut h = 0xcbf29ce484222325u64;
    fnv1a64(&mut h, schedule_seed.as_bytes());
    fnv1a64(&mut h, &[0xff]);
    for e in events {
        fnv1a64(&mut h, e.as_bytes());
        fnv1a64(&mut h, &[0xff]);
    }
    format!("{h:016x}")
}

/// `*.toml` names in lexicographic order. Missing or empty input is empty.
pub fn corpus_sorted_names(names: impl IntoIterator<Item = String>) -> Vec<String> {
    let mut out: Vec<String> = names.into_iter().filter(|n| n.ends_with(".toml")).collect();
    out.sort();
    out
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
        let a = fuzz_script(42, 2, 1, 0, 0, &[]);
        let b = fuzz_script(42, 2, 1, 0, 0, &[]);
        assert_eq!(a, b);
        assert!(!a.is_empty());
    }

    #[test]
    fn exhaust_alphabet_includes_taps_and_pump() {
        let a = exhaust_alphabet(2, 0, 0, 0, &[]);
        assert_eq!(
            a,
            vec!["tap 0".to_string(), "tap 1".into(), "pump 1".into()]
        );
    }

    #[test]
    fn exhaust_alphabet_pump_only() {
        assert_eq!(
            exhaust_alphabet(0, 0, 0, 0, &[]),
            vec!["pump 1".to_string()]
        );
    }

    #[test]
    fn exhaust_alphabet_indexes_fields_and_scrolls() {
        let a = exhaust_alphabet(0, 2, 2, 0, &[]);
        assert_eq!(
            a,
            vec![
                "text 0 a".to_string(),
                "type 0 a".into(),
                "backspace 0 1".into(),
                "text 1 a".into(),
                "type 1 a".into(),
                "backspace 1 1".into(),
                "scroll 0 40".into(),
                "scroll 1 40".into(),
                "pump 1".into(),
            ]
        );
    }

    #[test]
    fn exhaust_alphabet_includes_editor_keys() {
        let a = exhaust_alphabet(1, 0, 0, 1, &[]);
        assert_eq!(
            a,
            vec![
                "tap 0".to_string(),
                "key a a".into(),
                "key Enter".into(),
                "key Backspace".into(),
                "pump 1".into(),
            ]
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
    fn corpus_entry_name_is_idempotent() {
        let events = vec!["drive bumpIncreases 0".into(), "pump 1".into()];
        let a = corpus_entry_name("9", &events);
        let b = corpus_entry_name("9", &events);
        assert_eq!(a, b);
        assert_eq!(a.len(), 16);
        assert!(a.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn corpus_entry_name_matches_bad_example_pin() {
        assert_eq!(
            corpus_entry_name("42", &["drive bumpIncreases 0".into()]),
            "140caeeb01b1f10a"
        );
    }

    #[test]
    fn corpus_entry_name_changes_with_seed_or_events() {
        let ev = vec!["tap 0".to_string()];
        let base = corpus_entry_name("1", &ev);
        assert_ne!(base, corpus_entry_name("2", &ev));
        assert_ne!(base, corpus_entry_name("1", &["tap 1".into()]));
        assert_ne!(
            corpus_entry_name("ab", &["c".into()]),
            corpus_entry_name("a", &["bc".into()])
        );
    }

    #[test]
    fn corpus_entry_name_survives_repro_roundtrip() {
        let seed = "42";
        let events = vec!["drive bumpIncreases -1".into()];
        let name = corpus_entry_name(seed, &events);
        let text = repro_text(7, seed, &events);
        let r = parse_repro(&text).expect("repro toml");
        let sched = r.schedule_seed.as_deref().unwrap_or("");
        assert_eq!(corpus_entry_name(sched, &r.events), name);
    }

    #[test]
    fn corpus_sorted_names_toml_only_lexicographic() {
        let names = corpus_sorted_names([
            "b.toml".into(),
            "a.txt".into(),
            "a.toml".into(),
            "c.TOML".into(),
        ]);
        assert_eq!(names, vec!["a.toml".to_string(), "b.toml".into()]);
        assert!(corpus_sorted_names(Vec::<String>::new()).is_empty());
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
    fn drive_line_int_not_only_one_two_three() {
        let small: std::collections::HashSet<i64> = [1, 2, 3].into_iter().collect();
        let mut got = std::collections::HashSet::new();
        for seed in 0..32 {
            let line = drive_line("f i", seed);
            let n: i64 = line
                .split_whitespace()
                .nth(2)
                .expect("int arg")
                .parse()
                .expect("int");
            got.insert(n);
        }
        assert!(
            !got.is_subset(&small),
            "Int draws stayed in {{1,2,3}}: {got:?}"
        );
    }

    #[test]
    fn drive_int_clamps_to_published_bound() {
        for seed in 0..64 {
            let line = drive_line("f i>=0", seed);
            let n: i64 = line
                .split_whitespace()
                .nth(2)
                .expect("int arg")
                .parse()
                .expect("int");
            assert!(n >= 0, "draw {n} below i>=0");
        }
        for seed in 0..64 {
            let line = drive_line("f i>0", seed);
            let n: i64 = line
                .split_whitespace()
                .nth(2)
                .expect("int arg")
                .parse()
                .expect("int");
            assert!(n >= 1, "draw {n} below i>0");
        }
    }

    #[test]
    fn parse_int_bound_tokens() {
        assert_eq!(parse_int_bound("i"), IntBound::NONE);
        assert_eq!(
            parse_int_bound("i>=0"),
            IntBound {
                lo: Some(0),
                hi: None
            }
        );
        assert_eq!(
            parse_int_bound("i>0"),
            IntBound {
                lo: Some(1),
                hi: None
            }
        );
        assert_eq!(
            parse_int_bound("i<=10"),
            IntBound {
                lo: None,
                hi: Some(10)
            }
        );
        assert_eq!(
            parse_int_bound("i<10"),
            IntBound {
                lo: None,
                hi: Some(9)
            }
        );
        assert_eq!(
            parse_int_bound("i>=-1"),
            IntBound {
                lo: Some(-1),
                hi: None
            }
        );
    }

    #[test]
    fn drive_line_shrinks_int_toward_zero() {
        let specs = vec!["bumpIncreases i".into()];
        assert_eq!(
            drive_line_shrinks("drive bumpIncreases 101", &specs),
            vec![
                "drive bumpIncreases 0".to_string(),
                "drive bumpIncreases 50".into(),
                "drive bumpIncreases 100".into(),
            ]
        );
        assert!(drive_line_shrinks("drive bumpIncreases 0", &specs).is_empty());
    }

    #[test]
    fn drive_line_shrinks_int_to_refinement_bound() {
        let specs = vec!["f i>=10".into()];
        assert_eq!(
            drive_line_shrinks("drive f 101", &specs),
            vec![
                "drive f 10".to_string(),
                "drive f 55".into(),
                "drive f 100".into(),
            ]
        );
    }

    #[test]
    fn drive_line_shrinks_bool_and_string() {
        assert_eq!(
            drive_line_shrinks("drive flag true", &["flag b".into()]),
            vec!["drive flag false".to_string()]
        );
        assert!(drive_line_shrinks("drive flag false", &["flag b".into()]).is_empty());
        assert_eq!(
            drive_line_shrinks("drive w z", &["w s".into()]),
            vec!["drive w a".to_string()]
        );
        assert!(drive_line_shrinks("drive w a", &["w s".into()]).is_empty());
    }

    #[test]
    fn drive_script_lines_shuffles_or_subsets() {
        let drivers = vec!["a".into(), "b".into(), "c".into()];
        let mut saw_all = false;
        let mut saw_subset = false;
        let mut saw_shuffle = false;
        for seed in 0..48 {
            let lines = drive_script_lines(seed, &drivers);
            let names: Vec<&str> = lines
                .iter()
                .map(|l| l.split_whitespace().nth(1).unwrap_or(""))
                .collect();
            if names.len() == 3 {
                saw_all = true;
                if names != ["a", "b", "c"] {
                    saw_shuffle = true;
                }
            } else if !names.is_empty() {
                saw_subset = true;
            }
        }
        assert!(saw_all, "keep-all must stay one of the draws");
        assert!(saw_subset, "some iters must drop a suffix");
        assert!(saw_shuffle, "some full draws must shuffle order");
    }

    #[test]
    fn dump_push_never_forgets() {
        let mut seen = Vec::new();
        for i in 0..70 {
            dump_push(&mut seen, format!("dump-{i}"));
        }
        assert_eq!(seen.len(), 70);
        assert!(seen.iter().any(|d| d == "dump-0"));
        assert!(seen.iter().any(|d| d == "dump-69"));
    }

    #[test]
    fn fuzz_mutate_sites_are_distinct() {
        let sites = fuzz_mutate_sites(7, 5, 20);
        assert_eq!(sites.len(), 5);
        let mut uniq = sites.clone();
        uniq.sort();
        uniq.dedup();
        assert_eq!(uniq.len(), 5);
        assert!(sites.iter().all(|s| *s >= 0 && *s < 20));
    }

    #[test]
    fn sometimes_declared_first_seen() {
        let p = parse(
            r#"
@main def main: IO[Unit] =
  for {
    _ = Property.sometimes("a")
    _ = Property.sometimes("b")
    _ = Property.sometimes("a")
  } yield ()
"#,
        )
        .unwrap();
        assert_eq!(sometimes_declared_text(&p), "a\nb\n");
    }
}

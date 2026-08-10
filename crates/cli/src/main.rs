use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use scalui_compiler::driver::{
    compile_project, find_runtime_dir, wait_for_source_change, CompileOptions,
};
use scalui_compiler::format::format_source;
use scalui_compiler::manifest::load_manifest;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

#[derive(Parser, Debug)]
#[command(
    name = "scalui",
    version,
    about = "ScalUI — Stage-0 bootstrap CLI (release CLI is compiler-scalui)"
)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// Compile a ScalUI project to a native executable (LLVM)
    Build {
        /// Project directory containing scalui.toml
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Output directory
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
        /// Force a full rebuild (ignore incremental fingerprint)
        #[arg(long)]
        full: bool,
    },
    /// Build and run a ScalUI project
    Run {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Force Headless UiRuntime (no display required)
        #[arg(long)]
        headless: bool,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
        /// Rebuild and rerun when sources change (hot reload)
        #[arg(long)]
        watch: bool,
    },
    /// Watch sources and rebuild on change (hot reload compile loop)
    Watch {
        #[arg(default_value = ".")]
        path: PathBuf,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
    },
    /// Run tests: Headless golden PNGs when present (optional runtime C suites)
    Test {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Rewrite / seed goldens from Headless snapshots
        #[arg(long)]
        update: bool,
        /// Also run crates/runtime + ffi-skia C unit tests
        #[arg(long)]
        runtime_tests: bool,
    },
    /// Deterministic Headless fuzz: seeded random event scripts, or --replay
    Fuzz {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Replay a recorded repro.toml instead of generating scripts
        #[arg(long)]
        replay: Option<PathBuf>,
        /// Random scripts to try before declaring success
        #[arg(long, default_value_t = 32)]
        iters: u64,
        /// Base seed for script generation
        #[arg(long, default_value_t = 42)]
        seed: u64,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
    },
    /// Format ScalUI sources under src/
    Fmt {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Check only (nonzero exit if would reformat)
        #[arg(long)]
        check: bool,
    },
    /// Create a new ScalUI project
    New {
        name: String,
        #[arg(long, default_value = ".")]
        path: PathBuf,
        /// Scaffold a Headless UI sample instead of IO hello
        #[arg(long)]
        ui: bool,
    },
    /// Emit mobile packaging shells (android / ios / host)
    Package {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Packaging target: host, android, ios, or all
        #[arg(long, default_value = "all")]
        target: String,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
    },
}

fn main() -> ExitCode {
    match real_main() {
        Ok(code) => code,
        Err(e) => {
            eprintln!("scalui error: {e:#}");
            ExitCode::FAILURE
        }
    }
}

fn real_main() -> Result<ExitCode> {
    let cli = Cli::parse();
    match cli.command {
        Commands::Build { path, out_dir, full } => {
            let out = build(&path, &out_dir, !full)?;
            if out.cache_hit {
                eprintln!("up-to-date {}", out.executable.display());
            } else {
                eprintln!("built {}", out.executable.display());
            }
            Ok(ExitCode::SUCCESS)
        }
        Commands::Run {
            path,
            headless,
            out_dir,
            watch,
        } => {
            if watch {
                return watch_run(&path, &out_dir, headless);
            }
            run_once(&path, &out_dir, headless)
        }
        Commands::Watch { path, out_dir } => watch_build(&path, &out_dir),
        Commands::Test {
            path,
            update,
            runtime_tests,
        } => {
            if runtime_tests {
                let runtime = find_runtime_dir(&std::env::current_dir()?)?;
                let status = Command::new("make")
                    .arg("-C")
                    .arg(&runtime)
                    .arg("test")
                    .status()
                    .context("runtime tests")?;
                if !status.success() {
                    bail!("runtime tests failed");
                }

                let ffi = runtime
                    .parent()
                    .map(|p| p.join("ffi-skia"))
                    .unwrap_or_else(|| PathBuf::from("crates/ffi-skia"));
                if ffi.join("Makefile").is_file() {
                    let st = Command::new("make")
                        .arg("-C")
                        .arg(&ffi)
                        .arg("test")
                        .status()
                        .context("ffi-skia tests")?;
                    if !st.success() {
                        bail!("ffi-skia tests failed");
                    }
                }
            }

            let project_dir = resolve_dir(&path)?;
            if project_dir.join("scalui.toml").is_file() {
                let out = build(&path, &PathBuf::from("build"), false)?;
                eprintln!("project compile smoke ok");
                run_goldens(&project_dir, &out.executable, update)?;
            }
            eprintln!("scalui test ok");
            Ok(ExitCode::SUCCESS)
        }
        Commands::Fuzz {
            path,
            replay,
            iters,
            seed,
            out_dir,
        } => fuzz_project(&path, replay.as_deref(), iters, seed, &out_dir),
        Commands::Fmt { path, check } => fmt_project(&path, check),
        Commands::New { name, path, ui } => {
            let package_name = PathBuf::from(&name)
                .file_name()
                .and_then(|s| s.to_str())
                .filter(|s| !s.is_empty() && *s != "." && *s != "..")
                .map(|s| s.to_string())
                .unwrap_or_else(|| name.clone());
            if package_name.contains('/') || package_name.contains('\\') {
                bail!("invalid package name: {package_name}");
            }
            let dir = path.join(&package_name);
            if dir.exists() {
                bail!("{} already exists", dir.display());
            }
            std::fs::create_dir_all(dir.join("src"))?;
            if ui {
                std::fs::create_dir_all(dir.join("goldens"))?;
                std::fs::write(
                    dir.join("scalui.toml"),
                    format!(
                        r#"[package]
name = "{package_name}"
version = "0.1.0"

[targets.native]
kind = "executable"
main = "Main"

[ui]
default_runtime = "headless"
headless_size = [200, 120]
headless_scale = 1.0
bundle_id = "dev.scalui.{package_name}"
"#
                    ),
                )?;
                std::fs::write(
                    dir.join("src/Main.scala"),
                    r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    root = View.column(
      View.text("Counter"),
      View.textSignal(count, "count = "),
      View.row(View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)))
    )
    _ <- Ui.run(root)
  } yield ()
"#,
                )?;
                eprintln!(
                    "created {} (ui) — next: scalui test (seeds goldens) && scalui run --headless",
                    dir.display()
                );
            } else {
                std::fs::write(
                    dir.join("scalui.toml"),
                    format!(
                        r#"[package]
name = "{package_name}"
version = "0.1.0"

[targets.native]
kind = "executable"
main = "Main"
"#
                    ),
                )?;
                std::fs::write(
                    dir.join("src/Main.scala"),
                    r#"@main def main: IO[Unit] =
  IO.println("Hello, ScalUI!").flatMap(_ => IO.println("ready."))
"#,
                )?;
                eprintln!("created {}", dir.display());
            }
            Ok(ExitCode::SUCCESS)
        }
        Commands::Package {
            path,
            target,
            out_dir,
        } => package_project(&path, &target, &out_dir),
    }
}

fn run_once(path: &Path, out_dir: &Path, headless: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scalui.toml"))
        .with_context(|| format!("reading {}/scalui.toml", project_dir.display()))?;
    let default_rt = manifest
        .ui
        .as_ref()
        .map(|u| u.default_runtime.as_str())
        .unwrap_or("");
    // Prefer CLI `--headless`, then inherited SCALUI_UI_RUNTIME, then [ui].default_runtime.
    let env_rt = std::env::var("SCALUI_UI_RUNTIME").unwrap_or_default();
    let effective = if headless {
        "headless".to_string()
    } else if !env_rt.is_empty() {
        env_rt
    } else {
        default_rt.to_string()
    };
    let use_headless = effective.eq_ignore_ascii_case("headless");
    let use_mobile = effective.eq_ignore_ascii_case("mobile");
    let use_window = effective.eq_ignore_ascii_case("window")
        || (!use_headless && !use_mobile && manifest.ui.is_some());

    let out = build(path, &out_dir.to_path_buf(), true)?;
    let mut cmd = Command::new(&out.executable);
    if use_headless && (headless || manifest.ui.is_some()) {
        let snap = out
            .executable
            .parent()
            .unwrap_or(Path::new("."))
            .join("snapshot.png");
        apply_ui_env(&mut cmd, &manifest, &snap, /*tap*/ false);
        eprintln!(
            "scalui run --headless → snapshot {}",
            snap.display()
        );
    } else if use_mobile {
        cmd.env("SCALUI_UI_RUNTIME", "mobile");
        cmd.env("SCALUI_MOBILE_SHELL", "1");
        if let Some(ui) = &manifest.ui {
            cmd.env("SCALUI_UI_WIDTH", ui.width().to_string());
            cmd.env("SCALUI_UI_HEIGHT", ui.height().to_string());
        }
        eprintln!("scalui run → UiRuntime.Mobile (host shell)");
    } else if use_window {
        // Window peer + desktop embedder (X11 / Cocoa) when available.
        cmd.env("SCALUI_UI_RUNTIME", "window");
        if let Some(ui) = &manifest.ui {
            cmd.env("SCALUI_UI_WIDTH", ui.width().to_string());
            cmd.env("SCALUI_UI_HEIGHT", ui.height().to_string());
        }
        eprintln!("scalui run → UiRuntime.Window (desktop embedder)");
    }
    let status = cmd
        .status()
        .with_context(|| format!("running {}", out.executable.display()))?;
    if status.success() {
        Ok(ExitCode::SUCCESS)
    } else {
        Ok(ExitCode::from(status.code().unwrap_or(1) as u8))
    }
}

fn watch_build(path: &Path, out_dir: &Path) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    eprintln!("scalui watch {}", project_dir.display());
    loop {
        match build(path, &out_dir.to_path_buf(), false) {
            Ok(out) => eprintln!("rebuilt {}", out.executable.display()),
            Err(e) => eprintln!("scalui watch build error: {e:#}"),
        }
        if !wait_for_source_change(&project_dir, 60_000)? {
            // idle timeout — keep watching
            continue;
        }
    }
}

fn watch_run(path: &Path, out_dir: &Path, headless: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    eprintln!("scalui run --watch {}", project_dir.display());
    loop {
        match run_once(path, out_dir, headless) {
            Ok(_) => {}
            Err(e) => eprintln!("scalui watch run error: {e:#}"),
        }
        let _ = wait_for_source_change(&project_dir, 60_000)?;
    }
}

fn fmt_project(path: &Path, check: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let src = project_dir.join("src");
    if !src.is_dir() {
        bail!("missing src/ in {}", project_dir.display());
    }
    let mut dirty = 0usize;
    let mut paths = Vec::new();
    collect_scala(&src, &mut paths)?;
    for p in paths {
        let text = std::fs::read_to_string(&p)?;
        let formatted = format_source(&text).with_context(|| format!("formatting {}", p.display()))?;
        if formatted != text {
            if check {
                eprintln!("would reformat {}", p.display());
                dirty += 1;
            } else {
                std::fs::write(&p, formatted)?;
                eprintln!("formatted {}", p.display());
            }
        }
    }
    if check && dirty > 0 {
        bail!("{dirty} file(s) need formatting");
    }
    eprintln!("scalui fmt ok");
    Ok(ExitCode::SUCCESS)
}

fn collect_scala(dir: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() {
            collect_scala(&path, out)?;
        } else if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
            if matches!(ext, "scala" | "scalui") {
                out.push(path);
            }
        }
    }
    Ok(())
}

fn resolve_dir(path: &Path) -> Result<PathBuf> {
    if path.is_absolute() {
        Ok(path.to_path_buf())
    } else {
        Ok(std::env::current_dir()?.join(path))
    }
}

fn apply_ui_env(
    cmd: &mut Command,
    manifest: &scalui_compiler::manifest::Manifest,
    snapshot: &Path,
    tap: bool,
) {
    cmd.env("SCALUI_SNAPSHOT_PATH", snapshot);
    cmd.env("SCALUI_UI_RUNTIME", "headless");
    // Isolate Todo/Fs path so goldens do not share /tmp/scalui_todo.txt.
    let todo_path = std::env::temp_dir().join(format!(
        "scalui-todo-{}-{}",
        std::process::id(),
        snapshot
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("snap")
    ));
    let _ = std::fs::remove_file(&todo_path);
    cmd.env("SCALUI_TODO_PATH", &todo_path);
    if let Some(ui) = &manifest.ui {
        cmd.env("SCALUI_UI_WIDTH", ui.width().to_string());
        cmd.env("SCALUI_UI_HEIGHT", ui.height().to_string());
        cmd.env("SCALUI_UI_SCALE", ui.headless_scale.to_string());
    } else {
        cmd.env("SCALUI_UI_WIDTH", "200");
        cmd.env("SCALUI_UI_HEIGHT", "100");
        cmd.env("SCALUI_UI_SCALE", "1");
    }
    if tap {
        cmd.env("SCALUI_UI_TAP", "1");
        if let Some(ui) = &manifest.ui {
            if let Some(n) = ui.tap_button {
                cmd.env("SCALUI_UI_TAP_N", n.to_string());
            }
            if let Some(text) = &ui.tap_text {
                cmd.env("SCALUI_UI_TEXT", text);
            }
        }
    }
}

fn run_goldens(project_dir: &Path, exe: &Path, update: bool) -> Result<()> {
    let goldens = project_dir.join("goldens");
    if !goldens.is_dir() {
        return Ok(());
    }
    let manifest = load_manifest(&project_dir.join("scalui.toml"))?;
    let name = manifest.package.name.as_str();
    let out_dir = exe.parent().unwrap_or(Path::new("."));

    let mut pngs: Vec<PathBuf> = Vec::new();
    for entry in std::fs::read_dir(&goldens)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) == Some("png") {
            pngs.push(path);
        }
    }
    pngs.sort();

    if pngs.is_empty() {
        let base = goldens.join(format!("{name}.png"));
        let tap = goldens.join(format!("{name}_after_tap.png"));
        capture_golden(exe, &manifest, &base, false)?;
        capture_golden(exe, &manifest, &tap, true)?;
        eprintln!("seeded goldens: {}.png {}_after_tap.png", name, name);
        return Ok(());
    }

    for path in pngs {
        let stem = path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("golden");
        let tap = stem.ends_with("_after_tap");
        let actual = out_dir.join(format!("{stem}.actual.png"));
        capture_golden(exe, &manifest, &actual, tap)?;
        if update {
            std::fs::copy(&actual, &path)
                .with_context(|| format!("updating golden {}", path.display()))?;
            eprintln!("golden updated: {stem}");
        } else {
            let expected = std::fs::read(&path)?;
            let got = std::fs::read(&actual)?;
            if expected != got {
                bail!(
                    "golden mismatch: {} vs {} ({} vs {} bytes); re-run with --update to accept",
                    path.display(),
                    actual.display(),
                    expected.len(),
                    got.len()
                );
            }
            eprintln!("golden ok: {stem}");
        }
    }
    Ok(())
}

fn capture_golden(
    exe: &Path,
    manifest: &scalui_compiler::manifest::Manifest,
    snapshot: &Path,
    tap: bool,
) -> Result<()> {
    let mut cmd = Command::new(exe);
    apply_ui_env(&mut cmd, manifest, snapshot, tap);
    let status = cmd
        .status()
        .with_context(|| format!("running golden {}", snapshot.display()))?;
    if !status.success() {
        bail!("golden capture failed: {}", snapshot.display());
    }
    if !snapshot.is_file() {
        bail!("golden capture missing snapshot {}", snapshot.display());
    }
    Ok(())
}

/* --- scalui fuzz ----------------------------------------------------------
   Deterministic TestRuntime + Headless event scripts:
     (program, seed/config, event script) → exit code + signals + view dump
   Oracles: panic / unhandled SuError (nonzero exit). Structural, not pixels. */

#[derive(serde::Serialize, serde::Deserialize)]
struct ReproFile {
    fuzz: Repro,
}

#[derive(serde::Serialize, serde::Deserialize)]
struct Repro {
    seed: u64,
    events: Vec<String>,
}

/// SplitMix64 — deterministic and platform-independent script generation.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn below(&mut self, n: u64) -> u64 {
        if n == 0 {
            0
        } else {
            self.next() % n
        }
    }
}

struct FuzzRun {
    ok: bool,
    dump: String,
}

fn fuzz_exec(
    exe: &Path,
    manifest: &scalui_compiler::manifest::Manifest,
    fuzz_dir: &Path,
    events: &[String],
) -> Result<FuzzRun> {
    let script_path = fuzz_dir.join("script.txt");
    let dump_path = fuzz_dir.join("dump.txt");
    let mut script = events.join("\n");
    script.push('\n');
    std::fs::write(&script_path, script)?;
    let _ = std::fs::remove_file(&dump_path);
    let mut cmd = Command::new(exe);
    cmd.env("SCALUI_UI_RUNTIME", "headless");
    cmd.env("SCALUI_TESTRT", "1");
    cmd.env("SCALUI_UI_SCRIPT", &script_path);
    cmd.env("SCALUI_FUZZ_DUMP", &dump_path);
    if let Some(ui) = &manifest.ui {
        cmd.env("SCALUI_UI_WIDTH", ui.width().to_string());
        cmd.env("SCALUI_UI_HEIGHT", ui.height().to_string());
        cmd.env("SCALUI_UI_SCALE", ui.headless_scale.to_string());
    }
    let status = cmd
        .status()
        .with_context(|| format!("running fuzz script {}", script_path.display()))?;
    let dump = std::fs::read_to_string(&dump_path).unwrap_or_default();
    Ok(FuzzRun {
        ok: status.success(),
        dump,
    })
}

fn gen_script(seed: u64, n_buttons: u64, has_text_field: bool) -> Vec<String> {
    let mut rng = Rng(seed);
    let len = 1 + rng.below(12);
    let mut events = Vec::new();
    for _ in 0..len {
        let kinds = if has_text_field { 4 } else { 3 };
        match rng.below(kinds) {
            0 | 1 => events.push(format!("tap {}", rng.below(n_buttons.max(1)))),
            2 => events.push(format!("pump {}", 1 + rng.below(3))),
            _ => {
                let n = 1 + rng.below(7);
                let word: String =
                    (0..n).map(|_| (b'a' + rng.below(26) as u8) as char).collect();
                events.push(format!("text {word}"));
            }
        }
    }
    events
}

fn fuzz_project(
    path: &Path,
    replay: Option<&Path>,
    iters: u64,
    seed: u64,
    out_dir: &Path,
) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scalui.toml"))
        .with_context(|| format!("reading {}/scalui.toml", project_dir.display()))?;
    if manifest.ui.is_none() {
        bail!("scalui fuzz needs a [ui] project (Headless event scripts)");
    }
    let out = build(path, &out_dir.to_path_buf(), true)?;
    let fuzz_dir = out
        .executable
        .parent()
        .unwrap_or(Path::new("."))
        .join("fuzz");
    std::fs::create_dir_all(&fuzz_dir)?;

    if let Some(repro_path) = replay {
        let text = std::fs::read_to_string(repro_path)
            .with_context(|| format!("reading {}", repro_path.display()))?;
        let repro: ReproFile =
            toml::from_str(&text).with_context(|| format!("parsing {}", repro_path.display()))?;
        eprintln!(
            "scalui fuzz --replay {} ({} events, seed {})",
            repro_path.display(),
            repro.fuzz.events.len(),
            repro.fuzz.seed
        );
        let run = fuzz_exec(&out.executable, &manifest, &fuzz_dir, &repro.fuzz.events)?;
        eprintln!("fuzz dump: {}", fuzz_dir.join("dump.txt").display());
        if run.ok {
            eprintln!("fuzz replay ok (no failure)");
            return Ok(ExitCode::SUCCESS);
        }
        eprintln!(
            "fuzz replay reproduced a failure (script {})",
            fuzz_dir.join("script.txt").display()
        );
        return Ok(ExitCode::FAILURE);
    }

    /* Probe with no events: must pass; its view dump is the typed event
       surface (stable tap indices in scan order + text field presence). */
    let probe = fuzz_exec(&out.executable, &manifest, &fuzz_dir, &[])?;
    if !probe.ok {
        bail!("fuzz probe failed: app fails under TestRuntime before any event");
    }
    let n_buttons = probe
        .dump
        .lines()
        .filter(|l| l.starts_with("button:"))
        .count() as u64;
    let has_text_field = probe.dump.lines().any(|l| l.starts_with("textfield:"));
    if n_buttons == 0 && !has_text_field {
        eprintln!("scalui fuzz ok (no buttons or text fields; probe only)");
        return Ok(ExitCode::SUCCESS);
    }

    for iter in 0..iters {
        let script_seed = seed.wrapping_add(iter);
        let events = gen_script(script_seed, n_buttons, has_text_field);
        let run = fuzz_exec(&out.executable, &manifest, &fuzz_dir, &events)?;
        if !run.ok {
            let repro_path = fuzz_dir.join("repro.toml");
            let repro = ReproFile {
                fuzz: Repro {
                    seed: script_seed,
                    events,
                },
            };
            std::fs::write(&repro_path, toml::to_string_pretty(&repro)?)?;
            eprintln!(
                "fuzz failure at script {} (seed {}); wrote {}",
                iter,
                script_seed,
                repro_path.display()
            );
            eprintln!(
                "replay: scalui fuzz {} --replay {}",
                project_dir.display(),
                repro_path.display()
            );
            return Ok(ExitCode::FAILURE);
        }
    }
    eprintln!("scalui fuzz ok ({iters} scripts, seed {seed})");
    Ok(ExitCode::SUCCESS)
}

fn build(
    path: &Path,
    out_dir: &Path,
    incremental: bool,
) -> Result<scalui_compiler::CompileOutput> {
    let project_dir = resolve_dir(path)?;
    let out_dir = if out_dir.is_absolute() {
        out_dir.to_path_buf()
    } else {
        project_dir.join(out_dir)
    };
    let runtime_dir = find_runtime_dir(&std::env::current_dir()?)
        .or_else(|_| find_runtime_dir(&project_dir))?;
    let clang = std::env::var("SCALUI_CLANG").unwrap_or_else(|_| "clang".into());
    compile_project(&CompileOptions {
        project_dir,
        runtime_dir,
        out_dir,
        clang,
        incremental,
    })
}

fn package_project(path: &Path, target: &str, out_dir: &Path) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scalui.toml"))
        .with_context(|| format!("reading {}/scalui.toml", project_dir.display()))?;
    let package_out = if out_dir.is_absolute() {
        out_dir.to_path_buf()
    } else {
        project_dir.join(out_dir).join("package")
    };
    std::fs::create_dir_all(&package_out)?;

    let runtime_dir = find_runtime_dir(&std::env::current_dir()?)
        .or_else(|_| find_runtime_dir(&project_dir))?;
    let mobile_dir = runtime_dir
        .parent()
        .map(|p| p.join("embedder-mobile"))
        .unwrap_or_else(|| PathBuf::from("crates/embedder-mobile"));
    if !mobile_dir.join("Makefile").is_file() {
        bail!("missing embedder-mobile at {}", mobile_dir.display());
    }

    let clang = std::env::var("SCALUI_CLANG").unwrap_or_else(|_| "clang".into());
    let status = Command::new("make")
        .arg("-C")
        .arg(&mobile_dir)
        .arg("lib")
        .env("CC", &clang)
        .status()
        .context("building embedder-mobile")?;
    if !status.success() {
        bail!("embedder-mobile build failed");
    }

    let compiled = build(path, &PathBuf::from("build"), true)?;
    let target_lc = target.to_ascii_lowercase();
    let targets: Vec<&str> = if target_lc == "all" {
        vec!["host", "android", "ios"]
    } else if matches!(target_lc.as_str(), "host" | "android" | "ios") {
        vec![target_lc.as_str()]
    } else {
        bail!("unknown package target '{target_lc}' (host|android|ios|all)");
    };

    let bundle_id = manifest
        .ui
        .as_ref()
        .map(|u| u.bundle_id.as_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("dev.scalui.app");

    for t in targets {
        let dest = package_out.join(t);
        if dest.exists() {
            std::fs::remove_dir_all(&dest)?;
        }
        std::fs::create_dir_all(&dest)?;
        match t {
            "host" => {
                write_host_package(&dest, &compiled.executable, &manifest.package.name)?;
            }
            "android" => {
                copy_dir(&mobile_dir.join("shells/android"), &dest)?;
                patch_bundle_id(&dest.join("AndroidManifest.xml"), bundle_id)?;
                write_package_meta(&dest, &manifest.package.name, "android", bundle_id)?;
            }
            "ios" => {
                copy_dir(&mobile_dir.join("shells/ios"), &dest)?;
                patch_ios_bundle(&dest.join("Info.plist"), bundle_id)?;
                write_package_meta(&dest, &manifest.package.name, "ios", bundle_id)?;
            }
            _ => unreachable!(),
        }
        eprintln!("packaged {} → {}", t, dest.display());
    }
    eprintln!("scalui package ok ({})", package_out.display());
    Ok(ExitCode::SUCCESS)
}

fn write_host_package(dest: &Path, exe: &Path, name: &str) -> Result<()> {
    let run_sh = dest.join("run.sh");
    let exe_abs = if exe.is_absolute() {
        exe.to_path_buf()
    } else {
        std::env::current_dir()?.join(exe)
    };
    std::fs::write(
        &run_sh,
        format!(
            r#"#!/usr/bin/env bash
# Host Mobile shell smoke. Same app binary; Mobile peer + host embedder.
set -euo pipefail
export SCALUI_UI_RUNTIME=mobile
export SCALUI_MOBILE_SHELL=1
exec "{exe}" "$@"
"#,
            exe = exe_abs.display()
        ),
    )?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = std::fs::metadata(&run_sh)?.permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&run_sh, perms)?;
    }
    write_package_meta(dest, name, "host", "dev.scalui.app")?;
    std::fs::write(
        dest.join("README.md"),
        format!(
            "# {name} host mobile package\n\nRun `./run.sh` (sets `SCALUI_UI_RUNTIME=mobile`).\n"
        ),
    )?;
    Ok(())
}

fn write_package_meta(dest: &Path, name: &str, target: &str, bundle_id: &str) -> Result<()> {
    std::fs::write(
        dest.join("package.toml"),
        format!(
            r#"[package]
name = "{name}"
target = "{target}"
bundle_id = "{bundle_id}"
runtime = "mobile"
"#
        ),
    )?;
    Ok(())
}

fn patch_bundle_id(manifest: &Path, bundle_id: &str) -> Result<()> {
    if !manifest.is_file() {
        return Ok(());
    }
    let text = std::fs::read_to_string(manifest)?;
    let patched = text.replace("dev.scalui.app", bundle_id);
    std::fs::write(manifest, patched)?;
    Ok(())
}

fn patch_ios_bundle(plist: &Path, bundle_id: &str) -> Result<()> {
    patch_bundle_id(plist, bundle_id)
}

fn copy_dir(src: &Path, dst: &Path) -> Result<()> {
    if !src.is_dir() {
        bail!("missing shell template {}", src.display());
    }
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let from = entry.path();
        let to = dst.join(entry.file_name());
        if from.is_dir() {
            copy_dir(&from, &to)?;
        } else {
            std::fs::copy(&from, &to)
                .with_context(|| format!("copy {} → {}", from.display(), to.display()))?;
        }
    }
    Ok(())
}

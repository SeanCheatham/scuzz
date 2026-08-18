mod cmd_fuzz;
mod cmd_mutate;
mod support;

use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use scuzz_compiler::collect_fmt_sources;
use scuzz_compiler::compile_project;
use scuzz_compiler::driver::{find_runtime_dir, wait_for_source_change};
use scuzz_compiler::format::format_source;
use scuzz_compiler::manifest::load_manifest;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
use support::resolve_dir;

#[derive(Parser, Debug)]
#[command(
    name = "scuzz",
    version,
    about = "Scuzz Lang CLI",
    after_help = "Examples:\n  scuzz new myapp --ui\n  scuzz check\n  scuzz check --message-format=json\n  scuzz lsp\n  scuzz test\n  scuzz run --headless\n  scuzz run examples/studio\n  scuzz run --headless --script examples/studio/build/record.script --dump examples/studio/build/debug.dump examples/studio\n  scuzz watch\n  scuzz run --watch --headless\n  scuzz fuzz --iters 16\n  scuzz mutate --limit 16 --iters 4\n\nJSON diagnostics are the check protocol. `scuzz lsp` wraps `scuzz check` (open buffers overlay disk; not a second typer).\n`scuzz check` is the linter. `watch` rebuilds. `[ui] run --watch` is hot reload: it keeps the process, recompiles build/reload.dylib, and stamp-reloads the View tree (Signals stay). IO-only `run --watch` kills and reruns on source change. Live dump: build/debug.dump. Live inject: build/inject.script (tap/xy/text/type/pump/scroll/backspace). Desktop/Mobile record: build/record.script."
)]
struct Cli {
    /// Diagnostic format: human (default) or json (`check` protocol; LSP wraps check)
    #[arg(long, global = true, default_value = "human", value_parser = ["human", "json"])]
    message_format: String,
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// Compile a Scuzz Lang project to a native executable (LLVM)
    #[command(
        after_help = "Examples:\n  scuzz build\n  scuzz build --full examples/hello\n  scuzz build --verify examples/counter\n"
    )]
    Build {
        /// Project directory containing scuzz.toml
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Output directory
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
        /// Force a full rebuild (ignore incremental fingerprint)
        #[arg(long)]
        full: bool,
        /// Apply `*.scuzz_sim` + residual in-source `law` decls (TestRuntime / fuzz graph)
        #[arg(long)]
        verify: bool,
    },
    /// Build and run a Scuzz Lang project
    #[command(
        after_help = "Examples:\n  scuzz run\n  scuzz run --headless\n  scuzz run examples/studio\n  scuzz run --headless --script examples/studio/build/record.script --dump examples/studio/build/debug.dump examples/studio\n  scuzz run --watch --headless\n\nDesktop/Mobile `run` writes build/record.script (live OS input) and build/debug.dump. Replay Headless with --script and --dump. `[ui] run --watch` is hot reload. `build/inject.script` stays watch playback only."
    )]
    Run {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Force Headless UiRuntime (no display needed)
        #[arg(long)]
        headless: bool,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
        /// Keep running; [ui] hot-reloads the View tree; IO-only kills and reruns on source change
        #[arg(long)]
        watch: bool,
        /// Inject script path (SCUZZ_UI_SCRIPT); played after the first pump
        #[arg(long, value_name = "PATH")]
        script: Option<PathBuf>,
        /// Structural dump path at finish (SCUZZ_FUZZ_DUMP)
        #[arg(long, value_name = "PATH")]
        dump: Option<PathBuf>,
    },
    /// Watch sources and rebuild on change (compile loop, not hot reload)
    #[command(
        after_help = "Examples:\n  scuzz watch\n  scuzz watch examples/hello\n  scuzz run --watch --headless\n"
    )]
    Watch {
        #[arg(default_value = ".")]
        path: PathBuf,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
    },
    /// Run tests: Headless structural goldens for [ui] packages; IO smoke (TESTRT exit 0) otherwise
    #[command(
        after_help = "Examples:\n  scuzz test\n  scuzz test --update\n  scuzz test --pixels examples/counter\n  scuzz test examples/io\n\n[ui] packages need goldens/. Seed with --update, then compare without it. Missing scuzz.toml fails."
    )]
    Test {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Rewrite / seed goldens from Headless snapshots
        #[arg(long)]
        update: bool,
        /// Also compare / seed PNG pixel goldens
        #[arg(long)]
        pixels: bool,
    },
    /// Format-verify src/ + parse + typecheck (the linter; no codegen / link). JSON with --message-format=json.
    #[command(
        after_help = "Examples:\n  scuzz check\n  scuzz check examples/kernel\n  scuzz check --message-format=json\n"
    )]
    Check {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Language server wrapping `scuzz check` JSON diagnostics (stdin/stdout LSP)
    #[command(
        after_help = "Open buffers overlay disk text on didOpen / didChange / didClose. Hover, completion, definition, document symbols, and references use the same check parse.\n\nExamples:\n  scuzz lsp\n  scuzz lsp examples/hello\n"
    )]
    Lsp {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Format Scuzz Lang sources under src/
    #[command(
        after_help = "Examples:\n  scuzz fmt\n  scuzz fmt --check\n  scuzz fmt examples/hello\n"
    )]
    Fmt {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Check only (nonzero exit if files need reformat)
        #[arg(long)]
        check: bool,
    },
    /// Search in-source laws under TestRuntime ([ui] events × schedules; IO-only schedules)
    #[command(
        after_help = "Examples:\n  scuzz fuzz --iters 16\n  scuzz fuzz --iters 16 examples/io\n  scuzz fuzz --exhaust --depth 1\n  scuzz fuzz --replay build/fuzz/repro.toml\n"
    )]
    Fuzz {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Seeded search iterations (scripts or schedule seeds)
        #[arg(long, default_value_t = 32)]
        iters: i64,
        /// Deterministic LCG seed
        #[arg(long, default_value_t = 42)]
        seed: i64,
        /// Exhaustive [ui] event alphabet (needs --depth)
        #[arg(long)]
        exhaust: bool,
        /// Exhaustion depth (with --exhaust)
        #[arg(long)]
        depth: Option<i64>,
        /// Replay a repro.toml (events + optional schedule_seed)
        #[arg(long)]
        replay: Option<PathBuf>,
    },
    /// Mutate live def bodies (or residual oracles with --oracles) and probe
    #[command(
        after_help = "Examples:\n  scuzz mutate\n  scuzz mutate --limit 16 --iters 4\n  scuzz mutate --oracles examples/counter --limit 4 --iters 8\n  scuzz mutate examples/hello\n"
    )]
    Mutate {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Max mutation sites to probe
        #[arg(long, default_value_t = 16)]
        limit: i64,
        /// Fuzz iters per mutant after the idle probe (`0` is idle only)
        #[arg(long, default_value_t = 4)]
        iters: i64,
        /// Deterministic LCG seed
        #[arg(long, default_value_t = 42)]
        seed: i64,
        /// Mutate residual Law.check / Law.assert / .require predicates
        #[arg(long)]
        oracles: bool,
    },
    /// Create a new Scuzz Lang project
    #[command(
        after_help = "Examples:\n  scuzz new mycli\n  scuzz new myapp --ui\n  scuzz new myapp --ui --path /tmp\n"
    )]
    New {
        name: String,
        #[arg(long, default_value = ".")]
        path: PathBuf,
        /// Scaffold a Headless UI sample instead of an IO hello
        #[arg(long)]
        ui: bool,
    },
    /// Package a project for host, Android, or iOS
    #[command(
        after_help = "Examples:\n  scuzz package --target host\n  scuzz package --target all examples/counter\n  scuzz package --target ios examples/counter\n  scuzz package --target android examples/counter\n\nios builds a signed simulator .app. It needs Xcode. android packs a debug APK. It needs the NDK and the Android SDK. Missing xcrun, NDK, or SDK fails with one install line.\n"
    )]
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
            eprintln!("scuzz error: {e:#}");
            ExitCode::FAILURE
        }
    }
}

fn real_main() -> Result<ExitCode> {
    let cli = Cli::parse();
    let json = cli.message_format == "json";
    match cli.command {
        Commands::Build {
            path,
            out_dir,
            full,
            verify,
        } => {
            let out = build(&path, &out_dir, !full, verify)?;
            if out.cache_hit {
                eprintln!("up-to-date {}", out.executable.display());
            } else {
                eprintln!("built {}", out.executable.display());
            }
            Ok(ExitCode::SUCCESS)
        }
        Commands::Check { path } => {
            let diags = scuzz_compiler::check_project(&path)?;
            if diags.is_empty() {
                if json {
                    println!("[]");
                } else {
                    eprintln!("scuzz check ok");
                }
                Ok(ExitCode::SUCCESS)
            } else {
                let out = scuzz_compiler::format_diagnostics(&diags, json);
                if json {
                    println!("{out}");
                } else {
                    eprintln!("{out}");
                }
                Ok(ExitCode::FAILURE)
            }
        }
        Commands::Lsp { path } => {
            scuzz_compiler::run_lsp(&path)?;
            Ok(ExitCode::SUCCESS)
        }
        Commands::Run {
            path,
            headless,
            out_dir,
            watch,
            script,
            dump,
        } => {
            if watch && (script.is_some() || dump.is_some()) {
                bail!(
                    "scuzz run --watch does not accept --script or --dump. Run without --watch: scuzz run --headless --script PATH --dump PATH"
                );
            }
            if watch {
                return watch_run(&path, &out_dir, headless);
            }
            run_once(
                &path,
                &out_dir,
                headless,
                script.as_deref(),
                dump.as_deref(),
            )
        }
        Commands::Watch { path, out_dir } => watch_build(&path, &out_dir),
        Commands::Test {
            path,
            update,
            pixels,
        } => {
            let project_dir = resolve_dir(&path)?;
            if !project_dir.join("scuzz.toml").is_file() {
                bail!(
                    "missing scuzz.toml in {}. Create a package with `scuzz new myapp` or `scuzz new myapp --ui`.",
                    project_dir.display()
                );
            }
            let out = build(&path, &PathBuf::from("build"), false, false)?;
            eprintln!("project compile smoke ok");
            if out.manifest.ui.is_none() {
                run_io_smoke(&out.executable)?;
            } else {
                run_goldens(&project_dir, &out.executable, update, pixels)?;
            }
            eprintln!("scuzz test ok");
            Ok(ExitCode::SUCCESS)
        }
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
                    dir.join("scuzz.toml"),
                    format!(
                        r#"[package]
name = "{package_name}"
version = "0.1.0"

[ui]
default_runtime = "headless"
headless_size = [200, 120]
headless_scale = 1.0
bundle_id = "dev.scuzz.{package_name}"
"#
                    ),
                )?;
                std::fs::write(
                    dir.join("src/Main.scuzz"),
                    r#"@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column(
      View.text("Counter"),
      View.bindText(label),
      View.row(View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)))
    ))
  } yield ()
"#,
                )?;
                eprintln!(
                    "created {} (ui) — next: scuzz test --update && scuzz test && scuzz run --headless",
                    dir.display()
                );
            } else {
                std::fs::write(
                    dir.join("scuzz.toml"),
                    format!(
                        r#"[package]
name = "{package_name}"
version = "0.1.0"
"#
                    ),
                )?;
                std::fs::write(
                    dir.join("src/Main.scuzz"),
                    r#"@main def main: IO[Unit] =
  IO.println("Hello, Scuzz!").flatMap(_ => IO.println("ready."))
"#,
                )?;
                eprintln!("created {} — next: scuzz test && scuzz run", dir.display());
            }
            Ok(ExitCode::SUCCESS)
        }
        Commands::Fuzz {
            path,
            iters,
            seed,
            exhaust,
            depth,
            replay,
        } => cmd_fuzz::cmd_fuzz(&path, replay.as_deref(), iters, seed, exhaust, depth),
        Commands::Mutate {
            path,
            limit,
            iters,
            seed,
            oracles,
        } => cmd_mutate::cmd_mutate(&path, limit, iters, seed, oracles),
        Commands::Package {
            path,
            target,
            out_dir,
        } => package_project(&path, &target, &out_dir),
    }
}

/// Mobile/Desktop peer env: runtime kind, logical size, live input record.
fn apply_peer_env(
    cmd: &mut Command,
    manifest: &scuzz_compiler::manifest::Manifest,
    project_dir: &Path,
    mobile: bool,
) {
    cmd.env(
        "SCUZZ_UI_RUNTIME",
        if mobile { "mobile" } else { "desktop" },
    );
    if mobile {
        cmd.env("SCUZZ_MOBILE_SHELL", "1");
    }
    if let Some(ui) = &manifest.ui {
        cmd.env("SCUZZ_UI_WIDTH", ui.width().to_string());
        cmd.env("SCUZZ_UI_HEIGHT", ui.height().to_string());
    }
    cmd.env(
        "SCUZZ_UI_RECORD",
        project_dir.join("build").join("record.script"),
    );
}

fn run_once(
    path: &Path,
    out_dir: &Path,
    headless: bool,
    script: Option<&Path>,
    dump: Option<&Path>,
) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    let effective = effective_ui_runtime(&manifest, headless);
    let use_headless = effective.eq_ignore_ascii_case("headless");
    let use_mobile = effective.eq_ignore_ascii_case("mobile");
    let use_desktop = effective.eq_ignore_ascii_case("desktop")
        || (!use_headless && !use_mobile && manifest.ui.is_some());

    let out = build(path, &out_dir.to_path_buf(), true, false)?;
    let mut cmd = Command::new(&out.executable);
    if use_headless && (headless || manifest.ui.is_some()) {
        let snap = out
            .executable
            .parent()
            .unwrap_or(Path::new("."))
            .join("snapshot.png");
        apply_ui_env(&mut cmd, &manifest, &snap, /*tap*/ false);
        eprintln!("scuzz run --headless → snapshot {}", snap.display());
    } else if use_mobile {
        apply_peer_env(&mut cmd, &manifest, &project_dir, true);
        cmd.env(
            "SCUZZ_UI_DEBUG_DUMP",
            project_dir.join("build").join("debug.dump"),
        );
        eprintln!("scuzz run → UiRuntime.Mobile (host shell)");
    } else if use_desktop {
        // Desktop peer + desktop embedder (X11 / Cocoa) when available.
        apply_peer_env(&mut cmd, &manifest, &project_dir, false);
        cmd.env(
            "SCUZZ_UI_DEBUG_DUMP",
            project_dir.join("build").join("debug.dump"),
        );
        eprintln!("scuzz run → UiRuntime.Desktop (desktop embedder)");
    }
    if let Some(script) = script {
        cmd.env("SCUZZ_UI_SCRIPT", script);
    }
    if let Some(dump) = dump {
        cmd.env("SCUZZ_FUZZ_DUMP", dump);
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
    eprintln!(
        "scuzz watch {} (rebuild on change; not hot reload)",
        project_dir.display()
    );
    loop {
        match build(path, &out_dir.to_path_buf(), true, false) {
            Ok(out) if out.cache_hit => eprintln!("up-to-date {}", out.executable.display()),
            Ok(out) => eprintln!("rebuilt {}", out.executable.display()),
            Err(e) => eprintln!("scuzz watch build error: {e:#}"),
        }
        if !wait_for_source_change(&project_dir, 60_000)? {
            // idle timeout — keep watching
            continue;
        }
    }
}

fn watch_run(path: &Path, out_dir: &Path, headless: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    if manifest.ui.is_none() {
        eprintln!(
            "scuzz run --watch {} (rebuild and rerun on source change; kills a still-running process; not hot reload)",
            project_dir.display()
        );
        return watch_run_io(&project_dir, path, out_dir);
    }
    eprintln!(
        "scuzz run --watch {} (hot reload: recompiles build/reload.dylib then stamp-reloads View tree; live dump build/debug.dump; inject build/inject.script)",
        project_dir.display()
    );
    watch_run_ui(&project_dir, path, out_dir, headless)
}

fn watch_run_io(project_dir: &Path, path: &Path, out_dir: &Path) -> Result<ExitCode> {
    loop {
        let mut child = match build(path, &out_dir.to_path_buf(), true, false) {
            Ok(out) => match Command::new(&out.executable).spawn() {
                Ok(c) => {
                    eprintln!("scuzz run --watch: running (pid {})", c.id());
                    c
                }
                Err(e) => {
                    eprintln!("scuzz watch run error: {e:#}");
                    let _ = wait_for_source_change(project_dir, 60_000)?;
                    continue;
                }
            },
            Err(e) => {
                eprintln!("scuzz watch run error: {e:#}");
                let _ = wait_for_source_change(project_dir, 60_000)?;
                continue;
            }
        };
        loop {
            if wait_for_source_change(project_dir, 400)? {
                let _ = child.kill();
                let _ = child.wait();
                break;
            }
            if child.try_wait()?.is_some() {
                let _ = wait_for_source_change(project_dir, 60_000)?;
                break;
            }
        }
    }
}

fn watch_run_ui(
    project_dir: &Path,
    path: &Path,
    out_dir: &Path,
    headless: bool,
) -> Result<ExitCode> {
    let stamp = project_dir.join("build").join("reload.stamp");
    if let Some(parent) = stamp.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(&stamp, "0\n")?;
    let mut child: Option<std::process::Child> = None;
    let mut gen: u64 = 0;
    loop {
        let dead = match child.as_mut() {
            Some(c) => c.try_wait()?.is_some(),
            None => true,
        };
        if dead {
            if let Some(mut c) = child.take() {
                let _ = c.wait();
            }
            match spawn_ui_keep(path, out_dir, headless, &stamp) {
                Ok(c) => {
                    eprintln!("scuzz run --watch: running (pid {})", c.id());
                    child = Some(c);
                }
                Err(e) => eprintln!("scuzz watch run error: {e:#}"),
            }
        }
        if wait_for_source_change(project_dir, 400)? {
            match build(path, &out_dir.to_path_buf(), true, false) {
                Ok(_) => {
                    gen += 1;
                    std::fs::write(&stamp, format!("{gen}\n"))?;
                    eprintln!("scuzz: view reload stamp {gen}");
                }
                Err(e) => eprintln!("scuzz watch compile error: {e:#}"),
            }
        }
    }
}

fn spawn_ui_keep(
    path: &Path,
    out_dir: &Path,
    headless: bool,
    stamp: &Path,
) -> Result<std::process::Child> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))?;
    let effective = effective_ui_runtime(&manifest, headless);
    let use_headless = effective.eq_ignore_ascii_case("headless");
    let use_mobile = effective.eq_ignore_ascii_case("mobile");
    let use_desktop = effective.eq_ignore_ascii_case("desktop")
        || (!use_headless && !use_mobile && manifest.ui.is_some());
    let out = build(path, &out_dir.to_path_buf(), true, false)?;
    let mut cmd = Command::new(&out.executable);
    cmd.env("SCUZZ_UI_RELOAD_STAMP", stamp);
    cmd.env(
        "SCUZZ_UI_RELOAD_CODE",
        project_dir.join("build").join("reload.dylib"),
    );
    cmd.env(
        "SCUZZ_UI_DEBUG_DUMP",
        project_dir.join("build").join("debug.dump"),
    );
    cmd.env(
        "SCUZZ_UI_INJECT",
        project_dir.join("build").join("inject.script"),
    );
    if use_headless {
        cmd.env("SCUZZ_UI_RUNTIME", "headless");
        if let Some(ui) = &manifest.ui {
            cmd.env("SCUZZ_UI_WIDTH", ui.width().to_string());
            cmd.env("SCUZZ_UI_HEIGHT", ui.height().to_string());
            cmd.env("SCUZZ_UI_SCALE", ui.headless_scale.to_string());
        }
    } else if use_mobile {
        apply_peer_env(&mut cmd, &manifest, &project_dir, true);
    } else if use_desktop {
        apply_peer_env(&mut cmd, &manifest, &project_dir, false);
    }
    cmd.spawn()
        .with_context(|| format!("running {}", out.executable.display()))
}

fn fmt_project(path: &Path, check: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let src = project_dir.join("src");
    if !src.is_dir() {
        bail!("missing src/ in {}", project_dir.display());
    }
    let mut dirty = 0usize;
    for p in collect_fmt_sources(&src)? {
        let text = std::fs::read_to_string(&p)?;
        let formatted =
            format_source(&text).with_context(|| format!("formatting {}", p.display()))?;
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
    eprintln!("scuzz fmt ok");
    Ok(ExitCode::SUCCESS)
}

fn effective_ui_runtime(manifest: &scuzz_compiler::manifest::Manifest, headless: bool) -> String {
    if headless {
        return "headless".into();
    }
    let env_rt = std::env::var("SCUZZ_UI_RUNTIME").unwrap_or_default();
    if !env_rt.is_empty() {
        return env_rt;
    }
    manifest
        .ui
        .as_ref()
        .map(|u| u.default_runtime.clone())
        .unwrap_or_default()
}

fn apply_ui_env(
    cmd: &mut Command,
    manifest: &scuzz_compiler::manifest::Manifest,
    snapshot: &Path,
    tap: bool,
) {
    cmd.env("SCUZZ_SNAPSHOT_PATH", snapshot);
    cmd.env("SCUZZ_UI_RUNTIME", "headless");
    // Isolate studio Fs path so golden runs do not share one temp file.
    let todo_path = std::env::temp_dir().join(format!(
        "scuzz-todo-{}-{}",
        std::process::id(),
        snapshot
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("snap")
    ));
    let _ = std::fs::remove_file(&todo_path);
    cmd.env("SCUZZ_TODO_PATH", &todo_path);
    if let Some(ui) = &manifest.ui {
        cmd.env("SCUZZ_UI_WIDTH", ui.width().to_string());
        cmd.env("SCUZZ_UI_HEIGHT", ui.height().to_string());
        cmd.env("SCUZZ_UI_SCALE", ui.headless_scale.to_string());
    } else {
        cmd.env("SCUZZ_UI_WIDTH", "200");
        cmd.env("SCUZZ_UI_HEIGHT", "120");
        cmd.env("SCUZZ_UI_SCALE", "1");
    }
    if tap {
        cmd.env("SCUZZ_UI_TAP", "1");
        if let Some(ui) = &manifest.ui {
            if let Some(n) = ui.tap_button {
                cmd.env("SCUZZ_UI_TAP_N", n.to_string());
            }
            if let Some(text) = &ui.tap_text {
                cmd.env("SCUZZ_UI_TEXT", text);
            }
        }
    }
}

fn run_io_smoke(exe: &Path) -> Result<()> {
    let status = Command::new(exe)
        .env("SCUZZ_TESTRT", "1")
        .env("SCUZZ_SERVE", "1")
        .env("SCUZZ_KIT", "sealed")
        .status()
        .with_context(|| format!("IO smoke {}", exe.display()))?;
    if !status.success() {
        bail!("IO smoke failed: {}", exe.display());
    }
    eprintln!("IO smoke ok");
    Ok(())
}

fn run_goldens(project_dir: &Path, exe: &Path, update: bool, pixels: bool) -> Result<()> {
    let goldens = project_dir.join("goldens");
    if !goldens.is_dir() {
        bail!(
            "missing goldens/ for [ui] package {}. Seed with `scuzz test --update`.",
            project_dir.display()
        );
    }
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))?;
    let name = manifest.package.name.as_str();
    let out_dir = exe.parent().unwrap_or(Path::new("."));

    let mut dumps: Vec<PathBuf> = Vec::new();
    let mut pngs: Vec<PathBuf> = Vec::new();
    for entry in std::fs::read_dir(&goldens)? {
        let entry = entry?;
        let path = entry.path();
        match path.extension().and_then(|e| e.to_str()) {
            Some("dump") => dumps.push(path),
            Some("png") => pngs.push(path),
            _ => {}
        }
    }
    dumps.sort();
    pngs.sort();

    if dumps.is_empty() {
        if !update {
            bail!(
                "no structural goldens in {}. Seed with `scuzz test --update`.",
                goldens.display()
            );
        }
        let base = goldens.join(format!("{name}.dump"));
        let tap = goldens.join(format!("{name}_after_tap.dump"));
        capture_structural(
            exe,
            &manifest,
            &base,
            out_dir.join(format!("{name}.actual.png")),
            false,
        )?;
        capture_structural(
            exe,
            &manifest,
            &tap,
            out_dir.join(format!("{name}_after_tap.actual.png")),
            true,
        )?;
        eprintln!("seeded goldens: {}.dump {}_after_tap.dump", name, name);
        if pixels {
            let base_png = goldens.join(format!("{name}.png"));
            let tap_png = goldens.join(format!("{name}_after_tap.png"));
            std::fs::copy(out_dir.join(format!("{name}.actual.png")), &base_png)?;
            std::fs::copy(
                out_dir.join(format!("{name}_after_tap.actual.png")),
                &tap_png,
            )?;
            eprintln!("seeded pixel goldens: {}.png {}_after_tap.png", name, name);
        }
        return Ok(());
    }

    for path in &dumps {
        let stem = path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("golden");
        let tap = stem.ends_with("_after_tap");
        let actual_dump = out_dir.join(format!("{stem}.actual.dump"));
        let actual_png = out_dir.join(format!("{stem}.actual.png"));
        capture_structural(exe, &manifest, &actual_dump, actual_png.clone(), tap)?;
        if update {
            std::fs::copy(&actual_dump, path)
                .with_context(|| format!("updating golden {}", path.display()))?;
            eprintln!("golden updated: {stem}.dump");
            if pixels {
                let png_path = goldens.join(format!("{stem}.png"));
                if actual_png.is_file() {
                    std::fs::copy(&actual_png, &png_path)?;
                    eprintln!("golden updated: {stem}.png");
                }
            }
        } else {
            let expected = std::fs::read_to_string(path)?;
            let got = std::fs::read_to_string(&actual_dump)?;
            if expected != got {
                bail!(
                    "structural golden mismatch: {}\n--- expected ---\n{}--- actual ---\n{}",
                    path.display(),
                    expected,
                    got
                );
            }
            eprintln!("golden ok: {stem}.dump");
            if pixels {
                let png_path = goldens.join(format!("{stem}.png"));
                if png_path.is_file() {
                    let expected = std::fs::read(&png_path)?;
                    let got = std::fs::read(&actual_png)?;
                    if expected != got {
                        bail!(
                            "pixel golden mismatch: {} vs {} ({} vs {} bytes); re-run with --update --pixels",
                            png_path.display(),
                            actual_png.display(),
                            expected.len(),
                            got.len()
                        );
                    }
                    eprintln!("golden ok: {stem}.png");
                }
            }
        }
    }
    Ok(())
}

fn capture_structural(
    exe: &Path,
    manifest: &scuzz_compiler::manifest::Manifest,
    dump: &Path,
    snapshot: PathBuf,
    tap: bool,
) -> Result<()> {
    let mut cmd = Command::new(exe);
    apply_ui_env(&mut cmd, manifest, &snapshot, tap);
    cmd.env("SCUZZ_FUZZ_DUMP", dump);
    let status = cmd
        .status()
        .with_context(|| format!("running structural golden {}", dump.display()))?;
    if !status.success() {
        bail!("structural golden capture failed: {}", dump.display());
    }
    if !dump.is_file() {
        bail!("structural golden capture missing dump {}", dump.display());
    }
    Ok(())
}

fn build(
    path: &Path,
    out_dir: &Path,
    incremental: bool,
    verify: bool,
) -> Result<scuzz_compiler::CompileOutput> {
    compile_project(&support::compile_opts(path, out_dir, incremental, verify)?)
}

const IOS_XCODE_INSTALL: &str =
    "missing Xcode — install Xcode from the App Store, then xcode-select --install";

fn require_ios_simulator_sdk() -> Result<()> {
    let output = Command::new("xcrun")
        .args(["--sdk", "iphonesimulator", "--show-sdk-path"])
        .output();
    match output {
        Ok(out) if out.status.success() => {
            let path = String::from_utf8_lossy(&out.stdout);
            if path.trim().is_empty() {
                bail!("{IOS_XCODE_INSTALL}");
            }
            Ok(())
        }
        _ => bail!("{IOS_XCODE_INSTALL}"),
    }
}

const ANDROID_NDK_INSTALL: &str =
    "missing Android NDK — install the NDK, then set ANDROID_NDK_HOME";
const ANDROID_SDK_INSTALL: &str =
    "missing Android SDK — install the Android SDK, then set ANDROID_HOME";

fn android_sdk_roots() -> Vec<PathBuf> {
    let mut roots: Vec<PathBuf> = Vec::new();
    for key in ["ANDROID_HOME", "ANDROID_SDK_ROOT"] {
        if let Ok(p) = std::env::var(key) {
            if !p.is_empty() {
                roots.push(PathBuf::from(p));
            }
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        roots.push(PathBuf::from(&home).join("Library/Android/sdk"));
        roots.push(PathBuf::from(home).join("Android/Sdk"));
    }
    roots
}

fn ndk_clang(ndk: &Path) -> Option<PathBuf> {
    let prebuilt = ndk.join("toolchains/llvm/prebuilt");
    let hosts = std::fs::read_dir(&prebuilt).ok()?;
    for host in hosts.flatten() {
        let clang = host
            .path()
            .join("bin")
            .join("aarch64-linux-android24-clang");
        if clang.is_file() {
            return Some(clang);
        }
    }
    None
}

fn find_android_ndk() -> Option<PathBuf> {
    if let Ok(p) = std::env::var("ANDROID_NDK_HOME") {
        let pb = PathBuf::from(p);
        if ndk_clang(&pb).is_some() {
            return Some(pb);
        }
    }
    let mut found = Vec::new();
    for root in android_sdk_roots() {
        let ndk_root = root.join("ndk");
        let Ok(rd) = std::fs::read_dir(&ndk_root) else {
            continue;
        };
        for e in rd.flatten() {
            let p = e.path();
            if ndk_clang(&p).is_some() {
                found.push(p);
            }
        }
    }
    found.sort();
    found.pop()
}

fn sdk_has_packaging_tools(sdk: &Path) -> bool {
    let platforms = sdk.join("platforms");
    let tools = sdk.join("build-tools");
    let Ok(plats) = std::fs::read_dir(&platforms) else {
        return false;
    };
    let has_jar = plats
        .flatten()
        .any(|e| e.path().join("android.jar").is_file());
    if !has_jar {
        return false;
    }
    let Ok(bts) = std::fs::read_dir(&tools) else {
        return false;
    };
    bts.flatten().any(|e| {
        let p = e.path();
        p.join("aapt2").is_file()
            && p.join("d8").exists()
            && p.join("apksigner").exists()
            && p.join("zipalign").is_file()
    })
}

fn find_android_sdk() -> Option<PathBuf> {
    android_sdk_roots()
        .into_iter()
        .find(|root| sdk_has_packaging_tools(root))
}

fn require_android_ndk() -> Result<PathBuf> {
    find_android_ndk().ok_or_else(|| anyhow::anyhow!("{ANDROID_NDK_INSTALL}"))
}

fn require_android_sdk() -> Result<PathBuf> {
    find_android_sdk().ok_or_else(|| anyhow::anyhow!("{ANDROID_SDK_INSTALL}"))
}

fn package_android_ndk(
    project_dir: &Path,
    dest: &Path,
    name: &str,
    bundle_id: &str,
    mobile_dir: &Path,
) -> Result<()> {
    let ndk = require_android_ndk()?;
    let sdk = require_android_sdk()?;
    let ndk_script = mobile_dir.join("shells/android/build_ndk.sh");
    let apk_script = mobile_dir.join("shells/android/build_apk.sh");
    if !ndk_script.is_file() {
        bail!("missing Android NDK script {}", ndk_script.display());
    }
    if !apk_script.is_file() {
        bail!("missing Android APK script {}", apk_script.display());
    }
    let scuzz = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("scuzz"));
    let status = Command::new("bash")
        .arg(&ndk_script)
        .arg(project_dir)
        .env("SCUZZ_BUNDLE_ID", bundle_id)
        .env("SCUZZ", &scuzz)
        .env("ANDROID_NDK_HOME", &ndk)
        .status()
        .with_context(|| format!("running {}", ndk_script.display()))?;
    if !status.success() {
        bail!("Android NDK package failed");
    }
    let so = project_dir.join("build/android/lib/arm64-v8a/libscuzz.so");
    if !so.is_file() {
        bail!("Android NDK package did not emit {}", so.display());
    }
    let status = Command::new("bash")
        .arg(&apk_script)
        .arg(project_dir)
        .env("SCUZZ_BUNDLE_ID", bundle_id)
        .env("ANDROID_HOME", &sdk)
        .env("ANDROID_SDK_ROOT", &sdk)
        .status()
        .with_context(|| format!("running {}", apk_script.display()))?;
    if !status.success() {
        bail!("Android APK package failed");
    }
    let apk = project_dir
        .join("build/android")
        .join(format!("{name}.apk"));
    if !apk.is_file() {
        bail!("Android APK package did not emit {}", apk.display());
    }
    std::fs::copy(&apk, dest.join(format!("{name}.apk")))
        .with_context(|| format!("copying {}", apk.display()))?;
    let lib_dir = dest.join("lib/arm64-v8a");
    std::fs::create_dir_all(&lib_dir)?;
    std::fs::copy(&so, lib_dir.join("libscuzz.so"))
        .with_context(|| format!("copying {}", so.display()))?;
    write_package_meta(dest, name, "android", bundle_id)?;
    Ok(())
}

fn package_ios_sim(
    project_dir: &Path,
    dest: &Path,
    name: &str,
    bundle_id: &str,
    script: &Path,
) -> Result<()> {
    require_ios_simulator_sdk()?;
    if !script.is_file() {
        bail!("missing iOS simulator script {}", script.display());
    }
    let scuzz = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("scuzz"));
    let status = Command::new("bash")
        .arg(script)
        .arg(project_dir)
        .env("SCUZZ_BUNDLE_ID", bundle_id)
        .env("SCUZZ", &scuzz)
        .status()
        .with_context(|| format!("running {}", script.display()))?;
    if !status.success() {
        bail!("iOS simulator package failed");
    }
    let app = project_dir
        .join("build/ios-sim")
        .join(format!("{name}.app"));
    if !app.is_dir() {
        bail!("iOS simulator package did not emit {}", app.display());
    }
    copy_dir(&app, &dest.join(format!("{name}.app")))?;
    write_package_meta(dest, name, "ios", bundle_id)?;
    Ok(())
}

fn package_project(path: &Path, target: &str, out_dir: &Path) -> Result<ExitCode> {
    let target_lc = target.to_ascii_lowercase();
    let targets: Vec<&str> = if target_lc == "all" {
        vec!["host", "android", "ios"]
    } else if matches!(target_lc.as_str(), "host" | "android" | "ios") {
        vec![target_lc.as_str()]
    } else {
        bail!(
            "unknown package target '{target}' (host|android|ios|all). Example: scuzz package --target host"
        );
    };
    let want_host = targets.contains(&"host");
    let want_ios = targets.contains(&"ios");
    let want_android = targets.contains(&"android");
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", project_dir.display()))?;
    let package_out = if out_dir.is_absolute() {
        out_dir.to_path_buf()
    } else {
        project_dir.join(out_dir).join("package")
    };
    std::fs::create_dir_all(&package_out)?;

    let runtime_dir =
        find_runtime_dir(&std::env::current_dir()?).or_else(|_| find_runtime_dir(&project_dir))?;
    let mobile_dir = runtime_dir
        .parent()
        .map(|p| p.join("embedder-mobile"))
        .unwrap_or_else(|| PathBuf::from("crates/embedder-mobile"));
    if !mobile_dir.join("Makefile").is_file() {
        bail!("missing embedder-mobile at {}", mobile_dir.display());
    }

    if want_ios {
        require_ios_simulator_sdk()?;
    }
    if want_android {
        require_android_ndk()?;
        require_android_sdk()?;
    }

    let compiled = if want_host {
        let clang = std::env::var("SCUZZ_CLANG").unwrap_or_else(|_| "clang".into());
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
        Some(build(path, &PathBuf::from("build"), true, false)?)
    } else {
        None
    };

    let bundle_id = manifest
        .ui
        .as_ref()
        .map(|u| u.bundle_id.as_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("dev.scuzz.app");

    for t in targets {
        let dest = package_out.join(t);
        if dest.exists() {
            std::fs::remove_dir_all(&dest)?;
        }
        std::fs::create_dir_all(&dest)?;
        match t {
            "host" => {
                let exe = &compiled.as_ref().expect("host compile").executable;
                write_host_package(&dest, exe, &manifest.package.name, bundle_id)?;
            }
            "android" => {
                package_android_ndk(
                    &project_dir,
                    &dest,
                    &manifest.package.name,
                    bundle_id,
                    &mobile_dir,
                )?;
            }
            "ios" => {
                package_ios_sim(
                    &project_dir,
                    &dest,
                    &manifest.package.name,
                    bundle_id,
                    &mobile_dir.join("shells/ios/build_sim.sh"),
                )?;
            }
            _ => unreachable!(),
        }
        eprintln!("packaged {} → {}", t, dest.display());
    }
    eprintln!("scuzz package ok ({})", package_out.display());
    Ok(ExitCode::SUCCESS)
}

fn write_host_package(dest: &Path, exe: &Path, name: &str, bundle_id: &str) -> Result<()> {
    let dest_exe = dest.join(name);
    std::fs::copy(exe, &dest_exe)
        .with_context(|| format!("copying {} into {}", exe.display(), dest_exe.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = std::fs::metadata(&dest_exe)?.permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&dest_exe, perms)?;
    }
    let run_sh = dest.join("run.sh");
    std::fs::write(
        &run_sh,
        format!(
            r#"#!/usr/bin/env bash
# Host Mobile shell smoke. Same app binary; Mobile peer + host embedder.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
export SCUZZ_UI_RUNTIME=mobile
export SCUZZ_MOBILE_SHELL=1
exec "$DIR/{name}" "$@"
"#
        ),
    )?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = std::fs::metadata(&run_sh)?.permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&run_sh, perms)?;
    }
    write_package_meta(dest, name, "host", bundle_id)?;
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

fn copy_dir(src: &Path, dst: &Path) -> Result<()> {
    if !src.is_dir() {
        bail!("missing directory {}", src.display());
    }
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let name = entry.file_name();
        // Skip caches / VCS junk (for example .gradle).
        if name.to_string_lossy().starts_with('.') {
            continue;
        }
        let from = entry.path();
        let to = dst.join(&name);
        if from.is_dir() {
            copy_dir(&from, &to)?;
        } else {
            std::fs::copy(&from, &to)
                .with_context(|| format!("copy {} → {}", from.display(), to.display()))?;
        }
    }
    Ok(())
}

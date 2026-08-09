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
    about = "ScalUI — Scala-inspired native UI stack (Stage-0 CLI)"
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
    /// Run tests: runtime unit tests + Headless golden PNGs when present
    Test {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Format ScalUI sources under src/
    Fmt {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Check only (nonzero exit if would reformat)
        #[arg(long)]
        check: bool,
    },
    /// Start the basic ScalUI language server (stdio LSP)
    Lsp,
    /// Create a new ScalUI project
    New {
        name: String,
        #[arg(long, default_value = ".")]
        path: PathBuf,
        /// Scaffold a Headless UI sample instead of IO hello
        #[arg(long)]
        ui: bool,
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
        Commands::Test { path } => {
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

            let project_dir = resolve_dir(&path)?;
            if project_dir.join("scalui.toml").is_file() {
                let out = build(&path, &PathBuf::from("build"), false)?;
                eprintln!("project compile smoke ok");
                run_goldens(&project_dir, &out.executable)?;
            }
            eprintln!("scalui test ok");
            Ok(ExitCode::SUCCESS)
        }
        Commands::Fmt { path, check } => fmt_project(&path, check),
        Commands::Lsp => {
            scalui_compiler::lsp::run_stdio()?;
            Ok(ExitCode::SUCCESS)
        }
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
"#
                    ),
                )?;
                std::fs::write(
                    dir.join("src/Main.scala"),
                    r#"@main def main: IO[Unit] =
  Ui.runCounter
"#,
                )?;
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
  IO.println("Hello, ScalUI!").flatMap(_ => IO.println("Phase 0 online."))
"#,
                )?;
            }
            eprintln!("created {}", dir.display());
            Ok(ExitCode::SUCCESS)
        }
    }
}

fn run_once(path: &Path, out_dir: &Path, headless: bool) -> Result<ExitCode> {
    let project_dir = resolve_dir(path)?;
    let manifest = load_manifest(&project_dir.join("scalui.toml"))
        .with_context(|| format!("reading {}/scalui.toml", project_dir.display()))?;
    let use_headless = headless
        || manifest
            .ui
            .as_ref()
            .map(|u| u.default_runtime.eq_ignore_ascii_case("headless"))
            .unwrap_or(false);

    let out = build(path, &out_dir.to_path_buf(), true)?;
    let mut cmd = Command::new(&out.executable);
    if use_headless {
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
    } else {
        // Window peer + optional X11 embedder when DISPLAY is set.
        cmd.env("SCALUI_UI_RUNTIME", "window");
        if let Some(ui) = &manifest.ui {
            cmd.env("SCALUI_UI_WIDTH", ui.width().to_string());
            cmd.env("SCALUI_UI_HEIGHT", ui.height().to_string());
        }
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
    }
}

fn run_goldens(project_dir: &Path, exe: &Path) -> Result<()> {
    let goldens = project_dir.join("goldens");
    if !goldens.is_dir() {
        return Ok(());
    }
    let manifest = load_manifest(&project_dir.join("scalui.toml"))?;
    let mut ran = 0usize;
    for entry in std::fs::read_dir(&goldens)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("png") {
            continue;
        }
        let stem = path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("golden");
        let tap = stem.ends_with("_after_tap");
        let out_dir = exe.parent().unwrap_or(Path::new("."));
        let actual = out_dir.join(format!("{stem}.actual.png"));
        let mut cmd = Command::new(exe);
        apply_ui_env(&mut cmd, &manifest, &actual, tap);
        let status = cmd
            .status()
            .with_context(|| format!("running golden {stem}"))?;
        if !status.success() {
            bail!("golden {stem}: executable failed");
        }
        if !actual.is_file() {
            bail!("golden {stem}: missing actual snapshot {}", actual.display());
        }
        let expected = std::fs::read(&path)?;
        let got = std::fs::read(&actual)?;
        if expected != got {
            bail!(
                "golden mismatch: {} vs {} ({} vs {} bytes)",
                path.display(),
                actual.display(),
                expected.len(),
                got.len()
            );
        }
        eprintln!("golden ok: {stem}");
        ran += 1;
    }
    if ran == 0 {
        eprintln!("note: goldens/ present but no .png files");
    }
    Ok(())
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

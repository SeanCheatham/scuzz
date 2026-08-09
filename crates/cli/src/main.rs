use anyhow::{bail, Context, Result};
use clap::{Parser, Subcommand};
use scalui_compiler::driver::{compile_project, find_runtime_dir, CompileOptions};
use std::path::PathBuf;
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
    },
    /// Build and run a ScalUI project
    Run {
        #[arg(default_value = ".")]
        path: PathBuf,
        /// Force headless mode (Phase 0: no UI yet; accepted for forward compat)
        #[arg(long)]
        headless: bool,
        #[arg(long, default_value = "build")]
        out_dir: PathBuf,
    },
    /// Run tests (Phase 0: runtime IO tests + compile smoke)
    Test {
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Create a new ScalUI project
    New {
        name: String,
        #[arg(long, default_value = ".")]
        path: PathBuf,
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
        Commands::Build { path, out_dir } => {
            let out = build(&path, &out_dir)?;
            eprintln!("built {}", out.executable.display());
            Ok(ExitCode::SUCCESS)
        }
        Commands::Run {
            path,
            headless,
            out_dir,
        } => {
            if headless {
                eprintln!("note: --headless accepted (UI Headless runtime arrives in Phase 1)");
            }
            let out = build(&path, &out_dir)?;
            let status = Command::new(&out.executable)
                .status()
                .with_context(|| format!("running {}", out.executable.display()))?;
            if status.success() {
                Ok(ExitCode::SUCCESS)
            } else {
                Ok(ExitCode::from(status.code().unwrap_or(1) as u8))
            }
        }
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
            // Also compile the project if it looks like one
            if path.join("scalui.toml").is_file() {
                let _ = build(&path, &path.join("build"))?;
                eprintln!("project compile smoke ok");
            }
            eprintln!("scalui test ok");
            Ok(ExitCode::SUCCESS)
        }
        Commands::New { name, path } => {
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
            eprintln!("created {}", dir.display());
            Ok(ExitCode::SUCCESS)
        }
    }
}

fn build(
    path: &PathBuf,
    out_dir: &PathBuf,
) -> Result<scalui_compiler::CompileOutput> {
    let project_dir = if path.is_absolute() {
        path.clone()
    } else {
        std::env::current_dir()?.join(path)
    };
    let out_dir = if out_dir.is_absolute() {
        out_dir.clone()
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
    })
}

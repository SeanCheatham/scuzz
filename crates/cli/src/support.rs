use anyhow::{Context, Result};
use scuzz_compiler::driver::{find_runtime_dir, CompileOptions};
use std::path::{Path, PathBuf};
use std::process::Command;

pub fn resolve_dir(path: &Path) -> Result<PathBuf> {
    if path.is_absolute() {
        Ok(path.to_path_buf())
    } else {
        Ok(std::env::current_dir()?.join(path))
    }
}

pub fn compile_opts(
    path: &Path,
    out_dir: &Path,
    incremental: bool,
    verify: bool,
) -> Result<CompileOptions> {
    let project_dir = resolve_dir(path)?;
    let out_dir = if out_dir.is_absolute() {
        out_dir.to_path_buf()
    } else {
        project_dir.join(out_dir)
    };
    let runtime_dir =
        find_runtime_dir(&std::env::current_dir()?).or_else(|_| find_runtime_dir(&project_dir))?;
    let clang = std::env::var("SCUZZ_CLANG").unwrap_or_else(|_| "clang".into());
    Ok(CompileOptions {
        project_dir,
        runtime_dir,
        out_dir,
        clang,
        incremental,
        verify,
    })
}

pub struct TestrtUi<'a> {
    pub script: &'a Path,
    pub dump: &'a Path,
    pub width: i32,
    pub height: i32,
}

/// Run a verify-graph binary under TestRuntime. `ui` sets Headless + script/dump.
pub fn run_testrt(
    exe: &Path,
    reached: &Path,
    schedule_seed: &str,
    fault_seed: &str,
    ui: Option<TestrtUi<'_>>,
    drive_script: Option<&Path>,
) -> Result<i32> {
    let mut cmd = Command::new(exe);
    cmd.env("SCUZZ_TESTRT", "1")
        .env("SCUZZ_SOMETIMES_DUMP", reached)
        .env("SCUZZ_SERVE", "1")
        .env("SCUZZ_KIT", "sealed");
    if let Some(parent) = reached.parent() {
        cmd.env("SCUZZ_CLASSIFY_DUMP", parent.join("classify.dump"));
    }
    if let Some(ui) = ui {
        cmd.env("SCUZZ_UI_RUNTIME", "headless")
            .env("SCUZZ_UI_SCRIPT", ui.script)
            .env("SCUZZ_FUZZ_DUMP", ui.dump)
            .env("SCUZZ_UI_WIDTH", ui.width.to_string())
            .env("SCUZZ_UI_HEIGHT", ui.height.to_string());
    }
    if let Some(p) = drive_script {
        cmd.env("SCUZZ_DRIVE_SCRIPT", p);
    }
    if !schedule_seed.is_empty() {
        cmd.env("SCUZZ_SCHED_SEED", schedule_seed);
    }
    if !fault_seed.is_empty() && fault_seed != "0" {
        cmd.env("SCUZZ_FAULT_SEED", fault_seed);
    }
    let status = cmd
        .status()
        .with_context(|| format!("running {}", exe.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        if let Some(sig) = status.signal() {
            eprintln!("scuzz: testrt killed by signal {sig}");
        }
    }
    Ok(status.code().unwrap_or(1))
}

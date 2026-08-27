use anyhow::{Context, Result};
use scuzz_compiler::driver::{find_runtime_dir, CompileOptions};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitStatus};
use std::time::{Duration, Instant};

/// Exit code when a TestRuntime probe exceeds its wall-clock limit.
pub const TESTRT_TIMEOUT_CODE: i32 = 124;

pub fn resolve_dir(path: &Path) -> Result<PathBuf> {
    if path.is_absolute() {
        Ok(path.to_path_buf())
    } else {
        Ok(std::env::current_dir()?.join(path))
    }
}
/// Resolve `--out-dir` against the project root. Absolute passes through.
pub fn resolve_out_dir(project_dir: &Path, out_dir: &Path) -> PathBuf {
    if out_dir.is_absolute() {
        out_dir.to_path_buf()
    } else {
        project_dir.join(out_dir)
    }
}

/// Runtime crate dir + `SCUZZ_CLANG` (default `clang`).
pub fn runtime_dir_and_clang(project_dir: &Path) -> Result<(PathBuf, String)> {
    let runtime_dir =
        find_runtime_dir(&std::env::current_dir()?).or_else(|_| find_runtime_dir(project_dir))?;
    let clang = std::env::var("SCUZZ_CLANG").unwrap_or_else(|_| "clang".into());
    Ok((runtime_dir, clang))
}

pub fn compile_opts(
    path: &Path,
    out_dir: &Path,
    incremental: bool,
    verify: bool,
) -> Result<CompileOptions> {
    let project_dir = resolve_dir(path)?;
    let out_dir = resolve_out_dir(&project_dir, out_dir);
    let (runtime_dir, clang) = runtime_dir_and_clang(&project_dir)?;
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
/// `timeline` overrides the SCUZZ_TIMELINE_DUMP target (default: sibling of `reached`).
/// When `limit` is set, kill the process when that duration elapses (exit 124).
pub fn run_testrt_limit(
    exe: &Path,
    reached: &Path,
    schedule_seed: &str,
    fault_seed: &str,
    ui: Option<TestrtUi<'_>>,
    drive_script: Option<&Path>,
    timeline: Option<&Path>,
    limit: Option<Duration>,
) -> Result<i32> {
    let mut cmd = Command::new(exe);
    cmd.env("SCUZZ_TESTRT", "1")
        .env("SCUZZ_SOMETIMES_DUMP", reached)
        .env("SCUZZ_SERVE", "1")
        .env("SCUZZ_KIT", "sealed");
    if let Some(parent) = reached.parent() {
        cmd.env("SCUZZ_CLASSIFY_DUMP", parent.join("classify.dump"));
        cmd.env("SCUZZ_STATE_VARIED_DUMP", parent.join("state.varied"));
        let tl = timeline
            .map(|t| t.to_path_buf())
            .unwrap_or_else(|| parent.join("timeline.txt"));
        cmd.env("SCUZZ_TIMELINE_DUMP", tl);
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
    let mut child = cmd
        .spawn()
        .with_context(|| format!("running {}", exe.display()))?;
    wait_child(&mut child, limit)
}

fn wait_child(child: &mut Child, limit: Option<Duration>) -> Result<i32> {
    match limit {
        None => status_code(child.wait()?),
        Some(limit) => wait_child_limit(child, limit),
    }
}

fn wait_child_limit(child: &mut Child, limit: Duration) -> Result<i32> {
    let deadline = Instant::now() + limit;
    loop {
        if let Some(status) = child.try_wait()? {
            return status_code(status);
        }
        if Instant::now() >= deadline {
            let _ = child.kill();
            let _ = child.wait();
            eprintln!("scuzz: testrt timeout");
            return Ok(TESTRT_TIMEOUT_CODE);
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn status_code(status: ExitStatus) -> Result<i32> {
    #[cfg(unix)]
    {
        use std::os::unix::process::ExitStatusExt;
        if let Some(sig) = status.signal() {
            eprintln!("scuzz: testrt killed by signal {sig}");
        }
    }
    Ok(status.code().unwrap_or(1))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wait_child_kills_on_timeout() {
        let mut child = Command::new("sleep").arg("30").spawn().unwrap();
        let t0 = Instant::now();
        let code = wait_child(&mut child, Some(Duration::from_millis(250))).unwrap();
        assert!(
            t0.elapsed() < Duration::from_secs(3),
            "timeout waited {:?}",
            t0.elapsed()
        );
        assert_eq!(code, TESTRT_TIMEOUT_CODE);
    }
}

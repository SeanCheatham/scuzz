use anyhow::Result;
use scuzz_compiler::driver::{find_runtime_dir, CompileOptions};
use std::path::{Path, PathBuf};

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

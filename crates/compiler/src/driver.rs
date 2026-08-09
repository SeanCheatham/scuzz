use crate::codegen::emit_llvm;
use crate::manifest::{load_manifest, Manifest};
use crate::parser::parse;
use crate::typ::typecheck;
use anyhow::{bail, Context, Result};
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone)]
pub struct CompileOptions {
    pub project_dir: PathBuf,
    /// Directory containing crates/runtime (repo root relative or absolute)
    pub runtime_dir: PathBuf,
    pub out_dir: PathBuf,
    pub clang: String,
}

#[derive(Debug, Clone)]
pub struct CompileOutput {
    pub executable: PathBuf,
    pub llvm_ir: PathBuf,
    pub manifest: Manifest,
}

pub fn compile_project(opts: &CompileOptions) -> Result<CompileOutput> {
    let manifest_path = opts.project_dir.join("scalui.toml");
    let manifest = load_manifest(&manifest_path)
        .with_context(|| format!("reading {}", manifest_path.display()))?;

    let source = find_main_source(&opts.project_dir)?;
    let src_text = std::fs::read_to_string(&source)
        .with_context(|| format!("reading {}", source.display()))?;

    let program = parse(&src_text).map_err(|e| anyhow::anyhow!("parse error: {e}"))?;
    typecheck(&program).map_err(|e| anyhow::anyhow!("{e}"))?;

    let ir = emit_llvm(&program);

    std::fs::create_dir_all(&opts.out_dir)?;
    build_runtime(&opts.runtime_dir, &opts.clang)?;

    let lib = opts.runtime_dir.join("build/libscalui_rt.a");
    // Join must use a relative file name — absolute package names would replace out_dir.
    let exe_name = Path::new(&manifest.package.name)
        .file_name()
        .and_then(|s| s.to_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("app");
    if exe_name.contains('/') || exe_name.contains('\\') {
        bail!("invalid package name for executable: {}", manifest.package.name);
    }
    let exe = opts.out_dir.join(exe_name);
    let ll_path = opts.out_dir.join(format!("{exe_name}.ll"));
    std::fs::write(&ll_path, &ir)?;
    let include = opts.runtime_dir.join("include");

    let status = Command::new(&opts.clang)
        .arg(&ll_path)
        .arg(&lib)
        .arg(format!("-I{}", include.display()))
        .arg("-o")
        .arg(&exe)
        .status()
        .with_context(|| "spawning clang")?;

    if !status.success() {
        bail!("clang failed to link {}", exe.display());
    }

    Ok(CompileOutput {
        executable: exe,
        llvm_ir: ll_path,
        manifest,
    })
}

fn find_main_source(project_dir: &Path) -> Result<PathBuf> {
    let src = project_dir.join("src");
    if !src.is_dir() {
        bail!("missing src/ in {}", project_dir.display());
    }
    let mut candidates: Vec<PathBuf> = Vec::new();
    for entry in std::fs::read_dir(&src)? {
        let entry = entry?;
        let path = entry.path();
        if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
            if matches!(ext, "scala" | "scalui") {
                candidates.push(path);
            }
        }
    }
    if candidates.is_empty() {
        bail!("no .scala / .scalui sources in {}", src.display());
    }
    // Prefer Main.*
    if let Some(main) = candidates.iter().find(|p| {
        p.file_stem()
            .and_then(|s| s.to_str())
            .is_some_and(|s| s.eq_ignore_ascii_case("main"))
    }) {
        return Ok(main.clone());
    }
    candidates.sort();
    Ok(candidates.remove(0))
}

fn build_runtime(runtime_dir: &Path, clang: &str) -> Result<()> {
    let status = Command::new("make")
        .arg("-C")
        .arg(runtime_dir)
        .arg("lib")
        .env("CC", clang)
        .status()
        .with_context(|| "building runtime (make)")?;
    if !status.success() {
        bail!("runtime build failed in {}", runtime_dir.display());
    }
    Ok(())
}

/// Resolve repo root from an optional override or by walking parents looking for crates/runtime.
pub fn find_runtime_dir(start: &Path) -> Result<PathBuf> {
    let mut cur = start.to_path_buf();
    loop {
        let candidate = cur.join("crates/runtime");
        if candidate.join("include/scalui_rt.h").is_file() {
            return Ok(candidate);
        }
        if !cur.pop() {
            bail!("could not find crates/runtime from {}", start.display());
        }
    }
}

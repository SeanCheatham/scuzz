use crate::codegen::emit_llvm;
use crate::lower::lower_program;
use crate::manifest::{load_manifest, Manifest};
use crate::parser::parse_sources;
use crate::typ::typecheck;
use anyhow::{bail, Context, Result};
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::SystemTime;

#[derive(Debug, Clone)]
pub struct CompileOptions {
    pub project_dir: PathBuf,
    /// Directory containing crates/runtime (repo root relative or absolute)
    pub runtime_dir: PathBuf,
    pub out_dir: PathBuf,
    pub clang: String,
    /// Skip clang link when fingerprint matches (incremental).
    pub incremental: bool,
}

impl CompileOptions {
    pub fn new(project_dir: PathBuf, runtime_dir: PathBuf, out_dir: PathBuf, clang: String) -> Self {
        Self {
            project_dir,
            runtime_dir,
            out_dir,
            clang,
            incremental: true,
        }
    }
}

#[derive(Debug, Clone)]
pub struct CompileOutput {
    pub executable: PathBuf,
    pub llvm_ir: PathBuf,
    pub manifest: Manifest,
    pub cache_hit: bool,
}

pub fn compile_project(opts: &CompileOptions) -> Result<CompileOutput> {
    let manifest_path = opts.project_dir.join("scuzz.toml");
    let manifest = load_manifest(&manifest_path)
        .with_context(|| format!("reading {}", manifest_path.display()))?;

    let sources = find_sources(&opts.project_dir)?;
    let mut named: Vec<(String, String)> = Vec::new();
    for path in &sources {
        let text = std::fs::read_to_string(path)
            .with_context(|| format!("reading {}", path.display()))?;
        let label = path
            .file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("source")
            .to_string();
        named.push((label, text));
    }

    let fingerprint = fingerprint_sources(&named, &manifest.package.name, manifest.ui.is_some());

    // Join must use a relative file name — absolute package names would replace out_dir.
    let exe_name = Path::new(&manifest.package.name)
        .file_name()
        .and_then(|s| s.to_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("app");
    if exe_name.contains('/') || exe_name.contains('\\') {
        bail!("invalid package name for executable: {}", manifest.package.name);
    }

    std::fs::create_dir_all(&opts.out_dir)?;
    let cache_dir = opts.project_dir.join(".scuzz");
    std::fs::create_dir_all(&cache_dir)?;
    let fp_path = cache_dir.join("fingerprint");
    let exe = opts.out_dir.join(exe_name);
    let ll_path = opts.out_dir.join(format!("{exe_name}.ll"));

    if opts.incremental
        && exe.is_file()
        && fp_path.is_file()
        && std::fs::read_to_string(&fp_path).unwrap_or_default().trim() == fingerprint
    {
        return Ok(CompileOutput {
            executable: exe,
            llvm_ir: ll_path,
            manifest,
            cache_hit: true,
        });
    }

    let program =
        parse_sources(&named).map_err(|e| anyhow::anyhow!("parse error: {e}"))?;
    let program = lower_program(program);
    typecheck(&program).map_err(|e| anyhow::anyhow!("{e}"))?;

    let ir = emit_llvm(&program);
    std::fs::write(&ll_path, &ir)?;

    build_runtime(&opts.runtime_dir, &opts.clang)?;

    let lib = opts.runtime_dir.join("build/libscuzz_rt.a");
    let include = opts.runtime_dir.join("include");
    let with_ui = manifest.ui.is_some();

    let mut link = Command::new(&opts.clang);
    link.arg(&ll_path).arg(&lib).arg(format!("-I{}", include.display()));

    if with_ui {
        let ffi_skia_dir = opts
            .runtime_dir
            .parent()
            .map(|p| p.join("ffi-skia"))
            .unwrap_or_else(|| PathBuf::from("crates/ffi-skia"));
        let skia_lib = ffi_skia_dir.join("build/libsk_capi.a");
        let skia_include = ffi_skia_dir.join("include");
        link.arg(&skia_lib)
            .arg(format!("-I{}", skia_include.display()));

        // Optional desktop (X11) + mobile host shells. Link when libs exist.
        let crates_dir = opts
            .runtime_dir
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from("crates"));
        let embedder_lib = crates_dir
            .join("embedder-desktop")
            .join("build/libscuzz_embedder.a");
        let mobile_lib = crates_dir
            .join("embedder-mobile")
            .join("build/libscuzz_mobile.a");
        // Strong embedder symbols must override weak stubs in libscuzz_rt.a.
        if embedder_lib.is_file() {
            push_force_load(&mut link, &embedder_lib);
            if cfg!(target_os = "linux") {
                link.arg("-lX11");
            } else if cfg!(target_os = "macos") {
                link.arg("-framework").arg("Cocoa");
                link.arg("-lobjc");
            }
        }
        if mobile_lib.is_file() {
            push_force_load(&mut link, &mobile_lib);
        }
    }

    link.arg("-lpthread").arg("-o").arg(&exe);
    let status = link.status().with_context(|| "spawning clang")?;

    if !status.success() {
        bail!("clang failed to link {}", exe.display());
    }

    std::fs::write(&fp_path, &fingerprint)?;

    Ok(CompileOutput {
        executable: exe,
        llvm_ir: ll_path,
        manifest,
        cache_hit: false,
    })
}

fn push_force_load(link: &mut Command, archive: &Path) {
    if cfg!(target_os = "macos") {
        link.arg(format!("-Wl,-force_load,{}", archive.display()));
    } else {
        link.arg("-Wl,--whole-archive")
            .arg(archive)
            .arg("-Wl,--no-whole-archive");
    }
}

fn fingerprint_sources(sources: &[(String, String)], package: &str, with_ui: bool) -> String {
    let mut h = DefaultHasher::new();
    package.hash(&mut h);
    if with_ui {
        "ui=1".hash(&mut h);
    } else {
        "ui=0".hash(&mut h);
    }
    for (name, text) in sources {
        name.hash(&mut h);
        text.hash(&mut h);
    }
    format!("{:016x}", h.finish())
}

pub fn find_sources(project_dir: &Path) -> Result<Vec<PathBuf>> {
    let src = project_dir.join("src");
    if !src.is_dir() {
        bail!("missing src/ in {}", project_dir.display());
    }
    let mut candidates: Vec<PathBuf> = Vec::new();
    collect_sources(&src, &mut candidates)?;
    if candidates.is_empty() {
        bail!("no .scala / .scuzz sources in {}", src.display());
    }
    // Main.* last so package/enum units parse first (order only affects error msgs;
    // parse_sources merges). Prefer stable order: non-main first, then main.
    candidates.sort_by(|a, b| {
        let am = is_main_file(a);
        let bm = is_main_file(b);
        am.cmp(&bm).then_with(|| a.cmp(b))
    });
    Ok(candidates)
}

fn is_main_file(p: &Path) -> bool {
    p.file_stem()
        .and_then(|s| s.to_str())
        .is_some_and(|s| s.eq_ignore_ascii_case("main"))
}

fn collect_sources(dir: &Path, out: &mut Vec<PathBuf>) -> Result<()> {
    let mut entries: Vec<_> = std::fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|e| e.path());
    for entry in entries {
        let path = entry.path();
        if path.is_dir() {
            collect_sources(&path, out)?;
        } else if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
            if matches!(ext, "scala" | "scuzz") {
                out.push(path);
            }
        }
    }
    Ok(())
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

    // Best-effort embedder builds (optional; skipped if make fails).
    if let Some(parent) = runtime_dir.parent() {
        for name in ["embedder-desktop", "embedder-mobile"] {
            let embedder = parent.join(name);
            if embedder.join("Makefile").is_file() {
                let _ = Command::new("make")
                    .arg("-C")
                    .arg(&embedder)
                    .arg("lib")
                    .env("CC", clang)
                    .status();
            }
        }
    }
    Ok(())
}

/// Resolve repo root from an optional override or by walking parents looking for crates/runtime.
pub fn find_runtime_dir(start: &Path) -> Result<PathBuf> {
    let mut cur = start.to_path_buf();
    loop {
        let candidate = cur.join("crates/runtime");
        if candidate.join("include/scuzz_rt.h").is_file() {
            return Ok(candidate);
        }
        if !cur.pop() {
            bail!("could not find crates/runtime from {}", start.display());
        }
    }
}

/// Poll sources until change or timeout; returns true if a change was observed.
pub fn wait_for_source_change(project_dir: &Path, idle_ms: u64) -> Result<bool> {
    let sources = find_sources(project_dir)?;
    let last = latest_mtime(&sources)?;
    let start = SystemTime::now();
    loop {
        std::thread::sleep(std::time::Duration::from_millis(200));
        let now = latest_mtime(&sources)?;
        if now > last {
            return Ok(true);
        }
        if start.elapsed()?.as_millis() as u64 >= idle_ms {
            return Ok(false);
        }
    }
}

fn latest_mtime(paths: &[PathBuf]) -> Result<SystemTime> {
    let mut latest = SystemTime::UNIX_EPOCH;
    for p in paths {
        let m = std::fs::metadata(p)?.modified()?;
        if m > latest {
            latest = m;
        }
    }
    Ok(latest)
}

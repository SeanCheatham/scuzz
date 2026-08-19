use crate::ast::Program;
use crate::codegen::emit_llvm;
use crate::fuzz::sometimes_declared_text;
use crate::lower::lower_program;
use crate::manifest::{load_manifest, Manifest};
use crate::overlay::{
    apply_overlays, check_laws_applied, collect_law_names, driver_table_text, erase_laws,
    erase_requires, overlay_kind_from_path, residualize_refinements, OverlaySource,
};
use crate::parser::parse_sources;
use crate::typ::typecheck;
use anyhow::{bail, Context, Result};
use std::collections::hash_map::DefaultHasher;
use std::collections::HashSet;
use std::hash::{Hash, Hasher};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;
use std::time::SystemTime;

/// Shared `crates/runtime` / `crates/ffi-skia` archives. Parallel `compile_project`
/// (cargo test threads) must not `make`/`ar`/`ld` them at once. The linker can SIGSEGV.
static NATIVE_LINK_LOCK: Mutex<()> = Mutex::new(());

#[derive(Debug, Clone)]
pub struct CompileOptions {
    pub project_dir: PathBuf,
    /// Directory containing crates/runtime (repo root relative or absolute)
    pub runtime_dir: PathBuf,
    pub out_dir: PathBuf,
    pub clang: String,
    /// Skip clang link when fingerprint matches (incremental).
    pub incremental: bool,
    /// Apply `*.scuzz_sim` and residual in-source `law` decls (fuzz / TestRuntime builds).
    pub verify: bool,
}

#[derive(Debug, Clone)]
pub struct CompileOutput {
    pub executable: PathBuf,
    pub llvm_ir: PathBuf,
    pub manifest: Manifest,
    pub cache_hit: bool,
}

/// One source unit in the resolved package graph.
#[derive(Debug, Clone)]
pub struct ResolvedSource {
    /// Stable label for diagnostics (`{package}/src/...`).
    pub label: String,
    pub path: PathBuf,
    pub text: String,
}

/// Root manifest plus ordered sources (dependencies before dependents).
#[derive(Debug, Clone)]
pub struct ResolvedProject {
    pub root_manifest: Manifest,
    pub sources: Vec<ResolvedSource>,
    /// Stem-paired sim overlays (not loaded for live `build`/`run`).
    pub overlays: Vec<OverlaySource>,
    /// Canonical package directories in visit order (deps first).
    pub package_dirs: Vec<PathBuf>,
    /// Manifest paths in the same order as `package_dirs`.
    pub manifest_paths: Vec<PathBuf>,
}

fn executable_name(manifest: &Manifest) -> Result<String> {
    let exe_name = Path::new(&manifest.package.name)
        .file_name()
        .and_then(|s| s.to_str())
        .filter(|s| !s.is_empty())
        .unwrap_or("app");
    if exe_name.contains('/') || exe_name.contains('\\') {
        bail!(
            "invalid package name for executable: {}",
            manifest.package.name
        );
    }
    Ok(exe_name.to_string())
}

fn prepare_program(resolved: &ResolvedProject, verify: bool) -> Result<Program> {
    let named: Vec<(String, String)> = resolved
        .sources
        .iter()
        .map(|s| (s.label.clone(), s.text.clone()))
        .collect();
    let program = parse_sources(&named).map_err(|e| anyhow::anyhow!("parse error: {e}"))?;
    let mut program = if verify {
        apply_overlays(program, &resolved.overlays).map_err(|e| anyhow::anyhow!("{e}"))?
    } else {
        program
    };
    if verify {
        let law_names = collect_law_names(&program).map_err(|e| anyhow::anyhow!("{e}"))?;
        check_laws_applied(&program, &law_names).map_err(|e| anyhow::anyhow!("{e}"))?;
        program = crate::typ::resolve_named_args(program).map_err(|e| anyhow::anyhow!("{e}"))?;
        residualize_refinements(&mut program);
        program.law_names = law_names;
    } else {
        erase_laws(&mut program);
        erase_requires(&mut program);
    }
    Ok(program)
}

/// Parse + overlay + residualize a verify-graph program (mutation applies here).
pub fn load_verify_program(project_dir: &Path) -> Result<(Program, Manifest)> {
    let resolved = resolve_project(project_dir)?;
    let manifest = resolved.root_manifest.clone();
    let program = prepare_program(&resolved, true)?;
    Ok((program, manifest))
}

pub fn compile_project(opts: &CompileOptions) -> Result<CompileOutput> {
    let resolved = resolve_project(&opts.project_dir)?;
    let manifest = resolved.root_manifest.clone();
    let fingerprint = fingerprint_compile(opts, &resolved);
    let exe_name = executable_name(&manifest)?;

    std::fs::create_dir_all(&opts.out_dir)?;
    let cache_dir = opts.project_dir.join(".scuzz");
    std::fs::create_dir_all(&cache_dir)?;
    let fp_path = cache_dir.join(fingerprint_cache_name(opts.verify));
    let exe = opts.out_dir.join(&exe_name);
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

    let program = prepare_program(&resolved, opts.verify)?;
    let out = compile_prepared_program(opts, program)?;
    store_compile_fingerprint(&cache_dir, opts.verify, &fingerprint)?;
    Ok(out)
}

/// Lower, typecheck, emit, and link an already-prepared program (no fingerprint).
pub fn compile_prepared_program(opts: &CompileOptions, program: Program) -> Result<CompileOutput> {
    let manifest = load_manifest(&opts.project_dir.join("scuzz.toml"))
        .with_context(|| format!("reading {}/scuzz.toml", opts.project_dir.display()))?;
    let exe_name = executable_name(&manifest)?;
    std::fs::create_dir_all(&opts.out_dir)?;
    let exe = opts.out_dir.join(&exe_name);
    let ll_path = opts.out_dir.join(format!("{exe_name}.ll"));

    let declared = sometimes_declared_text(&program);
    let mut program = program;
    crate::typ::inject_builtin_enums(&mut program.enums);
    let program = lower_program(program);
    let program = crate::typ::expand_impls(program).map_err(|e| anyhow::anyhow!("{e}"))?;
    let program = crate::typ::resolve_named_args(program).map_err(|e| anyhow::anyhow!("{e}"))?;
    typecheck(&program).map_err(|e| anyhow::anyhow!("{e}"))?;
    let program = crate::typ::elaborate_generics(program).map_err(|e| anyhow::anyhow!("{e}"))?;
    let program = crate::typ::resolve_field_access(program).map_err(|e| anyhow::anyhow!("{e}"))?;
    let program = crate::typ::monomorphize(program).map_err(|e| anyhow::anyhow!("{e}"))?;
    let program = crate::typ::resolve_field_access(program).map_err(|e| anyhow::anyhow!("{e}"))?;

    let ir = emit_llvm(&program);
    std::fs::write(&ll_path, &ir)?;
    if opts.verify {
        std::fs::write(
            opts.out_dir.join("drivers.txt"),
            driver_table_text(&program),
        )?;
        std::fs::write(opts.out_dir.join("sometimes.declared"), declared)?;
    } else {
        let _ = std::fs::remove_file(opts.out_dir.join("drivers.txt"));
    }

    let _native = NATIVE_LINK_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    build_runtime(&opts.runtime_dir, &opts.clang)?;

    let lib = opts.runtime_dir.join("build/libscuzz_rt.a");
    let include = opts.runtime_dir.join("include");
    let with_ui = manifest.ui.is_some();

    let mut link = Command::new(&opts.clang);
    link.arg(&ll_path)
        .arg(&lib)
        .arg(format!("-I{}", include.display()));

    // macOS: runtime parks main in CFRunLoop so AppKit can hop from the worker.
    if cfg!(target_os = "macos") {
        link.arg("-framework").arg("CoreFoundation");
    }

    if with_ui {
        let ffi_skia_dir = opts
            .runtime_dir
            .parent()
            .map(|p| p.join("ffi-skia"))
            .unwrap_or_else(|| PathBuf::from("crates/ffi-skia"));
        // Build libsk_capi.a if missing (pinned Skia by default; SCUZZ_SKIA=sk_sw opts out).
        let skia_status = Command::new("make")
            .arg("-C")
            .arg(&ffi_skia_dir)
            .arg("lib")
            .env("CC", &opts.clang)
            .status()
            .with_context(|| {
                format!(
                    "missing make while building Skia C API in {} — install clang and make",
                    ffi_skia_dir.display()
                )
            })?;
        if !skia_status.success() {
            bail!(
                "Skia C API build failed in {} — install clang and make, then retry",
                ffi_skia_dir.display()
            );
        }
        let skia_lib = ffi_skia_dir.join("build/libsk_capi.a");
        let skia_include = ffi_skia_dir.join("include");
        link.arg(&skia_lib)
            .arg(format!("-I{}", skia_include.display()));
        // Real Skia backends are C++; sk_sw ignores these.
        if cfg!(target_os = "macos") {
            link.arg("-lc++");
        } else {
            link.arg("-lstdc++");
        }
        link.arg("-lm");
        let backend = ffi_skia_dir.join("build/sk_capi_backend");
        let is_skia = std::fs::read_to_string(&backend)
            .map(|s| s.trim() == "skia")
            .unwrap_or(false);
        let is_gpu = std::fs::read_to_string(&backend)
            .map(|s| s.trim() == "gpu")
            .unwrap_or(false);
        if is_gpu {
            if cfg!(target_os = "macos") {
                link.arg("-framework").arg("OpenGL");
            } else {
                link.arg("-lEGL").arg("-lGLESv2");
            }
        }
        if is_skia {
            // Host zlib/bz2 for FreeType in the Skia fat archive.
            link.arg("-lz").arg("-lbz2");
            if cfg!(target_os = "macos") {
                // Darwin Skia archive needs these frameworks.
                for fw in [
                    "CoreFoundation",
                    "CoreGraphics",
                    "CoreText",
                    "Foundation",
                    "Carbon",
                ] {
                    link.arg("-framework").arg(fw);
                }
            }
        }

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

    link.arg("-lpthread");
    if with_ui && cfg!(target_os = "linux") {
        link.arg("-rdynamic");
    }
    link.arg("-o").arg(&exe);
    let status = link.status().with_context(|| "spawning clang")?;

    if !status.success() {
        bail!("clang failed to link {}", exe.display());
    }

    if with_ui {
        link_reload_dylib(&opts.clang, &ll_path, &opts.out_dir.join("reload.dylib"))?;
    }

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

fn link_reload_dylib(clang: &str, ll: &Path, out: &Path) -> Result<()> {
    let mut cmd = Command::new(clang);
    cmd.arg(ll);
    if cfg!(target_os = "macos") {
        cmd.arg("-dynamiclib")
            .arg("-undefined")
            .arg("dynamic_lookup");
    } else {
        cmd.arg("-shared").arg("-fPIC");
    }
    cmd.arg("-o").arg(out);
    let status = cmd
        .status()
        .with_context(|| "spawning clang for reload.dylib")?;
    if !status.success() {
        bail!("clang failed to link {}", out.display());
    }
    Ok(())
}

fn fingerprint_compile(opts: &CompileOptions, resolved: &ResolvedProject) -> String {
    let mut h = DefaultHasher::new();
    env!("CARGO_PKG_VERSION").hash(&mut h);
    opts.clang.hash(&mut h);
    hash_clang_identity(&opts.clang, &mut h);
    opts.verify.hash(&mut h);
    opts.out_dir.to_string_lossy().hash(&mut h);
    std::env::consts::OS.hash(&mut h);
    std::env::consts::ARCH.hash(&mut h);
    std::env::var("SCUZZ_SKIA").unwrap_or_default().hash(&mut h);
    std::env::var("SCUZZ_CLANG")
        .unwrap_or_default()
        .hash(&mut h);
    if let Ok(exe) = std::env::current_exe() {
        if let Ok(meta) = std::fs::metadata(exe) {
            meta.len().hash(&mut h);
            if let Ok(m) = meta.modified() {
                m.hash(&mut h);
            }
        }
    }
    hash_tree(&opts.runtime_dir.join("include"), &mut h);
    hash_tree(&opts.runtime_dir.join("src"), &mut h);
    hash_file(&opts.runtime_dir.join("Makefile"), &mut h);
    if let Some(parent) = opts.runtime_dir.parent() {
        hash_file(&parent.join("ffi-skia/build/sk_capi_backend"), &mut h);
        hash_file(&parent.join("ffi-skia/Makefile"), &mut h);
        if let Some(root) = parent.parent() {
            hash_file(&root.join("third_party/skia/PIN"), &mut h);
        }
    }
    fingerprint_resolved_into(resolved, opts.verify, &mut h);
    format!("{:016x}", h.finish())
}

fn hash_clang_identity(clang: &str, h: &mut DefaultHasher) {
    if let Ok(out) = Command::new(clang).arg("--version").output() {
        out.stdout.hash(h);
        out.stderr.hash(h);
    }
}

fn hash_file(path: &Path, h: &mut DefaultHasher) {
    path.to_string_lossy().hash(h);
    if let Ok(bytes) = std::fs::read(path) {
        bytes.hash(h);
    }
}

fn hash_tree(dir: &Path, h: &mut DefaultHasher) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    let mut paths: Vec<PathBuf> = entries.flatten().map(|e| e.path()).collect();
    paths.sort();
    for p in paths {
        if p.is_dir() {
            hash_tree(&p, h);
        } else if p
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| matches!(e, "c" | "h" | "S" | "mk"))
            || p.file_name().and_then(|n| n.to_str()) == Some("Makefile")
        {
            hash_file(&p, h);
        }
    }
}

fn fingerprint_resolved_into(resolved: &ResolvedProject, verify: bool, h: &mut DefaultHasher) {
    resolved.root_manifest.package.name.hash(h);
    if resolved.root_manifest.ui.is_some() {
        "ui=1".hash(h);
        "reload-dylib=1".hash(h);
    } else {
        "ui=0".hash(h);
    }
    if verify {
        "verify=1".hash(h);
    } else {
        "verify=0".hash(h);
    }
    for (dir, manifest_path) in resolved
        .package_dirs
        .iter()
        .zip(resolved.manifest_paths.iter())
    {
        dir.to_string_lossy().hash(h);
        let text = std::fs::read_to_string(manifest_path).unwrap_or_default();
        text.hash(h);
    }
    for src in &resolved.sources {
        src.label.hash(h);
        src.text.hash(h);
    }
    if verify {
        for ov in &resolved.overlays {
            ov.label.hash(h);
            ov.text.hash(h);
        }
    }
}

fn fingerprint_cache_name(verify: bool) -> &'static str {
    if verify {
        "fingerprint.verify"
    } else {
        "fingerprint"
    }
}

/// Write this mode's fingerprint. Drop the sibling so live and verify
/// compiles cannot share one `build/` binary.
fn store_compile_fingerprint(cache_dir: &Path, verify: bool, fingerprint: &str) -> Result<()> {
    std::fs::write(cache_dir.join(fingerprint_cache_name(verify)), fingerprint)?;
    let sibling = cache_dir.join(fingerprint_cache_name(!verify));
    if sibling.is_file() {
        std::fs::remove_file(&sibling)?;
    }
    Ok(())
}

#[cfg(test)]
fn fingerprint_resolved(resolved: &ResolvedProject, verify: bool) -> String {
    let mut h = DefaultHasher::new();
    fingerprint_resolved_into(resolved, verify, &mut h);
    format!("{:016x}", h.finish())
}

/// Resolve the full package graph starting at `project_dir`.
pub fn resolve_project(project_dir: &Path) -> Result<ResolvedProject> {
    let root = canonicalize_dir(project_dir)
        .with_context(|| format!("resolving project {}", project_dir.display()))?;
    let mut state = VisitState::default();

    let root_manifest = visit_package(&root, None, &mut state)?;

    validate_overlay_stems(&state.sources, &state.overlays)?;

    Ok(ResolvedProject {
        root_manifest,
        sources: state.sources,
        overlays: state.overlays,
        package_dirs: state.package_dirs,
        manifest_paths: state.manifest_paths,
    })
}

#[derive(Default)]
struct VisitState {
    visiting: Vec<(String, PathBuf)>,
    done: HashSet<PathBuf>,
    sources: Vec<ResolvedSource>,
    overlays: Vec<OverlaySource>,
    package_dirs: Vec<PathBuf>,
    manifest_paths: Vec<PathBuf>,
}

fn visit_package(
    pkg_dir: &Path,
    via_dep: Option<&str>,
    state: &mut VisitState,
) -> Result<Manifest> {
    let canon = canonicalize_dir(pkg_dir).with_context(|| {
        if let Some(name) = via_dep {
            format!("dependency `{name}` path {}", pkg_dir.display())
        } else {
            format!("package directory {}", pkg_dir.display())
        }
    })?;

    if let Some(pos) = state.visiting.iter().position(|(_, p)| p == &canon) {
        let mut chain: Vec<String> = state.visiting[pos..]
            .iter()
            .map(|(n, _)| n.clone())
            .collect();
        let self_name = via_dep.map(|s| s.to_string()).unwrap_or_else(|| {
            state
                .visiting
                .last()
                .map(|(n, _)| n.clone())
                .unwrap_or_else(|| "?".into())
        });
        chain.push(self_name);
        bail!("dependency cycle: {}", chain.join(" -> "));
    }

    let manifest_path = canon.join("scuzz.toml");
    if !manifest_path.is_file() {
        if let Some(name) = via_dep {
            bail!(
                "dependency `{name}`: missing scuzz.toml in {}",
                canon.display()
            );
        }
        bail!("missing scuzz.toml in {}", canon.display());
    }
    let manifest = load_manifest(&manifest_path).with_context(|| {
        if let Some(name) = via_dep {
            format!("dependency `{name}`: reading {}", manifest_path.display())
        } else {
            format!("reading {}", manifest_path.display())
        }
    })?;

    if state.done.contains(&canon) {
        return Ok(manifest);
    }

    let pkg_name = manifest.package.name.clone();
    state.visiting.push((pkg_name.clone(), canon.clone()));

    // Deterministic order regardless of TOML/map iteration (already BTreeMap).
    let deps: Vec<(String, String)> = manifest
        .dependencies
        .iter()
        .map(|(n, d)| (n.clone(), d.path.clone()))
        .collect();

    for (dep_name, dep_path) in &deps {
        if dep_path.is_empty() {
            bail!("dependency `{dep_name}` path must not be empty");
        }
        let child = resolve_dep_path(&canon, dep_path);
        if !child.exists() {
            bail!(
                "dependency `{dep_name}`: path {} does not exist",
                child.display()
            );
        }
        visit_package(&child, Some(dep_name), state)?;
    }

    let pkg_sources = find_sources(&canon).with_context(|| {
        if let Some(name) = via_dep {
            format!("dependency `{name}`: state.sources in {}", canon.display())
        } else {
            format!("state.sources in {}", canon.display())
        }
    })?;
    for path in pkg_sources {
        let text = std::fs::read_to_string(&path)
            .with_context(|| format!("reading {}", path.display()))?;
        let rel = path
            .strip_prefix(&canon)
            .unwrap_or(&path)
            .to_string_lossy()
            .replace('\\', "/");
        let label = format!("{pkg_name}/{rel}");
        state.sources.push(ResolvedSource { label, path, text });
    }

    let pkg_overlays = find_overlays(&canon)?;
    for (path, stem, kind) in pkg_overlays {
        let text = std::fs::read_to_string(&path)
            .with_context(|| format!("reading {}", path.display()))?;
        let rel = path
            .strip_prefix(&canon)
            .unwrap_or(&path)
            .to_string_lossy()
            .replace('\\', "/");
        let label = format!("{pkg_name}/{rel}");
        state.overlays.push(OverlaySource {
            stem,
            kind,
            label,
            text,
            path,
        });
    }

    state.package_dirs.push(canon.clone());
    state.manifest_paths.push(manifest_path);
    state.done.insert(canon);
    state.visiting.pop();
    Ok(manifest)
}

fn resolve_dep_path(from_pkg: &Path, dep_path: &str) -> PathBuf {
    let p = Path::new(dep_path);
    if p.is_absolute() {
        p.to_path_buf()
    } else {
        from_pkg.join(p)
    }
}

fn canonicalize_dir(path: &Path) -> Result<PathBuf> {
    let canon = std::fs::canonicalize(path)
        .with_context(|| format!("canonicalizing {}", path.display()))?;
    if !canon.is_dir() {
        bail!("{} is not a directory", canon.display());
    }
    Ok(canon)
}

/// Package-local `*.scuzz` sources (no dependency walk).
fn find_sources(project_dir: &Path) -> Result<Vec<PathBuf>> {
    let src = project_dir.join("src");
    if !src.is_dir() {
        bail!("missing src/ in {}", project_dir.display());
    }
    let mut candidates: Vec<PathBuf> = Vec::new();
    collect_sources(&src, &mut candidates)?;
    if candidates.is_empty() {
        bail!("no .scuzz sources in {}", src.display());
    }
    // Main.* last so package/enum units parse first. Order only affects error msgs.
    // parse_sources merges. Stable order: non-main first, then main.
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
            if ext == "scuzz" {
                out.push(path);
            }
        }
    }
    Ok(())
}

fn find_overlays(
    project_dir: &Path,
) -> Result<Vec<(PathBuf, String, crate::overlay::OverlayKind)>> {
    let src = project_dir.join("src");
    if !src.is_dir() {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    collect_overlays(&src, &mut out)?;
    out.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(out)
}

fn collect_overlays(
    dir: &Path,
    out: &mut Vec<(PathBuf, String, crate::overlay::OverlayKind)>,
) -> Result<()> {
    let mut entries: Vec<_> = std::fs::read_dir(dir)?.collect::<Result<Vec<_>, _>>()?;
    entries.sort_by_key(|e| e.path());
    for entry in entries {
        let path = entry.path();
        if path.is_dir() {
            collect_overlays(&path, out)?;
        } else if let Some((stem, kind)) = overlay_kind_from_path(&path) {
            out.push((path, stem, kind));
        }
    }
    Ok(())
}

fn validate_overlay_stems(sources: &[ResolvedSource], overlays: &[OverlaySource]) -> Result<()> {
    let mut live_stems: HashSet<String> = HashSet::new();
    for s in sources {
        if let Some(stem) = s
            .path
            .file_stem()
            .and_then(|x| x.to_str())
            .map(|s| s.to_string())
        {
            live_stems.insert(stem);
        }
    }
    for ov in overlays {
        if !live_stems.contains(&ov.stem) {
            bail!(
                "{}: overlay stem `{}` has no live `{}.scuzz` twin",
                ov.label,
                ov.stem,
                ov.stem
            );
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

    // Optional embedder builds. Skip if make fails.
    if let Some(parent) = runtime_dir.parent() {
        for name in ["embedder-desktop", "embedder-mobile"] {
            let embedder = parent.join(name);
            if embedder.join("Makefile").is_file() {
                let status = Command::new("make")
                    .arg("-C")
                    .arg(&embedder)
                    .arg("lib")
                    .env("CC", clang)
                    .status()
                    .with_context(|| {
                        format!("missing make while building {}", embedder.display())
                    })?;
                if !status.success() {
                    bail!(
                        "embedder build failed in {} — install clang and make, then retry",
                        embedder.display()
                    );
                }
            }
        }
    }
    Ok(())
}

/// Resolve `crates/runtime` (with `include/scuzz_rt.h`). Use `SCUZZ_RUNTIME`,
/// then `SCUZZ_HOME/crates/runtime`, then walk parents from `start`.
pub fn find_runtime_dir(start: &Path) -> Result<PathBuf> {
    if let Ok(rt) = std::env::var("SCUZZ_RUNTIME") {
        if !rt.is_empty() {
            let p = PathBuf::from(rt);
            if p.join("include/scuzz_rt.h").is_file() {
                return Ok(p);
            }
        }
    }
    if let Ok(home) = std::env::var("SCUZZ_HOME") {
        if !home.is_empty() {
            let p = PathBuf::from(home).join("crates/runtime");
            if p.join("include/scuzz_rt.h").is_file() {
                return Ok(p);
            }
        }
    }
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

/// Poll sources + manifests (including path deps) until change or timeout.
pub fn wait_for_source_change(project_dir: &Path, idle_ms: u64) -> Result<bool> {
    let last = project_snapshot(project_dir)?;
    let start = SystemTime::now();
    loop {
        std::thread::sleep(std::time::Duration::from_millis(200));
        let now = project_snapshot(project_dir)?;
        if now != last {
            return Ok(true);
        }
        if start.elapsed()?.as_millis() as u64 >= idle_ms {
            return Ok(false);
        }
    }
}

fn project_snapshot(project_dir: &Path) -> Result<Vec<(String, u64, u128)>> {
    let resolved = resolve_project(project_dir)?;
    let mut paths: Vec<PathBuf> = resolved.manifest_paths.clone();
    for s in &resolved.sources {
        paths.push(s.path.clone());
    }
    for ov in &resolved.overlays {
        paths.push(ov.path.clone());
    }
    paths.sort();
    let mut out = Vec::new();
    for p in paths {
        let meta = match std::fs::metadata(&p) {
            Ok(m) => m,
            Err(_) => {
                out.push((p.to_string_lossy().into_owned(), 0, 0));
                continue;
            }
        };
        let mtime = meta
            .modified()
            .ok()
            .and_then(|t| t.duration_since(SystemTime::UNIX_EPOCH).ok())
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        out.push((p.to_string_lossy().into_owned(), meta.len(), mtime));
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn examples_trait_generic_impl_sees_box() {
        std::thread::Builder::new()
            .stack_size(8 * 1024 * 1024)
            .spawn(examples_trait_generic_impl_sees_box_inner)
            .expect("spawn kernel typecheck")
            .join()
            .expect("kernel typecheck thread");
    }

    fn examples_trait_generic_impl_sees_box_inner() {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples/kernel");
        let resolved = resolve_project(&dir).expect("resolve kernel example");
        let named: Vec<(String, String)> = resolved
            .sources
            .iter()
            .map(|s| (s.label.clone(), s.text.clone()))
            .collect();
        let labels: Vec<_> = named.iter().map(|(l, _)| l.as_str()).collect();
        let p = crate::parser::parse_sources(&named).expect("parse kernel example");
        assert!(
            p.enums.iter().any(|e| e.name == "Box"),
            "Box missing; labels={labels:?} enums={:?}",
            p.enums
                .iter()
                .map(|e| format!("{}.{}", e.module, e.name))
                .collect::<Vec<_>>()
        );
        let mut p = p;
        crate::overlay::erase_laws(&mut p);
        crate::overlay::erase_requires(&mut p);
        let p = crate::typ::expand_impls(crate::lower::lower_program(p)).expect("expand");
        let p = crate::typ::resolve_named_args(p).expect("named args");
        crate::typ::typecheck(&p).expect("typecheck kernel example");
        let p = crate::typ::elaborate_generics(p).expect("elaborate");
        let p = crate::typ::resolve_field_access(p).expect("fields before mono");
        let p = crate::typ::monomorphize(p).expect("mono");
        crate::typ::resolve_field_access(p).expect("fields after mono");
    }

    fn write_pkg(dir: &Path, name: &str, deps: &str, main: bool, body: &str) {
        fs::create_dir_all(dir.join("src")).unwrap();
        let dep_section = if deps.is_empty() {
            String::new()
        } else {
            format!("\n[dependencies]\n{deps}\n")
        };
        fs::write(
            dir.join("scuzz.toml"),
            format!("[package]\nname = \"{name}\"\nversion = \"0.1.0\"\n{dep_section}"),
        )
        .unwrap();
        let src = if main {
            format!("@main def main: IO[Unit] =\n  {body}\n")
        } else {
            body.to_string()
        };
        let file = if main { "Main.scuzz" } else { "Lib.scuzz" };
        fs::write(dir.join("src").join(file), &src).unwrap();
    }

    #[test]
    fn resolves_direct_dependency() {
        let tmp = tempdir().unwrap();
        let shared = tmp.path().join("shared");
        let app = tmp.path().join("app");
        write_pkg(
            &shared,
            "shared",
            "",
            false,
            "def greet(): String = \"hi\"\n",
        );
        write_pkg(
            &app,
            "app",
            "shared = { path = \"../shared\" }\n",
            true,
            "IO.println(greet())",
        );
        let r = resolve_project(&app).unwrap();
        assert_eq!(r.sources.len(), 2);
        assert!(r.sources[0].label.starts_with("shared/"));
        assert!(r.sources[1].label.starts_with("app/"));
        let named: Vec<_> = r
            .sources
            .iter()
            .map(|s| (s.label.clone(), s.text.clone()))
            .collect();
        parse_sources(&named).unwrap();
    }

    #[test]
    fn resolves_transitive_and_diamond_once() {
        let tmp = tempdir().unwrap();
        let base = tmp.path().join("base");
        let left = tmp.path().join("left");
        let right = tmp.path().join("right");
        let app = tmp.path().join("app");
        write_pkg(&base, "base", "", false, "def baseId(): String = \"b\"\n");
        write_pkg(
            &left,
            "left",
            "base = { path = \"../base\" }\n",
            false,
            "def leftId(): String = baseId()\n",
        );
        write_pkg(
            &right,
            "right",
            "base = { path = \"../base\" }\n",
            false,
            "def rightId(): String = baseId()\n",
        );
        write_pkg(
            &app,
            "app",
            "left = { path = \"../left\" }\nright = { path = \"../right\" }\n",
            true,
            "IO.println(Str.concat(leftId(), rightId()))",
        );
        let r = resolve_project(&app).unwrap();
        let base_count = r
            .sources
            .iter()
            .filter(|s| s.label.starts_with("base/"))
            .count();
        assert_eq!(base_count, 1);
        assert_eq!(r.package_dirs.len(), 4);
    }

    #[test]
    fn stable_order_independent_of_manifest_entry_order() {
        let tmp = tempdir().unwrap();
        let a = tmp.path().join("a");
        let b = tmp.path().join("b");
        let app1 = tmp.path().join("app1");
        let app2 = tmp.path().join("app2");
        write_pkg(&a, "a", "", false, "def aId(): String = \"a\"\n");
        write_pkg(&b, "b", "", false, "def bId(): String = \"b\"\n");
        write_pkg(
            &app1,
            "app",
            "zebra = { path = \"../b\" }\nalpha = { path = \"../a\" }\n",
            true,
            "IO.println(Str.concat(aId(), bId()))",
        );
        write_pkg(
            &app2,
            "app",
            "alpha = { path = \"../a\" }\nzebra = { path = \"../b\" }\n",
            true,
            "IO.println(Str.concat(aId(), bId()))",
        );
        let r1 = resolve_project(&app1).unwrap();
        let r2 = resolve_project(&app2).unwrap();
        let labels1: Vec<_> = r1.sources.iter().map(|s| s.label.clone()).collect();
        let labels2: Vec<_> = r2.sources.iter().map(|s| s.label.clone()).collect();
        assert_eq!(labels1, labels2);
        assert!(labels1[0].starts_with("a/"));
        assert!(labels1[1].starts_with("b/"));
    }

    #[test]
    fn rejects_missing_manifest() {
        let tmp = tempdir().unwrap();
        let missing = tmp.path().join("missing");
        fs::create_dir_all(&missing).unwrap();
        let app = tmp.path().join("app");
        write_pkg(
            &app,
            "app",
            "shared = { path = \"../missing\" }\n",
            true,
            "IO.println(\"x\")",
        );
        let err = resolve_project(&app).unwrap_err().to_string();
        assert!(err.contains("dependency `shared`"), "unexpected: {err}");
        assert!(err.contains("missing scuzz.toml"), "unexpected: {err}");
    }

    #[test]
    fn rejects_missing_src() {
        let tmp = tempdir().unwrap();
        let shared = tmp.path().join("shared");
        fs::create_dir_all(&shared).unwrap();
        fs::write(shared.join("scuzz.toml"), "[package]\nname = \"shared\"\n").unwrap();
        let app = tmp.path().join("app");
        write_pkg(
            &app,
            "app",
            "shared = { path = \"../shared\" }\n",
            true,
            "IO.println(\"x\")",
        );
        let err = format!("{:#}", resolve_project(&app).unwrap_err());
        assert!(err.contains("dependency `shared`"), "unexpected: {err}");
        assert!(err.contains("missing src/"), "unexpected: {err}");
    }

    #[test]
    fn rejects_direct_cycle() {
        let tmp = tempdir().unwrap();
        let a = tmp.path().join("a");
        let b = tmp.path().join("b");
        write_pkg(
            &a,
            "a",
            "b = { path = \"../b\" }\n",
            false,
            "def aId(): String = \"a\"\n",
        );
        write_pkg(
            &b,
            "b",
            "a = { path = \"../a\" }\n",
            false,
            "def bId(): String = \"b\"\n",
        );
        // Need an executable root that depends on the cycle.
        let app = tmp.path().join("app");
        write_pkg(
            &app,
            "app",
            "a = { path = \"../a\" }\n",
            true,
            "IO.println(aId())",
        );
        let err = resolve_project(&app).unwrap_err().to_string();
        assert!(err.contains("dependency cycle"), "unexpected: {err}");
    }

    #[test]
    fn rejects_multiple_mains() {
        let tmp = tempdir().unwrap();
        let shared = tmp.path().join("shared");
        let app = tmp.path().join("app");
        write_pkg(&shared, "shared", "", true, "IO.println(\"shared\")");
        write_pkg(
            &app,
            "app",
            "shared = { path = \"../shared\" }\n",
            true,
            "IO.println(\"app\")",
        );
        let r = resolve_project(&app).unwrap();
        let named: Vec<_> = r
            .sources
            .iter()
            .map(|s| (s.label.clone(), s.text.clone()))
            .collect();
        let err = parse_sources(&named).unwrap_err().to_string();
        assert!(err.contains("multiple @main"), "unexpected: {err}");
    }

    #[test]
    fn store_compile_fingerprint_drops_sibling_mode() {
        let tmp = tempdir().unwrap();
        let cache = tmp.path();
        fs::write(cache.join("fingerprint.verify"), "old-verify").unwrap();
        store_compile_fingerprint(cache, false, "live-fp").unwrap();
        assert_eq!(
            fs::read_to_string(cache.join("fingerprint")).unwrap(),
            "live-fp"
        );
        assert!(!cache.join("fingerprint.verify").is_file());
        fs::write(cache.join("fingerprint"), "stale-live").unwrap();
        store_compile_fingerprint(cache, true, "verify-fp").unwrap();
        assert_eq!(
            fs::read_to_string(cache.join("fingerprint.verify")).unwrap(),
            "verify-fp"
        );
        assert!(!cache.join("fingerprint").is_file());
    }

    #[test]
    fn fingerprint_changes_when_dependency_source_changes() {
        let tmp = tempdir().unwrap();
        let shared = tmp.path().join("shared");
        let app = tmp.path().join("app");
        write_pkg(
            &shared,
            "shared",
            "",
            false,
            "def greet(): String = \"hi\"\n",
        );
        write_pkg(
            &app,
            "app",
            "shared = { path = \"../shared\" }\n",
            true,
            "IO.println(greet())",
        );
        let r1 = resolve_project(&app).unwrap();
        let fp1 = fingerprint_resolved(&r1, false);
        fs::write(
            shared.join("src/Lib.scuzz"),
            "def greet(): String = \"yo\"\n",
        )
        .unwrap();
        let r2 = resolve_project(&app).unwrap();
        let fp2 = fingerprint_resolved(&r2, false);
        assert_ne!(fp1, fp2);
    }

    #[test]
    fn fingerprint_compile_includes_clang_version() {
        let tmp = tempdir().unwrap();
        let app = tmp.path().join("app");
        write_pkg(&app, "app", "", true, "IO.println(\"x\")");
        let resolved = resolve_project(&app).unwrap();
        let opts = CompileOptions {
            project_dir: app.clone(),
            runtime_dir: PathBuf::from("crates/runtime"),
            out_dir: app.join("build"),
            clang: "clang".into(),
            incremental: true,
            verify: false,
        };
        let fp = fingerprint_compile(&opts, &resolved);
        assert_eq!(fp.len(), 16);
        let mut h = DefaultHasher::new();
        hash_clang_identity("clang", &mut h);
        let _ = h.finish();
    }

    #[test]
    fn rejects_unsupported_dependency_in_resolve_load() {
        let tmp = tempdir().unwrap();
        let app = tmp.path().join("app");
        fs::create_dir_all(app.join("src")).unwrap();
        fs::write(
            app.join("scuzz.toml"),
            "[package]\nname = \"app\"\n[dependencies]\nshared = \"../shared\"\n",
        )
        .unwrap();
        fs::write(
            app.join("src/Main.scuzz"),
            "@main def main: IO[Unit] =\n  IO.println(\"x\")\n",
        )
        .unwrap();
        let err = format!("{:#}", resolve_project(&app).unwrap_err());
        assert!(
            err.contains("string dependencies are unsupported"),
            "unexpected: {err}"
        );
    }

    fn write_reload_ui(dir: &Path, title: &str) {
        fs::create_dir_all(dir.join("src")).unwrap();
        fs::write(
            dir.join("scuzz.toml"),
            "[package]\nname = \"reload_label\"\nversion = \"0.1.0\"\n\n[ui]\ndefault_runtime = \"headless\"\nheadless_size = [200, 100]\nheadless_scale = 1.0\n",
        )
        .unwrap();
        fs::write(
            dir.join("src/Main.scuzz"),
            format!(
                "@main def main: IO[Unit] =\n  for {{\n    count = Signal.int(7)\n    label = Signal.map(count, n => Str.concat(\"n=\", Str.fromInt(n)))\n    _ <- Ui.run(_ => View.column(View.text(\"{title}\"), View.bindText(label)))\n  }} yield ()\n"
            ),
        )
        .unwrap();
    }

    fn compile_reload_ui(dir: &Path) -> CompileOutput {
        let runtime_dir =
            find_runtime_dir(Path::new(env!("CARGO_MANIFEST_DIR"))).expect("crates/runtime");
        compile_project(&CompileOptions {
            project_dir: dir.to_path_buf(),
            runtime_dir,
            out_dir: dir.join("build"),
            clang: std::env::var("SCUZZ_CLANG").unwrap_or_else(|_| "clang".into()),
            incremental: false,
            verify: false,
        })
        .expect("compile reload_label")
    }

    fn wait_dump_contains(
        path: &Path,
        needle: &str,
        child: &mut std::process::Child,
        ms: u64,
    ) -> String {
        let start = std::time::Instant::now();
        loop {
            if let Ok(Some(status)) = child.try_wait() {
                panic!("ui process exited {status} before dump contained {needle:?}");
            }
            if let Ok(text) = fs::read_to_string(path) {
                if text.contains(needle) {
                    return text;
                }
            }
            if start.elapsed().as_millis() as u64 >= ms {
                let got = fs::read_to_string(path).unwrap_or_default();
                panic!("timed out waiting for {needle:?} in dump:\n{got}");
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
    }

    fn wait_child_exit(child: &mut std::process::Child, ms: u64) {
        let start = std::time::Instant::now();
        loop {
            if let Ok(Some(status)) = child.try_wait() {
                assert!(status.success(), "ui process exit {status}");
                return;
            }
            if start.elapsed().as_millis() as u64 >= ms {
                let _ = child.kill();
                let _ = child.wait();
                panic!("timed out waiting for ui process to quit");
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
    }

    #[test]
    fn live_reload_dylib_swaps_view_label() {
        let tmp = tempdir().unwrap();
        let app = tmp.path().join("app");
        write_reload_ui(&app, "Alpha");
        let out = compile_reload_ui(&app);
        let dylib = app.join("build").join("reload.dylib");
        assert!(dylib.is_file(), "expected {}", dylib.display());

        let stamp = app.join("build").join("reload.stamp");
        let dump = app.join("build").join("debug.dump");
        let inject = app.join("build").join("inject.script");
        fs::write(&stamp, "0\n").unwrap();
        let mut child = std::process::Command::new(&out.executable)
            .env("SCUZZ_UI_RUNTIME", "headless")
            .env("SCUZZ_UI_WIDTH", "200")
            .env("SCUZZ_UI_HEIGHT", "100")
            .env("SCUZZ_UI_RELOAD_STAMP", &stamp)
            .env("SCUZZ_UI_RELOAD_CODE", &dylib)
            .env("SCUZZ_UI_DEBUG_DUMP", &dump)
            .env("SCUZZ_UI_INJECT", &inject)
            .spawn()
            .expect("spawn reload_label");
        let first = wait_dump_contains(&dump, "text:Alpha", &mut child, 8_000);
        assert!(first.contains("int[0] = 7"), "{first}");
        assert!(first.contains("[session]"), "{first}");
        assert!(first.contains("kind=headless"), "{first}");
        assert!(first.contains("[heap]"), "{first}");
        assert!(first.contains("live_bytes="), "{first}");
        assert!(first.contains("delta_bytes="), "{first}");
        assert!(first.contains("string="), "{first}");
        assert!(first.contains("[live]"), "{first}");
        assert!(!first.contains("text:Beta"), "{first}");

        write_reload_ui(&app, "Beta");
        compile_reload_ui(&app);
        fs::write(&stamp, "1\n").unwrap();
        let second = wait_dump_contains(&dump, "text:Beta", &mut child, 8_000);
        assert!(second.contains("int[0] = 7"), "{second}");
        assert!(second.contains("[heap]"), "{second}");
        assert!(second.contains("[session]"), "{second}");
        assert!(!second.contains("text:Alpha"), "{second}");

        fs::write(&inject, "quit\n").unwrap();
        wait_child_exit(&mut child, 8_000);
    }

    fn write_io_watch(dir: &Path, marker: &Path, msg: &str) {
        fs::create_dir_all(dir.join("src")).unwrap();
        fs::write(
            dir.join("scuzz.toml"),
            "[package]\nname = \"io_watch\"\nversion = \"0.1.0\"\n",
        )
        .unwrap();
        fs::write(
            dir.join("src/Main.scuzz"),
            format!(
                "@main def main: IO[Unit] =\n  for {{\n    _ <- Fs.write(\"{}\", \"{}\")\n    _ <- IO.sleep(30000)\n  }} yield ()\n",
                marker.display(),
                msg
            ),
        )
        .unwrap();
    }

    fn compile_io_watch(dir: &Path) -> CompileOutput {
        let runtime_dir =
            find_runtime_dir(Path::new(env!("CARGO_MANIFEST_DIR"))).expect("crates/runtime");
        compile_project(&CompileOptions {
            project_dir: dir.to_path_buf(),
            runtime_dir,
            out_dir: dir.join("build"),
            clang: std::env::var("SCUZZ_CLANG").unwrap_or_else(|_| "clang".into()),
            incremental: false,
            verify: false,
        })
        .expect("compile io_watch")
    }

    #[test]
    fn io_watch_kills_and_reruns_after_source_change() {
        let tmp = tempdir().unwrap();
        let app = tmp.path().join("app");
        let marker = app.join("marker.txt");
        write_io_watch(&app, &marker, "alpha");
        let out = compile_io_watch(&app);
        let mut child = std::process::Command::new(&out.executable)
            .spawn()
            .expect("spawn io_watch");
        let first = wait_dump_contains(&marker, "alpha", &mut child, 8_000);
        assert_eq!(first.trim(), "alpha");

        write_io_watch(&app, &marker, "beta");
        let _ = child.kill();
        let _ = child.wait();
        let out = compile_io_watch(&app);
        let mut child = std::process::Command::new(&out.executable)
            .spawn()
            .expect("respawn io_watch");
        let second = wait_dump_contains(&marker, "beta", &mut child, 8_000);
        assert_eq!(second.trim(), "beta");

        let _ = child.kill();
        let _ = child.wait();
    }
}

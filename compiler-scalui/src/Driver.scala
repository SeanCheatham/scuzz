package scalui.compiler

// Stage-1 compile / link / run driver (kernel dialect + blessed Fs/Sys IO).

def emptyProg(): List =
  List.cons(
    "Prog",
    List.cons("", List.cons(List.empty, List.cons(unitExpr(), List.empty)))
  )

def chooseMain(a: List, b: List): List =
  if (streq(exprTag(b), "Unit") == 1) a else b

def mergeProg(acc: List, p: List): List =
  List.cons(
    "Prog",
    List.cons(
      nodeStr(p, 0),
      List.cons(
        listConcat(progDefs(acc), progDefs(p)),
        List.cons(chooseMain(progMain(acc), progMain(p)), List.empty)
      )
    )
  )

def parseSource(src: String): List = parseProgram(lex(src))

def readTomlName(toml: String): String =
  readTomlNameAt(toml, Str.indexOf(toml, "name = \""))

def readTomlNameAt(toml: String, idx: Int): String =
  if (idx < 0) "app"
  else readUntilQuote(toml, idx + 8, "")

def readUntilQuote(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc
  else if (Str.charAt(s, i) == 34) acc
  else readUntilQuote(s, i + 1, Str.concat(acc, Str.slice(s, i, i + 1)))

def isScalaName(name: String): Int =
  if (endsWith(name, ".scala") == 1) 1 else if (endsWith(name, ".scalui") == 1) 1 else 0

def isMainName(name: String): Int =
  if (streq(name, "Main.scala") == 1) 1
  else if (streq(name, "main.scala") == 1) 1
  else if (streq(name, "Main.scalui") == 1) 1
  else 0

def partitionSources(names: List, others: List, mains: List): List =
  if (List.isEmpty(names) == 1) listConcat(List.reverse(others), List.reverse(mains))
  else partitionSourcesOne(List.head(names), List.tail(names), others, mains)

def partitionSourcesOne(name: String, rest: List, others: List, mains: List): List =
  if (isScalaName(name) == 0) partitionSources(rest, others, mains)
  else if (isMainName(name) == 1) partitionSources(rest, others, List.cons(name, mains))
  else partitionSources(rest, List.cons(name, others), mains)

def resolveRuntimeEnv(env: String, projectDir: String): IO[String] =
  if (Str.len(env) > 0) IO.pure(env)
  else findRuntimeFrom(projectDir, 8)

def findRuntimeFrom(dir: String, fuel: Int): IO[String] =
  if (fuel <= 0) IO.pure(pathJoin(dir, "crates/runtime"))
  else
    Fs.read(pathJoin(pathJoin(dir, "crates/runtime"), "include/scalui_rt.h")).flatMap(_ =>
      IO.pure(pathJoin(dir, "crates/runtime"))
    ).handleErrorWith(_ => findRuntimeFrom(parentDir(dir), fuel - 1))

def clangOrDefault(env: String): String =
  if (Str.len(env) > 0) env else "clang"

def readSources(srcDir: String, names: List, acc: List): IO[List] =
  if (List.isEmpty(names) == 1) IO.pure(List.reverse(acc))
  else
    Fs.read(pathJoin(srcDir, List.head(names))).flatMap(text =>
      readSources(srcDir, List.tail(names), List.cons(text, acc))
    )

def mergeSources(texts: List, acc: List): List =
  if (List.isEmpty(texts) == 1) acc
  else mergeSources(List.tail(texts), mergeProg(acc, parseSource(List.head(texts))))

def buildRuntime(runtimeDir: String, clang: String): IO[Unit] =
  Sys.exec(str4("make -C ", runtimeDir, " lib CC=", clang)).flatMap(_ =>
    Sys.exec(
      str4("make -C ", pathJoin(parentDir(runtimeDir), "embedder-desktop"), " lib CC=", clang)
    ).flatMap(_ => IO.pure(()))
  )

/* Shell fragment: force-load desktop embedder + platform libs when archive exists. */
def embedderLinkFlags(embedder: String): String =
  str4(
    "$(test -f ",
    embedder,
    " && case $(uname -s) in Darwin) echo -Wl,-force_load,",
    str4(
      embedder,
      " -framework Cocoa -lobjc;; Linux) echo -Wl,--whole-archive ",
      embedder,
      " -Wl,--no-whole-archive -lX11;; esac)"
    )
  )

def linkCmd(clang: String, ll: String, lib: String, skia: String, inc: String, skInc: String, embedder: String, exe: String): String =
  str4(
    clang,
    " ",
    ll,
    str4(
      " ",
      lib,
      " ",
      str4(
        skia,
        " -I",
        inc,
        str4(
          " -I",
          skInc,
          " ",
          str4(embedderLinkFlags(embedder), " -o ", exe, "")
        )
      )
    )
  )

def runIfNeeded(exe: String, doRun: Int): IO[Unit] =
  if (doRun == 1) Sys.exec(exe).flatMap(_ => IO.pure(()))
  else IO.pure(())

def compileProject(projectDir: String, outDir: String, doRun: Int): IO[Unit] =
  Sys.getenv("SCALUI_RUNTIME").flatMap(rtEnv =>
    resolveRuntimeEnv(rtEnv, projectDir).flatMap(runtimeDir =>
      Sys.getenv("SCALUI_CLANG").flatMap(clangEnv =>
        compileProjectWith(projectDir, outDir, doRun, runtimeDir, clangOrDefault(clangEnv))
      )
    )
  )

def compileProjectWith(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String): IO[Unit] =
  Fs.read(pathJoin(projectDir, "scalui.toml")).flatMap(toml =>
    compileAfterToml(projectDir, outDir, doRun, runtimeDir, clang, readTomlName(toml))
  )

def compileAfterToml(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String, name: String): IO[Unit] =
  val srcDir = pathJoin(projectDir, "src")
  Fs.list(srcDir).flatMap(names =>
    readSources(srcDir, partitionSources(names, List.empty, List.empty), List.empty).flatMap(texts =>
      compileAfterSources(projectDir, outDir, doRun, runtimeDir, clang, name, texts)
    )
  )

def compileAfterSources(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String, name: String, texts: List): IO[Unit] =
  val prog = mergeSources(texts, emptyProg())
  val ir = emitProgram(prog)
  val ll = pathJoin(outDir, Str.concat(name, ".ll"))
  val exe = pathJoin(outDir, name)
  val lib = pathJoin(runtimeDir, "build/libscalui_rt.a")
  val ffi = pathJoin(parentDir(runtimeDir), "ffi-skia")
  val skia = pathJoin(ffi, "build/libsk_capi.a")
  val inc = pathJoin(runtimeDir, "include")
  val skInc = pathJoin(ffi, "include")
  val embedder = pathJoin(pathJoin(parentDir(runtimeDir), "embedder-desktop"), "build/libscalui_embedder.a")
  Fs.mkdirs(outDir).flatMap(_ =>
    Fs.write(ll, ir).flatMap(_ =>
      buildRuntime(runtimeDir, clang).flatMap(_ =>
        Sys.exec(linkCmd(clang, ll, lib, skia, inc, skInc, embedder, exe)).flatMap(_ =>
          runIfNeeded(exe, doRun)
        )
      )
    )
  )

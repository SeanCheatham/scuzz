package scuzz.compiler

def emptyProg(): List =
  List.cons("Prog", List.cons("", List.cons(List.empty(), List.cons(List.empty(), List.cons(unitExpr(), List.empty())))))

def chooseMain(a: List, b: List): List =
  if (streq(exprTag(b), "Unit") == 1) a else b

def mergeProg(acc: List, p: List): List =
  List.cons("Prog", List.cons(nodeStr(p, 0), List.cons(listConcat(progEnums(acc), progEnums(p)), List.cons(listConcat(progDefs(acc), progDefs(p)), List.cons(chooseMain(progMain(acc), progMain(p)), List.empty())))))

def parseSource(src: String): List =
  parseProgram(lex(src))

def readUntilQuote(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else if (Str.charAt(s, i) == 34) acc else readUntilQuote(s, i + 1, Str.concat(acc, Str.slice(s, i, i + 1)))

def isScalaName(name: String): Int =
  if (endsWith(name, ".scala") == 1) 1 else if (endsWith(name, ".scuzz") == 1) 1 else 0

def isMainName(name: String): Int =
  if (streq(name, "Main.scala") == 1) 1 else if (streq(name, "main.scala") == 1) 1 else if (streq(name, "Main.scuzz") == 1) 1 else 0

def partitionSources(names: List, others: List, mains: List): List =
  if (List.isEmpty(names) == 1) listConcat(List.reverse(others), List.reverse(mains)) else partitionSourcesOne(List.head(names), List.tail(names), others, mains)

def partitionSourcesOne(name: String, rest: List, others: List, mains: List): List =
  if (isScalaName(name) == 0) partitionSources(rest, others, mains) else if (isMainName(name) == 1) partitionSources(rest, others, List.cons(name, mains)) else partitionSources(rest, List.cons(name, others), mains)

def resolveRuntimeEnv(env: String, projectDir: String): IO[String] =
  if (Str.len(env) > 0) IO.pure(env) else Sys.getenv("SCUZZ_HOME").flatMap(home =>
  if (Str.len(home) > 0) IO.pure(pathJoin(home, "crates/runtime")) else tryFindRuntime(projectDir).handleErrorWith(_ =>
  Sys.getenv("PWD").flatMap(pwd => if (Str.len(pwd) > 0) tryFindRuntime(pwd) else IO.fail("could not find crates/runtime (set SCUZZ_HOME or SCUZZ_RUNTIME)"))
)
)

def tryFindRuntime(start: String): IO[String] =
  findRuntimeFrom(start, 8).flatMap(dir =>
    Fs.read(pathJoin(dir, "include/scuzz_rt.h")).flatMap(_ => IO.pure(dir))
  )

def findRuntimeFrom(dir: String, fuel: Int): IO[String] =
  if (fuel <= 0) IO.fail("could not find crates/runtime") else Fs.read(pathJoin(pathJoin(dir, "crates/runtime"), "include/scuzz_rt.h")).flatMap(_ => IO.pure(pathJoin(dir, "crates/runtime"))).handleErrorWith(_ =>
  findRuntimeFrom(parentDir(dir), fuel - 1)
)

def clangOrDefault(env: String): String =
  if (Str.len(env) > 0) env else "clang"

def execOk(cmd: String): IO[Unit] =
  Sys.exec(cmd).flatMap(code => if (code == 0) IO.pure(()) else IO.fail(str3("exec failed (", Str.fromInt(code), str3("): ", cmd, ""))))

def readSources(srcDir: String, names: List, acc: List): IO[List] =
  if (List.isEmpty(names) == 1) IO.pure(List.reverse(acc)) else Fs.read(pathJoin(srcDir, List.head(names))).flatMap(text => readSources(srcDir, List.tail(names), List.cons(text, acc)))

def mergeSources(texts: List, acc: List): List =
  if (List.isEmpty(texts) == 1) acc else mergeSources(List.tail(texts), mergeProg(acc, parseSource(List.head(texts))))

def buildRuntime(runtimeDir: String, clang: String): IO[Unit] =
  execOk(str4("make -C ", runtimeDir, " lib CC=", clang)).flatMap(_ =>
    execOk(str4("make -C ", pathJoin(parentDir(runtimeDir), "embedder-desktop"), " lib CC=", clang)).flatMap(_ => execOk(str4("make -C ", pathJoin(parentDir(runtimeDir), "embedder-mobile"), " lib CC=", clang)))
  )

def embedderLinkFlags(embedder: String): String =
  str5("$(test -f ", embedder, " && if [ \"`uname -s`\" = Darwin ]; then echo -Wl,-force_load,", embedder, str3(" -framework Cocoa -lobjc; elif [ \"`uname -s`\" = Linux ]; then echo -Wl,--whole-archive ", embedder, " -Wl,--no-whole-archive -lX11; fi)"))

def mobileLinkFlags(mobile: String): String =
  str5("$(test -f ", mobile, " && if [ \"`uname -s`\" = Darwin ]; then echo -Wl,-force_load,", mobile, str3("; elif [ \"`uname -s`\" = Linux ]; then echo -Wl,--whole-archive ", mobile, " -Wl,--no-whole-archive; fi)"))

def linkCmd(clang: String, ll: String, lib: String, skia: String, inc: String, skInc: String, embedder: String, mobile: String, exe: String): String =
  str4(clang, " ", ll, str4(" ", lib, " ", str4(skia, " -I", inc, str4(" -I", skInc, " -lpthread ", str5(embedderLinkFlags(embedder), " ", mobileLinkFlags(mobile), " -o ", exe)))))

def linkCmdIo(clang: String, ll: String, lib: String, inc: String, exe: String): String =
  str5(clang, " ", ll, " ", str4(lib, " -I", inc, str3(" -lpthread -o ", exe, "")))

def runIfNeeded(exe: String, doRun: Int): IO[Unit] =
  if (doRun == 1) execOk(exe) else IO.pure(())

def compileProject(projectDir: String, outDir: String, doRun: Int): IO[Unit] =
  Sys.getenv("SCUZZ_RUNTIME").flatMap(rtEnv =>
    resolveRuntimeEnv(rtEnv, projectDir).flatMap(runtimeDir =>
      Sys.getenv("SCUZZ_CLANG").flatMap(clangEnv => compileProjectWith(projectDir, outDir, doRun, runtimeDir, clangOrDefault(clangEnv)))
    )
  )

def compileProjectWith(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String): IO[Unit] =
  Fs.read(pathJoin(projectDir, "scuzz.toml")).flatMap(toml => compileAfterToml(projectDir, outDir, doRun, runtimeDir, clang, toml, readTomlName(toml)))

def compileAfterToml(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String, toml: String, name: String): IO[Unit] =
  for {
    srcDir = pathJoin(projectDir, "src")
  } yield Fs.list(srcDir).flatMap(names =>
  readSources(srcDir, partitionSources(names, List.empty(), List.empty()), List.empty()).flatMap(texts => compileAfterSources(projectDir, outDir, doRun, runtimeDir, clang, name, hasUiSection(toml), texts))
)

def checkProject(projectDir: String): IO[String] =
  for {
    srcDir = pathJoin(projectDir, "src")
  } yield Fs.list(srcDir).flatMap(names =>
  readSources(srcDir, partitionSources(names, List.empty(), List.empty()), List.empty()).flatMap(texts =>
    for {
      prog = lowerProg(mergeSources(texts, emptyProg()))
      ty = typecheckProg(prog)
    } yield IO.pure(ty)
  )
)

def linkProject(clang: String, ll: String, lib: String, ffi: String, inc: String, embedder: String, mobile: String, exe: String, withUi: Int): IO[Unit] =
  if (withUi == 0) execOk(linkCmdIo(clang, ll, lib, inc, exe)) else execOk(linkCmd(clang, ll, lib, pathJoin(ffi, "build/libsk_capi.a"), inc, pathJoin(ffi, "include"), embedder, mobile, exe))

def compileAfterSources(projectDir: String, outDir: String, doRun: Int, runtimeDir: String, clang: String, name: String, withUi: Int, texts: List): IO[Unit] =
  for {
    prog = lowerProg(mergeSources(texts, emptyProg()))
    ty = typecheckProg(prog)
    ir = emitProgram(prog)
    ll = pathJoin(outDir, Str.concat(name, ".ll"))
    exe = pathJoin(outDir, name)
    lib = pathJoin(runtimeDir, "build/libscuzz_rt.a")
    ffi = pathJoin(parentDir(runtimeDir), "ffi-skia")
    inc = pathJoin(runtimeDir, "include")
    embedder = pathJoin(pathJoin(parentDir(runtimeDir), "embedder-desktop"), "build/libscuzz_embedder.a")
    mobile = pathJoin(pathJoin(parentDir(runtimeDir), "embedder-mobile"), "build/libscuzz_mobile.a")
  } yield if (tyIsOk(ty) == 0) IO.fail(tyMsg(ty)) else Fs.mkdirs(outDir).flatMap(_ =>
  Fs.write(ll, ir).flatMap(_ =>
    buildRuntime(runtimeDir, clang).flatMap(_ =>
      linkProject(clang, ll, lib, ffi, inc, embedder, mobile, exe, withUi).flatMap(_ => runIfNeeded(exe, doRun))
    )
  )
)


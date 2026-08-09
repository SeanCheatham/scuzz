package scalui.compiler

// Stage-1 product CLI helpers (keep small for Stage-1 self-compile stack).

def uiMainTemplate(): String =
  Str.concat(
    str5(
      "@main def main: IO[Unit] =\n",
      "  val count = Signal.int(0)\n",
      "  val root = View.column()\n",
      "  val a = View.addChild(root, View.text(\"Counter\"))\n",
      "  val b = View.addChild(root, View.textSignal(count, \"count = \"))\n"
    ),
    str5(
      "  val row = View.row()\n",
      "  val c = View.addChild(row, View.buttonInc(\"+1\", count))\n",
      "  val d = View.addChild(root, row)\n",
      "  Ui.run(root)\n",
      ""
    )
  )

def helloMainTemplate(): String =
  str3(
    "@main def main: IO[Unit] =\n",
    "  IO.println(\"Hello, ScalUI!\").flatMap(_ => IO.println(\"Phase 0 online.\"))\n",
    ""
  )

def uiToml(name: String): String =
  Str.concat(
    str5(
      "[package]\nname = \"",
      name,
      "\"\nversion = \"0.1.0\"\n\n[targets.native]\nkind = \"executable\"\nmain = \"Main\"\n\n[ui]\n",
      "default_runtime = \"headless\"\nheadless_size = [200, 120]\nheadless_scale = 1.0\n",
      str3("bundle_id = \"dev.scalui.", name, "\"\n")
    ),
    ""
  )

def helloToml(name: String): String =
  str5(
    "[package]\nname = \"",
    name,
    "\"\nversion = \"0.1.0\"\n\n[targets.native]\nkind = \"executable\"\nmain = \"Main\"\n",
    "",
    ""
  )

def cmdNew(args: List): IO[Unit] =
  if (List.len(args) < 2) IO.println("usage: scalui new <name> [--ui] [parent]")
  else cmdNewNamed(List.at(args, 1), args)

def argHasUi(args: List, i: Int): Int =
  if (i >= List.len(args)) 0
  else if (streq(List.at(args, i), "--ui") == 1) 1
  else argHasUi(args, i + 1)

def argParent(args: List, i: Int): String =
  if (i >= List.len(args)) "."
  else if (streq(List.at(args, i), "--ui") == 1) argParent(args, i + 1)
  else if (i <= 1) argParent(args, i + 1)
  else List.at(args, i)

def cmdNewNamed(name: String, args: List): IO[Unit] =
  writeNewProject(pathJoin(argParent(args, 0), name), name, argHasUi(args, 0))

def writeNewProject(dir: String, name: String, ui: Int): IO[Unit] =
  if (ui == 1) writeNewUi(dir, name) else writeNewHello(dir, name)

def writeNewUi(dir: String, name: String): IO[Unit] =
  Fs.mkdirs(pathJoin(dir, "src")).flatMap(_ =>
    Fs.mkdirs(pathJoin(dir, "goldens")).flatMap(_ =>
      Fs.write(pathJoin(dir, "scalui.toml"), uiToml(name)).flatMap(_ =>
        Fs.write(pathJoin(dir, "src/Main.scala"), uiMainTemplate()).flatMap(_ =>
          IO.println(str3("created ", dir, " (ui)"))
        )
      )
    )
  )

def writeNewHello(dir: String, name: String): IO[Unit] =
  Fs.mkdirs(pathJoin(dir, "src")).flatMap(_ =>
    Fs.write(pathJoin(dir, "scalui.toml"), helloToml(name)).flatMap(_ =>
      Fs.write(pathJoin(dir, "src/Main.scala"), helloMainTemplate()).flatMap(_ =>
        IO.println(str3("created ", dir, ""))
      )
    )
  )

def cmdTest(projectDir: String): IO[Unit] =
  resolveRuntimeEnv("", projectDir).flatMap(runtimeDir =>
    Sys.exec(str3("make -C ", runtimeDir, " test")).flatMap(_ =>
      Sys.exec(str3("make -C ", pathJoin(parentDir(runtimeDir), "ffi-skia"), " test")).flatMap(_ =>
        compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
          Sys.exec(
            str3(
              "bash ",
              pathJoin(pathJoin(parentDir(parentDir(runtimeDir)), "scripts"), "run_goldens.sh"),
              Str.concat(" ", projectDir)
            )
          ).flatMap(_ => IO.println("scalui test ok"))
        )
      )
    )
  )

def checkFlagStr(check: Int): String =
  if (check == 1) " --check" else ""

def cmdFmt(projectDir: String, check: Int): IO[Unit] =
  Sys.getenv("SCALUI_CANARY").flatMap(canary =>
    if (Str.len(canary) > 0)
      Sys.exec(str4(canary, " fmt ", projectDir, checkFlagStr(check))).flatMap(_ => IO.pure(()))
    else fmtParseValidate(projectDir)
  )

def fmtParseValidate(projectDir: String): IO[Unit] =
  Fs.list(pathJoin(projectDir, "src")).flatMap(names =>
    fmtFiles(pathJoin(projectDir, "src"), partitionSources(names, List.empty, List.empty))
  ).flatMap(_ => IO.println("scalui fmt ok"))

def fmtFiles(srcDir: String, names: List): IO[Unit] =
  if (List.isEmpty(names) == 1) IO.pure(())
  else fmtFileOne(srcDir, List.head(names), List.tail(names))

def fmtFileOne(srcDir: String, name: String, rest: List): IO[Unit] =
  Fs.read(pathJoin(srcDir, name)).flatMap(text =>
    fmtFileAfterRead(srcDir, rest, exprTag(parseSource(text)))
  )

def fmtFileAfterRead(srcDir: String, rest: List, tag: String): IO[Unit] =
  fmtFiles(srcDir, rest)

def cmdWatch(projectDir: String): IO[Unit] =
  IO.println(str3("scalui watch ", projectDir, "")).flatMap(_ => watchLoop(projectDir))

def watchLoop(projectDir: String): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
    IO.println("rebuilt").flatMap(_ =>
      IO.sleep(2000).flatMap(_ => watchLoop(projectDir))
    )
  ).handleErrorWith(_ =>
    IO.println("scalui watch build error").flatMap(_ =>
      IO.sleep(2000).flatMap(_ => watchLoop(projectDir))
    )
  )

def cmdPackage(projectDir: String, target: String): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
    resolveRuntimeEnv("", projectDir).flatMap(runtimeDir =>
      Sys.exec(
        str5(
          "bash ",
          pathJoin(pathJoin(parentDir(parentDir(runtimeDir)), "scripts"), "package_project.sh"),
          " ",
          projectDir,
          Str.concat(" ", target)
        )
      ).flatMap(_ => IO.pure(()))
    )
  )

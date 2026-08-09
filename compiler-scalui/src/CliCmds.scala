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
      "  val c = View.addChild(row, View.button(\"+1\", _ => Signal.set(count, Signal.get(count) + 1)))\n",
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
  if (List.len(args) < 2) IO.println("usage: scalui new <name> [--ui] [--path <dir>]")
  else cmdNewResolved(newNameArg(args), args)

def newNameArg(args: List): String =
  positionalAt(args, 1, 0)

def argHasUi(args: List, i: Int): Int =
  if (i >= List.len(args)) 0
  else if (streq(List.at(args, i), "--ui") == 1) 1
  else argHasUi(args, i + 1)

def argPathValue(args: List, i: Int): String =
  if (i >= List.len(args)) "."
  else if (streq(List.at(args, i), "--path") == 1)
    if (i + 1 >= List.len(args)) "." else List.at(args, i + 1)
  else argPathValue(args, i + 1)

def newParentDir(args: List): String =
  if (argFlag(args, "--path", 0) == 1) argPathValue(args, 0)
  else "."

def cmdNewResolved(name: String, args: List): IO[Unit] =
  if (streq(name, ".") == 1)
    IO.println("usage: scalui new <name> [--ui] [--path <dir>]")
  else if (startsWith(name, "--") == 1)
    IO.println("usage: scalui new <name> [--ui] [--path <dir>]")
  else writeNewProject(pathJoin(newParentDir(args), name), name, argHasUi(args, 0))

def writeNewProject(dir: String, name: String, ui: Int): IO[Unit] =
  if (ui == 1) writeNewUi(dir, name) else writeNewHello(dir, name)

def writeNewUi(dir: String, name: String): IO[Unit] =
  Fs.mkdirs(pathJoin(dir, "src")).flatMap(_ =>
    Fs.mkdirs(pathJoin(dir, "goldens")).flatMap(_ =>
      Fs.write(pathJoin(dir, "scalui.toml"), uiToml(name)).flatMap(_ =>
        Fs.write(pathJoin(dir, "src/Main.scala"), uiMainTemplate()).flatMap(_ =>
          IO.println(
            str3(
              "created ",
              dir,
              " (ui) — next: scalui test (seeds goldens) && scalui run --headless"
            )
          )
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

def updateFlagEnv(update: Int): String =
  if (update == 1) "SCALUI_UPDATE_GOLDENS=1 " else ""

def cmdTest(projectDir: String, update: Int, runtimeTests: Int): IO[Unit] =
  resolveRuntimeEnv("", projectDir).flatMap(runtimeDir =>
    maybeRuntimeTests(runtimeDir, runtimeTests).flatMap(_ =>
      compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
        execOk(
          str5(
            updateFlagEnv(update),
            "bash ",
            pathJoin(pathJoin(parentDir(parentDir(runtimeDir)), "scripts"), "run_goldens.sh"),
            " ",
            projectDir
          )
        ).flatMap(_ => IO.println("scalui test ok"))
      )
    )
  )

def maybeRuntimeTests(runtimeDir: String, runtimeTests: Int): IO[Unit] =
  if (runtimeTests == 0) IO.pure(())
  else
    execOk(str3("make -C ", runtimeDir, " test")).flatMap(_ =>
      execOk(str3("make -C ", pathJoin(parentDir(runtimeDir), "ffi-skia"), " test"))
    )

def checkFlagStr(check: Int): String =
  if (check == 1) " --check" else ""

def cmdFmt(projectDir: String, check: Int): IO[Unit] =
  Sys.getenv("SCALUI_CANARY").flatMap(canary =>
    if (Str.len(canary) > 0)
      execOk(str4(canary, " fmt ", projectDir, checkFlagStr(check)))
    else
      IO.println(
        "scalui fmt: Stage-1 formatter not ported yet; set SCALUI_CANARY to the Stage-0 binary (cargo run -p scalui)"
      ).flatMap(_ => IO.fail("fmt requires SCALUI_CANARY until Stage-1 pretty-printer lands"))
  )

def cmdWatch(projectDir: String): IO[Unit] =
  IO.println(str3("scalui watch ", projectDir, "")).flatMap(_ =>
    srcFingerprint(projectDir).flatMap(fp => watchLoop(projectDir, fp, 1))
  )

def watchLoop(projectDir: String, lastFp: String, force: Int): IO[Unit] =
  maybeRebuild(projectDir, lastFp, force).flatMap(fp2 =>
    IO.sleep(500).flatMap(_ =>
      srcFingerprint(projectDir).flatMap(fp3 =>
        if (streq(fp3, fp2) == 1) watchLoop(projectDir, fp2, 0)
        else watchLoop(projectDir, fp3, 1)
      )
    )
  )

def maybeRebuild(projectDir: String, lastFp: String, force: Int): IO[String] =
  if (force == 0) IO.pure(lastFp)
  else
    compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
      IO.println("rebuilt").flatMap(_ => IO.pure(lastFp))
    ).handleErrorWith(_ =>
      IO.println("scalui watch build error").flatMap(_ => IO.pure(lastFp))
    )

def srcFingerprint(projectDir: String): IO[String] =
  Fs.list(pathJoin(projectDir, "src")).flatMap(names =>
    fpFiles(pathJoin(projectDir, "src"), partitionSources(names, List.empty, List.empty), "")
  )

def fpFiles(srcDir: String, names: List, acc: String): IO[String] =
  if (List.isEmpty(names) == 1) IO.pure(acc)
  else
    Fs.read(pathJoin(srcDir, List.head(names))).flatMap(text =>
      fpFiles(srcDir, List.tail(names), str4(acc, List.head(names), "\n", text))
    )

def cmdPackage(projectDir: String, target: String): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
    resolveRuntimeEnv("", projectDir).flatMap(runtimeDir =>
      execOk(
        str5(
          "bash ",
          pathJoin(pathJoin(parentDir(parentDir(runtimeDir)), "scripts"), "package_project.sh"),
          " ",
          projectDir,
          Str.concat(" ", target)
        )
      )
    )
  )

def cmdRun(projectDir: String, headless: Int): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ =>
    Fs.read(pathJoin(projectDir, "scalui.toml")).flatMap(toml =>
      runCompiled(projectDir, readTomlName(toml), toml, headless)
    )
  )

def runCompiled(projectDir: String, name: String, toml: String, headlessForce: Int): IO[Unit] =
  if (headlessForce == 1) runHeadless(projectDir, name, toml)
  else if (hasUiSection(toml) == 0) execOk(pathJoin(pathJoin(projectDir, "build"), name))
  else runWithDefaultRuntime(projectDir, name, toml)

def runWithDefaultRuntime(projectDir: String, name: String, toml: String): IO[Unit] =
  if (streq(readTomlDefaultRuntime(toml), "window") == 1) runWindow(projectDir, name, toml)
  else if (streq(readTomlDefaultRuntime(toml), "mobile") == 1) runMobile(projectDir, name, toml)
  else runHeadless(projectDir, name, toml)

def runHeadless(projectDir: String, name: String, toml: String): IO[Unit] =
  val exe = pathJoin(pathJoin(projectDir, "build"), name)
  val snap = pathJoin(pathJoin(projectDir, "build"), "snapshot.png")
  val w = readTomlHeadlessW(toml)
  val h = readTomlHeadlessH(toml)
  IO.println(str3("scalui run --headless → snapshot ", snap, "")).flatMap(_ =>
    execOk(
      str5(
        "env SCALUI_UI_RUNTIME=headless SCALUI_SNAPSHOT_PATH=",
        snap,
        " SCALUI_UI_WIDTH=",
        w,
        str4(" SCALUI_UI_HEIGHT=", h, " ", exe)
      )
    )
  )

def runWindow(projectDir: String, name: String, toml: String): IO[Unit] =
  val exe = pathJoin(pathJoin(projectDir, "build"), name)
  IO.println("scalui run → UiRuntime.Window (desktop embedder)").flatMap(_ =>
    execOk(
      str5(
        "env SCALUI_UI_RUNTIME=window SCALUI_UI_WIDTH=",
        readTomlHeadlessW(toml),
        " SCALUI_UI_HEIGHT=",
        readTomlHeadlessH(toml),
        Str.concat(" ", exe)
      )
    )
  )

def runMobile(projectDir: String, name: String, toml: String): IO[Unit] =
  val exe = pathJoin(pathJoin(projectDir, "build"), name)
  IO.println("scalui run → UiRuntime.Mobile (host shell)").flatMap(_ =>
    execOk(
      str5(
        "env SCALUI_UI_RUNTIME=mobile SCALUI_MOBILE_SHELL=1 SCALUI_UI_WIDTH=",
        readTomlHeadlessW(toml),
        " SCALUI_UI_HEIGHT=",
        readTomlHeadlessH(toml),
        Str.concat(" ", exe)
      )
    )
  )

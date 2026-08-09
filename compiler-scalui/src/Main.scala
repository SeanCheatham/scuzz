package scalui.compiler

def usage(): String =
  "scalui (build|run|test|fmt|watch|new|package) [args]"

def defaultProject(args: List): String =
  if (List.len(args) <= 1) "."
  else List.at(args, 1)

def argFlag(args: List, flag: String, i: Int): Int =
  if (i >= List.len(args)) 0
  else if (streq(List.at(args, i), flag) == 1) 1
  else argFlag(args, flag, i + 1)

def packageTarget(args: List): String =
  packageTargetAt(args, 0)

def packageTargetAt(args: List, i: Int): String =
  if (i >= List.len(args)) "all"
  else if (streq(List.at(args, i), "--target") == 1)
    if (i + 1 >= List.len(args)) "all" else List.at(args, i + 1)
  else packageTargetAt(args, i + 1)

def dispatch(args: List): IO[Unit] =
  if (List.isEmpty(args) == 1) IO.println(usage())
  else dispatchCmd(List.head(args), args)

def dispatchCmd(cmd: String, args: List): IO[Unit] =
  if (streq(cmd, "build") == 1) dispatchBuild(defaultProject(args), 0)
  else if (streq(cmd, "run") == 1) dispatchBuild(defaultProject(args), 1)
  else if (streq(cmd, "test") == 1) cmdTest(defaultProject(args))
  else if (streq(cmd, "fmt") == 1)
    cmdFmt(defaultProject(args), argFlag(args, "--check", 0))
  else if (streq(cmd, "watch") == 1) cmdWatch(defaultProject(args))
  else if (streq(cmd, "new") == 1) cmdNew(args)
  else if (streq(cmd, "package") == 1)
    cmdPackage(defaultProject(args), packageTarget(args))
  else IO.println(str3("unknown command: ", cmd, Str.concat("\n", usage())))

def dispatchBuild(projectDir: String, doRun: Int): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), doRun)

@main def main: IO[Unit] =
  Sys.args().flatMap(args => dispatch(args))

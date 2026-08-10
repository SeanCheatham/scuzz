package scalui.compiler

def usage(): String =
  "scalui (build|run|test|fmt|watch|new|package) [args]"

def isFlag(s: String): Int =
  if (startsWith(s, "--") == 1) 1 else 0

def positionalAt(args: List, i: Int, n: Int): String =
  if (i >= List.len(args)) "." else if (streq(List.at(args, i), "--path") == 1) positionalAt(args, i + 2, n) else if (isFlag(List.at(args, i)) == 1) positionalAt(args, i + 1, n) else if (n == 0) List.at(args, i) else positionalAt(args, i + 1, n - 1)

def defaultProject(args: List): String =
  positionalAt(args, 1, 0)

def argFlag(args: List, flag: String, i: Int): Int =
  if (i >= List.len(args)) 0 else if (streq(List.at(args, i), flag) == 1) 1 else argFlag(args, flag, i + 1)

def packageTarget(args: List): String =
  packageTargetAt(args, 0)

def packageTargetAt(args: List, i: Int): String =
  if (i >= List.len(args)) "all" else if (streq(List.at(args, i), "--target") == 1) if (i + 1 >= List.len(args)) "all" else List.at(args, i + 1) else packageTargetAt(args, i + 1)

def dispatch(args: List): IO[Unit] =
  if (List.isEmpty(args) == 1) IO.println(usage()) else dispatchCmd(List.head(args), args)

def dispatchCmd(cmd: String, args: List): IO[Unit] =
  if (streq(cmd, "build") == 1) dispatchBuild(defaultProject(args), 0) else if (streq(cmd, "run") == 1) cmdRun(defaultProject(args), argFlag(args, "--headless", 0)) else if (streq(cmd, "test") == 1) cmdTest(defaultProject(args), argFlag(args, "--update", 0), argFlag(args, "--runtime-tests", 0)) else if (streq(cmd, "fmt") == 1) cmdFmt(defaultProject(args), argFlag(args, "--check", 0)) else if (streq(cmd, "watch") == 1) cmdWatch(defaultProject(args)) else if (streq(cmd, "new") == 1) cmdNew(args) else if (streq(cmd, "package") == 1) cmdPackage(defaultProject(args), packageTarget(args)) else IO.println(str3("unknown command: ", cmd, Str.concat("\n", usage())))

def dispatchBuild(projectDir: String, doRun: Int): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), doRun)

@main def main: IO[Unit] =
  Sys.args().flatMap(args =>     dispatch(args))

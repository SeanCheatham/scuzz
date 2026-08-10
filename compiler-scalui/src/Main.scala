package scalui.compiler

def usage(): String =
  "scalui (build|run|test|check|fuzz|fmt|watch|new|package) [args]\n  --message-format=human|json"

def isFlag(s: String): Int =
  if (startsWith(s, "--") == 1) 1 else 0

def positionalAt(args: List, i: Int, n: Int): String =
  if (i >= List.len(args)) "." else if (streq(List.at(args, i), "--path") == 1) positionalAt(args, i + 2, n) else if (streq(List.at(args, i), "--target") == 1) positionalAt(args, i + 2, n) else if (isFlag(List.at(args, i)) == 1) positionalAt(args, i + 1, n) else if (n == 0) List.at(args, i) else positionalAt(args, i + 1, n - 1)

def defaultProject(args: List): String =
  positionalAt(args, 1, 0)

def argFlag(args: List, flag: String, i: Int): Int =
  if (i >= List.len(args)) 0 else if (streq(List.at(args, i), flag) == 1) 1 else argFlag(args, flag, i + 1)

def argValue(args: List, flag: String, i: Int): String =
  if (i >= List.len(args)) "" else if (streq(List.at(args, i), flag) == 1) if (i + 1 >= List.len(args)) "" else List.at(args, i + 1) else if (startsWith(List.at(args, i), Str.concat(flag, "=")) == 1) Str.slice(List.at(args, i), Str.len(flag) + 1, Str.len(List.at(args, i))) else argValue(args, flag, i + 1)

def messageFormatJson(args: List): Int =
  if (streq(argValue(args, "--message-format", 0), "json") == 1) 1 else 0

def packageTarget(args: List): String =
  packageTargetAt(args, 0)

def packageTargetAt(args: List, i: Int): String =
  if (i >= List.len(args)) "all" else if (streq(List.at(args, i), "--target") == 1) if (i + 1 >= List.len(args)) "all" else List.at(args, i + 1) else packageTargetAt(args, i + 1)

def dispatch(args: List): IO[Unit] =
  if (List.isEmpty(args) == 1) IO.println(usage()) else dispatchSkipFlags(args, args)

def dispatchSkipFlags(args: List, all: List): IO[Unit] =
  if (List.isEmpty(args) == 1) IO.println(usage()) else if (isFlag(List.head(args)) == 1) if (startsWith(List.head(args), "--message-format=") == 1) dispatchSkipFlags(List.tail(args), all) else if (streq(List.head(args), "--message-format") == 1) if (List.len(args) < 2) IO.println(usage()) else dispatchSkipFlags(List.tail(List.tail(args)), all) else dispatchCmd(List.head(args), args, all) else dispatchCmd(List.head(args), args, all)

def dispatchCmd(cmd: String, args: List, all: List): IO[Unit] =
  if (streq(cmd, "build") == 1) dispatchBuild(defaultProject(args), 0) else if (streq(cmd, "run") == 1) cmdRun(defaultProject(args), argFlag(args, "--headless", 0)) else if (streq(cmd, "test") == 1) cmdTest(defaultProject(args), argFlag(args, "--update", 0), argFlag(args, "--runtime-tests", 0), argFlag(args, "--pixels", 0)) else if (streq(cmd, "check") == 1) cmdCheck(defaultProject(args), messageFormatJson(all)) else if (streq(cmd, "fuzz") == 1) cmdFuzzArgs(args) else if (streq(cmd, "fmt") == 1) cmdFmt(defaultProject(args), argFlag(args, "--check", 0)) else if (streq(cmd, "watch") == 1) cmdWatch(defaultProject(args)) else if (streq(cmd, "new") == 1) cmdNew(args) else if (streq(cmd, "package") == 1) cmdPackage(defaultProject(args), packageTarget(args)) else IO.println(str3("unknown command: ", cmd, Str.concat("\n", usage())))

def dispatchBuild(projectDir: String, doRun: Int): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), doRun)

def cmdCheck(projectDir: String, json: Int): IO[Unit] =
  checkProject(projectDir).flatMap(ty => if (tyIsOk(ty) == 1) if (json == 1) IO.println("[]") else IO.println("scalui check ok") else emitCheckFail(ty, json))

def emitCheckFail(ty: String, json: Int): IO[Unit] =
  if (json == 1) IO.println(str3("[{\"severity\":\"error\",\"message\":", jsonQuote(tyMsg(ty)), "}]")).flatMap(_ => IO.fail(tyMsg(ty))) else IO.println(str3("error: ", tyMsg(ty), "")).flatMap(_ => IO.fail(tyMsg(ty)))

def jsonQuote(s: String): String =
  str3("\"", jsonEscape(s, 0, ""), "\"")

def jsonEscape(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else jsonEscapeChar(s, i, acc, Str.charAt(s, i))

def jsonEscapeChar(s: String, i: Int, acc: String, c: Int): String =
  if (c == 34) jsonEscape(s, i + 1, Str.concat(acc, "\\\"")) else if (c == 92) jsonEscape(s, i + 1, Str.concat(acc, "\\\\")) else if (c == 10) jsonEscape(s, i + 1, Str.concat(acc, "\\n")) else jsonEscape(s, i + 1, Str.concat(acc, charToStr(c)))

@main def main: IO[Unit] =
  Sys.args().flatMap(args => dispatch(args))

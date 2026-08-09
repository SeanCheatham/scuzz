package scalui.compiler

def usage(): String = "scalui (build|run) [project]"

def defaultProject(args: List): String =
  if (List.len(args) <= 1) "."
  else List.at(args, 1)

def dispatch(args: List): IO[Unit] =
  if (List.isEmpty(args) == 1) IO.println(usage())
  else dispatchCmd(List.head(args), args)

def dispatchCmd(cmd: String, args: List): IO[Unit] =
  if (streq(cmd, "build") == 1) dispatchBuild(defaultProject(args), 0)
  else if (streq(cmd, "run") == 1) dispatchBuild(defaultProject(args), 1)
  else IO.println(str3("unknown command: ", cmd, Str.concat("\n", usage())))

def dispatchBuild(projectDir: String, doRun: Int): IO[Unit] =
  compileProject(projectDir, pathJoin(projectDir, "build"), doRun)

@main def main: IO[Unit] =
  Sys.args().flatMap(args => dispatch(args))

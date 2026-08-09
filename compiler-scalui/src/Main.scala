package scalui.compiler

// Stage-1 lexer/parser smoke (built by Stage 0).
@main def main: IO[Unit] =
  val src = "@main def main: IO[Unit] = IO.println(\"x\")"
  val toks = lex(src)
  val prog = parseProgram(toks)
  IO.println(Str.concat("tokens:", Str.fromInt(List.len(toks)))).flatMap(_ =>
    IO.println(Str.concat("prog:", exprTag(prog)))
  )

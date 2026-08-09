package scalui.parser

// ScalUI-written parser smoke (Phase 3): classify + match in ScalUI,
// still built by Stage 0 (ADR 0005).
@main def main: IO[Unit] =
  val t = Lexer.classify("@main")
  t match {
    case Tok.AtMain =>
      IO.println("parser-stage0: AtMain").flatMap(_ =>
        val d = Lexer.classify("def")
        d match {
          case Tok.Def => IO.println("parser-stage0: ok")
          case _ => IO.println("parser-stage0: bad-def")
        }
      )
    case _ => IO.println("parser-stage0: bad-atmain")
  }

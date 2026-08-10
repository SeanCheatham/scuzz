// Phase 6 — blessed Clock.realTime / Clock.monotonic (live interpreters).
@main def main: IO[Unit] =
  Clock.realTime.flatMap(t =>
    IO.println(s"real:$t").flatMap(_ =>
      Clock.monotonic.flatMap(m =>
        IO.println(s"mono:$m")
      )
    )
  )

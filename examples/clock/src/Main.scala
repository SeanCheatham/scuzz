// Phase 6 — blessed Clock.realTime / Clock.monotonic (live interpreters).
@main def main: IO[Unit] =
  Clock.realTime.flatMap(t =>
    IO.println(Str.concat("real:", Str.fromInt(t))).flatMap(_ =>
      Clock.monotonic.flatMap(m =>
        IO.println(Str.concat("mono:", Str.fromInt(m)))
      )
    )
  )

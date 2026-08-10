@main def main: IO[Unit] =
  IO.println("Hello, ScalUI!").flatMap(_ =>     IO.println("Phase 0 online."))

@main def main: IO[Unit] =
  IO.println("Hello, ScalUI!").flatMap(_ => IO.println("ready."))

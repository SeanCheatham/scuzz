@main def main: IO[Unit] =
  for {
    n = Signal.int(0)
    root = View.column(
      View.text("Hello Headless"),
      View.textSignal(n, "taps = "),
      View.button("tap", _ => Signal.set(n, Signal.get(n) + 1))
    )
    _ <- Ui.run(root)
  } yield ()

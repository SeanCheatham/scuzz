@main def main: IO[Unit] =
  for {
    count = Signal.int(0)
    root = View.column(
      View.text("Counter"),
      View.textSignal(count, "count = "),
      View.row(
        View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)),
        View.icon(43, Theme.accent()),
        View.image(24, 24, Color.rgb(61, 126, 166), "")
      )
    )
    _ <- Ui.run(root)
  } yield ()

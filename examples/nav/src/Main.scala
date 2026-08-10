@main def main: IO[Unit] =
  for {
    page = Signal.int(0)
    count = Signal.int(0)
    home = View.column(
      View.text("Home"),
      View.textSignal(count, "count = "),
      View.button("+1", _ => Signal.set(count, Signal.get(count) + 1))
    )
    other = View.column(
      View.text("Other"),
      View.textSignal(page, "page = ")
    )
    root = View.column(
      View.row(
        View.button("Home", _ => Signal.set(page, 0)),
        View.button("Other", _ => Signal.set(page, 1))
      ),
      View.showWhen(page, 0, home),
      View.showWhen(page, 1, other)
    )
    _ <- Ui.run(root)
  } yield ()

@main def main: IO[Unit] =
  for {
    root = View.column(
      View.text("Live"),
      View.text("Press q or Esc to quit")
    )
    _ <- Ui.run(root)
  } yield ()

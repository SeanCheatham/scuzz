@main def main: IO[Unit] =
  for {
    root = View.column(View.text("Hello Headless"))
    _ <- Ui.run(root)
  } yield ()

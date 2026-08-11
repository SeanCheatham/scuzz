@main def main: IO[Unit] =
  Fs.mkdirs("examples/fs/build").flatMap(_ =>
    Fs.write("examples/fs/build/note.txt", "fs-note").flatMap(_ =>
      Fs.read("examples/fs/build/note.txt").flatMap(s => IO.println(s"fs:$s"))
    )
  )

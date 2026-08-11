@main def main: IO[Unit] =
  for {
    args <- Sys.args()
    line <- Sys.readLine()
    joined = List.join(args, " ")
    _ <- IO.println(Str.concat("args: ", joined))
    _ <- IO.println(Str.concat("line: ", line))
  } yield ()

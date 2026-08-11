package demo.color

@main def main: IO[Unit] =
  for {
    c = Color.Red
  } yield c match {
  case Color.Red => IO.println("adt:red")
  case Color.Blue => IO.println("adt:blue")
}

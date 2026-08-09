package demo.color

@main def main: IO[Unit] =
  val c = Color.Red
  c match {
    case Color.Red => IO.println("adt:red")
    case Color.Blue => IO.println("adt:blue")
  }

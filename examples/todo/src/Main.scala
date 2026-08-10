def setAt(xs: List, i: Int, v: String): List =
  if (List.isEmpty(xs) == 1) List.empty() else if (i == 0) List.cons(v, List.tail(xs)) else List.cons(List.head(xs), setAt(List.tail(xs), i - 1, v))

@main def main: IO[Unit] =
  Sys.getenv("SCALUI_TODO_PATH").flatMap(envPath =>
    for {
      path = if (Str.len(envPath) == 0) "/tmp/scalui_todo.txt" else envPath
      draft = Signal.str("")
      items = Signal.list([])
      list = View.list()
      root = View.column()
      _ = View.addChild(root, View.text("Todo"))
      row = View.row()
      _ = View.addChild(row, View.textField(draft, "item"))
      _ = View.addChild(row, View.button("Add", _ =>
        for {
          d = Signal.getStr(draft)
          n = Str.len(d)
        } yield if (n == 0) ()
        else
          for {
            xs = List.append(Signal.getList(items), d)
            _ = Signal.setList(items, xs)
            _ = View.setTexts(list, xs)
          } yield Signal.setStr(draft, "")
      ))
      _ = View.addChild(row, View.button("Rename", _ =>
        for {
          xs = Signal.getList(items)
          ys = setAt(xs, 0, "oat milk")
          _ = Signal.setList(items, ys)
        } yield View.setTexts(list, ys)
      ))
      _ = View.addChild(root, row)
      _ = View.addChild(root, View.scroll(list))
      _ = View.addChild(root, View.button("Save", _ =>
        for {
          xs = Signal.getList(items)
          body = if (List.isEmpty(xs) == 1) "" else Str.concat(List.join(xs, "\n"), "\n")
        } yield Fs.write(path, body)
      ))
      _ = View.addChild(root, View.button("Clear", _ =>
        for {
          _ = Signal.setList(items, [])
        } yield View.clearChildren(list)
      ))
      text <- Fs.read(path).handleErrorWith(_ => IO.pure(""))
      loaded0 = Str.lines(text)
      loaded = if (List.isEmpty(loaded0) == 1) ["milk"] else loaded0
      _ = Signal.setList(items, loaded)
      _ = View.setTexts(list, loaded)
      _ <- Ui.run(root)
    } yield ()
  )

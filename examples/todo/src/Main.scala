// ScalUI Todo — List literals + Signal.list + View lambdas (no C Todo controller)
@main def main: IO[Unit] =
  Sys.getenv("SCALUI_TODO_PATH").flatMap(envPath =>
    val path = if (Str.len(envPath) == 0) "/tmp/scalui_todo.txt" else envPath
    val draft = Signal.str("")
    val items = Signal.list([])
    val list = View.list()
    val root = View.column()
    val a = View.addChild(root, View.text("Todo"))
    val row = View.row()
    val b = View.addChild(row, View.textField(draft, "item"))
    val c = View.addChild(row, View.button("Add", _ =>
      val d = Signal.getStr(draft)
      val n = Str.len(d)
      val x = if (n == 0) () else View.addChild(list, View.text(s"- $d"))
      val y = if (n == 0) () else Signal.setList(items, List.append(Signal.getList(items), d))
      if (n == 0) () else Signal.setStr(draft, "")
    ))
    val d2 = View.addChild(root, row)
    val e = View.addChild(root, View.scroll(list))
    val f = View.addChild(root, View.button("Save", _ =>
      val xs = Signal.getList(items)
      val body = if (List.isEmpty(xs) == 1) "" else Str.concat(List.join(xs, "\n"), "\n")
      Fs.write(path, body)
    ))
    Fs.read(path).handleErrorWith(_ => IO.pure("")).flatMap(text =>
      val loaded = Str.lines(text)
      val g = Signal.setList(items, loaded)
      val h = View.addTexts(list, loaded)
      Ui.run(root)
    )
  )

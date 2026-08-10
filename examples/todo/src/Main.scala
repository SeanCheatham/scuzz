def setAt(xs: List, i: Int, v: String): List =
  if (List.isEmpty(xs) == 1) List.empty() else if (i == 0) List.cons(v, List.tail(xs)) else List.cons(List.head(xs), setAt(List.tail(xs), i - 1, v))

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
    val c = View.addChild(row, View.button("Add", _ => val d = Signal.getStr(draft)
val n = Str.len(d)
if (n == 0) ()
else
  val xs = List.append(Signal.getList(items), d)
  val y = Signal.setList(items, xs)
  val z = View.setTexts(list, xs)
  Signal.setStr(draft, "")))
    val rename = View.addChild(row, View.button("Rename", _ => val xs = Signal.getList(items)
val ys = setAt(xs, 0, "oat milk")
val y = Signal.setList(items, ys)
View.setTexts(list, ys)))
    val d2 = View.addChild(root, row)
    val e = View.addChild(root, View.scroll(list))
    val f = View.addChild(root, View.button("Save", _ => val xs = Signal.getList(items)
val body = if (List.isEmpty(xs) == 1) "" else Str.concat(List.join(xs, "\n"), "\n")
Fs.write(path, body)))
    val clear = View.addChild(root, View.button("Clear", _ => val y = Signal.setList(items, [])
View.clearChildren(list)))
    Fs.read(path).handleErrorWith(_ =>
  IO.pure("")
).flatMap(text =>
      val loaded0 = Str.lines(text)
      val loaded = if (List.isEmpty(loaded0) == 1) ["milk"] else loaded0
      val g = Signal.setList(items, loaded)
      val h = View.setTexts(list, loaded)
      Ui.run(root)
    )
  )

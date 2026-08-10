// ScalUI Counter — View tree expressed in ScalUI (Signal + View + Ui.run).
// Taps are first-class lambdas (closures) over the enclosing Signal.
@main def main: IO[Unit] =
  val count = Signal.int(0)
  val root = View.column()
  val a = View.addChild(root, View.text("Counter"))
  val b = View.addChild(root, View.textSignal(count, "count = "))
  val row = View.row()
  val c = View.addChild(row, View.button("+1", _ => Signal.set(count, Signal.get(count) + 1)))
  val d = View.addChild(row, View.icon(43, Theme.accent))
  val e = View.addChild(row, View.image(24, 24, Color.rgb(61, 126, 166), ""))
  val f = View.addChild(root, row)
  Ui.run(root)

# Kit catalog

Signatures live in `examples/compiler/src/Kits.scuzz`. The typechecker uses that same table.

| Kit | Params | Return |
| --- | --- | --- |
| `Str.fromInt` | `Int` | `String` |
| `Str.fromBool` | `Bool` | `String` |
| `Str.concat` | `String, String` | `String` |
| `Str.len` | `String` | `Int` |
| `Str.byteLen` | `String` | `Int` |
| `Str.charAt` | `String, Int` | `Int` |
| `Str.slice` | `String, Int, Int` | `String` |
| `Str.byteSlice` | `String, Int, Int` | `String` |
| `Str.toInt` | `String, Int` | `Int` |
| `Str.repeat` | `String, Int` | `String` |
| `Str.startsWith` | `String, String` | `Bool` |
| `Str.endsWith` | `String, String` | `Bool` |
| `Str.contains` | `String, String` | `Bool` |
| `Str.eq` | `String, String` | `Bool` |
| `Str.indexOf` | `String, String` | `Int` |
| `Str.lastIndexOf` | `String, String` | `Int` |
| `Str.isEmpty` | `String` | `Bool` |
| `Str.nonEmpty` | `String` | `Bool` |
| `Str.isBlank` | `String` | `Bool` |
| `Str.lines` | `String` | `List[String]` |
| `Str.trim` | `String` | `String` |
| `Str.reverse` | `String` | `String` |
| `Str.toLower` | `String` | `String` |
| `Str.toUpper` | `String` | `String` |
| `Str.capitalize` | `String` | `String` |
| `Str.take` | `String, Int` | `String` |
| `Str.drop` | `String, Int` | `String` |
| `Str.takeRight` | `String, Int` | `String` |
| `Str.dropRight` | `String, Int` | `String` |
| `Str.padLeft` | `String, Int, String` | `String` |
| `Str.padRight` | `String, Int, String` | `String` |
| `Str.replace` | `String, String, String` | `String` |
| `Str.stripPrefix` | `String, String` | `String` |
| `Str.stripSuffix` | `String, String` | `String` |
| `Str.split` | `String, String` | `List[String]` |
| `List.len` | `List` | `Int` |
| `List.concat` | `List, List` | `List` |
| `List.reverse` | `List` | `List` |
| `List.head` | `List` | `Option` |
| `List.tail` | `List` | `List` |
| `List.isEmpty` | `List` | `Bool` |
| `List.cons` | `A, List` | `List` |
| `List.at` | `List, Int` | `A` |
| `List.join` | `List[String], String` | `String` |
| `List.take` | `List, Int` | `List` |
| `List.drop` | `List, Int` | `List` |
| `List.takeRight` | `List, Int` | `List` |
| `List.dropRight` | `List, Int` | `List` |
| `List.init` | `List` | `List` |
| `List.last` | `List` | `List` |
| `List.flatten` | `List` | `List` |
| `List.empty` | `()` | `List` |
| `List.setAt` | `List, Int, A` | `List` |
| `List.getOrElse` | `List, Int, A` | `A` |
| `List.fill` | `Int, A` | `List` |
| `List.range` | `Int, Int` | `List[Int]` |
| `List.padTo` | `List, Int, A` | `List` |
| `List.nonEmpty` | `List` | `Bool` |
| `List.map` | `List, A => B` | `List` |
| `List.flatMap` | `List, A => List` | `List` |
| `List.filter` | `List, A => Bool` | `List` |
| `List.filterNot` | `List, A => Bool` | `List` |
| `List.find` | `List, A => Bool` | `List` |
| `List.findLast` | `List, A => Bool` | `List` |
| `List.exists` | `List, A => Bool` | `Bool` |
| `List.forall` | `List, A => Bool` | `Bool` |
| `List.count` | `List, A => Bool` | `Int` |
| `List.takeWhile` | `List, A => Bool` | `List` |
| `List.dropWhile` | `List, A => Bool` | `List` |
| `List.indexWhere` | `List, A => Bool` | `Int` |
| `List.lastIndexWhere` | `List, A => Bool` | `Int` |
| `List.span` | `List, A => Bool` | `List` |
| `List.partition` | `List, A => Bool` | `List` |
| `List.prefixLength` | `List, A => Bool` | `Int` |
| `List.segmentLength` | `List, A => Bool, Int` | `Int` |
| `List.sortBy` | `List, A => Int` | `List` |
| `List.maxBy` | `List, A => Int` | `A` |
| `List.minBy` | `List, A => Int` | `A` |
| `List.groupBy` | `List, A => B` | `Map` |
| `List.distinctBy` | `List, A => B` | `List` |
| `List.foldLeft` | `List, B, (B, A) => B` | `B` |
| `List.foldRight` | `List, B, (A, B) => B` | `B` |
| `List.scanLeft` | `List, B, (B, A) => B` | `List` |
| `List.scanRight` | `List, B, (A, B) => B` | `List` |
| `List.reduceLeft` | `List, (A, A) => A` | `A` |
| `List.reduceRight` | `List, (A, A) => A` | `A` |
| `List.tabulate` | `Int, Int => A` | `List` |
| `List.contains` | `List, A` | `Bool` |
| `List.indexOf` | `List, A` | `Int` |
| `List.lastIndexOf` | `List, A` | `Int` |
| `List.distinct` | `List` | `List` |
| `List.indices` | `List` | `List[Int]` |
| `List.inits` | `List` | `List` |
| `List.tails` | `List` | `List` |
| `List.transpose` | `List` | `List` |
| `List.zipWithIndex` | `List` | `List` |
| `List.toMap` | `List` | `Map` |
| `List.unzip` | `List` | `(List, List)` |
| `List.zip` | `List, List` | `List` |
| `List.zipAll` | `List, List, A, B` | `List` |
| `List.interleave` | `List, List` | `List` |
| `List.intersperse` | `List, A` | `List` |
| `List.diff` | `List, List` | `List` |
| `List.intersect` | `List, List` | `List` |
| `List.startsWith` | `List, List` | `Bool` |
| `List.endsWith` | `List, List` | `Bool` |
| `List.sameElements` | `List, List` | `Bool` |
| `List.patch` | `List, Int, List, Int` | `List` |
| `List.splitAt` | `List, Int` | `List` |
| `List.grouped` | `List, Int` | `List` |
| `List.sliding` | `List, Int` | `List` |
| `List.slice` | `List, Int, Int` | `List` |
| `List.isDefinedAt` | `List, Int` | `Bool` |
| `List.lengthCompare` | `List, Int` | `Int` |
| `List.sort` | `List` | `List` |
| `List.max` | `List` | `A` |
| `List.min` | `List` | `A` |
| `List.sum` | `List` | `Int` |
| `List.product` | `List` | `Int` |
| `List.toSet` | `List` | `Set` |
| `List.indexOfSlice` | `List, List` | `Int` |
| `List.lastIndexOfSlice` | `List, List` | `Int` |
| `List.append` | `List, A` | `List` |
| `Map.empty` | `()` | `Map` |
| `Map.set` | `Map, A, B` | `Map` |
| `Map.keys` | `Map` | `List` |
| `Map.values` | `Map` | `List` |
| `Map.size` | `Map` | `Int` |
| `Map.contains` | `Map, A` | `Bool` |
| `Map.get` | `Map, A` | `Option` |
| `Map.getOrElse` | `Map, A, B` | `B` |
| `Map.remove` | `Map, A` | `Map` |
| `Map.toList` | `Map` | `List` |
| `Map.isEmpty` | `Map` | `Bool` |
| `Map.nonEmpty` | `Map` | `Bool` |
| `Map.union` | `Map, Map` | `Map` |
| `Map.intersect` | `Map, Map` | `Map` |
| `Map.diff` | `Map, Map` | `Map` |
| `Map.filter` | `Map, B => Bool` | `Map` |
| `Map.mapValues` | `Map, B => C` | `Map` |
| `Map.exists` | `Map, B => Bool` | `Bool` |
| `Map.forall` | `Map, B => Bool` | `Bool` |
| `Set.empty` | `()` | `Set` |
| `Set.add` | `Set, A` | `Set` |
| `Set.toList` | `Set` | `List` |
| `Set.size` | `Set` | `Int` |
| `Set.contains` | `Set, A` | `Bool` |
| `Set.remove` | `Set, A` | `Set` |
| `Set.union` | `Set, Set` | `Set` |
| `Set.intersect` | `Set, Set` | `Set` |
| `Set.diff` | `Set, Set` | `Set` |
| `Set.isEmpty` | `Set` | `Bool` |
| `Set.nonEmpty` | `Set` | `Bool` |
| `Set.isSubset` | `Set, Set` | `Bool` |
| `Set.isDisjoint` | `Set, Set` | `Bool` |
| `Set.filter` | `Set, A => Bool` | `Set` |
| `Set.map` | `Set, A => B` | `Set` |
| `Set.exists` | `Set, A => Bool` | `Bool` |
| `Set.forall` | `Set, A => Bool` | `Bool` |
| `IO.pure` | `A` | `IO[A]` |
| `IO.println` | `String` | `IO[Unit]` |
| `IO.sleep` | `Int` | `IO[Unit]` |
| `IO.fail` | `E` | `IO[E, A]` |
| `IO.race` | `IO, IO` | `IO` |
| `IO.both` | `IO, IO` | `IO` |
| `IO.ensure` | `IO, IO[Unit]` | `IO` |
| `IO.timeout` | `Int, IO` | `IO` |
| `IO.forever` | `IO` | `IO` |
| `IO.repeatN` | `Int, IO` | `IO` |
| `IO.retryN` | `Int, IO` | `IO` |
| `IO.foreach` | `List, A => IO` | `IO` |
| `IO.foreachDiscard` | `List, A => IO` | `IO[Unit]` |
| `IO.when` | `Bool, IO` | `IO` |
| `IO.unless` | `Bool, IO` | `IO` |
| `Builder.empty` | `()` | `Builder` |
| `Builder.append` | `Builder, String` | `Builder` |
| `Builder.result` | `Builder` | `String` |
| `Clock.monotonic` | `()` | `IO[Int]` |
| `Clock.realTime` | `()` | `IO[Int]` |
| `Random.nextInt` | `Int` | `IO[Int]` |
| `Oracle.sumTo` | `Int` | `Int` |
| `Float.fromInt` | `Int` | `Float` |
| `Float.toInt` | `Float` | `Int` |
| `Resource.make` | `IO, A => IO` | `Resource` |
| `Resource.use` | `Resource, A => IO` | `IO` |
| `Deferred.empty` | `()` | `IO[Deferred]` |
| `Deferred.get` | `Deferred` | `IO[A]` |
| `Deferred.complete` | `Deferred, A` | `IO[Unit]` |
| `Deferred.fail` | `Deferred, String` | `IO[Unit]` |
| `Queue.unbounded` | `()` | `IO[Queue]` |
| `Queue.offer` | `Queue, A` | `IO[Unit]` |
| `Queue.take` | `Queue` | `IO[A]` |
| `Ref.of` | `A` | `IO[Ref]` |
| `Ref.get` | `Ref` | `IO[A]` |
| `Ref.set` | `Ref, A` | `IO[Unit]` |
| `Ref.update` | `Ref, A => A` | `IO[Unit]` |
| `Ref.updateAndGet` | `Ref, A => A` | `IO[A]` |
| `Fiber.fork` | `IO` | `IO[Fiber]` |
| `Fiber.join` | `Fiber` | `IO` |
| `Fiber.interrupt` | `Fiber` | `IO[Unit]` |
| `Stream.emit` | `A` | `Stream` |
| `Stream.emits` | `List` | `Stream` |
| `Stream.eval` | `IO` | `Stream` |
| `Stream.concat` | `Stream, Stream` | `Stream` |
| `Stream.map` | `Stream, A => B` | `Stream` |
| `Stream.evalMap` | `Stream, A => IO` | `Stream` |
| `Stream.evalTap` | `Stream, A => IO` | `Stream` |
| `Stream.filter` | `Stream, A => Bool` | `Stream` |
| `Stream.filterNot` | `Stream, A => Bool` | `Stream` |
| `Stream.take` | `Stream, Int` | `Stream` |
| `Stream.takeWhile` | `Stream, A => Bool` | `Stream` |
| `Stream.drop` | `Stream, Int` | `Stream` |
| `Stream.dropWhile` | `Stream, A => Bool` | `Stream` |
| `Stream.find` | `Stream, A => Bool` | `Stream` |
| `Stream.findLast` | `Stream, A => Bool` | `Stream` |
| `Stream.exists` | `Stream, A => Bool` | `IO[Bool]` |
| `Stream.forall` | `Stream, A => Bool` | `IO[Bool]` |
| `Stream.none` | `Stream, A => Bool` | `IO[Bool]` |
| `Stream.range` | `Int, Int` | `Stream` |
| `Stream.repeatN` | `Stream, Int` | `Stream` |
| `Stream.zip` | `Stream, Stream` | `Stream` |
| `Stream.zipWith` | `Stream, Stream, (A, B) => C` | `Stream` |
| `Stream.zipAll` | `Stream, Stream, A, B` | `Stream` |
| `Stream.zipWithIndex` | `Stream` | `Stream` |
| `Stream.interleave` | `Stream, Stream` | `Stream` |
| `Stream.intersperse` | `Stream, A` | `Stream` |
| `Stream.grouped` | `Stream, Int` | `Stream` |
| `Stream.sliding` | `Stream, Int` | `Stream` |
| `Stream.takeRight` | `Stream, Int` | `Stream` |
| `Stream.dropRight` | `Stream, Int` | `Stream` |
| `Stream.flatten` | `Stream` | `Stream` |
| `Stream.flatMap` | `Stream, A => Stream` | `Stream` |
| `Stream.mapConcat` | `Stream, A => List` | `Stream` |
| `Stream.scan` | `Stream, B, (B, A) => B` | `Stream` |
| `Stream.fold` | `Stream, B, (B, A) => B` | `IO` |
| `Stream.changes` | `Stream` | `Stream` |
| `Stream.orElse` | `Stream, Stream` | `Stream` |
| `Stream.iterate` | `A, Int, A => A` | `Stream` |
| `Stream.unfold` | `S, S => List` | `Stream` |
| `Stream.head` | `Stream` | `IO` |
| `Stream.last` | `Stream` | `IO` |
| `Stream.count` | `Stream` | `IO[Int]` |
| `Stream.compileToList` | `Stream` | `IO` |
| `Stream.drain` | `Stream` | `IO[Unit]` |
| `Fs.read` | `String` | `IO[String]` |
| `Fs.write` | `String, String` | `IO[Unit]` |
| `Fs.list` | `String` | `IO[List[(String, Bool)]]` |
| `Fs.mkdirs` | `String` | `IO[Unit]` |
| `Fs.exists` | `String` | `IO[Int]` |
| `Fs.delete` | `String` | `IO[Unit]` |
| `Fs.walk` | `String` | `IO[List[(String, Bool)]]` |
| `Fs.canonicalize` | `String` | `IO[String]` |
| `Fs.rename` | `String, String` | `IO[Unit]` |
| `Fs.join` | `String, String` | `String` |
| `Fs.dirname` | `String` | `String` |
| `Fs.basename` | `String` | `String` |
| `Sys.args` | `()` | `IO[List[String]]` |
| `Sys.getenv` | `String` | `IO[String]` |
| `Sys.write` | `String` | `IO[Unit]` |
| `Sys.read` | `Int` | `IO[String]` |
| `Sys.readLine` | `()` | `IO[String]` |
| `Sys.spawn` | `String` | `IO[Int]` |
| `Sys.exec` | `String` | `IO[(Int, String, String)]` |
| `Sys.alive` | `Int` | `IO[Int]` |
| `Sys.kill` | `Int` | `IO[Unit]` |
| `Sys.childWrite` | `Int, String` | `IO[Unit]` |
| `Sys.childRead` | `Int, Int` | `IO[String]` |
| `Sys.childClose` | `Int` | `IO[Unit]` |
| `Impurity.runKit` | `()` | `IO[Unit]` |
| `Json.parse` | `String` | `Result` |
| `Json.stringify` | `Json` | `Result` |
| `Json.keys` | `Json` | `List` |
| `Json.get` | `Json, String` | `List` |
| `Json.has` | `Json, String` | `Bool` |
| `Json.getStr` | `Json, String, String` | `String` |
| `Json.getBool` | `Json, String, Bool` | `Bool` |
| `Json.getInt` | `Json, String, Int` | `Int` |
| `Json.getFloat` | `Json, String, Float` | `Float` |
| `Json.intOr` | `Json, Int` | `Int` |
| `Json.boolOr` | `Json, Bool` | `Bool` |
| `Json.strOr` | `Json, String` | `String` |
| `Json.floatOr` | `Json, Float` | `Float` |
| `Json.arr` | `Json` | `List` |
| `Json.at` | `Json, Int` | `List` |
| `Json.isNull` | `Json` | `Bool` |
| `Json.isObj` | `Json` | `Bool` |
| `Json.isArr` | `Json` | `Bool` |
| `Json.isBool` | `Json` | `Bool` |
| `Json.isInt` | `Json` | `Bool` |
| `Json.isStr` | `Json` | `Bool` |
| `Json.isFloat` | `Json` | `Bool` |
| `Json.pairs` | `Json` | `List` |
| `Json.set` | `Json, String, Json` | `Json` |
| `Json.remove` | `Json, String` | `Json` |
| `Json.append` | `Json, Json` | `Json` |
| `Json.prepend` | `Json, Json` | `Json` |
| `Json.setAt` | `Json, Int, Json` | `Json` |
| `Json.dropAt` | `Json, Int` | `Json` |
| `Json.merge` | `Json, Json` | `Json` |
| `Json.Int` | `Int` | `Json` |
| `Json.Float` | `Float` | `Json` |
| `Json.Obj` | `List` | `Json` |
| `Json.Str` | `String` | `Json` |
| `Json.Bool` | `Bool` | `Json` |
| `Json.Arr` | `List` | `Json` |
| `Json.Null` | `()` | `Json` |
| `Json.asInt` | `Json` | `List` |
| `Json.asBool` | `Json` | `List` |
| `Json.asStr` | `Json` | `List` |
| `Json.asFloat` | `Json` | `List` |
| `Net.httpGet` | `String` | `IO[String]` |
| `Net.httpDelete` | `String` | `IO[String]` |
| `Net.httpHead` | `String` | `IO[String]` |
| `Net.httpPost` | `String, String` | `IO[String]` |
| `Net.httpPut` | `String, String` | `IO[String]` |
| `Net.httpPatch` | `String, String` | `IO[String]` |
| `Net.serve` | `Int, (String, String, String) => IO[String]` | `IO[Unit]` |
| `Net.serveOnce` | `Int, (String, String, String) => IO[String]` | `IO[Unit]` |
| `Net.tcpConnect` | `String, Int` | `IO[Tcp]` |
| `Net.tcpListen` | `Int` | `IO[Tcp]` |
| `Net.tcpAccept` | `Tcp` | `IO[Tcp]` |
| `Net.tcpRead` | `Tcp, Int` | `IO[String]` |
| `Net.tcpWrite` | `Tcp, String` | `IO[Unit]` |
| `Net.tcpClose` | `Tcp` | `IO[Unit]` |
| `Net.udpBind` | `Int` | `IO[Udp]` |
| `Net.udpSend` | `Udp, String, Int, String` | `IO[Unit]` |
| `Net.udpRecv` | `Udp, Int` | `IO[(String, Int, String)]` |
| `Net.udpClose` | `Udp` | `IO[Unit]` |
| `Signal.int` | `Int` | `Signal[Int]` |
| `Signal.intN` | `String, Int` | `Signal[Int]` |
| `Signal.get` | `Signal` | `Int` |
| `Signal.set` | `Signal, Int` | `Unit` |
| `Signal.map` | `Signal, Int => String` | `Signal[String]` |
| `Signal.mapN` | `String, Signal, Int => String` | `Signal[String]` |
| `Signal.str` | `String` | `Signal[String]` |
| `Signal.strN` | `String, String` | `Signal[String]` |
| `Signal.getStr` | `Signal` | `String` |
| `Signal.setStr` | `Signal, String` | `Unit` |
| `Signal.list` | `List[A]` | `Signal[List[A]]` |
| `Signal.listN` | `String, List[A]` | `Signal[List[A]]` |
| `Signal.getList` | `Signal[List[A]]` | `List[A]` |
| `Signal.setList` | `Signal[List[A]], List[A]` | `Unit` |
| `Color.rgb` | `Int, Int, Int` | `Int` |
| `Color.rgba` | `Int, Int, Int, Int` | `Int` |
| `Theme.accent` | `()` | `Int` |
| `Theme.primary` | `()` | `Int` |
| `Theme.muted` | `()` | `Int` |
| `Theme.foreground` | `()` | `Int` |
| `Ui.run` | `A => View` | `IO[Unit]` |
| `Ui.setTitle` | `String` | `IO[Unit]` |
| `Ui.setEditorCaret` | `Int, Int` | `IO[Unit]` |
| `Ui.editorCaret` | `()` | `IO[Int]` |
| `Ui.setEditorDiagnostics` | `List` | `IO[Unit]` |
| `Ui.setEditorTokens` | `List` | `IO[Unit]` |
| `Ui.setEditorInlays` | `List` | `IO[Unit]` |
| `Ui.setEditorFolds` | `List` | `IO[Unit]` |
| `View.column` | `View*` | `View` |
| `View.row` | `View*` | `View` |
| `View.stack` | `View*` | `View` |
| `View.wrap` | `View*` | `View` |
| `View.grid` | `Int, View*` | `View` |
| `View.divider` | `()` | `View` |
| `View.verticalDivider` | `()` | `View` |
| `View.text` | `String` | `View` |
| `View.bindText` | `Signal` | `View` |
| `View.button` | `String, A` | `View` |
| `View.iconButton` | `String, A` | `View` |
| `View.fab` | `String, A` | `View` |
| `View.outlinedButton` | `String, A` | `View` |
| `View.textButton` | `String, A` | `View` |
| `View.actionChip` | `String, A` | `View` |
| `View.onSecondary` | `View, A` | `View` |
| `View.center` | `View` | `View` |
| `View.scroll` | `View` | `View` |
| `View.scrollH` | `View` | `View` |
| `View.expanded` | `View` | `View` |
| `View.card` | `View` | `View` |
| `View.placeholder` | `View` | `View` |
| `View.unconstrainedBox` | `View` | `View` |
| `View.slider` | `Signal` | `View` |
| `View.progress` | `Signal` | `View` |
| `View.circularProgress` | `Signal` | `View` |
| `View.focusGroup` | `View` | `View` |
| `View.editor` | `Signal` | `View` |
| `View.avatar` | `String` | `View` |
| `View.background` | `Int, View` | `View` |
| `View.padding` | `Int, View` | `View` |
| `View.textColor` | `Int, View` | `View` |
| `View.fontSize` | `Int, View` | `View` |
| `View.minSize` | `Int, Int, View` | `View` |
| `View.sized` | `Int, Int, View` | `View` |
| `View.positioned` | `Int, Int, View` | `View` |
| `View.aspectRatio` | `Int, Int, View` | `View` |
| `View.fraction` | `Int, Int, View` | `View` |
| `View.maxSize` | `Int, Int, View` | `View` |
| `View.align` | `Int, Int, View` | `View` |
| `View.textField` | `Signal, String` | `View` |
| `View.checkbox` | `Signal, String` | `View` |
| `View.switch` | `Signal, String` | `View` |
| `View.chip` | `Signal, String` | `View` |
| `View.filterChip` | `Signal, String` | `View` |
| `View.inputChip` | `Signal, String` | `View` |
| `View.checkboxListTile` | `Signal, String` | `View` |
| `View.switchListTile` | `Signal, String` | `View` |
| `View.semantics` | `String, View` | `View` |
| `View.mergeSemantics` | `String, View` | `View` |
| `View.tooltip` | `String, View` | `View` |
| `View.badge` | `Signal, View` | `View` |
| `View.visibility` | `Signal, View` | `View` |
| `View.offstage` | `Signal, View` | `View` |
| `View.overlay` | `Signal, View` | `View` |
| `View.radio` | `Signal, Int, String` | `View` |
| `View.choiceChip` | `Signal, Int, String` | `View` |
| `View.radioListTile` | `Signal, Int, String` | `View` |
| `View.showWhen` | `Signal, Int, View` | `View` |
| `View.split` | `Signal, View, View` | `View` |
| `View.segmented` | `Signal, String, String` | `View` |
| `View.expansionTile` | `Signal, String, View` | `View` |
| `View.inkWell` | `String, A, View` | `View` |
| `View.listTile` | `String, View (opt 1)` | `View` |
| `View.image` | `Int, Int, Int, String` | `View` |
| `View.icon` | `Int, Int` | `View` |
| `View.each` | `Signal[List[A]], A => View (opt 1)` | `View` |
| `View.stretch` | `View` | `View` |
| `View.clip` | `View` | `View` |
| `View.opacity` | `Int, View` | `View` |
| `View.maxLines` | `Int, View` | `View` |
| `View.ellipsis` | `View` | `View` |
| `View.gap` | `Int, View` | `View` |
| `View.border` | `Int, Int, View` | `View` |
| `View.radius` | `Int, View` | `View` |
| `View.ignorePointer` | `View` | `View` |
| `View.absorbPointer` | `View` | `View` |
| `View.excludeSemantics` | `View` | `View` |
| `View.list` | `()` | `View` |
| `Timeline.len` | `Timeline` | `Int` |
| `Timeline.signalInt` | `Timeline, Int, String` | `Int` |
| `Timeline.signalListLen` | `Timeline, Int, String` | `Int` |
| `Timeline.signalStrHas` | `Timeline, Int, String, String` | `Bool` |
| `Timeline.a11yHas` | `Timeline, Int, String` | `Int` |
| `Timeline.lastHitHas` | `Timeline, Int, String` | `Int` |
| `Timeline.driveHas` | `Timeline, Int, String` | `Int` |
| `Timeline.effectHas` | `Timeline, Int, String` | `Int` |
| `Timeline.faultKindHas` | `Timeline, Int, String` | `Int` |
| `Timeline.effectCount` | `Timeline, Int` | `Int` |
| `Timeline.fiberLive` | `Timeline, Int` | `Int` |
| `Timeline.fiberReady` | `Timeline, Int` | `Int` |
| `Timeline.fiberParked` | `Timeline, Int` | `Int` |
| `Timeline.fiberDone` | `Timeline, Int` | `Int` |
| `Timeline.faultN` | `Timeline, Int` | `Int` |
| `Timeline.checkpoint` | `Timeline, Int` | `Int` |
| `Timeline.nearestCheckpoint` | `Timeline, Int` | `Int` |
| `Timeline.exists` | `Timeline, Int => Bool` | `Int` |
| `Property.signalInt` | `String` | `Int` |
| `Property.signalStr` | `String` | `String` |
| `Property.signalListLen` | `String` | `Int` |
| `Property.signalListAt` | `String, Int` | `String` |
| `Property.sometimes` | `String` | `Unit` |
| `Property.check` | `String, Bool, A` | `A` |
| `Property.assert` | `String, Bool` | `IO[Unit]` |
| `Property.classify` | `String, Bool` | `Bool` |
| `Property.a11yHas` | `String` | `Int` |
| `Property.force` | `IO` | `A` |
| `Verdict.ok` | `()` | `Verdict` |
| `Verdict.fail` | `Int, String` | `Verdict` |
| `Verdict.and` | `Verdict, Verdict` | `Verdict` |
| `Verdict.or` | `Verdict, Verdict` | `Verdict` |
| `Verdict.alwaysHas` | `Timeline, String` | `Verdict` |
| `Verdict.afterHit` | `Timeline, String, String` | `Verdict` |
| `Verdict.every` | `Timeline, Int => Bool` | `Verdict` |
| `Verdict.any` | `Timeline, Int => Bool` | `Verdict` |

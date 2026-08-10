package scalui.compiler

def strHasChar(s: String, ch: Int, i: Int): Int =
  if (i >= Str.len(s)) 0 else if (Str.charAt(s, i) == ch) 1 else strHasChar(s, ch, i + 1)

def escapeAt(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else escapeAtCont(s, i, acc, Str.charAt(s, i))

def escapeAtCont(s: String, i: Int, acc: String, c: Int): String =
  if (c == 92) escapeAt(s, i + 1, Str.concat(acc, "\\\\")) else if (c == 34) escapeAt(s, i + 1, Str.concat(acc, "\\\"")) else if (c == 10) escapeAt(s, i + 1, Str.concat(acc, "\\n")) else if (c == 9) escapeAt(s, i + 1, Str.concat(acc, "\\t")) else escapeAt(s, i + 1, Str.concat(acc, Str.slice(s, i, i + 1)))

def escape(s: String): String =
  escapeAt(s, 0, "")

def escapeInterpAt(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else escapeInterpAtCont(s, i, acc, Str.charAt(s, i))

def escapeInterpAtCont(s: String, i: Int, acc: String, c: Int): String =
  if (c == 92) escapeInterpAt(s, i + 1, Str.concat(acc, "\\\\")) else if (c == 34) escapeInterpAt(s, i + 1, Str.concat(acc, "\\\"")) else if (c == 36) escapeInterpAt(s, i + 1, Str.concat(acc, "\\$")) else if (c == 10) escapeInterpAt(s, i + 1, Str.concat(acc, "\\n")) else if (c == 9) escapeInterpAt(s, i + 1, Str.concat(acc, "\\t")) else escapeInterpAt(s, i + 1, Str.concat(acc, Str.slice(s, i, i + 1)))

def escapeInterpLit(s: String): String =
  escapeInterpAt(s, 0, "")

def padOf(indent: Int): String =
  if (indent <= 0) "" else Str.concat("  ", padOf(indent - 1))

def formatSource(source: String): String =
  prettyProg(parseSource(source))

def prettyProg(p: List): String =
  prettyProgParts(nodeStr(p, 0), progEnums(p), progDefs(p), progMain(p))

def prettyProgParts(pkg: String, enums: List, defs: List, main: List): String =
  val head = prettyPkg(pkg)
  val mid = Str.concat(prettyEnums(enums, ""), prettyDefs(defs, ""))
  if (streq(exprTag(main), "Unit") == 1) Str.concat(head, mid) else str4(head, mid, "@main def main: IO[Unit] =\n", Str.concat(prettyExpr(main, 1), "\n"))

def prettyPkg(pkg: String): String =
  if (Str.len(pkg) == 0) "" else str3("package ", pkg, "\n\n")

def prettyEnums(enums: List, acc: String): String =
  if (List.isEmpty(enums) == 1) acc else prettyEnums(List.tail(enums), Str.concat(acc, Str.concat(prettyEnum(List.head(enums)), "\n")))

def prettyEnum(e: List): String =
  str4("enum ", nodeStr(e, 0), ":\n", prettyEnumCases(nodeExpr(e, 1), ""))

def prettyEnumCases(cases: List, acc: String): String =
  if (List.isEmpty(cases) == 1) acc else prettyEnumCases(List.tail(cases), str4(acc, "  case ", List.head(cases), "\n"))

def prettyDefs(defs: List, acc: String): String =
  if (List.isEmpty(defs) == 1) acc else prettyDefs(List.tail(defs), Str.concat(acc, Str.concat(prettyDef(List.head(defs)), "\n\n")))

def prettyDef(d: List): String =
  str5("def ", nodeStr(d, 0), str3("(", prettyParams(nodeExpr(d, 1), ""), "): "), nodeStr(d, 2), str3(" =\n", prettyExpr(nodeExpr(d, 3), 1), ""))

def prettyParams(params: List, acc: String): String =
  if (List.isEmpty(params) == 1) acc else prettyParamsCont(List.head(params), List.tail(params), acc)

def prettyParamsCont(p: String, rest: List, acc: String): String =
  val name = Str.slice(p, 6, indexOfChar(p, 58, 6))
  val ty = Str.slice(p, indexOfChar(p, 58, 6) + 1, Str.len(p))
  val piece = str3(name, ": ", ty)
  if (Str.len(acc) == 0) prettyParams(rest, piece) else prettyParams(rest, str3(acc, ", ", piece))

def prettyExpr(expr: List, indent: Int): String =
  val tag = exprTag(expr)
  val pad = padOf(indent)
  if (streq(tag, "Unit") == 1) Str.concat(pad, "()") else if (streq(tag, "IntLit") == 1) Str.concat(pad, nodeStr(expr, 0)) else if (streq(tag, "StrLit") == 1) str4(pad, "\"", escape(nodeStr(expr, 0)), "\"") else if (streq(tag, "ListLit") == 1) str4(pad, "[", prettyExprList(nodeExpr(expr, 0), ""), "]") else if (streq(tag, "Interp") == 1) str4(pad, "s\"", prettyInterpParts(nodeExpr(expr, 0), ""), "\"") else if (streq(tag, "Println") == 1) str4(pad, "IO.println(", prettyExpr(nodeExpr(expr, 0), 0), ")") else if (streq(tag, "Delay") == 1) Str.concat(pad, "IO.delay(() => ())") else if (streq(tag, "Sleep") == 1) str4(pad, "IO.sleep(", prettyExpr(nodeExpr(expr, 0), 0), ")") else if (streq(tag, "Fail") == 1) str4(pad, "IO.fail(", prettyExpr(nodeExpr(expr, 0), 0), ")") else if (streq(tag, "Pure") == 1) str4(pad, "IO.pure(", prettyExpr(nodeExpr(expr, 0), 0), ")") else if (streq(tag, "Var") == 1) Str.concat(pad, nodeStr(expr, 0)) else if (streq(tag, "Adt") == 1) str4(pad, nodeStr(expr, 0), ".", nodeStr(expr, 1)) else if (streq(tag, "Lambda") == 1) str4(pad, nodeStr(expr, 0), " => ", prettyExpr(nodeExpr(expr, 1), 0)) else if (streq(tag, "Call") == 1) str4(pad, nodeStr(expr, 0), str3("(", prettyExprList(nodeExpr(expr, 1), ""), ")"), "") else if (streq(tag, "If") == 1) prettyIf(expr, indent, pad) else if (streq(tag, "BinOp") == 1) str4(pad, prettyExpr(nodeExpr(expr, 1), 0), str3(" ", nodeStr(expr, 0), " "), prettyExpr(nodeExpr(expr, 2), 0)) else if (streq(tag, "FlatMap") == 1) prettyFlatMap(expr, indent, pad) else if (streq(tag, "Handle") == 1) prettyHandle(expr, indent, pad) else if (streq(tag, "Attempt") == 1) str3(pad, prettyExpr(nodeExpr(expr, 0), 0), ".attempt") else if (streq(tag, "IoRace") == 1) str4(pad, "IO.race(", str3(prettyExpr(nodeExpr(expr, 0), 0), ", ", prettyExpr(nodeExpr(expr, 1), 0)), ")") else if (streq(tag, "IoBoth") == 1) str4(pad, "IO.both(", str3(prettyExpr(nodeExpr(expr, 0), 0), ", ", prettyExpr(nodeExpr(expr, 1), 0)), ")") else if (streq(tag, "Let") == 1) str4(pad, str4("val ", nodeStr(expr, 0), " = ", prettyExpr(nodeExpr(expr, 1), 0)), "\n", prettyExpr(nodeExpr(expr, 2), indent)) else if (streq(tag, "For") == 1) prettyFor(expr, indent, pad) else if (streq(tag, "Match") == 1) prettyMatch(expr, indent, pad) else Str.concat(pad, "()")

def prettyFor(expr: List, indent: Int, pad: String): String =
  str4(pad, "for {\n", prettyForBinders(nodeExpr(expr, 0), indent + 1, ""), str3(pad, "} yield ", prettyExpr(nodeExpr(expr, 1), 0)))

def prettyForBinders(binders: List, indent: Int, acc: String): String =
  if (List.isEmpty(binders) == 1) acc else prettyForBinders(List.tail(binders), indent, Str.concat(acc, prettyForBinder(List.head(binders), indent)))

def prettyForBinder(b: List, indent: Int): String =
  if (streq(List.head(b), "Draw") == 1) str4(padOf(indent), nodeStr(b, 0), " <- ", str3(prettyExpr(nodeExpr(b, 1), 0), "\n", "")) else str4(padOf(indent), nodeStr(b, 0), " = ", str3(prettyExpr(nodeExpr(b, 1), 0), "\n", ""))

def prettyIf(expr: List, indent: Int, pad: String): String =
  val thenS = prettyExpr(nodeExpr(expr, 1), 0)
  val elseS = prettyExpr(nodeExpr(expr, 2), 0)
  if (strHasChar(thenS, 10, 0) == 1) prettyIfBlock(expr, indent, pad, thenS) else if (strHasChar(elseS, 10, 0) == 1) prettyIfBlock(expr, indent, pad, thenS) else str4(pad, str4("if (", prettyExpr(nodeExpr(expr, 0), 0), ") ", thenS), " else ", elseS)

def prettyIfBlock(expr: List, indent: Int, pad: String, thenS: String): String =
  str4(pad, str4("if (", prettyExpr(nodeExpr(expr, 0), 0), ") ", thenS), str3("\n", pad, "else\n"), prettyExpr(nodeExpr(expr, 2), indent + 1))

def prettyFlatMap(expr: List, indent: Int, pad: String): String =
  val left = prettyExpr(nodeExpr(expr, 0), 0)
  val right = prettyExpr(nodeExpr(expr, 2), indent + 1)
  val p = nodeStr(expr, 1)
  val bodyTag = exprTag(nodeExpr(expr, 2))
  if (streq(bodyTag, "Let") == 1) prettyFlatMapMulti(pad, left, p, right) else if (streq(bodyTag, "Match") == 1) prettyFlatMapMulti(pad, left, p, right) else if (streq(bodyTag, "FlatMap") == 1) prettyFlatMapMulti(pad, left, p, right) else if (strHasChar(right, 10, 0) == 1) prettyFlatMapMulti(pad, left, p, right) else str4(pad, left, str4(".flatMap(", p, " => ", right), ")")

def prettyFlatMapMulti(pad: String, left: String, p: String, right: String): String =
  str4(pad, left, str4(".flatMap(", p, " =>\n", right), str3("\n", pad, ")"))

def prettyHandle(expr: List, indent: Int, pad: String): String =
  val left = prettyExpr(nodeExpr(expr, 0), 0)
  val right = prettyExpr(nodeExpr(expr, 1), indent + 1)
  str4(pad, left, str3(".handleErrorWith(_ =>\n", right, "\n"), str3(pad, ")", ""))

def prettyMatch(expr: List, indent: Int, pad: String): String =
  str4(pad, prettyExpr(nodeExpr(expr, 0), 0), " match {\n", Str.concat(prettyArms(nodeExpr(expr, 1), indent + 1, ""), Str.concat(pad, "}")))

def prettyArms(arms: List, indent: Int, acc: String): String =
  if (List.isEmpty(arms) == 1) acc else prettyArms(List.tail(arms), indent, Str.concat(acc, Str.concat(prettyArm(List.head(arms), indent), "\n")))

def prettyArm(arm: List, indent: Int): String =
  str4(padOf(indent), "case ", prettyPat(nodeExpr(arm, 0)), str3(" => ", prettyExpr(nodeExpr(arm, 1), 0), ""))

def prettyPat(pat: List): String =
  if (streq(exprTag(pat), "PatWild") == 1) "_" else str3(nodeStr(pat, 0), ".", nodeStr(pat, 1))

def prettyExprList(xs: List, acc: String): String =
  if (List.isEmpty(xs) == 1) acc else prettyExprListCont(List.head(xs), List.tail(xs), acc)

def prettyExprListCont(e: List, rest: List, acc: String): String =
  val piece = prettyExpr(e, 0)
  if (Str.len(acc) == 0) prettyExprList(rest, piece) else prettyExprList(rest, str3(acc, ", ", piece))

def prettyInterpParts(parts: List, acc: String): String =
  if (List.isEmpty(parts) == 1) acc else prettyInterpParts(List.tail(parts), Str.concat(acc, prettyInterpPart(List.head(parts))))

def prettyInterpPart(part: List): String =
  if (streq(exprTag(part), "Lit") == 1) escapeInterpLit(nodeStr(part, 0)) else prettyInterpHole(nodeExpr(part, 0))

def prettyInterpHole(e: List): String =
  if (streq(exprTag(e), "Var") == 1) Str.concat("$", nodeStr(e, 0)) else str3("${", prettyExpr(e, 0), "}")


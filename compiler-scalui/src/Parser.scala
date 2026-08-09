package scalui.compiler

// Stage-1 parser: token List → nested List AST (kernel dialect).

def ok(node: List, i: Int): List =
  List.cons(node, List.cons(Str.fromInt(i), List.empty))

def okStr(s: String, i: Int): List =
  List.cons(s, List.cons(Str.fromInt(i), List.empty))

def pAst(p: List): List = List.head(p)
def pStr(p: List): String = List.head(p)
def pIdx(p: List): Int = parseInt(List.head(List.tail(p)))

def exprTag(e: List): String = List.head(e)

def exprChild(e: List, i: Int): List = List.at(List.tail(e), i)

def unitExpr(): List = List.cons("Unit", List.empty)

def tokAt(tokens: List, i: Int): String =
  if (i >= List.len(tokens)) "Eof"
  else List.at(tokens, i)

def isTok(tokens: List, i: Int, t: String): Int =
  if (streq(tokAt(tokens, i), t) == 1) 1 else 0

def isIdentTok(t: String): Int = startsWith(t, "Ident:")
def isStringTok(t: String): Int = startsWith(t, "String:")
def isIntTok(t: String): Int = startsWith(t, "Int:")

def identName(t: String): String = Str.slice(t, 6, Str.len(t))
def stringVal(t: String): String = Str.slice(t, 7, Str.len(t))
def intDigits(t: String): String = Str.slice(t, 4, Str.len(t))

def expectTok(tokens: List, i: Int, t: String): Int =
  if (isTok(tokens, i, t) == 1) i + 1 else i + 1

def parseIdent(tokens: List, i: Int): List =
  val t = tokAt(tokens, i)
  if (isIdentTok(t) == 1) okStr(identName(t), i + 1)
  else okStr("", i)

def typeToString(name: String, inner: String): String =
  if (streq(name, "IO") == 1) str3("IO[", inner, "]")
  else name

def parseType(tokens: List, i: Int): List =
  val nameP = parseIdent(tokens, i)
  parseTypeAfterName(tokens, pStr(nameP), pIdx(nameP))

def parseTypeAfterName(tokens: List, name: String, i: Int): List =
  if (streq(name, "IO") == 1) parseTypeIo(tokens, i)
  else okStr(name, i)

def parseTypeIo(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "LBracket")
  val innerP = parseType(tokens, i1)
  val i2 = expectTok(tokens, pIdx(innerP), "RBracket")
  okStr(str3("IO[", pStr(innerP), "]"), i2)

def parsePackage(tokens: List, i: Int): List =
  if (isTok(tokens, i, "Package") == 0) okStr("", i)
  else parsePackageParts(tokens, i + 1, "")

def parsePackageParts(tokens: List, i: Int, acc: String): List =
  val nameP = parseIdent(tokens, i)
  val name = pStr(nameP)
  val i1 = pIdx(nameP)
  val next = if (Str.len(acc) == 0) name else str3(acc, ".", name)
  if (isTok(tokens, i1, "Dot") == 1) parsePackageParts(tokens, i1 + 1, next)
  else okStr(next, i1)

def parseParam(tokens: List, i: Int): List =
  val nameP = parseIdent(tokens, i)
  val i1 = expectTok(tokens, pIdx(nameP), "Colon")
  val tyP = parseType(tokens, i1)
  okStr(str3("Param:", pStr(nameP), Str.concat(":", pStr(tyP))), pIdx(tyP))

def parseParams(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RParen") == 1) ok(List.reverse(acc), i)
  else parseParamsCont(tokens, i, acc)

def parseParamsCont(tokens: List, i: Int, acc: List): List =
  val p = parseParam(tokens, i)
  val acc2 = List.cons(pStr(p), acc)
  if (isTok(tokens, pIdx(p), "Comma") == 1) parseParams(tokens, pIdx(p) + 1, acc2)
  else ok(List.reverse(acc2), pIdx(p))

def parseArgs(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RParen") == 1) ok(List.reverse(acc), i)
  else parseArgsCont(tokens, i, acc)

def parseArgsCont(tokens: List, i: Int, acc: List): List =
  val e = parseExpr(tokens, i)
  val acc2 = List.cons(pAst(e), acc)
  if (isTok(tokens, pIdx(e), "Comma") == 1) parseArgs(tokens, pIdx(e) + 1, acc2)
  else ok(List.reverse(acc2), pIdx(e))

def parseBlock(tokens: List, i: Int): List =
  if (isTok(tokens, i, "Val") == 1) parseLet(tokens, i + 1)
  else parseExpr(tokens, i)

def parseLet(tokens: List, i: Int): List =
  val nameP = parseIdent(tokens, i)
  val i1 = expectTok(tokens, pIdx(nameP), "Eq")
  val valueP = parseExpr(tokens, i1)
  val bodyP = parseBlock(tokens, pIdx(valueP))
  ok(
    List.cons(
      "Let",
      List.cons(
        pStr(nameP),
        List.cons(pAst(valueP), List.cons(pAst(bodyP), List.empty))
      )
    ),
    pIdx(bodyP)
  )

def parseExpr(tokens: List, i: Int): List = parseOr(tokens, i)

def parseOr(tokens: List, i: Int): List =
  val leftP = parseAnd(tokens, i)
  parseOrRest(tokens, pAst(leftP), pIdx(leftP))

def parseOrRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "PipePipe") == 1) parseOrRestOp(tokens, left, i + 1)
  else ok(left, i)

def parseOrRestOp(tokens: List, left: List, i: Int): List =
  val rightP = parseAnd(tokens, i)
  parseOrRest(
    tokens,
    List.cons(
      "BinOp",
      List.cons("||", List.cons(left, List.cons(pAst(rightP), List.empty)))
    ),
    pIdx(rightP)
  )

def parseAnd(tokens: List, i: Int): List =
  val leftP = parseCmp(tokens, i)
  parseAndRest(tokens, pAst(leftP), pIdx(leftP))

def parseAndRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "AmpAmp") == 1) parseAndRestOp(tokens, left, i + 1)
  else ok(left, i)

def parseAndRestOp(tokens: List, left: List, i: Int): List =
  val rightP = parseCmp(tokens, i)
  parseAndRest(
    tokens,
    List.cons(
      "BinOp",
      List.cons("&&", List.cons(left, List.cons(pAst(rightP), List.empty)))
    ),
    pIdx(rightP)
  )

def cmpOp(t: String): String =
  if (streq(t, "EqEq") == 1) "=="
  else if (streq(t, "BangEq") == 1) "!="
  else if (streq(t, "Lt") == 1) "<"
  else if (streq(t, "LtEq") == 1) "<="
  else if (streq(t, "Gt") == 1) ">"
  else if (streq(t, "GtEq") == 1) ">="
  else ""

def parseCmp(tokens: List, i: Int): List =
  val leftP = parseAdd(tokens, i)
  parseCmpRest(tokens, pAst(leftP), pIdx(leftP))

def parseCmpRest(tokens: List, left: List, i: Int): List =
  val op = cmpOp(tokAt(tokens, i))
  if (Str.len(op) == 0) ok(left, i)
  else parseCmpRestOp(tokens, left, i + 1, op)

def parseCmpRestOp(tokens: List, left: List, i: Int, op: String): List =
  val rightP = parseAdd(tokens, i)
  parseCmpRest(
    tokens,
    List.cons(
      "BinOp",
      List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty)))
    ),
    pIdx(rightP)
  )

def parseAdd(tokens: List, i: Int): List =
  val leftP = parseMul(tokens, i)
  parseAddRest(tokens, pAst(leftP), pIdx(leftP))

def parseAddRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "Plus") == 1) parseAddRestOp(tokens, left, i + 1, "+")
  else if (isTok(tokens, i, "Minus") == 1) parseAddRestOp(tokens, left, i + 1, "-")
  else ok(left, i)

def parseAddRestOp(tokens: List, left: List, i: Int, op: String): List =
  val rightP = parseMul(tokens, i)
  parseAddRest(
    tokens,
    List.cons(
      "BinOp",
      List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty)))
    ),
    pIdx(rightP)
  )

def parseMul(tokens: List, i: Int): List =
  val leftP = parsePostfix(tokens, i)
  parseMulRest(tokens, pAst(leftP), pIdx(leftP))

def parseMulRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "Star") == 1) parseMulRestOp(tokens, left, i + 1, "*")
  else if (isTok(tokens, i, "Slash") == 1) parseMulRestOp(tokens, left, i + 1, "/")
  else if (isTok(tokens, i, "Percent") == 1) parseMulRestOp(tokens, left, i + 1, "%")
  else ok(left, i)

def parseMulRestOp(tokens: List, left: List, i: Int, op: String): List =
  val rightP = parsePostfix(tokens, i)
  parseMulRest(
    tokens,
    List.cons(
      "BinOp",
      List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty)))
    ),
    pIdx(rightP)
  )

def parsePostfix(tokens: List, i: Int): List =
  val primP = parsePrimary(tokens, i)
  parsePostfixRest(tokens, pAst(primP), pIdx(primP))

def parsePostfixRest(tokens: List, expr: List, i: Int): List =
  if (isTok(tokens, i, "Dot") == 1) parsePostfixDot(tokens, expr, i + 1)
  else ok(expr, i)

def parsePostfixDot(tokens: List, expr: List, i: Int): List =
  val methodP = parseIdent(tokens, i)
  val method = pStr(methodP)
  val i1 = pIdx(methodP)
  if (streq(method, "flatMap") == 1) parseFlatMap(tokens, expr, i1)
  else if (streq(method, "handleErrorWith") == 1) parseHandle(tokens, expr, i1)
  else if (streq(method, "attempt") == 1) parseAttempt(tokens, expr, i1)
  else ok(expr, i1)

def parseLambda(tokens: List, i: Int): List =
  val t = tokAt(tokens, i)
  if (streq(t, "Underscore") == 1) parseLambdaBody(tokens, i + 1, "_")
  else if (isIdentTok(t) == 1) parseLambdaBody(tokens, i + 1, identName(t))
  else if (streq(t, "LParen") == 1) parseLambdaUnit(tokens, i + 1)
  else ok(unitExpr(), i)

def parseLambdaUnit(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "RParen")
  parseLambdaBody(tokens, i1, "_")

def parseLambdaBody(tokens: List, i: Int, param: String): List =
  val i1 = expectTok(tokens, i, "Arrow")
  val bodyP = parseBlock(tokens, i1)
  ok(List.cons(param, List.cons(pAst(bodyP), List.empty)), pIdx(bodyP))

def parseFlatMap(tokens: List, expr: List, i: Int): List =
  val i1 = expectTok(tokens, i, "LParen")
  val lamP = parseLambda(tokens, i1)
  val i2 = expectTok(tokens, pIdx(lamP), "RParen")
  val param = List.head(pAst(lamP))
  val body = List.head(List.tail(pAst(lamP)))
  parsePostfixRest(
    tokens,
    List.cons(
      "FlatMap",
      List.cons(expr, List.cons(param, List.cons(body, List.empty)))
    ),
    i2
  )

def parseHandle(tokens: List, expr: List, i: Int): List =
  val i1 = expectTok(tokens, i, "LParen")
  val lamP = parseLambda(tokens, i1)
  val i2 = expectTok(tokens, pIdx(lamP), "RParen")
  val body = List.head(List.tail(pAst(lamP)))
  parsePostfixRest(
    tokens,
    List.cons("Handle", List.cons(expr, List.cons(body, List.empty))),
    i2
  )

def parseAttempt(tokens: List, expr: List, i: Int): List =
  if (isTok(tokens, i, "LParen") == 1) parseAttemptParen(tokens, expr, i + 1)
  else parsePostfixRest(tokens, List.cons("Attempt", List.cons(expr, List.empty)), i)

def parseAttemptParen(tokens: List, expr: List, i: Int): List =
  val i1 = expectTok(tokens, i, "RParen")
  parsePostfixRest(tokens, List.cons("Attempt", List.cons(expr, List.empty)), i1)

def parsePrimary(tokens: List, i: Int): List =
  val t = tokAt(tokens, i)
  if (streq(t, "If") == 1) parseIf(tokens, i + 1)
  else if (streq(t, "LParen") == 1) parseParen(tokens, i + 1)
  else if (isIntTok(t) == 1)
    ok(List.cons("IntLit", List.cons(intDigits(t), List.empty)), i + 1)
  else if (isStringTok(t) == 1)
    ok(List.cons("StrLit", List.cons(stringVal(t), List.empty)), i + 1)
  else if (streq(t, "Minus") == 1) parseNegInt(tokens, i + 1)
  else if (streq(t, "Underscore") == 1)
    if (isTok(tokens, i + 1, "Arrow") == 1) parseLambdaExpr(tokens, i + 1, "_")
    else ok(unitExpr(), i + 1)
  else if (isIdentTok(t) == 1) parsePrimaryIdent(tokens, i, t)
  else ok(unitExpr(), i)

// The lexer emits `_` as an identifier, so `_ => …` also lands here. An Ident
// (or `_`) immediately followed by `=>` is a first-class lambda literal;
// otherwise it is a plain variable/call.
def parsePrimaryIdent(tokens: List, i: Int, t: String): List =
  if (isTok(tokens, i + 1, "Arrow") == 1) parseLambdaExpr(tokens, i + 1, identName(t))
  else parseIdentExpr(tokens, i, identName(t))

// First-class lambda literal `_ => expr` / `name => expr` (single param, tap
// callbacks). `param` is the already-consumed `_` or identifier text; `i`
// points at the `=>` token.
def parseLambdaExpr(tokens: List, i: Int, param: String): List =
  val i1 = expectTok(tokens, i, "Arrow")
  val bodyP = parseBlock(tokens, i1)
  ok(
    List.cons("Lambda", List.cons(param, List.cons(pAst(bodyP), List.empty))),
    pIdx(bodyP)
  )

def parseNegInt(tokens: List, i: Int): List =
  val t = tokAt(tokens, i)
  if (isIntTok(t) == 1)
    ok(List.cons("IntLit", List.cons(Str.concat("-", intDigits(t)), List.empty)), i + 1)
  else ok(unitExpr(), i)

def parseParen(tokens: List, i: Int): List =
  if (isTok(tokens, i, "RParen") == 1) ok(unitExpr(), i + 1)
  else parseParenExpr(tokens, i)

def parseParenExpr(tokens: List, i: Int): List =
  val e = parseExpr(tokens, i)
  val i1 = expectTok(tokens, pIdx(e), "RParen")
  ok(pAst(e), i1)

def parseIf(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "LParen")
  val condP = parseExpr(tokens, i1)
  val i2 = expectTok(tokens, pIdx(condP), "RParen")
  val thenP = parseExpr(tokens, i2)
  val i3 = expectTok(tokens, pIdx(thenP), "Else")
  val elseP = parseExpr(tokens, i3)
  ok(
    List.cons(
      "If",
      List.cons(
        pAst(condP),
        List.cons(pAst(thenP), List.cons(pAst(elseP), List.empty))
      )
    ),
    pIdx(elseP)
  )

def parseIdentExpr(tokens: List, i: Int, name: String): List =
  if (streq(name, "IO") == 1) parseIo(tokens, i + 1)
  else if (streq(name, "Str") == 1) parseModuleCall(tokens, i + 1, "Str")
  else if (streq(name, "List") == 1) parseModuleCall(tokens, i + 1, "List")
  else if (streq(name, "Fs") == 1) parseModuleCall(tokens, i + 1, "Fs")
  else if (streq(name, "Sys") == 1) parseModuleCall(tokens, i + 1, "Sys")
  else if (streq(name, "Clock") == 1) parseModuleCall(tokens, i + 1, "Clock")
  else if (streq(name, "Random") == 1) parseModuleCall(tokens, i + 1, "Random")
  else if (streq(name, "Net") == 1) parseModuleCall(tokens, i + 1, "Net")
  else if (streq(name, "Impurity") == 1) parseModuleCall(tokens, i + 1, "Impurity")
  else if (streq(name, "Signal") == 1) parseModuleCall(tokens, i + 1, "Signal")
  else if (streq(name, "View") == 1) parseModuleCall(tokens, i + 1, "View")
  else if (streq(name, "Todo") == 1) parseModuleCall(tokens, i + 1, "Todo")
  else if (streq(name, "Ui") == 1) parseModuleCall(tokens, i + 1, "Ui")
  else if (streq(name, "Effects") == 1) parseModuleCall(tokens, i + 1, "Effects")
  else if (streq(name, "Lexer") == 1) parseModuleCall(tokens, i + 1, "Lexer")
  else parseCallOrVar(tokens, i + 1, name)

def parseCallOrVar(tokens: List, i: Int, name: String): List =
  if (isTok(tokens, i, "LParen") == 1) parseCallArgs(tokens, i + 1, name)
  else if (isTok(tokens, i, "Dot") == 1) parseAdt(tokens, i + 1, name)
  else if (isTok(tokens, i, "Arrow") == 1) parseLambdaExpr(tokens, i, name)
  else ok(List.cons("Var", List.cons(name, List.empty)), i)

def parseAdt(tokens: List, i: Int, enumName: String): List =
  val caseP = parseIdent(tokens, i)
  ok(
    List.cons(
      "Adt",
      List.cons(enumName, List.cons(pStr(caseP), List.empty))
    ),
    pIdx(caseP)
  )

def parseCallArgs(tokens: List, i: Int, callee: String): List =
  val argsP = parseArgs(tokens, i, List.empty)
  val i1 = expectTok(tokens, pIdx(argsP), "RParen")
  ok(List.cons("Call", List.cons(callee, List.cons(pAst(argsP), List.empty))), i1)

def parseModuleCall(tokens: List, i: Int, mod: String): List =
  val i1 = expectTok(tokens, i, "Dot")
  val methodP = parseIdent(tokens, i1)
  val callee = str3(mod, ".", pStr(methodP))
  val i2 = pIdx(methodP)
  if (isTok(tokens, i2, "LParen") == 1) parseModuleCallArgs(tokens, i2 + 1, mod, callee)
  else ok(List.cons("Call", List.cons(callee, List.cons(List.empty, List.empty))), i2)

def parseModuleCallArgs(tokens: List, i: Int, mod: String, callee: String): List =
  if (streq(mod, "IO") == 1) parseIoArgs(tokens, i, callee)
  else parseCallArgs(tokens, i, callee)

def parseIo(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "Dot")
  val methodP = parseIdent(tokens, i1)
  val method = pStr(methodP)
  val i2 = pIdx(methodP)
  if (streq(method, "delay") == 1) parseIoDelay(tokens, i2)
  else if (isTok(tokens, i2, "LParen") == 1) parseIoArgs(tokens, i2 + 1, Str.concat("IO.", method))
  else ok(List.cons("Call", List.cons(Str.concat("IO.", method), List.cons(List.empty, List.empty))), i2)

def parseIoDelay(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "LParen")
  val i2 = expectTok(tokens, i1, "LParen")
  val i3 = expectTok(tokens, i2, "RParen")
  val i4 = expectTok(tokens, i3, "Arrow")
  val i5 = expectTok(tokens, i4, "LParen")
  val i6 = expectTok(tokens, i5, "RParen")
  val i7 = expectTok(tokens, i6, "RParen")
  ok(List.cons("Delay", List.empty), i7)

def parseIoArgs(tokens: List, i: Int, callee: String): List =
  val argsP = parseArgs(tokens, i, List.empty)
  val i1 = expectTok(tokens, pIdx(argsP), "RParen")
  val args = pAst(argsP)
  if (streq(callee, "IO.println") == 1)
    ok(List.cons("Println", List.cons(List.head(args), List.empty)), i1)
  else if (streq(callee, "IO.fail") == 1)
    ok(List.cons("Fail", List.cons(List.head(args), List.empty)), i1)
  else if (streq(callee, "IO.pure") == 1)
    ok(List.cons("Pure", List.cons(List.head(args), List.empty)), i1)
  else if (streq(callee, "IO.sleep") == 1)
    ok(List.cons("Sleep", List.cons(List.head(args), List.empty)), i1)
  else if (streq(callee, "IO.race") == 1)
    ok(
      List.cons(
        "IoRace",
        List.cons(List.head(args), List.cons(List.head(List.tail(args)), List.empty))
      ),
      i1
    )
  else if (streq(callee, "IO.both") == 1)
    ok(
      List.cons(
        "IoBoth",
        List.cons(List.head(args), List.cons(List.head(List.tail(args)), List.empty))
      ),
      i1
    )
  else ok(List.cons("Call", List.cons(callee, List.cons(args, List.empty))), i1)

def parseDef(tokens: List, i: Int): List =
  val nameP = parseIdent(tokens, i)
  val i1 = expectTok(tokens, pIdx(nameP), "LParen")
  val paramsP = parseParams(tokens, i1, List.empty)
  val i2 = expectTok(tokens, pIdx(paramsP), "RParen")
  val i3 = expectTok(tokens, i2, "Colon")
  val retP = parseType(tokens, i3)
  val i4 = expectTok(tokens, pIdx(retP), "Eq")
  val bodyP = parseBlock(tokens, i4)
  ok(
    List.cons(
      "Def",
      List.cons(
        pStr(nameP),
        List.cons(
          pAst(paramsP),
          List.cons(pStr(retP), List.cons(pAst(bodyP), List.empty))
        )
      )
    ),
    pIdx(bodyP)
  )

def parseMain(tokens: List, i: Int): List =
  val i1 = expectTok(tokens, i, "Def")
  val nameP = parseIdent(tokens, i1)
  val i2 = expectTok(tokens, pIdx(nameP), "Colon")
  val tyP = parseType(tokens, i2)
  val i3 = expectTok(tokens, pIdx(tyP), "Eq")
  parseBlock(tokens, i3)

def parseTop(tokens: List, i: Int, pkg: String, defs: List, main: List): List =
  val t = tokAt(tokens, i)
  if (streq(t, "Eof") == 1)
    List.cons(
      "Prog",
      List.cons(pkg, List.cons(List.reverse(defs), List.cons(main, List.empty)))
    )
  else if (streq(t, "Def") == 1) parseTopDef(tokens, i + 1, pkg, defs, main)
  else if (streq(t, "AtMain") == 1) parseTopMain(tokens, i + 1, pkg, defs)
  else parseTop(tokens, i + 1, pkg, defs, main)

def parseTopDef(tokens: List, i: Int, pkg: String, defs: List, main: List): List =
  val d = parseDef(tokens, i)
  parseTop(tokens, pIdx(d), pkg, List.cons(pAst(d), defs), main)

def parseTopMain(tokens: List, i: Int, pkg: String, defs: List): List =
  val m = parseMain(tokens, i)
  parseTop(tokens, pIdx(m), pkg, defs, pAst(m))

def parseProgram(tokens: List): List =
  val pkgP = parsePackage(tokens, 0)
  parseTop(tokens, pIdx(pkgP), pStr(pkgP), List.empty, unitExpr())

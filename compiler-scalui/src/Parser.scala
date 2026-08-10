package scalui.compiler

def ok(node: List, i: Int): List =
  List.cons(node, List.cons(Str.fromInt(i), List.empty()))

def okStr(s: String, i: Int): List =
  List.cons(s, List.cons(Str.fromInt(i), List.empty()))

def pAst(p: List): List =
  List.head(p)

def pStr(p: List): String =
  List.head(p)

def pIdx(p: List): Int =
  parseInt(List.head(List.tail(p)))

def exprTag(e: List): String =
  List.head(e)

def exprChild(e: List, i: Int): List =
  List.at(List.tail(e), i)

def unitExpr(): List =
  List.cons("Unit", List.empty())

def tokAt(tokens: List, i: Int): String =
  if (i >= List.len(tokens)) "Eof" else List.at(tokens, i)

def isTok(tokens: List, i: Int, t: String): Int =
  if (streq(tokAt(tokens, i), t) == 1) 1 else 0

def isIdentTok(t: String): Int =
  startsWith(t, "Ident:")

def isStringTok(t: String): Int =
  startsWith(t, "String:")

def isIntTok(t: String): Int =
  startsWith(t, "Int:")

def isInterpLitTok(t: String): Int =
  startsWith(t, "InterpLit:")

def isInterpIdTok(t: String): Int =
  startsWith(t, "InterpId:")

def isInterpBraceTok(t: String): Int =
  startsWith(t, "InterpBrace:")

def identName(t: String): String =
  Str.slice(t, 6, Str.len(t))

def stringVal(t: String): String =
  Str.slice(t, 7, Str.len(t))

def intDigits(t: String): String =
  Str.slice(t, 4, Str.len(t))

def interpLitVal(t: String): String =
  Str.slice(t, 10, Str.len(t))

def interpIdVal(t: String): String =
  Str.slice(t, 9, Str.len(t))

def interpBraceVal(t: String): String =
  Str.slice(t, 12, Str.len(t))

def expectTok(tokens: List, i: Int, t: String): Int =
  if (isTok(tokens, i, t) == 1) i + 1 else i + 1

def parseIdent(tokens: List, i: Int): List =
  for {
    t = tokAt(tokens, i)
  } yield if (isIdentTok(t) == 1) okStr(identName(t), i + 1) else okStr("", i)

def typeToString(name: String, inner: String): String =
  if (streq(name, "IO") == 1) str3("IO[", inner, "]") else name

def parseType(tokens: List, i: Int): List =
  for {
    nameP = parseIdent(tokens, i)
  } yield parseTypeAfterName(tokens, pStr(nameP), pIdx(nameP))

def parseTypeAfterName(tokens: List, name: String, i: Int): List =
  if (streq(name, "IO") == 1) parseTypeIo(tokens, i) else okStr(name, i)

def parseTypeIo(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LBracket")
    innerP = parseType(tokens, i1)
    i2 = expectTok(tokens, pIdx(innerP), "RBracket")
  } yield okStr(str3("IO[", pStr(innerP), "]"), i2)

def parsePackage(tokens: List, i: Int): List =
  if (isTok(tokens, i, "Package") == 0) okStr("", i) else parsePackageParts(tokens, i + 1, "")

def parsePackageParts(tokens: List, i: Int, acc: String): List =
  for {
    nameP = parseIdent(tokens, i)
    name = pStr(nameP)
    i1 = pIdx(nameP)
    next = if (Str.len(acc) == 0) name else str3(acc, ".", name)
  } yield if (isTok(tokens, i1, "Dot") == 1) parsePackageParts(tokens, i1 + 1, next) else okStr(next, i1)

def parseParam(tokens: List, i: Int): List =
  for {
    nameP = parseIdent(tokens, i)
    i1 = expectTok(tokens, pIdx(nameP), "Colon")
    tyP = parseType(tokens, i1)
  } yield okStr(str3("Param:", pStr(nameP), Str.concat(":", pStr(tyP))), pIdx(tyP))

def parseParams(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RParen") == 1) ok(List.reverse(acc), i) else parseParamsCont(tokens, i, acc)

def parseParamsCont(tokens: List, i: Int, acc: List): List =
  for {
    p = parseParam(tokens, i)
    acc2 = List.cons(pStr(p), acc)
  } yield if (isTok(tokens, pIdx(p), "Comma") == 1) parseParams(tokens, pIdx(p) + 1, acc2) else ok(List.reverse(acc2), pIdx(p))

def parseArgs(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RParen") == 1) ok(List.reverse(acc), i) else parseArgsCont(tokens, i, acc)

def parseArgsCont(tokens: List, i: Int, acc: List): List =
  for {
    e = parseExpr(tokens, i)
    acc2 = List.cons(pAst(e), acc)
  } yield if (isTok(tokens, pIdx(e), "Comma") == 1) parseArgs(tokens, pIdx(e) + 1, acc2) else ok(List.reverse(acc2), pIdx(e))

def parseListLit(tokens: List, i: Int): List =
  parseListLitElems(tokens, i, List.empty())

def parseListLitElems(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RBracket") == 1) ok(List.cons("ListLit", List.cons(List.reverse(acc), List.empty())), i + 1) else parseListLitElemsCont(tokens, i, acc)

def parseListLitElemsCont(tokens: List, i: Int, acc: List): List =
  for {
    e = parseExpr(tokens, i)
    acc2 = List.cons(pAst(e), acc)
  } yield if (isTok(tokens, pIdx(e), "Comma") == 1) parseListLitElems(tokens, pIdx(e) + 1, acc2) else parseListLitElemsEnd(tokens, pIdx(e), acc2)

def parseListLitElemsEnd(tokens: List, i: Int, acc: List): List =
  for {
    i1 = expectTok(tokens, i, "RBracket")
  } yield ok(List.cons("ListLit", List.cons(List.reverse(acc), List.empty())), i1)

def parseBlock(tokens: List, i: Int): List =
  parseExpr(tokens, i)

def parseExpr(tokens: List, i: Int): List =
  parseOr(tokens, i)

def parseOr(tokens: List, i: Int): List =
  for {
    leftP = parseAnd(tokens, i)
  } yield parseOrRest(tokens, pAst(leftP), pIdx(leftP))

def parseOrRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "PipePipe") == 1) parseOrRestOp(tokens, left, i + 1) else ok(left, i)

def parseOrRestOp(tokens: List, left: List, i: Int): List =
  for {
    rightP = parseAnd(tokens, i)
  } yield parseOrRest(tokens, List.cons("BinOp", List.cons("||", List.cons(left, List.cons(pAst(rightP), List.empty())))), pIdx(rightP))

def parseAnd(tokens: List, i: Int): List =
  for {
    leftP = parseCmp(tokens, i)
  } yield parseAndRest(tokens, pAst(leftP), pIdx(leftP))

def parseAndRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "AmpAmp") == 1) parseAndRestOp(tokens, left, i + 1) else ok(left, i)

def parseAndRestOp(tokens: List, left: List, i: Int): List =
  for {
    rightP = parseCmp(tokens, i)
  } yield parseAndRest(tokens, List.cons("BinOp", List.cons("&&", List.cons(left, List.cons(pAst(rightP), List.empty())))), pIdx(rightP))

def cmpOp(t: String): String =
  if (streq(t, "EqEq") == 1) "==" else if (streq(t, "BangEq") == 1) "!=" else if (streq(t, "Lt") == 1) "<" else if (streq(t, "LtEq") == 1) "<=" else if (streq(t, "Gt") == 1) ">" else if (streq(t, "GtEq") == 1) ">=" else ""

def parseCmp(tokens: List, i: Int): List =
  for {
    leftP = parseAdd(tokens, i)
  } yield parseCmpRest(tokens, pAst(leftP), pIdx(leftP))

def parseCmpRest(tokens: List, left: List, i: Int): List =
  for {
    op = cmpOp(tokAt(tokens, i))
  } yield if (Str.len(op) == 0) ok(left, i) else parseCmpRestOp(tokens, left, i + 1, op)

def parseCmpRestOp(tokens: List, left: List, i: Int, op: String): List =
  for {
    rightP = parseAdd(tokens, i)
  } yield parseCmpRest(tokens, List.cons("BinOp", List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty())))), pIdx(rightP))

def parseAdd(tokens: List, i: Int): List =
  for {
    leftP = parseMul(tokens, i)
  } yield parseAddRest(tokens, pAst(leftP), pIdx(leftP))

def parseAddRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "Plus") == 1) parseAddRestOp(tokens, left, i + 1, "+") else if (isTok(tokens, i, "Minus") == 1) parseAddRestOp(tokens, left, i + 1, "-") else ok(left, i)

def parseAddRestOp(tokens: List, left: List, i: Int, op: String): List =
  for {
    rightP = parseMul(tokens, i)
  } yield parseAddRest(tokens, List.cons("BinOp", List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty())))), pIdx(rightP))

def parseMul(tokens: List, i: Int): List =
  for {
    leftP = parsePostfix(tokens, i)
  } yield parseMulRest(tokens, pAst(leftP), pIdx(leftP))

def parseMulRest(tokens: List, left: List, i: Int): List =
  if (isTok(tokens, i, "Star") == 1) parseMulRestOp(tokens, left, i + 1, "*") else if (isTok(tokens, i, "Slash") == 1) parseMulRestOp(tokens, left, i + 1, "/") else if (isTok(tokens, i, "Percent") == 1) parseMulRestOp(tokens, left, i + 1, "%") else ok(left, i)

def parseMulRestOp(tokens: List, left: List, i: Int, op: String): List =
  for {
    rightP = parsePostfix(tokens, i)
  } yield parseMulRest(tokens, List.cons("BinOp", List.cons(op, List.cons(left, List.cons(pAst(rightP), List.empty())))), pIdx(rightP))

def parsePostfix(tokens: List, i: Int): List =
  for {
    primP = parsePrimary(tokens, i)
  } yield parsePostfixRest(tokens, pAst(primP), pIdx(primP))

def parsePostfixRest(tokens: List, expr: List, i: Int): List =
  if (isTok(tokens, i, "Dot") == 1) parsePostfixDot(tokens, expr, i + 1) else if (isTok(tokens, i, "Match") == 1) parseMatch(tokens, expr, i + 1) else ok(expr, i)

def parseMatch(tokens: List, scrut: List, i: Int): List =
  for {
    i1 = if (isTok(tokens, i, "LBrace") == 1) i + 1 else i
    armsP = parseMatchArms(tokens, i1, List.empty())
    i2 = if (isTok(tokens, pIdx(armsP), "RBrace") == 1) pIdx(armsP) + 1 else pIdx(armsP)
  } yield parsePostfixRest(tokens, List.cons("Match", List.cons(scrut, List.cons(pAst(armsP), List.empty()))), i2)

def parseMatchArms(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "Case") == 0) ok(List.reverse(acc), i) else parseMatchArm(tokens, i + 1, acc)

def parseMatchArm(tokens: List, i: Int, acc: List): List =
  for {
    patP = parsePattern(tokens, i)
    i1 = expectTok(tokens, pIdx(patP), "Arrow")
    bodyP = parseExpr(tokens, i1)
  } yield parseMatchArms(tokens, pIdx(bodyP), List.cons(List.cons("Arm", List.cons(pAst(patP), List.cons(pAst(bodyP), List.empty()))), acc))

def parsePattern(tokens: List, i: Int): List =
  if (isTok(tokens, i, "Underscore") == 1) ok(List.cons("PatWild", List.empty()), i + 1) else parsePatternAdt(tokens, i)

def parsePatternAdt(tokens: List, i: Int): List =
  for {
    enumP = parseIdent(tokens, i)
    i1 = expectTok(tokens, pIdx(enumP), "Dot")
    caseP = parseIdent(tokens, i1)
  } yield ok(List.cons("PatAdt", List.cons(pStr(enumP), List.cons(pStr(caseP), List.empty()))), pIdx(caseP))

def parsePostfixDot(tokens: List, expr: List, i: Int): List =
  for {
    methodP = parseIdent(tokens, i)
    method = pStr(methodP)
    i1 = pIdx(methodP)
  } yield if (streq(method, "flatMap") == 1) parseFlatMap(tokens, expr, i1) else if (streq(method, "handleErrorWith") == 1) parseHandle(tokens, expr, i1) else if (streq(method, "attempt") == 1) parseAttempt(tokens, expr, i1) else ok(expr, i1)

def parseLambda(tokens: List, i: Int): List =
  for {
    t = tokAt(tokens, i)
  } yield if (streq(t, "Underscore") == 1) parseLambdaBody(tokens, i + 1, "_") else if (isIdentTok(t) == 1) parseLambdaBody(tokens, i + 1, identName(t)) else if (streq(t, "LParen") == 1) parseLambdaUnit(tokens, i + 1) else ok(unitExpr(), i)

def parseLambdaUnit(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "RParen")
  } yield parseLambdaBody(tokens, i1, "_")

def parseLambdaBody(tokens: List, i: Int, param: String): List =
  for {
    i1 = expectTok(tokens, i, "Arrow")
    bodyP = parseBlock(tokens, i1)
  } yield ok(List.cons(param, List.cons(pAst(bodyP), List.empty())), pIdx(bodyP))

def parseFlatMap(tokens: List, expr: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LParen")
    lamP = parseLambda(tokens, i1)
    i2 = expectTok(tokens, pIdx(lamP), "RParen")
    param = List.head(pAst(lamP))
    body = List.head(List.tail(pAst(lamP)))
  } yield parsePostfixRest(tokens, List.cons("FlatMap", List.cons(expr, List.cons(param, List.cons(body, List.empty())))), i2)

def parseHandle(tokens: List, expr: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LParen")
    lamP = parseLambda(tokens, i1)
    i2 = expectTok(tokens, pIdx(lamP), "RParen")
    body = List.head(List.tail(pAst(lamP)))
  } yield parsePostfixRest(tokens, List.cons("Handle", List.cons(expr, List.cons(body, List.empty()))), i2)

def parseAttempt(tokens: List, expr: List, i: Int): List =
  if (isTok(tokens, i, "LParen") == 1) parseAttemptParen(tokens, expr, i + 1) else parsePostfixRest(tokens, List.cons("Attempt", List.cons(expr, List.empty())), i)

def parseAttemptParen(tokens: List, expr: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "RParen")
  } yield parsePostfixRest(tokens, List.cons("Attempt", List.cons(expr, List.empty())), i1)

def parsePrimary(tokens: List, i: Int): List =
  for {
    t = tokAt(tokens, i)
  } yield if (streq(t, "For") == 1) parseFor(tokens, i + 1) else if (streq(t, "If") == 1) parseIf(tokens, i + 1) else if (streq(t, "LParen") == 1) parseParen(tokens, i + 1) else if (isIntTok(t) == 1) ok(List.cons("IntLit", List.cons(intDigits(t), List.empty())), i + 1) else if (isStringTok(t) == 1) ok(List.cons("StrLit", List.cons(stringVal(t), List.empty())), i + 1) else if (streq(t, "InterpStart") == 1) parseInterp(tokens, i + 1, List.empty()) else if (streq(t, "LBracket") == 1) parseListLit(tokens, i + 1) else if (streq(t, "Minus") == 1) parseNegInt(tokens, i + 1) else if (streq(t, "Underscore") == 1) if (isTok(tokens, i + 1, "Arrow") == 1) parseLambdaExpr(tokens, i + 1, "_") else ok(unitExpr(), i + 1) else if (isIdentTok(t) == 1) parsePrimaryIdent(tokens, i, t) else ok(unitExpr(), i)

def parseBinderName(tokens: List, i: Int): List =
  if (isTok(tokens, i, "Underscore") == 1) okStr("_", i + 1) else parseIdent(tokens, i)

def parseFor(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LBrace")
    bindersP = parseForBinders(tokens, i1, List.empty())
    i2 = expectTok(tokens, pIdx(bindersP), "RBrace")
    i3 = expectTok(tokens, i2, "Yield")
    bodyP = parseExpr(tokens, i3)
  } yield ok(List.cons("For", List.cons(pAst(bindersP), List.cons(pAst(bodyP), List.empty()))), pIdx(bodyP))

def parseForBinders(tokens: List, i: Int, acc: List): List =
  if (isTok(tokens, i, "RBrace") == 1) ok(List.reverse(acc), i) else if (isTok(tokens, i, "Yield") == 1) ok(List.reverse(acc), i) else parseForBinder(tokens, i, acc)

def parseForBinder(tokens: List, i: Int, acc: List): List =
  for {
    nameP = parseBinderName(tokens, i)
  } yield if (isTok(tokens, pIdx(nameP), "Eq") == 1) parseForBinderEq(tokens, pIdx(nameP) + 1, pStr(nameP), acc) else if (isTok(tokens, pIdx(nameP), "LeftArrow") == 1) parseForBinderDraw(tokens, pIdx(nameP) + 1, pStr(nameP), acc) else ok(List.reverse(acc), pIdx(nameP))

def parseForBinderEq(tokens: List, i: Int, name: String, acc: List): List =
  for {
    valueP = parseExpr(tokens, i)
  } yield parseForBinders(tokens, pIdx(valueP), List.cons(List.cons("Eq", List.cons(name, List.cons(pAst(valueP), List.empty()))), acc))

def parseForBinderDraw(tokens: List, i: Int, name: String, acc: List): List =
  for {
    valueP = parseExpr(tokens, i)
  } yield parseForBinders(tokens, pIdx(valueP), List.cons(List.cons("Draw", List.cons(name, List.cons(pAst(valueP), List.empty()))), acc))

def parseInterp(tokens: List, i: Int, acc: List): List =
  for {
    t = tokAt(tokens, i)
  } yield if (streq(t, "InterpEnd") == 1) ok(List.cons("Interp", List.cons(List.reverse(acc), List.empty())), i + 1) else if (isInterpLitTok(t) == 1) parseInterp(tokens, i + 1, List.cons(List.cons("Lit", List.cons(interpLitVal(t), List.empty())), acc)) else if (isInterpIdTok(t) == 1) parseInterp(tokens, i + 1, List.cons(List.cons("Hole", List.cons(List.cons("Var", List.cons(interpIdVal(t), List.empty())), List.empty())), acc)) else if (isInterpBraceTok(t) == 1) parseInterpBrace(tokens, i, acc, interpBraceVal(t)) else ok(List.cons("Interp", List.cons(List.reverse(acc), List.empty())), i)

def parseInterpBrace(tokens: List, i: Int, acc: List, body: String): List =
  for {
    nested = parseExpr(lex(body), 0)
  } yield parseInterp(tokens, i + 1, List.cons(List.cons("Hole", List.cons(pAst(nested), List.empty())), acc))

def parsePrimaryIdent(tokens: List, i: Int, t: String): List =
  if (isTok(tokens, i + 1, "Arrow") == 1) parseLambdaExpr(tokens, i + 1, identName(t)) else parseIdentExpr(tokens, i, identName(t))

def parseLambdaExpr(tokens: List, i: Int, param: String): List =
  for {
    i1 = expectTok(tokens, i, "Arrow")
    bodyP = parseBlock(tokens, i1)
  } yield ok(List.cons("Lambda", List.cons(param, List.cons(pAst(bodyP), List.empty()))), pIdx(bodyP))

def parseNegInt(tokens: List, i: Int): List =
  for {
    t = tokAt(tokens, i)
  } yield if (isIntTok(t) == 1) ok(List.cons("IntLit", List.cons(Str.concat("-", intDigits(t)), List.empty())), i + 1) else ok(unitExpr(), i)

def parseParen(tokens: List, i: Int): List =
  if (isTok(tokens, i, "RParen") == 1) ok(unitExpr(), i + 1) else parseParenExpr(tokens, i)

def parseParenExpr(tokens: List, i: Int): List =
  for {
    e = parseExpr(tokens, i)
    i1 = expectTok(tokens, pIdx(e), "RParen")
  } yield ok(pAst(e), i1)

def parseIf(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LParen")
    condP = parseExpr(tokens, i1)
    i2 = expectTok(tokens, pIdx(condP), "RParen")
    thenP = parseIfBranch(tokens, i2)
    i3 = expectTok(tokens, pIdx(thenP), "Else")
    elseP = parseIfBranch(tokens, i3)
  } yield ok(List.cons("If", List.cons(pAst(condP), List.cons(pAst(thenP), List.cons(pAst(elseP), List.empty())))), pIdx(elseP))

def parseIfBranch(tokens: List, i: Int): List =
  parseExpr(tokens, i)

def parseIdentExpr(tokens: List, i: Int, name: String): List =
  if (streq(name, "IO") == 1) parseIo(tokens, i + 1) else if (streq(name, "Str") == 1) parseModuleCall(tokens, i + 1, "Str") else if (streq(name, "List") == 1) parseModuleCall(tokens, i + 1, "List") else if (streq(name, "Fs") == 1) parseModuleCall(tokens, i + 1, "Fs") else if (streq(name, "Sys") == 1) parseModuleCall(tokens, i + 1, "Sys") else if (streq(name, "Clock") == 1) parseModuleCall(tokens, i + 1, "Clock") else if (streq(name, "Random") == 1) parseModuleCall(tokens, i + 1, "Random") else if (streq(name, "Net") == 1) parseModuleCall(tokens, i + 1, "Net") else if (streq(name, "Impurity") == 1) parseModuleCall(tokens, i + 1, "Impurity") else if (streq(name, "Signal") == 1) parseModuleCall(tokens, i + 1, "Signal") else if (streq(name, "Theme") == 1) parseModuleCall(tokens, i + 1, "Theme") else if (streq(name, "View") == 1) parseModuleCall(tokens, i + 1, "View") else if (streq(name, "Ui") == 1) parseModuleCall(tokens, i + 1, "Ui") else if (streq(name, "Effects") == 1) parseModuleCall(tokens, i + 1, "Effects") else if (streq(name, "Lexer") == 1) parseModuleCall(tokens, i + 1, "Lexer") else parseCallOrVar(tokens, i + 1, name)

def parseCallOrVar(tokens: List, i: Int, name: String): List =
  if (isTok(tokens, i, "LParen") == 1) parseCallArgs(tokens, i + 1, name) else if (isTok(tokens, i, "Dot") == 1) parseDotCallOrAdt(tokens, i + 1, name) else ok(List.cons("Var", List.cons(name, List.empty())), i)

def parseDotCallOrAdt(tokens: List, i: Int, enumName: String): List =
  for {
    caseP = parseIdent(tokens, i)
    caseName = pStr(caseP)
    j = pIdx(caseP)
  } yield if (isTok(tokens, j, "LParen") == 1) parseCallArgs(tokens, j + 1, str3(enumName, ".", caseName)) else ok(List.cons("Adt", List.cons(enumName, List.cons(caseName, List.empty()))), j)

def parseCallArgs(tokens: List, i: Int, callee: String): List =
  for {
    argsP = parseArgs(tokens, i, List.empty())
    i1 = expectTok(tokens, pIdx(argsP), "RParen")
  } yield ok(List.cons("Call", List.cons(callee, List.cons(pAst(argsP), List.empty()))), i1)

def parseModuleCall(tokens: List, i: Int, mod: String): List =
  for {
    i1 = expectTok(tokens, i, "Dot")
    methodP = parseIdent(tokens, i1)
    callee = str3(mod, ".", pStr(methodP))
    i2 = pIdx(methodP)
  } yield if (isTok(tokens, i2, "LParen") == 1) parseModuleCallArgs(tokens, i2 + 1, mod, callee) else ok(List.cons("Call", List.cons(callee, List.cons(List.empty(), List.empty()))), i2)

def parseModuleCallArgs(tokens: List, i: Int, mod: String, callee: String): List =
  if (streq(mod, "IO") == 1) parseIoArgs(tokens, i, callee) else parseCallArgs(tokens, i, callee)

def parseIo(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "Dot")
    methodP = parseIdent(tokens, i1)
    method = pStr(methodP)
    i2 = pIdx(methodP)
  } yield if (streq(method, "delay") == 1) parseIoDelay(tokens, i2) else if (isTok(tokens, i2, "LParen") == 1) parseIoArgs(tokens, i2 + 1, Str.concat("IO.", method)) else ok(List.cons("Call", List.cons(Str.concat("IO.", method), List.cons(List.empty(), List.empty()))), i2)

def parseIoDelay(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "LParen")
    i2 = expectTok(tokens, i1, "LParen")
    i3 = expectTok(tokens, i2, "RParen")
    i4 = expectTok(tokens, i3, "Arrow")
    i5 = expectTok(tokens, i4, "LParen")
    i6 = expectTok(tokens, i5, "RParen")
    i7 = expectTok(tokens, i6, "RParen")
  } yield ok(List.cons("Delay", List.empty()), i7)

def parseIoArgs(tokens: List, i: Int, callee: String): List =
  for {
    argsP = parseArgs(tokens, i, List.empty())
    i1 = expectTok(tokens, pIdx(argsP), "RParen")
    args = pAst(argsP)
  } yield if (streq(callee, "IO.println") == 1) ok(List.cons("Println", List.cons(List.head(args), List.empty())), i1) else if (streq(callee, "IO.fail") == 1) ok(List.cons("Fail", List.cons(List.head(args), List.empty())), i1) else if (streq(callee, "IO.pure") == 1) ok(List.cons("Pure", List.cons(List.head(args), List.empty())), i1) else if (streq(callee, "IO.sleep") == 1) ok(List.cons("Sleep", List.cons(List.head(args), List.empty())), i1) else if (streq(callee, "IO.race") == 1) ok(List.cons("IoRace", List.cons(List.head(args), List.cons(List.head(List.tail(args)), List.empty()))), i1) else if (streq(callee, "IO.both") == 1) ok(List.cons("IoBoth", List.cons(List.head(args), List.cons(List.head(List.tail(args)), List.empty()))), i1) else ok(List.cons("Call", List.cons(callee, List.cons(args, List.empty()))), i1)

def parseDef(tokens: List, i: Int): List =
  for {
    nameP = parseIdent(tokens, i)
    i1 = expectTok(tokens, pIdx(nameP), "LParen")
    paramsP = parseParams(tokens, i1, List.empty())
    i2 = expectTok(tokens, pIdx(paramsP), "RParen")
    i3 = expectTok(tokens, i2, "Colon")
    retP = parseType(tokens, i3)
    i4 = expectTok(tokens, pIdx(retP), "Eq")
    bodyP = parseBlock(tokens, i4)
  } yield ok(List.cons("Def", List.cons(pStr(nameP), List.cons(pAst(paramsP), List.cons(pStr(retP), List.cons(pAst(bodyP), List.empty()))))), pIdx(bodyP))

def parseMain(tokens: List, i: Int): List =
  for {
    i1 = expectTok(tokens, i, "Def")
    nameP = parseIdent(tokens, i1)
    i2 = expectTok(tokens, pIdx(nameP), "Colon")
    tyP = parseType(tokens, i2)
    i3 = expectTok(tokens, pIdx(tyP), "Eq")
  } yield parseBlock(tokens, i3)

def parseTop(tokens: List, i: Int, pkg: String, enums: List, defs: List, main: List): List =
  for {
    t = tokAt(tokens, i)
  } yield if (streq(t, "Eof") == 1) List.cons("Prog", List.cons(pkg, List.cons(List.reverse(enums), List.cons(List.reverse(defs), List.cons(main, List.empty()))))) else if (streq(t, "Enum") == 1) parseTopEnum(tokens, i + 1, pkg, enums, defs, main) else if (streq(t, "Def") == 1) parseTopDef(tokens, i + 1, pkg, enums, defs, main) else if (streq(t, "AtMain") == 1) parseTopMain(tokens, i + 1, pkg, enums, defs) else parseTop(tokens, i + 1, pkg, enums, defs, main)

def parseEnum(tokens: List, i: Int): List =
  for {
    nameP = parseIdent(tokens, i)
    i1 = pIdx(nameP)
  } yield if (isTok(tokens, i1, "LBrace") == 1) parseEnumBrace(tokens, i1 + 1, pStr(nameP), List.empty()) else if (isTok(tokens, i1, "Colon") == 1) parseEnumColon(tokens, i1 + 1, pStr(nameP), List.empty()) else ok(List.cons("Enum", List.cons(pStr(nameP), List.cons(List.empty(), List.empty()))), i1)

def parseEnumBrace(tokens: List, i: Int, name: String, acc: List): List =
  if (isTok(tokens, i, "RBrace") == 1) ok(List.cons("Enum", List.cons(name, List.cons(List.reverse(acc), List.empty()))), i + 1) else parseEnumBraceCase(tokens, i, name, acc)

def parseEnumBraceCase(tokens: List, i: Int, name: String, acc: List): List =
  for {
    i1 = expectTok(tokens, i, "Case")
    caseP = parseIdent(tokens, i1)
    i2 = pIdx(caseP)
    acc2 = List.cons(pStr(caseP), acc)
  } yield if (isTok(tokens, i2, "Comma") == 1) parseEnumBrace(tokens, i2 + 1, name, acc2) else parseEnumBrace(tokens, i2, name, acc2)

def parseEnumColon(tokens: List, i: Int, name: String, acc: List): List =
  if (isTok(tokens, i, "Case") == 0) ok(List.cons("Enum", List.cons(name, List.cons(List.reverse(acc), List.empty()))), i) else parseEnumColonCase(tokens, i + 1, name, acc)

def parseEnumColonCase(tokens: List, i: Int, name: String, acc: List): List =
  for {
    caseP = parseIdent(tokens, i)
  } yield parseEnumColon(tokens, pIdx(caseP), name, List.cons(pStr(caseP), acc))

def parseTopEnum(tokens: List, i: Int, pkg: String, enums: List, defs: List, main: List): List =
  for {
    e = parseEnum(tokens, i)
  } yield parseTop(tokens, pIdx(e), pkg, List.cons(pAst(e), enums), defs, main)

def parseTopDef(tokens: List, i: Int, pkg: String, enums: List, defs: List, main: List): List =
  for {
    d = parseDef(tokens, i)
  } yield parseTop(tokens, pIdx(d), pkg, enums, List.cons(pAst(d), defs), main)

def parseTopMain(tokens: List, i: Int, pkg: String, enums: List, defs: List): List =
  for {
    m = parseMain(tokens, i)
  } yield parseTop(tokens, pIdx(m), pkg, enums, defs, pAst(m))

def parseProgram(tokens: List): List =
  for {
    pkgP = parsePackage(tokens, 0)
  } yield parseTop(tokens, pIdx(pkgP), pStr(pkgP), List.empty(), List.empty(), unitExpr())


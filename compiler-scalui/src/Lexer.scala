package scalui.compiler

def skipTrivia(source: String, i: Int): Int =
  if (i >= Str.len(source)) i else skipTriviaCont(source, i, Str.charAt(source, i))

def skipTriviaCont(source: String, i: Int, c: Int): Int =
  if (c == 32) skipTrivia(source, i + 1) else if (c == 9) skipTrivia(source, i + 1) else if (c == 10) skipTrivia(source, i + 1) else if (c == 13) skipTrivia(source, i + 1) else if (c == 47) skipTriviaSlash(source, i) else i

def skipTriviaSlash(source: String, i: Int): Int =
  if (i + 1 >= Str.len(source)) i else if (Str.charAt(source, i + 1) == 47) skipLineComment(source, i + 2) else i

def skipLineComment(source: String, i: Int): Int =
  if (i >= Str.len(source)) i else if (Str.charAt(source, i) == 10) skipTrivia(source, i + 1) else skipLineComment(source, i + 1)

def kwToken(name: String): String =
  if (streq(name, "package") == 1) "Package" else if (streq(name, "enum") == 1) "Enum" else if (streq(name, "case") == 1) "Case" else if (streq(name, "match") == 1) "Match" else if (streq(name, "def") == 1) "Def" else if (streq(name, "for") == 1) "For" else if (streq(name, "yield") == 1) "Yield" else if (streq(name, "if") == 1) "If" else if (streq(name, "else") == 1) "Else" else Str.concat("Ident:", name)

def lexIdentEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i else if (isIdentChar(Str.charAt(source, i)) == 1) lexIdentEnd(source, i + 1) else i

def lexDigitsEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i else if (isDigit(Str.charAt(source, i)) == 1) lexDigitsEnd(source, i + 1) else i

def readString(source: String, i: Int, acc: String): List =
  if (i >= Str.len(source)) pair(acc, Str.fromInt(i)) else readStringCont(source, i, acc, Str.charAt(source, i))

def readStringCont(source: String, i: Int, acc: String, c: Int): List =
  if (c == 34) pair(acc, Str.fromInt(i + 1)) else if (c == 92) readStringEsc(source, i + 1, acc) else readStringRun(source, i, acc)

def readStringEsc(source: String, i: Int, acc: String): List =
  if (i >= Str.len(source)) pair(acc, Str.fromInt(i)) else readStringEscCont(source, i, acc, Str.charAt(source, i))

def readStringEscCont(source: String, i: Int, acc: String, c: Int): List =
  if (c == 110) readString(source, i + 1, Str.concat(acc, "\n")) else if (c == 116) readString(source, i + 1, Str.concat(acc, "\t")) else if (c == 92) readString(source, i + 1, Str.concat(acc, "\\")) else if (c == 34) readString(source, i + 1, Str.concat(acc, "\"")) else readString(source, i + 1, Str.concat(acc, Str.slice(source, i, i + 1)))

def readStringRun(source: String, i: Int, acc: String): List =
  for {
    j = readStringRunEnd(source, i)
  } yield readString(source, j, Str.concat(acc, Str.slice(source, i, j)))

def readStringRunEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i else readStringRunEndCont(source, i, Str.charAt(source, i))

def readStringRunEndCont(source: String, i: Int, c: Int): Int =
  if (c == 34) i else if (c == 92) i else readStringRunEnd(source, i + 1)

def peekEq(source: String, i: Int, ch: Int): Int =
  if (i >= Str.len(source)) 0 else if (Str.charAt(source, i) == ch) 1 else 0

def lexAt(source: String, i0: Int, acc: List): List =
  for {
    i = skipTrivia(source, i0)
  } yield if (i >= Str.len(source)) List.cons("Eof", acc) else lexAtChar(source, i, acc, Str.charAt(source, i))

def lexAtChar(source: String, i: Int, acc: List, c: Int): List =
  if (c == 64) lexAtMain(source, i, acc) else if (c == 34) lexAtString(source, i, acc) else if (c == 61) lexAtEq(source, i, acc) else if (c == 33) lexAtBang(source, i, acc) else if (c == 60) lexAtLt(source, i, acc) else if (c == 62) lexAtGt(source, i, acc) else if (c == 38) lexAtAmp(source, i, acc) else if (c == 124) lexAtPipe(source, i, acc) else if (isDigit(c) == 1) lexAtInt(source, i, acc) else if (isIdentStart(c) == 1) lexAtIdent(source, i, acc) else if (c == 58) lexAt(source, i + 1, List.cons("Colon", acc)) else if (c == 46) lexAt(source, i + 1, List.cons("Dot", acc)) else if (c == 44) lexAt(source, i + 1, List.cons("Comma", acc)) else if (c == 40) lexAt(source, i + 1, List.cons("LParen", acc)) else if (c == 41) lexAt(source, i + 1, List.cons("RParen", acc)) else if (c == 123) lexAt(source, i + 1, List.cons("LBrace", acc)) else if (c == 125) lexAt(source, i + 1, List.cons("RBrace", acc)) else if (c == 91) lexAt(source, i + 1, List.cons("LBracket", acc)) else if (c == 93) lexAt(source, i + 1, List.cons("RBracket", acc)) else if (c == 43) lexAt(source, i + 1, List.cons("Plus", acc)) else if (c == 45) lexAt(source, i + 1, List.cons("Minus", acc)) else if (c == 42) lexAt(source, i + 1, List.cons("Star", acc)) else if (c == 47) lexAt(source, i + 1, List.cons("Slash", acc)) else if (c == 37) lexAt(source, i + 1, List.cons("Percent", acc)) else if (c == 95) lexAt(source, i + 1, List.cons("Underscore", acc)) else lexAt(source, i + 1, acc)

def lexAtMain(source: String, i: Int, acc: List): List =
  for {
    j = lexIdentEnd(source, i + 1)
    name = Str.slice(source, i + 1, j)
  } yield if (streq(name, "main") == 1) lexAt(source, j, List.cons("AtMain", acc)) else lexAt(source, j, acc)

def lexAtString(source: String, i: Int, acc: List): List =
  for {
    p = readString(source, i + 1, "")
  } yield lexAt(source, parseInt(snd(p)), List.cons(Str.concat("String:", fst(p)), acc))

def lexAtEq(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 62) == 1) lexAt(source, i + 2, List.cons("Arrow", acc)) else if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("EqEq", acc)) else lexAt(source, i + 1, List.cons("Eq", acc))

def lexAtBang(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("BangEq", acc)) else lexAt(source, i + 1, acc)

def lexAtLt(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("LtEq", acc)) else if (peekEq(source, i + 1, 45) == 1) lexAt(source, i + 2, List.cons("LeftArrow", acc)) else lexAt(source, i + 1, List.cons("Lt", acc))

def lexAtGt(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("GtEq", acc)) else lexAt(source, i + 1, List.cons("Gt", acc))

def lexAtAmp(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 38) == 1) lexAt(source, i + 2, List.cons("AmpAmp", acc)) else lexAt(source, i + 1, acc)

def lexAtPipe(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 124) == 1) lexAt(source, i + 2, List.cons("PipePipe", acc)) else lexAt(source, i + 1, acc)

def lexAtInt(source: String, i: Int, acc: List): List =
  for {
    j = lexDigitsEnd(source, i)
  } yield lexAt(source, j, List.cons(Str.concat("Int:", Str.slice(source, i, j)), acc))

def lexAtIdent(source: String, i: Int, acc: List): List =
  for {
    j = lexIdentEnd(source, i)
    name = Str.slice(source, i, j)
  } yield if (streq(name, "s") == 1) if (peekEq(source, j, 34) == 1) lexInterpString(source, j, acc) else lexAt(source, j, List.cons(kwToken(name), acc)) else lexAt(source, j, List.cons(kwToken(name), acc))

def lexInterpString(source: String, quoteI: Int, acc: List): List =
  for {
    p = readInterp(source, quoteI + 1, List.empty(), "")
  } yield lexAt(source, parseInt(snd(p)), pushInterpTokens(fstL(p), acc))

def pushInterpTokens(parts: List, acc: List): List =
  List.cons("InterpEnd", appendLists(parts, List.cons("InterpStart", acc)))

def appendLists(xs: List, ys: List): List =
  if (List.isEmpty(xs) == 1) ys else List.cons(List.head(xs), appendLists(List.tail(xs), ys))

def readInterp(source: String, i: Int, parts: List, lit: String): List =
  if (i >= Str.len(source)) pairLS(parts, Str.fromInt(i)) else readInterpCont(source, i, parts, lit, Str.charAt(source, i))

def readInterpCont(source: String, i: Int, parts: List, lit: String, c: Int): List =
  if (c == 34) pairLS(flushInterpLit(parts, lit), Str.fromInt(i + 1)) else if (c == 92) readInterpEsc(source, i + 1, parts, lit) else if (c == 36) readInterpDollar(source, i + 1, flushInterpLit(parts, lit)) else readInterp(source, i + 1, parts, Str.concat(lit, Str.slice(source, i, i + 1)))

def flushInterpLit(parts: List, lit: String): List =
  if (streq(lit, "") == 1) parts else List.cons(Str.concat("InterpLit:", lit), parts)

def readInterpEsc(source: String, i: Int, parts: List, lit: String): List =
  if (i >= Str.len(source)) pairLS(parts, Str.fromInt(i)) else readInterpEscCont(source, i, parts, lit, Str.charAt(source, i))

def readInterpEscCont(source: String, i: Int, parts: List, lit: String, c: Int): List =
  if (c == 110) readInterp(source, i + 1, parts, Str.concat(lit, "\n")) else if (c == 116) readInterp(source, i + 1, parts, Str.concat(lit, "\t")) else if (c == 92) readInterp(source, i + 1, parts, Str.concat(lit, "\\")) else if (c == 34) readInterp(source, i + 1, parts, Str.concat(lit, "\"")) else if (c == 36) readInterp(source, i + 1, parts, Str.concat(lit, "$")) else readInterp(source, i + 1, parts, Str.concat(lit, Str.slice(source, i, i + 1)))

def readInterpDollar(source: String, i: Int, parts: List): List =
  if (i >= Str.len(source)) pairLS(parts, Str.fromInt(i)) else if (Str.charAt(source, i) == 123) readInterpBrace(source, i + 1, parts, "", 1) else if (isIdentStart(Str.charAt(source, i)) == 1) readInterpIdent(source, i, parts) else pairLS(parts, Str.fromInt(i))

def readInterpIdent(source: String, i: Int, parts: List): List =
  for {
    j = lexIdentEnd(source, i)
  } yield readInterp(source, j, List.cons(Str.concat("InterpId:", Str.slice(source, i, j)), parts), "")

def readInterpBrace(source: String, i: Int, parts: List, body: String, depth: Int): List =
  if (i >= Str.len(source)) pairLS(parts, Str.fromInt(i)) else readInterpBraceCont(source, i, parts, body, depth, Str.charAt(source, i))

def readInterpBraceCont(source: String, i: Int, parts: List, body: String, depth: Int, c: Int): List =
  if (c == 123) readInterpBrace(source, i + 1, parts, Str.concat(body, "{"), depth + 1) else if (c == 125) if (depth == 1) readInterp(source, i + 1, List.cons(Str.concat("InterpBrace:", body), parts), "") else readInterpBrace(source, i + 1, parts, Str.concat(body, "}"), depth - 1) else readInterpBrace(source, i + 1, parts, Str.concat(body, Str.slice(source, i, i + 1)), depth)

def lex(source: String): List =
  List.reverse(lexAt(source, 0, List.empty()))


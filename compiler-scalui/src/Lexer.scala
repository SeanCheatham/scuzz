package scalui.compiler

// Stage-1 lexer: source → List of token strings (kernel dialect).

def printable(): String =
  " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

def char1(c: Int): String =
  if (c == 9) "\t"
  else if (c == 10) "\n"
  else if (c == 13) "\r"
  else if (c >= 32) if (c <= 126) Str.slice(printable(), c - 32, c - 31) else "?"
  else "?"

def skipTrivia(source: String, i: Int): Int =
  if (i >= Str.len(source)) i
  else skipTriviaCont(source, i, Str.charAt(source, i))

def skipTriviaCont(source: String, i: Int, c: Int): Int =
  if (c == 32) skipTrivia(source, i + 1)
  else if (c == 9) skipTrivia(source, i + 1)
  else if (c == 10) skipTrivia(source, i + 1)
  else if (c == 13) skipTrivia(source, i + 1)
  else if (c == 47) skipTriviaSlash(source, i)
  else i

def skipTriviaSlash(source: String, i: Int): Int =
  if (i + 1 >= Str.len(source)) i
  else if (Str.charAt(source, i + 1) == 47) skipLineComment(source, i + 2)
  else i

def skipLineComment(source: String, i: Int): Int =
  if (i >= Str.len(source)) i
  else if (Str.charAt(source, i) == 10) skipTrivia(source, i + 1)
  else skipLineComment(source, i + 1)

def kwToken(name: String): String =
  if (streq(name, "package") == 1) "Package"
  else if (streq(name, "enum") == 1) "Enum"
  else if (streq(name, "case") == 1) "Case"
  else if (streq(name, "match") == 1) "Match"
  else if (streq(name, "def") == 1) "Def"
  else if (streq(name, "val") == 1) "Val"
  else if (streq(name, "if") == 1) "If"
  else if (streq(name, "else") == 1) "Else"
  else Str.concat("Ident:", name)

def lexIdentEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i
  else if (isIdentChar(Str.charAt(source, i)) == 1) lexIdentEnd(source, i + 1)
  else i

def lexDigitsEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i
  else if (isDigit(Str.charAt(source, i)) == 1) lexDigitsEnd(source, i + 1)
  else i

def escChar(c: Int): String =
  if (c == 110) "\n"
  else if (c == 116) "\t"
  else if (c == 92) "\\"
  else if (c == 34) "\""
  else char1(c)

def readString(source: String, i: Int, acc: String): List =
  if (i >= Str.len(source)) pair(acc, Str.fromInt(i))
  else readStringCont(source, i, acc, Str.charAt(source, i))

def readStringCont(source: String, i: Int, acc: String, c: Int): List =
  if (c == 34) pair(acc, Str.fromInt(i + 1))
  else if (c == 92) readStringEsc(source, i + 1, acc)
  else readStringRun(source, i, acc)

def readStringEsc(source: String, i: Int, acc: String): List =
  if (i >= Str.len(source)) pair(acc, Str.fromInt(i))
  else readString(source, i + 1, Str.concat(acc, escChar(Str.charAt(source, i))))

def readStringRun(source: String, i: Int, acc: String): List =
  val j = readStringRunEnd(source, i)
  readString(source, j, Str.concat(acc, Str.slice(source, i, j)))

def readStringRunEnd(source: String, i: Int): Int =
  if (i >= Str.len(source)) i
  else readStringRunEndCont(source, i, Str.charAt(source, i))

def readStringRunEndCont(source: String, i: Int, c: Int): Int =
  if (c == 34) i
  else if (c == 92) i
  else readStringRunEnd(source, i + 1)

def peekEq(source: String, i: Int, ch: Int): Int =
  if (i >= Str.len(source)) 0
  else if (Str.charAt(source, i) == ch) 1
  else 0

def lexAt(source: String, i0: Int, acc: List): List =
  val i = skipTrivia(source, i0)
  if (i >= Str.len(source)) List.cons("Eof", acc)
  else lexAtChar(source, i, acc, Str.charAt(source, i))

def lexAtChar(source: String, i: Int, acc: List, c: Int): List =
  if (c == 64) lexAtMain(source, i, acc)
  else if (c == 34) lexAtString(source, i, acc)
  else if (c == 61) lexAtEq(source, i, acc)
  else if (c == 33) lexAtBang(source, i, acc)
  else if (c == 60) lexAtLt(source, i, acc)
  else if (c == 62) lexAtGt(source, i, acc)
  else if (c == 38) lexAtAmp(source, i, acc)
  else if (c == 124) lexAtPipe(source, i, acc)
  else if (isDigit(c) == 1) lexAtInt(source, i, acc)
  else if (isIdentStart(c) == 1) lexAtIdent(source, i, acc)
  else if (c == 58) lexAt(source, i + 1, List.cons("Colon", acc))
  else if (c == 46) lexAt(source, i + 1, List.cons("Dot", acc))
  else if (c == 44) lexAt(source, i + 1, List.cons("Comma", acc))
  else if (c == 40) lexAt(source, i + 1, List.cons("LParen", acc))
  else if (c == 41) lexAt(source, i + 1, List.cons("RParen", acc))
  else if (c == 123) lexAt(source, i + 1, List.cons("LBrace", acc))
  else if (c == 125) lexAt(source, i + 1, List.cons("RBrace", acc))
  else if (c == 91) lexAt(source, i + 1, List.cons("LBracket", acc))
  else if (c == 93) lexAt(source, i + 1, List.cons("RBracket", acc))
  else if (c == 43) lexAt(source, i + 1, List.cons("Plus", acc))
  else if (c == 45) lexAt(source, i + 1, List.cons("Minus", acc))
  else if (c == 42) lexAt(source, i + 1, List.cons("Star", acc))
  else if (c == 47) lexAt(source, i + 1, List.cons("Slash", acc))
  else if (c == 37) lexAt(source, i + 1, List.cons("Percent", acc))
  else if (c == 95) lexAt(source, i + 1, List.cons("Underscore", acc))
  else lexAt(source, i + 1, acc)

def lexAtMain(source: String, i: Int, acc: List): List =
  val j = lexIdentEnd(source, i + 1)
  val name = Str.slice(source, i + 1, j)
  if (streq(name, "main") == 1) lexAt(source, j, List.cons("AtMain", acc))
  else lexAt(source, j, acc)

def lexAtString(source: String, i: Int, acc: List): List =
  val p = readString(source, i + 1, "")
  lexAt(source, parseInt(snd(p)), List.cons(Str.concat("String:", fst(p)), acc))

def lexAtEq(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 62) == 1) lexAt(source, i + 2, List.cons("Arrow", acc))
  else if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("EqEq", acc))
  else lexAt(source, i + 1, List.cons("Eq", acc))

def lexAtBang(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("BangEq", acc))
  else lexAt(source, i + 1, acc)

def lexAtLt(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("LtEq", acc))
  else lexAt(source, i + 1, List.cons("Lt", acc))

def lexAtGt(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 61) == 1) lexAt(source, i + 2, List.cons("GtEq", acc))
  else lexAt(source, i + 1, List.cons("Gt", acc))

def lexAtAmp(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 38) == 1) lexAt(source, i + 2, List.cons("AmpAmp", acc))
  else lexAt(source, i + 1, acc)

def lexAtPipe(source: String, i: Int, acc: List): List =
  if (peekEq(source, i + 1, 124) == 1) lexAt(source, i + 2, List.cons("PipePipe", acc))
  else lexAt(source, i + 1, acc)

def lexAtInt(source: String, i: Int, acc: List): List =
  val j = lexDigitsEnd(source, i)
  lexAt(source, j, List.cons(Str.concat("Int:", Str.slice(source, i, j)), acc))

def lexAtIdent(source: String, i: Int, acc: List): List =
  val j = lexIdentEnd(source, i)
  lexAt(source, j, List.cons(kwToken(Str.slice(source, i, j)), acc))

def lex(source: String): List =
  List.reverse(lexAt(source, 0, List.empty))

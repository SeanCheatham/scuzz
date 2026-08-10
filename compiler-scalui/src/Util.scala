package scalui.compiler

def streq(a: String, b: String): Int =
  Str.eq(a, b)

def strcat(a: String, b: String): String =
  Str.concat(a, b)

def str3(a: String, b: String, c: String): String =
  Str.concat(Str.concat(a, b), c)

def str4(a: String, b: String, c: String, d: String): String =
  Str.concat(str3(a, b, c), d)

def str5(a: String, b: String, c: String, d: String, e: String): String =
  Str.concat(str4(a, b, c, d), e)

def str6(a: String, b: String, c: String, d: String, e: String, f: String): String =
  Str.concat(str5(a, b, c, d, e), f)

def startsWith(s: String, prefix: String): Int =
  if (Str.len(prefix) > Str.len(s)) 0 else if (Str.eq(Str.slice(s, 0, Str.len(prefix)), prefix)) 1 else 0

def endsWith(s: String, suffix: String): Int =
  if (Str.len(suffix) > Str.len(s)) 0 else if (Str.eq(Str.slice(s, Str.len(s) - Str.len(suffix), Str.len(s)), suffix)) 1 else 0

def isDigit(c: Int): Int =
  if (c >= 48) if (c <= 57) 1 else 0 else 0

def isAlpha(c: Int): Int =
  if (c >= 65) if (c <= 90) 1 else if (c >= 97) if (c <= 122) 1 else 0 else 0 else 0

def isIdentStart(c: Int): Int =
  if (isAlpha(c) == 1) 1 else if (c == 95) 1 else 0

def isIdentChar(c: Int): Int =
  if (isIdentStart(c) == 1) 1 else if (isDigit(c) == 1) 1 else 0

def listLen(xs: List): Int =
  List.len(xs)

def consStr(h: String, t: List): List =
  List.cons(h, t)

def reverse(xs: List): List =
  List.reverse(xs)

def join(xs: List, sep: String): String =
  List.join(xs, sep)

def pair(a: String, b: String): List =
  List.cons(a, List.cons(b, List.empty()))

def fst(p: List): String =
  List.head(p)

def snd(p: List): String =
  List.head(List.tail(p))

def pairSL(a: String, b: List): List =
  List.cons(a, List.cons(b, List.empty()))

def sndL(p: List): List =
  List.head(List.tail(p))

def pairLS(a: List, b: String): List =
  List.cons(a, List.cons(b, List.empty()))

def fstL(p: List): List =
  List.head(p)

def quad(code: String, value: String, kind: String, id: Int): List =
  List.cons(code, List.cons(value, List.cons(kind, List.cons(Str.fromInt(id), List.empty()))))

def qCode(q: List): String =
  List.head(q)

def qValue(q: List): String =
  List.head(List.tail(q))

def qKind(q: List): String =
  List.head(List.tail(List.tail(q)))

def qId(q: List): Int =
  parseInt(List.head(List.tail(List.tail(List.tail(q)))))

def parseInt(s: String): Int =
  parseIntAt(s, 0, 0)

def parseIntAt(s: String, i: Int, acc: Int): Int =
  if (i >= Str.len(s)) acc else parseIntAtCont(s, i, acc, Str.charAt(s, i))

def parseIntAtCont(s: String, i: Int, acc: Int, c: Int): Int =
  if (isDigit(c) == 0) acc else parseIntAt(s, i + 1, acc * 10 + c - 48)

def skipWs(s: String, i: Int): Int =
  if (i >= Str.len(s)) i else skipWsCont(s, i, Str.charAt(s, i))

def skipWsCont(s: String, i: Int, c: Int): Int =
  if (c == 32) skipWs(s, i + 1) else if (c == 9) skipWs(s, i + 1) else if (c == 10) skipWs(s, i + 1) else if (c == 13) skipWs(s, i + 1) else i

def pathJoin(a: String, b: String): String =
  if (Str.len(a) == 0) b else if (Str.charAt(a, Str.len(a) - 1) == 47) Str.concat(a, b) else str3(a, "/", b)

def indexOfChar(s: String, ch: Int, i: Int): Int =
  if (i >= Str.len(s)) 0 - 1 else if (Str.charAt(s, i) == ch) i else indexOfChar(s, ch, i + 1)

def lastIndexOfChar(s: String, ch: Int, i: Int, last: Int): Int =
  if (i >= Str.len(s)) last else if (Str.charAt(s, i) == ch) lastIndexOfChar(s, ch, i + 1, i) else lastIndexOfChar(s, ch, i + 1, last)

def parentDir(path: String): String =
  parentDirAt(path, lastIndexOfChar(path, 47, 0, 0 - 1))

def parentDirAt(path: String, slash: Int): String =
  if (slash < 0) "." else if (slash == 0) "/" else Str.slice(path, 0, slash)

def asciiPrintable(): String =
  " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"

def hexDigit(n: Int): String =
  Str.slice("0123456789ABCDEF", n, n + 1)

def hex2(c: Int): String =
  str3("\\", hexDigit(c / 16), hexDigit(c - c / 16 * 16))

def charToStr(c: Int): String =
  if (c >= 32) if (c <= 126) Str.slice(asciiPrintable(), c - 32, c - 31) else hex2(c) else hex2(c)

def llvmEscapeChar(c: Int): String =
  if (c == 10) "\\0A" else if (c == 9) "\\09" else if (c == 92) "\\5C" else if (c == 34) "\\22" else if (c >= 32) if (c <= 126) charToStr(c) else hex2(c) else hex2(c)

def llvmEscapeAt(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else llvmEscapeAt(s, i + 1, Str.concat(acc, llvmEscapeChar(Str.charAt(s, i))))

def llvmEscape(s: String): String =
  llvmEscapeAt(s, 0, "")

def listConcat(a: List, b: List): List =
  if (List.isEmpty(a) == 1) b else List.cons(List.head(a), listConcat(List.tail(a), b))

def nodeStr(e: List, i: Int): String =
  List.at(List.tail(e), i)

def nodeExpr(e: List, i: Int): List =
  List.at(List.tail(e), i)

def mkS(code: String, value: String, kind: String, id: Int, conts: String): List =
  List.cons(code, List.cons(value, List.cons(kind, List.cons(Str.fromInt(id), List.cons(conts, List.empty())))))

def sCode(s: List): String =
  List.head(s)

def sValue(s: List): String =
  List.head(List.tail(s))

def sKind(s: List): String =
  List.head(List.tail(List.tail(s)))

def sId(s: List): Int =
  parseInt(List.head(List.tail(List.tail(List.tail(s)))))

def sConts(s: List): String =
  List.head(List.tail(List.tail(List.tail(List.tail(s)))))

def isIoKind(k: String): Int =
  if (startsWith(k, "io") == 1) 1 else 0

def payloadOfKind(k: String): String =
  if (streq(k, "ioi") == 1) "int" else "ptr"

def envPut(env: List, name: String, value: String, kind: String): List =
  List.cons(str4(name, "=", value, Str.concat(":", kind)), env)

def envBindName(b: String): String =
  Str.slice(b, 0, indexOfChar(b, 61, 0))

def envBindRest(b: String): String =
  Str.slice(b, indexOfChar(b, 61, 0) + 1, Str.len(b))

def envRestVal(rest: String): String =
  Str.slice(rest, 0, indexOfChar(rest, 58, 0))

def envRestKind(rest: String): String =
  Str.slice(rest, indexOfChar(rest, 58, 0) + 1, Str.len(rest))

def envGet(env: List, name: String): String =
  if (List.isEmpty(env) == 1) "null:ptr" else envGetCont(List.head(env), env, name)

def envGetCont(b: String, env: List, name: String): String =
  if (streq(envBindName(b), name) == 1) envBindRest(b) else envGet(List.tail(env), name)

def envGetVal(env: List, name: String): String =
  envRestVal(envGet(env, name))

def envGetKind(env: List, name: String): String =
  envRestKind(envGet(env, name))


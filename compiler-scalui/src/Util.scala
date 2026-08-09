package scalui.compiler

// Small helpers for Stage-1 (string/list). Keep inside kernel dialect.

def streq(a: String, b: String): Int = Str.eq(a, b)

def strcat(a: String, b: String): String = Str.concat(a, b)

def str3(a: String, b: String, c: String): String =
  Str.concat(Str.concat(a, b), c)

def str4(a: String, b: String, c: String, d: String): String =
  Str.concat(str3(a, b, c), d)

def startsWith(s: String, prefix: String): Int =
  if (Str.len(prefix) > Str.len(s)) 0
  else if (Str.eq(Str.slice(s, 0, Str.len(prefix)), prefix)) 1
  else 0

def endsWith(s: String, suffix: String): Int =
  if (Str.len(suffix) > Str.len(s)) 0
  else if (Str.eq(Str.slice(s, Str.len(s) - Str.len(suffix), Str.len(s)), suffix)) 1
  else 0

def isDigit(c: Int): Int =
  if (c >= 48) if (c <= 57) 1 else 0 else 0

def isAlpha(c: Int): Int =
  if (c >= 65) if (c <= 90) 1 else if (c >= 97) if (c <= 122) 1 else 0 else 0 else 0

def isIdentStart(c: Int): Int =
  if (isAlpha(c) == 1) 1 else if (c == 95) 1 else 0

def isIdentChar(c: Int): Int =
  if (isIdentStart(c) == 1) 1 else if (isDigit(c) == 1) 1 else 0

def listLen(xs: List): Int = List.len(xs)

def consStr(h: String, t: List): List = List.cons(h, t)

def reverse(xs: List): List = List.reverse(xs)

def join(xs: List, sep: String): String = List.join(xs, sep)

def pair(a: String, b: String): List = List.cons(a, List.cons(b, List.empty))
def fst(p: List): String = List.head(p)
def snd(p: List): String = List.head(List.tail(p))

def quad(code: String, value: String, kind: String, id: Int): List =
  List.cons(code, List.cons(value, List.cons(kind, List.cons(Str.fromInt(id), List.empty))))

def qCode(q: List): String = List.head(q)
def qValue(q: List): String = List.head(List.tail(q))
def qKind(q: List): String = List.head(List.tail(List.tail(q)))
def qId(q: List): Int = parseInt(List.head(List.tail(List.tail(List.tail(q)))))

def parseInt(s: String): Int = parseIntAt(s, 0, 0)

def parseIntAt(s: String, i: Int, acc: Int): Int =
  if (i >= Str.len(s)) acc
  else parseIntAtCont(s, i, acc, Str.charAt(s, i))

def parseIntAtCont(s: String, i: Int, acc: Int, c: Int): Int =
  if (isDigit(c) == 0) acc
  else parseIntAt(s, i + 1, acc * 10 + (c - 48))

def skipWs(s: String, i: Int): Int =
  if (i >= Str.len(s)) i
  else skipWsCont(s, i, Str.charAt(s, i))

def skipWsCont(s: String, i: Int, c: Int): Int =
  if (c == 32) skipWs(s, i + 1)
  else if (c == 9) skipWs(s, i + 1)
  else if (c == 10) skipWs(s, i + 1)
  else if (c == 13) skipWs(s, i + 1)
  else i

def pathJoin(a: String, b: String): String =
  if (Str.len(a) == 0) b
  else if (Str.charAt(a, Str.len(a) - 1) == 47) Str.concat(a, b)
  else str3(a, "/", b)

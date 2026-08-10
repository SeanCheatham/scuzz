package scalui.compiler

def tomlSkip(s: String, i: Int): Int =
  if (i >= Str.len(s)) i else tomlSkipCont(s, i, Str.charAt(s, i))

def tomlSkipCont(s: String, i: Int, c: Int): Int =
  if (c == 32) tomlSkip(s, i + 1) else if (c == 9) tomlSkip(s, i + 1) else if (c == 10) tomlSkip(s, i + 1) else if (c == 13) tomlSkip(s, i + 1) else if (c == 35) tomlSkip(s, tomlSkipLine(s, i + 1)) else i

def tomlSkipLine(s: String, i: Int): Int =
  if (i >= Str.len(s)) i else if (Str.charAt(s, i) == 10) i + 1 else tomlSkipLine(s, i + 1)

def tomlSectionStart(s: String, name: String): Int =
  tomlFindSection(s, 0, name)

def tomlFindSection(s: String, i: Int, name: String): Int =
  if (i >= Str.len(s)) 0 - 1 else tomlFindSectionAt(s, tomlSkip(s, i), name)

def tomlFindSectionAt(s: String, j: Int, name: String): Int =
  if (j >= Str.len(s)) 0 - 1 else if (Str.charAt(s, j) == 91) tomlMatchSection(s, j + 1, name) else tomlFindSection(s, tomlSkipLine(s, j), name)

def tomlMatchSection(s: String, i: Int, name: String): Int =
  tomlMatchSectionAt(s, tomlSkip(s, i), name)

def tomlMatchSectionAt(s: String, j: Int, name: String): Int =
  if (startsWith(Str.slice(s, j, Str.len(s)), name) == 0) tomlFindSection(s, j, name) else tomlMatchSectionClose(s, tomlSkip(s, j + Str.len(name)), name)

def tomlMatchSectionClose(s: String, k: Int, name: String): Int =
  if (k >= Str.len(s)) 0 - 1 else if (Str.charAt(s, k) == 93) k + 1 else tomlFindSection(s, k, name)

def tomlSectionEnd(s: String, i: Int): Int =
  if (i >= Str.len(s)) i else tomlSectionEndAt(s, tomlSkip(s, i))

def tomlSectionEndAt(s: String, j: Int): Int =
  if (j >= Str.len(s)) j else if (Str.charAt(s, j) == 91) j else tomlSectionEnd(s, tomlSkipLine(s, j))

def tomlGetString(s: String, section: String, key: String, fallback: String): String =
  tomlGetStringAt(s, tomlSectionStart(s, section), key, fallback)

def tomlGetStringAt(s: String, start: Int, key: String, fallback: String): String =
  if (start < 0) fallback else tomlScanString(s, start, tomlSectionEnd(s, start), key, fallback)

def tomlScanString(s: String, i: Int, end: Int, key: String, fallback: String): String =
  if (i >= end) fallback else tomlScanStringAt(s, tomlSkip(s, i), end, key, fallback)

def tomlScanStringAt(s: String, j: Int, end: Int, key: String, fallback: String): String =
  if (j >= end) fallback else if (Str.charAt(s, j) == 91) fallback else if (startsWith(Str.slice(s, j, Str.len(s)), key) == 1) tomlParseStringValue(s, tomlSkip(s, j + Str.len(key)), fallback) else tomlScanString(s, tomlSkipLine(s, j), end, key, fallback)

def tomlParseStringValue(s: String, i: Int, fallback: String): String =
  tomlParseStringValueAt(s, tomlSkip(s, i), fallback)

def tomlParseStringValueAt(s: String, j: Int, fallback: String): String =
  if (j >= Str.len(s)) fallback else if (Str.charAt(s, j) != 61) fallback else tomlParseStringAfterEq(s, tomlSkip(s, j + 1), fallback)

def tomlParseStringAfterEq(s: String, k: Int, fallback: String): String =
  if (k >= Str.len(s)) fallback else if (Str.charAt(s, k) != 34) fallback else readUntilQuote(s, k + 1, "")

def tomlGetInt(s: String, section: String, key: String, fallback: String): String =
  tomlGetIntAt(s, tomlSectionStart(s, section), key, fallback)

def tomlGetIntAt(s: String, start: Int, key: String, fallback: String): String =
  if (start < 0) fallback else tomlScanInt(s, start, tomlSectionEnd(s, start), key, fallback)

def tomlScanInt(s: String, i: Int, end: Int, key: String, fallback: String): String =
  if (i >= end) fallback else tomlScanIntAt(s, tomlSkip(s, i), end, key, fallback)

def tomlScanIntAt(s: String, j: Int, end: Int, key: String, fallback: String): String =
  if (j >= end) fallback else if (Str.charAt(s, j) == 91) fallback else if (startsWith(Str.slice(s, j, Str.len(s)), key) == 1) tomlParseIntValue(s, tomlSkip(s, j + Str.len(key)), fallback) else tomlScanInt(s, tomlSkipLine(s, j), end, key, fallback)

def tomlParseIntValue(s: String, i: Int, fallback: String): String =
  tomlParseIntValueAt(s, tomlSkip(s, i), fallback)

def tomlParseIntValueAt(s: String, j: Int, fallback: String): String =
  if (j >= Str.len(s)) fallback else if (Str.charAt(s, j) != 61) fallback else tomlParseIntAfterEq(s, tomlSkip(s, j + 1), fallback)

def tomlParseIntAfterEq(s: String, k: Int, fallback: String): String =
  if (k >= Str.len(s)) fallback else if (isDigit(Str.charAt(s, k)) == 0) fallback else tomlTakeDigits(s, k, "")

def tomlTakeDigits(s: String, i: Int, acc: String): String =
  if (i >= Str.len(s)) acc else if (isDigit(Str.charAt(s, i)) == 1) tomlTakeDigits(s, i + 1, Str.concat(acc, Str.slice(s, i, i + 1))) else acc

def tomlGetArrayInt(s: String, section: String, key: String, which: Int, fallback: String): String =
  tomlGetArrayIntAt(s, tomlSectionStart(s, section), key, which, fallback)

def tomlGetArrayIntAt(s: String, start: Int, key: String, which: Int, fallback: String): String =
  if (start < 0) fallback else tomlScanArray(s, start, tomlSectionEnd(s, start), key, which, fallback)

def tomlScanArray(s: String, i: Int, end: Int, key: String, which: Int, fallback: String): String =
  if (i >= end) fallback else tomlScanArrayAt(s, tomlSkip(s, i), end, key, which, fallback)

def tomlScanArrayAt(s: String, j: Int, end: Int, key: String, which: Int, fallback: String): String =
  if (j >= end) fallback else if (Str.charAt(s, j) == 91) fallback else if (startsWith(Str.slice(s, j, Str.len(s)), key) == 1) tomlParseArrayValue(s, tomlSkip(s, j + Str.len(key)), which, fallback) else tomlScanArray(s, tomlSkipLine(s, j), end, key, which, fallback)

def tomlParseArrayValue(s: String, i: Int, which: Int, fallback: String): String =
  tomlParseArrayValueAt(s, tomlSkip(s, i), which, fallback)

def tomlParseArrayValueAt(s: String, j: Int, which: Int, fallback: String): String =
  if (j >= Str.len(s)) fallback else if (Str.charAt(s, j) != 61) fallback else tomlParseArrayAfterEq(s, tomlSkip(s, j + 1), which, fallback)

def tomlParseArrayAfterEq(s: String, k: Int, which: Int, fallback: String): String =
  if (k >= Str.len(s)) fallback else if (Str.charAt(s, k) != 91) fallback else tomlArrayNth(s, k + 1, which, fallback)

def tomlArrayNth(s: String, i: Int, which: Int, fallback: String): String =
  tomlArrayNthAt(s, tomlSkip(s, i), which, fallback)

def tomlArrayNthAt(s: String, j: Int, which: Int, fallback: String): String =
  if (j >= Str.len(s)) fallback else if (Str.charAt(s, j) == 93) fallback else if (isDigit(Str.charAt(s, j)) == 1) tomlArrayNthTake(s, j, which, fallback, "") else tomlArrayNth(s, j + 1, which, fallback)

def tomlArrayNthTake(s: String, i: Int, which: Int, fallback: String, acc: String): String =
  if (i >= Str.len(s)) fallback else if (isDigit(Str.charAt(s, i)) == 1) tomlArrayNthTake(s, i + 1, which, fallback, Str.concat(acc, Str.slice(s, i, i + 1))) else if (which == 0) acc else tomlArrayNth(s, i, 0, fallback)

def tomlGetArrayString(s: String, section: String, key: String): List =
  tomlGetArrayStringAt(s, tomlSectionStart(s, section), key)

def tomlGetArrayStringAt(s: String, start: Int, key: String): List =
  if (start < 0) List.empty() else tomlScanStrArray(s, start, tomlSectionEnd(s, start), key)

def tomlScanStrArray(s: String, i: Int, end: Int, key: String): List =
  if (i >= end) List.empty() else tomlScanStrArrayAt(s, tomlSkip(s, i), end, key)

def tomlScanStrArrayAt(s: String, j: Int, end: Int, key: String): List =
  if (j >= end) List.empty() else if (Str.charAt(s, j) == 91) List.empty() else if (startsWith(Str.slice(s, j, Str.len(s)), key) == 1) tomlParseStrArray(s, tomlSkip(s, j + Str.len(key))) else tomlScanStrArray(s, tomlSkipLine(s, j), end, key)

def tomlParseStrArray(s: String, j: Int): List =
  if (j >= Str.len(s)) List.empty() else if (Str.charAt(s, j) != 61) List.empty() else tomlParseStrArrayAfterEq(s, tomlSkip(s, j + 1))

def tomlParseStrArrayAfterEq(s: String, k: Int): List =
  if (k >= Str.len(s)) List.empty() else if (Str.charAt(s, k) != 91) List.empty() else tomlStrItems(s, k + 1, List.empty())

def tomlStrItems(s: String, i: Int, acc: List): List =
  if (i >= Str.len(s)) List.reverse(acc) else tomlStrItemsAt(s, tomlSkip(s, i), acc)

def tomlStrItemsAt(s: String, j: Int, acc: List): List =
  if (j >= Str.len(s)) List.reverse(acc) else if (Str.charAt(s, j) == 93) List.reverse(acc) else if (Str.charAt(s, j) == 34) tomlStrItemsTake(s, j + 1, acc) else tomlStrItems(s, j + 1, acc)

def tomlStrItemsTake(s: String, i: Int, acc: List): List =
  tomlStrItemsCont(s, i, readUntilQuote(s, i, ""), acc)

def tomlStrItemsCont(s: String, i: Int, v: String, acc: List): List =
  tomlStrItems(s, i + Str.len(v) + 1, List.cons(v, acc))

def readTomlName(toml: String): String =
  tomlGetString(toml, "package", "name", "app")

def readTomlDefaultRuntime(toml: String): String =
  tomlGetString(toml, "ui", "default_runtime", "headless")

def readTomlHeadlessW(toml: String): String =
  tomlGetArrayInt(toml, "ui", "headless_size", 0, "200")

def readTomlHeadlessH(toml: String): String =
  tomlGetArrayInt(toml, "ui", "headless_size", 1, "120")

def hasUiSection(toml: String): Int =
  if (tomlSectionStart(toml, "ui") < 0) 0 else 1


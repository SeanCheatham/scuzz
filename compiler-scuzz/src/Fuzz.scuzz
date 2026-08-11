package scuzz.compiler

def lcgSeed(seed: Int): Int =
  seed % 2147483646 + 1

def lcgNext(s: Int): Int =
  s * 48271 % 2147483647

def lcgBelow(s: Int, n: Int): Int =
  if (n <= 0) 0 else s % n

def maxOf(a: Int, b: Int): Int =
  if (a >= b) a else b

def boolPos(n: Int): Int =
  if (n > 0) 1 else 0

def letterAt(i: Int): String =
  Str.slice("abcdefghijklmnopqrstuvwxyz", i, i + 1)

def fuzzKindCount(hasText: Int): Int =
  if (hasText == 1) 4 else 3

def fuzzTap(s: Int, nButtons: Int): List =
  pair(strcat("tap ", Str.fromInt(lcgBelow(s, maxOf(nButtons, 1)))), Str.fromInt(s))

def fuzzPump(s: Int): List =
  pair(strcat("pump ", Str.fromInt(1 + lcgBelow(s, 3))), Str.fromInt(s))

def fuzzWordAcc(s: Int, n: Int, acc: String): List =
  if (n == 0) pair(strcat("text ", acc), Str.fromInt(s)) else fuzzWordAcc(lcgNext(s), n - 1, Str.concat(acc, letterAt(lcgBelow(s, 26))))

def fuzzText(s: Int): List =
  fuzzWordAcc(lcgNext(s), 1 + lcgBelow(s, 7), "")

def fuzzEvent(s: Int, nButtons: Int, hasText: Int): List =
  fuzzEventAt(lcgNext(s), nButtons, hasText)

def fuzzEventAt(s: Int, nButtons: Int, hasText: Int): List =
  fuzzEventKind(s, lcgBelow(s, fuzzKindCount(hasText)), nButtons)

def fuzzEventKind(s: Int, k: Int, nButtons: Int): List =
  if (k <= 1) fuzzTap(lcgNext(s), nButtons) else if (k == 2) fuzzPump(lcgNext(s)) else fuzzText(lcgNext(s))

def fuzzScript(seed: Int, nButtons: Int, hasText: Int): List =
  fuzzScriptLen(lcgNext(lcgSeed(seed)), nButtons, hasText)

def fuzzScriptLen(s: Int, nButtons: Int, hasText: Int): List =
  fuzzScriptAcc(s, 1 + lcgBelow(s, 12), nButtons, hasText, List.empty())

def fuzzScriptAcc(s: Int, remaining: Int, nButtons: Int, hasText: Int, acc: List): List =
  if (remaining == 0) List.reverse(acc) else fuzzScriptStep(fuzzEvent(s, nButtons, hasText), remaining, nButtons, hasText, acc)

def fuzzScriptStep(ev: List, remaining: Int, nButtons: Int, hasText: Int, acc: List): List =
  fuzzScriptAcc(parseInt(snd(ev)), remaining - 1, nButtons, hasText, List.cons(fst(ev), acc))

def lineEndAt(s: String, i: Int): Int =
  if (i >= Str.len(s)) i else if (Str.charAt(s, i) == 10) i else lineEndAt(s, i + 1)

def countPrefixLines(s: String, prefix: String, i: Int, acc: Int): Int =
  if (i >= Str.len(s)) acc else countPrefixLines(s, prefix, lineEndAt(s, i) + 1, acc + startsWith(Str.slice(s, i, Str.len(s)), prefix))

def fuzzScriptText(events: List): String =
  Str.concat(join(events, "\n"), "\n")

def fuzzCmd(exe: String, script: String, dump: String, w: String, h: String): String =
  Str.concat(str6("env SCUZZ_UI_RUNTIME=headless SCUZZ_TESTRT=1 SCUZZ_UI_SCRIPT=", script, " SCUZZ_FUZZ_DUMP=", dump, " SCUZZ_UI_WIDTH=", w), str4(" SCUZZ_UI_HEIGHT=", h, " ", exe))

def fuzzExec(exe: String, fuzzDir: String, w: String, h: String, events: List): IO[Int] =
  Fs.write(pathJoin(fuzzDir, "script.txt"), fuzzScriptText(events)).flatMap(_ =>
    Fs.write(pathJoin(fuzzDir, "dump.txt"), "").flatMap(_ => Sys.exec(fuzzCmd(exe, pathJoin(fuzzDir, "script.txt"), pathJoin(fuzzDir, "dump.txt"), w, h)))
  )

def quoteEvents(events: List, acc: List): List =
  if (List.isEmpty(events) == 1) List.reverse(acc) else quoteEvents(List.tail(events), List.cons(str3("\"", List.head(events), "\""), acc))

def reproText(seed: Int, events: List): String =
  str5("[fuzz]\nseed = ", Str.fromInt(seed), "\nevents = [", join(quoteEvents(events, List.empty()), ", "), "]\n")

def fuzzFail(projectDir: String, fuzzDir: String, scriptSeed: Int, iter: Int, events: List): IO[Unit] =
  Fs.write(pathJoin(fuzzDir, "repro.toml"), reproText(scriptSeed, events)).flatMap(_ =>
    IO.println(str6("fuzz failure at script ", Str.fromInt(iter), " (seed ", Str.fromInt(scriptSeed), "); wrote ", pathJoin(fuzzDir, "repro.toml"))).flatMap(_ =>
      IO.println(str4("replay: scuzz fuzz ", projectDir, " --replay ", pathJoin(fuzzDir, "repro.toml"))).flatMap(_ => IO.fail("fuzz failure"))
    )
  )

def fuzzExhaustFail(projectDir: String, fuzzDir: String, depth: Int, scriptIndex: Int, events: List): IO[Unit] =
  Fs.write(pathJoin(fuzzDir, "repro.toml"), reproText(depth, events)).flatMap(_ =>
    IO.println(str6("fuzz --exhaust failure at script ", Str.fromInt(scriptIndex), " (depth ", Str.fromInt(depth), "); wrote ", pathJoin(fuzzDir, "repro.toml"))).flatMap(_ =>
      IO.println(str4("replay: scuzz fuzz ", projectDir, " --replay ", pathJoin(fuzzDir, "repro.toml"))).flatMap(_ => IO.fail("fuzz exhaust failure"))
    )
  )

def fuzzLoop(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, nButtons: Int, hasText: Int, seed: Int, iter: Int, iters: Int): IO[Unit] =
  if (iter >= iters) IO.println(str5("scuzz fuzz ok (", Str.fromInt(iters), " scripts, seed ", Str.fromInt(seed), ")")) else fuzzIter(exe, fuzzDir, w, h, projectDir, nButtons, hasText, seed, iter, iters)

def fuzzIter(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, nButtons: Int, hasText: Int, seed: Int, iter: Int, iters: Int): IO[Unit] =
  for {
    events = fuzzScript(seed + iter, nButtons, hasText)
  } yield fuzzExec(exe, fuzzDir, w, h, events).flatMap(code => if (code == 0) fuzzLoop(exe, fuzzDir, w, h, projectDir, nButtons, hasText, seed, iter + 1, iters) else fuzzFail(projectDir, fuzzDir, seed + iter, iter, events))

def exhaustTapAcc(i: Int, nButtons: Int, acc: List): List =
  if (i >= nButtons) List.reverse(acc) else exhaustTapAcc(i + 1, nButtons, List.cons(strcat("tap ", Str.fromInt(i)), acc))

def exhaustAlphabet(nButtons: Int, hasText: Int): List =
  exhaustAlphabetFinish(exhaustAlphabetText(exhaustTapAcc(0, nButtons, List.empty()), hasText))

def exhaustAlphabetText(taps: List, hasText: Int): List =
  if (hasText == 1) List.append(List.append(taps, "text"), "text a") else taps

def exhaustAlphabetFinish(base: List): List =
  List.append(base, "pump 1")

def exhaustRunPrefix(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, depth: Int, scriptIndex: Int, prefix: List): IO[Int] =
  fuzzExec(exe, fuzzDir, w, h, prefix).flatMap(code => if (code == 0) IO.pure(scriptIndex + 1) else fuzzExhaustFail(projectDir, fuzzDir, depth, scriptIndex, prefix).flatMap(_ => IO.pure(scriptIndex)))

def exhaustExtend(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, maxDepth: Int, targetLen: Int, alphabet: List, prefix: List, scriptIndex: Int): IO[Int] =
  if (List.len(prefix) == targetLen) exhaustRunPrefix(exe, fuzzDir, w, h, projectDir, maxDepth, scriptIndex, prefix) else exhaustExtendOver(exe, fuzzDir, w, h, projectDir, maxDepth, targetLen, alphabet, alphabet, prefix, scriptIndex)

def exhaustExtendOver(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, maxDepth: Int, targetLen: Int, alphabet: List, remaining: List, prefix: List, scriptIndex: Int): IO[Int] =
  if (List.isEmpty(remaining) == 1) IO.pure(scriptIndex) else exhaustExtend(exe, fuzzDir, w, h, projectDir, maxDepth, targetLen, alphabet, List.append(prefix, List.head(remaining)), scriptIndex).flatMap(next => exhaustExtendOver(exe, fuzzDir, w, h, projectDir, maxDepth, targetLen, alphabet, List.tail(remaining), prefix, next))

def fuzzExhaustDepth(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, alphabet: List, depth: Int, maxDepth: Int, scriptIndex: Int): IO[Unit] =
  if (depth > maxDepth) IO.println(str5("scuzz fuzz --exhaust ok (depth ", Str.fromInt(maxDepth), ", ", Str.fromInt(scriptIndex), " scripts)")) else exhaustExtend(exe, fuzzDir, w, h, projectDir, maxDepth, depth, alphabet, List.empty(), scriptIndex).flatMap(next => fuzzExhaustDepth(exe, fuzzDir, w, h, projectDir, alphabet, depth + 1, maxDepth, next))

def fuzzExhaust(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, nButtons: Int, hasText: Int, depth: Int): IO[Unit] =
  fuzzExhaustDepth(exe, fuzzDir, w, h, projectDir, exhaustAlphabet(nButtons, hasText), 1, depth, 0)

def fuzzProbe(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, seed: Int, iters: Int, exhaust: Int, depth: Int): IO[Unit] =
  fuzzExec(exe, fuzzDir, w, h, List.empty()).flatMap(code => if (code != 0) IO.fail("fuzz probe failed: app fails under TestRuntime before any event") else Fs.read(pathJoin(fuzzDir, "dump.txt")).flatMap(dump => fuzzProbeCont(exe, fuzzDir, w, h, projectDir, seed, iters, exhaust, depth, countPrefixLines(dump, "button:", 0, 0), countPrefixLines(dump, "textfield:", 0, 0))))

def fuzzProbeCont(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, seed: Int, iters: Int, exhaust: Int, depth: Int, nButtons: Int, nFields: Int): IO[Unit] =
  if (nButtons + nFields == 0) IO.println("scuzz fuzz ok (no buttons or text fields; probe only)") else if (exhaust == 1) fuzzExhaust(exe, fuzzDir, w, h, projectDir, nButtons, boolPos(nFields), depth) else fuzzLoop(exe, fuzzDir, w, h, projectDir, nButtons, boolPos(nFields), seed, 0, iters)

def fuzzReplay(exe: String, fuzzDir: String, w: String, h: String, replayPath: String): IO[Unit] =
  Fs.read(replayPath).flatMap(text => fuzzReplayLoaded(exe, fuzzDir, w, h, replayPath, tomlGetArrayString(text, "fuzz", "events"), tomlGetInt(text, "fuzz", "seed", "0")))

def fuzzReplayLoaded(exe: String, fuzzDir: String, w: String, h: String, replayPath: String, events: List, seedStr: String): IO[Unit] =
  IO.println(str6("scuzz fuzz --replay ", replayPath, " (", Str.fromInt(List.len(events)), " events, seed ", str3(seedStr, ")", ""))).flatMap(_ =>
    fuzzExec(exe, fuzzDir, w, h, events).flatMap(code => fuzzReplayResult(code))
  )

def fuzzReplayResult(code: Int): IO[Unit] =
  if (code == 0) IO.println("fuzz replay ok (no failure)") else IO.println("fuzz replay reproduced a failure").flatMap(_ => IO.fail("fuzz replay failure"))

def isFuzzValueFlag(s: String): Int =
  if (streq(s, "--iters") == 1) 1 else if (streq(s, "--seed") == 1) 1 else if (streq(s, "--replay") == 1) 1 else if (streq(s, "--depth") == 1) 1 else 0

def fuzzProjectArg(args: List, i: Int): String =
  if (i >= List.len(args)) "." else if (isFuzzValueFlag(List.at(args, i)) == 1) fuzzProjectArg(args, i + 2) else if (isFlag(List.at(args, i)) == 1) fuzzProjectArg(args, i + 1) else List.at(args, i)

def argValueAt(args: List, flag: String, fallback: String, i: Int): String =
  if (i >= List.len(args)) fallback else if (streq(List.at(args, i), flag) == 1) argValueTake(args, i + 1, fallback) else argValueAt(args, flag, fallback, i + 1)

def argValueTake(args: List, i: Int, fallback: String): String =
  if (i >= List.len(args)) fallback else List.at(args, i)

def cmdFuzzArgs(args: List): IO[Unit] =
  cmdFuzzValidated(fuzzProjectArg(args, 1), argValueAt(args, "--replay", "", 0), parseInt(argValueAt(args, "--iters", "32", 0)), parseInt(argValueAt(args, "--seed", "42", 0)), argFlag(args, "--exhaust", 0), argValueAt(args, "--depth", "", 0))

def cmdFuzzValidated(projectDir: String, replay: String, iters: Int, seed: Int, exhaust: Int, depthStr: String): IO[Unit] =
  if (Str.len(replay) > 0) cmdFuzz(projectDir, replay, iters, seed, 0, 0) else if (exhaust == 1) cmdFuzzExhaust(projectDir, depthStr) else if (Str.len(depthStr) > 0) IO.fail("fuzz --depth requires --exhaust") else cmdFuzz(projectDir, replay, iters, seed, 0, 0)

def cmdFuzzExhaust(projectDir: String, depthStr: String): IO[Unit] =
  if (Str.len(depthStr) == 0) IO.fail("fuzz --exhaust requires --depth N") else cmdFuzzExhaustDepth(projectDir, parseInt(depthStr))

def cmdFuzzExhaustDepth(projectDir: String, depth: Int): IO[Unit] =
  if (depth <= 0) IO.fail("fuzz --exhaust --depth N requires N > 0") else cmdFuzz(projectDir, "", 0, 0, 1, depth)

def cmdFuzz(projectDir: String, replay: String, iters: Int, seed: Int, exhaust: Int, depth: Int): IO[Unit] =
  Fs.read(pathJoin(projectDir, "scuzz.toml")).flatMap(toml => fuzzChecked(projectDir, replay, iters, seed, exhaust, depth, toml))

def fuzzChecked(projectDir: String, replay: String, iters: Int, seed: Int, exhaust: Int, depth: Int, toml: String): IO[Unit] =
  if (hasUiSection(toml) == 0) IO.fail("scuzz fuzz needs a [ui] project (Headless event scripts)") else compileProject(projectDir, pathJoin(projectDir, "build"), 0).flatMap(_ => fuzzBuilt(projectDir, replay, iters, seed, exhaust, depth, toml))

def fuzzBuilt(projectDir: String, replay: String, iters: Int, seed: Int, exhaust: Int, depth: Int, toml: String): IO[Unit] =
  for {
    exe = pathJoin(pathJoin(projectDir, "build"), readTomlName(toml))
    fuzzDir = pathJoin(pathJoin(projectDir, "build"), "fuzz")
    w = readTomlHeadlessW(toml)
    h = readTomlHeadlessH(toml)
  } yield Fs.mkdirs(fuzzDir).flatMap(_ => fuzzDispatch(exe, fuzzDir, w, h, projectDir, replay, iters, seed, exhaust, depth))

def fuzzDispatch(exe: String, fuzzDir: String, w: String, h: String, projectDir: String, replay: String, iters: Int, seed: Int, exhaust: Int, depth: Int): IO[Unit] =
  if (Str.len(replay) > 0) fuzzReplay(exe, fuzzDir, w, h, replay) else fuzzProbe(exe, fuzzDir, w, h, projectDir, seed, iters, exhaust, depth)


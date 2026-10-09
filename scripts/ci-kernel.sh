#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

# Input errors and domain checks stay active on both engines.
python3 - <<'PY_INPUT'
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()

def command(*args, env=None, ok=True):
    result = subprocess.run([str(cli), *map(str, args)], env=env,
                            capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (args, result.stdout[-4000:], result.stderr[-4000:])
    return result.stdout.strip().splitlines()

rng = random.Random(42)
integers = ['', '+', '-', ' 1', '1 ', '1\n', '1x', '1.0', '1e2', '１２', 'é',
            '7\0', '7\0x', '0', '-0', '+0007', '0' * 80,
            '9223372036854775807', '-9223372036854775808',
            '9223372036854775808', '-9223372036854775809', '9' * 80, '9' * 80 + 'x']
for _ in range(128):
    value = rng.randrange(-(1 << 65), 1 << 65)
    integers.extend([str(value), str(value) + rng.choice(['x', '\0', ' ', '.0'])])
for _ in range(64):
    integers.append(''.join(rng.choice('0123456789+- x\0é') for _ in range(rng.randrange(40))))

def parsed(text):
    if not re.fullmatch(r'[+-]?[0-9]+', text):
        return 'error:Str.parseInt: invalid integer'
    value = int(text)
    if not -(1 << 63) <= value < (1 << 63):
        return 'error:Str.parseInt: out of range'
    return f'ok:{value}'

fields = [('{}', 'error:Json.field: missing field'),
          ('[]', 'error:Json.field: expected object'),
          ('null', 'error:Json.field: expected object'),
          ('{"quantity":null}', 'ok:null'),
          ('{"quantity":true}', 'ok:true'),
          ('{"quantity":"7"}', 'ok:"7"'),
          ('{"quantity":7}', 'ok:7')]
quantities = ['{}', '[]', 'null', '{', '{"quantity":null}', '{"quantity":"7"}',
              '{"quantity":true}', '{"quantity":1.0}', '{"quantity":-1}',
              '{"quantity":0}', '{"quantity":1}', '{"quantity":100}', '{"quantity":101}']

def array(texts):
    return '[' + ', '.join(f'Hex.decode("{text.encode().hex()}")' for text in texts) + ']'

with tempfile.TemporaryDirectory(prefix='scuzz-input-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="input"\nversion="0.1.0"\n[fuzz]\nscore_floor=0\n')
    shutil.copy('examples/kernel/src/Input.scuzz', root / 'src/Input.scuzz')
    source = '''def field(text: String): String =
  Json.parse(text) match {
    case Result.Ok(json) => Json.field(json, "quantity") match {
      case Result.Ok(value) => Json.stringify(value) match {
        case Result.Ok(encoded) => s"ok:$encoded"
        case Result.Err(error) => s"error:$error"
      }
      case Result.Err(error) => s"error:$error"
    }
    case Result.Err(error) => s"error:$error"
  }

@main def main: IO[Unit] =
  for {
    _ <- IO.foreachDiscard(INTEGERS, text => IO.println(Input.parsed(text)))
    _ <- IO.foreachDiscard(FIELDS, text => IO.println(field(text)))
    _ <- IO.foreachDiscard(QUANTITIES, text => IO.println(Str.fromBool(Input.accepted(Input.decodeQuantity(text)))))
  } yield ()
'''.replace('INTEGERS', array(integers)).replace('FIELDS', array([text for text, _ in fields])).replace('QUANTITIES', array(quantities))
    (root / 'src/Main.scuzz').write_text(source)
    expected = [parsed(text) for text in integers] + [value for _, value in fields]
    expected += ['false'] * 10 + ['true', 'true', 'false']
    command('fmt', root)
    for engine in ['run', 'eval']:
        output = command(engine, root)
        if engine == 'run' and output and output[0] == 'ok':
            output = output[1:]
        assert output == expected, (engine, [(i, a, b) for i, (a, b) in enumerate(zip(output, expected)) if a != b][:10], len(output), len(expected))
    (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.println(Str.fromBool(Input.proof()))\n')
    (root / 'input.scuzz_verify').write_text('''oracle roundTrip(n: Int): Bool =
  Input.parsed(Str.fromInt(n)) == s"ok:$n"

oracle invalidSuffix(n: Int): Bool =
  Input.parsed(s"${n}x") == "error:Str.parseInt: invalid integer"

oracle validQuantity(n: Int): Bool =
  Input.accepted(Input.parseQuantity(Str.fromInt(n))) == (n >= 1 && n <= 100) && Input.accepted(Input.decodeQuantity(s"{\\"quantity\\":$n}")) == (n >= 1 && n <= 100)

oracle boundaries(): Bool =
  Input.proof()
''')
    command('fmt', root)
    for compiled in [False, True]:
        env = dict(os.environ)
        if compiled:
            env['SCUZZ_FUZZ_ENGINE'] = 'compiled'
        else:
            env.pop('SCUZZ_FUZZ_ENGINE', None)
        command('fuzz', '--iterations', '64', '--seed', '42', root, env=env)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert report['fuzz']['ok']
        assert report['scope']['search']['failed'] == 0
        engines = {row['engine'] for row in report['scope']['workloads'] if not row['required']}
        assert engines == {'compiled' if compiled else 'evaluator'}, engines
    for body in ['Str.parseInt("7") match { case Result.Ok(n) => n }',
                 'Json.field(Json.Null, "quantity") match { case Result.Ok(Json.Int(n)) => n\n    case Result.Err(_) => 0 }',
                 'Str.parseInt("7") + 1']:
        (root / 'src/Bad.scuzz').write_text('def invalid(): Int =\n  ' + body + '\n')
        command('fmt', root)
        diagnostic = '\n'.join(command('check', root, ok=False))
        assert 'non-exhaustive match' in diagnostic if 'match' in body else 'type error' in diagnostic, diagnostic
        (root / 'src/Bad.scuzz').unlink()
print('input validation and native parity ok')
PY_INPUT

# Result bindings retain an explicit failure policy.
python3 - <<'PY_RESULT'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()

def command(args, ok=True, env=None):
    result = subprocess.run([str(cli), *map(str, args)], capture_output=True,
                            text=True, timeout=180, env=env)
    assert (result.returncode == 0) == ok, (args, result.stdout[-4000:], result.stderr[-4000:])
    return result

with tempfile.TemporaryDirectory(prefix='scuzz-result-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="result"\nversion="0.1.0"\n[fuzz]\nscore_floor=0\n')
    shutil.copy('examples/kernel/src/Input.scuzz', root / 'src/Input.scuzz')
    source = '''type Read = Result[String, Int]

def identity[E, A](result: Result[E, A]): Result[E, A] =
  for { copy = result } yield copy

def consume(result: Read): Int =
  result match {
    case Result.Ok(n) => n
    case Result.Err(_) => -1
  }

def handled(text: String): Int =
  for {
    result = Str.parseInt(text)
    next = identity(result)
    use = (_unused: Unit) => consume(next)
  } yield use(())

def guarded(text: String): Bool =
  for { result = Str.parseInt(text) } yield true match {
    case true if consume(result) >= 0 => true
    case _ => false
  }

@main def main: IO[Unit] =
  for {
    _ <- IO.println(Str.fromInt(handled("7")))
    _ <- IO.println(Str.fromInt(handled("bad")))
    _ <- IO.println(Str.fromBool(guarded("7")))
    _ <- IO.println(Str.fromBool(guarded("bad")))
    _ <- Input.attempt(true).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.attempt(false).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.unitBind(true).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.unitBind(false).handleErrorWith(_ => IO.pure(-1)).flatMap(n => IO.println(Str.fromInt(n)))
  } yield ()
'''
    (root / 'src/Main.scuzz').write_text(source)
    command(['fmt', root])
    for engine in ['run', 'eval']:
        output = command([engine, root]).stdout.strip().splitlines()
        if output and output[0] == 'ok':
            output = output[1:]
        assert output == ['7', '-1', 'true', 'false', '7', '-1', '7', '-1'], (engine, output)
    bad = [('_ = Str.parseInt("7")', '()', 'discarded'),
           ('result = Str.parseInt("7")', '()', 'discarded'),
           ('_ignored = Str.parseInt("7")', '()', 'discarded'),
           ('Result.Ok(n) = Str.parseInt("7")', '()', 'must cover success and failure'),
           ('Result.Err(error) = Str.parseInt("bad")', '()', 'must cover success and failure'),
           ('_ <- IO.attempt(IO.pure(7))', '()', 'discarded'),
           ('result <- IO.attempt(IO.pure(7))', '()', 'discarded'),
           ('Result.Ok(n) <- IO.attempt(IO.pure(7))', '()', 'must cover success and failure'),
           ('result = Str.parseInt("7")\n    use = (result: Int) => result', '()', 'discarded'),
           ('result = Str.parseInt("7")', 'true match { case result => () }', 'discarded'),
           ('result = Str.parseInt("7")', 'for { result = 1 } yield ()', 'discarded'),
           ('result = Str.parseInt("7")', 'for { copy = result } yield ()', 'discarded'),
           ('result = (Str.parseInt("7"): Read)', '()', 'discarded'),
           ('result = Str.parseInt("7")', '"result" match { case _ => () }', 'discarded'),
           ('() <- IO.pure(7)', '()', 'unit pattern needs Unit'),
           ('(_, n) = (Str.parseInt("bad"), 7)', '()', 'discarded'),
           ('((), _) = ((), Str.parseInt("bad"))', '()', 'discarded'),
           ('(result, result) = (Str.parseInt("7"), Str.parseInt("bad"))', '()', 'binding pattern repeats name'),
           ('(n, n) = (1, 2)', '()', 'binding pattern repeats name'),
           ('(first, second) | (first, first) = (Str.parseInt("7"), Str.parseInt("bad"))', '()', 'pattern alternatives must bind the same names'),
           ('Input.Read(result = result, count = result) = Input.Read(Str.parseInt("bad"), 7)', '()', 'binding pattern repeats name'),
           ('(result, n) = (Str.parseInt("bad"), 7)', '()', 'discarded'),
           ('(first, second) = (Str.parseInt("7"), Str.parseInt("bad"))\n    _ = consume(first)', '()', 'discarded'),
           ('((_, n), flag) = ((Str.parseInt("bad"), 7), true)', '()', 'discarded'),
           ('(Result.Ok(n), _) = (Str.parseInt("bad"), 7)', '()', 'must cover success and failure'),
           ('Input.Read(count = n) = Input.Read(Str.parseInt("bad"), 7)', '()', 'discarded'),
           ('Input.Read(_, n) = Input.Read(Str.parseInt("bad"), 7)', '()', 'discarded'),
           ('Input.Read(result = Result.Ok(n)) = Input.Read(Str.parseInt("bad"), 7)', '()', 'must cover success and failure'),
           ('(_, n) <- IO.pure((Str.parseInt("bad"), 7))', '()', 'discarded'),
           ('Input.Read(count = n) <- IO.pure(Input.Read(Str.parseInt("bad"), 7))', '()', 'discarded'),
           ('Result.Ok(n) :: tail = [Str.parseInt("bad")]', '()', 'must cover success and failure'),
           ('_ :: tail = [Str.parseInt("bad")]', '()', 'discarded'),
           ('(result, n) = (Str.parseInt("bad"), 7)\n    use = (result: Int) => result', '()', 'discarded')]
    for bindings, body, message in bad:
        effect = '<-' in bindings
        declaration = 'IO[Unit]' if effect else 'Unit'
        broken = f'type Read = Result[String, Int]\ndef broken(): {declaration} =\n  for {{\n    {bindings}\n  }} yield {body}\n'
        (root / 'src/Bad.scuzz').write_text(broken)
        command(['fmt', root])
        result = command(['check', '--message-format=json', root], ok=False)
        diagnostics = json.loads(result.stdout)
        error = next(row for row in diagnostics if row['severity'] == 'error')
        assert message in error['message'], error
        assert error['file'].endswith('Bad.scuzz') and error['line'] >= 4 and error['column'] > 0, (bindings, error)
        for entry in ['build', 'eval']:
            result = command([entry, root], ok=False)
            assert message in result.stdout + result.stderr, (entry, result.stdout, result.stderr)
        (root / 'src/Bad.scuzz').unlink()
    (root / 'result.scuzz_verify').write_text('''oracle handled(n: Int): Bool =
  Input.resolved(Str.fromInt(n)) == n && Input.resolved("bad") == -1

oracle fields(n: Int): Bool =
  Input.fields(Str.fromInt(n)) == Result.Ok(n) && Input.fields("bad") == Result.Err("Str.parseInt: invalid integer") && Property.force(Input.effectFields(Str.fromInt(n))) == Result.Ok(n) && Property.force(Input.effectFields("bad")) == Result.Err("Str.parseInt: invalid integer")

oracle attempts(flag: Bool): Bool =
  Property.force(Input.attempt(flag)) == (if (flag) 7 else -1)

oracle unitBinding(flag: Bool): Bool =
  Property.force(Input.unitBind(flag).handleErrorWith(_ => IO.pure(-1))) == (if (flag) 7 else -1)
''')
    command(['fmt', root])
    for compiled in [False, True]:
        env = dict(os.environ)
        if compiled:
            env['SCUZZ_FUZZ_ENGINE'] = 'compiled'
        else:
            env.pop('SCUZZ_FUZZ_ENGINE', None)
        command(['fuzz', '--iterations', '32', root], env=env)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert report['fuzz']['ok'] and report['scope']['search']['failed'] == 0
        engines = {row['engine'] for row in report['scope']['workloads'] if not row['required']}
        assert engines == {'compiled' if compiled else 'evaluator'}, engines
print('Result handling and source diagnostics ok')
PY_RESULT

"$SCUZZ" run examples/kernel | tee /tmp/kernel.out
# Runner segvs here are flaky. On failure, rerun the exe under gdb so the log keeps a backtrace.
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  sudo apt-get update -qq && sudo apt-get install -y -qq gdb
  gdb -batch -ex run -ex bt ./examples/kernel/build/kernel 2>&1 | tail -30 || true
  exit 1
fi
grep -Fxq 'input:y' /tmp/kernel.out
grep -Fxq 'triple:a"}b' /tmp/kernel.out
grep -Fxq 'nested:a}b' /tmp/kernel.out
grep -Fxq 'iget:y' /tmp/kernel.out
grep -Fxq 'mparam:y' /tmp/kernel.out
grep -Fxq 'nlist:y' /tmp/kernel.out
grep -Fxq 'extTail:y' /tmp/kernel.out
grep -Fxq 'extBy:y' /tmp/kernel.out
grep -Fxq 'reduceN:y' /tmp/kernel.out
grep -Fxq 'foldSeed:y' /tmp/kernel.out
grep -Fxq 'foldNum:y' /tmp/kernel.out
grep -Fxq 'foldFloat:y' /tmp/kernel.out
grep -Fxq 'floatList:y' /tmp/kernel.out
grep -Fxq 'floatOrder:y' /tmp/kernel.out
grep -Fxq 'floatNaN:y' /tmp/kernel.out
grep -Fxq 'floatInline:y' /tmp/kernel.out
grep -Fxq 'floatLocal:y' /tmp/kernel.out
grep -Fxq 'floatNested:y' /tmp/kernel.out
grep -Fxq 'floatDerived:y' /tmp/kernel.out
grep -Fxq 'floatBound:y' /tmp/kernel.out
grep -Fxq 'floatIndexed:y' /tmp/kernel.out
grep -Fxq 'floatCons:y' /tmp/kernel.out
grep -Fxq 'floatCallEq:y' /tmp/kernel.out
grep -Fxq 'floatBoxEq:y' /tmp/kernel.out
grep -Fxq 'floatTupleEq:y' /tmp/kernel.out
grep -Fxq 'floatMultiEq:y' /tmp/kernel.out
grep -Fxq 'floatSignalEq:y' /tmp/kernel.out
grep -Fxq 'floatListKitEq:y' /tmp/kernel.out
grep -Fxq 'floatListUpdateEq:y' /tmp/kernel.out
grep -Fxq 'floatMapSetEq:y' /tmp/kernel.out
grep -Fxq 'floatStreamEq:y' /tmp/kernel.out
grep -Fxq 'floatIoFailEq:y' /tmp/kernel.out
grep -Fxq 'zipAllN:y' /tmp/kernel.out
grep -Fxq 'unqualifiedItem:8' /tmp/kernel.out
grep -Fxq 'unqualifiedNamedItem:9' /tmp/kernel.out
grep -Fxq 'nestedUnqualifiedItem:8' /tmp/kernel.out
grep -Fxq 'nestedNamedItem:9' /tmp/kernel.out
grep -Fxq 'jsonNested:ok' /tmp/kernel.out
grep -Fxq 'jsonNestedInt:7' /tmp/kernel.out
grep -Fxq 'qualifiedRecord:4' /tmp/kernel.out
grep -Fxq 'remoteRecord:4' /tmp/kernel.out
grep -Fxq 'remoteMarker:ok' /tmp/kernel.out
grep -Fxq 'remoteBox:7' /tmp/kernel.out
grep -Fxq 'remotePairDigits:34' /tmp/kernel.out
grep -Fxq 'remotePairText:ab' /tmp/kernel.out
grep -Fxq 'remotePairTemp:a' /tmp/kernel.out
grep -Fxq 'remotePairTempInt:3' /tmp/kernel.out
grep -Fxq 'remotePairCallField:3' /tmp/kernel.out
grep -Fxq 'remotePairCallText:b' /tmp/kernel.out
grep -Fxq 'remotePairIf:3:8' /tmp/kernel.out
grep -Fxq 'shadowModule:7' /tmp/kernel.out
# The evaluator is a reference semantics: same stdout as the compiled kernel.
"$SCUZZ" eval examples/kernel | tee /tmp/kernel-eval.out
diff <(grep -vx ok /tmp/kernel.out) /tmp/kernel-eval.out
"$SCUZZ" run examples/scale | tee /tmp/scale.out
grep -q "mapn:3048" /tmp/scale.out
grep -q "maps:2098176" /tmp/scale.out
grep -q "hit:0:49" /tmp/scale.out
SCUZZ_SERVE=1 "$SCUZZ" run examples/io | tee /tmp/io.out
grep -q "ref-ok" /tmp/io.out
grep -q "queue-ok" /tmp/io.out
grep -q "deferred-ok" /tmp/io.out
grep -q "fork-ok" /tmp/io.out
grep -q "interrupted" /tmp/io.out
grep -q "repeat-ok" /tmp/io.out
grep -q "retry-ok" /tmp/io.out
grep -q "forever-stopped" /tmp/io.out
grep -q "foreach:a!,b!" /tmp/io.out
grep -q "foreachN:2" /tmp/io.out
grep -q "each:k" /tmp/io.out
grep -q "when:y" /tmp/io.out
grep -q "unless:y" /tmp/io.out
grep -q "refN:2" /tmp/io.out
grep -q "queueN:7" /tmp/io.out
grep -q "defN:8" /tmp/io.out
grep -q "forkN:9" /tmp/io.out
grep -q "fs:fs-note" /tmp/io.out
grep -q "file:200:8:ok:/ping" /tmp/io.out
grep -q "large-file:200:2097152" /tmp/io.out
grep -q "rand:ok" /tmp/io.out
grep -q "use:token" /tmp/io.out
grep -q "release:token" /tmp/io.out
grep -q "release:token2" /tmp/io.out
grep -q "recovered" /tmp/io.out
grep -q "timeout-fast" /tmp/io.out
grep -q "got:ok" /tmp/io.out
grep -q "timed-out" /tmp/io.out
grep -q "release:to-tok" /tmp/io.out
grep -q "a!,b!,c" /tmp/io.out
grep -q "drain:d" /tmp/io.out
grep -q "filter:a,b" /tmp/io.out
grep -q "map:a!,b!" /tmp/io.out
grep -q "takeWhile:a,b" /tmp/io.out
grep -q "dropWhile:a,b" /tmp/io.out
grep -q "find:a" /tmp/io.out
grep -q "exists:1" /tmp/io.out
grep -q "miss:0" /tmp/io.out
grep -q "filterN:4" /tmp/io.out
grep -q "intersperseN:2" /tmp/io.out
grep -Fxq 'zipAllN:y' /tmp/io.out
grep -q "real:" /tmp/io.out
grep -q "mono:" /tmp/io.out
grep -q "iso:1970-01-01T00:00:00.000Z" /tmp/io.out
grep -q "leap:2020-02-29T00:00:00.000Z" /tmp/io.out
grep -q "parse:0" /tmp/io.out
grep -q "parse-neg:-1" /tmp/io.out
grep -q "parse-off:0" /tmp/io.out
grep -q "zone:1970-01-01T05:30:00.000+05:30" /tmp/io.out
grep -q "zone0:1970-01-01T00:00:00.000Z" /tmp/io.out
grep -q "zone-west:2020-02-28T16:00:00.000-08:00" /tmp/io.out
grep -q "parse-west:1582934400000" /tmp/io.out
grep -q "zone-bad:miss" /tmp/io.out
grep -q "parse-bad:none" /tmp/io.out
grep -q "parse-leap:none" /tmp/io.out
grep -q "parse-frac:100" /tmp/io.out
grep -q "parse-plain:1000" /tmp/io.out
grep -q "parse-y0:-62167219200000" /tmp/io.out
grep -q "re:hit" /tmp/io.out
grep -q "re-miss:miss" /tmp/io.out
grep -q "re-bad:miss" /tmp/io.out
grep -q "sha:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" /tmp/io.out
grep -q "hex:616263" /tmp/io.out
grep -q "hex-rt:abc" /tmp/io.out
grep -q "hex-bad:miss" /tmp/io.out
grep -q "b64:YWJj" /tmp/io.out
grep -q "b64-rt:abc" /tmp/io.out
grep -q "b64-bad:miss" /tmp/io.out
grep -q "uuid:ok" /tmp/io.out
grep -q "bytes:2" /tmp/io.out
grep -q "kit:skip" /tmp/io.out
grep -q "fs:" /tmp/io.out
# The evaluator runs the same effects. Clock and random draws differ per run.
SCUZZ_SERVE=1 "$SCUZZ" eval examples/io | tee /tmp/io-eval.out
diff <(grep -vx ok /tmp/io.out | grep -Ev '^(real|mono|nethN):') <(grep -Ev '^(real|mono|nethN):' /tmp/io-eval.out)
"$SCUZZ" fuzz --iterations 0 examples/io | tee /tmp/io-test.out
grep -q "served:POST:/ping:hi" /tmp/io-test.out
grep -q "ping:200:ok:ok:/ping" /tmp/io-test.out
grep -q "miss:404:miss:missing" /tmp/io-test.out
grep -q "tls:200:ok:ok:/ping" /tmp/io-test.out
grep -q "files:200:ok:ok:/ping" /tmp/io-test.out
grep -q "iso:1970-01-01T00:00:00.000Z" /tmp/io-test.out
grep -q "leap:2020-02-29T00:00:00.000Z" /tmp/io-test.out
grep -q "parse:0" /tmp/io-test.out
grep -q "zone:1970-01-01T05:30:00.000+05:30" /tmp/io-test.out
grep -q "parse-bad:none" /tmp/io-test.out
grep -q "parse-y0:-62167219200000" /tmp/io-test.out
grep -q "re:hit" /tmp/io-test.out
grep -q "re-miss:miss" /tmp/io-test.out
grep -q "re-bad:miss" /tmp/io-test.out
grep -q "sha:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" /tmp/io-test.out
grep -q "hex:616263" /tmp/io-test.out
grep -q "hex-rt:abc" /tmp/io-test.out
grep -q "hex-bad:miss" /tmp/io-test.out
grep -q "b64:YWJj" /tmp/io-test.out
grep -q "b64-rt:abc" /tmp/io-test.out
grep -q "b64-bad:miss" /tmp/io-test.out
grep -q "uuid:ok" /tmp/io-test.out
grep -q "bytes:2" /tmp/io-test.out
grep -q "impurity-ok" /tmp/io-test.out
grep -q "net:" /tmp/io-test.out
"$SCUZZ" check examples/hello
"$SCUZZ" check examples/kernel
"$SCUZZ" check examples/scale
./examples/jump/gen.sh
"$SCUZZ" check examples/jump
"$SCUZZ" check examples/fmt
"$SCUZZ" check examples/tyck
"$SCUZZ" check examples/codegen
"$SCUZZ" check examples/cli
"$SCUZZ" check examples/counter
"$SCUZZ" check examples/studio
"$SCUZZ" check examples/editor
"$SCUZZ" check examples/bad-example
"$SCUZZ" check examples/bad-fault
"$SCUZZ" check examples/bad-adt
"$SCUZZ" check examples/bad-sched
"$SCUZZ" check examples/bad-response
"$SCUZZ" check examples/bad-split
"$SCUZZ" check examples/bad-sometimes
if "$SCUZZ" check examples/bad-intent; then
  echo "empty verify should fail check" && exit 1
fi
if "$SCUZZ" check examples/bad-alt; then
  echo "mismatched alternative bindings should fail check" && exit 1
fi
"$SCUZZ" check --message-format=json examples/hello | tee /tmp/hello-check.json
grep -q '"severity":"info"' /tmp/hello-check.json
grep -q 'unclaimed def' /tmp/hello-check.json
"$SCUZZ" lsp --help | tee /tmp/lsp-help.txt
grep -q "scuzz check" /tmp/lsp-help.txt
if "$SCUZZ" check testdata/fmt/needs_format > /tmp/fmt-check.err 2>&1; then echo "expected format error" && exit 1; fi
grep -q "needs formatting" /tmp/fmt-check.err
python3 - "$SCUZZ" <<'PYFMT'
import pathlib
import subprocess
import sys
import tempfile

cli = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="scuzz-format-") as tmp:
    root = pathlib.Path(tmp)
    (root / "scuzz.toml").write_text('[package]\nname = "format-proof"\nversion = "0.1.0"\n')
    sources = {
        "src/Main.scuzz": "@main def main:IO[Unit]=IO.pure(())\n",
        "drivers/world.scuzz_scenario": "def setup():IO[Unit]=IO.pure(())\n",
        "law.scuzz_verify": "oracle valid():Bool=true\n",
        "claims spaced/law.scuzz_verify": "oracle other():Bool=true\n",
    }
    ignored = {f"{folder}/ignored.scuzz_verify": "not Scuzz\n"
               for folder in ["build", "corpus", "goldens", ".hidden"]}
    ignored.update({"Ignored.scuzz": "not Scuzz\n",
                    "src/nested/Ignored.scuzz": "not Scuzz\n"})
    for name, content in {**sources, **ignored}.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def run(*args, ok=True):
        result = subprocess.run([cli, *args, str(root)], text=True, capture_output=True)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        return result.stdout + result.stderr

    run("check", ok=False)
    run("fmt", "--check", ok=False)
    assert all((root / name).read_text() == content for name, content in sources.items())
    run("fmt")
    formatted = {name: (root / name).read_text() for name in sources}
    assert all(formatted[name] != content for name, content in sources.items())
    run("fmt", "--check")
    run("fmt")
    assert all((root / name).read_text() == content for name, content in formatted.items())
    assert all((root / name).read_text() == content for name, content in ignored.items())
    run("check")
    broken = root / "drivers/world.scuzz_scenario"
    broken.write_text("def setup(:\n")
    assert "parse error" in run("fmt", ok=False)
    assert broken.read_text() == "def setup(:\n"
print("format source scope ok")
PYFMT

# A pure allocation loop must fail inside the probe memory limit.
python3 - "$SCUZZ" <<'PYMEM'
import os
import pathlib
import resource
import signal
import subprocess
import sys
import tempfile

if sys.platform != "linux":
    sys.exit(0)
cli = str(pathlib.Path(sys.argv[1]).resolve())

def outer_limit():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_AS, (1024 * 1024 * 1024,) * 2)

with tempfile.TemporaryDirectory(prefix="scuzz-probe-memory-") as tmp:
    root = pathlib.Path(tmp)
    (root / "src").mkdir()
    (root / "scuzz.toml").write_text('[package]\nname = "memory-proof"\n')
    (root / "src/Main.scuzz").write_text(
        'def grow(s: String): String =\n  grow(Str.concat(s, s))\n\n'
        '@main def main: IO[Unit] =\n  IO.println(grow("x"))\n'
    )
    child = subprocess.Popen(
        [cli, "fuzz", "--iterations", "0", tmp],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        start_new_session=True, preexec_fn=outer_limit,
    )
    try:
        output, _ = child.communicate(timeout=60)
    finally:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait()
    assert child.returncode != 0, output.decode()
    assert b"out of memory" in output, output.decode()
    assert b"fuzz live graph failed" in output, output.decode()
    assert resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss < 524288
PYMEM
"$SCUZZ" build examples/kernel
"$SCUZZ" build examples/kernel 2>&1 | tee /tmp/incr.out
if grep -q "^ok$" /tmp/incr.out; then echo "fingerprint hit should not rebuild" && exit 1; fi
test -x examples/kernel/build/kernel
# Path-dep invalidation: a change in a dependency source must rebuild the root.
"$SCUZZ" build --full examples/counter
"$SCUZZ" build examples/counter 2>&1 | tee /tmp/counter-incr.out
if grep -q "^ok$" /tmp/counter-incr.out; then echo "fingerprint hit should not rebuild" && exit 1; fi
cp examples/shared/src/Shared.scuzz /tmp/shared-orig.scuzz
printf 'def counterTitle(): String =\n  "Counter"\n\ndef countLabel(n: Int): String =\n  s"count = $n!"\n' > examples/shared/src/Shared.scuzz
"$SCUZZ" build examples/counter 2>&1 | tee /tmp/counter-inval.out
if ! grep -q "^ok$" /tmp/counter-inval.out; then echo "dependency edit should invalidate fingerprint" && exit 1; fi
cp /tmp/shared-orig.scuzz examples/shared/src/Shared.scuzz
"$SCUZZ" build --full examples/counter

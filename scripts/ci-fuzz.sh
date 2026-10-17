#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

fuzz() {
  local started=$SECONDS status=0
  echo "fuzz $*"
  "$SCUZZ" fuzz "$@" || status=$?
  echo "fuzz $*: $((SECONDS - started)) seconds (exit $status)"
  return "$status"
}

assert_fuzz_summary() {
  python3 - "$1" "$2" <<'PY_CHECK'
import json, sys
from pathlib import Path
summary = json.loads(Path(sys.argv[1]).read_text())
lines = Path(sys.argv[2]).read_text().splitlines()
budget = summary["fuzz"]["iterations"]
search = budget * 5 // 8
assert 0 <= summary["fuzz"]["search"] <= search
assert 0 <= summary["fuzz"]["search_failures"] <= summary["fuzz"]["search"]
assert 0 <= summary["mutate"]["ran"] <= min(budget - search, summary["mutate"]["sites"])
if summary["fuzz"]["ok"]:
    assert summary["fuzz"]["search"] == search
    assert summary["mutate"]["ran"] == min(budget - search, summary["mutate"]["sites"])
coverage = summary["coverage"]
branches = coverage["branches"]
for group in (coverage, branches):
    assert group["total"] == len(group["regions"])
    assert group["reached"] == sum(row["reached"] for row in group["regions"])
expected = [f"coverage: functions {coverage['reached']}/{coverage['total']}, "
            f"branches {branches['reached']}/{branches['total']}"]
for key, action in (("sometimes", "reached"), ("triggers", "fired")):
    group = summary[key]
    expected.append(f"{key}: {len(group['reached'])}/{len(group['declared'])} {action}")
for line in expected:
    assert lines.count(line) == 1, (line, lines)
status = "ok" if summary["fuzz"]["ok"] else "fail"
assert sum(line.startswith(f"scuzz fuzz {status} (") for line in lines) == 1
assert any(line.startswith(f"scuzz fuzz {status} ({summary['fuzz']['search']} search, {summary['fuzz']['search_failures']} search failures,") for line in lines)
PY_CHECK
}

# Mutation probes need terminal rejection evidence.
python3 - <<'PY_MUTATION'
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import time

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
root = Path(tempfile.mkdtemp(prefix='scuzz-mutation-status-'))
passed = False
try:
    (root / 'src').mkdir()
    (root / 'corpus').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname = "mutation-status"\n[fuzz]\nscore_floor = 0\n')
    (root / 'run.scuzz_scenario').write_text('def setup(): IO[Unit] = IO.pure(())\ndef exercise(): IO[Unit] = Main.probe()\n')
    (root / 'corpus/run.toml').write_text('[fuzz]\nevents = ["drive exercise"]\n')
    verify = root / 'run.scuzz_verify'
    verify.write_text('def completes(t: Timeline): Verdict = Verdict.every(t, i => !Timeline.driveHas(t, i, "exercise") || i >= 0)\n')
    def command(args, env=None, ok=True):
        result = subprocess.run([str(cli), *map(str, args)], env=env, capture_output=True, text=True, timeout=180)
        assert (result.returncode == 0) == ok, (args, result.stdout[-4000:], result.stderr[-4000:])
        return result
    def campaign(engine, body, expected, extra_env=None):
        (root / 'src/Main.scuzz').write_text('def choose(): Bool = true\ndef probe(): IO[Unit] = if (choose()) IO.pure(()) else ' + body + '\n@main def main: IO[Unit] = IO.pure(())\n')
        command(['fmt', root])
        shutil.rmtree(root / '.scuzz', ignore_errors=True)
        shutil.rmtree(root / 'build/fuzz/mutate', ignore_errors=True)
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        if extra_env:
            env.update(extra_env)
        started = time.monotonic()
        run = command(['fuzz', '--iterations', 1, '--seed', 0, root], env, expected != 'incomplete')
        if engine == 'evaluator':
            assert 'probes run compiled' not in run.stdout, run.stdout
        if extra_env and extra_env.get('SCUZZ_DEADLINE_PROOF') == '1':
            assert time.monotonic() - started >= 19, run.stdout
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        mut = report['mutate']
        assert mut['ran'] == 1, report
        assert mut['invalid'] == 0 and mut['excluded'] == 0, report
        assert mut['killed'] == (1 if expected == 'killed' else 0), report
        assert mut['incomplete'] == (1 if expected == 'incomplete' else 0), report
        assert report['scope']['required']['failed'] == 0 and report['scope']['required']['passed'] == 2, report
        assert mut['killed'] + mut['survived'] + mut['inert'] + mut['invalid'] + mut['excluded'] + mut['incomplete'] == mut['ran'], report
        if expected == 'incomplete':
            assert mut['score'] == 0 and report['fuzz']['ok'] is False, report
            assert 'mutation probes are incomplete' in run.stdout, run.stdout
            store = root / '.scuzz/mutate.results'
            assert 'incomplete' in store.read_text()
            again = command(['fuzz', '--iterations', 1, '--seed', 0, root], env, False)
            assert 'reused incomplete' not in again.stdout, again.stdout
        else:
            assert report['fuzz']['ok'] is True and mut['score'] == 1, report
            evidence = list((root / 'build/fuzz/mutate').rglob('rejection.txt'))
            assert any(path.read_text().startswith('v1\n') for path in evidence), evidence
            assert 'mutation source: Main.' in run.stdout and 'killed:' in run.stdout, run.stdout
        return run, report
    stall_source = root / 'stall.c'
    # Delay evidence publication past the unchanged host deadline.
    stall_source.write_text('''#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
char *getenv(const char *name) {
  size_t n = strlen(name);
  for (char **entry = environ; *entry; entry++) {
    if (strncmp(*entry, name, n) == 0 && (*entry)[n] == '=') {
      char *value = *entry + n + 1;
      if (strcmp(name, "SCUZZ_REJECTION_DUMP") == 0 && strstr(value, "/mutate/1/"))
        sleep(30);
      return value;
    }
  }
  return NULL;
}
''')
    stall = root / ('stall.dylib' if os.uname().sysname == 'Darwin' else 'stall.so')
    subprocess.run(['clang', '-dynamiclib' if os.uname().sysname == 'Darwin' else '-shared', '-fPIC', str(stall_source), '-o', str(stall)], check=True)
    stall_env = {'SCUZZ_DEADLINE_PROOF': '1', 'DYLD_INSERT_LIBRARIES' if os.uname().sysname == 'Darwin' else 'LD_PRELOAD': str(stall)}
    if os.uname().sysname == 'Darwin':
        stall_env['DYLD_FORCE_FLAT_NAMESPACE'] = '1'
    for engine in ('evaluator', 'compiled'):
        campaign(engine, 'Property.assert("terminal rule", false)', 'killed')
        campaign(engine, 'IO.pure(().require(false))', 'killed')
        campaign(engine, 'Fs.read("/definitely/missing").map(_ => ())', 'incomplete')
        campaign(engine, 'Property.assert("caught rule", false).handleErrorWith(_ => IO.pure(())).flatMap(_ => Fs.read("/definitely/missing").map(_ => ()))', 'incomplete')
        campaign(engine, 'IO.forever(IO.pure(()))', 'incomplete')
        campaign(engine, 'Property.assert("delayed rule", false)', 'incomplete', stall_env)
        verify.write_text('def noWrites(t: Timeline): Verdict = Verdict.every(t, i => !Timeline.effectHas(t, i, "Fs.write"))\n')
        campaign(engine, 'Fs.write("forbidden.txt", "write")', 'killed')
        verify.write_text('def completes(t: Timeline): Verdict = Verdict.every(t, i => !Timeline.driveHas(t, i, "exercise") || i >= 0)\n')
    heap = root / 'heap'
    (heap / 'src').mkdir(parents=True)
    (heap / 'scuzz.toml').write_text('[package]\nname = "heap-oracle"\n[fuzz]\nscore_floor = 0\n')
    (heap / 'src/Main.scuzz').write_text('record Box(flag: Bool)\ndef choose(): Bool = true\n@main def main: IO[Unit] = IO.pure(())\n')
    command(['fmt', heap])
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        for expression in ('[Main.choose()] == [true]', '(Main.choose(), 1) == (true, 1)', 'Result.Ok(Main.choose()) == Result.Ok(true)', 'Main.Box(Main.choose()) == Main.Box(true)'):
            (heap / 'heap.scuzz_verify').write_text('oracle heapEquality(): Bool = ' + expression + '\n')
            shutil.rmtree(heap / '.scuzz', ignore_errors=True)
            shutil.rmtree(heap / 'build/fuzz/mutate', ignore_errors=True)
            run = command(['fuzz', '--iterations', 1, '--seed', 0, heap], env)
            report = json.loads((heap / 'build/fuzz/summary.json').read_text())
            assert report['mutate']['killed'] == 1 and report['mutate']['incomplete'] == 0, report
            assert 'killed: oracle heapEquality rejected' in run.stdout, run.stdout
            if engine == 'evaluator':
                assert 'probes run compiled' not in run.stdout, run.stdout
    (heap / 'src/Main.scuzz').write_text('def checked(): Int = (0).require(value => value == 0)\n@main def main: IO[Unit] = IO.pure(())\n')
    (heap / 'heap.scuzz_verify').write_text('oracle checkedValue(): Bool = Main.checked() == 0\n')
    command(['fmt', heap])
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        shutil.rmtree(heap / '.scuzz', ignore_errors=True)
        shutil.rmtree(heap / 'build/fuzz/mutate', ignore_errors=True)
        run = command(['fuzz', '--iterations', 1, '--seed', 0, heap], env)
        report = json.loads((heap / 'build/fuzz/summary.json').read_text())
        assert report['mutate']['sites'] == 1 and report['mutate']['killed'] == 1, report
        assert 'killed: property check failed: require' in run.stdout, run.stdout
        if engine == 'evaluator':
            assert 'probes run compiled' not in run.stdout, run.stdout
    shutil.rmtree(heap)
    # A failed native link is incomplete. It does not become an invalid source.
    tools = root / 'tools'
    tools.mkdir()
    compiler = shutil.which('clang')
    assert compiler
    wrapper = tools / 'clang'
    wrapper.write_text('#!/bin/sh\ncase "$*" in *"/fuzz/mutate/"*) echo "link probe failure" >&2; exit 1;; esac\nexec ' + shlex.quote(compiler) + ' "$@"\n')
    wrapper.chmod(0o755)
    campaign('compiled', 'Property.assert("terminal rule", false)', 'incomplete', {'PATH': str(tools) + os.pathsep + os.environ['PATH']})
    passed = True
    print('Mutation status: terminal rejections, caught errors, real deadlines, link failures, and cache reuse checked')
finally:
    if passed:
        shutil.rmtree(root)
    else:
        print('Mutation status artifacts:', root)
PY_MUTATION

# Workload fields retain their values with optional TOML spaces.
python3 - <<'PY_FIELDS'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
layouts = [
    '[fuzz]\nschedule_seed = "42"\nfault_seed = "0"\nschedule_picks = ["setup 1:-", "writeValue 2:-"]\nevents = ["drive writeValue 7"]\n',
    '[fuzz]\nschedule_seed="42"\nfault_seed="0"\nschedule_picks=["setup 1:-", "writeValue 2:-"]\nevents=["drive writeValue 7"]\n',
    '[fuzz]\n# events=["drive writeValue 8"]\nschedule_seed\t=\t"42"\nfault_seed ="0"\nschedule_picks= [\n"setup 1:-", # "ignored"\n"writeValue 2:-"\n]\nevents=\n[\n"drive writeValue 7", # "drive writeValue 8"\n]\n',
]
with tempfile.TemporaryDirectory(prefix='scuzz-corpus-fields-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'corpus').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="corpus-fields"\n')
    (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
    (root / 'run.scuzz_scenario').write_text('def setup(): IO[Unit] = IO.pure(())\ndef writeValue(n: Int): IO[Unit] = Fs.write("value.txt", Str.fromInt(n))\n')
    (root / 'run.scuzz_verify').write_text('def contents(t: Timeline): Verdict = Verdict.every(t, i => !Timeline.driveHas(t, i, "writeValue") || Timeline.fileTextIs(t, i, "value.txt", "7"))\n')
    subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        for source in layouts:
            (root / 'corpus/run.toml').write_text(source)
            run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
            assert run.returncode == 0, (run.stdout, run.stderr)
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            row = next(row for row in report['scope']['workloads'] if row['label'].endswith('run.toml'))
            assert row['script'] == 'drive writeValue 7\n', row
            assert row['schedule'] == '42' and row['fault'] == '0' and row['schedule_picks'] == ['setup 1:-', 'writeValue 2:-'], row
            assert row['status'] == 'passed' and report['scope']['claims']['reached'] == ['contents'], report
            if engine == 'evaluator':
                assert 'probes run compiled' not in run.stdout, run.stdout
        (root / 'corpus/run.toml').write_text(layouts[1].replace('drive writeValue 7', 'drive writeValue 8'))
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
        assert run.returncode != 0 and 'claim contents rejected' in run.stdout + run.stderr, (run.stdout, run.stderr)
print('Corpus fields: layouts retain scripts, seeds, and scheduler picks; both engines reject wrong contents')
PY_FIELDS

# Mutation probes retain each workload's recorded scheduler choices.
python3 - <<'PY_MUT_PICKS'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-mutation-picks-') as directory:
    root = Path(directory)
    shutil.copytree('examples/bad-sched', root / 'app')
    app = root / 'app'
    (app / 'scuzz.toml').write_text('[package]\nname = "mutation-picks"\n[fuzz]\nscore_floor = 0.000\n')
    (app / 'order.scuzz_verify').write_text('def accepts(t: Timeline): Verdict = Verdict.every(t, i => true)\n')
    (app / 'src/Main.scuzz').write_text('def value(): Int = 0\n@main def main: IO[Unit] = IO.println(Str.fromInt(value()))\n')
    subprocess.run([str(cli), 'fmt', str(app)], check=True, capture_output=True)
    corpus = app / 'corpus/d037d00bc981a2fb.toml'
    pinned = corpus.read_text().replace('checkOrder 4:4,4', 'checkOrder 4:3,3')
    expected = ['setup 1:-', 'checkOrder 4:3,3']
    for engine in ('evaluator', 'compiled'):
        shutil.rmtree(app / 'build', ignore_errors=True)
        shutil.rmtree(app / '.scuzz', ignore_errors=True)
        env = dict(os.environ, SCUZZ_FUZZ_ENGINE=engine)
        corpus.write_text('[fuzz]\nschedule_seed = "1"\nevents = ["drive checkOrder"]\n')
        control = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(app)], env=env, text=True, capture_output=True, timeout=240)
        assert control.returncode == 0, (control.stdout, control.stderr)
        control_report = json.loads((app / 'build/fuzz/summary.json').read_text())
        control_row = next(row for row in control_report['scope']['workloads'] if row['label'].endswith('.toml'))
        assert control_row['schedule_picks'] != expected, control_row
        corpus.write_text(pinned)
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '8' , str(app)], env=env, text=True, capture_output=True, timeout=240)
        assert run.returncode == 0, (run.stdout, run.stderr)
        report = json.loads((app / 'build/fuzz/summary.json').read_text())
        assert report['mutate']['ran'] > 0, report
        base = app / 'build/fuzz/mutate/baseline/0/fuzz/schedule.picks'
        assert base.read_text().splitlines() == expected, (engine, base.read_text())
        replay_input = tomllib.loads((base.parents[1] / 'replay.toml').read_text())['fuzz']
        assert replay_input['schedule_picks'] == expected and replay_input['schedule_seed'] == '1', replay_input
        replay = list((app / 'build/fuzz/mutate').glob('[0-9]*/0/fuzz/schedule.picks'))
        assert replay, (engine, run.stdout)
        for path in replay:
            assert path.read_text().splitlines() == expected, (engine, path, path.read_text())
        if engine == 'evaluator':
            assert 'probes run compiled' not in run.stdout, run.stdout
print('Mutation replay: both engines retain recorded scheduler choices in baseline and mutant probes')
PY_MUT_PICKS

# Mutation cache entries identify the complete recorded campaign input.
python3 - <<'PY_MUT_IDENTITY'
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-mutation-identity-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'corpus').mkdir()
    manifest = root / 'scuzz.toml'
    source = root / 'src/Main.scuzz'
    rule = root / 'run.scuzz_verify'
    scenario = root / 'run.scuzz_scenario'
    corpus = root / 'corpus/run.toml'
    manifest.write_text('[package]\nname = "mutation-identity"\n[fuzz]\nscore_floor = 0.000\n')
    source.write_text('def value(): Int = 0\ndef unused(): Int = 1\n@main def main: IO[Unit] = IO.println(Str.fromInt(value()))\n')
    scenario.write_text('def setup(): IO[Unit] = IO.pure(())\ndef mark(n: Int): IO[Unit] = Fs.write("value.txt", Str.fromInt(n))\n')
    rule.write_text('def accepts(t: Timeline): Verdict = Verdict.every(t, i => true)\n')
    corpus.write_text('[fuzz]\nschedule_seed="1"\nevents=["drive mark 0"]\n')
    subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
    engine = 'compiled'
    budget = '1'
    seed = '7'
    environment_seed = '7'

    def campaign():
        env = dict(os.environ, SCUZZ_FUZZ_ENGINE=engine, SCUZZ_RAND_SEED=environment_seed)
        run = subprocess.run([str(cli), 'fuzz', '--iterations', budget, '--seed', seed, str(root)], env=env, text=True, capture_output=True, timeout=240)
        assert run.returncode == 0, (run.stdout, run.stderr)
        inputs = (root / 'build/fuzz/mutation-input.json').read_bytes()
        record = json.loads(inputs)
        key = hashlib.sha256(inputs).hexdigest()
        store = (root / '.scuzz/mutate.results').read_text()
        assert store.startswith('v 2\ninput ' + key + '\n'), store
        assert record['sources'] and record['manifests'] and record['native'], record
        assert record['program'] == hashlib.sha256((root / 'build/mutation-identity').read_bytes()).hexdigest(), record
        assert record['live_program'] == hashlib.sha256((root / 'build/live/mutation-identity').read_bytes()).hexdigest(), record
        for artifact, digest in record['native']:
            assert digest == hashlib.sha256(Path(artifact).read_bytes()).hexdigest(), (artifact, digest)
        if not (root / '.scuzz/mutation-selection-proof').exists():
            assert 'mutation source: Main.value:' in run.stdout, run.stdout
            (root / '.scuzz/mutation-selection-proof').write_text('ok')
        reused = bool(re.search(r'mutant \d+: reused ', run.stdout))
        return key, reused

    old, reused = campaign()
    assert not reused
    same, reused = campaign()
    assert same == old and reused

    changes = [
        (rule, 'true', 'i >= 0'),
        (source, 'IO.println(Str.fromInt(value()))', 'IO.println(Str.concat(Str.fromInt(value()), ""))'),
        (corpus, 'drive mark 0', 'drive mark 1'),
        (corpus, 'schedule_seed="1"', 'schedule_seed="2"'),
        (corpus, 'events=', 'schedule_picks=["setup 1:-", "mark 2:-"]\nevents='),
        (manifest, 'name = "mutation-identity"', 'name = "mutation-identity"\nversion = "0.1.1"'),
        (scenario, 'Fs.write("value.txt", Str.fromInt(n))', 'Fs.write("value.txt", Str.concat(Str.fromInt(n), ""))'),
    ]
    for path, before, after in changes:
        text = path.read_text()
        assert before in text, (path, text)
        path.write_text(text.replace(before, after, 1))
        subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
        new, reused = campaign()
        assert new != old and not reused, (path, old, new, reused)
        old = new
    dependency = root / 'dependency'
    (dependency / 'src').mkdir(parents=True)
    dep_manifest = dependency / 'scuzz.toml'
    dep_source = dependency / 'src/Helper.scuzz'
    dep_manifest.write_text('[package]\nname = "helper"\n')
    dep_source.write_text('def answer(): Int = 7\n')
    manifest.write_text(manifest.read_text() + '[dependencies]\nhelper = { path = "dependency" }\n')
    new, reused = campaign()
    assert new != old and not reused
    old = new
    for path, addition in ((dep_manifest, 'version = "0.1.1"\n'), (dep_source, '\ndef second(): Int = 8\n')):
        path.write_text(path.read_text() + addition)
        subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
        new, reused = campaign()
        assert new != old and not reused
        old = new
    budget = '9'
    new, reused = campaign()
    assert new != old and not reused
    old = new
    seed = '43'
    new, reused = campaign()
    assert new != old and not reused
    old = new
    environment_seed = '43'
    new, reused = campaign()
    assert new != old and not reused
    old = new
    engine = 'evaluator'
    new, reused = campaign()
    assert new != old and not reused
    same, reused = campaign()
    assert same == new and reused
print('Mutation identity: equal inputs reuse results; rule, caller, corpus, choices, manifest, dependency, driver, limits, environment, and engine changes clear results')
PY_MUT_IDENTITY

# Author rule evidence counts executed rules.
python3 - <<'PY_GUARDS'
import json
import os
from pathlib import Path
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
cases = [
    ('plain', '@main def main: IO[Unit] = IO.pure(())\n', [], []),
    ('unused', '@main def main: IO[Unit] = for { _ = Property.assert("unused", false)\n _ <- IO.pure(()) } yield ()\n', [], []),
    ('uncalled', 'def rule(): IO[Unit] = Property.assert("uncalled", true)\n@main def main: IO[Unit] = IO.pure(())\n', [], []),
    ('assert', '@main def main: IO[Unit] = Property.assert("active", true)\n', ['active'], []),
    ('check', '@main def main: IO[Unit] = IO.println(Str.fromInt((1).require("positive", n => n == 1)))\n', ['positive'], []),
    ('assume', '@main def main: IO[Unit] = for { _ = Property.assume("assumed", true)\n _ <- IO.pure(()) } yield ()\n', [], []),
    ('caught', '@main def main: IO[Unit] = Property.assert("caught", false).handleErrorWith(_ => IO.pure(()))\n', ['caught'], []),
    ('escaped', '@main def main: IO[Unit] = Property.assert("line\\nquoted \\"name\\"", true)\n', ['line\nquoted "name"'], []),
    ('claim', '@main def main: IO[Unit] = IO.pure(())\n', [], ['accepts']),
]
with tempfile.TemporaryDirectory(prefix='scuzz-guard-evidence-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="guard-evidence"\n')
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ, SCUZZ_FUZZ_ENGINE=engine)
        for label, source, properties, claims in cases:
            (root / 'src/Main.scuzz').write_text(source)
            rule = root / 'run.scuzz_verify'
            if claims:
                rule.write_text('def accepts(t: Timeline): Verdict = Verdict.every(t, i => true)\n')
            else:
                rule.unlink(missing_ok=True)
            formatted = subprocess.run([str(cli), 'fmt', str(root)], text=True, capture_output=True)
            assert formatted.returncode == 0, (label, formatted.stdout, formatted.stderr)
            run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
            assert run.returncode == 0, (engine, label, run.stdout, run.stderr)
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            expected = {'status': 'guarded' if properties or claims else 'unguarded', 'properties': properties, 'claims': claims}
            assert report['guards'] == expected, (engine, label, report['guards'], expected)
            assert report['scope']['required']['passed'] == 1, report
            assert all(not row['location'].startswith('R:') for row in report['coverage']['regions']), report
            if engine == 'evaluator':
                assert 'probes run compiled' not in run.stdout, (label, run.stdout)
        rule.unlink(missing_ok=True)
        (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
        (root / 'run.scuzz_scenario').write_text('def setup(): IO[Unit] = IO.pure(())\ndef mirror(value: String): IO[Unit] = IO.println(value)\n')
        (root / 'corpus').mkdir(exist_ok=True)
        (root / 'corpus/run.toml').write_text('[fuzz]\nevents=[' + json.dumps('drive mirror "hello"') + ']\n')
        subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
        assert run.returncode == 0, (run.stdout, run.stderr)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert report['guards'] == {'status': 'unguarded', 'properties': [], 'claims': []}, (engine, report)
        (root / 'run.scuzz_scenario').unlink()
        (root / 'corpus/run.toml').unlink()
        (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = Property.assert("rejects", false)\n')
        rule.unlink(missing_ok=True)
        subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
        assert run.returncode != 0 and 'oracle rejects rejected' in run.stdout + run.stderr, (engine, run.stdout, run.stderr)
print('Author rules: both engines distinguish execution from unused assertions and assumptions; active assertions, checks, caught assertions, and claims retain names')
PY_GUARDS

# Workload reports distinguish failure from incomplete execution.
python3 - <<'PY_STATUS'
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-workload-status-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'corpus').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="workload-status"\n')
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ, SCUZZ_FUZZ_ENGINE=engine)
        for source, status in (
            ('@main def main: IO[Unit] = IO.pure(())\n', 'passed'),
            ('@main def main: IO[Unit] = Property.assert("rejects", false)\n', 'failed'),
            ('@main def main: IO[Unit] = IO.println(Str.fromInt((0).require("positive", n => n > 0)))\n', 'failed'),
            ('def loop(): IO[Unit] = if (false) IO.pure(()) else IO.forever(IO.pure(()))\n@main def main: IO[Unit] = loop()\n', 'incomplete'),
            ('def failure(): IO[Unit] = if (false) IO.pure(()) else IO.fail("ordinary error")\n@main def main: IO[Unit] = failure()\n', 'failed'),
        ):
            (root / 'src/Main.scuzz').write_text(source)
            subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
            run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
            assert (run.returncode == 0) == (status == 'passed'), (engine, status, run.stdout, run.stderr)
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            assert report['status'] == status, (engine, status, report)
            assert 'scope' in report, (engine, source, run.stdout, run.stderr, report)
            assert report['scope']['required'][status] == 1, report
            assert report['scope']['workloads'][0]['status'] == status, report
        (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
        (root / 'run.scuzz_scenario').write_text('def setup(): IO[Unit] = IO.pure(())\ndef hang(): IO[Unit] = IO.forever(IO.pure(()))\n')
        (root / 'corpus/run.toml').write_text('[fuzz]\nevents=["drive hang"]\n')
        subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert run.returncode != 0 and report['status'] == 'incomplete', (run.stdout, run.stderr, report)
        assert report['scope']['required']['passed'] == 1 and report['scope']['required']['incomplete'] == 1 and report['scope']['required']['failed'] == 0, report
        (root / 'run.scuzz_scenario').unlink()
        (root / 'corpus/run.toml').unlink()
        (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.println(7)\n')
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=180)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert run.returncode != 0 and report['status'] == 'incomplete' and 'fuzz' not in report, report
    (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
    subprocess.run([str(cli), 'fmt', str(root)], check=True, capture_output=True)
    shim_source = root / 'deadline.c'
    shim_source.write_text(r'''#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
char *getenv(const char *name) {
  size_t n = strlen(name);
  for (char **entry = environ; *entry; entry++) {
    if (strncmp(*entry, name, n) == 0 && (*entry)[n] == '=') {
      if (strcmp(name, "SCUZZ_RAND_SEED") == 0)
        sleep(30);
      return *entry + n + 1;
    }
  }
  return NULL;
}
''')
    mac = os.uname().sysname == 'Darwin'
    shim = root / ('deadline.dylib' if mac else 'deadline.so')
    subprocess.run(['clang', '-dynamiclib' if mac else '-shared', '-fPIC', str(shim_source), '-o', str(shim)], check=True)
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ, SCUZZ_FUZZ_ENGINE=engine, SCUZZ_RAND_SEED='1')
        env['DYLD_INSERT_LIBRARIES' if mac else 'LD_PRELOAD'] = str(shim)
        if mac:
            env['DYLD_FORCE_FLAT_NAMESPACE'] = '1'
        started = time.monotonic()
        run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, text=True, capture_output=True, timeout=90)
        elapsed = time.monotonic() - started
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert run.returncode != 0 and report['status'] == 'incomplete', (run.stdout, run.stderr, report)
        assert report['scope']['required']['incomplete'] == 1 and report['scope']['required']['failed'] == 0, report
        assert '124' in report['scope']['workloads'][0]['reason'], report
        assert 18 <= elapsed < 35, elapsed
        if engine == 'compiled':
            replay = root / 'deadline.toml'
            replay.write_text('[fuzz]\nevents=[]\n')
            started = time.monotonic()
            run = subprocess.run([str(cli), 'fuzz', '--replay', str(replay), str(root)], env=env, text=True, capture_output=True, timeout=90)
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            assert run.returncode != 0 and report['kind'] == 'replay' and report['status'] == 'incomplete', report
            assert report['scope']['required']['incomplete'] == 1 and '124' in report['scope']['workloads'][0]['reason'], report
            assert 'fuzz replay incomplete' in run.stdout and 'reproduced a failure' not in run.stdout, run.stdout
            assert 18 <= time.monotonic() - started < 35
print('Workload status: both engines retain rule failures, distinguish bounded and deadline probes, and replace stale reports before a source failure')
PY_STATUS

# Replay reports describe one executed input.
python3 - <<'PY_REPLAY'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-replay-report-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="replay-report"\n')
    main = root / 'src/Main.scuzz'
    replay = root / 'replay.toml'
    replay.write_text('[fuzz]\nschedule_seed="19"\nfault_seed="23"\nevents=[]\n')

    def command(args, ok, env=None):
        run = subprocess.run([str(cli), *map(str, args)], env=env, text=True, capture_output=True, timeout=180)
        assert (run.returncode == 0) == ok, (args, run.stdout, run.stderr)
        return run

    def report(status, extra=(), properties=(), mode='simulation'):
        command(['fmt', root], True)
        run = command(['fuzz', *extra, '--replay', replay, root], status == 'passed')
        data = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert data['kind'] == 'replay' and data['status'] == status, (data, run.stdout)
        assert data['input'] == str(replay) and data['mode'] == mode and data['asan'] is False, data
        assert data['scope']['required'] == dict(passed=int(status == 'passed'), failed=int(status == 'failed'), excluded=int(status == 'excluded'), incomplete=int(status == 'incomplete'), planned=1, unrun=0), data
        assert data['scope']['search'] == dict(passed=0, failed=0, excluded=0, incomplete=0), data
        row, = data['scope']['workloads']
        assert row['status'] == status and row['engine'] == 'compiled' and row['required'], row
        assert row['script'] == '' and row['schedule'] == '19' and row['fault'] == '23', row
        assert data['declared_schedule_picks'] == [], data
        assert data['guards']['properties'] == list(properties) and data['guards']['claims'] == [], data
        assert data['guards']['status'] == ('guarded' if properties else 'unguarded'), data
        assert ('author rules: guarded' if properties else 'author rules: unguarded') in run.stdout, run.stdout
        expected = {'passed': 'fuzz replay ok', 'failed': 'fuzz replay reproduced a failure', 'excluded': 'fuzz replay excluded', 'incomplete': 'fuzz replay incomplete'}[status]
        assert expected in run.stdout, run.stdout
        return data

    for source, status, properties in (
        ('@main def main: IO[Unit] = IO.pure(())\n', 'passed', ()),
        ('@main def main: IO[Unit] = Property.assert("active", true)\n', 'passed', ('active',)),
        ('@main def main: IO[Unit] = Property.assert("rejects", false)\n', 'failed', ('rejects',)),
        ('@main def main: IO[Unit] = for { _ = Property.assume("excluded input", false)\n _ <- IO.pure(()) } yield ()\n', 'excluded', ()),
        ('def loop(): IO[Unit] = if (false) IO.pure(()) else IO.forever(IO.pure(()))\n@main def main: IO[Unit] = loop()\n', 'incomplete', ()),
        ('def failure(): IO[Unit] = if (false) IO.pure(()) else IO.fail("ordinary error")\n@main def main: IO[Unit] = failure()\n', 'failed', ()),
    ):
        main.write_text(source)
        report(status, properties=properties)

    main.write_text('@main def main: IO[Unit] = IO.pure(())\n')
    report('passed', ('--live',), mode='live')
    report('passed', ('--session',), mode='session')
    rule = root / 'run.scuzz_verify'
    rule.write_text('def accepts(t: Timeline): Verdict = Verdict.every(t, i => true)\n')
    command(['fmt', root], True)
    command(['fuzz', '--replay', replay, root], True)
    data = json.loads((root / 'build/fuzz/summary.json').read_text())
    assert data['guards'] == dict(status='guarded', properties=[], claims=['accepts']), data
    rule.unlink()
    for failure in ('source', 'input', 'link'):
        report('passed')
        args = ['fuzz', '--replay', replay, root]
        env = None
        if failure == 'source':
            main.write_text('@main def main: IO[Unit] = missing()\n')
        elif failure == 'input':
            args = ['fuzz', '--replay', root / 'absent.toml', root]
        else:
            (root / 'build/replay-report').unlink(missing_ok=True)
            shim = root / 'tools'
            shim.mkdir()
            (shim / 'clang').write_text('#!/bin/sh\nexit 9\n')
            (shim / 'clang').chmod(0o755)
            env = dict(os.environ, PATH=str(shim) + os.pathsep + os.environ['PATH'])
            main.write_text('@main def main: IO[Unit] = IO.println(12345)\n')
            command(['fmt', root], True)
        command(args, False, env)
        data = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert data['kind'] == 'replay' and data['status'] == 'incomplete', data
        assert data['scope']['required']['unrun'] == 1 and data['scope']['workloads'] == [], data
        main.write_text('@main def main: IO[Unit] = IO.pure(())\n')

    app = root / 'scheduled'
    shutil.copytree('examples/bad-sched', app, ignore=shutil.ignore_patterns('build'))
    pinned = app / 'corpus/d037d00bc981a2fb.toml'
    command(['fuzz', '--replay', pinned, app], False)
    data = json.loads((app / 'build/fuzz/summary.json').read_text())
    assert data['status'] == 'failed' and data['declared_schedule_picks'] == ['setup 1:-', 'checkOrder 4:4,4'], data
    row, = data['scope']['workloads']
    assert row['schedule_picks'] == data['declared_schedule_picks'] and row['script'] == 'drive checkOrder\n', row
    assert data['guards']['status'] == 'guarded', data
print('Replay reports: verdicts, modes, rule execution, choices, and stale report replacement pass')
PY_REPLAY

# Claim execution and trigger reach have separate results.
python3 - <<'PY_REACH'
import json
import os
from pathlib import Path
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-claim-reach-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="claim-reach"\n[fuzz]\nscore_floor=0\n')
    main = root / 'src/Main.scuzz'
    main.write_text('def identity(n: Int): Int = n\n@main def main: IO[Unit] = IO.pure(())\n')
    verify = root / 'reach.scuzz_verify'
    verify.write_text('''oracle identity(n: Int): Bool =
  Main.identity(n) == n

def conditional(t: Timeline): Verdict =
  Verdict.afterHit(t, "button:Missing", "text:ready")
''')
    def command(args, env=None, ok=True):
        run = subprocess.run([str(cli), *map(str, args)], env=env,
                             capture_output=True, text=True, timeout=180)
        assert (run.returncode == 0) == ok, (args, run.stdout[-4000:], run.stderr[-4000:])
        return run
    command(['fmt', root])
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        def campaign(iterations, ok):
            run = command(['fuzz', '--iterations', iterations, '--seed', 42, root], env, ok)
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            assert report['fuzz']['ok'] == ok, report
            return run, report
        _, partial = campaign(0, True)
        assert partial['scope']['claims']['declared'] == ['conditional'], partial
        assert partial['scope']['claims']['reached'] == ['conditional'], partial
        assert partial['triggers']['declared'] == ['button:Missing'], partial
        assert partial['triggers']['reached'] == [], partial
        assert partial['triggers']['never'] == ['button:Missing'], partial
        run, blocked = campaign(8, False)
        assert blocked['scope']['claims']['reached'] == ['conditional'], blocked
        assert blocked['triggers']['never'] == ['button:Missing'], blocked
        assert 'trigger never fired: button:Missing' in run.stdout, run.stdout

    verify.write_text('''oracle identity(n: Int): Bool =
  Main.identity(n) == n

def noWrites(t: Timeline): Verdict =
  Verdict.every(t, i => !Timeline.effectHas(t, i, "Fs.write"))
''')
    command(['fmt', root])
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        main.write_text('def identity(n: Int): Int = n\n@main def main: IO[Unit] = IO.pure(())\n')
        command(['fmt', root])
        _, passed = campaign(0, True)
        assert passed['scope']['claims']['reached'] == ['noWrites'], passed
        main.write_text('def identity(n: Int): Int = n\n@main def main: IO[Unit] = Fs.write("forbidden.txt", "changed")\n')
        command(['fmt', root])
        run, rejected = campaign(0, False)
        assert rejected['scope']['required']['failed'] == 1, rejected
        assert rejected['scope']['required']['unrun'] > 0, rejected
        assert rejected['scope']['workloads'][0]['label'] == 'idle', rejected
        assert rejected['scope']['workloads'][0]['status'] == 'failed', rejected
        assert rejected['scope']['claims']['reached'] == ['noWrites'], rejected
        assert 'claim noWrites rejected' in run.stdout + run.stderr, (run.stdout, run.stderr)
        if engine == 'evaluator':
            assert 'claim noWrites rejected' in (root / 'build/fuzz/ev/probe.log').read_text()
    # Idle UI workloads contain no implicit tap.
    (root / 'scuzz.toml').write_text('[package]\nname="claim-reach"\n[ui]\ndefault_runtime="headless"\n[fuzz]\nscore_floor=0\n')
    main.write_text('@main def main: IO[Unit] = Ui.run(_ => View.text("idle"))\n')
    verify.write_text('def conditional(t: Timeline): Verdict =\n  Verdict.afterHit(t, "button:Missing", "text:ready")\n')
    command(['fmt', root])
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        command(['fuzz', '--iterations', 0, root], env)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        idle = report['scope']['workloads'][0]
        assert idle['label'] == 'idle' and idle['script'] == '' and idle['status'] == 'passed', report
        assert report['triggers']['never'] == ['button:Missing'], report
print('Claim reach and absence rules: both engines separate execution, reach, and rejection')
PY_REACH

# Excluded inputs have separate results on both engines.
python3 - <<'PY_SCOPE'
import json
import os
from pathlib import Path
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()

def command(args, env=None, ok=True):
    result = subprocess.run([str(cli), *map(str, args)], env=env, capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (args, result.stdout[-4000:], result.stderr[-4000:])
    return result

with tempfile.TemporaryDirectory(prefix='scuzz-scope-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'corpus').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname = "scope"\n\n[fuzz]\nscore_floor = 0\n')
    source = '''def accepts(n: Int): Bool =
  guarded(Property.assume("nonnegative input", n >= 0), n)

def guarded(_u: Unit, n: Int): Bool =
  n >= 0

@main def main: IO[Unit] =
  IO.pure(())
'''
    main = root / 'src/Main.scuzz'
    main.write_text(source)
    verify = root / 'input.scuzz_verify'
    verify.write_text('''oracle check(n: Int): Bool =
  Main.accepts(n)

def history(t: Timeline): Verdict =
  Verdict.every(t, i => i >= 0)
''')
    command(['fmt', root])

    def campaign(iterations, engine=None, ok=True, extra=()):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine:
            env['SCUZZ_FUZZ_ENGINE'] = engine
        result = command(['fuzz', '--iterations', iterations, '--seed', 41, *extra, root], env, ok)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        scope = report['scope']
        for required, key in ((True, 'required'), (False, 'search')):
            rows = [row for row in scope['workloads'] if row['required'] == required]
            for status in ('passed', 'failed', 'excluded', 'incomplete'):
                assert scope[key][status] == sum(row['status'] == status for row in rows), scope
            if required:
                assert len(rows) + scope[key]['unrun'] == scope[key]['planned']
        assert len([row for row in scope['workloads'] if not row['required']]) == report['fuzz']['search']
        assert scope['search']['failed'] + scope['search']['incomplete'] == report['fuzz']['search_failures']
        assert 'scope: required ' in result.stdout
        return report

    # Both engines identify the same excluded search inputs.
    reports = [campaign(16, engine) for engine in (None, 'compiled')]
    for report in reports:
        scope = report['scope']
        assert scope['required']['excluded'] == 0
        assert scope['search']['excluded'] > 0, scope
        assert scope['search']['passed'] > 0, scope
        assert scope['search']['failed'] == 0
        assert scope['claims']['declared'] == ['history'], scope
        assert scope['claims']['reached'] == ['history'], scope
        assert scope['inputs'] == ['check i'], scope
        excluded = [row for row in scope['workloads'] if row['status'] == 'excluded']
        assert all(row['reason'] == 'nonnegative input' and 'drive check' in row['script'] for row in excluded)
        assert report['mutate']['killed'] + report['mutate']['survived'] + report['mutate']['inert'] + report['mutate']['invalid'] + report['mutate']['excluded'] + report['mutate']['incomplete'] == report['mutate']['ran']
    assert any(report['scope']['mutant_excluded'] for report in reports), reports
    for report in reports:
        mut = report['mutate']
        assert mut['excluded'] == len(report['scope']['mutant_excluded'])
        count = mut['ran'] - mut['invalid'] - mut['inert']
        if count > 0:
            assert abs(mut['score'] - mut['killed'] / count) < 0.001, mut
    manifest = root / 'scuzz.toml'
    saved_manifest = manifest.read_text()
    manifest.write_text(saved_manifest.replace('score_floor = 0', 'score_floor = 1'))
    strict = campaign(16, 'compiled', ok=False)
    assert strict['mutate']['excluded'] > 0 and strict['mutate']['score'] < 1, strict
    manifest.write_text(saved_manifest)

    assert {row['engine'] for row in reports[0]['scope']['workloads'] if not row['required']} == {'evaluator'}
    assert {row['engine'] for row in reports[1]['scope']['workloads']} == {'compiled'}
    assert [(row['script'], row['status']) for row in reports[0]['scope']['workloads']] == [(row['script'], row['status']) for row in reports[1]['scope']['workloads']]

    # A corpus exclusion blocks the campaign. Later entries stay unrun.
    rejected = root / 'corpus/00-excluded.toml'
    rejected.write_text('[fuzz]\nevents = ["drive check -1"]\n')
    (root / 'corpus/01-pass.toml').write_text('[fuzz]\nevents = ["drive check 1"]\n')
    report = campaign(0, ok=False)
    assert report['scope']['required']['excluded'] == 1, report
    assert report['scope']['required']['unrun'] > 0, report
    assert report['corpus']['failures'] == 0, report
    report = campaign(8, ok=False, extra=['--no-fail-fast'])
    assert report['scope']['required']['excluded'] == 1
    assert report['fuzz']['search'] == 5
    assert report['fuzz']['ok'] is False
    command(['fuzz', '--replay', rejected, root], ok=False)
    for path in (root / 'corpus').iterdir():
        path.unlink()

    # Required seeds and idle probes cannot pass through an assumption.
    main.write_text(source.replace('n >= 0), n)', 'n > 0), n)'))
    command(['fmt', root])
    report = campaign(0, ok=False)
    assert report['scope']['required']['excluded'] == 1
    assert report['scope']['search']['excluded'] == 0
    main.write_text(source.replace('IO.pure(())', 'IO.println(Str.fromBool(accepts(-1)))'))
    command(['fmt', root])
    report = campaign(0, ok=False)
    assert report['scope']['workloads'][0]['label'] == 'idle', report
    assert report['scope']['workloads'][0]['status'] == 'excluded', report
    assert report['scope']['claims']['reached'] == [], report
    assert report['scope']['claims']['never'] == ['history'], report

    # The assumption does not validate live input on either engine.
    for action in ('run', 'eval'):
        result = command([action, root])
        assert 'false' in result.stdout, result

    # Empty and unsupported generator bounds fail the source check.
    saved_verify = verify.read_text()
    for bounds in ('n > 4 && n < 3', 'n == 1 && n == 2', 'n > 9223372036854775807', 'n + 1 > n'):
        verify.write_text('oracle check(n: Int where ' + bounds + '): Bool =\n  Main.accepts(n)\n')
        command(['fmt', root])
        result = command(['check', root], ok=False)
        assert 'generator-friendly' in result.stdout + result.stderr, result
    verify.write_text(saved_verify)

    # Assertions remain failures. An excluded shrink cannot replace one.
    main.write_text(source.replace('  n >= 0\n\n@main', '  n == 0\n\n@main'))
    command(['fmt', root])
    report = campaign(16, ok=False)
    assert report['scope']['search']['failed'] == 1, report
    repro = Path(report['fuzz']['repro'])
    result = command(['fuzz', '--replay', repro, root], ok=False)
    assert 'reproduced a failure' in result.stdout
    assert 'excluded input' not in result.stderr
    main.write_text(source.replace('Property.assume("nonnegative input", n >= 0)', 'Property.check("nonnegative input", n >= 0, ())'))
    command(['fmt', root])
    rejected.write_text('[fuzz]\nevents = ["drive check -1"]\n')
    report = campaign(0, ok=False)
    assert report['scope']['required']['failed'] == 1, report
    assert report['scope']['required']['excluded'] == 0, report

    # Headless UI search also separates assumptions from claim failures.
    for path in (root / 'corpus').iterdir():
        path.unlink()
    (root / 'scuzz.toml').write_text('[package]\nname = "scope"\n\n[ui]\ndefault_runtime = "headless"\n\n[fuzz]\nscore_floor = 0\n')
    main.write_text('''record CountA(value: Int)

record CountB(value: Int)

@main def main: IO[Unit] =
  for {
    count = Signal.make(CountB(0))
    _ <- Ui.run(_ => View.button("Step", _ => for {
  _ = Property.assume("one step per input", Signal.get(count).value == 0)
  _ = Signal.set(count, CountB(Signal.get(count).value + 1))
} yield ()))
  } yield ()
''')
    verify.write_text('''def history(t: Timeline): Verdict =
  Verdict.alwaysHas(t, "button:Step")
''')
    command(['fmt', root])
    ui_reports = [campaign(8, engine) for engine in (None, 'compiled')]
    for report in ui_reports:
        assert report['scope']['search']['excluded'] > 0, report
        assert report['scope']['claims']['reached'] == ['history'], report
        assert all(row['reason'] == 'one step per input' for row in report['scope']['workloads'] if row['status'] == 'excluded')
    assert {row['engine'] for row in ui_reports[0]['scope']['workloads'] if not row['required']} == {'evaluator'}
    assert [(row['script'], row['status']) for row in ui_reports[0]['scope']['workloads']] == [(row['script'], row['status']) for row in ui_reports[1]['scope']['workloads']]

print('Verification scope: search, corpus, seed, idle, Headless UI, live erasure, and failure replay pass')
PY_SCOPE

# Failed corpus entries and search iterations have separate counts.
search_counts_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-search-counts.XXXXXX")"
mkdir -p "$search_counts_dir/src" "$search_counts_dir/corpus"
cat > "$search_counts_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "search-counts"
MANIFEST
cat > "$search_counts_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 37

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$search_counts_dir/input.scuzz_verify" <<'CLAIMS'
oracle check(n: Int): Bool =
  Main.accepts(n)
CLAIMS
cat > "$search_counts_dir/corpus/rejected.toml" <<'CORPUS'
[fuzz]
events = ["drive check 37"]
CORPUS
if fuzz --iterations 8 "$search_counts_dir" > "$search_counts_dir/corpus.log" 2>&1; then
  echo "corpus failure must fail the campaign" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/corpus.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/corpus.json"
if fuzz --iterations 2 --no-fail-fast "$search_counts_dir" > "$search_counts_dir/continued.log" 2>&1; then
  echo "passing search must not hide a corpus failure" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/continued.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/continued.json"
rm "$search_counts_dir/corpus/rejected.toml"
python3 - "$search_counts_dir/src/Main.scuzz" <<'PY_CHANGE'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(p.read_text().replace("n != 37", "n == 0"))
PY_CHANGE
if fuzz --iterations 16 "$search_counts_dir" > "$search_counts_dir/search.log" 2>&1; then
  echo "search failure must fail the campaign" && exit 1
fi
assert_fuzz_summary "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/search.log"
cp "$search_counts_dir/build/fuzz/summary.json" "$search_counts_dir/search.json"
python3 - "$search_counts_dir" <<'PY_CHECK'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
for name, ran, failures, corpus_failures in (("corpus", 0, 0, 1), ("continued", 1, 0, 1), ("search", 2, 1, 0)):
    report = json.loads((root / f"{name}.json").read_text())
    assert report["fuzz"]["ok"] is False
    assert report["fuzz"]["search"] == ran, report["fuzz"]
    assert report["fuzz"]["search_failures"] == failures
    assert report["corpus"]["failures"] == corpus_failures
PY_CHECK
rm -rf "$search_counts_dir"

fuzz --iterations 16 examples/counter | tee /tmp/scuzz-counter-summary.log
assert_fuzz_summary examples/counter/build/fuzz/summary.json /tmp/scuzz-counter-summary.log
python3 - <<'PY'
import json
with open("examples/counter/build/fuzz/summary.json") as f:
    d = json.load(f)
br = d["breadth"]
assert d["fuzz"]["ok"] is True
assert "signals" in br["varied"], br
assert "count" in br["claimed"]["signalInt"], br
assert "signals" not in br["unclaimed"], br
assert d["mutate"]["score"] >= 0.5, d["mutate"]
assert d["coverage"]["reached"] > 0
assert d["sometimes"]["reached"]
PY
fuzz --iterations 16 examples/studio | tee /tmp/scuzz-studio-summary.log
assert_fuzz_summary examples/studio/build/fuzz/summary.json /tmp/scuzz-studio-summary.log
fuzz --relate examples/counter
if fuzz --relate examples/bad-sched; then
  echo "relate should have caught the schedule divergence" && exit 1
fi
# A failing search promotes its repro into <pkg>/corpus/. Run the campaign that
# must fail on a copy so the tracked corpus stays what a human committed.
rm -rf /tmp/bad-example
cp -R examples/bad-example /tmp/bad-example
rm -rf /tmp/bad-example/build
if fuzz --no-fail-fast --iterations 8 /tmp/bad-example; then
  echo "fuzz should have found the property failure" && exit 1
fi
test -f /tmp/bad-example/build/fuzz/repro.toml
test -f /tmp/bad-example/corpus/search-42-0.toml
python3 - <<'PY'
import json
with open("/tmp/bad-example/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] >= 1
assert d["corpus"]["entries"] >= 1
assert d["corpus"]["failures"] >= 1
assert d["scope"]["required"]["failed"] >= 1
assert d["mutate"]["ran"] == 0
assert d["mutate"]["killed"] == 0 and d["mutate"]["inert"] == 0
PY
if fuzz --replay /tmp/bad-example/build/fuzz/repro.toml /tmp/bad-example; then
  echo "replay should have reproduced the property failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-example; then
  echo "corpus-only should pin the bad-example failure" && exit 1
fi
fuzz --iterations 0 examples/kernel
grep -q 'drive addTwoThree' examples/kernel/build/seeds.txt
grep -q 'drive sumToTen' examples/kernel/build/seeds.txt
grep -q 'drive termDiff' examples/kernel/build/seeds.txt
grep -q 'termDiff e:N' examples/kernel/build/drivers.txt
fuzz --iterations 16 examples/kernel
fuzz --iterations 8 examples/scale
fuzz --iterations 8 examples/fmt

# A backward parser step rejects the formatter rule without a deadline.
python3 - <<'PY_ENUM_STEP'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-enum-step-') as directory:
    root = Path(directory)
    for name in ('fmt', 'syntax'):
        shutil.copytree(Path('examples') / name, root / name,
                        ignore=shutil.ignore_patterns('build', '.scuzz'))
    source = root / 'syntax/src/Parse.scuzz'
    text = source.read_text()
    for anchor in ('parseEnumCasesNext(parseEnumCase(toks, i + 1), name, ts, toks, acc, i)',
                   'parseEnumCase2(_identOf(_at(toks, i)), toks, i + 1)'):
        assert text.count(anchor) == 1
        source.write_text(text.replace(anchor, anchor.replace('i + 1', 'i - 1')))
        result = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root / 'fmt')],
                                capture_output=True, text=True, timeout=180)
        report = json.loads((root / 'fmt/build/fuzz/summary.json').read_text())
        assert result.returncode != 0 and report['status'] == 'failed', (result.stdout, result.stderr)
        assert report['scope']['required']['failed'] > 0 and report['scope']['required']['incomplete'] == 0
        assert 'fmtEnum' in report['guards']['properties'] and 'oracle fmtEnum rejected' in result.stdout + result.stderr
print('Enum parser progress: a backward step has terminal formatter rejection evidence')
PY_ENUM_STEP
# Generated compiler checks run in the regular verification path.
"$ROOT/scripts/ci.sh" compiler-cases
# Compiler corpus replays run in separate CI slices. Full campaigns use source copies.
# Set SCUZZ_COMPILER_FUZZ=1 for the search campaign.
compiler_campaigns() (
  campaign_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-compiler-fuzz.XXXXXX")"
  trap 'status=$?; if [ "$status" -eq 0 ]; then rm -rf "$campaign_dir"; else echo "Compiler campaign artifacts: $campaign_dir" >&2; fi' EXIT
  cp "$SCUZZ" "$campaign_dir/scuzz"
  for pkg in syntax compiler compiler-cases tyck codegen; do
    mkdir -p "$campaign_dir/$pkg"
    cp "examples/$pkg/scuzz.toml" "$campaign_dir/$pkg/"
    cp -R "examples/$pkg/src" "$campaign_dir/$pkg/"
    for claim in "examples/$pkg/"*.scuzz_verify; do
      if [ -f "$claim" ]; then
        cp "$claim" "$campaign_dir/$pkg/"
      fi
    done
    if [ -d "examples/$pkg/corpus" ]; then
      cp -R "examples/$pkg/corpus" "$campaign_dir/$pkg/"
    fi
  done
  "$campaign_dir/scuzz" fuzz --iterations 2 "$campaign_dir/tyck"
  "$campaign_dir/scuzz" fuzz --iterations 2 "$campaign_dir/codegen"
)
if [ "${SCUZZ_COMPILER_FUZZ:-0}" = 1 ]; then
  compiler_campaigns
fi
rm -rf /tmp/bad-seed
cp -R examples/bad-example /tmp/bad-seed
rm -rf /tmp/bad-seed/build /tmp/bad-seed/corpus
if fuzz --iterations 0 /tmp/bad-seed; then
  echo "zero-arg seed should fail bump with no stored corpus" && exit 1
fi
grep -q 'drive bump 0' /tmp/bad-seed/build/fuzz/repro.toml
if fuzz --iterations 0 examples/bad-fault; then
  echo "corpus-only should pin the bad-fault failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-adt; then
  echo "corpus-only should pin the bad-adt failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-sched; then
  echo "corpus-only should pin the bad-sched failure" && exit 1
fi
grep -q 'drive area Rect(' examples/bad-adt/corpus/209ce82661a8103a.toml
python3 - <<'PY'
import json
with open("examples/bad-adt/build/fuzz/summary.json") as f:
    d = json.load(f)
names = {c["name"]: c for c in d["classify"]}
assert names["square"]["false"] >= 1 and names["wide"]["false"] >= 1
PY
if fuzz --replay examples/bad-adt/corpus/209ce82661a8103a.toml examples/bad-adt; then
  echo "replay should have reproduced the ADT property failure" && exit 1
fi
test -f examples/bad-fault/build/fuzz/repro.toml
grep -q 'fault_seed' examples/bad-fault/build/fuzz/repro.toml
if fuzz --replay examples/bad-fault/corpus/f83245e1fbf633a5.toml examples/bad-fault; then
  echo "fault replay should have reproduced the failure" && exit 1
fi
grep -v fault_seed examples/bad-fault/corpus/f83245e1fbf633a5.toml > /tmp/bad-fault-nofault.toml
if ! fuzz --replay /tmp/bad-fault-nofault.toml examples/bad-fault; then
  echo "replay without fault_seed should pass" && exit 1
fi
grep -q 'schedule_seed = "' examples/bad-sched/corpus/d037d00bc981a2fb.toml
grep -q 'drive checkOrder' examples/bad-sched/corpus/d037d00bc981a2fb.toml
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml examples/bad-sched; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v -e schedule_seed -e schedule_picks examples/bad-sched/corpus/d037d00bc981a2fb.toml > /tmp/bad-sched-fifo.toml
if ! fuzz --replay /tmp/bad-sched-fifo.toml examples/bad-sched; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
# A pinned schedule replays recorded picks. An unrelated edit keeps the
# failure red; a fiber-structure edit reports drift instead of going green.
rm -rf /tmp/bad-sched-picks
cp -R examples/bad-sched /tmp/bad-sched-picks
rm -rf /tmp/bad-sched-picks/build
python3 - <<'PY_EDIT'
from pathlib import Path
p = Path("/tmp/bad-sched-picks/src/Main.scuzz")
text = p.read_text()
old = 'IO.println("sched-example")'
if old not in text:
    raise SystemExit("edit fixture: println site missing")
p.write_text(text.replace(old, 'IO.println("sched example")', 1))
PY_EDIT
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml /tmp/bad-sched-picks > /tmp/scuzz-sched-keep.log 2>&1; then
  echo "pinned schedule must stay red after an unrelated edit" && exit 1
fi
if grep -q "schedule drift" /tmp/scuzz-sched-keep.log; then
  echo "unrelated edit must not drift" && exit 1
fi
python3 - <<'PY_DRIFT'
from pathlib import Path
p = Path("/tmp/bad-sched-picks/sched.scuzz_scenario")
text = p.read_text()
old = '    _ <- IO.both(Queue.offer(q, "L"), Queue.offer(q, "R"))'
new = '    _ <- Fiber.fork(Deferred.empty().flatMap(w => Deferred.get(w))).flatMap(_ => IO.both(Queue.offer(q, "L"), Queue.offer(q, "R")))'
if old not in text:
    raise SystemExit("drift fixture: IO.both site missing")
p.write_text(text.replace(old, new, 1))
PY_DRIFT
if fuzz --replay examples/bad-sched/corpus/d037d00bc981a2fb.toml /tmp/bad-sched-picks > /tmp/scuzz-sched-drift.log 2>&1; then
  echo "drifted replay must not pass" && exit 1
fi
cat /tmp/scuzz-sched-drift.log
grep -q "schedule drift" /tmp/scuzz-sched-drift.log
rm -rf /tmp/bad-sched-picks
# A claim that guards on a renamed driver fails check. A claim antecedent
# that never fires fails a campaign with search iterations.
if "$SCUZZ" check examples/bad-driver > /tmp/scuzz-bad-driver.log 2>&1; then
  echo "renamed driveHas name should fail check" && exit 1
fi
cat /tmp/scuzz-bad-driver.log
grep -q 'Timeline.driveHas name drop is not a drive in this package' /tmp/scuzz-bad-driver.log
vacuous_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-vacuous.XXXXXX")"
mkdir -p "$vacuous_dir/src"
cat > "$vacuous_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "vacuous"
MANIFEST
cat > "$vacuous_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$vacuous_dir/vac.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def calm(): IO[Unit] =
  IO.pure(())

def storm(): IO[Unit] =
  IO.pure(())

SCENARIO
cat > "$vacuous_dir/vac.scuzz_verify" <<'CLAIMS'
def stormWrote(t: Timeline): Verdict =
  Verdict.every(t, i => !Timeline.driveHas(t, i, "storm") || Timeline.effectHas(t, i, "Fs.write"))

CLAIMS
"$SCUZZ" check "$vacuous_dir"
if fuzz --seed 42 --iterations 2 "$vacuous_dir" > /tmp/scuzz-vacuous.log 2>&1; then
  echo "vacuous claim antecedent should fail the campaign" && exit 1
fi
cat /tmp/scuzz-vacuous.log
grep -q 'claim drive never fired: storm' /tmp/scuzz-vacuous.log
python3 - "$vacuous_dir/build/fuzz/summary.json" <<'PY_VACUOUS'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] == 0
assert d["claim_drives"]["declared"] == ["storm"]
assert d["claim_drives"]["reached"] == []
assert d["claim_drives"]["never"] == ["storm"]
PY_VACUOUS
if ! fuzz --seed 42 --iterations 0 "$vacuous_dir" > /tmp/scuzz-vacuous-corpus.log 2>&1; then
  echo "corpus-only must report never-fired without failing" && exit 1
fi
grep -q 'claim drive never fired: storm' /tmp/scuzz-vacuous-corpus.log
rm -rf "$vacuous_dir"

# The scenario declares its fault surface. An unknown entry fails check.
fault_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-faults.XXXXXX")"
mkdir -p "$fault_dir/src"
cat > "$fault_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "faults"
MANIFEST
cat > "$fault_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$fault_dir/faults.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def faults(): List[String] =
  ["disk"]

def calm(): IO[Unit] =
  IO.pure(())

SCENARIO
if "$SCUZZ" check "$fault_dir" > /tmp/scuzz-faults.log 2>&1; then
  echo "unknown faults entry should fail check" && exit 1
fi
cat /tmp/scuzz-faults.log
grep -q 'faults entry disk must be fs, net, or queue' /tmp/scuzz-faults.log
rm -rf "$fault_dir"

# Mutation results identify the complete campaign input. An equal campaign
# reuses them. An edit clears site results and selects changed definitions
# first. The default floor fails a surviving mutant.
gate_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-mutgate.XXXXXX")"
mkdir -p "$gate_root"
cp -R examples/counter "$gate_root/counter"
cp -R examples/shared "$gate_root/shared"
rm -rf "$gate_root/counter/build" "$gate_root/counter/.scuzz"
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-1.log 2>&1
test -f "$gate_root/counter/.scuzz/mutate.results"
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-2.log 2>&1
python3 - "$gate_root/counter/build/fuzz/summary.json" <<'PY_GATE'
import json, sys
with open(sys.argv[1]) as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["mutate"]["ran"] >= 1
PY_GATE
GATE_DIR="$gate_root/counter" python3 - <<'PY_EDIT'
import os
from pathlib import Path
p = Path(os.environ["GATE_DIR"]) / "src" / "Main.scuzz"
text = p.read_text()
old = "Str.concat(value, env)"
assert old in text
p.write_text(text.replace(old, 'Str.concat(value, Str.concat(env, ""))', 1))
PY_EDIT
fuzz --iterations 16 "$gate_root/counter" > /tmp/scuzz-mutgate-3.log 2>&1
grep -q 'mutant 0: survived at Main.capturedLabel' /tmp/scuzz-mutgate-3.log
reused2="$(grep -c 'reused' /tmp/scuzz-mutgate-2.log)"
reused3="$(grep -c 'reused' /tmp/scuzz-mutgate-3.log)"
test "$reused2" -gt "$reused3"
! grep -Eq 'mutant [0-9]+: reused ' /tmp/scuzz-mutgate-3.log
rm -rf "$gate_root"
floordef_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-floordef.XXXXXX")"
mkdir -p "$floordef_dir/src"
cat > "$floordef_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "floordef"
MANIFEST
cat > "$floordef_dir/src/Main.scuzz" <<'SOURCE'
def content(n: Int): String =
  Str.fromInt(n + 1)

@main def main: IO[Unit] =
  Fs.write("out.txt", content(1))
SOURCE
if fuzz --iterations 16 "$floordef_dir" > /tmp/scuzz-floordef.log 2>&1; then
  echo "default floor must fail a surviving mutant" && exit 1
fi
cat /tmp/scuzz-floordef.log
grep -q 'mutation score 0.000 is below floor 1.000' /tmp/scuzz-floordef.log
rm -rf "$floordef_dir"

# Generation: a compound where keeps both bounds; unbounded Ints reach the
# overflow edges; strings cover the delimiter alphabet; ADT string fields
# carry delimiters through both engines.
gen_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-gen.XXXXXX")"
mkdir -p "$gen_dir/src"
cat > "$gen_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "gen"
MANIFEST
cat > "$gen_dir/src/Main.scuzz" <<'SOURCE'
def inBand(n: Int): Bool =
  n >= 10 && n <= 20

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$gen_dir/gen.scuzz_verify" <<'CLAIMS'
oracle clamped(n: Int where n >= 10 && n <= 20): Bool =
  Main.inBand(n)

CLAIMS
"$SCUZZ" check "$gen_dir"
fuzz --seed 42 --iterations 8 "$gen_dir"
grep -q 'clamped i>=10&&i<=20' "$gen_dir/build/drivers.txt"
grep -q 'drive clamped 10' "$gen_dir/build/seeds.txt"
rm -rf "$gen_dir"

edge_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-edge.XXXXXX")"
mkdir -p "$edge_dir/src"
cat > "$edge_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "edge"
MANIFEST
cat > "$edge_dir/src/Main.scuzz" <<'SOURCE'
def notMax(n: Int): Bool =
  n != 9223372036854775807

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$edge_dir/edge.scuzz_verify" <<'CLAIMS'
oracle edge(n: Int): Bool =
  Main.notMax(n)

CLAIMS
if fuzz --seed 43 --iterations 16 "$edge_dir" > /tmp/scuzz-edge.log 2>&1; then
  echo "search must reach the overflow edge" && exit 1
fi
grep -Fqx 'events = ["drive edge 9223372036854775807"]' "$edge_dir/build/fuzz/repro.toml"
if fuzz --replay "$edge_dir/build/fuzz/repro.toml" "$edge_dir"; then
  echo "overflow replay must preserve the failure" && exit 1
fi
rm -rf "$edge_dir"

str_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-genstr.XXXXXX")"
mkdir -p "$str_dir/src"
cat > "$str_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "genstr"
MANIFEST
cat > "$str_dir/src/Main.scuzz" <<'SOURCE'
enum Pair:
  case a(s: String, n: Int)

def noNewline(s: String): Bool =
  !Str.contains(s, "\n")

def firstOk(p: Pair): Bool =
  p match {
    case Pair.a(s, _n) => !Str.contains(s, ",")
  }

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$str_dir/str.scuzz_verify" <<'CLAIMS'
oracle strOk(s: String): Bool =
  Main.noNewline(s)

CLAIMS
if fuzz --seed 0 --iterations 16 "$str_dir" > /tmp/scuzz-genstr.log 2>&1; then
  echo "search must reach the newline alphabet" && exit 1
fi
grep -Fqx 'events = ["drive strOk \"\\n\""]' "$str_dir/build/fuzz/repro.toml"
if fuzz --replay "$str_dir/build/fuzz/repro.toml" "$str_dir"; then
  echo "newline replay must preserve the failure" && exit 1
fi
rm -f "$str_dir/str.scuzz_verify"
cat > "$str_dir/pair.scuzz_verify" <<'CLAIMS'
oracle pairOk(p: Pair): Bool =
  Main.firstOk(p)

CLAIMS
rm -rf "$str_dir/build" "$str_dir/corpus"
if fuzz --seed 42 --iterations 16 "$str_dir" > /tmp/scuzz-genpair.log 2>&1; then
  echo "search must reach a delimiter ADT field" && exit 1
fi
grep -Fqx 'events = ["drive pairOk a(\"a,b](x)\",9223372036854775806)"]' "$str_dir/build/fuzz/repro.toml"
if fuzz --replay "$str_dir/build/fuzz/repro.toml" "$str_dir"; then
  echo "ADT delimiter replay must preserve the failure" && exit 1
fi
rm -rf "$str_dir"
if fuzz --iterations 8 examples/bad-response; then
  echo "fuzz should have found the response failure" && exit 1
fi
test -f examples/bad-response/build/fuzz/repro.toml
if fuzz --iterations 0 examples/bad-response; then
  echo "corpus-only should pin the bad-response failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-split > /tmp/scuzz-bad-split.log 2>&1; then
  echo "live/verify split should fail the campaign" && exit 1
fi
cat /tmp/scuzz-bad-split.log
grep -q "fuzz live/verify split: silent observation mismatch" /tmp/scuzz-bad-split.log
if ! fuzz --iterations 0 examples/bad-sometimes | tee /tmp/scuzz-sometimes-summary.log; then
  echo "corpus-only should report never-reached without failing" && exit 1
fi
assert_fuzz_summary examples/bad-sometimes/build/fuzz/summary.json /tmp/scuzz-sometimes-summary.log
python3 - <<'PY'
import json
with open("examples/bad-sometimes/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["fuzz"]["iterations"] == 0
assert "tappedPlus" in d["sometimes"]["declared"]
assert "tappedPlus" in d["sometimes"]["never"]
assert "button:+1" in d["triggers"]["declared"]
assert "button:+1" in d["triggers"]["never"]
assert "tappedPlus" not in d["sometimes"]["reached"]
assert "button:+1" not in d["triggers"]["reached"]
PY
if fuzz --iterations 8 examples/bad-sometimes > /tmp/scuzz-bad-sometimes.log 2>&1; then
  echo "search should fail when tappedPlus and button:+1 never fire" && exit 1
fi
cat /tmp/scuzz-bad-sometimes.log
assert_fuzz_summary examples/bad-sometimes/build/fuzz/summary.json /tmp/scuzz-bad-sometimes.log
grep -q "sometimes never reached: tappedPlus" /tmp/scuzz-bad-sometimes.log
grep -q "trigger never fired: button:+1" /tmp/scuzz-bad-sometimes.log
python3 - <<'PY'
import json
with open("examples/bad-sometimes/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False
assert d["fuzz"]["search_failures"] == 0
assert "tappedPlus" in d["sometimes"]["never"]
assert "button:+1" in d["triggers"]["never"]
PY
chrome_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-chrome-breadth.XXXXXX")"
chrome_dir="$chrome_root/counter"
mkdir -p "$chrome_dir" "$chrome_root/shared"
cp -R examples/counter/. "$chrome_dir/"
cp -R examples/shared/. "$chrome_root/shared/"
rm -rf "$chrome_dir/build"
rm -f "$chrome_dir"/count.scuzz_verify
python3 - <<PY
from pathlib import Path
p = Path("$chrome_dir") / "src" / "Main.scuzz"
text = p.read_text()
old = 'View.row([View.minSize(80, 36, View.button("+1"'
new = 'View.row([View.minSize(80, 36, View.button("skip", _ => IO.pure(()))), View.minSize(80, 36, View.button("+1"'
if old not in text:
    raise SystemExit("chrome decoy: +1 button site missing")
p.write_text(text.replace(old, new, 1))
PY
cat > "$chrome_dir/chrome.scuzz_verify" <<'EOF'
def plusVisible(t: Timeline): Verdict =
  Verdict.alwaysHas(t, "button:+1")

def titleVisible(t: Timeline): Verdict =
  Verdict.alwaysHas(t, "text:Counter")
EOF
if ! fuzz --iterations 0 "$chrome_dir" > /tmp/scuzz-chrome-breadth.log 2>&1; then
  cat /tmp/scuzz-chrome-breadth.log
  echo "chrome-only corpus replay must stay green" && exit 1
fi
cat /tmp/scuzz-chrome-breadth.log
grep -Eq "varied but unclaimed:.*signals" /tmp/scuzz-chrome-breadth.log
CHROME_DIR="$chrome_dir" python3 - <<'PY'
import json, os
with open(os.environ["CHROME_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
br = d["breadth"]
assert d["fuzz"]["ok"] is True
assert "signals" in br["varied"], br
assert br["claimed"]["signalInt"] == [], br
assert "signals" in br["unclaimed"], br
PY
rm -rf "$chrome_root"
rm -rf /tmp/scuzz-fuzzbug
"$SCUZZ" new --ui --path /tmp scuzz-fuzzbug
# The replacement fixture uses runtime failure checks.
rm /tmp/scuzz-fuzzbug/scuzz-fuzzbug.scuzz_verify
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column([
      View.text("Bug"),
      View.bindText(label),
      View.row([View.button("boom", _ => Fs.read("/definitely/missing"))])
    ]))
  } yield ()
EOF
if fuzz --iterations 8 /tmp/scuzz-fuzzbug; then
  echo "fuzz should have found the failure" && exit 1
fi
test -f /tmp/scuzz-fuzzbug/build/fuzz/repro.toml
test -n "$(ls /tmp/scuzz-fuzzbug/corpus/*.toml 2>/dev/null)"
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["promoted"] >= 1
PY
if fuzz --replay /tmp/scuzz-fuzzbug/build/fuzz/repro.toml /tmp/scuzz-fuzzbug; then
  echo "replay should have reproduced the failure" && exit 1
fi
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column([
      View.text("Bug"),
      View.bindText(label),
      View.row([View.button("boom", _ => Signal.set(count, Signal.get(count) + 1))])
    ]))
  } yield ()
EOF
if ! fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pass after the source fix" && exit 1
fi
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["entries"] >= 1 and d["corpus"]["failures"] == 0
PY
cat > /tmp/scuzz-fuzzbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    count = Signal.make(0)
    label = Signal.map(count, n => s"count = $n")
    _ <- Ui.run(_ => View.column([
      View.text("Bug"),
      View.bindText(label),
      View.row([View.button("boom", _ => Fs.read("/definitely/missing"))])
    ]))
  } yield ()
EOF
if fuzz --iterations 0 /tmp/scuzz-fuzzbug; then
  echo "corpus-only should pin the reintroduced bug" && exit 1
fi
python3 - <<'PY'
import json
with open("/tmp/scuzz-fuzzbug/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["corpus"]["entries"] >= 1
assert d["corpus"]["failures"] >= 1
assert d["corpus"]["promoted"] == 0
PY
rm -rf /tmp/scuzz-schedbug
"$SCUZZ" new --path /tmp scuzz-schedbug
rm /tmp/scuzz-schedbug/scuzz-schedbug.scuzz_verify
cat > /tmp/scuzz-schedbug/src/Main.scuzz <<'EOF'
@main def main: IO[Unit] =
  for {
    q <- Queue.unbounded()
    _ <- IO.both(Queue.offer(q, "L"), Queue.offer(q, "R"))
    first <- Queue.take(q)
    _ <- if (Str.eq(first, "L")) IO.pure(()) else IO.fail("expected L first")
  } yield ()
EOF
"$SCUZZ" fuzz --iterations 0 /tmp/scuzz-schedbug
if fuzz --iterations 12 /tmp/scuzz-schedbug; then
  echo "schedule fuzz should have found the interleaving bug" && exit 1
fi
test -f /tmp/scuzz-schedbug/build/fuzz/repro.toml
grep -q 'schedule_seed' /tmp/scuzz-schedbug/build/fuzz/repro.toml
if fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro.toml /tmp/scuzz-schedbug; then
  echo "schedule replay should have reproduced the failure" && exit 1
fi
grep -v schedule_seed /tmp/scuzz-schedbug/build/fuzz/repro.toml > /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml
if ! fuzz --replay /tmp/scuzz-schedbug/build/fuzz/repro-fifo.toml /tmp/scuzz-schedbug; then
  echo "FIFO replay (no schedule_seed) should pass" && exit 1
fi
invalid_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-invalid-map.XXXXXX")"
mkdir -p "$invalid_dir/src"
cat > "$invalid_dir/scuzz.toml" <<'EOF'
[package]
name = "invalid-map"
EOF
cat > "$invalid_dir/src/Main.scuzz" <<'EOF'
def label(count: Signal[Int]): Signal[String] =
  Signal.map(count, n => Str.fromInt(n))

def unused(): Int =
  3

@main def main: IO[Unit] =
  IO.pure(label(Signal.make(2))).map(_ => ())
EOF
fuzz --iterations 2 "$invalid_dir"
INVALID_DIR="$invalid_dir" python3 - <<'PY'
import json, os
with open(os.environ["INVALID_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["mutate"]["invalid"] == 1
assert d["mutate"]["killed"] == 0
assert d["coverage"]["total"] == 3
assert d["coverage"]["reached"] == 2
assert "score" not in d["mutate"], "invalid mutants must not produce a score"
PY
rm -rf "$invalid_dir"
boolean_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-boolean-claim.XXXXXX")"
mkdir -p "$boolean_dir/src"
cat > "$boolean_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "boolean-claim"
MANIFEST
cat > "$boolean_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
check_boolean_claim() {
  local expression="$1" expected="$2" status=0
  printf 'oracle fact(): Bool =\n  %s\n\n' "$expression" > "$boolean_dir/fact.scuzz_verify"
  rm -f "$boolean_dir/build/fuzz/summary.json"
  fuzz --iterations 0 "$boolean_dir" > /tmp/scuzz-boolean-claim.log 2>&1 || status=$?
  cat /tmp/scuzz-boolean-claim.log
  test "$status" -eq "$expected"
  assert_fuzz_summary "$boolean_dir/build/fuzz/summary.json" /tmp/scuzz-boolean-claim.log
  python3 - "$boolean_dir/build/fuzz/summary.json" "$expected" <<'PY_BOOLEAN'
import json, sys
with open(sys.argv[1]) as f:
    summary = json.load(f)
expected = int(sys.argv[2])
assert summary["fuzz"]["ok"] == (expected == 0)
assert summary["fuzz"]["search"] == 0
assert summary["corpus"]["entries"] == 1
assert summary["corpus"]["failures"] == expected
PY_BOOLEAN
}
check_boolean_claim 'false && false' 1
check_boolean_claim 'true && false' 1
check_boolean_claim 'false && true' 1
check_boolean_claim 'true && true' 0
check_boolean_claim 'false || false' 1
check_boolean_claim 'true || false' 0
check_boolean_claim 'false || true' 0
check_boolean_claim '1 != 1' 1
check_boolean_claim '1 != 2' 0
check_boolean_claim '1 > 2' 1
check_boolean_claim '1 < 2' 0
check_boolean_claim '1 == 2' 1
check_boolean_claim '1 == 1' 0
check_boolean_for() {
  local bind="$1" yield="$2" expected="$3"
  check_boolean_claim "$(printf 'for {\n    %s\n  } yield %s' "$bind" "$yield")" "$expected"
}
check_boolean_for 'x = false' 'x' 1
check_boolean_for 'x = true' 'x' 0
check_boolean_for 'pair = (7, "seven")' 'pair._1 == 7 && pair._2 == "seven"' 0
rm -rf "$boolean_dir"

match_require_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-match-require.XXXXXX")"
mkdir -p "$match_require_dir/src" "$match_require_dir/corpus"
cat > "$match_require_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "match-require"
MANIFEST
cat > "$match_require_dir/world.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] = IO.pure(())
def run(n: Int): IO[Int] = Main.checked(n)
SCENARIO
cat > "$match_require_dir/corpus/payload.toml" <<'CORPUS'
[fuzz]
events = ["drive run 7", "drive run -1"]
CORPUS
check_match_require() {
  local comparison="$1" expected="$2" status=0
  cat > "$match_require_dir/src/Main.scuzz" <<SOURCE
enum Packet:
  case Value(n: Int)

def checked(n: Int): IO[Int] =
  Packet.Value(n) match {
    case Packet.Value(7) => IO.pure(7).require("literal payload", result => result $comparison 7)
    case Packet.Value(value) => IO.pure(value).require("matched payload", result => result $comparison value)
  }

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
  fuzz --iterations 0 "$match_require_dir" > /tmp/scuzz-match-require.log 2>&1 || status=$?
  cat /tmp/scuzz-match-require.log
  test "$status" -eq "$expected"
  assert_fuzz_summary "$match_require_dir/build/fuzz/summary.json" /tmp/scuzz-match-require.log
  python3 - "$match_require_dir/build/fuzz/summary.json" "$expected" <<'PY_MATCH'
import json, sys
with open(sys.argv[1]) as f:
    summary = json.load(f)
expected = int(sys.argv[2])
assert summary["fuzz"]["ok"] == (expected == 0)
assert summary["corpus"]["failures"] == expected
PY_MATCH
  if [ "$expected" -ne 0 ]; then
    if fuzz --replay "$match_require_dir/build/fuzz/repro.toml" "$match_require_dir" > /tmp/scuzz-match-require-replay.log 2>&1; then
      echo "false payload assertion must fail replay" && exit 1
    fi
    grep -q 'fuzz replay reproduced a failure' /tmp/scuzz-match-require-replay.log
  fi
}
check_match_require '==' 0
check_match_require '!=' 1
rm -rf "$match_require_dir"

# Both engines give the same campaign results and required inputs.
# Evaluator feedback can select different search scripts.
# Reports keep engine names, scripts, and recorded scheduler picks.
# The default run must use the evaluator.
fuzz_both_engines() {
  local dir="$1" iterations="$2" name="$3"
  rm -f "$dir/.scuzz/mutate.results"
  fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-summary.log"
  if grep -q 'probes run compiled' "/tmp/scuzz-$name-summary.log"; then
    echo "$dir: the evaluator engine fell back to compiled probes" && exit 1
  fi
  cp "$dir/build/fuzz/summary.json" "/tmp/scuzz-$name-ev.json"
  rm -f "$dir/.scuzz/mutate.results"
  SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-compiled.log"
  python3 - "/tmp/scuzz-$name-ev.json" "$dir/build/fuzz/summary.json" <<'PY_ENGINE'
import json, sys
from pathlib import Path
ev, compiled = [json.loads(Path(path).read_text()) for path in sys.argv[1:]]
for report, engine in ((ev, "evaluator"), (compiled, "compiled")):
    rows = report["scope"]["workloads"]
    assert all(row["engine"] == ("compiled" if row["required"] and row["label"] != "idle" else engine) for row in rows)
    for row in rows:
        row.pop("engine")
        row.pop("schedule_picks")
        if not row["required"]:
            row.pop("script")
assert ev == compiled, "evaluator and compiled results or required inputs differ"
PY_ENGINE
}

fuzz_both_engines examples/webhook 160 webhook
assert_fuzz_summary examples/webhook/build/fuzz/summary.json /tmp/scuzz-webhook-summary.log
python3 - <<'PY_WEBHOOK'
import json
with open("examples/webhook/build/fuzz/summary.json") as f:
    webhook = json.load(f)
assert webhook["mutate"]["ran"] == webhook["mutate"]["sites"]
assert webhook["mutate"]["survived"] == 0
assert webhook["mutate"]["invalid"] == 0
assert "webhook.json" in webhook["breadth"]["claimed"]["fileSame"]
assert {"concurrent", "continued"} <= set(webhook["breadth"]["claimed"]["driveHas"])
assert {"alpha.status", "beta.status", "forged.status", "final.status"} <= set(webhook["breadth"]["claimed"]["fileTextIs"])
with open("examples/webhook/build/drivers.txt") as f:
    drivers = [line.split()[0] for line in f if line.strip()]
assert "faulted" not in drivers and "rejected" not in drivers
PY_WEBHOOK

fuzz_api_report() {
  local dir="examples/api-report" iterations=320 name="api-report"
  rm -f "$dir/.scuzz/mutate.results"
  fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-summary.log"
  if grep -q 'probes run compiled' "/tmp/scuzz-$name-summary.log"; then
    echo "$dir: the evaluator engine fell back to compiled probes" && exit 1
  fi
  cp "$dir/build/fuzz/summary.json" "/tmp/scuzz-$name-ev.json"
  rm -f "$dir/.scuzz/mutate.results"
  SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations "$iterations" "$dir" | tee "/tmp/scuzz-$name-compiled.log"
  python3 - "/tmp/scuzz-$name-ev.json" "$dir/build/fuzz/summary.json" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    ev = json.load(f)
with open(sys.argv[2]) as f:
    compiled = json.load(f)

def reached_locs(block):
    return {r["location"] for r in block["regions"] if r["reached"]}

def reach_names(block):
    return set(block["reached"])

def subset(kind, left, right):
    extra = left - right
    if extra:
        raise SystemExit(f"compiled {kind} reach is not inside the evaluator: {sorted(extra)[:8]}")

assert ev["fuzz"]["ok"] and compiled["fuzz"]["ok"]
assert ev["mutate"] == compiled["mutate"], (ev["mutate"], compiled["mutate"])
assert ev["corpus"] == compiled["corpus"], (ev["corpus"], compiled["corpus"])
assert ev["coverage"]["total"] == compiled["coverage"]["total"]
assert ev["coverage"]["branches"]["total"] == compiled["coverage"]["branches"]["total"]
subset("functions", reached_locs(compiled["coverage"]), reached_locs(ev["coverage"]))
subset("branches", reached_locs(compiled["coverage"]["branches"]), reached_locs(ev["coverage"]["branches"]))
for key in ("sometimes", "triggers", "claim_drives"):
    assert ev[key]["declared"] == compiled[key]["declared"]
    subset(key, reach_names(compiled[key]), reach_names(ev[key]))
PY
}

fuzz_api_report
assert_fuzz_summary examples/api-report/build/fuzz/summary.json /tmp/scuzz-api-report-summary.log
python3 - <<'PY_CHECK'
import json
with open("examples/api-report/build/fuzz/summary.json") as f:
    report = json.load(f)
assert report["breadth"]["claimed"]["fileSame"] == ["report.json"]
assert "queryPages" in report["breadth"]["claimed"]["driveHas"]
assert "files" not in report["breadth"]["unclaimed"]
assert report["mutate"]["ran"] == report["mutate"]["sites"]
assert report["mutate"]["survived"] == 0
assert report["mutate"]["invalid"] == 0
PY_CHECK

# One success drive on the host loopback TLS client. Clock and files stay simulated.
if ! fuzz --live --replay examples/api-report/live.toml examples/api-report | tee /tmp/scuzz-api-report-live.log | grep "fuzz replay ok" >/dev/null; then
  echo "examples/api-report: live loopback replay failed" && exit 1
fi

python3 - <<'PY_LIVE_REPLAY'
import json
from pathlib import Path
report = json.loads(Path('examples/api-report/build/fuzz/summary.json').read_text())
assert report['kind'] == 'replay' and report['status'] == 'passed' and report['mode'] == 'live', report
assert report['scope']['required']['passed'] == 1 and report['scope']['required']['planned'] == 1, report
assert report['guards']['status'] == 'guarded', report
assert report['scope']['workloads'][0]['script'] == 'drive success 0\n', report
PY_LIVE_REPLAY

# URLSession certificate checks and TLS handshake errors on host loopback.
if ! fuzz --session --iterations 0 examples/transport | tee /tmp/scuzz-transport-session.log | grep "fuzz ok" >/dev/null; then
  echo "examples/transport: session replay failed" && exit 1
fi

# TLS rules constrain each completed request driver.
python3 - <<'PY_TRANSPORT'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
report = json.loads(Path('examples/transport/build/fuzz/summary.json').read_text())
assert report['scope']['required']['passed'] == 2, report
assert report['claim_drives']['declared'] == ['cases'], report
assert report['claim_drives']['reached'] == ['cases'], report
assert report['scope']['claims']['reached'] == ['handshakeFails', 'trustFails'], report
with tempfile.TemporaryDirectory(prefix='scuzz-transport-rule-') as directory:
    root = Path(directory)
    shutil.copytree('examples/transport/src', root / 'src')
    shutil.copytree('examples/transport/corpus', root / 'corpus')
    for name in ('scuzz.toml', 'transport.scuzz_verify'):
        shutil.copyfile(Path('examples/transport') / name, root / name)
    original = Path('examples/transport/transport.scuzz_scenario').read_text()
    for path, claim in (('handshake.txt', 'handshakeFails'), ('trust.txt', 'trustFails')):
        broken = original.replace('Fs.write(path, message)', 'Fs.write(path, if (path == "' + path + '") "wrong result" else message)')
        assert broken != original
        (root / 'transport.scuzz_scenario').write_text(broken)
        run = subprocess.run([str(cli), 'fuzz', '--session', '--iterations', '0', str(root)], capture_output=True, text=True, timeout=180)
        assert run.returncode != 0 and ('claim ' + claim + ' rejected') in run.stdout + run.stderr, (run.stdout, run.stderr)
        report = json.loads((root / 'build/fuzz/summary.json').read_text())
        assert report['scope']['workloads'][0]['status'] == 'passed', report
        assert report['scope']['required']['failed'] == 1 and report['corpus']['failures'] == 1, report
print('Transport rules: idle passes; each wrong TLS result fails its request claim')
PY_TRANSPORT

# File comparisons judge recorded contents at both states.
file_compare_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-file-compare.XXXXXX")"
mkdir -p "$file_compare_dir/src" "$file_compare_dir/corpus"
cat > "$file_compare_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "file-compare"
MANIFEST
cat > "$file_compare_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$file_compare_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def keep(): IO[Unit] =
  IO.pure(())

def change(): IO[Unit] =
  Fs.write("report.txt", "after!")
SCENARIO
cat > "$file_compare_dir/files.scuzz_verify" <<'CLAIMS'
private def unchanged(t: Timeline, a: Int, b: Int): Bool =
  Timeline.fileSame(t, a, b, "report.txt")

def preserved(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => unchanged(t, pair._1, pair._2))
CLAIMS
cat > "$file_compare_dir/corpus/files.toml" <<'CORPUS'
[fuzz]
events = ["drive keep"]
CORPUS
fuzz --iterations 0 "$file_compare_dir"
cat > "$file_compare_dir/corpus/files.toml" <<'CORPUS'
[fuzz]
events = ["drive change"]
CORPUS
if fuzz --iterations 0 "$file_compare_dir" > /tmp/scuzz-file-compare.log 2>&1; then
  echo "file comparison must reject changed contents" >&2
  exit 1
fi
FILE_COMPARE_DIR="$file_compare_dir" python3 - <<'PY_CHECK'
import json, os
with open(os.environ["FILE_COMPARE_DIR"] + "/build/fuzz/summary.json") as f:
    comparison = json.load(f)
assert comparison["fuzz"]["ok"] is False
assert comparison["corpus"]["failures"] == 1
assert comparison["breadth"]["claimed"]["fileSame"] == ["report.txt"]
PY_CHECK
rm -rf "$file_compare_dir"

# A storm faults every later call. A partial write keeps the previous file.
fault_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-fault-plan.XXXXXX")"
mkdir -p "$fault_dir/src" "$fault_dir/corpus"
cat > "$fault_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "fault-plan"
MANIFEST
cat > "$fault_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$fault_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def faults(): List[String] =
  ["fs"]

def replaceOnce(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => IO.pure(()))

def replaceTwice(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => Fs.write("report.txt", "second-write").handleErrorWith(_ => IO.pure(())))

def afterOnce(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(error => IO.println(error))

def afterTwice(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => Fs.write("report.txt", "second-write").handleErrorWith(error => IO.println(error)))
SCENARIO
cat > "$fault_dir/files.scuzz_verify" <<'CLAIMS'
private def okTwice(t: Timeline, i: Int): Bool =
  !Timeline.driveHas(t, i, "replaceTwice") || Timeline.fileTextIs(t, i, "report.txt", "before")

private def okOnce(t: Timeline, i: Int): Bool =
  !Timeline.driveHas(t, i, "replaceOnce") || (Timeline.effectHas(t, i, "Fs.partial") && Timeline.fileTextIs(t, i, "report.txt", "before"))

def whole(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => okTwice(t, pair._2) && okOnce(t, pair._2) && (!Timeline.driveHas(t, pair._2, "afterOnce") || Timeline.effectHas(t, pair._2, "Fs.after") && Timeline.fileTextIs(t, pair._2, "report.txt", "after-complete")) && (!Timeline.driveHas(t, pair._2, "afterTwice") || Timeline.effectHas(t, pair._2, "Fs.after") && Timeline.fileTextIs(t, pair._2, "report.txt", "second-write")))
CLAIMS
cat > "$fault_dir/corpus/storm.toml" <<'CORPUS'
[fuzz]
fault_seed = "145"
events = ["drive replaceTwice"]
CORPUS
cat > "$fault_dir/corpus/partial.toml" <<'CORPUS'
[fuzz]
fault_seed = "289"
events = ["drive replaceOnce"]
CORPUS
cat > "$fault_dir/corpus/after.toml" <<'CORPUS'
[fuzz]
fault_seed = "577"
events = ["drive afterOnce"]
CORPUS
cat > "$fault_dir/corpus/after-storm.toml" <<'CORPUS'
[fuzz]
fault_seed = "721"
events = ["drive afterTwice"]
CORPUS
fuzz --iterations 0 "$fault_dir"

# Search reaches a partial-write plan. That plan fails a claim that rejects it.
partial_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-fault-partial.XXXXXX")"
mkdir -p "$partial_dir/src" "$partial_dir/corpus"
cat > "$partial_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "fault-partial"
[fuzz]
score_floor = 0
MANIFEST
cat > "$partial_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$partial_dir/files.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  Fs.write("report.txt", "before")

def faults(): List[String] =
  ["fs"]

def replaceOnce(): IO[Unit] =
  Fs.write("report.txt", "after-complete").handleErrorWith(_ => IO.pure(()))
SCENARIO
cat > "$partial_dir/files.scuzz_verify" <<'CLAIMS'
def noPartial(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => !Timeline.effectHas(t, pair._2, "Fs.partial"))
CLAIMS
cat > "$partial_dir/corpus/one.toml" <<'CORPUS'
[fuzz]
fault_seed = "1"
events = ["drive replaceOnce"]
CORPUS
if fuzz --iterations 64 "$partial_dir" > /tmp/scuzz-fault-partial.log 2>&1; then
  echo "search must reach a partial write" >&2
  exit 1
fi
grep -q 'fault_seed = "289"' "$partial_dir/build/fuzz/repro.toml"
cat > "$partial_dir/files.scuzz_verify" <<'CLAIMS'
def noAfter(t: Timeline): Verdict =
  Verdict.stepEvery(t, pair => !Timeline.effectHas(t, pair._2, "Fs.after"))
CLAIMS
if fuzz --iterations 128 "$partial_dir" > /tmp/scuzz-fault-after.log 2>&1; then
  echo "search must reach an error after replacement" >&2
  exit 1
fi
grep -q 'fault_seed = "577"' "$partial_dir/build/fuzz/repro.toml"
rm -rf "$fault_dir" "$partial_dir"
# Resource rules reject changes to release, cancellation, and timeout.
python3 - <<'PY_RESOURCE'
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
original = Path('examples/io/src/Bracket.scuzz').read_text()
changes = [
    (original.replace('count => count + 1', 'count => count + 2', 1), 'cancel releases once without a later write'),
    (original.replace('Fiber.interrupt(worker)', 'IO.sleep(101)', 1), 'cancel releases once without a later write'),
    (original.split('def timedOut()', 1)[0] + 'def timedOut()' + original.split('def timedOut()', 1)[1].replace('IO.sleep(100)', 'IO.sleep(0)', 1), 'timeout releases once without a later write'),
    (original.split('def recovered()', 1)[0] + 'def recovered()' + original.split('def recovered()', 1)[1].replace('count => count + 1', 'count => count + 2', 1), 'error releases once and recovers once'),
]
with tempfile.TemporaryDirectory(prefix='scuzz-resource-rules-') as directory:
    root = Path(directory)
    shutil.copytree('examples/io/src', root / 'src')
    shutil.copytree('examples/io/corpus', root / 'corpus')
    shutil.copyfile('examples/io/scuzz.toml', root / 'scuzz.toml')
    for path in Path('examples/io').glob('*.scuzz_*'):
        shutil.copyfile(path, root / path.name)
    for engine in ('evaluator', 'compiled'):
        env = dict(os.environ)
        env.pop('SCUZZ_FUZZ_ENGINE', None)
        if engine == 'compiled':
            env['SCUZZ_FUZZ_ENGINE'] = engine
        for source, rule in changes:
            assert source != original
            (root / 'src/Bracket.scuzz').write_text(source)
            run = subprocess.run([str(cli), 'fuzz', '--iterations', '0', str(root)], env=env, capture_output=True, text=True, timeout=180)
            assert run.returncode != 0 and ('oracle ' + rule + ' rejected') in run.stdout + run.stderr, (engine, rule, run.stdout[-1500:], run.stderr[-1500:])
            report = json.loads((root / 'build/fuzz/summary.json').read_text())
            assert report['scope']['required']['failed'] >= 1 and report['fuzz']['ok'] is False, report
            if engine == 'evaluator':
                assert 'probes run compiled' not in run.stdout, run.stdout
print('Resource rules: both engines reject wrong release counts, missing cancellation, late writes, and missing timeout')
PY_RESOURCE

fuzz_both_engines examples/io 16 io
python3 - <<'PY'
import json
with open("examples/io/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["v"] == 1 and d["kind"] == "fuzz"
assert d["fuzz"]["ok"] is True
assert d["coverage"]["reached"] >= 1
assert any(r["reached"] for r in d["coverage"]["regions"])
PY
# Corpus replay on the compiled engine under AddressSanitizer. Leaks fail.
if ! fuzz --asan --iterations 0 examples/io | tee /tmp/scuzz-io-asan.log; then
  echo "examples/io: ASan corpus replay failed" && exit 1
fi
if grep -E 'ERROR: (Address|Leak)Sanitizer' /tmp/scuzz-io-asan.log; then
  echo "examples/io: ASan reported a defect" && exit 1
fi
fuzz --asan --replay examples/io/corpus/composition.toml examples/io
python3 - <<'PY_ASAN_REPLAY'
import json
from pathlib import Path
report = json.loads(Path('examples/io/build/fuzz/summary.json').read_text())
assert report['kind'] == 'replay' and report['status'] == 'passed' and report['asan'] is True, report
assert report['mode'] == 'simulation' and report['guards']['status'] == 'guarded', report
assert report['scope']['required']['passed'] == 1 and report['scope']['required']['planned'] == 1, report
assert report['scope']['workloads'][0]['engine'] == 'compiled', report
PY_ASAN_REPLAY
fuzz --iterations 4 examples/hello
grep -q 'drive greetFact' examples/hello/build/seeds.txt
# Distance feedback: the evaluator search climbs to `code == 4242` and
# reaches the sometimes; the compiled control has no feedback and does not.
fuzz --iterations 32 examples/reach | tee /tmp/scuzz-reach-summary.log
grep -q '^sometimes: 1/1 reached' /tmp/scuzz-reach-summary.log
if grep -q 'probes run compiled' /tmp/scuzz-reach-summary.log; then
  echo "examples/reach: the evaluator engine fell back to compiled probes" && exit 1
fi
if SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations 32 examples/reach | tee /tmp/scuzz-reach-compiled.log; then
  echo "examples/reach: the compiled control reached the magic value without feedback" && exit 1
fi
grep -q '^sometimes: 0/1 reached' /tmp/scuzz-reach-compiled.log
fuzz --iterations 4 --oracles examples/counter
python3 - <<'PY'
import json
with open("examples/counter/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["mutate"]["oracles"] is True
assert d["mutate"]["score"] >= 0.5, d["mutate"]
PY
floor_root="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-floor-fail.XXXXXX")"
floor_dir="$floor_root/counter"
mkdir -p "$floor_dir" "$floor_root/shared"
cp -R examples/counter/. "$floor_dir/"
cp -R examples/shared/. "$floor_root/shared/"
rm -rf "$floor_dir/build"
python3 - <<PY
from pathlib import Path
p = Path("$floor_dir") / "scuzz.toml"
text = p.read_text()
old = "score_floor = 0.500"
new = "score_floor = 1.001"
if old not in text:
    raise SystemExit("floor-fail: score_floor 0.500 missing")
p.write_text(text.replace(old, new, 1))
PY
if fuzz --iterations 16 "$floor_dir" > /tmp/scuzz-floor-fail.log 2>&1; then
  cat /tmp/scuzz-floor-fail.log
  echo "score_floor 1.001 must fail live-code mutation on counter" && exit 1
fi
cat /tmp/scuzz-floor-fail.log
grep -q "is below floor 1.001" /tmp/scuzz-floor-fail.log
grep -q "scuzz fuzz fail (" /tmp/scuzz-floor-fail.log
if grep -q "scuzz fuzz ok" /tmp/scuzz-floor-fail.log; then
  echo "floor-fail must not print scuzz fuzz ok" && exit 1
fi
FLOOR_DIR="$floor_dir" python3 - <<'PY'
import json, os
with open(os.environ["FLOOR_DIR"] + "/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is False, d["fuzz"]
assert d["fuzz"]["search_failures"] == 0, d["fuzz"]
PY
rm -rf "$floor_root"

workload_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-workload.XXXXXX")"
mkdir -p "$workload_dir/src"
cat > "$workload_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "workload"
MANIFEST
cat > "$workload_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 3

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$workload_dir/input.scuzz_verify" <<'CLAIMS'
oracle input(n: Int): Bool =
  Main.accepts(n)
CLAIMS
fuzz --iterations 0 "$workload_dir"
if fuzz --seed 0 --iterations 16 "$workload_dir"; then
  echo "workload search must find an input absent from the seeds" >&2
  exit 1
fi
cp "$workload_dir/build/fuzz/repro.toml" "$workload_dir/first.toml"
grep -Fqx 'events = ["drive input 3"]' "$workload_dir/first.toml"
if fuzz --replay "$workload_dir/first.toml" "$workload_dir"; then
  echo "workload replay must preserve the failure" >&2
  exit 1
fi
rm -rf "$workload_dir/corpus"
if fuzz --seed 0 --iterations 16 "$workload_dir"; then
  echo "the same seed must find the same failure" >&2
  exit 1
fi
cmp "$workload_dir/first.toml" "$workload_dir/build/fuzz/repro.toml"
rm -rf "$workload_dir"

# A failing search stores the minimal script: shrink drops unneeded script
# lines to a fixpoint, then moves Int arguments toward the where bound or zero.
shrink_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-shrink.XXXXXX")"
mkdir -p "$shrink_dir/src" "$shrink_dir/corpus"
cat > "$shrink_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "shrink"
MANIFEST
cat > "$shrink_dir/src/Main.scuzz" <<'SOURCE'
@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$shrink_dir/shrink.scuzz_scenario" <<'SCENARIO'
def setup(): IO[Unit] =
  IO.pure(())

def calm(): IO[Unit] =
  IO.pure(())

def trip(n: Int): IO[Unit] =
  if (n >= 10) IO.fail("tripped") else IO.pure(())
SCENARIO
cat > "$shrink_dir/corpus/seed.toml" <<'CORPUS'
[fuzz]
events = ["drive calm", "drive calm", "drive calm"]
CORPUS
if fuzz --seed 42 --iterations 16 "$shrink_dir"; then
  echo "shrink search must find the boundary failure" >&2
  exit 1
fi
grep -Fqx 'events = ["drive trip 10"]' "$shrink_dir/build/fuzz/repro.toml"
grep -Fqx 'events = ["drive trip 10"]' "$shrink_dir"/corpus/search-*.toml
if fuzz --replay "$shrink_dir/build/fuzz/repro.toml" "$shrink_dir"; then
  echo "shrunk replay must preserve the failure" >&2
  exit 1
fi
rm -rf "$shrink_dir"

# A repeated campaign skips emit and link on a stamp hit.
stamp_dir="$(mktemp -d "${TMPDIR:-/tmp}/scuzz-stamp.XXXXXX")"
mkdir -p "$stamp_dir/src"
cat > "$stamp_dir/scuzz.toml" <<'MANIFEST'
[package]
name = "stamp"
MANIFEST
cat > "$stamp_dir/src/Main.scuzz" <<'SOURCE'
def accepts(n: Int): Bool =
  n != 3

@main def main: IO[Unit] =
  IO.pure(())
SOURCE
cat > "$stamp_dir/input.scuzz_verify" <<'CLAIMS'
oracle input(n: Int): Bool =
  Main.accepts(n)
CLAIMS
fuzz --iterations 0 "$stamp_dir"
stamp_before="$(python3 - "$stamp_dir" <<'PY_STAMP'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
paths = ("build/stamp.ll", "build/stamp", "build/live/stamp.ll", "build/live/stamp")
print(json.dumps({path: (root / path).stat().st_mtime_ns for path in paths}))
PY_STAMP
)"
sleep 1
fuzz --iterations 0 "$stamp_dir"
python3 - "$stamp_dir" "$stamp_before" <<'PY_STAMP'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
for path, before in json.loads(sys.argv[2]).items():
    assert (root / path).stat().st_mtime_ns == before, path
PY_STAMP
rm -rf "$stamp_dir"

# A compiler change invalidates live and verification artifacts.
python3 - "$SCUZZ" <<'PY_COMPILER_CACHE'
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

if sys.platform != "linux":
    sys.exit(0)
with tempfile.TemporaryDirectory(prefix="scuzz-compiler-cache-") as tmp:
    root = Path(tmp)
    compiler = root / "compiler"
    shutil.copy2(sys.argv[1], compiler)
    env = dict(os.environ, SCUZZ_EXECUTABLE_SHA256="caller-controlled")
    packages = []
    for kind in ("live", "verify"):
        pkg = root / kind
        (pkg / "src").mkdir(parents=True)
        (pkg / "scuzz.toml").write_text('[package]\nname = "cache-proof"\n')
        (pkg / "src/Main.scuzz").write_text(
            'def id(n: Int): Int = n\n@main def main: IO[Unit] = IO.pure(())\n')
        (pkg / "facts.scuzz_verify").write_text(
            'oracle identity(n: Int): Bool =\n  Main.id(n) == n\n\n')
        packages.append((kind, pkg))

    def run(kind, pkg):
        args = ["build"] if kind == "live" else ["fuzz", "--iterations", "0"]
        subprocess.run([str(compiler), *args, str(pkg)], env=env,
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        paths = [pkg / "build/cache-proof.ll", pkg / "build" / (
            "fingerprint" if kind == "live" else "fingerprint.verify")]
        if kind == "verify":
            paths.extend([pkg / "build/live/cache-proof.ll", pkg / "build/live/fingerprint"])
        digest = hashlib.sha256(compiler.read_bytes()).hexdigest()
        for path in paths:
            if "fingerprint" in path.name:
                assert path.read_text().splitlines()[0] == digest
        return [p.stat().st_mtime_ns for p in paths]

    before = [run(kind, pkg) for kind, pkg in packages]
    assert before == [run(kind, pkg) for kind, pkg in packages]
    # ELF permits trailing data. The compiler behavior stays the same.
    with compiler.open("ab") as out:
        out.write(b"compiler-cache-identity-proof")
    after = [run(kind, pkg) for kind, pkg in packages]
    assert all(all(a != b for a, b in zip(old, new)) for old, new in zip(before, after))
    assert after == [run(kind, pkg) for kind, pkg in packages]
    mode = root / "output-mode"
    (mode / "src").mkdir(parents=True)
    (mode / "scuzz.toml").write_text('[package]\nname = "output-mode"\n')
    (mode / "src/Main.scuzz").write_text(
        'def message(): String = "LIVE"\n'
        '@main def main: IO[Unit] = IO.println(message())\n')
    (mode / "mode.scuzz_scenario").write_text(
        'def Main.message(): String = "VERIFY"\n')
    def command(*args):
        result = subprocess.run([str(compiler), *args, str(mode)], env=env,
                                check=True, capture_output=True, text=True)
        return result.stdout.splitlines()
    assert "LIVE" in command("run", "--out-dir", "alternate")
    command("build", "--verify")
    live = mode / "alternate/fingerprint"
    verify_ir = mode / "build/output-mode.ll"
    verify_stamp = mode / "build/fingerprint.verify"
    assert not verify_stamp.exists() and not (mode / "build/fingerprint").exists()
    assert "VERIFY" in verify_ir.read_text()
    assert "LIVE" in (mode / "alternate/output-mode.ll").read_text()
    stamps = [p.stat().st_mtime_ns for p in (live, verify_ir)]
    assert "LIVE" in command("run", "--out-dir", "alternate")
    assert stamps == [p.stat().st_mtime_ns for p in (live, verify_ir)]
    assert "LIVE" in command("run")
    assert (mode / "build/fingerprint").is_file() and not verify_stamp.exists()
print("compiler cache identity and output mode ok")
PY_COMPILER_CACHE

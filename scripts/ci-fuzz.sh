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
mut = summary["mutate"]
assert len(mut["incomplete_sites"]) == mut["incomplete"], mut
assert len(mut["excluded_sites"]) == mut["excluded"], mut
assert mut["excluded_sites"] == summary["scope"]["mutant_excluded"], (mut, summary["scope"])
assert len(mut["survivors"]) == mut["survived"], mut
workload_labels = {row["label"] for row in summary["scope"]["workloads"]}
reached_claims = set(summary["scope"]["claims"]["reached"])
for survivor in mut["survivors"]:
    assert set(survivor) == {
        "site", "kind", "file", "line", "column", "end_line", "end_column",
        "diverged_workloads", "passed_claims",
    }, survivor
    assert 0 <= survivor["site"] < mut["sites"], survivor
    assert survivor["kind"] and survivor["file"].endswith(".scuzz"), survivor
    assert survivor["line"] >= 1 and survivor["column"] >= 1, survivor
    assert (survivor["end_line"], survivor["end_column"]) > (survivor["line"], survivor["column"]), survivor
    assert survivor["diverged_workloads"], survivor
    assert set(survivor["diverged_workloads"]).issubset(workload_labels), (survivor, workload_labels)
    assert set(survivor["passed_claims"]).issubset(reached_claims), (survivor, reached_claims)
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
        assert mut['incomplete_sites'] == ([1] if expected == 'incomplete' else []), report
        assert mut['excluded_sites'] == [] and mut['survivors'] == [], report
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
        assert mut['excluded_sites'] == report['scope']['mutant_excluded'], mut
        assert mut['incomplete_sites'] == [], mut
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
assert d["mutate"]["survivors"] == [] and d["mutate"]["incomplete_sites"] == [] and d["mutate"]["excluded_sites"] == []
PY
if fuzz --replay /tmp/bad-example/build/fuzz/repro.toml /tmp/bad-example; then
  echo "replay should have reproduced the property failure" && exit 1
fi
if fuzz --iterations 0 examples/bad-example; then
  echo "corpus-only should pin the bad-example failure" && exit 1
fi
rm -rf /tmp/bad-example-survivor /tmp/bad-example-survivor.log /tmp/bad-example-survivor.json
cp -R examples/bad-example /tmp/bad-example-survivor
rm -rf /tmp/bad-example-survivor/build /tmp/bad-example-survivor/.scuzz
python3 - <<'PY_FIX_BUMP'
from pathlib import Path
p = Path("/tmp/bad-example-survivor/src/Main.scuzz")
text = p.read_text()
assert "n - 1" in text
p.write_text(text.replace("n - 1", "n + 1", 1))
PY_FIX_BUMP
if fuzz --iterations 16 /tmp/bad-example-survivor > /tmp/bad-example-survivor.log 2>&1; then
  echo "weakly claimed scale and main mutants must fail the default floor" && exit 1
fi
grep -q 'mutant 2: survived at Main.scuzz:5:5 (Main.scale, arith-flip;' /tmp/bad-example-survivor.log
grep -q 'mutant 3: survived at Main.scuzz:9:21 (Main.main, arith-flip;' /tmp/bad-example-survivor.log
grep -q 'mutant 4: survived at Main.scuzz:9:18 (Main.main, int-flip;' /tmp/bad-example-survivor.log
grep -q 'mutant 5: survived at Main.scuzz:9:29 (Main.main, int-flip;' /tmp/bad-example-survivor.log
cp /tmp/bad-example-survivor/build/fuzz/summary.json /tmp/bad-example-survivor-ev.json
if "$SCUZZ" fuzz --iterations 16 --message-format=json /tmp/bad-example-survivor > /tmp/bad-example-survivor.json; then
  echo "json mode must also fail when reused survivors miss the floor" && exit 1
fi
rm -f /tmp/bad-example-survivor/.scuzz/mutate.results
if SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations 16 /tmp/bad-example-survivor > /tmp/bad-example-survivor-compiled.log 2>&1; then
  echo "compiled engine must also fail on surviving mutants" && exit 1
fi
python3 - <<'PY_SURVIVORS'
import json
from pathlib import Path
ev = json.loads(Path("/tmp/bad-example-survivor-ev.json").read_text())
reused_json = json.loads(Path("/tmp/bad-example-survivor.json").read_text())
compiled = json.loads(Path("/tmp/bad-example-survivor/build/fuzz/summary.json").read_text())
assert ev["mutate"] == reused_json["mutate"] == compiled["mutate"], (ev["mutate"], reused_json["mutate"], compiled["mutate"])
mut = ev["mutate"]
assert mut["killed"] == 2 and mut["survived"] == 4 and mut["ran"] == 6, mut
assert mut["incomplete_sites"] == [] and mut["excluded_sites"] == [], mut
by_site = {row["site"]: row for row in mut["survivors"]}
assert sorted(by_site) == [2, 3, 4, 5], by_site
want_spans = {
    2: ("arith-flip", "Main.scuzz", 5, 5, 5, 6),
    3: ("arith-flip", "Main.scuzz", 9, 21, 9, 22),
    4: ("int-flip", "Main.scuzz", 9, 18, 9, 19),
    5: ("int-flip", "Main.scuzz", 9, 29, 9, 30),
}
want_workloads = ["idle"]
for site, (kind, file_name, line, col, end_line, end_col) in want_spans.items():
    row = by_site[site]
    assert (row["kind"], row["file"], row["line"], row["column"], row["end_line"], row["end_column"]) == (
        kind, file_name, line, col, end_line, end_col
    ), row
    assert row["diverged_workloads"] == want_workloads, row
    assert row["passed_claims"] == ["wroteTotal"], row
PY_SURVIVORS
cat > /tmp/bad-example-survivor/bump.scuzz_verify <<'STRONG_VERIFY'
oracle bump(n: Int): Bool =
  Main.bump(n) == n + 1

oracle scale(n: Int): Bool =
  Main.scale(n) == n * 2

def wroteTotal(t: Timeline): Verdict =
  Verdict.every(t, i => Timeline.exists(t, k => Timeline.driveHas(t, k, "bump") || Timeline.driveHas(t, k, "scale")) || Timeline.fileTextIs(t, i, "total.txt", "3"))
STRONG_VERIFY
fuzz --iterations 16 /tmp/bad-example-survivor
python3 - <<'PY_KILLED'
import json
with open("/tmp/bad-example-survivor/build/fuzz/summary.json") as f:
    d = json.load(f)
assert d["fuzz"]["ok"] is True
assert d["mutate"]["survived"] == 0 and d["mutate"]["survivors"] == [], d["mutate"]
assert d["mutate"]["score"] == 1, d["mutate"]
PY_KILLED
rm -rf /tmp/bad-example-survivor /tmp/bad-example-survivor-ev.json /tmp/bad-example-survivor.log /tmp/bad-example-survivor-compiled.log /tmp/bad-example-survivor.json
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
assert len(d["mutate"]["survivors"]) == d["mutate"]["survived"] == 1, d["mutate"]
surv = d["mutate"]["survivors"][0]
assert (surv["site"], surv["kind"], surv["file"], surv["line"], surv["column"], surv["end_line"], surv["end_column"]) == (
    0, "int-flip", "Main.scuzz", 9, 26, 9, 27
), surv
assert "idle" in surv["diverged_workloads"], surv
assert surv["passed_claims"] == [
    "afterPlusShowsOne",
    "countFollowsTaps",
    "countStaysNonNeg",
    "neverCountNegOne",
    "plusVisible",
    "titleVisible",
], surv
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
grep -q 'mutant 0: survived at Main.scuzz:9:26 (Main.capturedLabel, int-flip;' /tmp/scuzz-mutgate-3.log
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

fuzz_both_engines examples/ide-sync 64 ide-sync
assert_fuzz_summary examples/ide-sync/build/fuzz/summary.json /tmp/scuzz-ide-sync-summary.log
python3 - <<'PY_IDE_SYNC'
import json
with open("examples/ide-sync/build/fuzz/summary.json") as f:
    sync = json.load(f)
assert sync["fuzz"]["ok"] is True
assert sync["mutate"]["survived"] == 0
assert sync["mutate"]["invalid"] == 0
assert sync["mutate"]["incomplete"] == 0
assert sync["coverage"]["reached"] == sync["coverage"]["total"]
assert {"unauthorizedMissing", "unauthorizedWrongToken", "offlineBatchReplay", "staleBetaAfterAlpha", "competingAcceptRace", "recoverAcceptJournal", "mobileSteerGenerateAndServe", "acceptAdvancesAndRetiresStale", "dirtyBlocksAndPreservesSteer", "crashActiveSteerRecovery", "verifierGateRejections"} <= set(sync["breadth"]["claimed"]["driveHas"])
assert {"ws/src/Main.scuzz", "ws/.scuzz/ide/journal.json", "ws/.scuzz/ide/steer.json", "ws-gen/src/Main.scuzz", "ws-adv/src/Main.scuzz", "ws-gen-dirty/src/Main.scuzz", "ws-gen-crash/src/Main.scuzz"} <= set(sync["breadth"]["claimed"]["fileSame"])
PY_IDE_SYNC
if ! fuzz --live --replay examples/ide-sync/live.toml examples/ide-sync | tee /tmp/scuzz-ide-sync-live.log | grep "fuzz replay ok" >/dev/null; then
  echo "examples/ide-sync: live loopback replay failed" && exit 1
fi
if ! fuzz --asan --iterations 0 examples/ide-sync | tee /tmp/scuzz-ide-sync-asan.log; then
  echo "examples/ide-sync: ASan corpus replay failed" && exit 1
fi
if grep -E 'ERROR: (Address|Leak)Sanitizer' /tmp/scuzz-ide-sync-asan.log; then
  echo "examples/ide-sync: ASan reported a defect" && exit 1
fi
python3 - "$SCUZZ" <<'PY_IDE_SERVE_CLI'
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

cli = str(Path(sys.argv[1]).resolve())
with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]

with tempfile.TemporaryDirectory(prefix="scuzz-ide-serve-") as tmp:
    root = Path(tmp)
    (root / "src").mkdir()
    (root / "scuzz.toml").write_text('[package]\nname = "sync-smoke"\n')
    (root / "src/Main.scuzz").write_text("@main def main: IO[Unit] =\n  IO.pure(())\n")

    proc = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--once", str(root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        token_path = root / ".scuzz/ide/sync-token.txt"
        deadline = time.monotonic() + 10.0
        while not token_path.exists() or not token_path.read_text().strip():
            assert proc.poll() is None, proc.communicate()
            assert time.monotonic() < deadline, "timed out waiting for sync-token.txt"
            time.sleep(0.05)
        token = token_path.read_text().strip()
        time.sleep(0.05)
        req = urllib.request.Request(f"http://127.0.0.1:{port}/v1/ide/sync")
        try:
            urllib.request.urlopen(req, timeout=5)
            raise AssertionError("expected 401 without bearer token")
        except urllib.error.HTTPError as exc:
            assert exc.code == 401, exc.code
        out, err = proc.communicate(timeout=10)
        assert proc.returncode == 0, (proc.returncode, out, err)
        assert f"pairing bearer token: {token}" in out, out
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()

    proc2 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--once", str(root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        time.sleep(0.1)
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/ide/sync",
            headers={"Authorization": f"Bearer {token}"},
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            assert resp.status == 200
            doc = json.loads(resp.read().decode())
            assert doc["v"] == 1 and doc["cards"] == [] and doc["steer"] == [], doc
        out2, err2 = proc2.communicate(timeout=10)
        assert proc2.returncode == 0, (proc2.returncode, out2, err2)
    finally:
        if proc2.poll() is None:
            proc2.kill()
            proc2.wait()

    (root / "src/Main.scuzz").write_text('def step(): IO[Unit] =\n  Fs.write("out.txt", "v1")\n\n@main def main: IO[Unit] =\n  step()\n')
    (root / "smoke.scuzz_scenario").write_text(
        'def setup(): IO[Unit] =\n  Fs.write("out.txt", "init")\n\n'
        'def runStep(): IO[Unit] =\n  Main.step()\n'
    )
    (root / "smoke.scuzz_verify").write_text(
        'def writesVersion(t: Timeline): Verdict =\n'
        '  Verdict.every(t, i => !Timeline.driveHas(t, i, "runStep") || Timeline.fileTextIs(t, i, "out.txt", "v1") || Timeline.fileTextIs(t, i, "out.txt", "v2") || Timeline.fileTextIs(t, i, "out.txt", "v3"))\n'
    )
    (root / "corpus").mkdir()
    (root / "corpus/run.toml").write_text('[fuzz]\nevents = ["drive runStep"]\n')
    fmt_run = subprocess.run([cli, "fmt", str(root)], capture_output=True, text=True, timeout=30)
    assert fmt_run.returncode == 0, (fmt_run.stdout, fmt_run.stderr)

    proc3 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--once", str(root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        time.sleep(0.1)
        steer_body = json.dumps({"v": 1, "id": "st-1", "kind": "steer", "prompt": "switch output to v2"}).encode()
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/ide/sync",
            data=steer_body,
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            assert resp.status == 200
            post_doc = json.loads(resp.read().decode())
            assert post_doc["results"][0]["status"] == "queued", post_doc
        out3, err3 = proc3.communicate(timeout=10)
        assert proc3.returncode == 0, (proc3.returncode, out3, err3)
    finally:
        if proc3.poll() is None:
            proc3.kill()
            proc3.wait()

    bad_dir = root / "build/proposals/01-bad/src"
    bad_dir.mkdir(parents=True)
    (bad_dir / "Main.scuzz").write_text("@main def main: IO[Unit] =\n  123\n")
    good_dir = root / "build/proposals/02-good/src"
    good_dir.mkdir(parents=True)
    (good_dir / "Main.scuzz").write_text('def step(): IO[Unit] =\n  Fs.write("out.txt", "v2")\n\n@main def main: IO[Unit] =\n  step()\n')

    crank = subprocess.run(
        [cli, "ide", "crank", "--rounds", "2", "--message-format=json", str(root)],
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert crank.returncode == 0, (crank.returncode, crank.stdout, crank.stderr)
    assert len(crank.stdout.splitlines()) == 1, crank.stdout
    crank_doc = json.loads(crank.stdout)
    assert crank_doc["published"] == 1 and crank_doc["rejected"] == 1, crank_doc

    proc4 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--once", "--message-format=json", str(root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        listen_doc = json.loads(proc4.stdout.readline())
        assert listen_doc["status"] == "listening" and listen_doc["port"] == port, listen_doc
        req = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/ide/sync",
            headers={"Authorization": f"Bearer {token}"},
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            assert resp.status == 200
            feed_doc = json.loads(resp.read().decode())
            assert len(feed_doc["cards"]) == 1 and feed_doc["cards"][0]["proposal"] == "02-good", feed_doc
            assert feed_doc["steer"] == [], feed_doc
        out4, err4 = proc4.communicate(timeout=10)
        assert proc4.returncode == 0 and out4 == "", (proc4.returncode, out4, err4)
    finally:
        if proc4.poll() is None:
            proc4.kill()
            proc4.wait()

    gen_dir = root / "build/proposals/03-gen/src"
    gen_dir.mkdir(parents=True)
    (gen_dir / "Main.scuzz").write_text('def step(): IO[Unit] =\n  Fs.write("out.txt", "v3")\n\n@main def main: IO[Unit] =\n  step()\n')

    proc5 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--generate", "--rounds", "1", str(root)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        card_id = feed_doc["cards"][0]["id"]
        base_v1 = feed_doc["baseline"]
        accept_batch = json.dumps({
            "v": 1,
            "actions": [
                {"id": "acc-1", "kind": "accept", "card": card_id, "baseline": base_v1},
                {"id": "st-2", "kind": "steer", "prompt": "switch output to v3"},
            ],
        }).encode()
        deadline = time.monotonic() + 10.0
        posted = False
        while not posted:
            assert proc5.poll() is None, proc5.communicate()
            assert time.monotonic() < deadline, "timed out posting accept+steer to serve --generate"
            try:
                req = urllib.request.Request(
                    f"http://127.0.0.1:{port}/v1/ide/sync",
                    data=accept_batch,
                    headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
                    method="POST",
                )
                with urllib.request.urlopen(req, timeout=5) as resp:
                    assert resp.status == 200
                    batch_doc = json.loads(resp.read().decode())
                    assert [r["status"] for r in batch_doc["results"]] == ["applied", "queued"], batch_doc
                    posted = True
            except urllib.error.URLError:
                time.sleep(0.05)

        deadline = time.monotonic() + 20.0
        ready_v3 = None
        while ready_v3 is None:
            assert proc5.poll() is None, proc5.communicate()
            assert time.monotonic() < deadline, "timed out waiting for background --generate card"
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}/v1/ide/sync",
                headers={"Authorization": f"Bearer {token}"},
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                doc5 = json.loads(resp.read().decode())
                if doc5["cards"] and doc5["cards"][0]["proposal"] == "03-gen":
                    ready_v3 = doc5
                else:
                    time.sleep(0.1)
        assert ready_v3["baseline"] != base_v1, ready_v3
        assert ready_v3["steer"] == [], ready_v3
    finally:
        if proc5.poll() is None:
            proc5.terminate()
            try:
                proc5.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc5.kill()
                proc5.wait()
PY_IDE_SERVE_CLI

rm -f examples/ide-mobile/.scuzz/mutate.results
fuzz --iterations 64 examples/ide-mobile | tee /tmp/scuzz-ide-mobile-summary.log
if grep -q 'probes run compiled' /tmp/scuzz-ide-mobile-summary.log; then
  echo "examples/ide-mobile: the evaluator engine fell back to compiled probes" && exit 1
fi
assert_fuzz_summary examples/ide-mobile/build/fuzz/summary.json /tmp/scuzz-ide-mobile-summary.log
python3 - <<'PY_IDE_MOBILE'
import json
with open("examples/ide-mobile/build/fuzz/summary.json") as f:
    mob = json.load(f)
assert mob["fuzz"]["ok"] is True
assert mob["mutate"]["survived"] == 0
assert mob["mutate"]["invalid"] == 0
assert mob["mutate"]["incomplete"] == 0
assert not mob["triggers"]["never"], mob["triggers"]
assert "slider:Swipe card" in mob["triggers"]["reached"]
assert {"offlineSwipeSteerRestartFlush", "retryIdempotentBatch", "staleAndDirtyHandling", "unauthorizedKeepsOutbox", "chunkedLargeOutbox", "corruptStoreRecovery"} <= set(mob["claim_drives"]["reached"])
PY_IDE_MOBILE
SCUZZ_FUZZ_ENGINE=compiled fuzz --iterations 0 examples/ide-mobile
python3 - "$SCUZZ" <<'PY_IDE_MOBILE_LIVE'
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

cli = str(Path(sys.argv[1]).resolve())
repo = Path.cwd()
app = str((repo / "examples/ide-mobile").resolve())

def source_hash(files):
    rows = [{"path": p, "content": c} for p, c in sorted(files)]
    return hashlib.sha256(json.dumps(rows, separators=(",", ":")).encode()).hexdigest()

with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]

with tempfile.TemporaryDirectory(prefix="scuzz-ide-mobile-live-") as tmp:
    base = Path(tmp)
    ws = base / "ws"
    mob_dir = base / "mobile-state"
    out_dir = base / "mobile-build"
    (ws / "src").mkdir(parents=True)
    (ws / ".scuzz/ide/cards").mkdir(parents=True)
    (ws / "scuzz.toml").write_text('[package]\nname = "demo"\n')
    (ws / "src/Main.scuzz").write_text("v0")

    card_id = "a" * 64
    base_h = source_hash([("src/Main.scuzz", "v0")])
    prop_h = source_hash([("src/Main.scuzz", "alpha")])
    card_dir = ws / ".scuzz/ide/cards" / card_id
    card_dir.mkdir(parents=True)
    (card_dir / "card.json").write_text(json.dumps({
        "v": 1,
        "id": card_id,
        "proposal": "hidden-candidate-name",
        "publication": '{"generator":"local"}',
        "objective": "ship mobile deck",
        "baseline": [{"path": "src/Main.scuzz", "content": "v0"}],
        "proposed": [{"path": "src/Main.scuzz", "content": "alpha"}],
        "a": base_h,
        "b": prop_h,
        "flip": 0,
        "started": 1,
        "ready": 2,
        "status": "ready",
        "question": {"workload": "idle", "step": 0},
        "evidence": {"workloads": [{"label": "idle", "delta": {"changes": [{"section": "signals"}]}}]},
    }))

    token = "live-mobile-token-123456"
    env = dict(
        os.environ,
        SCUZZ_IDE_MOBILE_DIR=str(mob_dir),
        SCUZZ_IDE_HOST="127.0.0.1",
        SCUZZ_IDE_PORT=str(port),
        SCUZZ_IDE_TOKEN=token,
    )

    def run_mobile(events):
        script = base / "script.json"
        script.write_text(json.dumps({"v": 1, "kind": "inject", "events": events}))
        subprocess.run(
            [cli, "run", "--target", "headless", "--out-dir", str(out_dir), "--exec", str(script), app],
            check=True,
            env=env,
        )

    # 1. Online prefetch from scuzz ide serve (--once serves GET /v1/ide/sync).
    srv1 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--token", token, "--once", str(ws)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        time.sleep(0.1)
        run_mobile([{"op": "tap", "id": "button:Sync"}])
        out1, err1 = srv1.communicate(timeout=10)
        assert srv1.returncode == 0, (srv1.returncode, out1, err1)
    finally:
        if srv1.poll() is None:
            srv1.kill()
            srv1.wait()

    state1 = json.loads((mob_dir / "state.json").read_text())
    assert len(state1["snapshot"]["cards"]) == 1, state1
    assert "proposal" not in json.dumps(state1), state1

    # 2. Restart offline: swipe-drag right to accept, queue a steer prompt, and try Sync while server is down.
    run_mobile([
        {"op": "drag", "x1": 195, "y1": 608, "x2": 315, "y2": 608},
        {"op": "type", "i": 0, "value": "keep mobile fast"},
        {"op": "tap", "id": "button:Queue steer"},
        {"op": "tap", "id": "button:Sync"},
    ])
    state2 = json.loads((mob_dir / "state.json").read_text())
    assert state2["sync_status"] == "Offline", state2
    assert [a["kind"] for a in state2["outbox"]] == ["accept", "steer"], state2

    # 3. Reconnect after second restart: flush FIFO outbox (POST) + refresh feed (GET) against live scuzz ide serve.
    srv2 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(port), "--token", token, str(ws)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        time.sleep(0.1)
        run_mobile([{"op": "tap", "id": "button:Sync"}])
    finally:
        srv2.terminate()
        try:
            srv2.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv2.kill()
            srv2.wait()

    state3 = json.loads((mob_dir / "state.json").read_text())
    assert state3["outbox"] == [], state3
    assert state3["sync_status"] == "Synced rev 2", state3
    assert (ws / "src/Main.scuzz").read_text() == "alpha"
    steer_doc = json.loads((ws / ".scuzz/ide/steer.json").read_text())
    assert [s["prompt"] for s in steer_doc["items"]] == ["keep mobile fast"], steer_doc
PY_IDE_MOBILE_LIVE

if ! fuzz --replay examples/editor/corpus/c_complete_session_ui.toml examples/editor | grep "fuzz replay ok" >/dev/null; then
  echo "examples/editor: complete session evaluator replay failed" && exit 1
fi
if ! SCUZZ_FUZZ_ENGINE=compiled fuzz --replay examples/editor/corpus/c_complete_session_ui.toml examples/editor | grep "fuzz replay ok" >/dev/null; then
  echo "examples/editor: complete session compiled replay failed" && exit 1
fi

python3 - "$SCUZZ" <<'PY_IDE_COMPLETE_SESSION'
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

cli = str(Path(sys.argv[1]).resolve())
cli_sha = hashlib.sha256(Path(cli).read_bytes()).hexdigest()
repo = Path.cwd()


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def source_hash(files):
    rows = [{"path": p, "content": c} for p, c in sorted(files)]
    return hashlib.sha256(json.dumps(rows, separators=(",", ":")).encode()).hexdigest()


def fmt_in_pkg(base_dir, rel, body):
    pkg = base_dir / "fmt-pkg"
    shutil.rmtree(pkg, ignore_errors=True)
    (pkg / "src").mkdir(parents=True, exist_ok=True)
    (pkg / "scuzz.toml").write_text('[package]\nname = "fmtpkg"\n')
    if not rel.startswith("src/"):
        (pkg / "src/Dummy.scuzz").write_text("def d(): Int =\n  0\n")
    target = pkg / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(body)
    subprocess.run([cli, "fmt", str(pkg)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return target.read_text()


def read_json(path):
    try:
        return json.loads(path.read_text()) if path.is_file() else {}
    except Exception:
        return {}


def publish_behavior(ws, name, request_doc, files, generator="session-proof", override_base=None):
    stage = ws / "build" / f"{name}.stage"
    dest = ws / "build/proposals" / name
    shutil.rmtree(stage, ignore_errors=True)
    shutil.rmtree(dest, ignore_errors=True)
    meta_files = []
    for rel, body in files:
        formatted = fmt_in_pkg(ws.parent, rel, body)
        target = stage / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(formatted)
        meta_files.append({"path": rel, "sha256": hashlib.sha256(formatted.encode()).hexdigest()})
    (stage / "proposal.json").write_text(json.dumps({
        "v": 1,
        "request": request_doc["request"],
        "baseline": override_base or request_doc["baseline"],
        "kind": "behavior",
        "generator": generator,
        "files": meta_files,
    }))
    dest.parent.mkdir(parents=True, exist_ok=True)
    stage.replace(dest)


def publish_rule(ws, name, rule_req, rel_verify, verify_body):
    stage = ws / "build" / f"{name}.stage"
    dest = ws / "build/proposals" / name
    shutil.rmtree(stage, ignore_errors=True)
    shutil.rmtree(dest, ignore_errors=True)
    stage.mkdir(parents=True, exist_ok=True)
    formatted = fmt_in_pkg(ws.parent, rel_verify, verify_body)
    vpath = stage / rel_verify
    vpath.write_text(formatted)
    (stage / "proposal.json").write_text(json.dumps({
        "v": 1,
        "request": rule_req["request"],
        "baseline": rule_req["baseline"],
        "kind": "rule",
        "witness": rule_req["witness"]["id"],
        "generator": "session-rule-proof",
        "files": [{"path": rel_verify, "sha256": hashlib.sha256(formatted.encode()).hexdigest()}],
    }))
    dest.parent.mkdir(parents=True, exist_ok=True)
    stage.replace(dest)


def write_ops(where, events):
    path = where / "inject.json"
    temp = where / "inject.tmp"
    temp.write_text(json.dumps({"v": 1, "kind": "inject", "stamp": time.monotonic_ns(), "events": events}))
    temp.replace(path)
    return path


class HeadlessEditor:
    def __init__(self, editor_exe, ws, initial_events, extra_env=None):
        self.ws = ws
        empty = ws / "empty-script.json"
        empty.write_text(json.dumps({"v": 1, "kind": "inject", "events": []}))
        inject = write_ops(ws, [])
        (ws / "debug.json").unlink(missing_ok=True)
        env = dict(
            os.environ,
            SCUZZ=cli,
            SCUZZ_EXECUTABLE_SHA256=cli_sha,
            SCUZZ_HOME=str(repo),
            SCUZZ_UI_RUNTIME="headless",
            SCUZZ_UI_WIDTH="1280",
            SCUZZ_UI_HEIGHT="900",
            SCUZZ_UI_SERVE="1",
            SCUZZ_LIVE_FRAMES="30000",
            SCUZZ_UI_SCRIPT=str(empty),
            SCUZZ_UI_INJECT=str(inject),
            SCUZZ_UI_DEBUG_DUMP=str(ws / "debug.json"),
        )
        if extra_env:
            env.update(extra_env)
        self.log_path = ws / "headless-editor.log"
        self.log = open(self.log_path, "w")
        self.proc = subprocess.Popen([str(editor_exe)], cwd=str(ws), env=env, stdout=self.log, stderr=self.log)
        self.wait_for(lambda: (self.ws / "debug.json").is_file(), label="initial debug.json")
        if initial_events:
            self.send(initial_events)

    def ui(self):
        return json.loads((self.ws / "debug.json").read_text())

    def signals(self):
        return {v["name"]: v.get("value") for v in self.ui().get("signals", []) if "name" in v}

    def send(self, events):
        write_ops(self.ws, events)

    def wait_for(self, predicate, label="condition", timeout=45.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            assert self.proc.poll() is None, f"editor exited early during {label}:\n" + self.log_path.read_text()[-4000:]
            try:
                res = predicate()
            except (FileNotFoundError, json.JSONDecodeError, KeyError, StopIteration):
                res = None
            if res:
                return res
            time.sleep(0.05)
        sig_dump = ""
        cards_dump = []
        recs_dump = []
        try:
            big = {"buf", "orig", "bodies", "origs", "deckCard", "ruleCard", "deltaRaw", "laneTexts", "laneA", "laneB", "rowsA", "rowsB", "stepViewA", "stepViewB", "srcFiles"}
            sig_dump = json.dumps({k: v for k, v in self.signals().items() if k not in big})
            cards_dump = [(c.get("proposal"), c.get("status"), c.get("diagnostic")) for p in sorted((self.ws / ".scuzz/ide/cards").glob("*/card.json")) for c in [json.loads(p.read_text())]]
            recs_dump = [(r.get("proposal"), r.get("kind"), r.get("decision"), r.get("diagnostic")) for p in sorted((self.ws / ".scuzz/ide/records").glob("*.json")) for r in [json.loads(p.read_text())]]
        except Exception:
            pass
        raise AssertionError(f"timed out waiting for {label} (signals={sig_dump}, cards={cards_dump}, recs={recs_dump}):\n" + self.log_path.read_text()[-4000:])

    def goto_section(self, idx, chip_label):
        if self.signals().get("developSection") != idx:
            self.send([{"op": "tap", "id": f"choicechip:{chip_label}"}])
            self.wait_for(lambda: self.signals().get("developSection") == idx, label=f"section {chip_label}")

    def wait_armed_card(self, expected_proposal=None):
        def check():
            sigs = self.signals()
            if sigs.get("deckBusy") != 0 or not sigs.get("deckArmed"):
                return None
            if "Choose left" not in json.dumps(self.ui().get("taps", [])):
                return None
            card_path = self.ws / ".scuzz/ide/cards" / sigs["deckArmed"] / "card.json"
            if not card_path.is_file():
                return None
            card = json.loads(card_path.read_text())
            if card.get("status") != "ready":
                return None
            if expected_proposal and card.get("proposal") != expected_proposal:
                return None
            return card
        return self.wait_for(check, label=f"armed card {expected_proposal or '*'}", timeout=180.0)

    def close(self, expect_zero=True):
        try:
            if self.proc.poll() is None:
                self.send([{"op": "quit"}])
                rc = self.proc.wait(timeout=15)
                if expect_zero:
                    assert rc == 0, (rc, self.log_path.read_text()[-3000:])
        finally:
            if self.proc.poll() is None:
                self.proc.kill()
                self.proc.wait()
            self.log.close()


with tempfile.TemporaryDirectory(prefix="scuzz-ide-e2e-") as tmp:
    base = Path(tmp)
    # Copy shared + counter for UI scratch workspace, and webhook for IO scratch workspace.
    shutil.copytree(repo / "examples/shared", base / "shared", ignore=shutil.ignore_patterns("build", ".scuzz"))
    ui_ws = base / "ui-counter"
    shutil.copytree(repo / "examples/counter", ui_ws, ignore=shutil.ignore_patterns("build", ".scuzz"))
    io_ws = base / "io-webhook"
    shutil.copytree(repo / "examples/webhook", io_ws, ignore=shutil.ignore_patterns("build", ".scuzz"))

    # Build Headless editor executable once and reuse across UI and IO scratch workspaces.
    editor_out = base / "editor-out"
    quit_json = base / "quit.json"
    quit_json.write_text(json.dumps({"v": 1, "kind": "inject", "events": [{"op": "quit"}]}))
    subprocess.run(
        [cli, "run", "--target", "headless", "--out-dir", str(editor_out), "--exec", str(quit_json), str(repo / "examples/editor")],
        cwd=str(ui_ws),
        env=dict(os.environ, SCUZZ_HOME=str(repo), SCUZZ_UI_RUNTIME="headless", SCUZZ_UI_WIDTH="960", SCUZZ_UI_HEIGHT="560"),
        check=True,
        timeout=300,
        stdout=subprocess.DEVNULL,
    )
    editor_exe = editor_out / "editor"
    assert editor_exe.is_file()

    # Build controlled private-loopback HTTP local-inference harness (127.0.0.1) for real process generation + interruption cleanup.
    gen_pkg = base / "controlled-gen"
    shutil.copytree(repo / "examples/editor/generation", gen_pkg / "generation", ignore=shutil.ignore_patterns("build", ".scuzz"))
    gen_toml = gen_pkg / "generation/scuzz.toml"
    gen_toml.write_text(gen_toml.read_text().replace("../../compiler", os.path.relpath(repo / "examples/compiler", gen_pkg / "generation")))
    server_py = b'''#!/usr/bin/env python3
import http.server, json, os, pathlib, sys, time
if "--version" in sys.argv:
    print("build 11146, commit 7fe450e19")
    sys.exit(0)
if "--list-devices" in sys.argv:
    print("Available devices:\\n  Vulkan0: Controlled GPU")
    sys.exit(0)
args = sys.argv[1:]
port = int(args[args.index("--port") + 1])
key = args[args.index("--api-key") + 1]
pathlib.Path(os.environ["CONTROLLED_PID"]).write_text(str(os.getpid()))
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        assert self.headers["Authorization"] == "Bearer " + key
        self.reply({"data": [{"id": key}]})
    def do_POST(self):
        assert self.headers["Authorization"] == "Bearer " + key
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if self.path == "/apply-template":
            self.reply({"prompt": "controlled prompt"})
            return
        if self.path == "/tokenize":
            self.reply({"tokens": [1, 2, 3]})
            return
        pathlib.Path(os.environ["CONTROLLED_RECEIVED"]).write_text("1")
        if os.environ.get("CONTROLLED_MODE") == "hang":
            time.sleep(120)
        proposal = json.loads(pathlib.Path(os.environ["CONTROLLED_PROPOSAL"]).read_text())
        self.reply({"choices": [{"finish_reason": "stop", "message": {"content": json.dumps(proposal)}}]})
http.server.HTTPServer(("127.0.0.1", port), Handler).serve_forever()
'''
    archive_buf = io.BytesIO()
    with tarfile.open(fileobj=archive_buf, mode="w:gz") as tar:
        entry = tarfile.TarInfo("llama-b11146/llama-server")
        entry.size = len(server_py)
        entry.mode = 0o700
        tar.addfile(entry, io.BytesIO(server_py))
    artifact = archive_buf.getvalue()
    weights = b"controlled-e2e-weights"
    models_scuzz = gen_pkg / "generation/src/Models.scuzz"
    msrc = models_scuzz.read_text()
    for cnt, dig in (
        (2740937888, "00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4"),
        (5680522464, "03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8"),
    ):
        msrc = msrc.replace(str(cnt), str(len(weights))).replace(dig, hashlib.sha256(weights).hexdigest())
    msrc = re.sub(
        r'Backend\("([^"]+)", "([^"]+)", [0-9]+, "[a-f0-9]+"\)',
        lambda m: f'Backend("{m[1]}", "{m[2]}", {len(artifact)}, "{hashlib.sha256(artifact).hexdigest()}")',
        msrc,
    )
    models_scuzz.write_text(msrc)
    gen_scuzz = gen_pkg / "generation/src/Generate.scuzz"
    gsrc = gen_scuzz.read_text()
    gsrc = gsrc.replace('port <- Random.nextInt(30000)', 'port <- Sys.getenv("CONTROLLED_PORT").map(s => Str.toInt(s, 0))').replace("port + 30000", "port").replace('Sys.getenv("SCUZZ_EXECUTABLE_SHA256")', 'Sys.getenv("CONTROLLED_COMPILER")')
    gen_scuzz.write_text(gsrc)
    xdg_cache = gen_pkg / "xdg"
    bplat = platform.system() + "-" + platform.machine()
    if bplat in ("Linux-x86_64", "Linux-aarch64"):
        bplat += "-vulkan"
    bdir = xdg_cache / "scuzz/tools/llama/b11146" / bplat
    bdir.mkdir(parents=True)
    (bdir / "release.tar.gz").write_bytes(artifact)
    mcache = gen_pkg / "mcache/qwen3.5-4b/e87f176479d0855a907a41277aca2f8ee7a09523"
    mcache.mkdir(parents=True)
    (mcache / "Qwen3.5-4B-Q4_K_M.gguf").write_bytes(weights)
    tools_dir = gen_pkg / "tools"
    tools_dir.mkdir()
    real_awk = shutil.which("awk")
    assert real_awk
    (tools_dir / "awk").write_text(
        '#!/bin/sh\ncase "$1" in *MemAvailable:*) printf \'34359738368\\n\';; *"Pages free:"*) printf \'34359738368\\n\';; *) exec '
        + shlex.quote(real_awk)
        + ' "$@";; esac\n'
    )
    (tools_dir / "awk").chmod(0o700)
    (gen_pkg / "src").mkdir()
    (gen_pkg / "scuzz.toml").write_text('[package]\nname = "gen-driver"\n[dependencies]\ngeneration = { path = "generation" }\n')
    (gen_pkg / "src/Main.scuzz").write_text(
        '@main def main: IO[Unit] =\n'
        '  for {\n'
        '    root <- Sys.getenv("CONTROLLED_ROOT")\n'
        '    req <- Sys.getenv("CONTROLLED_REQ")\n'
        '    out <- Sys.getenv("CONTROLLED_OUT")\n'
        '    mode <- Sys.getenv("CONTROLLED_MODE")\n'
        '    _ <- if (root == "") IO.pure(()) else if (mode == "hang") IO.timeout(2500, Generate.command(root, req, "qwen3.5-4b", out, false)) else Generate.command(root, req, "qwen3.5-4b", out, false)\n'
        '  } yield ()\n'
    )
    gen_base_env = dict(
        os.environ,
        SCUZZ=cli,
        SCUZZ_EXECUTABLE_SHA256=cli_sha,
        CONTROLLED_COMPILER=cli_sha,
        SCUZZ_HOME=str(repo),
        PATH=str(tools_dir) + os.pathsep + os.environ["PATH"],
        XDG_CACHE_HOME=str(xdg_cache),
        SCUZZ_MODEL_CACHE=str(gen_pkg / "mcache"),
        CONTROLLED_ROOT="",
        CONTROLLED_REQ="",
        CONTROLLED_OUT="",
        CONTROLLED_MODE="ok",
    )
    subprocess.run([cli, "run", str(gen_pkg)], env=gen_base_env, check=True, timeout=240, stdout=subprocess.DEVNULL)
    gen_driver = gen_pkg / "build/gen-driver"

    def run_controlled_local_gen(ws, req_path, out_dir, edits, change="Controlled edit", hang=False):
        pid_file = base / "llama.pid"
        rec_file = base / "llama.rec"
        prop_file = base / "llama.proposal.json"
        pid_file.unlink(missing_ok=True)
        rec_file.unlink(missing_ok=True)
        prop_file.write_text(json.dumps({"change": change, "edits": edits}))
        env = dict(
            gen_base_env,
            CONTROLLED_ROOT=str(ws),
            CONTROLLED_REQ=str(req_path),
            CONTROLLED_OUT=str(out_dir),
            CONTROLLED_PID=str(pid_file),
            CONTROLLED_RECEIVED=str(rec_file),
            CONTROLLED_PROPOSAL=str(prop_file),
            CONTROLLED_PORT=str(free_port()),
            CONTROLLED_MODE="hang" if hang else "ok",
        )
        return subprocess.run([str(gen_driver)], env=env, capture_output=True, text=True, timeout=30), pid_file, rec_file

    # =========================================================================
    # PART A: Complete UI Objective Session (scratch copy of examples/counter)
    # =========================================================================
    ui_orig = (ui_ws / "src/Main.scuzz").read_text()
    ui_verify_orig = (ui_ws / "count.scuzz_verify").read_text()
    target_snippet = "View.bindText(label),"
    assert target_snippet in ui_orig
    ui_anchor_line = next(line for line in ui_orig.splitlines() if target_snippet in line)
    ui_abstain_line = ui_anchor_line.replace(target_snippet, 'View.text("Tap +1"), View.bindText(label),', 1)

    def fmt_src(body, rel="src/Main.scuzz"):
        return fmt_in_pkg(base, rel, body)

    ui_abstain_src = fmt_src(ui_orig.replace(target_snippet, 'View.text("Tap +1"), View.bindText(label),', 1))
    ui_base_src = fmt_src(ui_orig.replace(target_snippet, 'View.text("Ready"), View.bindText(label),', 1))
    ui_reset_src = fmt_src(ui_orig.replace(
        target_snippet,
        'View.bindText(label), View.button("Reset", _ => Signal.set(state, Signal.get(state).copy(value = 0))),',
        1,
    ))
    ui_next_src = fmt_src(ui_reset_src.replace(
        'View.button("Reset"',
        'View.text("Reset ready"), View.button("Reset"',
        1,
    ))
    ui_violate_rule_src = fmt_src(ui_orig.replace(target_snippet, 'View.text("Dropped reset"), View.bindText(label),', 1))
    ui_nodiff_src = fmt_src(ui_orig + "\ndef unusedSessionHelper(): Int =\n  0\n")
    ui_bad_static_src = ui_orig.replace('CounterState(0, "Counter")', 'CounterState(1 + "bad", "Counter")', 1)
    ui_bad_claim_src = ui_orig.replace("Signal.get(count) + 1", "Signal.get(count) + 2", 1)

    ed = HeadlessEditor(editor_exe, ui_ws, [])
    lanes_tapped = set()
    try:
        ed.goto_section(2, "Session")
        ed.send([
            {"op": "text", "i": 0, "value": "Add a Reset button to Counter"},
            {"op": "text", "i": 1, "value": "src/Main.scuzz"},
            {"op": "tap", "id": "button:Start"},
        ])
        ed.wait_for(
            lambda: bool(read_json(ui_ws / "build/ide/request.json").get("baseline")) and "External proposals: publish a complete directory" in json.dumps(ed.ui()),
            label="initial UI session started",
        )
        ed.goto_section(0, "Review")
        req1 = json.loads((ui_ws / "build/ide/request.json").read_text())
        assert req1["objective"] == "Add a Reset button to Counter", req1

        # 1a. Interrupted local inference process cleanup over private loopback HTTP.
        hang_res, hang_pid_file, hang_rec = run_controlled_local_gen(
            ui_ws,
            ui_ws / "build/ide/request.json",
            ui_ws / "build/proposals/00-hang",
            [{"path": "src/Main.scuzz", "search": ui_anchor_line, "replacement": ui_abstain_line}],
            hang=True,
        )
        assert hang_res.returncode != 0 and hang_rec.is_file(), (hang_res.returncode, hang_res.stdout, hang_res.stderr)
        spawned_pid = int(hang_pid_file.read_text().strip())
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            try:
                os.kill(spawned_pid, 0)
                time.sleep(0.05)
            except ProcessLookupError:
                break
        else:
            raise AssertionError(f"interrupted llama-server pid {spawned_pid} still alive")
        shutil.rmtree(ui_ws / "build/proposals/00-hang", ignore_errors=True)

        # 1b. Invalid path, stale baseline, and static failure checks via review-suggestion before publishing live queue items.
        bad_escape = ui_ws / "build/00-unsafe-check"
        bad_escape.mkdir(parents=True, exist_ok=True)
        (bad_escape / "proposal.json").write_text(json.dumps({
            "v": 1,
            "request": req1["request"],
            "baseline": req1["baseline"],
            "kind": "behavior",
            "generator": "unsafe",
            "files": [{"path": "../escape.scuzz", "sha256": "a" * 64}],
        }))
        bad_rev = subprocess.run(
            [cli, "ide", "review-suggestion", str(ui_ws), "--request", str(ui_ws / "build/ide/request.json"), "--candidate", str(bad_escape)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert bad_rev.returncode != 0 and not (ui_ws.parent / "escape.scuzz").exists(), (bad_rev.stdout, bad_rev.stderr)

        stale_cand = ui_ws / "build/01-stale-check"
        (stale_cand / "src").mkdir(parents=True, exist_ok=True)
        (stale_cand / "src/Main.scuzz").write_text(ui_abstain_src)
        (stale_cand / "proposal.json").write_text(json.dumps({
            "v": 1,
            "request": req1["request"],
            "baseline": "0" * 64,
            "kind": "behavior",
            "generator": "stale",
            "files": [{"path": "src/Main.scuzz", "sha256": hashlib.sha256(ui_abstain_src.encode()).hexdigest()}],
        }))
        stale_rev = subprocess.run(
            [cli, "ide", "review-suggestion", str(ui_ws), "--request", str(ui_ws / "build/ide/request.json"), "--candidate", str(stale_cand)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert stale_rev.returncode != 0, (stale_rev.stdout, stale_rev.stderr)

        static_cand = ui_ws / "build/02-static-check"
        (static_cand / "src").mkdir(parents=True, exist_ok=True)
        (static_cand / "src/Main.scuzz").write_text(ui_bad_static_src)
        (static_cand / "proposal.json").write_text(json.dumps({
            "v": 1,
            "request": req1["request"],
            "baseline": req1["baseline"],
            "kind": "behavior",
            "generator": "static-bad",
            "files": [{"path": "src/Main.scuzz", "sha256": hashlib.sha256(ui_bad_static_src.encode()).hexdigest()}],
        }))
        static_rev = subprocess.run(
            [cli, "ide", "review-suggestion", str(ui_ws), "--request", str(ui_ws / "build/ide/request.json"), "--candidate", str(static_cand)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        assert static_rev.returncode != 0, (static_rev.stdout, static_rev.stderr)

        # 1c. Claim-failing candidate pauses external queue with exact claim diagnostic, then Retry preparation continues!
        publish_behavior(ui_ws, "03-claim-fail", req1, [("src/Main.scuzz", ui_bad_claim_src)])
        ed.wait_for(
            lambda: "claim afterPlusShowsOne rejected" in ed.signals().get("readyStatus", ""),
            label="03-claim-fail rejected by afterPlusShowsOne",
            timeout=120.0,
        )
        shutil.rmtree(ui_ws / "build/proposals/03-claim-fail", ignore_errors=True)

        # 1d. Controlled local model generation over private loopback HTTP publishes 05-abstain, alongside no-diff, duplicate, and 07-base (filling 2-card ready queue).
        publish_behavior(ui_ws, "04-nodiff", req1, [("src/Main.scuzz", ui_nodiff_src)])
        gen_res, _, _ = run_controlled_local_gen(
            ui_ws,
            ui_ws / "build/ide/request.json",
            ui_ws / "build/proposals/05-abstain",
            [{"path": "src/Main.scuzz", "search": ui_anchor_line, "replacement": ui_abstain_line}],
            change="Add Tap +1 prompt",
        )
        assert gen_res.returncode == 0 and json.loads(gen_res.stdout)["result"] == "published", (gen_res.stdout, gen_res.stderr)
        publish_behavior(ui_ws, "06-dup", req1, [("src/Main.scuzz", ui_abstain_src)])
        publish_behavior(ui_ws, "07-base", req1, [("src/Main.scuzz", ui_base_src)])

        ed.send([{"op": "tap", "id": "button:Retry preparation"}])
        c_abstain = ed.wait_armed_card("05-abstain")

        # Verify short witness retained on disk (compiled replay checked after editor closes so ui-host is not rebuilt mid-session).
        witnesses = c_abstain["evidence"].get("witnesses", [])
        assert witnesses, c_abstain["evidence"]
        wit_path = ui_ws / ".scuzz/ide/cards" / c_abstain["id"] / witnesses[0]["path"]
        assert wit_path.is_file()

        # Abstain with explicit reason ("Need another scenario") and verify reset + isolated reveal + warm next card 07-base!
        ed.send([{"op": "tap", "id": "choicechip:Need another scenario"}, {"op": "tap", "id": "outlined:Can't decide"}])
        c_base = ed.wait_armed_card("07-base")
        sigs_after_abstain = ed.signals()
        assert sigs_after_abstain["abstainReason"] == 0, sigs_after_abstain
        assert "abstained" in sigs_after_abstain.get("deckLast", "") and "07-base" not in sigs_after_abstain.get("deckLast", ""), sigs_after_abstain

        # Choose baseline lane on 07-base, then publish 08-reset against the refreshed request folding abstain + baseline feedback!
        base_lane = "Choose left" if c_base["flip"] == 0 else "Choose right"
        lanes_tapped.add(base_lane)
        ed.send([{"op": "tap", "id": f"button:{base_lane}"}])
        ed.wait_for(
            lambda: ed.signals().get("deckArmed") == "" and ed.signals().get("deckBusy") == 0 and len(read_json(ui_ws / "build/ide/request.json").get("feedback", [])) >= 2,
            label="request refreshed with abstain + baseline feedback",
        )
        req_after_prefs = json.loads((ui_ws / "build/ide/request.json").read_text())
        publish_behavior(ui_ws, "08-reset", req_after_prefs, [("src/Main.scuzz", ui_reset_src)])
        c_reset = ed.wait_armed_card("08-reset")

        # Dirty buffer refusal check before accepting 08-reset.
        ed.goto_section(1, "Code")
        ed.wait_for(lambda: ed.ui().get("editors"), label="Code editor")
        ed.send([{"op": "caret", "offset": len(ui_orig)}, {"op": "key", "key": "x", "text": "x"}])
        ed.wait_for(lambda: ed.signals().get("buf") == ui_orig + "x", label="dirty buffer edit")
        reset_prop_lane = "Choose left" if c_reset["flip"] == 1 else "Choose right"
        ed.goto_section(0, "Review")
        ed.send([{"op": "tap", "id": f"button:{reset_prop_lane}"}])
        ed.wait_for(lambda: "dirty" in json.dumps(ed.ui()).lower(), label="dirty buffer refusal")
        assert (ui_ws / "src/Main.scuzz").read_text() == ui_orig

        # Revert dirty edit, re-arm 08-reset after dirty retry, then accept 08-reset via its proposal lane!
        ed.goto_section(1, "Code")
        ed.send([{"op": "caret", "offset": len(ui_orig) + 1}, {"op": "key", "key": "Backspace", "text": ""}])
        ed.wait_for(lambda: ed.signals().get("buf") == ui_orig, label="reverted buffer")
        ed.goto_section(0, "Review")
        ed.send([{"op": "tap", "id": "button:Retry preparation"}])
        c_reset = ed.wait_armed_card("08-reset")
        reset_prop_lane = "Choose left" if c_reset["flip"] == 1 else "Choose right"
        lanes_tapped.add(reset_prop_lane)
        ed.send([{"op": "tap", "id": f"button:{reset_prop_lane}"}])
        ed.wait_for(
            lambda: (ui_ws / "src/Main.scuzz").read_text() == ui_reset_src
            and ed.signals().get("deckBusy") == 0
            and ed.signals().get("ruleChoiceCard") == c_reset["id"]
            and read_json(ui_ws / "build/ide/request.json").get("baseline", "") not in ("", req_after_prefs["baseline"]),
            label="08-reset accepted",
        )

        # Test region focus controls on post-choice reveal.
        ed.wait_for(lambda: "Retain region" in json.dumps(ed.ui()), label="region controls visible")
        ed.send([{"op": "tap", "id": "outlined:Retain region"}, {"op": "tap", "id": "outlined:All regions"}])

        # Rule suggestion -> untriggered draft rejected -> valid rule draft verified & installed!
        ed.send([{"op": "tap", "id": "outlined:Suggest rule"}])
        ed.wait_for(lambda: read_json(ui_ws / "build/ide/rule-request.json").get("kind") == "rule", label="rule-request.json")
        rule_req = json.loads((ui_ws / "build/ide/rule-request.json").read_text())
        assert rule_req["kind"] == "rule" and rule_req["preference"]["decision"] == "accept", rule_req

        untriggered_verify = ui_verify_orig + '\ndef untriggeredDraft(t: Timeline): Verdict =\n  Verdict.afterHit(t, "button:NeverExists", "text:Counter")\n'
        publish_rule(ui_ws, "01-bad-rule", rule_req, "count.scuzz_verify", untriggered_verify)
        ed.send([{"op": "tap", "id": "outlined:Import rule draft"}])
        ed.wait_for(
            lambda: "untriggered new claim" in ed.signals().get("ruleDiagnostic", "") and ed.signals().get("deckBusy") == 0,
            label="untriggered rule rejected",
        )
        assert ed.signals().get("ruleVerified") == 0 and "button:Install rule" not in json.dumps(ed.ui().get("taps", []))
        ed.send([{"op": "tap", "id": "outlined:Reject rule"}])
        ed.wait_for(
            lambda: ed.signals().get("ruleArmed", "") == "" and ed.signals().get("deckBusy") == 0,
            label="01-bad-rule rejected",
        )

        valid_verify = ui_verify_orig + '\ndef afterPlusKeepsReset(t: Timeline): Verdict =\n  Verdict.afterHit(t, "button:+1", "button:Reset")\n'
        publish_rule(ui_ws, "02-good-rule", rule_req, "count.scuzz_verify", valid_verify)
        ed.send([{"op": "tap", "id": "outlined:Import rule draft"}])
        ed.wait_for(
            lambda: ed.signals().get("ruleVerified") == 1 and ed.signals().get("deckBusy") == 0,
            label="valid rule verified",
        )
        assert "Not a universal proof." in json.dumps(ed.ui())
        req_before_rule = read_json(ui_ws / "build/ide/request.json")
        ed.send([{"op": "tap", "id": "button:Install rule"}])
        ed.wait_for(
            lambda: "afterPlusKeepsReset" in (ui_ws / "count.scuzz_verify").read_text()
            and "Installed rule in count.scuzz_verify" in ed.signals().get("ruleStatus", "")
            and ed.signals().get("deckBusy") == 0
            and read_json(ui_ws / "build/ide/request.json").get("inputs", "") not in ("", req_before_rule.get("inputs", "")),
            label="rule installed on disk",
        )

        # Next-baseline request.json is automatically refreshed on rule installation and folds prior abstention, baseline choice, and acceptance!
        req2 = json.loads((ui_ws / "build/ide/request.json").read_text())
        assert req2["baseline"] != req1["baseline"], (req1["baseline"], req2["baseline"])
        fb_decisions = {(f.get("decision"), f.get("reason", "")) for f in req2.get("feedback", [])}
        assert ("abstain", "need-another-scenario") in fb_decisions, req2["feedback"]
        assert ("baseline", "") in fb_decisions and ("accept", "") in fb_decisions, req2["feedback"]

        # Installed rule blocks 09-violates-rule with exact afterPlusKeepsReset rejection, while compliant 10-next-ready arms!
        ed.goto_section(0, "Review")
        publish_behavior(ui_ws, "09-violates-rule", req2, [("src/Main.scuzz", ui_violate_rule_src)])
        ed.wait_for(
            lambda: "claim afterPlusKeepsReset rejected" in ed.signals().get("readyStatus", ""),
            label="09-violates-rule blocked by installed afterPlusKeepsReset rule",
            timeout=120.0,
        )
        shutil.rmtree(ui_ws / "build/proposals/09-violates-rule", ignore_errors=True)

        publish_behavior(ui_ws, "10-next-ready", req2, [("src/Main.scuzz", ui_next_src)])
        ed.send([{"op": "tap", "id": "button:Retry preparation"}])
        c_next = ed.wait_armed_card("10-next-ready")
        auto_decisions = [json.loads(p.read_text()).get("decision") for p in (ui_ws / ".scuzz/ide/records").glob("*.json") if json.loads(p.read_text()).get("kind") == "automatic"]
        assert "no-observed-difference" in auto_decisions and "duplicate-source" in auto_decisions, auto_decisions
    finally:
        ed.close()

    # Replay the durably retained short witness on the compiled engine!
    assert wit_path.is_file()
    wit_copy = base / "ui-wit-replay"
    shutil.copytree(ui_ws, wit_copy, ignore=shutil.ignore_patterns("build", ".scuzz"))
    wit_replay = subprocess.run([cli, "fuzz", "--replay", str(wit_path), str(wit_copy)], capture_output=True, text=True, timeout=180)
    assert wit_replay.returncode == 0 and "fuzz replay ok" in wit_replay.stdout, (wit_replay.stdout, wit_replay.stderr)

    # Interrupted journal write via LD_PRELOAD rename fault on Linux, followed by restart recovery + Undo + Session summary!
    if os.uname().sysname == "Linux":
        fault_c = base / "fault.c"
        fault_so = base / "fault.so"
        fault_c.write_text(
            "#define _GNU_SOURCE\n#include <dlfcn.h>\n#include <errno.h>\n#include <string.h>\n"
            "int rename(const char *from, const char *to) {\n"
            "  static int failed;\n"
            "  const char *suf = \"/src/Main.scuzz\";\n"
            "  size_t n = strlen(to), m = strlen(suf);\n"
            "  if (!failed && (strcmp(to, \"src/Main.scuzz\") == 0 || (n >= m && strcmp(to + n - m, suf) == 0))) {\n"
            "    failed = 1; errno = EIO; return -1;\n"
            "  }\n"
            "  int (*next)(const char *, const char *) = dlsym(RTLD_NEXT, \"rename\");\n"
            "  return next(from, to);\n"
            "}\n"
        )
        subprocess.run(["clang", "-shared", "-fPIC", str(fault_c), "-ldl", "-o", str(fault_so)], check=True)
        ed_fault = HeadlessEditor(editor_exe, ui_ws, [{"op": "tap", "id": "button:Start"}], extra_env={"LD_PRELOAD": str(fault_so)})
        try:
            c_resumed = ed_fault.wait_armed_card("10-next-ready")
            next_prop_lane = "Choose left" if c_resumed["flip"] == 1 else "Choose right"
            lanes_tapped.add(next_prop_lane)
            ed_fault.send([{"op": "tap", "id": f"button:{next_prop_lane}"}])
            ed_fault.wait_for(lambda: (ui_ws / ".scuzz/ide/journal.json").is_file() and (ui_ws / ".scuzz/ide/journal.json").read_text().strip() != "", label="interrupted journal retained")
        finally:
            ed_fault.close()
        assert (ui_ws / "src/Main.scuzz").read_text() == ui_reset_src

    # Restart cleanly: Cards.recover finishes 10-next-ready, restores paused status, then Undo 10-next-ready and verify summary!
    ed_rec = HeadlessEditor(editor_exe, ui_ws, [])
    try:
        ed_rec.goto_section(2, "Session")
        ed_rec.wait_for(lambda: "Session restored in paused state. Select Start to resume." in json.dumps(ed_rec.ui()), label="paused restore status")
        if os.uname().sysname == "Linux":
            assert (ui_ws / ".scuzz/ide/journal.json").read_text() == ""
            assert (ui_ws / "src/Main.scuzz").read_text() == ui_next_src
            ed_rec.goto_section(0, "Review")
            ed_rec.send([{"op": "tap", "id": "outlined:Undo"}])
            ed_rec.wait_for(
                lambda: (ui_ws / "src/Main.scuzz").read_text() == ui_reset_src and ed_rec.signals().get("deckBusy") == 0,
                label="Undo restored 08-reset",
            )
        ed_rec.goto_section(2, "Session")
        ed_rec.send([{"op": "tap", "id": "outlined:Refresh summary"}])
        ed_rec.wait_for(lambda: "Rule reviews: installed 1 (active 1, undone 0); rejected 1" in json.dumps(ed_rec.ui()), label="UI session summary lines")
        ui_sum_blob = json.dumps(ed_rec.ui())
        assert "Conservative source measure" in ui_sum_blob, ui_sum_blob
        assert "Accepted: 2; retained: 1;" in ui_sum_blob, ui_sum_blob
        assert "Baseline choices: 1; Undo: 1" in ui_sum_blob, ui_sum_blob
        assert "need-another-scenario: 1" in ui_sum_blob, ui_sum_blob
        assert "08-reset" in ui_sum_blob and ": retained " in ui_sum_blob, ui_sum_blob
        assert "10-next-ready" in ui_sum_blob and ": undone " in ui_sum_blob, ui_sum_blob
    finally:
        ed_rec.close()

    # =========================================================================
    # PART B: Complete IO Objective Session (scratch copy of examples/webhook)
    # =========================================================================
    io_orig = (io_ws / "src/Hook.scuzz").read_text()
    io_ver_orig = (io_ws / "delivery.scuzz_verify").read_text()
    old_store_arm = 'case Result.Ok(value) => if (!Json.isObj(value)) reply(400, "Expected a JSON object") else Fs.write(output, body).flatMap(_ => reply(200, "Stored")).handleErrorWith(_ => reply(503, "Cannot store delivery"))'
    assert old_store_arm in io_orig
    io_anchor_line = next(line for line in io_orig.splitlines() if old_store_arm in line)
    io_audit_2_line = io_anchor_line.replace(
        'Fs.write(output, body).flatMap(_ => reply(200, "Stored"))',
        'Fs.write("delivery.log", "audit:local").flatMap(_ => Fs.write(output, body)).flatMap(_ => reply(200, "Stored"))',
        1,
    )
    io_audit_1 = fmt_src(io_orig.replace(
        'Fs.write(output, body).flatMap(_ => reply(200, "Stored"))',
        'Fs.write("delivery.log", "audit:crank").flatMap(_ => Fs.write(output, body)).flatMap(_ => reply(200, "Stored"))',
        1,
    ))
    io_audit_2 = fmt_src(io_orig.replace(
        'Fs.write(output, body).flatMap(_ => reply(200, "Stored"))',
        'Fs.write("delivery.log", "audit:local").flatMap(_ => Fs.write(output, body)).flatMap(_ => reply(200, "Stored"))',
        1,
    ))
    io_audit_3 = fmt_src(io_orig.replace(
        'Fs.write(output, body).flatMap(_ => reply(200, "Stored"))',
        'Fs.write("delivery.log", "accepted:hmac-verified").flatMap(_ => Fs.write(output, body)).flatMap(_ => reply(200, "Stored"))',
        1,
    ))

    # Seed session.json WITHOUT "allowed" to exercise restoredInit fallback on IO workspace too.
    (io_ws / ".scuzz/ide").mkdir(parents=True, exist_ok=True)
    (io_ws / ".scuzz/ide/session.json").write_text(json.dumps({"v": 1, "objective": "Write delivery.log audit receipt on authenticated /webhook deliveries"}))

    # Exercise live loopback HTTP sync server (401 unauthorized, 400 malformed, 200 steer) + scuzz ide crank over IO candidates.
    sync_port = free_port()
    sync_token = "io-session-token-123456"
    srv = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(sync_port), "--token", sync_token, str(io_ws)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{sync_port}/v1/ide/sync", timeout=2)
            except urllib.error.HTTPError as exc:
                assert exc.code == 401
                break
            except urllib.error.URLError:
                time.sleep(0.05)
        else:
            raise AssertionError("io sync server did not start")

        bad_req = urllib.request.Request(
            f"http://127.0.0.1:{sync_port}/v1/ide/sync",
            data=b"{bad",
            headers={"Authorization": f"Bearer {sync_token}", "Content-Type": "application/json"},
            method="POST",
        )
        try:
            urllib.request.urlopen(bad_req, timeout=5)
            raise AssertionError("expected 400 for malformed JSON")
        except urllib.error.HTTPError as exc:
            assert exc.code == 400

        steer_req = urllib.request.Request(
            f"http://127.0.0.1:{sync_port}/v1/ide/sync",
            data=json.dumps({"v": 1, "id": "io-st-1", "kind": "steer", "prompt": "clarify port 8080 HMAC banner"}).encode(),
            headers={"Authorization": f"Bearer {sync_token}", "Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(steer_req, timeout=5) as resp:
            assert resp.status == 200
            assert json.loads(resp.read().decode())["results"][0]["status"] == "queued"
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
            srv.wait()

    # Populate broken IO candidate (breaks HMAC signature claim) + valid IO candidate for scuzz ide crank.
    bad_io = io_ws / "build/proposals/01-broken-hmac/src"
    bad_io.mkdir(parents=True, exist_ok=True)
    (bad_io / "Hook.scuzz").write_text(io_orig.replace("sha256=", "broken=", 1))
    good_io = io_ws / "build/proposals/02-crank-audit/src"
    good_io.mkdir(parents=True, exist_ok=True)
    (good_io / "Hook.scuzz").write_text(io_audit_1)

    crank_io = subprocess.run(
        [cli, "ide", "crank", "--rounds", "2", "--message-format=json", str(io_ws)],
        capture_output=True,
        text=True,
        timeout=180,
    )
    assert crank_io.returncode == 0, (crank_io.stdout, crank_io.stderr)
    crank_doc = json.loads(crank_io.stdout)
    assert crank_doc["rejected"] == 1 and crank_doc["published"] == 1, crank_doc

    # Fetch verified IO card over live loopback GET /v1/ide/sync and record abstention ("outside-my-goal") via POST /v1/ide/sync!
    srv2 = subprocess.Popen(
        [cli, "ide", "serve", "--port", str(sync_port), "--token", sync_token, str(io_ws)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        deadline = time.monotonic() + 10.0
        feed_io = None
        while time.monotonic() < deadline:
            try:
                get_req = urllib.request.Request(
                    f"http://127.0.0.1:{sync_port}/v1/ide/sync",
                    headers={"Authorization": f"Bearer {sync_token}"},
                )
                with urllib.request.urlopen(get_req, timeout=5) as resp:
                    feed_io = json.loads(resp.read().decode())
                    break
            except urllib.error.URLError:
                time.sleep(0.05)
        assert feed_io and len(feed_io["cards"]) == 1 and feed_io["cards"][0]["proposal"] == "02-crank-audit", feed_io
        crank_card_id = feed_io["cards"][0]["id"]
        skip_req = urllib.request.Request(
            f"http://127.0.0.1:{sync_port}/v1/ide/sync",
            data=json.dumps({"v": 1, "id": "io-skip-1", "kind": "skip", "card": crank_card_id, "reason": "outside-my-goal"}).encode(),
            headers={"Authorization": f"Bearer {sync_token}", "Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(skip_req, timeout=5) as resp:
            skip_doc = json.loads(resp.read().decode())
            assert skip_doc["results"][0]["status"] == "applied", skip_doc
    finally:
        srv2.terminate()
        try:
            srv2.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv2.kill()
            srv2.wait()
    shutil.rmtree(io_ws / "build/proposals/01-broken-hmac", ignore_errors=True)
    shutil.rmtree(io_ws / "build/proposals/02-crank-audit", ignore_errors=True)

    # Open IO workspace in Headless editor: verify fallback allowed paths, local generation, both lanes, and IO rule install + Undo!
    ed_io = HeadlessEditor(editor_exe, io_ws, [])
    try:
        ed_io.goto_section(2, "Session")
        ed_io.wait_for(lambda: "Session restored in paused state. Select Start to resume." in json.dumps(ed_io.ui()), label="IO restored status")
        assert "src/Hook.scuzz,src/Main.scuzz" in json.dumps(ed_io.ui()), ed_io.ui()

        ed_io.send([{"op": "tap", "id": "button:Start"}])
        ed_io.wait_for(
            lambda: bool(read_json(io_ws / "build/ide/request.json").get("baseline")) and "External proposals: publish a complete directory" in json.dumps(ed_io.ui()),
            label="IO session started",
        )
        ed_io.goto_section(0, "Review")

        # Controlled local inference over loopback HTTP generates 03-local-io, plus 04-final-io in the 2-card queue!
        io_req = json.loads((io_ws / "build/ide/request.json").read_text())
        assert io_req.get("allowed") == ["src/Hook.scuzz", "src/Main.scuzz"], io_req
        assert any(f.get("decision") == "abstain" and f.get("reason") == "outside-my-goal" for f in io_req.get("feedback", [])), io_req["feedback"]
        gen_io_res, _, _ = run_controlled_local_gen(
            io_ws,
            io_ws / "build/ide/request.json",
            io_ws / "build/proposals/03-local-io",
            [{"path": "src/Hook.scuzz", "search": io_anchor_line, "replacement": io_audit_2_line}],
            change="Write delivery.log audit receipt on authenticated deliveries",
        )
        assert gen_io_res.returncode == 0 and json.loads(gen_io_res.stdout)["result"] == "published", (gen_io_res.stdout, gen_io_res.stderr)
        publish_behavior(io_ws, "04-final-io", io_req, [("src/Hook.scuzz", io_audit_3)])

        c_io_2 = ed_io.wait_armed_card("03-local-io")
        io_base_lane = "Choose left" if c_io_2["flip"] == 0 else "Choose right"
        lanes_tapped.add(io_base_lane)
        ed_io.send([{"op": "tap", "id": f"button:{io_base_lane}"}])

        c_io_3 = ed_io.wait_armed_card("04-final-io")
        io_acc_lane = "Choose left" if c_io_3["flip"] == 1 else "Choose right"
        lanes_tapped.add(io_acc_lane)
        ed_io.send([{"op": "tap", "id": f"button:{io_acc_lane}"}])
        ed_io.wait_for(
            lambda: (io_ws / "src/Hook.scuzz").read_text() == io_audit_3
            and ed_io.signals().get("deckBusy") == 0
            and ed_io.signals().get("ruleChoiceCard") == c_io_3["id"]
            and read_json(io_ws / "build/ide/request.json").get("baseline", "") not in ("", io_req["baseline"]),
            label="04-final-io accepted",
        )

        # Guarantee both lane buttons were exercised across the session; if random flips happened to align, decide one more two-lane card on the remaining button!
        if lanes_tapped != {"Choose left", "Choose right"}:
            extra_req = json.loads((io_ws / "build/ide/request.json").read_text())
            publish_behavior(io_ws, "05-lane-check", extra_req, [("src/Hook.scuzz", io_audit_2)])
            c_extra = ed_io.wait_armed_card("05-lane-check")
            missing_btn = next(iter({"Choose left", "Choose right"} - lanes_tapped))
            lanes_tapped.add(missing_btn)
            ed_io.send([{"op": "tap", "id": f"button:{missing_btn}"}])
            ed_io.wait_for(lambda: ed_io.signals().get("deckBusy") == 0, label="extra lane settled")
            if (io_ws / "src/Hook.scuzz").read_text() == io_audit_2:
                ed_io.send([{"op": "tap", "id": "outlined:Undo"}])
                ed_io.wait_for(
                    lambda: (io_ws / "src/Hook.scuzz").read_text() == io_audit_3
                    and ed_io.signals().get("deckBusy") == 0
                    and ed_io.signals().get("ruleChoiceCard") == c_io_3["id"],
                    label="restore io_audit_3",
                )
        assert lanes_tapped == {"Choose left", "Choose right"}, lanes_tapped

        # Suggest, verify, install, and Undo a delivery.scuzz_verify IO rule, confirming rule Undo stays out of behavior Undo count!
        ed_io.send([{"op": "tap", "id": "outlined:Suggest rule"}])
        ed_io.wait_for(lambda: read_json(io_ws / "build/ide/rule-request.json").get("kind") == "rule", label="IO rule-request.json")
        io_rule_req = json.loads((io_ws / "build/ide/rule-request.json").read_text())
        io_rule_src = (
            io_ver_orig
            + '\ndef deliveryWritesAuditLog(t: Timeline): Verdict =\n'
            + '  Verdict.every(t, i => !Timeline.driveHas(t, i, "delivery") || Timeline.fileTextIs(t, i, "delivery.log", "accepted:hmac-verified") || faulted(t, i))\n'
        )
        publish_rule(io_ws, "01-io-rule", io_rule_req, "delivery.scuzz_verify", io_rule_src)
        ed_io.send([{"op": "tap", "id": "outlined:Import rule draft"}])
        ed_io.wait_for(
            lambda: ed_io.signals().get("ruleVerified") == 1 and ed_io.signals().get("deckBusy") == 0,
            label="IO rule verified",
            timeout=120.0,
        )
        io_req_before_rule = read_json(io_ws / "build/ide/request.json")
        ed_io.send([{"op": "tap", "id": "button:Install rule"}])
        ed_io.wait_for(
            lambda: "deliveryWritesAuditLog" in (io_ws / "delivery.scuzz_verify").read_text()
            and "Installed rule in delivery.scuzz_verify" in ed_io.signals().get("ruleStatus", "")
            and ed_io.signals().get("deckBusy") == 0
            and read_json(io_ws / "build/ide/request.json").get("inputs", "") not in ("", io_req_before_rule.get("inputs", "")),
            label="IO rule installed",
        )

        # Undo the installed rule from Rule review and check that delivery.scuzz_verify reverts AND behavior Undo is not inflated!
        io_req_after_rule = read_json(io_ws / "build/ide/request.json")
        ed_io.send([{"op": "tap", "id": "outlined:Undo"}])
        ed_io.wait_for(
            lambda: (io_ws / "delivery.scuzz_verify").read_text() == io_ver_orig
            and ed_io.signals().get("deckBusy") == 0
            and read_json(io_ws / "build/ide/request.json").get("inputs", "") not in ("", io_req_after_rule.get("inputs", "")),
            label="IO rule undone",
        )
        ed_io.goto_section(2, "Session")
        ed_io.send([{"op": "tap", "id": "outlined:Refresh summary"}])
        ed_io.wait_for(lambda: "Rule reviews: installed 1 (active 0, undone 1); rejected 0" in json.dumps(ed_io.ui()), label="IO summary after rule Undo")
        io_sum_blob = json.dumps(ed_io.ui())
        assert "outside-my-goal: 1" in io_sum_blob, io_sum_blob
        assert "04-final-io" in io_sum_blob and ": retained " in io_sum_blob, io_sum_blob
    finally:
        ed_io.close()

    # Verify accepted IO workspace still passes scuzz fuzz --iterations 0!
    io_check = subprocess.run([cli, "fuzz", "--iterations", "0", str(io_ws)], capture_output=True, text=True, timeout=180)
    assert io_check.returncode == 0, (io_check.stdout, io_check.stderr)

    # Verify external-edit journal conflict preserves external edits on restart recovery!
    conflict_card = "e" * 64
    (io_ws / "src/Hook.scuzz").write_text(io_audit_3 + "\n// external author note\n")
    (io_ws / ".scuzz/ide/journal.json").write_text(json.dumps({
        "operation": f"accept-{conflict_card}",
        "card": conflict_card,
        "decision": "accept",
        "writes": [{
            "path": "src/Hook.scuzz",
            "before": io_orig,
            "before_exists": True,
            "after": io_audit_1,
            "after_exists": True,
        }],
    }))
    ed_conf = HeadlessEditor(editor_exe, io_ws, [{"op": "tap", "id": "choicechip:More evidence"}])
    try:
        ed_conf.wait_for(lambda: "recovery conflict: preserve external edits" in json.dumps(ed_conf.signals().get("deltaStatus", [])) and "recovery conflict: preserve external edits" in json.dumps(ed_conf.ui()), label="recovery conflict preserved")
        assert "// external author note" in (io_ws / "src/Hook.scuzz").read_text()
    finally:
        ed_conf.close()
PY_IDE_COMPLETE_SESSION

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

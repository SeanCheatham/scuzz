#!/usr/bin/env python3
"""Check the pure function and effect protocol in the formal experiment."""

import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


class IncompleteCheck(Exception):
    """A resource limit or missing evidence prevents a result."""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def solve(solver, query, seconds=5, extra=(), model=False):
    started = time.monotonic()
    text = query + '(check-sat)\n' + ('(get-value (p0))\n' if model else '')
    try:
        run = subprocess.run([solver, *extra, '-in'], input=text, text=True,
                             capture_output=True, timeout=seconds)
    except FileNotFoundError:
        return dict(status='incomplete', reason='missing solver', seconds=time.monotonic() - started)
    except subprocess.TimeoutExpired:
        return dict(status='incomplete', reason='solver deadline', seconds=time.monotonic() - started)
    lines = run.stdout.splitlines()
    result = lines[0] if lines else ''
    row = dict(status=result, seconds=time.monotonic() - started,
               query_sha256=digest(query.encode()), stdout=run.stdout, stderr=run.stderr)
    if run.returncode != 0 or result not in ('sat', 'unsat'):
        row.update(status='incomplete', reason='solver unknown or error')
    if model and result == 'sat':
        match = re.search(r'\(p0 #x([0-9a-fA-F]{16})\)', run.stdout)
        if match is None:
            row.update(status='incomplete', reason='unsupported counterexample format')
        else:
            value = int(match[1], 16)
            row['input'] = value if value < 2**63 else value - 2**64
    return row


def check(solver, queries):
    if queries['status'] != 'ready':
        return dict(status='incomplete', reason=queries['reason'])
    rows = {key: solve(solver, queries[key]) for key in ('domain', 'violation', 'reach')}
    status = 'incomplete'
    reason = 'solver does not complete'
    if all(row['status'] != 'incomplete' for row in rows.values()):
        if rows['domain']['status'] == 'unsat':
            status, reason = 'invalid', 'contradictory assumption'
        elif rows['violation']['status'] == 'sat':
            witness = solve(solver, queries['violation'], model=True)
            rows['counterexample'] = witness
            if witness['status'] == 'sat' and 'input' in witness:
                status, reason = 'failed', 'author oracle rejects an input'
        elif rows['reach']['status'] == 'unsat':
            status, reason = 'invalid', 'required trigger is impossible in this domain'
        else:
            status, reason = 'passed', 'exhaustive signed 64-bit input domain'
    return dict(status=status, reason=reason, queries=rows)


def command(cli, args, logs, label, success=True, env=None, stdin=None, seconds=180):
    run = subprocess.run([str(cli), *map(str, args)], capture_output=True, text=True,
                         timeout=seconds, env=env, input=stdin)
    (logs / (label + '.stdout')).write_text(run.stdout)
    (logs / (label + '.stderr')).write_text(run.stderr)
    if (run.returncode == 0) != success:
        raise AssertionError(f'{label}: exit {run.returncode}\n{run.stdout}\n{run.stderr}')
    return run


def replay(cli, source, claim, files, value, package, logs, label, passes):
    (package / 'src').mkdir(parents=True, exist_ok=True)
    (package / 'scuzz.toml').write_text('[package]\nname="replay"\n')
    (package / 'src/Amount.scuzz').write_text(source)
    (package / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
    (package / 'balance.scuzz_verify').write_text(claim)
    workload = package / 'replay.toml'
    script = f'drive balance {value}\n'
    workload.write_text('[fuzz]\nschedule_seed="0"\nfault_seed="0"\n'
                        + 'events=' + json.dumps(script.splitlines()) + '\n')
    command(cli, ['fmt', package], logs, label + '-format')
    command(cli, ['fuzz', '--replay', workload, package], logs, label + '-native', success=passes)
    native = json.loads((package / 'build/fuzz/summary.json').read_text())
    row, = native['scope']['workloads']
    assert row['status'] == ('passed' if passes else 'failed'), native
    assert row['script'] == script and row['engine'] == 'compiled', native
    drive = package / 'build/fuzz/drive.json'
    request = package / 'request.txt'
    probe_log = package / 'evaluator.log'
    picks = package / 'evaluator.picks'
    rejection = package / 'evaluator.rejection'
    rejection.write_text('')
    request.write_text(f'TESTRT=1\nDRIVE_SCRIPT={drive}\nSCHED_SEED=0\nFAULT_SEED=0\n'
                       f'SCHED_PICKS={";".join(row["schedule_picks"])}\n'
                       f'SCHED_PICKS_DUMP={picks}\nREJECTION_DUMP={rejection}\nPROBE_LOG={probe_log}\n')
    probe_dir = package / 'evaluator'
    probe_dir.mkdir(exist_ok=True)
    (probe_dir / 'files.txt').write_text(''.join(stem + '\n' for stem, _ in files))
    for index, (_, content) in enumerate(files):
        (probe_dir / f'{index}.scuzz').write_text(content)
    env = dict(os.environ, SCUZZ_EV_REQUEST=str(request))
    evaluated = command(cli, ['eval', '--probe', probe_dir], logs, label + '-evaluator',
                        env=env, stdin='probe\nquit\n')
    lines = evaluated.stdout.splitlines()
    assert len(lines) == 2 and lines[0] == 'ready', (lines, probe_log.read_text())
    assert (lines[1] == '0') == passes, ('replay mismatch', lines, probe_log.read_text())
    assert rejection.read_bytes() == (package / 'build/rejection.txt').read_bytes(), 'replay rejection mismatch'
    assert picks.read_text().splitlines() == row['schedule_picks'], (picks.read_text(), row)
    if not passes:
        assert 'oracle balance rejected' in probe_log.read_text(), probe_log.read_text()
    return dict(input=value, workload=str(workload), script=script,
                schedule_picks=row['schedule_picks'], native_status=row['status'],
                rejection=rejection.read_text(), evaluator_exit=int(lines[1]),
                evaluator_status='passed' if passes else 'failed', parity='passed')


def live_validation(cli, source, package, logs):
    inputs = ['0', '99', '100', '-1', '101', '9223372036854775807',
              '-9223372036854775808', '9223372036854775808', ' 1', 'x']
    expected = ['ok:1', 'ok:100', 'ok:100', 'error', 'error', 'error',
                'error', 'error', 'error', 'error']
    (package / 'src').mkdir(parents=True, exist_ok=True)
    (package / 'scuzz.toml').write_text('[package]\nname="validation"\n')
    (package / 'src/Amount.scuzz').write_text(source)
    body = '\n'.join(f'    _ <- show({json.dumps(text)})' for text in inputs)
    main = ('def show(text: String): IO[Unit] =\n  Amount.validated(text) match {\n'
            '    case Result.Ok(n) => IO.println(s"ok:$n")\n'
            '    case Result.Err(_) => IO.println("error")\n  }\n\n'
            '@main def main: IO[Unit] =\n  for {\n' + body + '\n  } yield ()\n')
    (package / 'src/Main.scuzz').write_text(main)
    command(cli, ['fmt', package], logs, 'validation-format')
    evaluator = command(cli, ['eval', package], logs, 'validation-evaluator').stdout.splitlines()
    native = command(cli, ['run', package], logs, 'validation-native').stdout.splitlines()
    assert evaluator == expected and native[-len(expected):] == expected, (evaluator, native)
    return dict(inputs=inputs, expected=expected, evaluator='passed', native='passed')


def translation_checks(cli, solver, cases, package, logs):
    def signed(value):
        return (value + 2**63) % 2**64 - 2**63

    models = dict(wrap=lambda n: signed(signed(n + 1) - 1) == n,
                  multiply=lambda n: signed(n * 2) < n,
                  minimum=lambda n: n == -2**63,
                  order=lambda n: (n < 0 or n > 100) and not (n >= 0 and n <= 100),
                  binding=lambda n: (signed(-n) if n < 0 else n) != n,
                  negative=lambda n: signed(-n) > 0,
                  bool=lambda n: n == 0)
    (package / 'src').mkdir(parents=True, exist_ok=True)
    (package / 'scuzz.toml').write_text('[package]\nname="translation"\n')
    expected = []
    calls = []
    checked = []
    for index, case in enumerate(cases):
        assert case['queries']['status'] == 'ready', case
        source = case['source'].replace('oracle balance(', 'def balance(', 1)
        (package / f'src/Case{index}.scuzz').write_text(source)
        for value in case['values']:
            n = value['input']
            want = models[case['name']](n)
            shown = 'true' if want else 'false'
            assert value['result'] == shown, ('independent evaluator mismatch', case['name'], value)
            query = case['queries']['violation'] + f'(assert (= p0 (_ bv{n % 2**64} 64)))\n'
            result = solve(solver, query)
            assert result['status'] == ('unsat' if want else 'sat'), (case['name'], n, result)
            checked.append(dict(case=case['name'], input=n, expected=want, solver=result['status']))
            calls.append(f'    _ <- IO.println(Str.fromBool(Case{index}.balance({n})))')
            expected.append(shown)
    (package / 'src/Main.scuzz').write_text('@main def main: IO[Unit] =\n  for {\n'
                                          + '\n'.join(calls) + '\n  } yield ()\n')
    command(cli, ['fmt', package], logs, 'translation-format')
    evaluator = command(cli, ['eval', package], logs, 'translation-evaluator').stdout.splitlines()
    native = command(cli, ['run', package], logs, 'translation-native').stdout.splitlines()
    assert evaluator == expected and native[-len(expected):] == expected, 'translation replay mismatch'
    return dict(status='passed', inputs=checked, evaluator='passed', native='passed',
                expected_results='independent host arithmetic with signed 64-bit wrapping')


def protocol_package(cli, data, package, logs, defective=False):
    (package / 'src').mkdir(parents=True, exist_ok=True)
    (package / 'scuzz.toml').write_text('[package]\nname="protocol"\n')
    source = data['defective_source'] if defective else data['source']
    (package / 'src/Protocol.scuzz').write_text(source)
    (package / 'src/Main.scuzz').write_text('@main def main: IO[Unit] = IO.pure(())\n')
    (package / 'protocol.scuzz_scenario').write_text(data['scenario'])
    (package / 'protocol.scuzz_verify').write_text(data['claims'])
    (package / 'corpus').mkdir(exist_ok=True)
    for driver in ('complete', 'cancel'):
        (package / f'corpus/{driver}.toml').write_text('[fuzz]\nschedule_seed="0"\nfault_seed="0"\nevents=["drive ' + driver + ' 1"]\n')
    command(cli, ['fmt', package], logs, package.name + '-format')
    command(cli, ['fuzz', '--iterations', '0', package], logs, package.name + '-build', success=not defective)
    probe = package / 'evaluator'
    probe.mkdir(exist_ok=True)
    files = data['defective_files'] if defective else data['files']
    (probe / 'files.txt').write_text(''.join(stem + '\n' for stem, _ in files))
    for index, (_, source) in enumerate(files):
        (probe / f'{index}.scuzz').write_text(source)
    return package / 'build/protocol', probe


def protocol_probe(executable, directory, driver, n, fault, prefix=(), recorded=None, seconds=20):
    directory.mkdir(parents=True, exist_ok=True)
    script = f'drive {driver} {n}\n'
    drive = directory / 'drive.json'
    drive.write_text(json.dumps(dict(v=1, kind='inject', events=[dict(op='drive', name=driver, args=[str(n)])])))
    picks = ';'.join(recorded) if recorded is not None else (f'{driver} 0:' + ','.join(map(str, prefix)) if prefix else '')
    settings = dict(TESTRT='1', DRIVE_SCRIPT=str(drive), SCHED_SEED='0', FAULT_SEED=str(fault),
                    RAND_SEED='0', SCHED_PICKS=picks, SCHED_PICKS_DUMP=str(directory / 'picks.txt'),
                    SCHED_CHOICES_DUMP=str(directory / 'choices.txt'),
                    REJECTION_DUMP=str(directory / 'rejection.txt'), CLAIM_DUMP=str(directory / 'claims.txt'),
                    TIMELINE_DUMP=str(directory / 'timeline.txt'), COVERAGE_DUMP=str(directory / 'coverage.txt'))
    for filename in ('rejection.txt', 'claims.txt', 'timeline.txt', 'coverage.txt'):
        (directory / filename).write_text('')
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('SCUZZ_'):
            del env[key]
    env.update({'SCUZZ_' + key: value for key, value in settings.items()})
    run = subprocess.run([str(executable)], env=env, text=True, capture_output=True, timeout=seconds)
    (directory / 'stdout').write_text(run.stdout)
    (directory / 'stderr').write_text(run.stderr)
    rejection = (directory / 'rejection.txt').read_text()
    status = 'passed' if run.returncode == 0 else 'failed' if rejection else 'incomplete'
    choices = []
    ends = []
    for line in (directory / 'choices.txt').read_text().splitlines():
        fields = line.split()
        if fields[0] == 'choice':
            choices.append(dict(name=fields[1], step=int(fields[2]), ready=list(map(int, fields[3:]))))
        elif fields[0] == 'end':
            assert len(fields) == 7, fields
            ends.append(dict(name=fields[1], contention=int(fields[2]), steps=int(fields[3]),
                             fibers=int(fields[4]), ok=bool(int(fields[5])), bounded=bool(int(fields[6]))))
        else:
            raise AssertionError('unsupported scheduler evidence: ' + line)
    executed = (directory / 'picks.txt').read_text().splitlines()
    workload = directory / 'replay.toml'
    workload.write_text('[fuzz]\nschedule_seed="0"\nfault_seed="' + str(fault) + '"\n'
                        + 'schedule_picks=' + json.dumps(executed) + '\n'
                        + 'events=' + json.dumps([script.strip()]) + '\n')
    return dict(status=status, script=script, fault=fault, input=n, driver=driver, prefix=list(prefix),
                schedule_picks=executed, choices=choices, runs=ends, rejection=rejection,
                workload=str(workload), settings=settings)


def executed_properties(path):
    return sorted(bytes.fromhex(line[2:]).decode() for line in path.read_text().splitlines() if line.startswith('R:'))


def protocol_parity(cli, probe, row, directory, logs, step_limit, seconds=180):
    request = directory / 'request.txt'
    settings = dict(row['settings'], PROBE_LOG=str(directory / 'evaluator.log'),
                    SCHED_PICKS=';'.join(row['schedule_picks']),
                    SCHED_PICKS_DUMP=str(directory / 'evaluator.picks'),
                    SCHED_CHOICES_DUMP=str(directory / 'evaluator.choices'),
                    REJECTION_DUMP=str(directory / 'evaluator.rejection'),
                    CLAIM_DUMP=str(directory / 'evaluator.claims'),
                    TIMELINE_DUMP=str(directory / 'evaluator.timeline'), COVERAGE_DUMP=str(directory / 'evaluator.coverage'))
    for name in ('evaluator.rejection', 'evaluator.claims', 'evaluator.timeline', 'evaluator.coverage'):
        (directory / name).write_text('')
    request.write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
    run = command(cli, ['eval', '--probe', probe], logs, directory.name + '-evaluator',
                  env=dict(os.environ, SCUZZ_EV_REQUEST=str(request)), stdin='probe\nquit\n', seconds=seconds)
    lines = run.stdout.splitlines()
    assert len(lines) == 2 and lines[0] == 'ready', lines
    assert (lines[1] == '0') == (row['status'] == 'passed'), ('protocol replay mismatch', row, lines)
    for native, evaluator in (('picks.txt', 'evaluator.picks'), ('rejection.txt', 'evaluator.rejection'),
                              ('claims.txt', 'evaluator.claims'), ('timeline.txt', 'evaluator.timeline')):
        assert (directory / native).read_bytes() == (directory / evaluator).read_bytes(), ('protocol replay mismatch', native, row)
    assert executed_properties(directory / 'coverage.txt') == executed_properties(directory / 'evaluator.coverage'), 'protocol author rule execution mismatch'
    name = row['driver']
    native_trace = (directory / 'choices.txt').read_text().splitlines()
    evaluator_trace = (directory / 'evaluator.choices').read_text().splitlines()
    assert [line for line in native_trace if line.startswith('choice ' + name + ' ')] == [
        line for line in evaluator_trace if line.startswith('choice ' + name + ' ')], 'protocol permitted choice mismatch'
    native_end = [line.split()[2:] for line in native_trace if line.startswith('end ' + name + ' ')]
    evaluator_end = [line.split()[2:] for line in evaluator_trace if line.startswith('end ' + name + ' ')]
    assert len(native_end) == len(evaluator_end) == 1, 'protocol terminal scheduler evidence is missing'
    a, b = native_end[0], evaluator_end[0]
    assert len(a) == len(b) == 5 and a[0] == b[0] and a[2:] == b[2:], 'protocol terminal scheduler mismatch'
    if int(b[1]) > step_limit:
        raise IncompleteCheck('evaluator scheduler step bound is incomplete')
    return dict(status='passed', evaluator_exit=int(lines[1]), evaluator_steps=int(b[1]), workload=row['workload'])


def protocol_checks(cli, data, output, contention_limit=16):
    executable, probe = protocol_package(cli, data, output / 'protocol', output)
    bounds = dict(inputs=[1, 2], requests=2, cancellation=[False, True],
                  faults=[0, 1, 10, 19, 28, 289, 298, 307, 316],
                  fault_scope='no fault or one filesystem failure or interrupted write at calls 1 through 4',
                  schedules='every ready fiber at each contention', contention_steps=contention_limit,
                  scheduler_steps=128, executions=1024)
    rows = []
    for driver in ('complete', 'cancel'):
        for n in bounds['inputs']:
            for fault in bounds['faults']:
                pending = [()]
                queued = {()}
                leaves = set()
                while pending:
                    if len(rows) >= bounds['executions']:
                        raise IncompleteCheck('exhaustive execution budget is incomplete')
                    prefix = pending.pop()
                    directory = output / f'protocol-run-{len(rows)}'
                    row = protocol_probe(executable, directory, driver, n, fault, prefix)
                    rows.append(row)
                    row['executed_properties'] = executed_properties(directory / 'coverage.txt')
                    if row['status'] == 'incomplete':
                        raise IncompleteCheck('protocol does not complete: ' + str(row))
                    assert row['status'] == 'passed', ('protocol rejects', row)
                    assert {'cancellation prevents a write', 'each request releases once', 'two requests bound writes',
                            'first request releases once', 'valid inputs settle',
                            'a request waits before its write'} <= set(row['executed_properties']), 'required author assertion does not execute'
                    runs = [run for run in row['runs'] if run['name'] == driver]
                    assert len(runs) == 1, ('missing terminal scheduler evidence', row)
                    terminal = runs[0]
                    assert terminal['bounded'] and terminal['ok'], ('unbounded or incomplete scheduler', row)
                    if terminal['steps'] > bounds['scheduler_steps']:
                        raise IncompleteCheck('scheduler step bound is incomplete')
                    if terminal['contention'] > bounds['contention_steps']:
                        raise IncompleteCheck('contention bound is incomplete')
                    choices = [choice for choice in row['choices'] if choice['name'] == driver]
                    assert len(choices) == terminal['contention'], 'missing scheduler choices'
                    picks = next(line for line in row['schedule_picks'] if line.startswith(driver + ' ')).split(':')[1]
                    path = tuple(map(int, picks.split(','))) if picks != '-' else ()
                    assert len(path) == len(choices) and path[:len(prefix)] == prefix, 'scheduler prefix mismatch'
                    for index, choice in enumerate(choices):
                        assert choice['step'] == index + 1 and len(choice['ready']) >= 2
                        assert path[index] in choice['ready'], 'selected fiber is not permitted'
                        for alternative in choice['ready']:
                            branch = path[:index] + (alternative,)
                            if alternative != path[index] and branch not in queued:
                                queued.add(branch)
                                pending.append(branch)
                    if path not in leaves:
                        leaves.add(path)
                        row['parity'] = protocol_parity(cli, probe, row, directory, output, bounds['scheduler_steps'])
                        assert 'checked\tfilesRemainValid' in (directory / 'claims.txt').read_text()
                        assert 'checked\tcancellationPreserves' in (directory / 'claims.txt').read_text()
    observed = {'normal': False, 'failure': False, 'cancellation': False, 'interrupted_write': False}
    for row in rows:
        directory = Path(row['workload']).parent
        transcript = (directory / 'stdout').read_text()
        observed['normal'] |= row['driver'] == 'complete' and row['fault'] == 0 and transcript.count('request completes') == 2
        observed['failure'] |= 'Fs: injected fault' in transcript
        observed['cancellation'] |= row['driver'] == 'cancel' and 'request stops:' in transcript
        observed['interrupted_write'] |= 'Fs: partial write' in transcript
        if 'Fs: partial write' in transcript:
            assert 'failed write preserves the file' in row['executed_properties'], 'interrupted write preservation is not checked'
    assert all(observed.values()), ('required protocol paths are not reached', observed)
    defective, defect_probe = protocol_package(cli, data, output / 'protocol-defect', output, defective=True)
    directory = output / 'protocol-defect-witness'
    witness = protocol_probe(defective, directory, 'cancel', 1, 289)
    assert witness['status'] == 'failed' and 'valid inputs settle' in witness['rejection'], witness
    assert 'first request releases once' in (directory / 'stderr').read_text(), 'release defect does not reach its rule'
    witness['parity'] = protocol_parity(cli, defect_probe, witness, directory, output, bounds['scheduler_steps'])
    command(cli, ['fuzz', '--replay', witness['workload'], output / 'protocol-defect'], output,
            'protocol-defect-replay', success=False)
    command(cli, ['fuzz', '--iterations', '16', output / 'protocol'], output, 'protocol-ordinary-fuzz')
    return dict(status='passed', method='exhaustive-bounded-simulation', bounds=bounds,
                operations=['IO', 'Ref', 'Fiber', 'Deferred', 'Resource', 'Clock.monotonic', 'IO.sleep', 'Fs.read', 'Fs.write'],
                request_order='first worker with optional cancellation; second request after the first settles',
                workloads=rows, reached=observed, counterexample=witness,
                native_sha256=digest(executable.read_bytes()),
                fairness='none; every permitted finite scheduler choice is checked',
                progress='all explored runs complete within the stated step bounds',
                open_obligations=['unbounded completion', 'other inputs and fault combinations', 'live filesystem and OS cancellation'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', default=os.environ.get('SCUZZ', 'examples/cli/build/cli'))
    parser.add_argument('--solver', default=os.environ.get('SCUZZ_SOLVER', 'z3'))
    parser.add_argument('--recovery', action='store_true', help='Check the application recovery path without a solver.')
    args = parser.parse_args()
    os.chdir(ROOT)
    cli = Path(args.cli).resolve()
    if args.recovery:
        return recovery_experiment(cli)
    output = ROOT / 'examples/proof/build/experiment'
    output.mkdir(parents=True, exist_ok=True)
    report_path = output / 'summary.json'
    report = dict(status='incomplete', gate='formal-experiment',
                  methods=['symbolic-bitvector', 'exhaustive-bounded-simulation'],
                  open_obligations=['required project checks', 'product implementation gate'])

    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n')

    save()
    started = time.monotonic()
    try:
        exported = command(cli, ['run', 'examples/proof'], output, 'export',
                           env=dict(os.environ, SCUZZ_PROOF_EXPORT='1')).stdout
        experiment = json.loads(next(line for line in exported.splitlines() if line.startswith('{')))
        data = experiment['pure']
        report['build_seconds'] = time.monotonic() - started
        started = time.monotonic()
        solver = shutil.which(args.solver)
        report['identities'] = dict(compiler_sha256=digest(cli.read_bytes()),
                                    source_sha256=data['source_sha256'],
                                    query_exporter_sha256=digest((ROOT / 'examples/proof/build/proof').read_bytes()),
                                    translation_source_sha256=digest((ROOT / 'examples/proof/src/Proof.scuzz').read_bytes()),
                                    checking_script_sha256=digest(Path(__file__).read_bytes()),
                                    runtime_sha256=digest((ROOT / 'crates/runtime/build/libscuzz_rt.a').read_bytes()))
        clang = shutil.which('clang')
        if clang is None:
            raise IncompleteCheck('native compiler identity is missing')
        report['identities']['host'] = dict(platform=platform.platform(), python=platform.python_version())
        report['identities']['clang'] = dict(path=clang, sha256=digest(Path(clang).read_bytes()),
            version=subprocess.run([clang, '--version'], capture_output=True, text=True,
                                   check=True, timeout=5).stdout)
        assert digest(data['source'].encode()) == data['source_sha256']
        report['identities']['sources'] = [dict(path=str(path.relative_to(ROOT)), sha256=digest(path.read_bytes()))
            for directory in ('examples/compiler', 'examples/syntax', 'examples/proof')
            for path in sorted((ROOT / directory).rglob('*'))
            if path.is_file() and 'build' not in path.relative_to(ROOT / directory).parts
            and path.suffix in ('.scuzz', '.scuzz_verify', '.scuzz_scenario', '.toml')]
        if solver is None:
            report['reason'] = 'missing solver support'
            save()
            print(f'Incomplete: missing solver support. Report: {report_path}')
            return 2
        version = subprocess.run([solver, '-version'], capture_output=True, text=True,
                                 check=True, timeout=5).stdout.strip()
        report['identities'].update(solver_sha256=digest(Path(solver).read_bytes()), solver=version)
        report['scope'] = dict(input='all signed 64-bit Int values', arithmetic='modulo 2^64',
                               operations=['integer literals', 'Boolean literals', 'variables',
                                           '+', '-', '*', '<', '<=', '>', '>=', '==', '!=',
                                           '&&', '||', '!', 'if', 'pure bindings', 'nonrecursive calls'],
                               effects=[], expression_depth=32, call_depth=8, parameters=4,
                               platform='host evaluator and native replay',
                               solver_deadline_seconds=5, checking_budget_seconds=60)
        report['trusted'] = ['Scuzz parser and type checker', 'bit-vector translation',
                             version, 'evaluator', 'LLVM emission', 'clang', 'C runtime and scheduler choice trace', 'host OS']
        report['checks'] = {key: check(solver, data[key])
                            for key in ('correct', 'defect', 'contradiction', 'impossible')}
        assert report['checks']['correct']['status'] == 'passed', report['checks']
        assert report['checks']['defect']['status'] == 'failed', report['checks']
        assert report['checks']['contradiction']['status'] == 'invalid', report['checks']
        assert report['checks']['impossible']['status'] == 'invalid', report['checks']
        assert all(row['result']['status'] == 'unsupported' for row in data['unsupported']), data['unsupported']
        report['unsupported'] = data['unsupported']
        report['translation'] = translation_checks(cli, solver, data['translation'], output / 'translation', output)
        query = data['correct']['violation']
        missing = solve(str(output / 'missing-solver'), query)
        unknown = solve(solver, query, extra=('-rlimit:1',))
        slow = output / 'slow-solver'
        slow.write_text('#!/usr/bin/env python3\nimport time\ntime.sleep(1)\n')
        slow.chmod(0o700)
        timeout = solve(str(slow), query, seconds=0.01)
        assert all(row['status'] == 'incomplete' for row in (missing, unknown, timeout))
        report['incomplete_cases'] = dict(missing=missing, unknown=unknown, timeout=timeout)
        pure = (ROOT / 'examples/proof/src/Amount.scuzz').read_text()
        claim = (ROOT / 'examples/proof/balance.scuzz_verify').read_text()
        witness = report['checks']['defect']['queries']['counterexample']['input']
        assert witness == 2**63 - 1, witness
        assert data['defective_source'].endswith('\n' + claim)
        defect = data['defective_source'][:-len('\n' + claim)]
        report['replays'] = [replay(cli, pure, claim, data['probe_files'], witness, output / 'correct', output, 'correct', True),
                             replay(cli, defect, claim, data['defect_probe_files'], witness, output / 'defect', output, 'defect', False),
                             replay(cli, pure, claim, data['probe_files'], -2**63, output / 'minimum', output, 'minimum', True)]
        command(cli, ['fuzz', '--iterations', '16', output / 'correct'], output, 'ordinary-fuzz')
        report['ordinary_fuzz'] = json.loads((output / 'correct/build/fuzz/summary.json').read_text())
        assert report['ordinary_fuzz']['status'] == 'passed'
        try:
            replay(cli, defect, claim, data['probe_files'], witness,
                   output / 'mismatch', output, 'mismatch', False)
        except AssertionError as error:
            assert 'replay mismatch' in str(error), error
            report['mismatch_check'] = dict(status='rejected', reason='evaluator and native verdicts differ')
        else:
            raise AssertionError('replay mismatch is not rejected')
        report['live_validation'] = live_validation(cli, pure, output / 'validation', output)
        report['protocol'] = protocol_checks(cli, experiment['protocol'], output)
        try:
            protocol_checks(cli, experiment['protocol'], output / 'truncated', contention_limit=0)
        except IncompleteCheck as error:
            report['protocol_bound_check'] = dict(status='incomplete', reason=str(error))
        else:
            raise AssertionError('a truncated schedule check produces a result')
        report['checking_seconds'] = time.monotonic() - started
        if report['checking_seconds'] > 60:
            raise IncompleteCheck('checking budget is incomplete')
        report['status'] = 'passed'
        save()
        print(f'Formal experiment checks pass. Report: {report_path}')
        return 0
    except Exception as error:
        status = 'incomplete' if isinstance(error, (IncompleteCheck, subprocess.TimeoutExpired, FileNotFoundError)) else 'failed'
        report.update(status=status, reason=str(error), checking_seconds=time.monotonic() - started)
        save()
        raise


def recovery_sources():
    selected = {
        'examples/editor/src/Cards.scuzz': ['text', 'str', 'pairs', 'safePath', 'need', 'inside', 'storage',
            'setupStorage', 'propsOf', 'sourceWrites', 'matches', 'writeOne', 'recordPath', 'finish',
            'readOptional', 'recover', 'undoOperation'],
        'examples/editor/generation/src/Proposal.scuzz': ['str', 'pairsJson', 'pairs', 'safePath', 'inside'],
        'examples/editor/generation/src/Models.scuzz': ['need'],
        'examples/editor/src/Diags.scuzz': ['jsonField'],
        'examples/editor/src/LspClient.scuzz': ['parseJson'],
    }
    sources, identities = {}, []
    for name, functions in selected.items():
        source = (ROOT / name).read_text()
        starts = list(re.finditer(r'^def ([A-Za-z_][A-Za-z0-9_]*)\(', source, re.M))
        bodies = {match[1]: source[match.start():starts[i + 1].start() if i + 1 < len(starts) else len(source)]
                  for i, match in enumerate(starts)}
        assert set(functions) <= bodies.keys(), ('production function is missing', name)
        subset = ''.join(bodies[function] for function in functions)
        sources[Path(name).stem] = subset
        identities.append(dict(path=name, sha256=digest(source.encode()), functions=functions,
                               selected_sha256=digest(subset.encode())))
    sources['Recovery'] = (ROOT / 'examples/proof/recovery.scuzz').read_text()
    sources['Main'] = '@main def main: IO[Unit] = IO.pure(())\n'
    return sources, identities


def recovery_package(cli, sources, output, variant):
    package = output / variant
    (package / 'src').mkdir(parents=True, exist_ok=True)
    (package / 'scuzz.toml').write_text('[package]\nname="recovery"\n')
    for name, source in sources.items():
        (package / 'src' / (name + '.scuzz')).write_text(source)
    setup = ['def setup(): IO[Unit] =', '  for {']
    before = '@main def main: IO[Unit] =\n  IO.println(1)\n'
    after = '@main def main: IO[Unit] =\n  IO.println(2)\n'
    addition = 'def other(): Int =\n  3\n'
    identity = digest(b'finite recovery')
    writes = [dict(path='src/Main.scuzz', before=before, before_exists=True, after=after, after_exists=True),
              dict(path='src/Other.scuzz', before='', before_exists=False, after=addition, after_exists=True)]
    accepted = dict(operation='accept-' + identity, card=identity, decision='accept', writes=writes)
    undone = dict(operation='undo-' + identity, card=identity, decision='undo',
                  writes=[dict(path=w['path'], before=w['after'], before_exists=w['after_exists'],
                               after=w['before'], after_exists=w['before_exists']) for w in writes], time=2)
    for mode in range(4):
        root = f'recovery-{mode}'
        storage = root + '/.scuzz/ide'
        for path in (root + '/src', storage + '/cards', storage + '/records'):
            setup.append(f'    _ <- Fs.mkdirs({json.dumps(path)})')
        files = {root + '/src/Main.scuzz': 'external bytes' if mode in (1, 3) else after if mode == 0 else before,
                 storage + '/journal.json': json.dumps(accepted if mode < 2 else undone, separators=(',', ':'))}
        if mode >= 2:
            files[root + '/src/Other.scuzz'] = addition
            files[storage + '/last.json'] = json.dumps(accepted, separators=(',', ':'))
        for path, content in files.items():
            setup.append(f'    _ <- Fs.write({json.dumps(path)}, {json.dumps(content)})')
    setup.append('  } yield ()')
    (package / 'recovery.scuzz_scenario').write_text('\n'.join(setup) + '\n\n'
        'def faults(): List[String] = ["fs:fail"]\n\n'
        'def recover(n: Int where n >= 0 && n <= 7): IO[Unit] = Recovery.run(n)\n\n'
        'def repeat(n: Int where n == 0 || n == 2): IO[Unit] = Recovery.repeat(n)\n\n'
        'def preserve(n: Int where n == 1 || n == 3 || n == 5 || n == 7): IO[Unit] = Recovery.preserve(n)\n')
    (package / 'recovery.scuzz_verify').write_text(
        'def externalBytes(t: Timeline): Verdict =\n'
        '  Verdict.every(t, i => !Timeline.driveHas(t, i, "recover") || '
        'Timeline.fileTextIs(t, i, "recovery-1/src/Main.scuzz", "external bytes") && '
        'Timeline.fileTextIs(t, i, "recovery-3/src/Main.scuzz", "external bytes"))\n')
    command(cli, ['fmt', package], output, variant + '-format')
    command(cli, ['fuzz', '--iterations', '0', package], output, variant + '-build')
    exported = command(cli, ['run', 'examples/proof'], output, variant + '-export',
                       env=dict(os.environ, SCUZZ_PROOF_EXPORT='recovery', SCUZZ_RECOVERY_PACKAGE=str(package)))
    files = json.loads(next(line for line in exported.stdout.splitlines() if line.startswith('[')))
    probe = package / 'evaluator'
    probe.mkdir(exist_ok=True)
    (probe / 'files.txt').write_text(''.join(name + '\n' for name, _ in files))
    for index, (_, source) in enumerate(files):
        (probe / f'{index}.scuzz').write_text(source)
    (package / 'corpus').mkdir(exist_ok=True)
    for n in range(8):
        (package / f'corpus/recovery-{n}.toml').write_text(
            '[fuzz]\nschedule_seed="0"\nfault_seed="0"\n' +
            'events=' + json.dumps([f'drive recover {n}']) + '\n')
    for driver, n in [('repeat', 0), ('repeat', 2)] + [('preserve', n) for n in (1, 3, 5, 7)]:
        (package / f'corpus/{driver}-{n}.toml').write_text(
            '[fuzz]\nschedule_seed="0"\nfault_seed="0"\n' +
            'events=' + json.dumps([f'drive {driver} {n}']) + '\n')
    return package, package / 'build/recovery', probe


def recovery_finite(cli, package, executable, probe, output, defective=False):
    started = time.monotonic()
    rows, incomplete, unique = [], [], set()
    faults = [0] + [1 + 9 * i for i in range(16)] + [289 + 9 * i for i in range(16)]
    bounds = dict(inputs=list(range(8)), fault_calls=[1, 16], fault_modes=['fail', 'partial'],
                  contention_steps=1000, scheduler_steps=1000, executions=2048, seconds=60)
    required = {'recovery preserves external bytes', 'recovery completes both source writes',
                'recovery retains a conflicting journal', 'recovery clears a completed journal',
                'recovery records completed writes', 'recovery retains the Undo state'}
    def remaining():
        seconds = bounds['seconds'] - (time.monotonic() - started)
        if seconds <= 0:
            raise IncompleteCheck('checking budget')
        return seconds
    pending = deque((n, fault, prefix) for n in bounds['inputs'] for fault in faults
                    for prefix in ([(), (2, 2)] if n >= 4 else [()]))
    queued = set(pending)
    covered = set()
    try:
        while pending:
            if time.monotonic() - started >= bounds['seconds'] or len(rows) >= bounds['executions']:
                sequential = [row for row in rows if row['input'] < 4]
                return dict(status='incomplete', reason='checking budget', bounds=bounds,
                            seconds=time.monotonic() - started, workloads=rows, incomplete=incomplete,
                            distinct_replays=len(unique), pending_prefixes=len(pending),
                            sequential_scope=dict(status='passed' if len(sequential) == 132 else 'incomplete',
                                                  inputs=[0, 1, 2, 3], workloads=len(sequential)))
            n, fault, prefix = pending.popleft()
            if (n, fault, prefix) in covered:
                continue
            directory = output / f'finite-{len(rows)}'
            row = protocol_probe(executable, directory, 'recover', n, fault, prefix,
                                 seconds=min(20, remaining()))
            rows.append(row)
            row['executed_properties'] = executed_properties(directory / 'coverage.txt')
            terminals = [run for run in row['runs'] if run['name'] == 'recover']
            if len(terminals) != 1:
                raise IncompleteCheck('terminal scheduler evidence is missing')
            terminal = terminals[0]
            choices = [choice for choice in row['choices'] if choice['name'] == 'recover']
            if terminal['contention'] > bounds['contention_steps'] or terminal['steps'] > bounds['scheduler_steps'] or row['status'] == 'incomplete':
                row['scope_status'] = 'incomplete'
                incomplete.append(dict(input=n, fault=fault, reason='scheduler bound or incomplete execution',
                                       workload=row['workload'], contention=terminal['contention']))
                continue
            assert terminal['bounded'] and len(choices) == terminal['contention']
            picks = next(line for line in row['schedule_picks'] if line.startswith('recover ')).split(':')[1]
            path = tuple(map(int, picks.split(','))) if picks != '-' else ()
            assert len(path) == len(choices) and path[:len(prefix)] == prefix
            for length in range(len(path) + 1):
                covered.add((n, fault, path[:length]))
            key = n, fault, path
            if key not in unique:
                unique.add(key)
                row['parity'] = protocol_parity(cli, probe, row, directory, output, 1000, seconds=remaining())
                if row['status'] == 'passed':
                    assert 'checked\texternalBytes' in (directory / 'claims.txt').read_text()
            if row['status'] == 'failed':
                assert defective, ('application recovery rejects', row)
                command(cli, ['fuzz', '--replay', row['workload'], package], output,
                        'counterexample-replay', success=False, seconds=remaining())
                return dict(status='failed', bounds=bounds, counterexample=row,
                            seconds=time.monotonic() - started, workloads=rows, incomplete=incomplete)
            assert required <= set(row['executed_properties']), 'recovery author rules do not execute'
            for index, choice in enumerate(choices):
                assert choice['step'] == index + 1 and path[index] in choice['ready']
                for alternative in choice['ready']:
                    branch = path[:index] + (alternative,)
                    if alternative != path[index] and (n, fault, branch) not in queued:
                        queued.add((n, fault, branch))
                        pending.append((n, fault, branch))
    except (IncompleteCheck, subprocess.TimeoutExpired) as error:
        sequential = [row for row in rows if row['input'] < 4]
        return dict(status='incomplete', reason='checking budget' if isinstance(error, subprocess.TimeoutExpired) else str(error), bounds=bounds,
                    seconds=time.monotonic() - started, workloads=rows, incomplete=incomplete,
                    distinct_replays=sum('parity' in row for row in rows), pending_prefixes=len(pending) + 1,
                    sequential_scope=dict(status='passed' if len(sequential) == 132
                        and all(row['status'] == 'passed' and 'parity' in row for row in sequential)
                        else 'incomplete', inputs=[0, 1, 2, 3], workloads=len(sequential)))
    reached = {name: any(name in (Path(row['workload']).parent / 'stdout').read_text()
                        for row in rows if 'parity' in row)
               for name in ('Fs: injected fault', 'Fs: partial write', 'fiber interrupted')}
    assert all(reached.values()), ('required application paths do not execute', reached)
    sequential = [row for row in rows if row['input'] < 4]
    assert len(sequential) == 132 and all(row['status'] == 'passed' and 'parity' in row for row in sequential)
    return dict(status='incomplete' if incomplete else 'passed', bounds=bounds, reached=reached,
                sequential_scope=dict(status='passed', inputs=[0, 1, 2, 3], workloads=len(sequential)),
                seconds=time.monotonic() - started, workloads=rows, incomplete=incomplete,
                distinct_replays=len(unique))


def recovery_sampled(cli, package, output):
    started = time.monotonic()
    process = subprocess.Popen([str(cli), 'fuzz', '--no-fail-fast', '--iterations', '64', '--seed', '42', str(package)],
                               text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
        (output / 'sampled.stdout').write_text(stdout)
        (output / 'sampled.stderr').write_text(stderr)
        summary = json.loads((package / 'build/fuzz/summary.json').read_text())
        return dict(status=summary['status'], exit=process.returncode, seconds=time.monotonic() - started,
                    seconds_budget=60, requested_iterations=64, report=summary)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
        (output / 'sampled.stdout').write_text(stdout)
        (output / 'sampled.stderr').write_text(stderr)
        return dict(status='incomplete', reason='checking budget', seconds=time.monotonic() - started,
                    seconds_budget=60, requested_iterations=64)


def recovery_experiment(cli):
    destination = ROOT / 'examples/proof/build/recovery'
    destination.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=destination))
    report = dict(status='incomplete', methods=['exhaustive-bounded-simulation', 'ordinary-fuzz'],
                  trusted=['source function selection', 'Scuzz compiler and evaluator', 'C runtime', 'host OS'],
                  open_obligations=['fault calls after 16', 'multiple faults', 'external edits during recovery',
                                    'live filesystem and process termination', 'other application paths'])
    path = destination / 'summary.json'
    def save():
        path.write_text(json.dumps(report, indent=2) + '\n')
    save()
    try:
        sources, identities = recovery_sources()
        report['identities'] = dict(production_sources=identities, compiler_sha256=digest(cli.read_bytes()),
            runtime_sha256=digest((ROOT / 'crates/runtime/build/libscuzz_rt.a').read_bytes()),
            checking_script_sha256=digest(Path(__file__).read_bytes()),
            author_source_sha256=digest(sources['Recovery'].encode()), platform=platform.platform())
        clang = shutil.which('clang')
        if clang is None:
            raise IncompleteCheck('native compiler identity is missing')
        report['identities']['clang'] = dict(path=clang, sha256=digest(Path(clang).read_bytes()),
            version=subprocess.run([clang, '--version'], capture_output=True, text=True,
                                   check=True, timeout=5).stdout)
        report['identities']['graph_exporter_source_sha256'] = digest((ROOT / 'examples/proof/src/Main.scuzz').read_bytes())
        report['identities']['python'] = platform.python_version()
        report['scope'] = dict(files=2, operations=['accept', 'undo'], external_edit='before recovery only',
                              entry='resume one pending write, then recover twice',
                              methods_budget_seconds=60, sampled_seed=42, sampled_iterations=64, required_recovery_inputs=list(range(8)),
                              required_repeat_inputs=[0, 2], required_preserve_inputs=[1, 3, 5, 7],
                              observed_files=['two sources', 'journal', 'operation record', 'Undo state'],
                              ordinary_scope='the default search also includes repeated calls and persistent faults')
        variants = {'correct': sources}
        skip = dict(sources)
        anchor = '_ <- IO.foreachDiscard(ws, w => writeOne(root, w))'
        assert sources['Cards'].count(anchor) == 1
        skip['Cards'] = sources['Cards'].replace(anchor, '_ <- IO.pure(())')
        variants['missing-write'] = skip
        overwrite = dict(sources)
        anchor = '_ <- need(List.forall(checks, b => b), "recovery conflict: preserve external edits")'
        assert sources['Cards'].count(anchor) == 1
        overwrite['Cards'] = sources['Cards'].replace(anchor, '_ <- IO.pure(())')
        anchor = '_ <- need(after || before, Str.concat("recovery conflict: ", str(w, "path")))'
        assert overwrite['Cards'].count(anchor) == 1
        overwrite['Cards'] = overwrite['Cards'].replace(anchor, '_ <- IO.pure(())')
        variants['external-overwrite'] = overwrite
        report['variants'] = {}
        for variant, source in variants.items():
            started = time.monotonic()
            package, executable, probe = recovery_package(cli, source, output, variant)
            row = dict(build_seconds=time.monotonic() - started,
                       native_sha256=digest(executable.read_bytes()),
                       inputs=[dict(path=str(file.relative_to(package)), sha256=digest(file.read_bytes()))
                               for file in sorted(package.rglob('*')) if file.is_file()
                               and file.suffix in ('.scuzz', '.scuzz_scenario', '.scuzz_verify', '.toml')
                               and 'build' not in file.relative_to(package).parts],
                       exporter_sha256=digest((ROOT / 'examples/proof/build/proof').read_bytes()))
            report['variants'][variant] = row
            print('Check recovery:', variant, flush=True)
            if variant == 'correct':
                checked = time.monotonic()
                rules = []
                inputs = [('recover', n) for n in range(8)] + [('repeat', 0), ('repeat', 2)]
                inputs += [('preserve', n) for n in (1, 3, 5, 7)]
                for driver, n in inputs:
                    directory = package / f'rule-{driver}-{n}'
                    witness = protocol_probe(executable, directory, driver, n, 0)
                    assert witness['status'] == 'passed', ('recovery rule check rejects', witness)
                    witness['executed_properties'] = executed_properties(directory / 'coverage.txt')
                    if driver == 'repeat':
                        assert 'repeated recovery preserves settled files' in witness['executed_properties']
                    elif driver == 'preserve':
                        assert 'recovery conflict preserves all operation files' in witness['executed_properties']
                    else:
                        assert {'recovery preserves external bytes', 'recovery completes both source writes',
                                'recovery retains a conflicting journal', 'recovery clears a completed journal',
                                'recovery records completed writes', 'recovery retains the Undo state'} <= set(witness['executed_properties'])
                    witness['parity'] = protocol_parity(cli, probe, witness, directory, package, 1000)
                    rules.append(witness)
                report['rule_checks'] = dict(status='passed', fault=0, workloads=rules,
                                            seconds=time.monotonic() - checked)
                save()
            row['finite'] = recovery_finite(cli, package, executable, probe, package, variant != 'correct')
            save()
            row['sampled'] = recovery_sampled(cli, package, package)
            search = row['sampled'].get('report', {}).get('scope', {}).get('search', {})
            row['sampled']['search_status'] = ('failed' if search.get('failed', 0) else 'incomplete'
                if search.get('incomplete', 0) or not search else 'passed')
            save()
            if variant != 'correct':
                assert row['finite']['status'] == 'failed', 'deliberate recovery defect is not rejected'
                assert row['sampled']['search_status'] == 'failed', 'ordinary search does not reject the deliberate defect'
        report['mutation_regressions'] = {}
        regressions = [
            ('field-lookup', 'Diags', 'p._1 == key', 'p._1 != key', 'recover', 0,
             'recovery returns an expected error'),
            ('path-guard', 'Models', 'if (ok) IO.pure(()) else IO.fail(why)',
             'if (ok) IO.fail(why) else IO.pure(())', 'recover', 0, 'recovery returns an expected error'),
            ('resolved-path', 'Proposal', 'resolved == Fs.join(base, path)',
             'resolved != Fs.join(base, path)', 'recover', 0, 'recovery returns an expected error'),
            ('conflict-write', 'Cards',
             '_ <- need(List.forall(checks, b => b), "recovery conflict: preserve external edits")',
             '_ <- if (List.forall(checks, b => b)) IO.pure(()) else Fs.write(Fs.join(root, "src/Other.scuzz"), "unexpected bytes")\n'
             '    _ <- need(List.forall(checks, b => b), "recovery conflict: preserve external edits")',
             'preserve', 1, 'recovery conflict preserves all operation files'),
            ('repeat-undo-state', 'Cards',
             'if (Str.isEmpty(raw)) IO.pure(()) else finish(root, LspClient.parseJson(raw))',
             'if (Str.isEmpty(raw)) Fs.write(Fs.join(storage(root), "last.json"), "") else finish(root, LspClient.parseJson(raw))',
             'repeat', 0, 'repeated recovery preserves settled files'),
        ]
        for name, module, anchor, replacement, driver, n, rule in regressions:
            changed = dict(sources)
            assert changed[module].count(anchor) == 1
            changed[module] = changed[module].replace(anchor, replacement)
            started = time.monotonic()
            package, executable, probe = recovery_package(cli, changed, output, 'mutation-' + name)
            build_seconds = time.monotonic() - started
            directory = package / 'witness'
            started = time.monotonic()
            witness = protocol_probe(executable, directory, driver, n, 0)
            native_seconds = time.monotonic() - started
            assert witness['status'] == 'failed' and rule in witness['rejection']
            assert next(run for run in witness['runs'] if run['name'] == 'setup')['ok']
            started = time.monotonic()
            witness['parity'] = protocol_parity(cli, probe, witness, directory, package, 1000)
            parity_seconds = time.monotonic() - started
            started = time.monotonic()
            replay = command(cli, ['fuzz', '--replay', witness['workload'], package], package,
                             'mutation-replay', success=False)
            replay_seconds = time.monotonic() - started
            replay_report = json.loads((package / 'build/fuzz/summary.json').read_text())
            assert replay_report['status'] == 'failed' and rule in replay_report['guards']['properties']
            assert replay_report['scope']['required']['incomplete'] == 0
            assert f'oracle {rule} rejected' in replay.stdout
            report['mutation_regressions'][name] = dict(status='detected', module=module,
                source_sha256=digest(changed[module].encode()), native_sha256=digest(executable.read_bytes()),
                build_seconds=build_seconds, native_check_seconds=native_seconds,
                parity_seconds=parity_seconds, replay_seconds=replay_seconds,
                counterexample=witness)
            if name == 'conflict-write':
                directory = package / 'existing-rules'
                previous = protocol_probe(executable, directory, 'recover', 1, 0)
                assert previous['status'] == 'passed', 'existing recovery rules reject the conflict control'
                previous['parity'] = protocol_parity(cli, probe, previous, directory, package, 1000)
                report['mutation_regressions'][name]['existing_rules'] = previous
            save()
        report['comparison'] = dict(
            deliberate_defects=dict(finite=2, ordinary_search=2),
            stronger_rule_control=dict(defect='conflict-write', existing_rules='passed', added_rule='failed'),
            conclusion='Both methods reject the controls. General application benefit remains unproven.',
            limits=['finite exploration must complete within its checking budget',
                    'search scopes differ; timing does not establish a speed advantage'])
        report['status'] = 'incomplete' if report['variants']['correct']['finite']['status'] == 'incomplete' else 'passed'
        if any(row['sampled']['status'] == 'incomplete' for row in report['variants'].values()):
            report['status'] = 'incomplete'
        save()
        print('Recovery experiment:', report['status'], 'Report:', path)
        return 2 if report['status'] == 'incomplete' else 0
    except Exception as error:
        report.update(status='incomplete' if isinstance(error, (IncompleteCheck, subprocess.TimeoutExpired, FileNotFoundError)) else 'failed',
                      reason=str(error))
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())

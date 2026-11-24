#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

# CLI and language server share project diagnostics.
python3 - <<'PY_PROJECT'
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
def command(*args, ok=True):
    result = subprocess.run([str(cli), *map(str, args)], capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result

def diagnostics(root, source, additional=None):
    messages = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'rootUri': root.as_uri()}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': (root / 'src/Main.scuzz').as_uri(), 'languageId': 'scuzz', 'version': 1, 'text': source}}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'shutdown', 'params': None},
        {'jsonrpc': '2.0', 'method': 'exit', 'params': None},
    ]
    if additional:
        messages.insert(2, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': additional[0].as_uri(), 'languageId': 'scuzz', 'version': 1, 'text': additional[1]}}})
    wire = b''
    for message in messages:
        body = json.dumps(message).encode()
        wire += f'Content-Length: {len(body)}\r\n\r\n'.encode() + body
    run = subprocess.run([str(cli), 'lsp', str(root)], input=wire, capture_output=True, timeout=180)
    assert run.returncode == 0, run.stderr.decode()
    data = run.stdout
    rows = []
    while data:
        header, data = re.split(b'\r?\n\r?\n', data, maxsplit=1)
        size = int(header.split(b':', 1)[1].strip())
        message = json.loads(data[:size])
        data = data[size:]
        if message.get('method') == 'textDocument/publishDiagnostics':
            rows += message['params']['diagnostics']
    return rows

def lsp_format_edits(root, source):
    uri = (root / 'src/Main.scuzz').as_uri()
    messages = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'rootUri': root.as_uri()}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'scuzz', 'version': 1, 'text': source}}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/formatting', 'params': {'textDocument': {'uri': uri}, 'options': {'tabSize': 2, 'insertSpaces': True}}},
        {'jsonrpc': '2.0', 'id': 3, 'method': 'shutdown', 'params': None},
        {'jsonrpc': '2.0', 'method': 'exit', 'params': None},
    ]
    wire = b''
    for message in messages:
        body = json.dumps(message).encode()
        wire += f'Content-Length: {len(body)}\r\n\r\n'.encode() + body
    run = subprocess.run([str(cli), 'lsp', str(root)], input=wire, capture_output=True, timeout=180)
    assert run.returncode == 0, run.stderr.decode()
    data = run.stdout
    while data:
        header, data = re.split(b'\r?\n\r?\n', data, maxsplit=1)
        size = int(header.split(b':', 1)[1].strip())
        message = json.loads(data[:size])
        data = data[size:]
        if message.get('id') == 2:
            return message.get('result')
    raise AssertionError('missing formatting response')

with tempfile.TemporaryDirectory(prefix='scuzz-project-check-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    manifest = root / 'scuzz.toml'
    source_file = root / 'src/Main.scuzz'
    manifest.write_text('[package]\nname="project-check"\n')
    source_file.write_text('@main def main: IO[Unit] = IO.pure(())\n')
    command('fmt', root)
    source = source_file.read_text()
    good_manifest = manifest.read_text()
    for mode in ('missing-dependency', 'invalid-manifest', 'format', 'verify', 'typed-verify', 'typed-driver'):
        (root / "bad.scuzz_verify").unlink(missing_ok=True)
        (root / "run.scuzz_scenario").unlink(missing_ok=True)
        manifest.write_text(good_manifest)
        source_file.write_text(source)
        if mode == 'missing-dependency':
            manifest.write_text(good_manifest + '[dependencies]\nhelper={path="dep"}\n')
        elif mode == 'invalid-manifest':
            manifest.write_text('[package]\nname="project-check"\nunknown=1\n')
        elif mode == 'format':
            source_file.write_text('@main def main: IO[Unit] = IO.pure(())')
        elif mode == 'verify':
            (root / 'bad.scuzz_verify').write_text('def broken(): Bool = true\n')
        elif mode == 'typed-verify':
            (root / 'bad.scuzz_verify').write_text('oracle wrong(): Bool = 7\n')
            command('fmt', root)
        else:
            (root / 'run.scuzz_scenario').write_text('def setup(): IO[Unit] = IO.pure(())\ndef wrong(): IO[Unit] = IO.println(7)\n')
            command('fmt', root)
        report = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
        errors = [row for row in report if row['severity'] == 'error']
        assert errors, (mode, report)
        rows = diagnostics(root, source_file.read_text())
        assert [row['message'] for row in rows if row['severity'] == 1] == [row['message'] for row in errors], (mode, errors, rows)
    (root / 'bad.scuzz_verify').unlink(missing_ok=True)
    (root / 'run.scuzz_scenario').unlink(missing_ok=True)
    manifest.write_text(good_manifest)
    source_file.write_text(source)
    command('check', root)
    assert not [row for row in diagnostics(root, source) if row['severity'] == 1]
    source_file.write_text('@main def main: IO[Unit] = IO.println(missing)\n')
    command('fmt', root)
    overlay = source_file.read_text()
    errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
    source_file.write_text(source)
    rows = diagnostics(root, overlay)
    assert [row['message'] for row in rows if row['severity'] == 1] == [row['message'] for row in errors], (errors, rows)
    assert 'unbound variable missing' in rows[0]['message'], rows
    parse_cases = [
        ('def ok(): Int = 1\nrecord record(value: Int)\n', 2, 8, 2, 14, 'unexpected token; expected identifier'),
        ('private oracle hidden(): Bool = true\n', 1, 9, 1, 15, 'unexpected token; expected def'),
        ('def f(x: Int = ): Int = 1\n', 1, 16, 1, 17, 'unexpected token; expected expression'),
        ('def f(x: Int where ): Int = 1\n', 1, 20, 1, 21, 'unexpected token; expected expression'),
        ('import Domain\n@main def main: IO[Unit] = IO.pure(())\n', 2, 1, 2, 6, 'unexpected token; expected .'),
        ('@main def record: IO[Unit] = IO.println(1)\n', 1, 11, 1, 17, 'unexpected token; expected identifier'),
        ('def f(): Int = 1 match { case n if => n }\n', 1, 36, 1, 38, 'unexpected token; expected expression'),
        ('def f(): Int = (1 + 2', 1, 22, 1, 23, 'unexpected token; expected )'),
        ('def broken(): Int = 1\n)\n', 2, 1, 2, 2, 'unexpected token; expected declaration'),
    ]
    for bad_src, line, col, eline, ecol, message in parse_cases:
        source_file.write_text(bad_src)
        human = command('check', root, ok=False).stdout.strip()
        assert f'Main.scuzz:{line}:{col}: error: {message}' in human, (bad_src, human)
        errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
        assert len(errors) == 1, (bad_src, errors)
        err = errors[0]
        assert (err['file'], err['message'], err['line'], err['column'], err['end_line'], err['end_column']) == (
            'Main.scuzz', message, line, col, eline, ecol
        ), (bad_src, err)
        source_file.write_text(source)
        rows = [row for row in diagnostics(root, bad_src) if row['severity'] == 1]
        assert len(rows) == 1 and rows[0]['message'] == message, (bad_src, rows)
        assert rows[0]['range'] == {
            'start': {'line': line - 1, 'character': col - 1},
            'end': {'line': eline - 1, 'character': ecol - 1},
        }, (bad_src, rows[0])
    multi_bad_src = (
        'import record.Item\n'
        'trait record:\n'
        '  def f(): Int\n'
        'def depthGuard(): Int = for {\n'
        '  x = 1 +\n'
        'record = 2\n'
        '} yield x\n'
        'def semanticBad(): Int = "bad" + 1\n'
        '@main def record: IO[Unit] = IO.println("x")\n'
    )
    multi_spans = [
        (1, 8, 1, 14, 'unexpected token; expected identifier'),
        (2, 7, 2, 13, 'unexpected token; expected identifier'),
        (6, 1, 6, 7, 'unexpected token; expected expression'),
        (9, 11, 9, 17, 'unexpected token; expected identifier'),
    ]
    source_file.write_text(multi_bad_src)
    fmt_out = command('fmt', root, ok=False)
    assert 'parse error; source is unchanged' in fmt_out.stdout, fmt_out.stdout
    assert source_file.read_text() == multi_bad_src
    human = command('check', root, ok=False).stdout.strip().splitlines()
    assert human == [
        f'Main.scuzz:{line}:{col}: error: {message}'
        for line, col, _, _, message in multi_spans
    ], human
    errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
    assert [
        (err['file'], err['message'], err['line'], err['column'], err['end_line'], err['end_column'])
        for err in errors
    ] == [
        ('Main.scuzz', message, line, col, eline, ecol)
        for line, col, eline, ecol, message in multi_spans
    ], errors
    source_file.write_text(source)
    rows = [row for row in diagnostics(root, multi_bad_src) if row['severity'] == 1]
    assert [
        (row['message'], row['range'])
        for row in rows
    ] == [
        (
            message,
            {
                'start': {'line': line - 1, 'character': col - 1},
                'end': {'line': eline - 1, 'character': ecol - 1},
            },
        )
        for line, col, eline, ecol, message in multi_spans
    ], rows
    intra_decl_src = (
        'trait Codec[A]:\n'
        '  def encode(x: A where ): String\n'
        '  def decode(raw: ): A\n'
        'impl Codec[Int] for Box:\n'
        '  def encode(x: ): String =\n'
        '    for {\n'
        '      a = 1 +\n'
        '      b = "bad" +\n'
        '      c = x match {\n'
        '        case 0 if => 10\n'
        '        case 1 => 20 +\n'
        '        case _ => 30\n'
        '      }\n'
        '    } yield Str.fromInt(c)\n'
        'def blockBad(): Int = {\n'
        '  u = 1 + )\n'
        '  v = "wrong" + ]\n'
        '  u + v\n'
        '}\n'
        '@main def main: IO[Unit] =\n'
        '  IO.println("first")\n'
        '  IO.println(1 + )\n'
    )
    intra_decl_spans = [
        (2, 25, 2, 26, 'unexpected token; expected expression'),
        (3, 19, 3, 20, 'unexpected token; expected identifier'),
        (5, 17, 5, 18, 'unexpected token; expected identifier'),
        (8, 7, 8, 8, 'unexpected token; expected expression'),
        (9, 7, 9, 8, 'unexpected token; expected expression'),
        (10, 19, 10, 21, 'unexpected token; expected expression'),
        (12, 9, 12, 13, 'unexpected token; expected expression'),
        (15, 23, 15, 24, 'unexpected token; expected case'),
        (16, 11, 16, 12, 'unexpected token; expected expression'),
        (17, 17, 17, 18, 'unexpected token; expected expression'),
        (22, 18, 22, 19, 'unexpected token; expected expression'),
    ]
    source_file.write_text(intra_decl_src)
    fmt_out = command('fmt', root, ok=False)
    assert 'parse error; source is unchanged' in fmt_out.stdout, fmt_out.stdout
    assert source_file.read_text() == intra_decl_src
    human = command('check', root, ok=False).stdout.strip().splitlines()
    assert human == [
        f'Main.scuzz:{line}:{col}: error: {message}'
        for line, col, _, _, message in intra_decl_spans
    ], human
    errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
    assert [
        (err['file'], err['message'], err['line'], err['column'], err['end_line'], err['end_column'])
        for err in errors
    ] == [
        ('Main.scuzz', message, line, col, eline, ecol)
        for line, col, eline, ecol, message in intra_decl_spans
    ], errors
    source_file.write_text(source)
    rows = [row for row in diagnostics(root, intra_decl_src) if row['severity'] == 1]
    assert [
        (row['message'], row['range'])
        for row in rows
    ] == [
        (
            message,
            {
                'start': {'line': line - 1, 'character': col - 1},
                'end': {'line': eline - 1, 'character': ecol - 1},
            },
        )
        for line, col, eline, ecol, message in intra_decl_spans
    ], rows
    assert lsp_format_edits(root, intra_decl_src) == []
    dep = root / 'dep'
    (dep / 'src').mkdir(parents=True)
    (dep / 'scuzz.toml').write_text('[package]\nname="helper"\n')
    helper = dep / 'src/Help.scuzz'
    helper.write_text('def word(): String = "ok"\n')
    manifest.write_text(good_manifest + '[dependencies]\nhelper={path="dep"}\n')
    source_file.write_text('@main def main: IO[Unit] = IO.println(Help.word())\n')
    command('fmt', root)
    source = source_file.read_text()
    helper.write_text('def word(): Int = 7\n')
    command('fmt', dep)
    bad_helper = helper.read_text()
    errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
    helper.write_text('def word(): String =\n  "ok"\n\n')
    rows = diagnostics(root, source, (helper, bad_helper))
    assert any(row['message'] == errors[0]['message'] and row['severity'] == 1 for row in rows), (errors, rows)
    (dep / 'scuzz.toml').write_text('[package]\nname="helper"\n[dependencies]\nagain={path=".."}\n')
    errors = json.loads(command('check', '--message-format=json', root, ok=False).stdout)
    assert errors[0]['scope'] == 'project' and 'cycle' in errors[0]['message'], errors

def lsp_session(root, messages):
    wire = b''
    for message in messages:
        body = json.dumps(message).encode()
        wire += f'Content-Length: {len(body)}\r\n\r\n'.encode() + body
    run = subprocess.run([str(cli), 'lsp', str(root)], input=wire, capture_output=True, timeout=180)
    assert run.returncode == 0, run.stderr.decode()
    data = run.stdout
    out = []
    while data:
        header, data = re.split(b'\r?\n\r?\n', data, maxsplit=1)
        size = int(header.split(b':', 1)[1].strip())
        out.append(json.loads(data[:size]))
        data = data[size:]
    return out

with tempfile.TemporaryDirectory(prefix='scuzz-lsp-editor-') as directory:
    ws = Path(directory)
    (ws / 'src').mkdir()
    (ws / 'dep/src').mkdir(parents=True)
    (ws / 'scuzz.toml').write_text('[package]\nname="lsp-ws"\n[dependencies]\ndep={path="dep"}\n')
    (ws / 'dep/scuzz.toml').write_text('[package]\nname="dep"\n')
    help_file = ws / 'dep/src/Help.scuzz'
    dep_main = ws / 'dep/src/Main.scuzz'
    main_file = ws / 'src/Main.scuzz'
    help_file.write_text('record Box(v: Int)\n\ndef word(n: Int): String =\n  Str.fromInt(n)\n')
    command('fmt', ws / 'dep')
    valid_main = (
        'enum Choice:\n'
        '  case One\n'
        '  case Two(n: Int)\n\n'
        'record Pair(a: Int, b: Int)\n\n'
        'trait Show:\n'
        '  def show(): String\n\n'
        'impl Show for Pair:\n'
        '  def show(): String =\n'
        '    Help.word(self.a)\n\n'
        'oracle nonneg(x: Int): Bool =\n'
        '  x >= 0\n\n'
        'def calc(x: Int): Int =\n'
        '  for {\n'
        '    y = x + 1\n'
        '    z = (y: Int)\n'
        '    w = Help.word(x)\n'
        '  } yield x match {\n'
        '    case 0 => y\n'
        '    case _ => z + Str.len(w)\n'
        '  }\n\n'
        'def other(x: Int): Int =\n'
        '  x * 2\n\n'
        'def shadowCheck(x: Int, y: Int): Int =\n'
        '  x + y\n\n'
        'def joinNums(a: Int, b: Int): Int =\n'
        '  a + b\n\n'
        'def shifted(p: Pair): Pair =\n'
        '  p.copy(b = p.a)\n\n'
        'def pickedNum(): Int =\n'
        '  joinNums(b = 2, a = 1)\n\n'
        'def cutWord(s: String): String =\n'
        '  s.slice(0, 1)\n\n'
        '@main def main: IO[Unit] =\n'
        '  IO.println(Help.word(calc(1)))\n'
    )
    main_file.write_text(valid_main)
    command('fmt', ws)
    dep_main.write_text('def onlyDep(): Int =\n  "bad"\n\n')
    formatted_on_disk = main_file.read_text()
    unformatted_main = formatted_on_disk.replace('def other(x: Int): Int =\n  x * 2', 'def other(x: Int): Int =   x * 2')
    main_uri = main_file.as_uri()
    help_uri = help_file.as_uri()
    dep_main_uri = dep_main.as_uri()
    calc_line = unformatted_main.splitlines().index('    w = Help.word(x)')
    word_col = unformatted_main.splitlines()[calc_line].index('word')
    x_param_line = unformatted_main.splitlines().index('def calc(x: Int): Int =')
    x_param_col = unformatted_main.splitlines()[x_param_line].index('x: Int')
    shadow_line = unformatted_main.splitlines().index('def shadowCheck(x: Int, y: Int): Int =')
    shadow_col = unformatted_main.splitlines()[shadow_line].index('x: Int')
    sig_lines = unformatted_main.splitlines()
    word_arg_line = next(i for i, line in enumerate(sig_lines) if 'Help.word(x)' in line)
    word_arg_col = sig_lines[word_arg_line].index('Help.word(x)') + len('Help.word(')
    named_line = next(i for i, line in enumerate(sig_lines) if 'joinNums(b = 2, a = 1)' in line)
    named_col = sig_lines[named_line].index('joinNums(b = 2, a = 1)') + len('joinNums(b = ')
    copy_line = next(i for i, line in enumerate(sig_lines) if 'p.copy(b = p.a)' in line)
    copy_col = sig_lines[copy_line].index('p.a') + 2
    slice_line = next(i for i, line in enumerate(sig_lines) if 's.slice(0, 1)' in line)
    slice_col = sig_lines[slice_line].index('s.slice(0, 1)') + len('s.slice(0, ')

    diag_msgs = lsp_session(ws, [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'rootUri': ws.as_uri()}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': main_uri, 'languageId': 'scuzz', 'version': 1, 'text': formatted_on_disk}}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': dep_main_uri, 'languageId': 'scuzz', 'version': 1, 'text': dep_main.read_text()}}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'shutdown', 'params': None},
        {'jsonrpc': '2.0', 'method': 'exit', 'params': None},
    ])
    pub = {}
    for m in diag_msgs:
        if m.get('method') == 'textDocument/publishDiagnostics':
            pub[m['params']['uri']] = m['params']['diagnostics']
    assert any(d['severity'] == 1 for d in pub.get(dep_main_uri, [])), pub
    assert not any(d['severity'] == 1 for d in pub.get(main_uri, [])), pub

    msgs = lsp_session(ws, [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'rootUri': ws.as_uri()}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': main_uri, 'languageId': 'scuzz', 'version': 1, 'text': unformatted_main}}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/inlayHint', 'params': {'textDocument': {'uri': main_uri}}},
        {'jsonrpc': '2.0', 'id': 3, 'method': 'textDocument/foldingRange', 'params': {'textDocument': {'uri': main_uri}}},
        {'jsonrpc': '2.0', 'id': 4, 'method': 'textDocument/codeAction', 'params': {'textDocument': {'uri': main_uri}, 'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': 0}}}},
        {'jsonrpc': '2.0', 'id': 5, 'method': 'textDocument/hover', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}}},
        {'jsonrpc': '2.0', 'id': 6, 'method': 'textDocument/definition', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}}},
        {'jsonrpc': '2.0', 'id': 7, 'method': 'textDocument/completion', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}}},
        {'jsonrpc': '2.0', 'id': 8, 'method': 'textDocument/rename', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': x_param_line, 'character': x_param_col}, 'newName': 'n0'}},
        {'jsonrpc': '2.0', 'id': 9, 'method': 'textDocument/rename', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': shadow_line, 'character': shadow_col}, 'newName': 'y'}},
        {'jsonrpc': '2.0', 'id': 10, 'method': 'textDocument/rename', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': x_param_line, 'character': x_param_col}, 'newName': 'match'}},
        {'jsonrpc': '2.0', 'id': 11, 'method': 'textDocument/rename', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}, 'newName': 'greeting'}},
        {'jsonrpc': '2.0', 'id': 13, 'method': 'textDocument/references', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}, 'context': {'includeDeclaration': True}}},
        {'jsonrpc': '2.0', 'id': 14, 'method': 'textDocument/references', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': calc_line, 'character': word_col}, 'context': {'includeDeclaration': False}}},
        {'jsonrpc': '2.0', 'id': 15, 'method': 'textDocument/references', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': x_param_line, 'character': x_param_col}, 'context': {'includeDeclaration': False}}},
        {'jsonrpc': '2.0', 'id': 16, 'method': 'textDocument/references', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': 0, 'character': 0}}},
        {'jsonrpc': '2.0', 'id': 17, 'method': 'textDocument/signatureHelp', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': word_arg_line, 'character': word_arg_col}}},
        {'jsonrpc': '2.0', 'id': 18, 'method': 'textDocument/signatureHelp', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': named_line, 'character': named_col}}},
        {'jsonrpc': '2.0', 'id': 19, 'method': 'textDocument/signatureHelp', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': copy_line, 'character': copy_col}}},
        {'jsonrpc': '2.0', 'id': 20, 'method': 'textDocument/signatureHelp', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': slice_line, 'character': slice_col}}},
        {'jsonrpc': '2.0', 'id': 21, 'method': 'textDocument/signatureHelp', 'params': {'textDocument': {'uri': main_uri}, 'position': {'line': 0, 'character': 0}}},
        {'jsonrpc': '2.0', 'id': 12, 'method': 'shutdown', 'params': None},
        {'jsonrpc': '2.0', 'method': 'exit', 'params': None},
    ])
    by_id = {m['id']: m['result'] for m in msgs if 'id' in m}
    caps = by_id[1]['capabilities']
    assert caps.get('inlayHintProvider') is True and caps.get('foldingRangeProvider') is True and caps.get('codeActionProvider') is True and caps.get('referencesProvider') is True and caps.get('signatureHelpProvider') is True, caps

    inlays = [(h['position']['line'], h['label']) for h in by_id[2]]
    y_line = next(i for i, l in enumerate(unformatted_main.splitlines()) if 'y = x + 1' in l)
    z_line = next(i for i, l in enumerate(unformatted_main.splitlines()) if 'z =' in l)
    assert (y_line, ': Int') in inlays and (calc_line, ': String') in inlays, inlays
    assert all(line != z_line for line, _ in inlays), inlays

    folds = {(r['startLine'], r['endLine']) for r in by_id[3]}
    assert all(end > start for start, end in folds) and len(folds) >= 6, folds

    actions = by_id[4]
    assert len(actions) == 1 and actions[0]['title'] == 'Format file' and actions[0]['kind'] == 'source.fixAll', actions
    formatted_main = actions[0]['edit']['changes'][main_uri][0]['newText']
    assert formatted_main == formatted_on_disk and formatted_main != unformatted_main

    assert 'def Help.word(n: Int): String' in by_id[5]['contents']['value'], by_id[5]
    assert by_id[6][0]['uri'] == help_uri, by_id[6]
    comp_labels = {item['label'] for item in by_id[7]['items']}
    assert 'Help.word' in comp_labels, comp_labels

    renamed_local = by_id[8]['changes'][main_uri][0]['newText']
    assert 'def calc(n0: Int): Int =' in renamed_local and 'def other(x: Int): Int =' in renamed_local, renamed_local
    assert by_id[9] is None and by_id[10] is None, (by_id[9], by_id[10])
    cross_changes = by_id[11]['changes']
    assert 'Help.greeting' in cross_changes[main_uri][0]['newText'] and 'def greeting(n: Int): String' in cross_changes[help_uri][0]['newText'], cross_changes

    def at_pos(locs, uri, line, col):
        return any(loc['uri'] == uri and loc['range']['start']['line'] == line and loc['range']['start']['character'] == col for loc in locs)

    help_lines = help_file.read_text().splitlines()
    word_def_line = next(i for i, line in enumerate(help_lines) if line.startswith('def word'))
    word_def_col = help_lines[word_def_line].index('word')
    word_refs = by_id[13]
    word_uses = by_id[14]
    assert at_pos(word_refs, help_uri, word_def_line, word_def_col), word_refs
    assert at_pos(word_refs, main_uri, calc_line, word_col), word_refs
    assert any('Help.word' in line for line in unformatted_main.splitlines()) and len(word_refs) >= 2, word_refs
    assert not at_pos(word_uses, help_uri, word_def_line, word_def_col), word_uses
    assert at_pos(word_uses, main_uri, calc_line, word_col) and all(loc['uri'] == main_uri for loc in word_uses), word_uses
    x_uses = by_id[15]
    assert x_uses and not at_pos(x_uses, main_uri, x_param_line, x_param_col), x_uses
    assert all(unformatted_main.splitlines()[loc['range']['start']['line']][loc['range']['start']['character']:].startswith('x') for loc in x_uses), x_uses
    assert by_id[16] is None, by_id[16]

    def active_text(result):
        sig = result['signatures'][0]
        span = sig['parameters'][result['activeParameter']]['label']
        return sig['label'][span[0]:span[1]]

    sig_word = by_id[17]
    assert sig_word['activeSignature'] == 0 and sig_word['activeParameter'] == 0 and 'def Help.word(n: Int): String' in sig_word['signatures'][0]['label'] and active_text(sig_word).startswith('n:'), sig_word
    sig_named = by_id[18]
    assert sig_named['activeParameter'] == 1 and 'def joinNums(a: Int, b: Int): Int' in sig_named['signatures'][0]['label'] and active_text(sig_named).startswith('b:'), sig_named
    sig_copy = by_id[19]
    assert sig_copy['activeParameter'] == 1 and sig_copy['signatures'][0]['label'].startswith('copy(') and active_text(sig_copy).startswith('b:'), sig_copy
    sig_slice = by_id[20]
    assert sig_slice['activeParameter'] == 1 and sig_slice['signatures'][0]['label'] == 'slice(Int, Int): String' and active_text(sig_slice) == 'Int' and sig_slice['signatures'][0]['parameters'][1]['label'][0] > sig_slice['signatures'][0]['parameters'][0]['label'][0], sig_slice
    assert by_id[21] is None, by_id[21]

    clean_msgs = lsp_session(ws, [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'rootUri': ws.as_uri()}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': main_uri, 'languageId': 'scuzz', 'version': 2, 'text': formatted_main}}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/codeAction', 'params': {'textDocument': {'uri': main_uri}, 'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': 0}}}},
        {'jsonrpc': '2.0', 'method': 'textDocument/didChange', 'params': {'textDocument': {'uri': main_uri, 'version': 3}, 'contentChanges': [{'text': 'def broken(\n'}]}},
        {'jsonrpc': '2.0', 'id': 3, 'method': 'textDocument/codeAction', 'params': {'textDocument': {'uri': main_uri}, 'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': 0}}}},
        {'jsonrpc': '2.0', 'id': 4, 'method': 'shutdown', 'params': None},
        {'jsonrpc': '2.0', 'method': 'exit', 'params': None},
    ])
    clean_by_id = {m['id']: m['result'] for m in clean_msgs if 'id' in m}
    assert clean_by_id[2] == [] and clean_by_id[3] == [], clean_by_id
print('Project checks: CLI and language server retain load, format, verify, and overlay errors')
PY_PROJECT

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
paths = [('{"order":{}}', 'error:Json.path: missing field order.quantity'),
         ('{"order":1}', 'error:Json.path: wrong case at order.quantity'),
         ('[]', 'error:Json.path: wrong case at order'),
         ('{"order":null}', 'error:Json.path: wrong case at order.quantity'),
         ('{"order":{"quantity":null}}', 'ok:null'),
         ('{"order":{"quantity":7}}', 'ok:7')]
quantities = ['{}', '[]', 'null', '{', '{"quantity":null}', '{"quantity":"7"}',
              '{"quantity":true}', '{"quantity":1.0}', '{"quantity":-1}',
              '{"quantity":0}', '{"quantity":1}', '{"quantity":100}', '{"quantity":101}']
orders = [('{"name":"bolt","item":{"quantity":2},"note":"ok"}', 'ok:bolt:2:"ok"'),
          ('{"name":"bolt","item":{},"note":"ok"}', 'error:Json.decode: missing field item.quantity'),
          ('{"name":"bolt","item":{"quantity":"2"},"note":"ok"}', 'error:Json.decode: wrong case at item.quantity'),
          ('{"name":"bolt","item":{"quantity":2},"note":null}', 'ok:bolt:2:null'),
          ('{"name":"bolt","item":null,"note":"ok"}', 'error:Json.decode: wrong case at item.quantity')]
lists = [('{"names":["a","b"],"lines":[{"qty":2},{"qty":3}],"flags":[true,false],"ratios":[1.5,2.5],"counts":[4,5]}', 'ok:a,b:2,3:true,false:4,5:pair'),
         ('{"names":[],"lines":[],"flags":[],"ratios":[],"counts":[]}', 'ok:::::empty'),
         ('{"lines":[],"flags":[],"ratios":[],"counts":[]}', 'error:Json.decode: missing field names'),
         ('{"names":["a",1],"lines":[],"flags":[],"ratios":[],"counts":[]}', 'error:Json.decode: wrong case at names.1'),
         ('{"names":[null],"lines":[],"flags":[],"ratios":[],"counts":[]}', 'error:Json.decode: wrong case at names.0'),
         ('{"names":[],"lines":[{"qty":2},{"qty":true}],"flags":[],"ratios":[],"counts":[]}', 'error:Json.decode: wrong case at lines.1.qty'),
         ('{"names":[],"flags":[],"ratios":[],"counts":[]}', 'error:Json.decode: missing field lines')]
options = [('{"title":"a"}', 'ok:a:none:none:none:none:none'),
           ('{"title":"a","label":null,"count":null,"flag":null,"ratio":null,"note":null}', 'ok:a:none:none:none:none:none'),
           ('{"title":"a","label":"x","count":0,"flag":false,"ratio":1.5,"note":{"text":"hi"}}', 'ok:a:x:0:false:1.5:hi'),
           ('{"title":"a","label":1}', 'error:Json.decode: wrong case at label'),
           ('{"title":"a","note":{"text":true}}', 'error:Json.decode: wrong case at note.text'),
           ('{"label":"x"}', 'error:Json.decode: missing field title'),
           ('{"title":null}', 'error:Json.decode: wrong case at title'),
           ('{"title":"a","note":{}}', 'error:Json.decode: missing field note.text'),
           ('{"title":"a","ratio":1}', 'error:Json.decode: wrong case at ratio'),
           ('{"title":"a","count":7}', 'ok:a:none:7:none:none:none'),
           ('{"title":"a","note":1}', 'error:Json.decode: wrong case at note.text')]
enums = [('{"title":"a","tone":"Calm","mood":{"tone":"Loud"}}', 'ok:a:Calm:Loud'),
         ('{"title":"a","tone":"Loud","mood":{"tone":"Calm"}}', 'ok:a:Loud:Calm'),
         ('{"title":"a","tone":"Gone","mood":{"tone":"Calm"}}', 'error:Json.decode: unknown case at tone: Gone'),
         ('{"title":"a","tone":"Calm","mood":{"tone":"Gone"}}', 'error:Json.decode: unknown case at mood.tone: Gone'),
         ('{"title":"a","tone":1,"mood":{"tone":"Calm"}}', 'error:Json.decode: wrong case at tone: 1'),
         ('{"title":"a","tone":"Calm","mood":{"tone":1}}', 'error:Json.decode: wrong case at mood.tone: 1'),
         ('{"title":"a","tone":null,"mood":{"tone":"Calm"}}', 'error:Json.decode: wrong case at tone: null'),
         ('{"title":"a","tone":true,"mood":{"tone":"Calm"}}', 'error:Json.decode: wrong case at tone: true'),
         ('{"title":"a","mood":{"tone":"Calm"}}', 'error:Json.decode: missing field tone'),
         ('{"title":"a","tone":"Calm","mood":{}}', 'error:Json.decode: missing field mood.tone')]
bands = [('{"tones":["Calm","Loud"],"pick":"Calm"}', 'ok:Calm,Loud:Calm'),
         ('{"tones":["Calm","Gone"],"pick":"Calm"}', 'error:Json.decode: unknown case at tones.1: Gone'),
         ('{"tones":["Calm"]}', 'ok:Calm:none'),
         ('{"tones":["Loud"],"pick":null}', 'ok:Loud:none'),
         ('{"tones":[],"pick":"Loud"}', 'ok:empty:Loud'),
         ('{"tones":["Calm"],"pick":"Gone"}', 'error:Json.decode: unknown case at pick: Gone'),
         ('{"tones":[1],"pick":"Calm"}', 'error:Json.decode: wrong case at tones.0: 1'),
         ('{"tones":["Calm"],"pick":1}', 'error:Json.decode: wrong case at pick: 1'),
         ('{"tones":[null],"pick":null}', 'error:Json.decode: wrong case at tones.0: null'),
         ('{"pick":"Calm"}', 'error:Json.decode: missing field tones')]
grids = [('{"items":[["a","b"],["c"]],"rows":[[{"qty":2}],[{"qty":3},{"qty":4}]],"tones":[["Calm"],["Loud","Calm"]]}', 'ok:a,b|c:2|3,4:Calm|Loud,Calm'),
         ('{"items":[[],[]],"rows":[],"tones":[[]]}', 'ok:empty|empty:empty:empty'),
         ('{"items":[["a"],[1]],"rows":[],"tones":[]}', 'error:Json.decode: wrong case at items.1.0'),
         ('{"items":[[null]],"rows":[],"tones":[]}', 'error:Json.decode: wrong case at items.0.0'),
         ('{"items":[["a"],"b"],"rows":[],"tones":[]}', 'error:Json.decode: wrong case at items.1'),
         ('{"items":[],"rows":[[{"qty":1}],[{"qty":true}]],"tones":[]}', 'error:Json.decode: wrong case at rows.1.0.qty'),
         ('{"items":[],"rows":[[{}]],"tones":[]}', 'error:Json.decode: missing field rows.0.0.qty'),
         ('{"items":[],"rows":[],"tones":[["Calm"],["Gone"]]}', 'error:Json.decode: unknown case at tones.1.0: Gone'),
         ('{"items":[],"rows":[],"tones":[[1]]}', 'error:Json.decode: wrong case at tones.0.0: 1'),
         ('{"items":[],"rows":[],"tones":[[null]]}', 'error:Json.decode: wrong case at tones.0.0: null'),
         ('{"rows":[],"tones":[]}', 'error:Json.decode: missing field items')]
payloads = [('{"reply":{"Count":7}}', 'ok:count:7'),
            ('{"reply":{"Count":101}}', 'ok:count:101'),
            ('{"reply":{"Label":"a"}}', 'ok:label:a'),
            ('{"reply":{"Item":{"quantity":2}}}', 'ok:item:2'),
            ('{"reply":{"Tone":"Loud"}}', 'ok:tone:Loud'),
            ('{"reply":{"Box":{"Count":4}}}', 'ok:box:4'),
            ('{"reply":{"Keep":null}}', 'ok:keep:null'),
            ('{"reply":{"Keep":{"a":1}}}', 'ok:keep:{"a":1}'),
            ('{"reply":{"Flag":false}}', 'ok:flag:false'),
            ('{"reply":{"Ratio":1.5}}', 'ok:ratio:1.5'),
            ('{"reply":{"Count":7,"extra":true}}', 'ok:count:7'),
            ('{"reply":{"Count":"x"}}', 'error:Json.decode: wrong case at reply.n'),
            ('{"reply":1}', 'error:Json.decode: wrong case at reply: 1'),
            ('{"reply":"Count"}', 'error:Json.decode: wrong case at reply: "Count"'),
            ('{"reply":null}', 'error:Json.decode: wrong case at reply: null'),
            ('{"reply":{"Gone":1}}', 'error:Json.decode: unknown case at reply: {"Gone":1}'),
            ('{"reply":{}}', 'error:Json.decode: unknown case at reply: {}'),
            ('{"reply":{"Count":1,"Label":"a"}}', 'error:Json.decode: wrong case at reply: {"Count":1,"Label":"a"}'),
            ('{}', 'error:Json.decode: missing field reply'),
            ('{"reply":{"Item":{}}}', 'error:Json.decode: missing field reply.piece.quantity'),
            ('{"reply":{"Item":{"quantity":true}}}', 'error:Json.decode: wrong case at reply.piece.quantity'),
            ('{"reply":{"Tone":"Gone"}}', 'error:Json.decode: unknown case at reply.tone: Gone'),
            ('{"reply":{"Tone":1}}', 'error:Json.decode: wrong case at reply.tone: 1'),
            ('{"reply":{"Box":1}}', 'error:Json.decode: wrong case at reply.inner: 1'),
            ('{"reply":{"Box":{"Count":"x"}}}', 'error:Json.decode: wrong case at reply.inner.n'),
            ('{"reply":{"Ratio":1}}', 'error:Json.decode: wrong case at reply.ratio')]
ranges = [('{"name":"bolt","item":{"quantity":2},"note":null}', 'true'),
          ('{"name":"bolt","item":{"quantity":101},"note":null}', 'false')]
reply_ranges = [('{"reply":{"Count":7}}', 'true'),
                ('{"reply":{"Count":1}}', 'true'),
                ('{"reply":{"Count":100}}', 'true'),
                ('{"reply":{"Count":101}}', 'false'),
                ('{"reply":{"Count":0}}', 'false')]

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

def nested(text: String): String =
  Json.parse(text) match {
    case Result.Ok(json) => Json.path(json, ["order", "quantity"]) match {
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
    _ <- IO.foreachDiscard(PATHS, text => IO.println(nested(text)))
    _ <- IO.foreachDiscard(QUANTITIES, text => IO.println(Str.fromBool(Input.accepted(Input.decodeQuantity(text)))))
    _ <- IO.foreachDiscard(ORDERS, text => IO.println(Input.orderText(text)))
    _ <- IO.foreachDiscard(LISTS, text => IO.println(Input.listText(text)))
    _ <- IO.foreachDiscard(OPTIONS, text => IO.println(Input.ticketText(text)))
    _ <- IO.foreachDiscard(ENUMS, text => IO.println(Input.jobText(text)))
    _ <- IO.foreachDiscard(BANDS, text => IO.println(Input.choirText(text)))
    _ <- IO.foreachDiscard(GRIDS, text => IO.println(Input.gridText(text)))
    _ <- IO.foreachDiscard(PAYLOADS, text => IO.println(Input.packetText(text)))
    _ <- IO.foreachDiscard(RANGES, text => IO.println(Str.fromBool(Input.orderInRange(text))))
    _ <- IO.foreachDiscard(REPLIES, text => IO.println(Str.fromBool(Input.replyInRange(text))))
  } yield ()
'''.replace('INTEGERS', array(integers)).replace('FIELDS', array([text for text, _ in fields])).replace('PATHS', array([text for text, _ in paths])).replace('QUANTITIES', array(quantities)).replace('ORDERS', array([text for text, _ in orders])).replace('LISTS', array([text for text, _ in lists])).replace('OPTIONS', array([text for text, _ in options])).replace('ENUMS', array([text for text, _ in enums])).replace('BANDS', array([text for text, _ in bands])).replace('GRIDS', array([text for text, _ in grids])).replace('PAYLOADS', array([text for text, _ in payloads])).replace('REPLIES', array([text for text, _ in reply_ranges])).replace('RANGES', array([text for text, _ in ranges]))
    (root / 'src/Main.scuzz').write_text(source)
    expected = [parsed(text) for text in integers] + [value for _, value in fields] + [value for _, value in paths]
    expected += ['false'] * 10 + ['true', 'true', 'false']
    expected += [value for _, value in orders] + [value for _, value in lists] + [value for _, value in options] + [value for _, value in enums] + [value for _, value in bands] + [value for _, value in grids] + [value for _, value in payloads] + [value for _, value in ranges] + [value for _, value in reply_ranges]
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
    for body, needle in [
        ('record Row(xs: Map[String, Int])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read xs'),
        ('record Row(xs: List[Json])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read xs'),
        ('record Row(xs: List[List[Option[String]]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read xs'),
        ('record Node(name: String, kids: List[Node])\ndef decodeNode(json: Json): Result[String, Node] =\n  Json.decode(json)\n', 'Json.decode: cannot read kids'),
        ('record Row(label: Option[List[String]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read label'),
        ('record Row(label: Option[Json])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read label'),
        ('record Row(label: Option[Option[String]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read label'),
        ('record Row(labels: List[Option[String]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read labels'),
        ('record Node(name: String, child: Option[Node])\ndef decodeNode(json: Json): Result[String, Node] =\n  Json.decode(json)\n', 'Json.decode: cannot read child'),
        ('enum Mode:\n  case On(n: Int)\n  case Off\nrecord Row(mode: Mode)\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read mode'),
        ('enum Mode:\n  case On(n: Int)\n  case Off\nrecord Row(modes: List[Mode])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read modes'),
        ('enum Mode:\n  case On(n: Int)\n  case Off\nrecord Row(mode: Option[Mode])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read mode'),
        ('enum Wrap[A]:\n  case Empty\nrecord Row(items: List[Wrap[String]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read items'),
        ('enum Wrap[A]:\n  case Empty\nrecord Row(item: Option[Wrap[String]])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item'),
        ('enum Wrap[A]:\n  case Empty\nrecord Row(item: Wrap[String])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item'),
        ('enum Pair:\n  case Both(n: Int, text: String)\nrecord Row(item: Pair)\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item'),
        ('enum Bag:\n  case Items(xs: List[String])\nrecord Row(item: Bag)\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item.xs'),
        ('enum Maybe:\n  case Num(n: Option[Int])\nrecord Row(item: Maybe)\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item.n'),
        ('enum Reply:\n  case Count(n: Int)\nrecord Row(items: List[Reply])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read items'),
        ('enum Reply:\n  case Count(n: Int)\nrecord Row(item: Option[Reply])\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item'),
        ('enum Loop:\n  case Box(inner: Loop)\nrecord Row(item: Loop)\ndef decodeRow(json: Json): Result[String, Row] =\n  Json.decode(json)\n', 'Json.decode: cannot read item.inner'),
        ('def badPos(json: Json): Json =\n  Json.decode(json)\n', 'Json.decode needs a record result'),
    ]:
        (root / 'src/BadDecode.scuzz').write_text(body)
        command('fmt', root)
        diagnostic = '\n'.join(command('check', root, ok=False))
        assert needle in diagnostic, diagnostic
        (root / 'src/BadDecode.scuzz').unlink()
print('input validation and native parity ok')
PY_INPUT

# Validated paths retain live checks and constructor access.
python3 - <<'PY_PATH'
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
proposal = Path('examples/editor/generation/src/Proposal.scuzz').read_text()
names = ('relativePath', 'pathChars')
starts = list(re.finditer(r'^(?:private )?def ([A-Za-z_][A-Za-z0-9_]*)\(', proposal, re.M))
bodies = {match[1]: proposal[match.start():starts[i + 1].start() if i + 1 < len(starts) else len(proposal)]
          for i, match in enumerate(starts)}
declaration = re.search(r'^record RelativePath private\(value: String\)\n', proposal, re.M)
assert declaration is not None and all(name in bodies for name in names)
source = declaration[0] + '\n' + ''.join(bodies[name] for name in names)
inputs = ['', '/', '/tmp/file', '../file', 'src/../Main.scuzz', './src/Main.scuzz',
          'src//Main.scuzz', 'src/Main.scuzz/', 'C:/file', 'src\\Main.scuzz',
          'src/Main.scuzz\0other', 'src/\nMain.scuzz', 'src/Main.scuzz',
          '.scuzz/ide/journal.json', 'build/cards/one.toml', 'folder with space/é']
rng = random.Random(42)
inputs += [''.join(rng.choice('ab./\\:\0\n é') for _ in range(rng.randrange(32))) for _ in range(64)]
def valid(path):
    return bool(path) and all(ord(c) >= 32 and ord(c) != 127 and c not in '\\:' for c in path) and all(part not in ('', '.', '..') for part in path.split('/'))
expected = ['true' if valid(path) else 'false' for path in inputs]
with tempfile.TemporaryDirectory(prefix='scuzz-validated-path-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="validated-path"\n')
    (root / 'src/Proposal.scuzz').write_text(source)
    values = '[' + ', '.join(f'Hex.decode("{path.encode().hex()}")' for path in inputs) + ']'
    (root / 'src/Main.scuzz').write_text('''def accepted(text: String): Bool =
  Proposal.relativePath(text) match {
    case Result.Ok(path) => path.value == text
    case Result.Err(_) => false
  }
@main def main: IO[Unit] =
  IO.foreachDiscard(VALUES, text => IO.println(Str.fromBool(accepted(text))))
'''.replace('VALUES', values))
    run = subprocess.run([str(cli), 'fmt', str(root)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    for engine in ('eval', 'run'):
        run = subprocess.run([str(cli), engine, str(root)], capture_output=True, text=True, timeout=180)
        lines = run.stdout.splitlines()
        if engine == 'run' and lines[0] == 'ok':
            lines = lines[1:]
        assert run.returncode == 0 and lines == expected, (engine, run.stdout, run.stderr)
    for bad in ('def bad(): Proposal.RelativePath = Proposal.RelativePath("../file")',
                'def bad(path: Proposal.RelativePath): Proposal.RelativePath = path.copy(value = "../file")',
                '@main def main: IO[Unit] = Proposal.relativePath("../file") match { case Result.Ok(_) => IO.pure(()) case Result.Err(error) => IO.fail(error) }'):
        (root / 'src/Main.scuzz').write_text(bad + '\n')
        run = subprocess.run([str(cli), 'fmt', str(root)], capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        command = 'run' if 'IO[Unit]' in bad else 'check'
        run = subprocess.run([str(cli), command, str(root)], capture_output=True, text=True, timeout=180)
        assert run.returncode != 0, (bad, run.stdout, run.stderr)
        assert ('invalid relative path' if command == 'run' else 'private record') in run.stdout + run.stderr, (bad, run.stdout, run.stderr)
print('Validated paths: both engines match the independent input model; construction and copy cannot bypass validation')
PY_PATH

# Integer operations use an independent model on both engines.
python3 - <<'PY_ARITHMETIC'
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile

cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
minimum, maximum = -(1 << 63), (1 << 63) - 1
edges = [minimum, minimum + 1, -maximum, -257, -8, -3, -2, -1, 0,
         1, 2, 3, 8, 257, maximum - 1, maximum]
pairs = [(a, b) for a in edges for b in edges]
rng = random.Random(42)
pairs += [(rng.randint(minimum, maximum), rng.randint(minimum, maximum)) for _ in range(128)]

def model(a, b):
    if b == 0:
        return 'error:divisor must be nonzero'
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    r = a - q * b
    wrapped = (q + (1 << 63)) % (1 << 64) - (1 << 63)
    return f'{wrapped},{r}'

with tempfile.TemporaryDirectory(prefix='scuzz-arithmetic-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    (root / 'scuzz.toml').write_text('[package]\nname="arithmetic"\n')
    shutil.copy('examples/kernel/src/Input.scuzz', root / 'src/Input.scuzz')
    source = '''def result(pair: (Int, Int)): String =
  Input.division(pair._1, pair._2) match {
    case Result.Ok((q, r)) => s"$q,$r"
    case Result.Err(error) => s"error:$error"
  }

@main def main: IO[Unit] =
  IO.foreachDiscard(PAIRS, pair => IO.println(result(pair)))
'''.replace('PAIRS', '[' + ', '.join(f'({a}, {b})' for a, b in pairs) + ']')
    main = root / 'src/Main.scuzz'
    main.write_text(source)
    def command(action, env=None):
        return subprocess.run([str(cli), action, str(root)], env=env,
                              capture_output=True, text=True, timeout=180)
    formatted = command('fmt')
    assert formatted.returncode == 0, formatted.stderr
    before = {p.name: p.read_text() for p in (root / 'src').glob('*.scuzz')}
    assert command('fmt').returncode == 0
    assert before == {p.name: p.read_text() for p in (root / 'src').glob('*.scuzz')}
    assert command('check').returncode == 0
    expected = [model(a, b) for a, b in pairs]
    for engine in ('run', 'eval'):
        run = command(engine)
        assert run.returncode == 0, (engine, run.stdout[-4000:], run.stderr[-4000:])
        output = run.stdout.strip().splitlines()
        if output and output[0] == 'ok':
            output = output[1:]
        assert output == expected, (engine, output[-16:], expected[-16:])
    for operation in ('quotient', 'remainder'):
        for a in (minimum, 0, maximum):
            main.write_text(f'@main def main: IO[Unit] = IO.println(Str.fromInt(Input.{operation}({a}, 0)))\n')
            assert command('fmt').returncode == 0
            for engine in ('run', 'eval'):
                for verify in (False, True):
                    env = dict(os.environ)
                    env.pop('SCUZZ_TESTRT', None)
                    env.pop('SCUZZ_EV_TESTRT', None)
                    if not verify:
                        run = command(engine, env)
                        assert run.returncode != 0, (engine, verify, operation, a)
                        error = run.stdout + run.stderr
                    elif engine == 'run':
                        env['SCUZZ_TESTRT'] = '1'
                        run = subprocess.run([str(root / 'build/arithmetic')], env=env,
                                             capture_output=True, text=True, timeout=30)
                        assert run.returncode != 0, (engine, verify, operation, a)
                        error = run.stdout + run.stderr
                    else:
                        probe = root / 'probe'
                        probe.mkdir(exist_ok=True)
                        (probe / 'files.txt').write_text('Input\nMain\n')
                        (probe / '0.scuzz').write_text((root / 'src/Input.scuzz').read_text())
                        (probe / '1.scuzz').write_text(main.read_text())
                        log = probe / 'probe.log'
                        request = probe / 'request.txt'
                        request.write_text(f'TESTRT=1\nPROBE_LOG={log}\n')
                        env['SCUZZ_EV_REQUEST'] = str(request)
                        run = subprocess.run([str(cli), 'eval', '--probe', str(probe)],
                                             input='probe\n', env=env, capture_output=True,
                                             text=True, timeout=60)
                        assert run.returncode == 0 and run.stdout.splitlines() == ['ready', '1'], (run.stdout, run.stderr)
                        error = log.read_text()
                    assert 'division by zero' in error and 'Input.scuzz:' in error, error[-4000:]
                    assert 'Floating point exception' not in error, error[-4000:]
print(f'Integer boundaries: {len(pairs)} model cases and 24 zero failures across both engines')
PY_ARITHMETIC

python3 - <<'PY_JSON_LARGE'
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tempfile

repo = Path.cwd()
cli = Path(os.environ.get('SCUZZ', 'examples/cli/build/cli')).resolve()
with tempfile.TemporaryDirectory(prefix='scuzz-json-large-') as directory:
    root = Path(directory)
    (root / 'src').mkdir()
    dependency = os.path.relpath(repo / 'examples/compiler', root)
    (root / 'scuzz.toml').write_text('[package]\nname="json-large"\n[dependencies]\ncompiler={path="' + dependency + '"}\n')
    (root / 'src/Main.scuzz').write_text('@main def main: IO[Unit] =\n  Fs.read("input").flatMap(s => IO.println(Check.jsonStr(s)))\n')
    unit = 'prefix "\\\t\n\r' + ''.join(chr(i) for i in range(32)) + 'éλ🙂 suffix\n'
    text = unit * 4096
    (root / 'input').write_bytes(text.encode())
    # The wire format uses Unicode escapes for backspace and form feed.
    expected = (json.dumps(text, ensure_ascii=False).replace('\\b', '\\u0008').replace('\\f', '\\u000c') + '\n').encode()
    env = dict(os.environ, SCUZZ_HOME=str(repo))
    build = subprocess.run([str(cli), 'run', str(root)], cwd=root, env=env,
                           capture_output=True, timeout=180)
    assert build.returncode == 0, build.stderr[-2000:]
    assert build.stdout.endswith(expected), (len(build.stdout), len(expected))
    def limits():
        if sys.platform.startswith('linux'):
            resource.setrlimit(resource.RLIMIT_AS, (512 * 1024 * 1024,) * 2)
        resource.setrlimit(resource.RLIMIT_STACK, (8 * 1024 * 1024,) * 2)
    result = subprocess.run([str(root / 'build/json-large')], cwd=root, env=env,
                            capture_output=True, timeout=20, preexec_fn=limits)
    assert result.returncode == 0, result.stderr[-2000:]
    assert result.stdout == expected, (len(result.stdout), len(expected))
    assert json.loads(result.stdout) == text
    print(f'Native JSON: exact {len(text.encode())} input bytes and {len(expected)} output bytes; control bytes and UTF-8; 8 MiB stack; Linux 512 MiB memory; 20-second deadline')
PY_JSON_LARGE

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

def resultReceiver(text: String): Int =
  for {
    result = Str.parseInt(text)
    kept = result.require("input", item => true)
  } yield consume(kept)

def batchText(texts: List[String]): String =
  Input.batch(texts) match {
    case Result.Ok(values) => Str.concat("ok:", List.join(List.map(values, value => Str.fromInt(value.value)), ","))
    case Result.Err(error) => Str.concat("error:", error)
  }

def batchAlias(value: List[Read]): List[Read] =
  for { all @ _ = value } yield all

def optionAlias(value: Option[Read]): Option[Read] =
  for { all @ _ = value } yield all

record Batch[A](item: A, count: Int)

enum Packet[A]:
  case Ready(value: A)
  case Idle

enum ResultTree:
  case Leaf(value: Read)
  case Branch(left: ResultTree, right: ResultTree)
  case Empty

def forwardedBatch(text: String): Batch[Read] =
  for {
    batch = Batch(Str.parseInt(text), 3)
  } yield batch

def recordHandled(text: String): Int =
  for {
    batch = forwardedBatch(text)
  } yield consume(batch.item) + batch.count

def packetHandled(flag: Bool, text: String): Int =
  (if (flag) Packet.Ready(Str.parseInt(text)) else Packet.Idle) match {
    case Packet.Ready(Result.Ok(n)) => n
    case Packet.Ready(Result.Err(_)) => -1
    case _ => 0
  }

def treeHandled(tree: ResultTree): Int =
  tree match {
    case ResultTree.Leaf(Result.Ok(n)) => n
    case ResultTree.Leaf(Result.Err(_)) => -1
    case ResultTree.Branch(left, right) => treeHandled(left) + treeHandled(right)
    case _ => 0
  }

def pairHandled(text: String): Int =
  for {
    pair = (Str.parseInt(text), 2)
    checked = Property.check("input", consume(pair._1) != 0, pair._1)
  } yield consume(checked) + pair._2

def validatedEnv(name: String): IO[String] =
  for {
    value <- Sys.getenv(name)
  } yield if (value == "") "unset" else value.require("env", s => s != "")

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
    _ <- Input.totalBindings(true, 7).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.totalBindings(false, -1).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.aliasEffect((IO.pure(7): IO[String, Int])).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- Input.aliasEffect((IO.fail("alias failure"): IO[String, Int])).handleErrorWith(error => IO.pure(if (error == "alias failure") -1 else -2)).flatMap(n => IO.println(Str.fromInt(n)))
    _ <- IO.println(batchText(["1", "100"]))
    _ <- IO.println(batchText(["7", "bad"]))
    _ <- IO.println(batchText(["0"]))
    _ <- IO.println(batchText([]))
    _ <- IO.println(Str.fromInt(resultReceiver("7")))
    _ <- IO.println(Str.fromInt(resultReceiver("bad")))
    _ <- IO.println(Str.fromInt(pairHandled("7")))
    _ <- IO.println(Str.fromInt(pairHandled("bad")))
    _ <- IO.println(Str.fromInt(recordHandled("7")))
    _ <- IO.println(Str.fromInt(recordHandled("bad")))
    _ <- IO.println(Str.fromInt(packetHandled(true, "7")))
    _ <- IO.println(Str.fromInt(packetHandled(true, "bad")))
    _ <- IO.println(Str.fromInt(packetHandled(false, "7")))
    _ <- IO.println(Str.fromInt(treeHandled(ResultTree.Branch(ResultTree.Leaf(Str.parseInt("5")), ResultTree.Leaf(Str.parseInt("bad"))))))
    _ <- validatedEnv("SCUZZ_UNSET_GUARD_TEST").flatMap(s => IO.println(s))
  } yield ()
'''
    (root / 'src/Main.scuzz').write_text(source)
    command(['fmt', root])
    for engine in ['run', 'eval']:
        output = command([engine, root]).stdout.strip().splitlines()
        if output and output[0] == 'ok':
            output = output[1:]
        assert output == ['7', '-1', 'true', 'false', '7', '-1', '7', '-1', '7', '-1', '7', '-1',
                          'ok:1,100', 'error:Str.parseInt: invalid integer',
                          'error:quantity must be 1 through 100', 'ok:', '7', '-1', '9', '1',
                          '10', '2', '7', '-1', '0', '4', 'unset'], (engine, output)
    bad = [('_ = List.map(["7", "bad"], text => Str.parseInt(text))', '()', 'Result list is discarded'),
           ('ignored = [Str.parseInt("bad")]', '()', 'Result list is discarded'),
           ('_ignored = [Str.parseInt("bad")]', '()', 'Result list is discarded'),
           ('ignored = [[Str.parseInt("bad")]]', '()', 'Result list is discarded'),
           ('ignored = [Option.Some(Str.parseInt("bad"))]', '()', 'Result list is discarded'),
           ('ignored = [(Str.parseInt("bad"), 7)]', '()', 'Result list is discarded'),
           ('ignored <- IO.pure([Str.parseInt("bad")])', '()', 'Result list is discarded'),
           ('ignored = [Str.parseInt("bad")]\n    use = (ignored: Int) => ignored', '()', 'Result list is discarded'),
           ('ignored = [Str.parseInt("bad")]', 'for { ignored = 1 } yield ()', 'Result list is discarded'),
           ('ignored = [Str.parseInt("bad")]\n    copy = ignored', '()', 'Result list is discarded'),
           ('_ = ([Str.parseInt("bad")]: List[Read])', '()', 'Result list is discarded'),
           ('_ = ([Str.parseInt("bad")]: List[Result])', '()', 'Result list is discarded'),
           ('all @ _ = [Str.parseInt("bad")]', '()', 'Result list is discarded'),
           ('ignored = Map.set(Map.empty(), "k", Str.parseInt("bad"))', '()', 'Result map is discarded'),
           ('m = Map.set(Map.empty(), "k", Str.parseInt("bad"))\n    _ = Map.size(m)', '()', 'Result map is discarded'),
           ('m = Map.set(Map.empty(), Str.parseInt("bad"), 1)\n    _ = Map.values(m)', '()', 'Result map is discarded'),
           ('m = Map.set(Map.empty(), "k", Str.parseInt("bad"))\n    _ = Map.exists(m, _ => true)', '()', 'Result map is discarded'),
           ('cell <- Ref.of(Str.parseInt("bad"))', '()', 'Result reference is discarded'),
           ('result = Str.parseInt("bad")\n    cell <- Ref.of(Result.Ok(0))\n    _ <- Ref.set(cell, result)', '()', 'Result value is discarded'),
           ('result = Str.parseInt("bad")\n    cell <- Ref.of(Result.Ok(0))\n    _ <- Ref.update(cell, _ => result)', '()', 'Result value is discarded'),
           ('ignored = Set.add(Set.empty(), Str.parseInt("bad"))', '()', 'Result set is discarded'),
           ('s = Set.add(Set.empty(), Str.parseInt("bad"))\n    _ = Set.size(s)', '()', 'Result set is discarded'),
           ('s = Set.add(Set.empty(), Str.parseInt("bad"))\n    _ = Set.contains(s, Str.parseInt("1"))', '()', 'Result set is discarded'),
           ('s = Set.add(Set.empty(), Str.parseInt("bad"))\n    _ = Set.exists(s, _ => true)', '()', 'Result set is discarded'),
           ('s = Set.add(Set.empty(), Str.parseInt("bad"))\n    _ = s.isEmpty()', '()', 'Result set is discarded'),
           ('ignored = Stream.emit(Str.parseInt("bad"))', '()', 'Result stream is discarded'),
           ('s = Stream.emit(Str.parseInt("bad"))\n    _ <- Stream.drain(s)', '()', 'Result stream is discarded'),
           ('s = Stream.emit(Str.parseInt("bad"))\n    _ <- Stream.count(s)', '()', 'Result stream is discarded'),
           ('s = Stream.emit(Str.parseInt("bad"))\n    _ <- Stream.exists(s, _ => true)', '()', 'Result stream is discarded'),
           ('s = Stream.emit(Str.parseInt("bad"))\n    _ <- s.forall(_ => true)', '()', 'Result stream is discarded'),
           ('q <- Queue.unbounded(): IO[Queue[Result[String, Int]]]', '()', 'Result queue is discarded'),
           ('q <- Queue.unbounded(): IO[Queue[Result[String, Int]]]\n    _ <- Queue.offer(q, Str.parseInt("1"))', '()', 'Result queue is discarded'),
           ('result = Str.parseInt("bad")\n    q <- Queue.unbounded(): IO[Queue[Result[String, Int]]]\n    _ <- Queue.offer(q, result)', '()', 'Result value is discarded'),
           ('ignored = Signal.make(Str.parseInt("bad"))', '()', 'Result signal is discarded'),
           ('s = Signal.make(Str.parseInt("bad"))\n    _ = Signal.set(s, Str.parseInt("1"))', '()', 'Result signal is discarded'),
           ('s = Signal.make(Str.parseInt("bad"))\n    _ = s.set(Str.parseInt("1"))', '()', 'Result signal is discarded'),
           ('result = Str.parseInt("bad")\n    s = Signal.make(Str.parseInt("0"))\n    _ = Signal.set(s, result)', '()', 'Result value is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    _ <- Deferred.complete(d, Str.parseInt("1"))', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    _ <- d.complete(Str.parseInt("1"))', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    _ <- Deferred.fail(d, "no")', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    _ <- d.fail("no")', '()', 'Result deferred is discarded'),
           ('result = Str.parseInt("bad")\n    d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    _ <- Deferred.complete(d, result)', '()', 'Result value is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]\n    value <- Deferred.get(d)', '()', 'Result value is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[List[Read]]]', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Option[Read]]]', '()', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]', 'd match { case _ => () }', 'Result deferred is discarded'),
           ('d <- Deferred.empty(): IO[Deferred[Result[String, Int]]]', 'Property.check("n", true, ())', 'Result deferred is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))', '()', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))\n    _ <- Fiber.interrupt(f)', '()', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))\n    _ <- f.interrupt()', '()', 'Result fiber is discarded'),
           ('result = Str.parseInt("bad")\n    f <- Fiber.fork(IO.pure(result))\n    _ <- Fiber.interrupt(f)', '()', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))\n    value <- Fiber.join(f)', '()', 'Result value is discarded'),
           ('f <- Fiber.fork(IO.pure([Str.parseInt("bad")]))', '()', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Option.Some(Str.parseInt("bad"))))', '()', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))', 'f match { case _ => () }', 'Result fiber is discarded'),
           ('f <- Fiber.fork(IO.pure(Str.parseInt("bad")))', '(()).require("n", n => true)', 'Result fiber is discarded'),
           ('_ = Option.Some(Str.parseInt("bad"))', '()', 'Result option is discarded'),
           ('ignored = Option.Some(Str.parseInt("bad"))', '()', 'Result option is discarded'),
           ('ignored <- IO.pure(Option.Some(Str.parseInt("bad")))', '()', 'Result option is discarded'),
           ('results = [Str.parseInt("bad")]\n    _ = List.head(results)', '()', 'Result option is discarded'),
           ('_ = (Option.Some(Str.parseInt("bad")): Option[Read])', '()', 'Result option is discarded'),
           ('all @ _ = Option.Some(Str.parseInt("bad"))', '()', 'Result option is discarded'),
           ('_ = (Str.parseInt("bad"), 7)', '()', 'discarded in live code'),
           ('pair = (Str.parseInt("bad"), 7)', '()', 'discarded in live code'),
           ('pair = ((Str.parseInt("bad"), 7): (Read, Int))\n    _ = pair._2', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    pair = (result, 7)\n    _ = pair._2', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = (result, 7)._2', '()', 'discarded in live code'),
           ('pair <- IO.both(IO.attempt(IO.pure(7)), IO.pure(1))\n    _ = pair._2', '()', 'discarded in live code'),
           ('_ = Input.Read(Str.parseInt("bad"), 7)', '()', 'discarded in live code'),
           ('rec = Input.Read(Str.parseInt("bad"), 7)', '()', 'discarded in live code'),
           ('rec <- IO.pure(Input.Read(Str.parseInt("bad"), 7))\n    _ = rec.count', '()', 'discarded in live code'),
           ('rec = Input.Read(Str.parseInt("bad"), 7)\n    _ = rec.count', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = Input.Read(result, 7).count', '()', 'discarded in live code'),
           ('batch = Main.Batch([Str.parseInt("bad")], 2)\n    _ = batch.count', '()', 'Result list is discarded'),
           ('_ = Main.Packet.Ready(Str.parseInt("bad"))', '()', 'discarded in live code'),
           ('pkt = Main.Packet.Ready(Str.parseInt("bad"))', '()', 'discarded in live code'),
           ('tree = Main.ResultTree.Branch(Main.ResultTree.Leaf(Str.parseInt("bad")), Main.ResultTree.Empty)', '()', 'discarded in live code'),
           ('pkt = Main.Packet.Ready(Str.parseInt("bad"))', 'pkt match { case Main.Packet.Ready(_) => ()\ncase Main.Packet.Idle => () }', 'discarded in live code'),
           ('pkt = Main.Packet.Ready(Str.parseInt("bad"))', 'pkt match { case Main.Packet.Ready(res) => ()\ncase Main.Packet.Idle => () }', 'discarded in live code'),
           ('pkt = Main.Packet.Ready(Str.parseInt("bad"))', 'pkt match { case Main.Packet.Idle => ()\ncase _ => () }', 'discarded in live code'),
           ('rec = Input.Read(Str.parseInt("bad"), 7)', 'rec match { case Input.Read(count = _) => () }', 'discarded in live code'),
           ('rec = Input.Read(Str.parseInt("bad"), 7)', 'rec match { case Input.Read(res, _) => () }', 'discarded in live code'),
           ('_ = Str.parseInt("7")', '()', 'discarded'),
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
           ('(result, n) = (Str.parseInt("bad"), 7)\n    use = (result: Int) => result', '()', 'discarded'),
           ('Some(n) = None', '()', 'binding pattern does not cover every value'),
           ('Some(n) <- IO.pure(None)', '()', 'binding pattern does not cover every value'),
           ('n :: _ = List.empty()', '()', 'binding pattern does not cover every value'),
           ('[] = [1]', '()', 'binding pattern does not cover every value'),
           ('true = false', '()', 'binding pattern does not cover every value'),
           ('(true, n) = (false, 7)', '()', 'binding pattern does not cover every value'),
           ('0 = 1', '()', 'binding pattern does not cover every value'),
           ('"ok" = "bad"', '()', 'binding pattern does not cover every value'),
           ('result = Str.parseInt("bad")\n    _ = (7).require("input", n => consume(result) > 0)', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = (7).require(if (consume(result) > 0) "ok" else "error", n => true)', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = (7).require("input", n => (false).require("nested", b => consume(result) > 0))', '()', 'discarded in live code'),
           ('result <- IO.attempt(IO.pure(7))\n    _ = (7).require("input", n => consume(result) > 0)', '()', 'discarded in live code'),
           ('(result, _) = (Str.parseInt("bad"), 7)\n    _ = (7).require("input", n => consume(result) > 0)', '()', 'discarded in live code'),
           ('results = [Str.parseInt("bad")]\n    _ = (7).require("input", n => List.forall(results, item => consume(item) > 0))', '()', 'discarded in live code'),
           ('results <- IO.pure([Str.parseInt("bad")])\n    _ = (7).require("input", n => List.forall(results, item => consume(item) > 0))', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")', 'Property.check("input", consume(result) > 0, ())', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ <- Property.assert("input", consume(result) > 0)', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = Property.assume("input", consume(result) > 0)', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = Property.sometimes(if (consume(result) > 0) "ok" else "error")', '()', 'discarded in live code'),
           ('result = Str.parseInt("bad")\n    _ = Property.classify("input", consume(result) > 0)', '()', 'discarded in live code'),
           ('opt = Option.Some(Str.parseInt("bad"))', 'Property.check("input", opt match { case Some(v) => consume(v) > 0\ncase None => false }, ())', 'Result option is discarded in live code'),
           ('pair = (Str.parseInt("bad"), 7)\n    _ = Property.check("input", consume(pair._1) > 0, pair._2)', '()', 'discarded in live code'),
           ('trimmed = Str.trim("  ok  ")\n    _ = (7).require("input", n => trimmed != "")', '()', 'is used only in erased checks in live code'),
           ('trimmed = Str.trim("  ok  ")', 'Property.check("input", trimmed != "", ())', 'is used only in erased checks in live code'),
           ('trimmed = Str.trim("  ok  ")\n    _ = Property.assume("input", trimmed != "")', '()', 'is used only in erased checks in live code'),
           ('trimmed = Str.trim("  ok  ")\n    _ <- Property.assert("input", trimmed != "")', '()', 'is used only in erased checks in live code'),
           ('raw <- Fs.read("in.txt")\n    _ = Property.assume("input", raw != "")', '()', 'is used only in erased checks in live code'),
           ('raw <- Fs.read("in.txt")\n    _ <- Property.assert("input", raw != "")', '()', 'is used only in erased checks in live code'),
           ('raw <- Fs.read("in.txt")\n    _ = Property.assume("input", raw != "")\n    _ <- IO.println(raw)', '()', 'is guarded only by erased checks in live code'),
           ('raw <- Fs.read("in.txt")\n    _ <- Property.assert("input", raw != "")\n    _ <- IO.println(raw)', '()', 'is guarded only by erased checks in live code'),
           ('raw <- Fs.read("in.txt")\n    _ <- IO.println(Property.check("input", raw != "", raw))', '()', 'is guarded only by erased checks in live code'),
           ('secret <- Sys.getenv("KEY")\n    _ <- IO.println(secret.require(v => v != ""))', '()', 'is guarded only by erased checks in live code'),
           ('args <- Sys.args()\n    _ = Property.assume("args", List.nonEmpty(args))\n    _ <- IO.println(Str.fromInt(List.len(args)))', '()', 'is guarded only by erased checks in live code'),
           ('_ <- 1', '()', '<- needs IO'),
           ('_ <- Str.parseInt("7")', '()', '<- needs IO'),
           ('_ <- [IO.pure(7)]', '()', '<- needs IO')]
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
        minimum_line = 3 if bindings == 'true = false' else 4
        assert error['file'].endswith('Bad.scuzz') and error['line'] >= minimum_line and error['column'] > 0, (bindings, error)
        if bindings == 'true = false':
            assert error['line'] == 3, error
        for entry in ['build', 'eval']:
            result = command([entry, root], ok=False)
            assert message in result.stdout + result.stderr, (entry, result.stdout, result.stderr)
        (root / 'src/Bad.scuzz').unlink()
    effect_types = ['Int', 'String', 'Bool', 'Float', 'Unit', 'List[Int]',
                    'List[IO[Int]]', 'Map[String, Int]', 'Option[Int]',
                    'Result[String, Int]', '(Int, String)', 'Int => Int',
                    'Stream[Int]', 'Resource[Int]', 'Input.Quantity', 'A']
    for ty in effect_types:
        generic = '[A]' if ty == 'A' else ''
        broken = f'def broken{generic}(value: {ty}): IO[Unit] =\n  for {{\n    _ <- value\n  }} yield ()\n'
        (root / 'src/Bad.scuzz').write_text(broken)
        command(['fmt', root])
        result = command(['check', '--message-format=json', root], ok=False)
        error = next(row for row in json.loads(result.stdout) if row['severity'] == 'error')
        assert '<- needs IO' in error['message'], (ty, error)
        assert error['file'].endswith('Bad.scuzz') and error['line'] == 3 and error['column'] > 0, (ty, error)
        for entry in ['build', 'eval']:
            result = command([entry, root], ok=False)
            assert '<- needs IO' in result.stdout + result.stderr, (ty, entry, result.stdout, result.stderr)
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
grep -Fxq 'textModule:module-text' /tmp/kernel.out
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
python3 - <<'PY'
import re
from pathlib import Path
text = Path("examples/bytes/src/Main.scuzz").read_text()
found = re.search(r'"([0-9a-f]{80,})"', text)
if found is None:
    raise SystemExit("image hex missing")
raw = bytes.fromhex(found.group(1))
png = Path("examples/bytes/pixel.png").read_bytes()
if png != raw:
    raise SystemExit("pixel.png does not match image hex")
try:
    png.decode("utf-8")
except UnicodeDecodeError:
    pass
else:
    raise SystemExit("pixel.png is valid UTF-8")
if b"\x00" not in png or png[0] != 0x89:
    raise SystemExit("pixel.png is not the binary image")
print("byte file", len(png))
PY
"$SCUZZ" run examples/bytes | tee /tmp/bytes.out
grep -Fxq 'image:bin:ok:2:2:match' /tmp/bytes.out
"$SCUZZ" eval examples/bytes | tee /tmp/bytes-eval.out
diff <(grep -vx ok /tmp/bytes.out) /tmp/bytes-eval.out
"$SCUZZ" fuzz --iterations 0 examples/bytes | tee /tmp/bytes-fuzz.out
grep -Fxq 'image:bin:ok:2:2:match' /tmp/bytes-fuzz.out
"$SCUZZ" check examples/hello
"$SCUZZ" check examples/bytes
python3 - <<'PY'
from pathlib import Path
text = Path("examples/http-bytes/src/Main.scuzz").read_text()
if "fffe80" not in text or "Net.httpGetBytes" not in text:
    raise SystemExit("http byte proof missing")
raw = bytes.fromhex("fffe80")
try:
    raw.decode("utf-8")
except UnicodeDecodeError:
    pass
else:
    raise SystemExit("http body is valid UTF-8")
print("http body", len(raw))
PY
"$SCUZZ" run examples/http-bytes | tee /tmp/http-bytes.out
grep -Fxq 'http:bin:ok:3:match' /tmp/http-bytes.out
"$SCUZZ" eval examples/http-bytes | tee /tmp/http-bytes-eval.out
diff <(grep -vx ok /tmp/http-bytes.out) /tmp/http-bytes-eval.out
"$SCUZZ" fuzz --iterations 0 examples/http-bytes | tee /tmp/http-bytes-fuzz.out
grep -Fxq 'http:bin:ok:3:match' /tmp/http-bytes-fuzz.out
"$SCUZZ" check examples/http-bytes
"$SCUZZ" check examples/kernel
"$SCUZZ" check examples/scale
./examples/jump/gen.sh
"$SCUZZ" check examples/jump
"$SCUZZ" check examples/syntax
"$SCUZZ" check examples/fmt
"$SCUZZ" check examples/tyck
"$SCUZZ" check examples/codegen
"$SCUZZ" check examples/cli
"$SCUZZ" check examples/ide-sync
"$SCUZZ" check examples/ide-mobile
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
python3 - "$SCUZZ" <<'PYJSONCLI'
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import urllib.request

cli = str(pathlib.Path(sys.argv[1]).resolve())

def run_json(*args, ok=True):
    proc = subprocess.run([cli, *args], text=True, capture_output=True, timeout=120)
    assert (proc.returncode == 0) == ok, (args, proc.returncode, proc.stdout, proc.stderr)
    lines = proc.stdout.splitlines()
    assert len(lines) == 1, (args, proc.stdout, proc.stderr)
    return json.loads(lines[0])

with tempfile.TemporaryDirectory(prefix="scuzz-json-cli-") as tmp:
    base = pathlib.Path(tmp)
    empty = base / "empty"
    empty.mkdir()

    # 1. Missing manifest failures emit single-line JSON and exit 1.
    for sub in (["fuzz"], ["package"], ["ide", "crank"], ["ide", "serve", "--once"]):
        err_doc = run_json(*sub, "--message-format=json", str(empty), ok=False)
        assert err_doc["v"] == 1 and err_doc["ok"] is False and err_doc["status"] == "failed", err_doc
        assert "scuzz.toml" in err_doc["error"], err_doc

    # 2. fuzz pass + relate pass/fail + package pass/fail (editor trailing-flag order).
    pkg = base / "pkg"
    (pkg / "src").mkdir(parents=True)
    (pkg / "scuzz.toml").write_text('[package]\nname = "json-proof"\n')
    (pkg / "src/Main.scuzz").write_text(
        'def pos(n: Int): Bool = n > 0\n'
        'def big(n: Int): Bool = n > 2\n'
        'def neg(n: Int): Bool = n < 0\n'
        '@main def main: IO[Unit] = IO.println("hi")\n'
    )
    (pkg / "law.scuzz_verify").write_text(
        'oracle a(n: Int): Bool = if (Main.pos(n)) true else true\n'
        'oracle b(n: Int): Bool = if (Main.big(n)) true else true\n'
    )
    fuzz_ok = run_json("fuzz", "--iterations", "0", "--message-format=json", str(pkg), ok=True)
    assert fuzz_ok["v"] == 1 and fuzz_ok["kind"] == "fuzz" and fuzz_ok["status"] == "passed" and fuzz_ok["fuzz"]["ok"] is True, fuzz_ok

    rel_ok = run_json("fuzz", "--relate", "--message-format=json", str(pkg), ok=True)
    assert rel_ok["kind"] == "relate" and rel_ok["ok"] is True and rel_ok["failed"] == 0, rel_ok

    (pkg / "law.scuzz_verify").write_text(
        'oracle pos(n: Int): Bool = Main.pos(n)\n'
        'oracle neg(n: Int): Bool = Main.neg(n)\n'
    )
    rel_bad = run_json("fuzz", "--relate", "--message-format=json", str(pkg), ok=False)
    assert rel_bad["kind"] == "relate" and rel_bad["ok"] is False and rel_bad["failed"] >= 1, rel_bad

    fuzz_bad = run_json("fuzz", "--iterations", "16", "--seed", "1", "--message-format=json", str(pkg), ok=False)
    assert fuzz_bad["v"] == 1 and fuzz_bad["kind"] == "fuzz" and fuzz_bad["status"] == "failed" and fuzz_bad["fuzz"]["ok"] is False, fuzz_bad
    repro_path = pathlib.Path(fuzz_bad["fuzz"]["repro"])
    assert repro_path.is_file(), fuzz_bad
    rep_bad = run_json("fuzz", "--replay", str(repro_path), "--message-format=json", str(pkg), ok=False)
    assert rep_bad["v"] == 1 and rep_bad["kind"] == "replay" and rep_bad["status"] == "failed", rep_bad

    pkg_ok = run_json("package", str(pkg), "--message-format=json", ok=True)
    assert pkg_ok["kind"] == "package" and pkg_ok["ok"] is True and pkg_ok["status"] == "passed", pkg_ok
    assert pkg_ok["artifacts"] and pathlib.Path(pkg_ok["artifacts"][0]).exists(), pkg_ok

    pkg_bad = run_json("package", "--target", "web", str(pkg), "--message-format=json", ok=False)
    assert pkg_bad["kind"] == "package" and pkg_bad["ok"] is False and pkg_bad["status"] == "failed", pkg_bad
    assert pkg_bad["error"], pkg_bad

    # 3. ide crank + ide serve --once emit single-line JSON with zero progress chatter.
    ws = base / "ws"
    (ws / "src").mkdir(parents=True)
    (ws / "corpus").mkdir(parents=True)
    (ws / "scuzz.toml").write_text('[package]\nname = "ide-json"\n')
    (ws / "src/Main.scuzz").write_text('def step(): IO[Unit] =\n  Fs.write("out.txt", "v1")\n\n@main def main: IO[Unit] =\n  step()\n')
    (ws / "smoke.scuzz_scenario").write_text(
        'def setup(): IO[Unit] =\n  Fs.write("out.txt", "init")\n\n'
        'def runStep(): IO[Unit] =\n  Main.step()\n'
    )
    (ws / "smoke.scuzz_verify").write_text(
        'def writesVersion(t: Timeline): Verdict =\n'
        '  Verdict.every(t, i => !Timeline.driveHas(t, i, "runStep") || Timeline.fileTextIs(t, i, "out.txt", "v1") || Timeline.fileTextIs(t, i, "out.txt", "v2"))\n'
    )
    (ws / "corpus/run.toml").write_text('[fuzz]\nevents = ["drive runStep"]\n')
    subprocess.run([cli, "fmt", str(ws)], check=True, capture_output=True, text=True, timeout=30)
    prop = ws / "build/proposals/01-next/src"
    prop.mkdir(parents=True)
    (prop / "Main.scuzz").write_text('def step(): IO[Unit] =\n  Fs.write("out.txt", "v2")\n\n@main def main: IO[Unit] =\n  step()\n')

    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    def serve_once(req_builder):
        srv = subprocess.Popen(
            [cli, "ide", "serve", "--port", str(port), "--once", "--message-format=json", str(ws)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            first_line = srv.stdout.readline()
            assert first_line, srv.stderr.read()
            srv_doc = json.loads(first_line)
            assert srv_doc["kind"] == "ide-serve" and srv_doc["ok"] is True and srv_doc["status"] == "listening", srv_doc
            assert srv_doc["port"] == port and srv_doc["token"], srv_doc
            with urllib.request.urlopen(req_builder(srv_doc["token"]), timeout=5) as resp:
                assert resp.status == 200
                payload = json.loads(resp.read().decode())
            rest_out, rest_err = srv.communicate(timeout=10)
            assert srv.returncode == 0 and rest_out == "", (srv.returncode, rest_out, rest_err)
            return payload
        finally:
            if srv.poll() is None:
                srv.kill()
                srv.wait()

    post_doc = serve_once(lambda tok: urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/ide/sync",
        data=json.dumps({"v": 1, "id": "st-1", "kind": "steer", "prompt": "switch output to v2"}).encode(),
        headers={"Authorization": f"Bearer {tok}", "Content-Type": "application/json"},
        method="POST",
    ))
    assert post_doc["results"][0]["status"] == "queued", post_doc

    crank_ok = run_json("ide", "crank", "--rounds", "1", "--message-format=json", str(ws), ok=True)
    assert crank_ok["kind"] == "ide-crank" and crank_ok["ok"] is True and crank_ok["published"] == 1, crank_ok

    sync_doc = serve_once(lambda tok: urllib.request.Request(
        f"http://127.0.0.1:{port}/v1/ide/sync",
        headers={"Authorization": f"Bearer {tok}"},
    ))
    assert len(sync_doc["cards"]) == 1, sync_doc
print("cli json evidence ok")
PYJSONCLI
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

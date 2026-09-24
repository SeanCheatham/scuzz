#!/usr/bin/env bash
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SCUZZ="${SCUZZ:-$ROOT/examples/cli/build/cli}"

"$SCUZZ" run examples/kernel | tee /tmp/kernel.out
# Runner segvs here are flaky. On failure, rerun the exe under gdb so the log keeps a backtrace.
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
  sudo apt-get update -qq && sudo apt-get install -y -qq gdb
  gdb -batch -ex run -ex bt ./examples/kernel/build/kernel 2>&1 | tail -30 || true
  exit 1
fi
grep -Fxq 'triple:a"}b' /tmp/kernel.out
grep -Fxq 'nested:a}b' /tmp/kernel.out
grep -Fxq 'iget:y' /tmp/kernel.out
grep -Fxq 'nlist:y' /tmp/kernel.out
# The evaluator is a reference semantics: same stdout as the compiled kernel.
"$SCUZZ" eval examples/kernel | tee /tmp/kernel-eval.out
diff <(grep -vx ok /tmp/kernel.out) /tmp/kernel-eval.out
"$SCUZZ" run examples/scale | tee /tmp/scale.out
grep -q "mapn:3048" /tmp/scale.out
grep -q "maps:2098176" /tmp/scale.out
grep -q "hit:0:49" /tmp/scale.out
"$SCUZZ" run examples/io | tee /tmp/io.out
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
"$SCUZZ" eval examples/io | tee /tmp/io-eval.out
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


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
grep -q "adt:red" /tmp/kernel.out
grep -q "bare:42" /tmp/kernel.out
grep -q "bare:none" /tmp/kernel.out
grep -q "barehue" /tmp/kernel.out
grep -q "bare:2" /tmp/kernel.out
grep -q "barenone:9" /tmp/kernel.out
grep -q "optbare:1,0" /tmp/kernel.out
grep -q "unpackBare:6" /tmp/kernel.out
grep -q "phmap:1,2,3" /tmp/kernel.out
grep -q "phadd:2,3,4" /tmp/kernel.out
grep -q "phfilt:a,c" /tmp/kernel.out
grep -q "phid:a,b" /tmp/kernel.out
grep -q "guard:hit" /tmp/kernel.out
grep -q "guard:pos:3" /tmp/kernel.out
grep -q "guard:nonpos:0" /tmp/kernel.out
grep -q "guard:none" /tmp/kernel.out
grep -q "lit:zero" /tmp/kernel.out
grep -q "lit:other" /tmp/kernel.out
grep -q "lit:yes" /tmp/kernel.out
grep -q "lit:good" /tmp/kernel.out
grep -q "lit:some0" /tmp/kernel.out
grep -q "lit:hit0" /tmp/kernel.out
grep -q "or:primary" /tmp/kernel.out
grep -q "or:tiny" /tmp/kernel.out
grep -q "or:opt01" /tmp/kernel.out
grep -q "or:either:4" /tmp/kernel.out
grep -q "or:hit" /tmp/kernel.out
grep -q "as:adt:describe:42" /tmp/kernel.out
grep -q "as:z:0" /tmp/kernel.out
grep -q "as:primary" /tmp/kernel.out
grep -q "as:both:1" /tmp/kernel.out
grep -q "record:8" /tmp/kernel.out
grep -q "Point(3,5)" /tmp/kernel.out
grep -q "get:3" /tmp/kernel.out
grep -q "show:box" /tmp/kernel.out
grep -q "show:some" /tmp/kernel.out
grep -q "get:2" /tmp/kernel.out
grep -q "7ok" /tmp/kernel.out
grep -q "float:3.0" /tmp/kernel.out
grep -q "from:3.0" /tmp/kernel.out
grep -q "to:2" /tmp/kernel.out
grep -q "cmp:lt" /tmp/kernel.out
grep -q "neg:-1.5" /tmp/kernel.out
grep -q "uneg:-3" /tmp/kernel.out
grep -q "not:y" /tmp/kernel.out
grep -q "hex:255" /tmp/kernel.out
grep -q "bin:10" /tmp/kernel.out
grep -q "sep:1000" /tmp/kernel.out
grep -q "hexsep:65280" /tmp/kernel.out
grep -q "binsep:161" /tmp/kernel.out
grep -q "sci:15.0" /tmp/kernel.out
grep -q "scisep:1.0" /tmp/kernel.out
grep -q "tri:3" /tmp/kernel.out
grep -q "hole:3" /tmp/kernel.out
grep -q "band:15" /tmp/kernel.out
grep -q "bor:15" /tmp/kernel.out
grep -q "bxor:2" /tmp/kernel.out
grep -q "shl:8" /tmp/kernel.out
grep -q "shr:4" /tmp/kernel.out
grep -q "bnot:-1" /tmp/kernel.out
grep -q "named:8" /tmp/kernel.out
grep -q "namedkit:2" /tmp/kernel.out
grep -q "namedfn:7" /tmp/kernel.out
grep -q "copy:12" /tmp/kernel.out
grep -q "copyPos:6" /tmp/kernel.out
grep -q "copyBox:5" /tmp/kernel.out
grep -q "copyId:8" /tmp/kernel.out
grep -q "ascribe:9" /tmp/kernel.out
grep -q "ascribeList:0" /tmp/kernel.out
grep -q "ascribeInt:1" /tmp/kernel.out
grep -q "eqStr:y" /tmp/kernel.out
grep -q "eqOwned:y" /tmp/kernel.out
grep -q "eqList:y" /tmp/kernel.out
grep -q "eqAdt:y" /tmp/kernel.out
grep -q "eqOpt:y" /tmp/kernel.out
grep -q "eqRec:y" /tmp/kernel.out
grep -q "eqMap:y" /tmp/kernel.out
grep -q "eqSet:y" /tmp/kernel.out
grep -q "eqNest:y" /tmp/kernel.out
grep -q "tup:42" /tmp/kernel.out
grep -q "tup:ok" /tmp/kernel.out
grep -q "swap:ok:42" /tmp/kernel.out
grep -q "eqTup:y" /tmp/kernel.out
grep -q "both:x" /tmp/kernel.out
grep -q "first:9" /tmp/kernel.out
grep -q "map:1" /tmp/kernel.out
grep -q "miss:no" /tmp/kernel.out
grep -q "has:y" /tmp/kernel.out
grep -q "set:y" /tmp/kernel.out
grep -q "keys:a,b" /tmp/kernel.out
grep -q "vals:1,2" /tmp/kernel.out
grep -q "msize:2" /tmp/kernel.out
grep -q "gone:n" /tmp/kernel.out
grep -q "slist:x,y" /tmp/kernel.out
grep -q "ssize:2" /tmp/kernel.out
grep -q "sgone:n" /tmp/kernel.out
grep -q "mempty:y" /tmp/kernel.out
grep -q "sempty:y" /tmp/kernel.out
grep -q "mne:y" /tmp/kernel.out
grep -q "sne:y" /tmp/kernel.out
grep -q "mget:1" /tmp/kernel.out
grep -q "mmiss:" /tmp/kernel.out
grep -q "union:x,y,z" /tmp/kernel.out
grep -q "isect:y" /tmp/kernel.out
grep -q "sdiff:x" /tmp/kernel.out
grep -q "munion:1,9,3" /tmp/kernel.out
grep -q "misect:2" /tmp/kernel.out
grep -q "mdiff:1" /tmp/kernel.out
grep -q "sub:y" /tmp/kernel.out
grep -q "disj:y" /tmp/kernel.out
grep -q "take:a,b" /tmp/kernel.out
grep -q "drop:b,c" /tmp/kernel.out
grep -q "find:b" /tmp/kernel.out
grep -q "exists:y" /tmp/kernel.out
grep -q "takeWhile:a" /tmp/kernel.out
grep -q "dropWhile:b,c" /tmp/kernel.out
grep -q "forall:y" /tmp/kernel.out
grep -q "contains:y" /tmp/kernel.out
grep -q "ends:y" /tmp/kernel.out
grep -q "toInt:7" /tmp/kernel.out
grep -q "miss:9" /tmp/kernel.out
grep -q "repl:a:b" /tmp/kernel.out
grep -q "split:a:b" /tmp/kernel.out
grep -q "empty:y" /tmp/kernel.out
grep -q "strne:y" /tmp/kernel.out
grep -q "lower:ab" /tmp/kernel.out
grep -q "upper:AB" /tmp/kernel.out
grep -q "cap:Hello" /tmp/kernel.out
grep -q "repeat:aaa" /tmp/kernel.out
grep -q "strip:bc" /tmp/kernel.out
grep -q "suffix:ab" /tmp/kernel.out
grep -q "padL:xxa" /tmp/kernel.out
grep -q "padR:axx" /tmp/kernel.out
grep -q "blank:y" /tmp/kernel.out
grep -q "lix:3" /tmp/kernel.out
grep -q "take:ab" /tmp/kernel.out
grep -q "drop:bc" /tmp/kernel.out
grep -q "takeR:bc" /tmp/kernel.out
grep -q "dropR:ab" /tmp/kernel.out
grep -q "srev:cba" /tmp/kernel.out
grep -q "rev:c,b,a" /tmp/kernel.out
grep -q "count:2" /tmp/kernel.out
grep -q "fnot:a,c" /tmp/kernel.out
grep -q "llen:3" /tmp/kernel.out
grep -q "head:a" /tmp/kernel.out
grep -q "at:b" /tmp/kernel.out
grep -q "tail:b,c" /tmp/kernel.out
grep -q "fmap:a,a,b,b" /tmp/kernel.out
grep -q "padTo:a,z,z" /tmp/kernel.out
grep -q "nempty:y" /tmp/kernel.out
grep -q "range:1,2,3" /tmp/kernel.out
grep -q "typedlam:1,2,3" /tmp/kernel.out
grep -q "caselam:1,n" /tmp/kernel.out
grep -q "caselambare:1,n" /tmp/kernel.out
grep -q "caselam0:" /tmp/kernel.out
grep -q "caselit:z,n" /tmp/kernel.out
grep -q "funapply:3" /tmp/kernel.out
grep -q "funapplyN:?" /tmp/kernel.out
grep -q "funph:1" /tmp/kernel.out
grep -q "funtl:2" /tmp/kernel.out
grep -q "funadd:4" /tmp/kernel.out
grep -q "funtwice:11" /tmp/kernel.out
grep -q "funeta:1" /tmp/kernel.out
grep -q "mapeta:1,2" /tmp/kernel.out
grep -q "mapeta0:" /tmp/kernel.out
grep -q "usereta:3" /tmp/kernel.out
grep -q "funlet:4" /tmp/kernel.out
grep -q "funtyped:5" /tmp/kernel.out
grep -q "funret:6" /tmp/kernel.out
grep -q "funcap:7" /tmp/kernel.out
grep -q "funpass:6" /tmp/kernel.out
grep -q "funpass0:7" /tmp/kernel.out
grep -q "funmap:1,2" /tmp/kernel.out
grep -q "funmap0:" /tmp/kernel.out
grep -q "funapp:6" /tmp/kernel.out
grep -q "funcurry:7" /tmp/kernel.out
grep -q "funlit:7" /tmp/kernel.out
grep -q "fungrp:4" /tmp/kernel.out
grep -q "funphapp:9" /tmp/kernel.out
grep -q "pairapp:5" /tmp/kernel.out
grep -q "paireta:5" /tmp/kernel.out
grep -q "pairtup:5" /tmp/kernel.out
grep -q "pairexpr:5" /tmp/kernel.out
grep -q "pairfold:6" /tmp/kernel.out
grep -q "pairfold0:7" /tmp/kernel.out
grep -q "ifone:fn" /tmp/kernel.out
grep -q "ifone:y" /tmp/kernel.out
grep -q "tab:0,1,2" /tmp/kernel.out
grep -q "isp:a,|,b,|,c" /tmp/kernel.out
grep -q "grp:a,b|c" /tmp/kernel.out
grep -q "sld:a,b|b,c" /tmp/kernel.out
grep -q "lslice:b,c" /tmp/kernel.out
grep -q "ixw:1" /tmp/kernel.out
grep -q "lixw:2" /tmp/kernel.out
grep -q "ixs:0,1,2" /tmp/kernel.out
grep -Fq "sat:a|b,c" /tmp/kernel.out
grep -Fq "span:a,b|c" /tmp/kernel.out
grep -Fq "part:b|a,c" /tmp/kernel.out
grep -Fq "inits:|a|a,b|a,b,c" /tmp/kernel.out
grep -Fq "tails:a,b,c|b,c|c|" /tmp/kernel.out
grep -Fq "zip:a,1|b,2" /tmp/kernel.out
grep -Fq "zipAll:a,1|b,9|c,9" /tmp/kernel.out
grep -Fq "unzip:a,b|1,2" /tmp/kernel.out
grep -Fq "zipN:1,a|2,b" /tmp/kernel.out
grep -Fq "zix:0,a|1,b|2,c" /tmp/kernel.out
grep -q "fold:6" /tmp/kernel.out
grep -q "foldL:!ab" /tmp/kernel.out
grep -q "foldR:ab!" /tmp/kernel.out
grep -q "scanL:0,1,3,6" /tmp/kernel.out
grep -q "scanR:6,5,3,0" /tmp/kernel.out
grep -Fq "scanS:!|!a|!ab" /tmp/kernel.out
grep -Fq "scanSR:ab!|b!|!" /tmp/kernel.out
grep -q "scan0:7" /tmp/kernel.out
grep -q "red:6" /tmp/kernel.out
grep -q "redL:ab" /tmp/kernel.out
grep -q "redR:ab" /tmp/kernel.out
grep -Fq "tr:a,c|b,d" /tmp/kernel.out
grep -q "lhas:y" /tmp/kernel.out
grep -q "lix:1" /tmp/kernel.out
grep -q "llix:2" /tmp/kernel.out
grep -q "uniq:a,b,c" /tmp/kernel.out
grep -q "dby:aa,b" /tmp/kernel.out
grep -q "dby0:" /tmp/kernel.out
grep -q "tomap:1" /tmp/kernel.out
grep -q "tomapN:9" /tmp/kernel.out
grep -q "tomap0:0" /tmp/kernel.out
grep -q "toset:a,b" /tmp/kernel.out
grep -q "tosetN:1,2" /tmp/kernel.out
grep -Fq "tolist:a:1|b:2" /tmp/kernel.out
grep -q "ldiff:b,c" /tmp/kernel.out
grep -q "lisect:a" /tmp/kernel.out
grep -q "nhas:y" /tmp/kernel.out
grep -q "ixslice:0" /tmp/kernel.out
grep -q "lixslice:2" /tmp/kernel.out
grep -q "seglen:2" /tmp/kernel.out
grep -q "defat:y" /tmp/kernel.out
grep -q "lcmp:0" /tmp/kernel.out
grep -q "slen:3" /tmp/kernel.out
grep -q "slice:bc" /tmp/kernel.out
grep -q "ix:1" /tmp/kernel.out
grep -q "ch:98" /tmp/kernel.out
grep -q "lines:a,b" /tmp/kernel.out
grep -q "concat:a,b,c" /tmp/kernel.out
grep -q "flat:a,b,c" /tmp/kernel.out
grep -q "takeRight:b,c" /tmp/kernel.out
grep -q "dropRight:a,b" /tmp/kernel.out
grep -q "init:a,b" /tmp/kernel.out
grep -q "last:c" /tmp/kernel.out
grep -q "get:b" /tmp/kernel.out
grep -q "miss:z" /tmp/kernel.out
grep -q "fill:a,a,a" /tmp/kernel.out
grep -q "failif:7" /tmp/kernel.out
grep -q "failif:miss" /tmp/kernel.out
grep -q "forif:3" /tmp/kernel.out
grep -q "forif:miss" /tmp/kernel.out
grep -q "iomap:4" /tmp/kernel.out
grep -q "iomapS:a!" /tmp/kernel.out
grep -q "iomapU:7" /tmp/kernel.out
grep -q "caseio:1" /tmp/kernel.out
grep -q "tco:0" /tmp/kernel.out
grep -q "tcom:0" /tmp/kernel.out
grep -q "tcol:6" /tmp/kernel.out
grep -q "diff:y" /tmp/kernel.out
grep -q "build:2097152" /tmp/kernel.out
"$SCUZZ" run examples/scale | tee /tmp/scale.out
grep -q "mapn:3048" /tmp/scale.out
grep -q "maps:2098176" /tmp/scale.out
grep -q "hit:0:49" /tmp/scale.out
SCUZZ=./examples/cli/build/cli
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
grep -q "fs:fs-note" /tmp/io.out
grep -q "rand:ok" /tmp/io.out
grep -q "real:" /tmp/io.out
grep -q "mono:" /tmp/io.out
grep -q "kit:skip" /tmp/io.out
grep -q "fs:" /tmp/io.out
"$SCUZZ" test examples/io | tee /tmp/io-test.out
grep -q "served:POST:/ping:hi" /tmp/io-test.out
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
"$SCUZZ" test examples/bad-example
"$SCUZZ" check examples/bad-fault
"$SCUZZ" test examples/bad-fault
"$SCUZZ" check examples/bad-adt
"$SCUZZ" test examples/bad-adt
"$SCUZZ" check examples/bad-sched
"$SCUZZ" test examples/bad-sched
"$SCUZZ" check examples/bad-response
"$SCUZZ" check examples/bad-split
if "$SCUZZ" check examples/bad-intent; then
  echo "empty verify should fail check" && exit 1
fi
"$SCUZZ" check --message-format=json examples/hello | tee /tmp/hello-check.json
grep -q '"severity":"info"' /tmp/hello-check.json
grep -q 'unclaimed def' /tmp/hello-check.json
"$SCUZZ" lsp --help | tee /tmp/lsp-help.txt
grep -q "scuzz check" /tmp/lsp-help.txt
if "$SCUZZ" check testdata/fmt/needs_format > /tmp/fmt-check.err 2>&1; then echo "expected format error" && exit 1; fi
grep -q "needs formatting" /tmp/fmt-check.err
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


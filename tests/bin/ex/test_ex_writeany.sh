#!/bin/bash
# write to an existing file other than the current one needs `!` (or the
# writeany option).  It overwrote it without a word.
#
# Usage: test_ex_writeany.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_writeany.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1
printf 'new\n' >f

echo old >other
out=$(printf 'w other\nq\n' | "$EX_BIN" -s f 2>&1)
if [ "$(cat other)" != old ]; then
	fail "w overwrote an existing other file without !: [$(cat other)]"
fi
if ! grep -q 'File exists' <<<"$out"; then
	fail "w to an existing other file was not refused: [$out]"
fi

printf 'w! other\nq\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat other)" = new ] || fail "w! did not write the other file: [$(cat other)]"

echo old >other
printf 'set writeany\nw other\nq\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat other)" = new ] || fail "w with writeany did not write the other file: [$(cat other)]"

echo old >other
printf 'w >> other\nq\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat other)" = "$(printf 'old\nnew')" ] || fail "w >> to an existing file: [$(cat other)]"

# a new file, and the current file under another name, need no !
printf 'w fresh\nw ./f\nq\n' | "$EX_BIN" -s f >"$T/out" 2>&1
[ "$(cat fresh 2>/dev/null)" = new ] || fail "w to a new file: [$(cat "$T/out")]"
grep -q 'File exists' "$T/out" && fail "w ./f of the current file was refused: [$(cat "$T/out")]"

out=$(printf 'set wa?\nset wa\nset writeany?\nq\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "$(printf 'nowriteany\nwriteany')" ] || fail "set writeany / wa: [$out]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_writeany"

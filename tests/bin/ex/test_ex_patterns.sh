#!/bin/bash
# The last pattern, substitute's options, and bad patterns.
#  - an empty pattern is the last pattern any command used: s// after a
#    search used only the last substitute's (nothing), and g// matched every
#    line and recorded nothing;
#  - substitute takes POSIX's [options][count][flags]: c asks about each
#    match, a count runs from the range's last line, and && keeps the
#    previous options -- all were "Bad substitute flags";
#  - a pattern that will not compile says so: s/[/x/ did nothing silently.
#
# Usage: test_ex_patterns.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_patterns.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

# s// after a search is the search's pattern
printf 'x\na\nb\n' >f
out=$(printf '/a/\ns//c/\n%%p\nq!\n' | "$EX_BIN" -s f 2>&1)
if [ "$out" != "$(printf 'a\nx\nc\nb')" ]; then
	fail "s// after /a/: [$out]"
fi

# g// after a search matches the search's lines only
out=$(printf '/b/\ng//p\nq!\n' | "$EX_BIN" -s f 2>&1)
if [ "$out" != "$(printf 'b\nb')" ]; then
	fail "g// after /b/: [$out]"
fi

# g's pattern becomes the last: s// after it
out=$(printf 'g/a/s//A/\n%%p\nq!\n' | "$EX_BIN" -s f 2>&1)
if [ "$out" != "$(printf 'x\nA\nb')" ]; then
	fail "s// inside g/a/: [$out]"
fi

# and v's, though it runs on the lines that do not match
out=$(printf 'v/a/p\n2s//X/\n%%p\nq!\n' | "$EX_BIN" -s f 2>&1)
if [ "$out" != "$(printf 'x\nb\nx\nX\nb')" ]; then
	fail "s// after v/a/: [$out]"
fi
out=$(printf 'v/a/p\ng//p\nq!\n' | "$EX_BIN" -s f 2>&1)
if [ "$out" != "$(printf 'x\nb\na')" ]; then
	fail "g// after v/a/: [$out]"
fi

# the c option asks about each match (in batch nothing echoes the answer,
# so the line printed after follows the last prompt)
printf 'x x x\n' >c
out=$(printf 's/x/y/gc\ny\nn\ny\n1p\nq!\n' | "$EX_BIN" -s c 2>&1)
if [ "$(tail -1 <<<"$out" | sed 's/^\[y\/n\/q\] //')" != "y x y" ] ||
   [ "$(grep -c '\^' <<<"$out")" -ne 3 ]; then
	fail "s/x/y/gc answered y n y: [$out]"
fi
out=$(printf 's/x/y/gc\ny\nq\n1p\nq!\n' | "$EX_BIN" -s c 2>&1)
if [ "$(tail -1 <<<"$out" | sed 's/^\[y\/n\/q\] //')" != "y x x" ]; then
	fail "s/x/y/gc answered y q: [$out]"
fi

# a count runs from the range's last line
printf 'x\nx\nx\nx\n' >n
out=$(printf '2s/x/y/ 2\n%%p\nq!\n' | "$EX_BIN" -s n 2>&1)
if [ "$out" != "$(printf 'x\ny\ny\nx')" ]; then
	fail "2s/x/y/ 2: [$out]"
fi

# && keeps the previous options (g); & alone does not
printf 'aa\naa\naa\n' >r
out=$(printf '1s/a/b/g\n2&&\n3&\n%%p\nq!\n' | "$EX_BIN" -s r 2>&1)
if [ "$out" != "$(printf 'bb\nbb\nba')" ]; then
	fail "&& and &: [$out]"
fi

# a pattern that will not compile is reported, and is an error
out=$(printf 's/[/x/\nq!\n' | "$EX_BIN" -s f 2>&1)
status=$?
if [ "$status" -eq 0 ] || ! grep -qi "pattern" <<<"$out"; then
	fail "s/[/x/: status $status [$out]"
fi
out=$(printf 'g/[/d\nq!\n' | "$EX_BIN" -s f 2>&1)
status=$?
if [ "$status" -eq 0 ] || ! grep -qi "pattern" <<<"$out"; then
	fail "g/[/d: status $status [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_patterns"

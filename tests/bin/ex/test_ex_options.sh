#!/bin/bash
# POSIX options that had no effect (EXVI-53), those batch use shows:
# beautify, edcompatible, errorbells, paragraphs/sections (pairs only),
# taglength and autowrite.  (autoprint, report, warn, prompt and term are
# interactive: test_ex_options_tty.py; the vi ones: test_vi_options.py.)
#
# Usage: test_ex_options.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_options.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

# beautify: control characters but tab and form feed go from text read
printf 'a\001b\177c\td\fe\n' >ctl
printf 'x\n' >f
printf 'set bf\n$r ctl\nw! out\nq!\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat out)" = "$(printf 'x\nabc\td\fe')" ] || fail "beautify: [$(od -c out | head -2 | tr -s ' ')]"
printf '$r ctl\nw! out\nq!\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat out)" = "$(printf 'x\na\001b\177c\td\fe')" ] || fail "nobeautify changed the text"

# edcompatible: s remembers g (and c), and g written again turns it off
printf 'aaaa bbbb\n' >f
out=$(printf 'set ed\n1s/a/X/g\n1s/b/Y/\np\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "XXXX YYYY" ] || fail "edcompatible remembers g: [$out]"
out=$(printf 'set ed\n1s/a/X/g\n1s/b/Y/g\np\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "XXXX Ybbb" ] || fail "edcompatible toggles g: [$out]"
out=$(printf '1s/a/X/g\n1s/b/Y/\np\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "XXXX Ybbb" ] || fail "noedcompatible: [$out]"

# errorbells: a bell before ex's error message
out=$(printf 'set eb\nnosuchcommand\n' | "$EX_BIN" -s f 2>&1 | od -c | head -1)
case "$out" in *'\a'*) ;; *) fail "errorbells: no bell: [$out]" ;; esac
out=$(printf 'nosuchcommand\n' | "$EX_BIN" -s f 2>&1 | od -c | head -1)
case "$out" in *'\a'*) fail "noerrorbells rang" ;; esac

# paragraphs and sections: pairs of characters
out=$(printf 'set para=IPL\nq!\n' | "$EX_BIN" -s f 2>&1)
[ -n "$out" ] || fail "an odd paragraphs value was taken"
out=$(printf 'set sect=SHN\nq!\n' | "$EX_BIN" -s f 2>&1)
[ -n "$out" ] || fail "an odd sections value was taken"
out=$(printf 'set para=IPLP\nset para?\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "paragraphs=IPLP" ] || fail "an even paragraphs value: [$out]"

# taglength: the first taglength characters of tag names count
printf 'foo\ttarget\t/^here/\n' >tags
printf 'top\nhere\n' >target
out=$(printf 'set tl=3\nta fooxx\n.=\nq!\n' | "$EX_BIN" -s f 2>&1)
[ "$out" = "$(printf 'here\n2')" ] || fail "taglength=3: [$out]"
out=$(printf 'ta fooxx\nq!\n' | "$EX_BIN" -s f 2>&1)
case "$out" in *"not found"*) ;; *) fail "taglength=0 found fooxx: [$out]" ;; esac

# autowrite: written before !, so q quits
printf 'one\ntwo\n' >f
printf 'set aw\n1d\n!true\nq\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat f)" = "two" ] || fail "autowrite before !: [$(cat f)]"
printf 'one\ntwo\n' >f
printf '1d\n!true\nq\n' | "$EX_BIN" -s f >/dev/null 2>&1
[ "$(cat f)" = "$(printf 'one\ntwo')" ] || fail "noautowrite wrote"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_options"

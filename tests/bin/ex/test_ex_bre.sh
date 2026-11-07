#!/bin/bash
# ex and vi patterns are POSIX basic regular expressions, with \< and \>,
# and ~ standing for the last substitute's replacement; the magic option,
# on by default, gives . [ * and ~ their special meaning, and nomagic gives
# it to their escaped forms instead.  Patterns were compiled as EREs, so
# s/a+/X/ on "aa+" gave "X+", `|` was alternation, a ctags address
# /^int foo(void)$/ found nothing, and there was no magic option.  vim -es
# agrees on each result below.
#
# Usage: test_ex_bre.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_bre.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check TEXT CMDS WANT: run CMDS (printf %b, one per line) on a file of TEXT, want its lines WANT, space-joined
check() {
	local text="$1" cmds="$2" want="$3" got

	printf '%b' "$text" >"$T/f"
	got=$(printf '%b\n%%p\nq!\n' "$cmds" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmds: [$got], expected [$want]"
	fi
}

# basic, not extended
check 'aa+\n' '1s/a+/X/' 'aX'
check 'a|b\n' 's/a|b/X/' 'X'
check 'abab\n' 's/\\(ab\\)\\1/X/' 'X'
check 'foo\n' 's/o\\{2\\}/X/' 'fX'
check 'x\nint foo(void)\ny\n' '/^int foo(void)$/d' 'x y'
# word anchors
check 'foo foobar foo\n' 's/\\<foo\\>/X/g' 'X foobar X'
# ~ is the last replacement; \~ and ~ in brackets are the character
check 'a\nxy\n' '1s/a/xy/\n2s/~/Z/' 'xy Z'
check 'a~\n' 's/\\~/T/' 'aT'
check 'a~\n' 's/[~]/T/' 'aT'
# nomagic: the bare forms are ordinary, the escaped ones special
check 'abc a.c\n' 'set nomagic\ns/a.c/X/' 'abc X'
check 'abc a.c\n' 'set nomagic\ns/a\\.c/X/' 'X a.c'
check 'x*\n' 'set nomagic\ns/x*/Y/' 'Y'
check 'a~\n' 'set nomagic\ns/~/T/' 'aT'
check 'a\nxa\n' '1s/a/x/\nset nomagic\n2s/\\~a/Q/' 'x Q'
check 'a.c abc\n' 'set nomagic\nset magic\ns/a.c/X/g' 'X X'

# the option itself
got=$(printf 'set magic?\nset nomagic\nset magic?\nq!\n' | "$EX_BIN" -s 2>&1 | tr '\n' ' ')
if [ "$got" != "magic nomagic " ]; then
	fail "set magic?: [$got], expected [magic nomagic]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_bre"

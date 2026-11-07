#!/bin/bash
# substitute's replacement is read as POSIX ex reads it: & is the match,
# \1..\9 the subexpressions, ~ the last replacement, \u \l \U \L \E change
# case, a backslash before a newline splits the line, and before anything
# else stands for that character; with nomagic, \& and \~ are the special
# forms.  The replacement was copied literally, so s/b/[&]/ on "abc" gave
# "a[&]c" and s/\(foo\) \(bar\)/\2 \1/ gave "2 1".  vim -es agrees on each
# result below but the split, where it writes a NUL and POSIX splits.
#
# Usage: test_ex_replacement.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_replacement.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check TEXT CMDS WANT: run CMDS (printf %b) on a file of TEXT, want its lines WANT, space-joined
check() {
	local text="$1" cmds="$2" want="$3" got

	printf '%b' "$text" >"$T/f"
	got=$(printf '%b\n%%p\nq!\n' "$cmds" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmds: [$got], expected [$want]"
	fi
}

# & and \1..\9
check 'abc\n' 's/b/[&]/' 'a[b]c'
check 'foo bar\n' 's/\\(foo\\) \\(bar\\)/\\2 \\1/' 'bar foo'
check 'ab\n' 's/\\(a\\)\\(x\\)*b/[\\2]/' '[]'
check 'abc\n' 's/b/\\&/' 'a&c'
check 'a b c\n' 's/[a-z]/<&>/g' '<a> <b> <c>'
# ~
check 'a\nb\n' '1s/a/X/\n2s/b/~~/' 'X XX'
check 'abc\n' 's/b/\\~/' 'a~c'
# case
check 'foo\n' 's/foo/\\u&/' 'Foo'
check 'ab cd\n' 's/.*/\\U&/' 'AB CD'
check 'ab cd\n' 's/\\(ab\\) \\(cd\\)/\\U\\1\\E \\2/' 'AB cd'
check 'ABC\n' 's/.*/\\L&/' 'abc'
check 'ABC\n' 's/ABC/\\l&/' 'aBC'
check 'abc\n' 's/b/\\Uxy\\ez/' 'aXYzc'
check 'abc\n' 's/b/\\u\\Lxy/' 'aXyc'
# nomagic
check 'abc\n' 'set nomagic\ns/b/[&]/' 'a[&]c'
check 'abc\n' 'set nomagic\ns/b/[\\&]/' 'a[b]c'
# escapes
check 'abc\n' 's/b/\\//' 'a/c'
check 'abc\n' 's/b/\\\\/' 'a\c'
# the repeat takes the replacement as it was read
check 'a\na\n' '1s/a/[&]/\n2&' '[a] [a]'
# a backslash before a newline splits the line (POSIX), once per match and line
check 'abc\n' 's/b/x\\\ny/' 'ax yc'
check 'ab\ncb\nz\n' '1,2s/b/1\\\n2/' 'a1 2 c1 2 z'
check 'abab\n' 's/b/\\\n/g' 'a a '

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_replacement"

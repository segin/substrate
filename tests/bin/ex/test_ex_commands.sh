#!/bin/bash
# ex commands POSIX lists that were "Unknown command" (EXVI-49): z, ^D, <,
# >, ~, @ and *, chdir/cd, shell, source, ex, the \/ \? and \& addresses,
# and the ve abbreviation.  Expected results are POSIX ex's (Adjust
# Window, <control>-D, Shift, Execute, ...).
#
# Usage: test_ex_commands.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_commands.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

cd "$T" || exit 1

# check NAME SCRIPT EXPECTED [TEXT]: SCRIPT (printf format, q! added) on a
# file of TEXT (printf format; 1..10 by default), its output's lines
# joined by spaces
check() {
	if [ $# -ge 4 ]; then printf "$4" >f; else seq 1 10 >f; fi
	out=$(printf "$2"'q!\n' | "$EX_BIN" -s f 2>&1 | tr '\n' ' ' | sed 's/ $//')
	if [ "$out" != "$3" ]; then
		echo "FAIL: $1: [$out], expected [$3]"
		fails=$((fails + 1))
	fi
}

H=----------------------------------------
check "z with a count" '1z3\n.=\n' "1 2 3 3"
check "z-" '5z-3\n.=\n' "3 4 5 5"
check "z." '5z.3\n.=\n' "4 5 6 6"
check "z=" '5z=3\n.=\n' "4 $H 5 $H 6 5"
check "z+" '5z+3\n.=\n' "6 7 8 8"
check "z^" '9z^3\n.=\n' "4 5 6 6"
check "z with no line" '3\nz3\n.=\n' "3 4 5 6 6"
check "^D" 'set scroll=2\n3\n\004\n.=\n' "3 4 5 5"
# z with no count writes 2 x scroll lines (POSIX Adjust Window)
check "z with scroll" 'set scroll=2\n1z\n.=\n' "1 2 3 4 4"
check "z with scroll, no line" 'set scroll=3\n2\nz\n.=\n' "2 3 4 5 6 7 8 8"
check "nu" '2nu\n' "      2 2"
check "#" '2#\n' "      2 2"
check "number" '2,3number\n' "      2 2       3 3"
# the error on stderr, before stdout's buffered line
check "^D on the last line" '10\n\004\n' "At end of file 10"
check "ve" 've\n' "Substrate vi v0.1"
check "\\/ and \\?" '/5/\n\\/=\n\\?=\n' "5 5 5"
check "\\&" 's/3/X/\n1\n\\&=\n' "1 3"
check "~" '1s/foo/X/\n/bar/\n~\n%%p\n' "bar X bar foo X" 'foo bar foo\nbar\n'
check "@" '1ya a\n2,3@a\n%%p\n' "s/a/b/ xb yb za" 's/a/b/\nxa\nya\nza\n'
check "@@ and *" '1ya a\n2@a\n3@@\n4*\n%%p\n' "s/a/b/ xb yb zb" 's/a/b/\nxa\nya\nza\n'
check "@ of an empty buffer" '@b\n' "Buffer is empty" 'x\n'
# p shows the tab of the second line as spaces
check "< and >" 'set sw=4\n1>\n2>>\n1<\n%%p\n' "1         2 3" '1\n2\n3\n'

check "map lists" 'map K dd\nmap! jj X\nmap\nab teh the\nab\nunmap K\nmap\n' "K	dd teh	the" 'x\n'
check "unmap of nothing" 'unmap K\n' "K is not mapped" 'x\n'
check "unabbreviate" 'ab teh the\nab hte the\nuna teh\nab\n' "hte	the" 'x\n'
printf '2d\n$p\n' >src.ex
check "source" 'so src.ex\n%%p\n' "3 1 3" '1\n2\n3\n'
printf 'one\n' >other
check "ex" 'ex other\n%%p\n' "one" 'x\n'
mkdir sub
check "cd" "cd $T/sub\nr !pwd\n\$p\n" "$T/sub" 'x\n'
printf '#!/bin/sh\necho ran >%s/sh.mark\n' "$T" >fakesh
chmod +x fakesh
check "sh" "set shell=$T/fakesh\nsh\n" "" 'x\n'
[ "$(cat sh.mark 2>/dev/null)" = ran ] || { echo "FAIL: sh did not run the shell"; fails=$((fails + 1)); }

# the shifted file itself: tabs and spaces as tabstop and shiftwidth make
printf '1\n2\n3\n' >g
printf 'set sw=4\n1>\n2>>\n3>\nwq\n' | "$EX_BIN" -s g >/dev/null 2>&1
if [ "$(cat g)" != "$(printf '    1\n\t2\n    3')" ]; then
	echo "FAIL: shifted text: [$(od -c g | head -2 | tr -s ' ')]"
	fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_commands"

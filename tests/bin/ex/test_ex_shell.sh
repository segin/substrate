#!/bin/bash
# !, r ! and w ! run their command as POSIX ex does: by the shell option's
# program (default $SHELL) with -c, after % # and ! in it are expanded to
# the file name, the alternate file name and the last command; and a range
# given to ! is replaced with the command's output for it.  The command was
# split at blanks and run by execvp(), so `!echo a | tr a b` printed
# "a | tr a b", `!echo %` printed "%", `!!` failed and 2,3!sort -r changed
# nothing.  vim -es agrees on each result below.
#
# Usage: test_ex_shell.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_shell.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check CMDS WANT: run CMDS (printf %b) on a file of 1..4 with SHELL=/bin/sh, want the output WANT, space-joined
check() {
	local cmds="$1" want="$2" got

	seq 1 4 >"$T/f"
	got=$(cd "$T" && printf '%b\nq!\n' "$cmds" | SHELL=/bin/sh "$EX_BIN" -s f 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmds: [$got], expected [$want]"
	fi
}

# the shell runs it
check '!echo a | tr a b' 'b'
check '!echo one; echo two' 'one two'
# % # ! and their escapes
check '!echo %' 'f'
check '!echo \\%' '%'
check 'e! g\n!echo #' 'f'
check '!echo a\n!!' 'a a'
check '!echo x\n!echo !y' 'x echo xy'
# a range is filtered
check '2,3!sort -r\n%p' '1 3 2 4'
check '%!tr 1-4 a-d\n%p' 'a b c d'
check '2!echo x; echo y\n%p' '1 x y 3 4'
check '2,3!true\n%p' '1 4'
# the lines replaced go to the unnamed buffer, in line mode; the current
# line is the last line read in
check '2,3!sort -r\n$put\n%p' '1 3 2 4 2 3'
check '2,3!sort -r\n.=' '3'
# r ! and w ! go through the shell too
check '$r !echo x | tr x z\n%p' '1 2 3 4 z'
check 'w !tr 1 X' 'X 2 3 4'
check '2,3w !cat -n | tr -s " \\t" ":"' ':1:2 :2:3'

# the shell option, its default $SHELL
got=$(printf 'set shell?\nq!\n' | SHELL=/bin/sh "$EX_BIN" -s 2>&1)
[ "$got" = "shell=/bin/sh" ] || fail "set shell? with SHELL=/bin/sh: [$got]"
got=$(printf '!echo a\nq!\n' | SHELL=/bin/false "$EX_BIN" -s 2>&1)
[ -z "$got" ] || fail "!echo a with SHELL=/bin/false: [$got], expected nothing"
got=$(printf 'set shell=/bin/sh\n!echo a\nq!\n' | SHELL=/bin/false "$EX_BIN" -s 2>&1)
[ "$got" = "a" ] || fail "set shell=/bin/sh, !echo a: [$got], expected [a]"

# what is not there to expand is an error, and nothing runs
got=$(printf '!touch %s/ran #\nq!\n' "$T" | SHELL=/bin/sh "$EX_BIN" -s 2>&1)
[ ! -e "$T/ran" ] || fail "!touch ran #, no alternate file: it ran"
case $got in *"No alternate filename"*) ;; *) fail "!cmd # with no alternate file: [$got]" ;; esac

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_shell"

#!/bin/bash
# undo after global or v undoes the whole command, every command it ran,
# as one change (POSIX).  Each command global ran took its own snapshot,
# so g/[24]/d then u brought back only the 4.  vim -es agrees on each
# result below.
#
# Usage: test_ex_global_undo.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_global_undo.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# check CMDS WANT: run CMDS (printf %b) on a file of 1..5, want its lines WANT, space-joined
check() {
	local cmds="$1" want="$2" got

	seq 1 5 >"$T/f"
	got=$(printf '%b\n%%p\nq!\n' "$cmds" | "$EX_BIN" -s "$T/f" 2>&1 | tr '\n' ' ')
	if [ "$got" != "$want " ]; then
		fail "$cmds: [$got], expected [$want]"
	fi
}

check 'g/[24]/d\nu' '1 2 3 4 5'
check 'g/./s/$/x/\nu' '1 2 3 4 5'
check 'v/3/d\nu' '1 2 3 4 5'
check 'g/[24]/d\n1d\nu' '1 3 5'
# a global that changes nothing is nothing to undo: u undoes what came before
check '1d\ng/zzz/d\nu' '1 2 3 4 5'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_global_undo"

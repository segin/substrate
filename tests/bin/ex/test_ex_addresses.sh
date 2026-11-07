#!/bin/bash
# Addresses, counts, flags, buffers and the current line, as POSIX ex has
# them -- each case the audit's evidence of what was wrong:
#  - a backwards range and line 0 for a command that takes no line 0 are
#    errors (3,1d, 0d, 1,0d did nothing, silently);
#  - an address omitted beside , or ; is the current line, and of more
#    addresses than a command takes the last ones count (,5p and 1,3,5p were
#    "Unknown command");
#  - a count runs from the range's last line, and p # l print the line
#    after (1,3d 2 deleted 1-3, 2p# was "Unknown command");
#  - d names a buffer, and A-Z append (2d x and 2y A went to the unnamed one);
#  - each command leaves the current line POSIX says, and ex starts on the
#    last line of the file (2,4d left line 1, ex started on line 1).
#
# Usage: test_ex_addresses.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_addresses.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

# check NAME "COMMANDS" EXPECTED [LINES]: run COMMANDS (q! added) on a
# file of 1..LINES (6), expecting EXPECTED (newlines as spaces) and status 0
check() {
	local name=$1 cmds=$2 want=$3 lines=${4:-6}
	seq 1 "$lines" >f
	out=$(printf '%s\nq!\n' "$cmds" | "$EX_BIN" -s f 2>&1)
	status=$?
	out=$(tr '\n' ' ' <<<"$out" | sed 's/ $//')
	if [ "$status" -ne 0 ] || [ "$out" != "$want" ]; then
		fail "$name: status $status [$out], expected [$want]"
	fi
}

# error NAME "COMMAND": an error, and the file left as it was
error() {
	local name=$1 cmd=$2
	seq 1 6 >f
	out=$(printf '%s\nwq\n' "$cmd" | "$EX_BIN" -s f 2>&1)
	status=$?
	if [ "$status" -eq 0 ] || [ "$(seq 1 6)" != "$(cat f)" ]; then
		fail "$name: status $status [$out] f=[$(tr '\n' ' ' <f)]"
	fi
}

error "3,1d" "3,1d"
error "0d" "0d"
error "1,0d" "1,0d"

check ",4p from line 2" "2|,4p" "2 2 3 4"
check "1,p from line 3" "3|1,p" "3 1 2 3"
check "1,3,5p" "1,3,5p" "3 4 5"
check "2;+1p" "2;+1p" "2 3"

check "1,3d 2" "1,3d 2|%p" "1 2 5 6"
check "3j 3" "3j 3|%p" "1 2 3 4 5 6"
check "3j 3 joined" "3j 3|3p" "3 4 5"
check "2p#" "2p#" "$(printf '%7s' 2) 2"
check "2pl" "2pl" "2\$"
# (a letter straight after d is a buffer name: flags come after a count)
check "2d 1 p" "2d 1 p" "3"

check "2d x" "2d x|\$pu x|%p" "1 3 4 5 6 2"
check "1y a, 2y A" "1y a|2y A|\$pu a|%p" "1 2 3 4 5 6 1 2"

check "startup" ".=" "6"
check "2,4d" "2,4d|.=" "2" 10
check "8,10d" "8,10d|.=" "7" 10
check "2,3m\$" "2,3m\$|.=" "10" 10
check "2,4t0" "2,4t0|.=" "3" 10
printf 'r1\nr2\n' >two
check "2r two" "2r two|.=" "4" 10
check "e f" "e! f|.=" "6"

# join: an appended line's leading blanks go; two spaces after a `.`, none
# before `)`, none after a blank (`a` + `   b` gave `a   b`)
joined() {
	printf "$1" >jf
	out=$(printf '1,$j|1p\nq!\n' | "$EX_BIN" -s jf 2>&1)
	if [ "$out" != "$2" ]; then
		fail "join of [$1]: [$out], expected [$2]"
	fi
}
joined 'a\n   b\n' 'a b'
joined 'end.\nnext\n' 'end.  next'
joined 'f(\n)\n' 'f()'
joined 'a \n\tb\n' 'a b'
joined 'a\n\nb\n' 'a b'

# edit +command runs the command in the file it loads (it was taken as part
# of the file name: `e +2 three` edited "+2 three")
seq 1 3 >three
check "e +2 three" "e! +2 three|.=" "2 2"     # (2, as a command, prints line 2)

# a command that cannot be done says so: $j, and s with no substitute before
error "\$j" '$j'
error "s with no previous substitute" 's'

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_addresses"

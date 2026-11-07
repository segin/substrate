#!/bin/bash
# undo gives back the lines, not the rest of the buffer's state: the file
# name stays the one being edited, the buffer is modified -- it is no
# longer what was written -- and marks stay on the lines undo keeps.  The
# whole buffer was put back from the snapshot, so `f fb' then u brought
# the old name back, u after a write left the buffer unmodified and q
# quit, losing it, and every mark went.  vim -es agrees on each result.
#
# Usage: test_ex_undo_state.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_undo_state.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# run CMDS: run CMDS (printf %b) on a file f of 1..4 in $T; sets out and status
run() {
	seq 1 4 >"$T/f"
	rm -f "$T/fb"
	out=$(cd "$T" && printf '%b\n' "$1" | "$EX_BIN" -s f 2>&1)
	status=$?
}

# a write, then u: the buffer differs from the file, and q refuses
run '1s/1/X/\nw\nu\nq'
[ "$status" -ne 0 ] || fail "u after w: q quit, status 0"
[ "$(tr '\n' ' ' <"$T/f")" = "X 2 3 4 " ] || fail "u after w: the file is [$(tr '\n' ' ' <"$T/f")]"
case $out in *"No write since last change"*) ;; *) fail "u after w, q: [$out]" ;; esac

# the file name stays
run '1d\nf fb\nu\nw\nq'
[ "$status" -eq 0 ] || fail "f fb, u, w: status $status [$out]"
got=$(cat "$T/fb" 2>/dev/null | tr '\n' ' ')
[ "$got" = "1 2 3 4 " ] || fail "f fb, u, w: fb is [$got]"
[ "$(tr '\n' ' ' <"$T/f")" = "1 2 3 4 " ] || fail "f fb, u, w: f changed"

# marks stay on the lines undo keeps
run "2ka\n3d\nu\n'ap\nq!"
[ "$out" = "2" ] || fail "2ka, 3d, u, 'ap: [$out], expected [2]"
run "3d\n2ka\nu\n'ap\nq!"
[ "$out" = "2" ] || fail "3d, 2ka, u, 'ap: [$out], expected [2]"
run "2ka\n1s/1/X/\nu\n'ap\nq!"
[ "$out" = "2" ] || fail "2ka, 1s/1/X/, u, 'ap: [$out], expected [2]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_undo_state"

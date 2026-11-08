#!/bin/bash
# SIGINT stops the current command and returns to the prompt with the
# editor intact, every time.  Its handler longjmp'd to the ex main loop:
# through a jmp_buf not yet set when it came before the loop (vi, and
# `ex -c vi`, crashed), with SIGINT left blocked afterwards (a second ^C
# did nothing), and out of the middle of whatever was running: a global
# interrupted between its commands left its marks set, so the next global
# ran on lines it never matched.
#
# Usage: test_ex_interrupt.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_interrupt.XXXXXX")
pid=
trap '[ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null; rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

# ex reading commands from a fifo, so that it waits for them.  Not under
# timeout(1): the signals must reach ex itself (the EXIT trap kills a
# straggler, and closing the fifo ends it).
start() {
	mkfifo "$T/in"
	"$EX_BIN" -s "$@" <"$T/in" >"$T/out" 2>&1 &
	pid=$!
	exec 3>"$T/in"
}
say() { printf '%s\n' "$1" >&3; sleep 0.3; }
# wait until ex has read the file (a sanitizer build is slow at it): $= is
# answered with the line count
ready() {
	printf '$=\n' >&3
	for _ in $(seq 1 300); do
		grep -qx "$1" "$T/out" && return 0
		sleep 0.1
	done
	return 1
}
finish() {
	exec 3>&-
	wait "$pid"
	status=$?
	pid=
	rm -f "$T/in"
}

# two interrupts while waiting for a command; both are seen, and ex goes on
printf 'one\ntwo\n' >"$T/f"
start "$T/f"
sleep 0.3
kill -INT "$pid"; sleep 0.3
blocked=$(awk '/^SigBlk:/ { print $2 }' "/proc/$pid/status" 2>/dev/null)
kill -INT "$pid"; sleep 0.3
say '1p'
say 'q!'
finish
if [ "$(grep -c '^Interrupt$' "$T/out")" -ne 2 ]; then
	fail "two interrupts were not both seen: [$(cat "$T/out")]"
fi
if [ -n "$blocked" ] && [ $((0x$blocked & 2)) -ne 0 ]; then
	fail "SIGINT was left blocked after an interrupt (SigBlk $blocked)"
fi
if ! grep -qx 'one' "$T/out" || [ "$status" -ne 0 ]; then
	fail "ex did not go on after the interrupts (status $status): [$(cat "$T/out")]"
fi

# an interrupted global leaves no marks behind for the next global.  (Two
# million lines: g/./s/$/x/ takes seconds on them, so the interrupt comes
# while it runs -- on the 4000 this had, it was over in 20 ms, before.)
seq 1 2000000 >"$T/big"
start "$T/big"
ready 2000000
printf 'g/./s/$/x/\n' >&3
sleep 0.5
kill -INT "$pid"; sleep 0.5
say '1g/^1x$/p'
say 'q!'
finish
if ! grep -qx Interrupt "$T/out"; then
	fail "the global was not interrupted while it ran: [$(head -3 "$T/out")]"
fi
printed=$(grep -v -e '^Interrupt$' -e '^$' -e '^2000000$' "$T/out")
if [ "$printed" != "1x" ]; then
	fail "an interrupted global left its marks set: 1g/^1x\$/p printed $(grep -c . <<<"$printed") lines: [$(head -3 <<<"$printed")]"
fi

# and undo works after it: 1d then u brings line 1 back
start "$T/big"
ready 2000000
printf 'g/./s/$/x/\n' >&3
sleep 0.5
kill -INT "$pid"; sleep 0.5
say '1d'
say 'u'
say '1p'
say 'q!'
finish
printed=$(grep -v -e '^Interrupt$' -e '^$' -e '^2000000$' "$T/out")
if ! grep -qx Interrupt "$T/out" || [ "$printed" != "1x" ]; then
	fail "undo after an interrupted command: 1d, u, 1p printed [$(head -3 <<<"$printed")]"
fi

# `-c vi` before the main loop: hands over to vi (not found here) instead
# of crashing
printf 'one\n' >"$T/f"
mkdir "$T/nopath"
echo q | env PATH="$T/nopath" "$EX_BIN" -s -c vi "$T/f" >"$T/out" 2>&1
status=$?
if [ "$status" -ge 128 ]; then
	fail "ex -c vi died of signal $((status - 128)): [$(cat "$T/out")]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_interrupt"

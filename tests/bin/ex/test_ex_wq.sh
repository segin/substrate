#!/bin/bash
# wq and xit write as write does, then quit -- and only if the write
# succeeded.  They ignored their file argument and wrote the current file
# instead (`wq f9` lost line 1 of the current file and never made f9),
# quit with `!` even when the write failed (the edit lost, status 0), and
# xit rewrote an unmodified buffer, or failed for want of a file name
# instead of quitting.
#
# Usage: test_ex_wq.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_wq.XXXXXX")
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

# a failed wq! does not quit: the commands after it still run
mkdir ro
printf 'r\n' >ro/f
chmod 555 ro
if [ "$(id -u)" -ne 0 ]; then
	out=$(printf 's/r/R/\nwq!\np\nq!\n' | "$EX_BIN" -s ro/f 2>&1)
	if ! grep -qx R <<<"$out"; then
		fail "wq! quit although its write failed: [$out]"
	fi
fi
chmod 755 ro

# wq file / x file write that file, and leave the current one alone
for c in wq x; do
	printf '1\n2\n3\n' >f
	rm -f f9
	out=$(printf '1d\n%s f9\nq!\n' "$c" | "$EX_BIN" -s f 2>&1)
	if [ "$(cat f9 2>/dev/null)" != "$(printf '2\n3')" ]; then
		fail "$c f9 did not write f9: [$(cat f9 2>/dev/null)] [$out]"
	fi
	if [ "$(cat f)" != "$(printf '1\n2\n3')" ]; then
		fail "$c f9 wrote the current file: [$(cat f)]"
	fi
	# the buffer is still modified: without ! it does not quit
	if ! grep -q 'No write since last change' <<<"$out"; then
		fail "$c f9 quit with the buffer still modified: [$out]"
	fi
done

# a range: 2,3wq part
printf '1\n2\n3\n' >f
out=$(printf '2,3wq part\n' | "$EX_BIN" -s f 2>&1)
if [ "$(cat part 2>/dev/null)" != "$(printf '2\n3')" ]; then
	fail "2,3wq part wrote [$(cat part 2>/dev/null)] [$out]"
fi

# x on an unmodified buffer only quits
printf 'keep\n' >u
touch -d '2001-01-01 00:00' u
before=$(stat -c %Y u)
printf 'x\n' | "$EX_BIN" -s u >/dev/null 2>&1
if [ "$(stat -c %Y u)" != "$before" ]; then
	fail "x rewrote an unmodified buffer"
fi
out=$(printf 'x\np\n' | "$EX_BIN" -s 2>&1)
status=$?
if [ "$status" -ne 0 ] || [ -n "$out" ]; then
	fail "x with no file and nothing to write: status $status [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_wq"

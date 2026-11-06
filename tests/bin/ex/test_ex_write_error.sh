#!/bin/bash
# A write that fails part-way -- a full disk, a file size limit -- is
# reported, leaves the file as it was and the buffer modified.  The writes
# were not checked: the half-written temporary file replaced the original,
# the buffer was marked written and `q` left without a word (status 0).
#
# The file size limit (with SIGXFSZ ignored, so the write fails with EFBIG
# instead of killing the editor) stands in for a full disk.
#
# Usage: test_ex_write_error.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_write_error.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

seq 1 2000 >"$T/big"
cp "$T/big" "$T/big.orig"

# ulimit -f counts 1024-byte blocks in bash: 4 KiB, under half the file
(
	trap '' XFSZ
	ulimit -f 4
	printf '1s/1/X/\nw\nq\n' | "$EX_BIN" -s "$T/big" >"$T/out" 2>&1
)
if ! cmp -s "$T/big" "$T/big.orig"; then
	fail "a failed :w changed the file ($(wc -c <"$T/big") bytes, was $(wc -c <"$T/big.orig"))"
fi
if ! grep -q 'File too large' "$T/out"; then
	fail "a failed :w was not reported: [$(cat "$T/out")]"
fi
# the buffer is still modified: q refuses.  (Given with -c: a command read
# from a pipe that fails ends the session, and q would never run.)
(
	trap '' XFSZ
	ulimit -f 4
	"$EX_BIN" -s -c '1s/1/X/' -c w -c q "$T/big" </dev/null >"$T/out" 2>&1
)
if ! grep -q 'No write since last change' "$T/out"; then
	fail "a failed :w left the buffer marked written: [$(cat "$T/out")]"
fi

# the same through append: the error is reported
: >"$T/log"
(
	trap '' XFSZ
	ulimit -f 4
	printf 'w >> %s\nq!\n' "$T/log" | "$EX_BIN" -s "$T/big" >"$T/out" 2>&1
)
if ! grep -q 'File too large' "$T/out"; then
	fail "a failed :w >> was not reported: [$(cat "$T/out")]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_write_error"

#!/bin/bash
# A file that cannot be read is reported, and the partial (empty) buffer is
# not written back over it without `!`.  An fopen failure was ignored and a
# read error taken for the end of the file: a file the user could write but
# not read opened as an empty buffer, without a word, and `:wq` emptied it.
#
# Usage: test_ex_read_error.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_read_error.XXXXXX")
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

if [ "$(id -u)" -eq 0 ]; then
	echo "test_ex_read_error: skip (root reads any file)"
	exit 0
fi

# write-only: reported, and :wq refuses to empty it
printf 'secret\n' >"$T/ur"
chmod 200 "$T/ur"
out=$(printf 'wq\nq!\n' | "$EX_BIN" -s "$T/ur" 2>&1)
chmod 600 "$T/ur"
if [ "$(cat "$T/ur")" != secret ]; then
	fail ":wq of a file that could not be read emptied it"
fi
if ! grep -q 'Permission denied' <<<"$out"; then
	fail "a file that could not be opened was not reported: [$out]"
fi
if ! grep -q 'not read in full' <<<"$out"; then
	fail ":wq of a file that could not be read was not refused: [$out]"
fi

# a file that does not exist is a new file, not an error
out=$(printf 'a\nnew\n.\nwq\n' | "$EX_BIN" -s "$T/newf" 2>&1)
if [ "$(cat "$T/newf" 2>/dev/null)" != new ] || [ -n "$out" ]; then
	fail "a new file: [$out], file [$(cat "$T/newf" 2>/dev/null)]"
fi

# a directory: read fails part-way (EISDIR), and is reported
mkdir "$T/dir"
out=$(printf 'q!\n' | "$EX_BIN" -s "$T/dir" 2>&1)
if ! grep -qi 'directory' <<<"$out"; then
	fail "a directory opened as a file was not reported: [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_read_error"

#!/bin/bash
# :w writes the file itself.  It wrote a temporary file and renamed it over
# the name: a symbolic link became a regular file (its target untouched), a
# hard link was split from the file, a file in a read-only directory or
# with a name near NAME_MAX could not be saved, nothing was fsynced, and
# fchmod before fchown let the fchown clear set-user-ID and set-group-ID.
# Now a link is followed, and a file is renamed over only where that is
# the same as writing it (a regular file, one link, the user's own);
# otherwise it is written in place, and put back if that fails.
#
# Usage: test_ex_write_inplace.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_inplace.XXXXXX")
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1

# a symbolic link: its target is written, and it stays a link
echo tgt >tgt
ln -s tgt lnk
printf 's/tgt/new/\nw\nq\n' | "$EX_BIN" -s lnk >/dev/null 2>&1
[ -L lnk ] || fail ":w turned a symbolic link into a regular file"
[ "$(cat tgt)" = new ] || fail ":w through a link left its target [$(cat tgt)]"

# a hard link: both names see the change
echo old >h1
ln h1 h2
printf 's/old/new/\nw\nq\n' | "$EX_BIN" -s h1 >/dev/null 2>&1
[ "$(cat h2)" = new ] || fail ":w split a hard link: h2 is [$(cat h2)]"
[ "$(stat -c %h h1)" = 2 ] || fail ":w left h1 with $(stat -c %h h1) links"

# a writable file in a read-only directory
if [ "$(id -u)" -ne 0 ]; then
	mkdir rod
	echo old >rod/f
	chmod 555 rod
	out=$(printf 's/old/new/\nw\nq\n' | "$EX_BIN" -s rod/f 2>&1)
	chmod 755 rod
	[ "$(cat rod/f)" = new ] || fail ":w of a file in a read-only directory: [$out]"
fi

# a name too long for name.tmp.XXXXXX beside it
long=$(printf 'n%.0s' $(seq 1 250))
echo old >"$long"
out=$(printf 's/old/new/\nw\nq\n' | "$EX_BIN" -s "$long" 2>&1)
[ "$(cat "$long")" = new ] || fail ":w of a 250-byte name: [$out]"

# a write in place that fails puts the file back
printf 'a\n' >p1
ln p1 p2
seq 1 2000 >big
(
	trap '' XFSZ
	ulimit -f 4
	printf 'r big\nw\nq!\n' | "$EX_BIN" -s p1 >out 2>&1
)
[ "$(cat p1)" = a ] || fail "a failed in-place write left p1 $(wc -c <p1) bytes"
grep -q 'File too large' out || fail "a failed in-place write was not reported: [$(cat out)]"

# a file the user may not write is not replaced, though the directory
# would let a renamed copy take its place
if [ "$(id -u)" -ne 0 ]; then
	echo old >ro
	chmod 444 ro
	out=$(printf 's/old/new/\nw\nq!\n' | "$EX_BIN" -s ro 2>&1)
	[ "$(cat ro)" = old ] || fail ":w replaced a file without write permission"
	grep -q 'Permission denied' <<<"$out" || fail ":w of a 0444 file was not refused: [$out]"
fi

# modes: set-user-ID, set-group-ID, 0600 survive :w
for m in 4755 2755 600; do
	echo x >m$m
	chmod $m m$m
	printf 's/x/y/\nw\nq\n' | "$EX_BIN" -s m$m >/dev/null 2>&1
	[ "$(stat -c %a m$m)" = $m ] || fail ":w changed mode $m to $(stat -c %a m$m)"
done

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_write_inplace"

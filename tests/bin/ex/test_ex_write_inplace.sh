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

# owner and group survive :w.  A file of one of the user's other groups
# does it; as root, one of another user's.  Otherwise there is no such file
# to make, and it is skipped.
echo x >og
other=
for g in $(id -G); do
	[ "$g" != "$(id -g)" ] && other=$g && break
done
if [ "$(id -u)" = 0 ]; then
	chown 65534:65534 og
elif [ -n "$other" ]; then
	chgrp "$other" og
else
	og=
fi
if [ -n "${og-x}" ]; then
	before=$(stat -c %u:%g og)
	printf 's/x/y/\nw\nq\n' | "$EX_BIN" -s og >/dev/null 2>&1
	[ "$(cat og)" = y ] || fail ":w did not write og: [$(cat og)]"
	[ "$(stat -c %u:%g og)" = "$before" ] ||
		fail ":w changed owner:group $before to $(stat -c %u:%g og)"
else
	echo "SKIP: owner and group (no other group, not root)"
fi

# the data is on stable storage before :w succeeds: fsync (or fdatasync)
# of the file, before any rename over its name.  Seen by strace, where
# there is one.
if command -v strace >/dev/null 2>&1; then
	echo x >fs
	strace -f -o trace -e trace=fsync,fdatasync,rename,renameat,renameat2 \
		"$EX_BIN" -s -c 's/x/y/' -c w -c q fs </dev/null >/dev/null 2>&1
	sync_at=$(grep -n -m1 -E 'f(data)?sync\(' trace | cut -d: -f1)
	rename_at=$(grep -n -m1 -E 'rename(at2?)?\(' trace | cut -d: -f1)
	if [ "$(cat fs)" != y ] || [ -z "$sync_at" ]; then
		fail ":w did not fsync: [$(cat trace)]"
	elif [ -n "$rename_at" ] && [ "$rename_at" -lt "$sync_at" ]; then
		fail ":w renamed before it fsynced: [$(cat trace)]"
	fi
else
	echo "SKIP: fsync (no strace)"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_write_inplace"

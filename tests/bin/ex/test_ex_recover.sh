#!/bin/bash
# Recover files are the user's own: in <directory>/exvi-<uid> (mode 0700),
# created afresh with mode 0600, never through a symbolic link, and only
# one the user owns is recovered.  They were `<file>.recover` beside the
# file, opened with fopen("w"): a symbolic link planted under that name in a
# shared directory had the buffer written through it, a 0600 file was
# preserved world-readable, and any `<file>.recover` there was loaded as
# recovered text.  And every :w of a file deleted a `<file>.recover` beside
# it, whoever made it -- now only the recover file this session wrote or
# recovered from is removed.
#
# Usage: test_ex_recover.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_recover.XXXXXX")
pid=
trap '[ -n "$pid" ] && kill -KILL "$pid" 2>/dev/null; rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

D="$T/rd"
R="$D/exvi-$(id -u)"
mkdir "$D"
cd "$T" || exit 1
ex() { "$EX_BIN" -s -c "set dir=$D" "$@"; }

# :preserve does not write through a link beside the file, and keeps 0600
echo victim >victim
ln -s victim v.recover
echo vv >v
chmod 600 v
printf 's/vv/OVER/\npreserve\nq!\n' | ex v >out 2>&1
if [ "$(cat victim)" != victim ]; then
	fail ":preserve wrote through v.recover: victim is [$(cat victim)]"
fi
rec=$(ls "$R" 2>/dev/null | head -1)
if [ -z "$rec" ]; then
	fail ":preserve made no recover file in $R: [$(cat out)]"
else
	[ "$(stat -c %a "$R")" = 700 ] || fail "recover directory mode $(stat -c %a "$R")"
	[ "$(stat -c %a "$R/$rec")" = 600 ] || fail "recover file mode $(stat -c %a "$R/$rec")"
fi

# :recover gets it back
out=$(printf 'recover\n1p\nq!\n' | ex v 2>&1)
if ! grep -qx OVER <<<"$out"; then
	fail ":recover did not get the preserved text: [$out]"
fi

# but not over unwritten changes (an error, which ends a batch ex)...
out=$(printf 's/vv/MINE/\nrecover\n1p\nq!\n' | ex v 2>&1)
if grep -qx OVER <<<"$out" || ! grep -q "No write since last change" <<<"$out"; then
	fail ":recover replaced a modified buffer: [$out]"
fi
# ...which :recover! discards
out=$(printf 's/vv/MINE/\nrecover!\n1p\nq!\n' | ex v 2>&1)
if ! grep -qx OVER <<<"$out"; then
	fail ":recover! did not recover over a modified buffer: [$out]"
fi

# SIGTERM saves there too, and not through the link
mkfifo in
# the binary itself, not the function: $! must be ex's pid
"$EX_BIN" -s -c "set dir=$D" v <in >out 2>&1 &
pid=$!
exec 3>in
printf 's/vv/TERMED/\n' >&3
sleep 0.5
kill -TERM "$pid"
wait "$pid"
pid=
exec 3>&-
if [ "$(cat victim)" != victim ]; then
	fail "SIGTERM wrote through v.recover: victim is [$(cat victim)]"
fi
out=$(printf 'recover\n1p\nq!\n' | ex v 2>&1)
if ! grep -qx TERMED <<<"$out"; then
	fail "the SIGTERM save could not be recovered: [$out]"
fi

# a link in the recover directory is not followed, and a directory others
# can read is not used
rm -f "$R"/*
echo planted >planted
ln -s "$T/planted" "$R/$rec"
out=$(printf 'recover\n1p\nq!\n' | ex v 2>&1)
if grep -qx planted <<<"$out"; then
	fail ":recover followed a link in the recover directory"
fi

# :preserve onto a symbolic or hard link planted at its own image's name
# writes neither one's file: the name is made afresh, a file of its own
for kind in symbolic hard; do
	rm -f "$R"/*
	echo planted >planted
	if [ "$kind" = symbolic ]; then
		ln -s "$T/planted" "$R/$rec"
	else
		ln "$T/planted" "$R/$rec"
	fi
	printf 's/^/Y/\npreserve\nq!\n' | ex v >/dev/null 2>&1
	if [ "$(cat planted)" != planted ]; then
		fail ":preserve wrote through a $kind link at its image's name: [$(cat planted)]"
	fi
	if [ -L "$R/$rec" ] || [ ! -f "$R/$rec" ] || [ "$(stat -c %h "$R/$rec")" != 1 ]; then
		fail ":preserve left a $kind link at its image's name"
	fi
done

# a recover file another user owns is not recovered.  Making one needs
# root (chown); others skip it.
if [ "$(id -u)" = 0 ]; then
	rm -f "$R"/*
	printf 's/^/Z/\npreserve\nq!\n' | ex v >/dev/null 2>&1
	chown 65534 "$R/$rec"
	out=$(printf 'recover\n1p\nq!\n' | ex v 2>&1)
	if grep -q '^Z' <<<"$out"; then
		fail ":recover took a recover file another user owns: [$out]"
	fi
else
	echo "SKIP: a recover file another user owns (needs root)"
fi
rm -f "$R"/*
printf 's/vv/X/\npreserve\nq!\n' | ex v >/dev/null 2>&1
chmod 750 "$R"
out=$(printf 'recover\n1p\nq!\n' | ex v 2>&1)
if grep -qx X <<<"$out"; then
	fail ":recover used a recover directory open to its group"
fi
chmod 700 "$R"

# :w leaves alone what this session did not make: a user's notes.recover
# beside the file, and a recover file from an earlier session
echo keep >notes.recover
echo n >notes
printf 's/n/m/\npreserve\nq!\n' | ex notes >/dev/null 2>&1
before=$(ls "$R" | wc -l)
printf 's/m/k/\nw\nq\n' | ex notes >/dev/null 2>&1
if [ ! -e notes.recover ]; then
	fail ":w deleted notes.recover beside the file"
fi
if [ "$(ls "$R" | wc -l)" -ne "$before" ]; then
	fail ":w deleted a recover file an earlier session left"
fi
# ... but a session that recovered it removes it when it writes
printf 'recover\nw\nq\n' | ex notes >/dev/null 2>&1
if [ "$(ls "$R" | wc -l)" -ne $((before - 1)) ]; then
	fail ":w after :recover left the recover file"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_recover"

#!/bin/bash
# A tags entry's third field is an address -- a line number or a search
# pattern -- and nothing more.  :tag passed it to the ex command
# interpreter, so a tags file (data, often from the project being edited)
# could run any ex command, `!` shell commands included.
#
# Usage: test_ex_tag.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_tag.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1
printf 'one\ntwo\nthree\nbaz\n' >f.c
printf 'bar\tf.c\t3\nbaz\tf.c\t/^baz$/\nevil\tf.c\t!touch PWNED\nwipe\tf.c\td\n' >tags

out=$(printf 'tag evil\nq!\n' | "$EX_BIN" -s 2>&1)
if [ -e PWNED ]; then
	fail "a tags entry ran a shell command"
fi
if ! grep -q 'Bad tag address' <<<"$out"; then
	fail "a tags entry that is not an address was not reported: [$out]"
fi

# (with -c: a command read from a pipe that fails ends the session, and
# w g.c would never run)
out=$("$EX_BIN" -s -c 'tag wipe' -c 'w g.c' -c 'q!' </dev/null 2>&1)
if ! cmp -s f.c g.c; then
	fail "a tags entry ran an ex command: [$(cat g.c 2>/dev/null)]"
fi

# (ex shows the line it lands on)
out=$(printf 'tag bar\n.=\ntag baz\n.=\nq!\n' | "$EX_BIN" -s 2>&1)
if [ "$out" != "$(printf 'three\n3\nbaz\n4')" ]; then
	fail "tags entries with a line number and a pattern: [$out]"
fi

# the tags option's files are searched in turn, not only the first that
# opens; and with no file having the tag, "Tag not found" once
printf 'other\tf.c\t1\n' >first.tags
printf 'later\tf.c\t2\n' >second.tags
out=$(printf 'set tags=missing.tags,first.tags,second.tags\ntag later\n.=\nq!\n' | "$EX_BIN" -s 2>&1)
if [ "$out" != "$(printf 'two\n2')" ]; then
	fail "a tag in the second tags file: [$out]"
fi
out=$(printf 'set tags=first.tags,second.tags\ntag nowhere\nq!\n' | "$EX_BIN" -s 2>&1)
if [ "$(grep -c 'Tag not found' <<<"$out")" -ne 1 ]; then
	fail "a tag in no tags file: [$out]"
fi

# the error paths free what they took: a tag in no file, a tags file that
# cannot be read, a bad address, a tag found with the buffer modified (the
# one that leaked the tags file's name).  (-c, so each error does not end
# the session; the ASan build's LeakSanitizer reports at exit, failing it.)
printf 'unread\tf.c\t1\n' >unread.tags
chmod 000 unread.tags
out=$("$EX_BIN" -s -c 'set tags=unread.tags,first.tags' -c 'tag nowhere' \
	-c 'tag unread' -c 'set tags=tags' -c 'tag evil' -c '1s/^/x/' -c 'tag bar' \
	-c 'q!' f.c </dev/null 2>&1)
status=$?
chmod 600 unread.tags
if grep -q 'Sanitizer' <<<"$out" || [ "$status" -ne 0 ]; then
	fail ":tag's error paths: status $status [$(grep -A3 'Sanitizer' <<<"$out" | head -5)]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_tag"

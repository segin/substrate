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

out=$(printf 'tag wipe\nw g.c\nq!\n' | "$EX_BIN" -s 2>&1)
if ! cmp -s f.c g.c; then
	fail "a tags entry ran an ex command: [$(cat g.c 2>/dev/null)]"
fi

# (ex shows the line it lands on)
out=$(printf 'tag bar\n.=\ntag baz\n.=\nq!\n' | "$EX_BIN" -s 2>&1)
if [ "$out" != "$(printf 'three\n3\nbaz\n4')" ]; then
	fail "tags entries with a line number and a pattern: [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_tag"

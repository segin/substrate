#!/bin/bash
# set's reports (POSIX ex, set):
#  - set with no argument lists the options that differ from their defaults
#    (it listed showmode and wrapscan, at their defaults);
#  - set all lists every option, off booleans included;
#  - set noX? reports X as it is (set noai? with ai off printed autoindent);
#  - POSIX's options are all there (shiftwidth, report, window, ...).
#
# Usage: test_ex_set.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_set.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

cd "$T" || exit 1
echo x >f
run() { printf '%s\nq!\n' "$1" | env -u EXINIT "$EX_BIN" -s f 2>&1; }

out=$(run 'set')
[ -z "$out" ] || fail "set at the defaults listed [$out]"

out=$(run 'set ai|set sw=4|set')
[ "$out" = "$(printf 'autoindent\nshiftwidth=4')" ] || fail "set after ai, sw=4: [$out]"

out=$(run 'set noai?')
[ "$out" = noautoindent ] || fail "set noai? with ai off: [$out]"
out=$(run 'set ai|set noai?')
[ "$out" = autoindent ] || fail "set noai? with ai on: [$out]"

out=$(run 'set all')
for want in noautoindent autoprint nolist shiftwidth=8 report=5 wrapscan nowriteany; do
	grep -qx "$want" <<<"$out" || fail "set all has no [$want]"
done

out=$(run 'set ts=0')
grep -q "Bad tabstop" <<<"$out" || fail "set ts=0: [$out]"

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_set"

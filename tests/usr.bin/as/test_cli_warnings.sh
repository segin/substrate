#!/bin/sh
# The options that say what becomes of a warning: --warn, --no-warn, -W
# and --fatal-warnings.
#
# A source that draws no warning assembles alike under all of them and
# shows nothing, so every case here is a source that draws one: a
# sixteen-bit number stored in a byte, which is assembled truncated and
# remarked on.  What each option then does is GNU as 2.46's.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
AS=${AS:-"$top/usr.bin/as/as"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work" || exit 1

fail=0
bad() { echo "FAIL: $*"; fail=1; }

text() {
    objcopy -O binary --only-section=.text "$1" text.bin 2>/dev/null || return 1
    od -An -v -tx1 text.bin | tr -d ' \n'
}

printf '\t.text\n\tmovb $0x1234, %%al\n\tret\n' > warn.s
printf '\t.text\n\tmovb $0x34, %%al\n\tret\n' > quiet.s
CODE=b034c3

# run OPTION...: assemble warn.s; leaves $rc, t.o or none, and err.
run() {
    rm -f t.o
    "$AS" -32 "$@" -o t.o warn.s > out 2> err
    rc=$?
}

# warned: how many lines of err are a warning that names line 2.
warned() {
    grep -ci 'warning' err
}

# With no option: assembled, and one warning, which gives the line.
run
[ "$rc" -eq 0 ] || bad "a warning alone makes the exit status $rc"
[ "$(text t.o)" = "$CODE" ] || bad "the truncated byte: '$(text t.o)', not '$CODE'"
[ "$(warned)" -eq 1 ] || bad "one warning is drawn and $(warned) are written"
grep -i 'warning' err | grep -q ':2:' || bad "the warning does not give line 2: $(grep -i warning err | head -1)"
[ -s out ] && bad "the warning is on the standard output"

# The control: the source that needs no truncating draws none.
rm -f t.o
"$AS" -32 -o t.o quiet.s > out 2> err || bad "the source with no fault is refused"
[ -s err ] && bad "the source with no fault draws: $(head -1 err)"

# --warn is the default said aloud.
run --warn
[ "$rc" -eq 0 ] && [ "$(text t.o)" = "$CODE" ] || bad "--warn: exit $rc, code '$(text t.o)'"
[ "$(warned)" -eq 1 ] || bad "--warn: $(warned) warnings written, not 1"

# --no-warn and -W: assembled the same, and nothing said.
for opt in --no-warn -W; do
    run "$opt"
    [ "$rc" -eq 0 ] || bad "$opt: exit $rc"
    [ "$(text t.o)" = "$CODE" ] || bad "$opt: code '$(text t.o)', not '$CODE'"
    [ -s err ] && bad "$opt: still says: $(head -1 err)"
done

# --fatal-warnings: the warning is written, the run fails, and there is
# no object.
run --fatal-warnings
[ "$rc" -eq 1 ] || bad "--fatal-warnings: exit $rc for a source that draws a warning, not 1"
[ -e t.o ] && bad "--fatal-warnings: an object is written"
[ "$(warned)" -ge 1 ] || bad "--fatal-warnings: the warning is not written"

# ... and changes nothing for a source that draws none.
rm -f t.o
"$AS" -32 --fatal-warnings -o t.o quiet.s > out 2> err || bad "--fatal-warnings: a source with no fault is refused"
[ "$(text t.o)" = "$CODE" ] || bad "--fatal-warnings: a source with no fault: '$(text t.o)'"

[ "$fail" -eq 0 ] && echo "ok: --warn, --no-warn, -W, --fatal-warnings"
exit "$fail"

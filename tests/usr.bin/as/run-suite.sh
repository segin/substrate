#!/bin/sh
# run-suite.sh - run every test of the assembler and say how each ended.
#
# Each test_*.sh here is a test.  The driver-level ones run an assembler
# given as $AS; this script builds one for the host in a directory of its
# own and hands it to them, so nothing is built in the source tree and no
# cross toolchain is needed.
#
# Not every test passes: the assembler has known defects (docs/as-audit.md)
# and the list of them is worked through in docs/as-tasks.md.  A test that
# is expected to fail is named in xfail.list with the reason; it is still
# run.  The suite fails when a test not in that list fails, and also when
# one in the list passes -- so that a mended defect has its line removed in
# the commit that mends it.
#
# Usage: run-suite.sh [-v] [test_name.sh ...]
#   -v   print each failing test's last lines
# Exit status: 0 when every test ended as expected.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

verbose=0
if [ "${1:-}" = "-v" ]; then verbose=1; shift; fi

${CC:-cc} -O1 -w -o "$work/as" \
    -idirafter "$top/include" -idirafter "$top/sys" -idirafter "$top/sys/include" \
    -I"$top/usr.lib/elfobj/src" "$top"/usr.bin/as/*.c "$top"/usr.lib/elfobj/src/*.c 2> "$work/build.err" ||
    { echo "FAIL: the assembler does not build for the host"; head -20 "$work/build.err"; exit 1; }
AS="$work/as"
export AS

cd "$here" || exit 1
if [ $# -gt 0 ]; then tests="$*"; else tests=$(ls test_*.sh | sort); fi

pass=0; xfail=0; bad=0; skipped=0
for t in $tests; do
    expected=pass
    if grep -q "^$t[[:space:]]" xfail.list 2>/dev/null; then expected=fail; fi
    # A reason that begins SKIP: the test is not run at all, because
    # running it changes the source tree.
    if grep -q "^$t[[:space:]][[:space:]]*SKIP" xfail.list 2>/dev/null; then
        skipped=$((skipped + 1)); echo "skip   $t"; continue
    fi
    if TMPDIR="$work" timeout "${TEST_TIMEOUT:-600}" sh "./$t" > "$work/out" 2>&1; then got=pass; else got=fail; fi
    case "$expected/$got" in
    pass/pass) pass=$((pass + 1)); echo "ok     $t" ;;
    fail/fail) xfail=$((xfail + 1)); echo "xfail  $t" ;;
    pass/fail) bad=$((bad + 1)); echo "FAIL   $t"; tail -5 "$work/out" | sed 's/^/         /' ;;
    fail/pass) bad=$((bad + 1)); echo "XPASS  $t (passes: take it out of xfail.list)" ;;
    esac
    if [ "$verbose" = 1 ] && [ "$got" = fail ] && [ "$expected" = fail ]; then
        tail -3 "$work/out" | sed 's/^/         /'
    fi
done

echo "assembler tests: $pass passed, $xfail failed as expected, $skipped not run, $bad not as expected"
[ "$bad" -eq 0 ]

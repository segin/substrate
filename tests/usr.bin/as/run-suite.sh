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
# The tests are run several at a time -- as many as there are processors,
# or $JOBS -- each with a temporary directory of its own, and reported in
# the order of their names when all have ended.
#
# Usage: run-suite.sh [-v] [test_name.sh ...]
#   -v   print each failing test's last lines
# Exit status: 0 when every test ended as expected.
set -u

here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../../.." && pwd)

# run-suite.sh --one test_name.sh: one test, for the script itself; how it
# ended goes to a file beside its output.
if [ "${1:-}" = "--one" ]; then
    t=$2
    mkdir -p "$SUITE_WORK/tmp/$t"
    cd "$here" || exit 1
    if TMPDIR="$SUITE_WORK/tmp/$t" timeout "${TEST_TIMEOUT:-600}" sh "./$t" > "$SUITE_WORK/out/$t" 2>&1; then
        echo pass > "$SUITE_WORK/res/$t"
    else
        echo fail > "$SUITE_WORK/res/$t"
    fi
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

verbose=0
if [ "${1:-}" = "-v" ]; then verbose=1; shift; fi

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

# compile_into DIR COMPILER FILE...: each file to an object in DIR, several
# compilers at a time.  The compiler is a command line, not one word.
SUITE_INC="-idirafter $top/include -idirafter $top/sys -idirafter $top/sys/include -I$top/usr.lib/elfobj/src"
export SUITE_INC
compile_into() {
    SUITE_OBJ=$1 SUITE_CC=$2
    export SUITE_OBJ SUITE_CC
    shift 2
    mkdir "$SUITE_OBJ" || return 1
    # shellcheck disable=SC2016
    printf '%s\n' "$@" |
        xargs -P "$jobs" -n 1 sh -c '$SUITE_CC -O1 -w $SUITE_INC -c -o "$SUITE_OBJ/$(basename "$0" .c).o" "$0"'
}

# shellcheck disable=SC2086
{
    compile_into "$work/o-elfobj" "${CC:-cc}" "$top"/usr.lib/elfobj/src/*.c &&
    compile_into "$work/o-as" "${CC:-cc}" "$top"/usr.bin/as/*.c &&
    ${CC:-cc} -o "$work/as" "$work"/o-as/*.o "$work"/o-elfobj/*.o
} 2> "$work/build.err" ||
    { echo "FAIL: the assembler does not build for the host"; head -20 "$work/build.err"; exit 1; }
AS="$work/as"
export AS

# Some of the tests link what they assemble, with substrate's linker.
# shellcheck disable=SC2086
{
    compile_into "$work/o-ld" "${CC:-cc}" "$top"/usr.bin/ld/*.c &&
    ${CC:-cc} -o "$work/ld" "$work"/o-ld/*.o "$work"/o-elfobj/*.o
} 2> "$work/build.err" ||
    { echo "FAIL: the linker does not build for the host"; head -20 "$work/build.err"; exit 1; }
LD="$work/ld"
export LD

# And some unit tests link the object library itself.  They compile with
# plain `cc`, whatever $CC is, so the library is built with it too: $CC
# may be `gcc -m32`, to try the assembler and linker as 32-bit programs.
{
    compile_into "$work/elfobj" cc "$top"/usr.lib/elfobj/src/*.c &&
    ar rcs "$work/libelfobj.a" "$work"/elfobj/*.o
} 2> "$work/build.err" ||
    { echo "FAIL: libelfobj does not build for the host"; head -20 "$work/build.err"; exit 1; }
ELFOBJ_A="$work/libelfobj.a"
export ELFOBJ_A

cd "$here" || exit 1
if [ $# -gt 0 ]; then tests="$*"; else tests=$(ls test_*.sh | sort); fi

# A reason that begins SKIP: the test is not run at all, because running
# it changes the source tree.
is_skipped() {
    grep -q "^$1[[:space:]][[:space:]]*SKIP" xfail.list 2>/dev/null
}

SUITE_WORK=$work
export SUITE_WORK
mkdir "$work/tmp" "$work/out" "$work/res"
for t in $tests; do
    is_skipped "$t" || echo "$t"
done | xargs -P "$jobs" -n 1 sh "$here/run-suite.sh" --one

pass=0; xfail=0; bad=0; skipped=0
for t in $tests; do
    expected=pass
    if grep -q "^$t[[:space:]]" xfail.list 2>/dev/null; then expected=fail; fi
    if is_skipped "$t"; then
        skipped=$((skipped + 1)); echo "skip   $t"; continue
    fi
    got=$(cat "$work/res/$t" 2>/dev/null || echo fail)
    case "$expected/$got" in
    pass/pass) pass=$((pass + 1)); echo "ok     $t" ;;
    fail/fail) xfail=$((xfail + 1)); echo "xfail  $t" ;;
    pass/fail) bad=$((bad + 1)); echo "FAIL   $t"; tail -5 "$work/out/$t" | sed 's/^/         /' ;;
    fail/pass) bad=$((bad + 1)); echo "XPASS  $t (passes: take it out of xfail.list)" ;;
    esac
    if [ "$verbose" = 1 ] && [ "$got" = fail ] && [ "$expected" = fail ]; then
        tail -3 "$work/out/$t" | sed 's/^/         /'
    fi
done

echo "assembler tests: $pass passed, $xfail failed as expected, $skipped not run, $bad not as expected"
[ "$bad" -eq 0 ]

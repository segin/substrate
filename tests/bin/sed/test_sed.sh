#!/bin/sh
# test_sed.sh -- behaviour tests for bin/sed, run against the host build.
#
# Builds bin/sed/sed_host (NATIVE_BUILD=1, static libregex) and runs each
# case under a timeout, comparing standard output and the exit status.
#
#   sh tests/bin/sed/test_sed.sh

TOP=$(cd "$(dirname "$0")/../../.." && pwd)
SED="$TOP/bin/sed/sed_host"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

(cd "$TOP/bin/sed" && rm -f sed_host && make NATIVE_BUILD=1 sed_host >/dev/null) || exit 1

# case <name> <input> <expected-output> <expected-status> <sed args...>
# Input and expected output are printf formats.
case_() {
    name=$1 input=$2 want=$3 want_rc=$4
    shift 4
    printf "$input" > "$T/in"
    printf "$want" > "$T/want"
    timeout 5 "$SED" "$@" < "$T/in" > "$T/out" 2> "$T/err"
    rc=$?
    if [ "$rc" = 124 ]; then
        echo "FAIL $name: timed out"
        fails=$((fails + 1))
    elif [ "$rc" != "$want_rc" ] || ! cmp -s "$T/want" "$T/out"; then
        echo "FAIL $name: exit $rc (want $want_rc), output:"
        od -c "$T/out" | head -5
        fails=$((fails + 1))
    else
        echo "ok   $name"
    fi
}

# D with no newline in the pattern space acts as d (POSIX).
case_ "D without newline"        'a\n'       ''          0 D
case_ "D without newline, 2 lines" 'a\nb\n'  ''          0 D
case_ "D with newline (\$!N;P;D)" 'a\nb\nc\n' 'a\nb\nc\n' 0 '$!N;P;D'
case_ "D with newline (\$!N;D)"  'a\nb\nc\n' ''          0 '$!N;D'

if [ "$fails" -eq 0 ]; then
    echo "Result: PASSED"
else
    echo "Result: FAILED"
    exit 1
fi

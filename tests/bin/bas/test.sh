#!/bin/sh
# test.sh -- bas(1) compiler error handling (host build).
#
# Each case feeds a program on standard input, with a timeout, and checks
# the output for an expected string and the absence of another.

TOP=$(cd "$(dirname "$0")/../../.." && pwd)
BIN="$TOP/bin/bas/bas"
T=$(mktemp -d)
trap 'rm -rf "$T"; (cd "$TOP/bin/bas" && make NATIVE_BUILD=1 clean >/dev/null)' EXIT
fails=0

(cd "$TOP/bin/bas" && make NATIVE_BUILD=1 clean all >/dev/null) || exit 1

# case_ <name> <program> <must-contain> <must-not-contain>
case_() {
    name=$1 prog=$2 want=$3 unwanted=$4
    printf '%s\nlist\nquit\n' "$prog" > "$T/in"
    timeout 5 "$BIN" < "$T/in" > "$T/out" 2>&1
    rc=$?
    if [ "$rc" -ge 124 ]; then
        echo "FAIL $name: exit $rc (hang or crash)"
        fails=$((fails + 1))
    elif ! grep -qF -- "$want" "$T/out"; then
        echo "FAIL $name: no \"$want\" in output:"
        head -c 300 "$T/out"; echo
        fails=$((fails + 1))
    elif [ -n "$unwanted" ] && grep -qF -- "$unwanted" "$T/out"; then
        echo "FAIL $name: \"$unwanted\" in output"
        fails=$((fails + 1))
    else
        echo "ok   $name"
    fi
}

# A PRINT item the expression parser cannot consume is a syntax error, and
# the line is not stored.
case_ "PRINT )"        '10 PRINT )'        'syntax error'  '10 PRINT'
case_ "PRINT 1 )"      '10 PRINT 1 )'      'syntax error'  '10 PRINT'
case_ "good line kept" '10 PRINT 1'        '10 PRINT 1'    'syntax error'

if [ "$fails" -eq 0 ]; then
    echo "Result: PASSED"
else
    echo "Result: FAILED"
    exit 1
fi

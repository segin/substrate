#!/bin/sh
#
# run_tests.sh - behaviour tests for seq(1).
#
#   sh run_tests.sh [path/to/seq]
#
# Prints "ok"/"FAIL" per case and a final "Result:" line; exits non-zero
# if any case failed.

HERE=$(cd "$(dirname "$0")" && pwd)
SEQ=${1:-$HERE/../../../usr.bin/seq/seq}
case $SEQ in /*) ;; *) SEQ=$(pwd)/$SEQ ;; esac

[ -x "$SEQ" ] || { echo "run_tests.sh: no seq at $SEQ" >&2; exit 2; }

passed=0
failed=0

# check NAME EXPECTED ARGS... -- output with newlines turned into spaces
check() {
    name=$1 want=$2
    shift 2
    got=$("$SEQ" "$@" 2>/dev/null | tr '\n' ' ')
    if [ "$got" = "$want" ]; then
        echo "ok: $name"
        passed=$((passed + 1))
    else
        echo "FAIL: $name: expected '$want', got '$got'"
        failed=$((failed + 1))
    fi
}

# fails NAME ARGS... -- must exit non-zero with a message on stderr
fails() {
    name=$1
    shift
    if "$SEQ" "$@" > /dev/null 2> err.$$ || [ ! -s err.$$ ]; then
        echo "FAIL: $name: expected an error"
        failed=$((failed + 1))
    else
        echo "ok: $name"
        passed=$((passed + 1))
    fi
    rm -f err.$$
}

check "last only"                "1 2 3 "            3
check "first last"               "4 5 6 "            4 6
check "first incr last"          "1 4 7 10 "         1 3 10
check "counting down"            "5 3 1 -1 "         5 -2 -1
check "negative operands"        "-3 -2 -1 "         -3 -1
check "empty range"              ""                  5 1
check "zero"                     "0 "                0 0
check "decimals follow input"    "0.50 0.75 1.00 "   0.5 0.25 1.00
check "no drift over 0.1 steps"  "0.0 0.1 0.2 0.3 "  0 0.1 0.3
check "-s separator"             "1,2,3 "            -s , 1 3
check "-s attached"              "1:2 "              -s: 1 2
check "-w equal width"           "08 09 10 "         -w 8 10
check "-w with negatives"        "-2 -1 00 01 "      -w -2 1
check "-f format"                "1.00 2.00 "        -f %.2f 1 2
check "-f with text"             "x=1 x=2 "          -f 'x=%g' 1 2
check "-- ends options"          "-2 -1 "            -- -2 -1

fails "zero increment"           1 0 3
fails "invalid number"           abc
fails "format with %d"           -f %d 1 2
fails "format with two conversions" -f '%g%g' 1 2
fails "no operands"
fails "too many operands"        1 2 3 4
fails "unknown option"           -q 1

echo
echo "Result: $([ $failed -eq 0 ] && echo PASS || echo FAIL) ($passed passed, $failed failed)"
[ $failed -eq 0 ]

#!/bin/sh
# test.sh -- printf(1) numeric-argument parsing (host build).
#
# A numeric argument that is a lone ' or " (the character-code form with no
# character) made the parser read past the end of the argument -- into the
# next argv string, so the read itself goes unnoticed -- and pass as a
# valid 0.  It must be diagnosed like any other non-number: print 0, exit 1.

TOP=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

cc -g -o "$T/printf" "$TOP/bin/printf/printf.c" || exit 1

# case_ <name> <want-stdout> <want-status> <args...>
case_() {
    name=$1 want=$2 want_rc=$3
    shift 3
    "$T/printf" "$@" > "$T/out" 2> "$T/err"
    rc=$?
    got=$(cat "$T/out")
    if [ "$rc" != "$want_rc" ] || [ "$got" != "$want" ]; then
        echo "FAIL $name: exit $rc (want $want_rc), output '$got' (want '$want')"
        fails=$((fails + 1))
    else
        echo "ok   $name"
    fi
}

case_ "%d with lone '"   0   1 '%d' "'"
case_ '%d with lone "'   0   1 '%d' '"'
case_ "%u with lone '"   0   1 '%u' "'"
case_ "%x with lone '"   0   1 '%x' "'"
case_ "%d 'a is 97"      97  0 '%d' "'a"
case_ "%u \"A is 65"     65  0 '%u' '"A'

if [ "$fails" -eq 0 ]; then
    echo "Result: PASSED"
else
    echo "Result: FAILED"
    exit 1
fi

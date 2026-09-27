#!/bin/sh
# test.sh -- sleep(1) operand handling (host build).
#
# sleep converted its operand with atoi() and passed it to sleep(3): a
# negative value became a huge unsigned one ("sleep -1" never returned),
# garbage was taken as 0, and fractions were dropped.

TOP=$(cd "$(dirname "$0")/../../.." && pwd)
BIN="$TOP/bin/sleep/sleep"
fails=0

(cd "$TOP/bin/sleep" && make NATIVE_BUILD=1 clean all >/dev/null) || exit 1

now_ms() { date +%s%3N; }

# expect <name> <want-status> <min-ms> <max-ms> <args...>
expect() {
    name=$1 want=$2 min=$3 max=$4
    shift 4
    t0=$(now_ms)
    timeout 5 "$BIN" "$@" >/dev/null 2>&1
    rc=$?
    ms=$(( $(now_ms) - t0 ))
    if [ "$rc" != "$want" ] || [ "$ms" -lt "$min" ] || [ "$ms" -gt "$max" ]; then
        echo "FAIL $name: exit $rc (want $want) after ${ms} ms (want $min-$max)"
        fails=$((fails + 1))
    else
        echo "ok   $name (${ms} ms)"
    fi
}

expect "sleep -1 fails at once"   1 0 1000 -1
expect "sleep x fails at once"    1 0 1000 x
expect "sleep 1x fails at once"   1 0 1000 1x
expect "sleep '' fails at once"   1 0 1000 ''
expect "sleep 0.2 sleeps 0.2 s"   0 180 1500 0.2
expect "sleep .1 sleeps 0.1 s"    0 80 1500 .1
expect "sleep 1 sleeps 1 s"       0 950 2500 1
expect "sleep 0 returns at once"  0 0 1000 0

(cd "$TOP/bin/sleep" && make NATIVE_BUILD=1 clean >/dev/null)

if [ "$fails" -eq 0 ]; then
    echo "Result: PASSED"
else
    echo "Result: FAILED"
    exit 1
fi

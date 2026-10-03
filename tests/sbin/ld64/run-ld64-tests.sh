#!/bin/sh
# run-ld64-tests.sh - run the /sbin/ld64.so tests on the target (x86_64
# kernel).  Expects the programs of tests/sbin/ld64 in $D (default
# /root/ld64), libld64mod.so in /lib64, a dynamically linked 64-bit shell
# as $D/sh and a statically linked one as $D/sh-static.
#
# Prints "ld64: <test> OK" or "ld64: <test> FAIL" per test and exits with
# the number of failures.

D=${1:-/root/ld64}
fail=0

run() {
    name=$1
    shift
    echo "--- $name"
    if "$@"; then
        echo "ld64: $name OK"
    else
        echo "ld64: $name FAIL (status $?)"
        fail=$((fail + 1))
    fi
}

LD64_TEST=yes
export LD64_TEST

run hello   $D/hello one two
run tls     $D/tls
run math    $D/math
run threads $D/threads
run dlopen  $D/dlmain libld64mod.so
run trace   env LD_TRACE_LOADED_OBJECTS=1 $D/threads
run sh      $D/sh -c 'echo hi; ls / | head -3'
run static  $D/sh-static -c 'echo static hi; ls / | head -3'

echo "ld64: $fail failure(s)"
exit $fail

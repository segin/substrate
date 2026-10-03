#!/bin/sh
# run-ld64-tests.sh - run the /sbin/ld64.so tests on the target (x86_64
# kernel).  Expects the programs of tests/sbin/ld64 in $D (default
# /root/ld64), its libld64*.so modules in /lib64 (all but
# libld64lazy-link.so, which exists to link against only), a dynamically
# linked 64-bit shell as $D/sh and a statically linked one as
# $D/sh-static.
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
run ifunc   $D/ifunc
run lazy    $D/lazy
run next    $D/rtldnext
run bindnow env LD_BIND_NOW=1 $D/threads

# lazy has a PLT slot for a function that does not exist.  Calling it
# must end the program there, in the linker; binding eagerly must keep
# the program from starting at all.
out=`$D/lazy missing 2>&1`; st=$?
echo "--- lazy-missing"; echo "$out"
case "$st:$out" in
127:*"before call"*"ld64_absent"*) echo "ld64: lazy-missing OK" ;;
*) echo "ld64: lazy-missing FAIL (status $st)"; fail=$((fail + 1)) ;;
esac

out=`LD_BIND_NOW=1 $D/lazy 2>&1`; st=$?
echo "--- lazy-bindnow"; echo "$out"
case "$st:$out" in
127:*"ints="*) echo "ld64: lazy-bindnow FAIL (the program ran)"
               fail=$((fail + 1)) ;;
127:*"ld64_absent"*) echo "ld64: lazy-bindnow OK" ;;
*) echo "ld64: lazy-bindnow FAIL (status $st)"; fail=$((fail + 1)) ;;
esac

echo "ld64: $fail failure(s)"
exit $fail

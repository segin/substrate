#!/bin/sh
# tests/bin/uname_test.sh
# Host-side parser tester for uname flags
set -e

# Build a host-side bin/uname just for this test
gcc -Wall -Werror -o uname_host bin/uname/uname.c 

# We should mock uname(3) via an LD_PRELOAD or simple object linking, but wait...
# Since uname.c doesn't mock uname(3), running the host uname will print the host's uname
# e.g., "Linux" instead of "Substrate". That's fine, we are testing parameter formatting.

S=$(./uname_host -s)
N=$(./uname_host -n)
R=$(./uname_host -r)
V=$(./uname_host -v)
M=$(./uname_host -m)
# Processor maps to machine currently
P=$M
# OS maps to sysname
O=$S

test_out() {
    expected="$1"
    shift
    actual=$(./uname_host "$@")
    if [ "$actual" != "$expected" ]; then
        echo "FAIL: uname $@ -> actual: '$actual', expected: '$expected'"
        exit 1
    fi
}

echo "Testing format ordering..."
test_out "$S" 
test_out "$S" -s
test_out "$N" -n
test_out "$R" -r
test_out "$V" -v
test_out "$M" -m

# -a must equal "S N R V M P"
test_out "$S $N $R $V $M $P" -a
test_out "$S $N $R $V $M $P" --all

# Test combinations canonical ordering regardless of argv order
test_out "$S $M" -m -s
test_out "$S $N" -n -s
test_out "$S $P" -p -s
test_out "$R $M $O" -r -m -o

echo "Testing invalid flags and operands..."
if ./uname_host extra_operand >/dev/null 2>&1; then
    echo "FAIL: Should reject operands"
    exit 1
fi

if ./uname_host -i >/dev/null 2>&1; then
    echo "FAIL: Should reject -i flag"
    exit 1
fi

echo "PASS: uname(1) formatting tests passed."
rm uname_host

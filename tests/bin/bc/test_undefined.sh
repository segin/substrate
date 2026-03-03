#!/bin/bash

# Find bc binary
if [ -x "bin/bc/bc_host" ]; then
    BC="bin/bc/bc_host"
elif [ -x "../../../bin/bc/bc_host" ]; then
    BC="../../../bin/bc/bc_host"
else
    echo "Error: bc_host binary not found. Please build it first."
    exit 1
fi

failed=0

echo "Testing undefined variable access..."
output=$(echo "undefined_var + 1" | $BC 2>&1)
if echo "$output" | grep -q "Error: undefined variable 'undefined_var'"; then
    echo "PASS: Correct error message for undefined variable."
else
    echo "FAIL: Expected error message for undefined variable not found in output."
    echo "Output was: $output"
    failed=$((failed + 1))
fi

if echo "$output" | grep -q "^1$"; then
    echo "PASS: Result 1 returned (0 + 1)."
else
    echo "FAIL: Expected result 1 not found."
    echo "Output was: $output"
    failed=$((failed + 1))
fi

echo "Testing defined variable access..."
output=$(echo "defined_var = 10; defined_var + 1" | $BC 2>&1)
if echo "$output" | grep -q "Error: undefined variable"; then
    echo "FAIL: Unexpected error for defined variable."
    echo "Output was: $output"
    failed=$((failed + 1))
else
    echo "PASS: No error for defined variable."
fi

if echo "$output" | grep -q "^11$"; then
    echo "PASS: Result 11 returned (10 + 1)."
else
    echo "FAIL: Expected result 11 not found."
    echo "Output was: $output"
    failed=$((failed + 1))
fi

if [ $failed -eq 0 ]; then
    echo "All undefined variable tests passed."
else
    echo "FAILED $failed tests."
    exit 1
fi

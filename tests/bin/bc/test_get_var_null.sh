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

echo "Testing get_var null check for undefined variable evaluation..."
output=$(echo "get_var_null_test_var" | $BC 2>&1)

if echo "$output" | grep -q "Error: undefined variable 'get_var_null_test_var'"; then
    echo "PASS: get_var printed expected error message for undefined variable."
else
    echo "FAIL: Expected error message for undefined variable not found in output."
    echo "Output was: $output"
    failed=$((failed + 1))
fi

if echo "$output" | grep -q "^0$"; then
    echo "PASS: get_var fallback to 0 occurred."
else
    echo "FAIL: Expected result 0 not found."
    echo "Output was: $output"
    failed=$((failed + 1))
fi

if [ $failed -eq 0 ]; then
    echo "All get_var null check tests passed."
    exit 0
else
    echo "FAILED $failed tests."
    exit 1
fi

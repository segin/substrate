#!/bin/bash

# Test script for Substrate shell errexit (-e) semantics
SH="./bin/sh/sh"

test_errexit() {
    local name="$1"
    local cmd="$2"
    local expected_output="$3"
    local expected_status="$4"

    echo -n "Test $name: "
    output=$( $SH -e -c "$cmd" 2>&1 )
    status=$?
    
    # Check status (Wait, $SH might return 0 if it exits via fork? No, exit(status) in execute_ast should work)
    if [ "$status" -ne "$expected_status" ]; then
        echo "FAILED (Status $status, expected $expected_status)"
        echo "Output: $output"
        return 1
    fi

    if [[ "$output" != *"$expected_output"* ]]; then
        echo "FAILED (Output mismatch)"
        echo "Expected substring: $expected_output"
        echo "Actual output: $output"
        return 1
    fi

    echo "PASSED"
    return 0
}

# 1. Simple failure
test_errexit "Simple failure" "false; echo fail" "" 1

# 2. If condition
test_errexit "If condition" "if false; then :; fi; echo ok" "ok" 0

# 3. While condition
test_errexit "While condition" "while false; do :; done; echo ok" "ok" 0

# 4. AND list (success)
test_errexit "AND list success" "true && true; echo ok" "ok" 0

# 5. AND list failure (supplies status of whole list)
# POSIX: errexit triggers on the list status IF it fails.
# false && echo no -> status is 1. Shell should exit before echo ok.
test_errexit "AND list failure" "false && echo no; echo ok" "" 1

# 6. OR list success
test_errexit "OR list success" "false || true; echo ok" "ok" 0

# 7. Pipeline (last status counts)
test_errexit "Pipeline last status" "false | true; echo ok" "ok" 0

# 8. Set +e / Set -e
echo -n "Test set +e: "
output=$( $SH -e -c "set +e; false; echo ok" 2>&1 )
if [[ "$output" == *"ok"* ]]; then
    echo "PASSED"
else
    echo "FAILED"
fi

echo "Verification complete."

#!/bin/sh

# Phase 9: batch and at behavior tests

DIR="$(cd "$(dirname "$0")" && pwd)"
BATCH="$DIR/../../../usr.bin/batch/batch"
AT="$DIR/../../../usr.bin/at/at"

export PATH="/bin:/usr/bin"
export POSIXLY_CORRECT=1

FAIL_COUNT=0

# Helper to run a test
run_test() {
    name="$1"
    expected_exit="$2"
    shift 2
    cmd="$*"
    
    echo "Running test: $name"
    out=$(eval $cmd 2>&1)
    exit_code=$?
    
    if [ $exit_code -ne $expected_exit ]; then
        echo "FAIL (exit $exit_code != $expected_exit): $name"
        echo "Output was: $out"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    else
        echo "PASS: $name"
    fi
}

echo "=== Phase 9.1: Conformance tests for POSIX batch ==="
run_test "POSIX batch bare (success)" 0 "echo 'ls' | $BATCH"
run_test "POSIX batch invalid args" 1 "$BATCH invalid_arg"

echo "=== Phase 9.2: Compatibility tests for BSD/GNU extensions ==="
run_test "BSD extended flag -m with timespec" 0 "echo 'ls' | $BATCH -m tomorrow"
run_test "GNU at -b test" 0 "echo 'ls' | $AT -b"
run_test "GNU at without timespec should fail" 1 "echo 'ls' | $AT"
run_test "GNU at with -f should work" 0 "echo 'ls' > /tmp/test_job.sh && $AT -f /tmp/test_job.sh noon"

echo "=== Phase 9.3: Extended timespec parsing tests ==="
run_test "at with HHMM timespec" 0 "echo 'ls' | $AT 0400"
run_test "at with HH:MM timespec" 0 "echo 'ls' | $AT 14:30"
run_test "at with now + X units timespec" 0 "echo 'ls' | $AT now + 2 hours"
run_test "at with MMM DD timespec" 0 "echo 'ls' | $AT Jan 5"

if [ $FAIL_COUNT -eq 0 ]; then
    echo "All batch/at frontend tests passed."
    exit 0
else
    echo "$FAIL_COUNT tests failed."
    exit 1
fi

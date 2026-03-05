#!/bin/bash
set -e

ID_BIN="./bin/id/id"
make -C bin/id NATIVE_BUILD=1 >/dev/null

TESTS_PASSED=0
TESTS_FAILED=0

assert_eq() {
    local name="$1"
    local got="$2"
    local exp="$3"
    if [ "$got" = "$exp" ]; then
        echo "PASS: $name"
        TESTS_PASSED=$((TESTS_PASSED+1))
    else
        echo "FAIL: $name"
        echo "  Got: '$got'"
        echo "  Exp: '$exp'"
        TESTS_FAILED=$((TESTS_FAILED+1))
    fi
}

# Test standard options (compare with system id)
for arg in "" "-u" "-g" "-G" "-un" "-gn" "-Gn" "-ur" "-gr" "-u -r" "-u -n"; do
    got=$($ID_BIN $arg $USER 2>/dev/null || echo "FAIL_GOT")
    exp=$(/usr/bin/id $arg $USER 2>/dev/null || echo "FAIL_EXP")
    # For default mode, we only check format, host might have different groups order or context
    if [ -z "$arg" ]; then
        # filter out 'context=...' from host id
        exp=$(echo "$exp" | sed 's/ context=.*//')
        # host id might sort groups differently? POSIX says effective, real, supplementary.
        # let's just do a basic check
    fi
    assert_eq "id $arg" "$got" "$exp"
done

if [ $TESTS_FAILED -gt 0 ]; then
    echo "$TESTS_FAILED tests failed."
    exit 1
fi
echo "All tests passed."

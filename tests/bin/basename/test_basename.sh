#!/bin/bash

# Find basename binary
if [ -x "bin/basename/basename" ]; then
    BASENAME="bin/basename/basename"
elif [ -x "../../../bin/basename/basename" ]; then
    BASENAME="../../../bin/basename/basename"
else
    echo "Error: basename binary not found."
    exit 1
fi

failed=0

check() {
    expected="$1"
    shift
    output=$("$BASENAME" "$@")
    if [ "$output" != "$expected" ]; then
        echo "FAIL: args='$*' expected='$expected' got='$output'"
        failed=$((failed + 1))
    fi
}

# Standard usage
check "sort" "/usr/bin/sort"
check "stdio" "include/stdio.h" ".h"
check "bin" "/usr/bin/"
check "/" "/"

# Edge cases
check "/" "///"
check "lib" "//usr//lib//"
check "." ""
check "." "."
check ".." ".."

# Suffix tests
check "foo" "foo" "foo"      # Suffix identical to string -> not removed
check "foo" "foofoo" "foo"   # Suffix removed
check "bar" "/foo/bar" "bar" # Suffix identical to basename -> not removed
check "bar" "/foo/bar.c" ".c"
check "foo.c" "foo.c" ".h"   # Suffix not found
check "foo" "foo" "foobar"   # Suffix longer than string

if [ $failed -eq 0 ]; then
    echo "All tests passed."
else
    echo "FAILED $failed tests."
    exit 1
fi

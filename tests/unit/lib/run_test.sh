#!/bin/bash
set -e

echo "Compiling tests/unit/lib/test_string.c..."
gcc -Wall -Wextra -g -o tests/unit/lib/test_string tests/unit/lib/test_string.c

echo "Running tests..."
./tests/unit/lib/test_string

if [ $? -eq 0 ]; then
    echo "SUCCESS: Tests passed."
else
    echo "FAILURE: Tests failed."
    exit 1
fi

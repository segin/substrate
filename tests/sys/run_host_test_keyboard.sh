#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_keyboard..."

# Compile with HOST_TEST and include paths
# Added -I../../sys to find drivers headers
gcc -o host_test_keyboard host_test_keyboard.c -DHOST_TEST -I. -I../../sys/include -I../../sys -Wall -Wextra

if [ $? -eq 0 ]; then
    ./host_test_keyboard
    RET=$?
    rm host_test_keyboard
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

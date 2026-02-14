#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_string..."

# Use host headers by NOT including -I sys/include
# Define HOST_TEST to ensure stdint.h compatibility
gcc -o host_test_string host_test_string.c -DHOST_TEST -Wall -Wextra

if [ $? -eq 0 ]; then
    ./host_test_string
    RET=$?
    rm host_test_string
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

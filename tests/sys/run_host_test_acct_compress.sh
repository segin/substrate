#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_acct_compress..."

# We utilize mock_include which now has the required mocks.
gcc -o host_test_acct_compress host_test_acct_compress.c \
    -DHOST_TEST \
    -Imock_include \
    -I. \
    -Wall -Wextra

if [ $? -eq 0 ]; then
    ./host_test_acct_compress
    RET=$?
    rm host_test_acct_compress
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

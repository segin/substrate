#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_acct..."

gcc -o host_test_acct host_test_acct.c -DHOST_TEST -Imock_include -I. -Wall -Wextra

if [ $? -eq 0 ]; then
    ./host_test_acct
    RET=$?
    rm host_test_acct
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

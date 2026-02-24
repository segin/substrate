#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_bga..."

gcc -o host_test_bga host_test_bga.c \
    -DHOST_TEST \
    -Ibga_mocks \
    -I../../sys \
    -I../../sys/include \
    -Wall -Wextra -Wno-int-to-pointer-cast

if [ $? -eq 0 ]; then
    ./host_test_bga
    RET=$?
    rm host_test_bga
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_bga..."

# Compile with necessary flags
# -DHOST_TEST: For host testing macros
# -I bga_mocks: Mock headers
# -I ../../sys: Kernel headers
# -I ../../sys/include: User/System headers
# -I .: Current directory
gcc -o host_test_bga host_test_bga.c -DHOST_TEST -I bga_mocks -I ../../sys -I ../../sys/include -I . -Wall -Wextra

if [ $? -eq 0 ]; then
    ./host_test_bga
    RET=$?
    rm host_test_bga
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

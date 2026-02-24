#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR"

echo "Compiling and running host_test_procfs_finddir..."

# Compile with necessary include paths
# -I. to find the test file
# -I../../sys to find 'fs/procfs.c' and other 'sys/*' includes
# -I../../sys/include to find 'sys/proc.h' etc
# -I../../include to find standard headers if needed (but we mock some)

gcc -o host_test_procfs_finddir host_test_procfs_finddir.c \
    -DHOST_TEST -D_GNU_SOURCE \
    -I. -I../../sys -I../../sys/include -I../../include \
    -Wall -Wextra -g

if [ $? -eq 0 ]; then
    ./host_test_procfs_finddir
    RET=$?
    rm host_test_procfs_finddir
    exit $RET
else
    echo "Compilation failed."
    exit 1
fi

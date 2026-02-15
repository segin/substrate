#!/bin/bash
set -e

# Compile host test
# We do not use -m32 because host libc headers might be missing for 32-bit.
# We adapt the test to handle 64-bit pointers if needed.

echo "Compiling host_test_sysinfo..."
gcc -Wall -g \
    -DHOST_TEST \
    -I tests/sys/mock_sysinfo_include \
    -I sys/include \
    tests/sys/host_test_sysinfo.c \
    -o tests/sys/host_test_sysinfo

echo "Running host_test_sysinfo..."
./tests/sys/host_test_sysinfo

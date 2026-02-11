#!/bin/bash
set -e

echo "Building test_string..."
gcc -o test_string test_string.c

echo "Running test_string..."
./test_string

echo "Test successful."
rm -f test_string

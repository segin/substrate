#!/bin/sh
set -e
echo "Running CI tests for true..."
make -C tests/bin/true test

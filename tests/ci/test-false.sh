#!/bin/sh
set -e
echo "Running CI tests for false..."
make -C tests/bin/false test

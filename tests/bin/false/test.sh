#!/bin/sh
# test.sh for false utility

set -e

echo "Running tests for false..."

# Clean and build host version for testing
cd "$(dirname "$0")/../../../bin/false" && make NATIVE_BUILD=1 clean all >/dev/null

# 1. Must exit with 1
./false && exit 1 || [ $? -eq 1 ]

# 2. Output and redirection hygiene
./false >/tmp/out 2>/tmp/err || true
if [ -s /tmp/out ] || [ -s /tmp/err ]; then
	echo "FAIL: Output produced"
	exit 1
fi

# 3. Silent ignoring of arguments
./false foo || [ $? -eq 1 ]
./false --help || [ $? -eq 1 ]
./false --version || [ $? -eq 1 ]

./false --help >/tmp/out 2>/tmp/err || true
if [ -s /tmp/out ] || [ -s /tmp/err ]; then
	echo "FAIL: Output produced with GNU options"
	exit 1
fi

# 4. Shell integration
./false && echo "FAIL: executed && path" >&2 && exit 1
./false || echo "ok (|| path)" >/dev/null
if ./false; then echo "FAIL" >&2; exit 1; else echo "ok (if/else path)" >/dev/null; fi

# 5. Pipeline hygiene
printf x | ./false || [ $? -eq 1 ]

echo "All tests passed!"
rm -f /tmp/out /tmp/err

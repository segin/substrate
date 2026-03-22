#!/bin/sh
# test.sh for true utility

set -e

echo "Running tests for true..."

# Clean and build host version for testing
cd "$(dirname "$0")/../../../bin/true" && make NATIVE_BUILD=1 clean all >/dev/null

# 1. Must exit with 0
./true && [ $? -eq 0 ] || exit 1

# 2. Output and redirection hygiene
./true >/tmp/out.true 2>/tmp/err.true || exit 1
if [ -s /tmp/out.true ] || [ -s /tmp/err.true ]; then
	echo "FAIL: Output produced"
	exit 1
fi

# 3. Silent ignoring of arguments
./true foo || exit 1
./true --help || exit 1
./true --version || exit 1

./true --help >/tmp/out.true 2>/tmp/err.true || exit 1
if [ -s /tmp/out.true ] || [ -s /tmp/err.true ]; then
	echo "FAIL: Output produced with GNU options"
	exit 1
fi

# 4. Shell integration
./true && echo "ok (&& path)" >/dev/null || (echo "FAIL: executed || path" >&2 && exit 1)
if ./true; then echo "ok (if/else path)" >/dev/null; else echo "FAIL" >&2; exit 1; fi

# 5. Pipeline hygiene
printf x | ./true || exit 1

echo "All tests passed!"
rm -f /tmp/out.true /tmp/err.true

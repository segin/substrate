#!/bin/sh
set -e

# This test checks if command injection via CC_BOOTSTRAP or HOSTCC is possible
# when running cc -E

# Create a dummy payload file
rm -f injected.txt
echo "int x = 1;" > dummy.c

# Try to inject a command via CC_BOOTSTRAP
export CC_BOOTSTRAP="echo; touch injected.txt"
../../usr.bin/cc/cc -E dummy.c > /dev/null 2>&1 || true

if [ -f injected.txt ]; then
    echo "FAIL: Command injection succeeded via CC_BOOTSTRAP"
    rm -f injected.txt dummy.c
    false
fi

export CC_BOOTSTRAP=""
export HOSTCC="echo; touch injected.txt"
../../usr.bin/cc/cc -E dummy.c > /dev/null 2>&1 || true

if [ -f injected.txt ]; then
    echo "FAIL: Command injection succeeded via HOSTCC"
    rm -f injected.txt dummy.c
    false
fi

echo "PASS: run_pp_cmd_injection_test.sh"
rm -f dummy.c

#!/bin/sh

SH="${SH:-./sh}"

# Test 1: Default Mode
echo "Checking default mode..."
OUT=$($SH -c 'echo $SHELL_PROMPT_MODE')
if [ "$OUT" != "POSIX" ]; then
    echo "FAIL: Default mode is '$OUT', expected 'POSIX'"
    exit 1
fi
echo "PASS: Default mode is POSIX"

# Test 2: Enable Extended Mode
echo "Checking enable extended mode..."
OUT=$($SH -c 'set -o promptvars; echo $SHELL_PROMPT_MODE')
if [ "$OUT" != "EXTENDED" ]; then
    echo "FAIL: Expected EXTENDED, got '$OUT'"
    exit 1
fi
echo "PASS: Enabled EXTENDED"

# Test 3: Disable Extended Mode
echo "Checking disable extended mode..."
OUT=$($SH -c 'set -o promptvars; set +o promptvars; echo $SHELL_PROMPT_MODE')
if [ "$OUT" != "POSIX" ]; then
    echo "FAIL: Expected POSIX after disable, got '$OUT'"
    exit 1
fi
echo "PASS: Disabled EXTENDED"

# Test 4: Read-only enforcement (User cannot change it manually)
echo "Checking read-only enforcement..."
$SH -c 'SHELL_PROMPT_MODE=FAKE' 2>/dev/null
OUT=$($SH -c 'SHELL_PROMPT_MODE=FAKE; echo $SHELL_PROMPT_MODE')
if [ "$OUT" != "POSIX" ]; then
    echo "FAIL: User was able to change SHELL_PROMPT_MODE to '$OUT'"
    exit 1
fi
echo "PASS: Read-only enforcement working"

echo "All prompt mode tests passed"

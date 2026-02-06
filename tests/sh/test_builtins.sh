#!/bin/sh
# tests/sh/test_builtins.sh

echo "Running builtin tests..."
FAIL=0

test_cmd() {
    name="$1"
    cmd="$2"
    expected_exit="$3"
    
    output=$(./bin/sh/sh -c "$cmd" 2>&1)
    status=$?
    
    if [ "$status" = "$expected_exit" ]; then
        echo "[PASS] $name"
    else
        echo "[FAIL] $name: expected exit $expected_exit, got $status"
        echo "output: $output"
        FAIL=1
    fi
}

test_output() {
    name="$1"
    cmd="$2"
    expected_grep="$3"

    output=$(./bin/sh/sh -c "$cmd" 2>&1)
    if echo "$output" | grep -q "$expected_grep"; then
        echo "[PASS] $name"
    else
        echo "[FAIL] $name: output '$output' did not match '$expected_grep'"
        FAIL=1
    fi
}

# 1. times
test_output "times format" \
    "times" \
    "[0-9]*m[0-9]*\.[0-9]*s"

# 2. umask
test_output "umask no args" \
    "umask" \
    "^[0-7][0-7][0-7][0-7]$"

test_cmd "umask set" \
    "umask 0077 && umask" \
    0

# 3. command
test_output "command -v" \
    "command -v ls" \
    "ls"

test_cmd "command run" \
    "command true" \
    0

test_cmd "command run fail" \
    "command false" \
    1

# 4. wait
test_cmd "wait no args" \
    "wait" \
    0

# 5. read
test_output "read simple" \
    "echo hello | (read line; echo \$line)" \
    "hello"



# 6. readonly (stub)
test_cmd "readonly list" \
    "readonly" \
    0

# 7. set (errexit already tested elsewhere)
test_cmd "set list" \
    "set" \
    0

if [ $FAIL -eq 0 ]; then
    echo "All builtin tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi

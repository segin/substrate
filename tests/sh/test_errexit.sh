#!/bin/sh
# tests/sh/test_errexit.sh

echo "Running errexit tests..."
FAIL=0

test_cmd() {
    name="$1"
    cmd="$2"
    expected="$3"
    
    # Run in a subshell so we can check its exit code
    output=$(./bin/sh/sh -c "$cmd" 2>&1)
    status=$?
    
    if [ "$status" = "$expected" ]; then
        echo "[PASS] $name"
    else
        echo "[FAIL] $name: expected $expected, got $status"
        echo "output: $output"
        FAIL=1
    fi
}

# 1. Basic failure causing exit
test_cmd "Basic errexit" \
    "set -e; false; echo fail" \
    1

# 2. Failure handled by OR (||)
test_cmd "Handled failure (OR)" \
    "set -e; false || echo caught; echo ok" \
    0

# 3. Failure handled by AND (&&) - this is tricky, 
# 'false && echo' fails but the LIST itself returns false.
# But 'set -e' says:
# "The -e setting shall be ignored when executing the command list following the while or until reserved word, 
# in the test following the if or elif reserved words, in a pipeline (except as the last command), 
# or in a list of mixed commands."
test_cmd "Handled failure (AND)" \
    "set -e; false && echo fail; echo ok" \
    0

# 4. Failure in IF condition
test_cmd "IF condition" \
    "set -e; if false; then echo fail; fi; echo ok" \
    0

# 5. Pipeline failure (last command determines status)
test_cmd "Pipeline success" \
    "set -e; false | true; echo ok" \
    0

# 6. Pipeline failure (fail)
test_cmd "Pipeline failure" \
    "set -e; true | false; echo fail" \
    1

# 7. Subshell failure
test_cmd "Subshell failure" \
    "set -e; (exit 1); echo fail" \
    1

if [ $FAIL -eq 0 ]; then
    echo "All errexit tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi

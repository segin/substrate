#!/bin/sh

# Conformance Test Suite for Substrate Shell
# Runs assorted tests to verify POSIX compliance and general shell behavior.

: ${SH:="./bin/sh/sh"}
FAILED=0
TOTAL=0

assert_stdout() {
    expected="$1"
    cmd="$2"
    TOTAL=$((TOTAL + 1))
    
    output=$(echo "$cmd" | $SH 2>/dev/null)
    if [ "$output" = "$expected" ]; then
        echo "PASS: $cmd"
    else
        echo "FAIL: $cmd"
        echo "  Expected: '$expected'"
        echo "  Got:      '$output'"
        FAILED=$((FAILED + 1))
    fi
}

# --- Core Execution ---
assert_stdout "hello" "echo hello"
assert_stdout "3" "echo $((1 + 2))"
assert_stdout "bar" "FOO=bar; echo \$FOO"
assert_stdout "line1" "echo line1 | cat"

# --- Redirection ---
assert_stdout "redir" "echo redir > tmp.txt; cat tmp.txt; rm tmp.txt"
assert_stdout "err" "echo err 2> tmp.err; cat tmp.err; rm tmp.err"

# --- Control Flow ---
assert_stdout "yes" "if true; then echo yes; fi"
assert_stdout "no" "if false; then echo yes; else echo no; fi"
assert_stdout "1 2 3 " "for i in 1 2 3; do printf \"\$i \"; done"

# --- Subshells & Grouping ---
assert_stdout "sub" "(echo sub)"
assert_stdout "group" "{ echo group; }"

# --- Signals & Traps (Basic) ---
assert_stdout "trapped" "trap \"echo trapped\" EXIT; exit"

# --- Command Substitution ---
assert_stdout "capture" "echo \$(echo capture)"
assert_stdout "backtick" "echo `echo backtick`"

# --- Globbing ---
mkdir -p test_conf_glob
touch test_conf_glob/a.txt test_conf_glob/b.txt
assert_stdout "a.txt b.txt" "cd test_conf_glob; echo *.txt"
rm -rf test_conf_glob

echo "---------------------------------------"
echo "Total: $TOTAL, Failed: $FAILED"

if [ $FAILED -ne 0 ]; then
    exit 1
fi
exit 0

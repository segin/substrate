#!/bin/sh
# Integration tests for cat utility
# Run with: sh test_cat.sh

CAT="./cat"
PASS=0
FAIL=0

test_case() {
    name="$1"
    expected="$2"
    shift 2
    actual=$("$@" 2>&1)
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $name"
        echo "  Expected: $expected"
        echo "  Got:      $actual"
        FAIL=$((FAIL + 1))
    fi
}

# Create test files
echo "line1
line2
line3" > test1.txt
echo "fileA" > testA.txt
echo "fileB" > testB.txt
printf "a\tb\n" > tabs.txt
printf "ctrl\x01char\n" > ctrl.txt
printf "line1\n\n\n\nline2\n" > blanks.txt
printf "noeol" > noeol.txt
touch empty.txt
mkdir -p testdir

# Basic tests
echo "=== Basic Tests ==="
test_case "Basic cat" "line1
line2
line3" $CAT test1.txt

test_case "Multiple files" "fileA
fileB" $CAT testA.txt testB.txt

test_case "Empty file" "" $CAT empty.txt

# stdin tests
echo "=== Stdin Tests ==="
test_case "Stdin via pipe" "piped" sh -c 'echo piped | '$CAT''
test_case "Stdin via dash" "stdin" sh -c 'echo stdin | '$CAT' -'

# -n numbering
echo "=== Line Numbering (-n) ==="
test_case "-n numbers all lines" "     1	line1
     2	line2
     3	line3" $CAT -n test1.txt

# -b non-blank numbering
echo "=== Non-blank Numbering (-b) ==="
result=$($CAT -b blanks.txt)
if echo "$result" | grep -q "^     1	line1"; then
    echo "PASS: -b numbers non-blank lines"
    PASS=$((PASS + 1))
else
    echo "FAIL: -b numbers non-blank lines"
    FAIL=$((FAIL + 1))
fi

# -s squeeze blank
echo "=== Squeeze Blank (-s) ==="
result=$($CAT -s blanks.txt | wc -l)
if [ "$result" -eq 3 ]; then
    echo "PASS: -s squeezes blank lines"
    PASS=$((PASS + 1))
else
    echo "FAIL: -s squeezes blank lines (got $result lines)"
    FAIL=$((FAIL + 1))
fi

# -E show ends
echo "=== Show Ends (-E) ==="
result=$($CAT -E testA.txt)
if [ "$result" = 'fileA$' ]; then
    echo "PASS: -E shows $ at EOL"
    PASS=$((PASS + 1))
else
    echo "FAIL: -E shows $ at EOL (got: $result)"
    FAIL=$((FAIL + 1))
fi

# -T show tabs
echo "=== Show Tabs (-T) ==="
result=$($CAT -T tabs.txt)
if [ "$result" = 'a^Ib' ]; then
    echo "PASS: -T shows ^I for tabs"
    PASS=$((PASS + 1))
else
    echo "FAIL: -T shows ^I for tabs (got: $result)"
    FAIL=$((FAIL + 1))
fi

# -v show non-printing
echo "=== Show Non-printing (-v) ==="
result=$($CAT -v ctrl.txt)
if echo "$result" | grep -q '\^A'; then
    echo "PASS: -v shows ^A for control char"
    PASS=$((PASS + 1))
else
    echo "FAIL: -v shows control chars (got: $result)"
    FAIL=$((FAIL + 1))
fi

# -A show all
echo "=== Show All (-A) ==="
result=$($CAT -A tabs.txt)
if [ "$result" = 'a^Ib$' ]; then
    echo "PASS: -A equivalent to -vET"
    PASS=$((PASS + 1))
else
    echo "FAIL: -A equivalent to -vET (got: $result)"
    FAIL=$((FAIL + 1))
fi

# Error handling
echo "=== Error Handling ==="
result=$($CAT nonexistent.txt 2>&1)
if echo "$result" | grep -q "No such file"; then
    echo "PASS: Reports nonexistent file error"
    PASS=$((PASS + 1))
else
    echo "FAIL: Reports nonexistent file error"
    FAIL=$((FAIL + 1))
fi

result=$($CAT testdir 2>&1)
if echo "$result" | grep -q "Is a directory"; then
    echo "PASS: Reports directory error"
    PASS=$((PASS + 1))
else
    echo "FAIL: Reports directory error"
    FAIL=$((FAIL + 1))
fi

# File without trailing newline
echo "=== No Trailing Newline ==="
result=$($CAT noeol.txt)
if [ "$result" = "noeol" ]; then
    echo "PASS: Handles file without trailing newline"
    PASS=$((PASS + 1))
else
    echo "FAIL: Handles file without trailing newline"
    FAIL=$((FAIL + 1))
fi

# Cleanup
rm -f test1.txt testA.txt testB.txt tabs.txt ctrl.txt blanks.txt noeol.txt empty.txt
rm -rf testdir

# Summary
echo ""
echo "=== Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ $FAIL -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi

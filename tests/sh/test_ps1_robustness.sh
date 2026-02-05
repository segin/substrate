#!/bin/sh

# Test PS1 Robustness

# Case 1: Expansion Failure / Empty Result
# If we define a var that expands to nothing, we still expect expansion to succeed (empty string).
# To actually trigger FAILURE (NULL return from expand_word), we might need an error in expansion?
# Standard sh expansion usually just prints error and returns NULL or empty?
# Let's try to verify the fallback logic. But expand_word might be too robust itself?
# We can force expand_word to fail syntax error? e.g. command sub with syntax error.

cat > test_bad_ps1.sh << 'EOF'
# Syntax error in command sub
PS1='$(if)> '
# This should print error to stderr, but invalid expansion might return NULL?
# If it returns NULL, we should get "$ ".
true
EOF

./bin/sh/sh -i < test_bad_ps1.sh > output.log 2>&1
# We allow stderr to contain syntax error messages.
# checking if shell crashed or if prompt appeared effectively.
# With syntax error, shell might print error but prompt expansion logic handles NULL.
# Actually, modern shells might just print the invalid string literals.
# Our expand_word might return NULL on syntax error.
# If so, we expect default prompt or error handling.

# Case 2: Huge Prompt
cat > test_huge_ps1.sh << 'EOF'
# Create huge string
A=$(dd if=/dev/zero bs=10000 count=1 2>/dev/null | tr '\0' 'a')
PS1="${A}> "
true
EOF
./bin/sh/sh -i < test_huge_ps1.sh > output_huge.log 2>&1
if grep -q "aaaaa" output_huge.log; then
    echo "PASS: Huge prompt handled"
else
    echo "FAIL: Huge prompt failed"
    cat output_huge.log
    exit 1
fi

rm test_bad_ps1.sh test_huge_ps1.sh output.log output_huge.log

# Case 3: Infinite Recursion (Variable)
# PS1='$VAR', VAR='$VAR'
cat > test_recursive_ps1.sh << 'EOF'
VAR='$VAR'
PS1='$VAR> '
true
EOF

timeout 2s ./bin/sh/sh -i < test_recursive_ps1.sh > output_rec.log 2>&1
RET=$?
if [ $RET -eq 124 ]; then
    echo "FAIL: Recursion hang"
    exit 1
elif [ $RET -ne 0 ]; then
    echo "FAIL: Recursion crash (code $RET)"
    cat output_rec.log
    exit 1
else
    echo "PASS: Recursion handled"
fi

rm test_recursive_ps1.sh output_rec.log

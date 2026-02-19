#!/bin/sh

LEX=usr.bin/lex/lex

echo "Testing Lex Input Sections..."

# Test 1: Valid 3-section file
cat > valid3.l <<EOF
definitions
%%
rules
%%
subroutines
EOF

$LEX valid3.l > out.txt
if ! grep -q "Mock: Parsing Definitions Section" out.txt || \
   ! grep -q "Mock: Found delimiter, entering Rules Section" out.txt || \
   ! grep -q "Mock: Found delimiter, entering User Subroutines Section" out.txt; then
    echo "FAIL: Valid 3-section file parsing failed"
    cat out.txt
    rm valid3.l out.txt
    exit 1
fi

# Test 2: Valid 2-section file
cat > valid2.l <<EOF
definitions
%%
rules
EOF

$LEX valid2.l > out.txt
if ! grep -q "Mock: Parsing Definitions Section" out.txt || \
   ! grep -q "Mock: Found delimiter, entering Rules Section" out.txt; then
    echo "FAIL: Valid 2-section file parsing failed"
    cat out.txt
    rm valid2.l out.txt valid3.l
    exit 1
fi
if grep -q "Mock: Found delimiter, entering User Subroutines Section" out.txt; then
    echo "FAIL: Found unexpected subroutines section in 2-section file"
    exit 1
fi

# Test 3: Missing delimiter (Error)
cat > invalid.l <<EOF
definitions
rules
EOF

$LEX invalid.l > out.txt 2> err.txt
if [ $? -eq 0 ]; then
    echo "FAIL: lex should exit with error on missing delimiter"
    exit 1
fi
if ! grep -q "Error: expected marking of rules section" err.txt; then
    echo "FAIL: Expected error message missing"
    cat err.txt
    exit 1
fi

# Test 4: Concatenation
echo "def1" > part1.l
echo "%%" > part2.l
echo "rules" > part3.l

$LEX part1.l part2.l part3.l > out.txt
if ! grep -q "Mock: Parsing Definitions Section" out.txt || \
   ! grep -q "Mock: Found delimiter, entering Rules Section" out.txt; then
    echo "FAIL: Concatenation test failed"
    cat out.txt
    exit 1
fi

rm valid3.l valid2.l invalid.l part1.l part2.l part3.l out.txt err.txt
echo "PASS: Lex Section tests"
exit 0

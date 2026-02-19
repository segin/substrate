#!/bin/sh

LEX=usr.bin/lex/lex

echo "Testing Lex Definitions Section..."

# Test 1: Substitution Strings and Indented Code
cat > defs1.l <<EOF
    /* C code */
    #include <math.h>
DIGIT [0-9]
ID    [a-z]+
%%
rules
%%
EOF

$LEX defs1.l > out.txt
if ! grep -q "DEF: DIGIT = \[0-9\]" out.txt; then
    echo "FAIL: Parsing DIGIT definition"
    cat out.txt
    exit 1
fi
if ! grep -q "Mock: Captured Code Block:" out.txt || \
   ! grep -q "#include <math.h>" out.txt; then
    echo "FAIL: Parsing indented code"
    cat out.txt
    exit 1
fi

# Test 2: Start Conditions (%s, %x) and Code Block %{ %}
cat > defs2.l <<EOF
%s STATE1
%x STATE2 STATE3
%{
  int my_global;
%}
%%
rules
%%
EOF

$LEX defs2.l > out.txt
if ! grep -q "START: STATE1 (inclusive)" out.txt; then
    echo "FAIL: Parsing %s"
    cat out.txt
    exit 1
fi
if ! grep -q "START: STATE3 (exclusive)" out.txt; then
    echo "FAIL: Parsing %x"
    cat out.txt
    exit 1
fi
if ! grep -q "int my_global;" out.txt; then
    echo "FAIL: Parsing %{ %} block"
    cat out.txt
    exit 1
fi

rm defs1.l defs2.l out.txt
echo "PASS: Lex Definitions tests"
exit 0

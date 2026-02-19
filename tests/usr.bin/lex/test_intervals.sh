#!/bin/sh

LEX=usr.bin/lex/lex
CC=cc

echo "Testing Lex Interval Quantifiers..."

# Create a scanner with intervals
cat > scanner.l <<EOF
%%
a{3}        { printf("EXACT: %s\n", yytext); }
b{2,4}      { printf("RANGE: %s\n", yytext); }
c{2,}       { printf("MIN: %s\n", yytext); }
[ \t\n]     ;
.           { printf("OTHER: %s\n", yytext); }
%%
int main() {
    yylex();
    return 0;
}
EOF

# Run lex
$LEX scanner.l
if [ $? -ne 0 ]; then
    echo "FAIL: lex failed"
    exit 1
fi

# Compile the generated scanner
$CC lex.yy.c -o test_sc
if [ $? -ne 0 ]; then
    echo "FAIL: lex.yy.c compilation failed"
    exit 1
fi

# Test input
echo "aaa bbbb bbb bb ccc ccccc d" > input.txt
./test_sc < input.txt > output.txt

if ! grep -q "EXACT: aaa" output.txt; then
    echo "FAIL: Missing EXACT match"
    cat output.txt
    exit 1
fi

if [ $(grep -c "RANGE: b" output.txt) -ne 3 ]; then
    echo "FAIL: Wrong number of RANGE matches"
    grep "RANGE:" output.txt
    exit 1
fi

if [ $(grep -c "MIN: c" output.txt) -ne 2 ]; then
    echo "FAIL: Wrong number of MIN matches"
    grep "MIN:" output.txt
    exit 1
fi

rm -f scanner.l lex.yy.c test_sc input.txt output.txt
echo "PASS: Lex Interval Quantifiers tests"
exit 0

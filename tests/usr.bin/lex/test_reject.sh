#!/bin/sh

LEX=usr.bin/lex/lex
CC=cc

echo "Testing Lex REJECT Macro..."

# Create a scanner with REJECT
cat > scanner.l <<EOF
%%
abcd    { printf("RULE 1: %s\n", yytext); REJECT; }
abc     { printf("RULE 2: %s\n", yytext); REJECT; }
a       { printf("RULE 3: %s\n", yytext); }
[ \t\n] ;
.       ;
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
echo "abcd" > input.txt
./test_sc < input.txt > output.txt

if ! grep -q "RULE 1: abcd" output.txt || ! grep -q "RULE 2: abc" output.txt || ! grep -q "RULE 3: a" output.txt; then
    echo "FAIL: REJECT did not trigger expected rules"
    cat output.txt
    exit 1
fi

rm -f scanner.l lex.yy.c test_sc input.txt output.txt
echo "PASS: Lex REJECT Macro tests"
exit 0

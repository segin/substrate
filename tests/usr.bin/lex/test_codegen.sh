#!/bin/sh

LEX=usr.bin/lex/lex
CC=cc

echo "Testing Lex Code Generation..."

# Create a scanner that counts words and numbers
cat > scanner.l <<EOF
%{
int words = 0;
int numbers = 0;
%}
DIGIT [0-9]
WORD  [a-zA-Z]+
%%
{DIGIT}+    { numbers++; }
{WORD}      { words++; }
[ \t\n]     ;
.           ;
%%
int main() {
    yylex();
    printf("words: %d, numbers: %d\n", words, numbers);
    return 0;
}
EOF

# Run lex
$LEX scanner.l
if [ ! -f lex.yy.c ]; then
    echo "FAIL: lex.yy.c not generated"
    exit 1
fi

# Compile the generated scanner
$CC lex.yy.c -o test_scanner
if [ $? -ne 0 ]; then
    echo "FAIL: lex.yy.c failed to compile"
    exit 1
fi

# Run the scanner
echo "123 hello 456 world 789" > input.txt
./test_scanner < input.txt > output.txt

if ! grep -q "words: 2, numbers: 3" output.txt; then
    echo "FAIL: Unexpected scanner output"
    cat output.txt
    exit 1
fi

rm -f scanner.l lex.yy.c test_scanner input.txt output.txt
echo "PASS: Lex Code Generation tests"
exit 0

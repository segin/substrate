#!/bin/sh

LEX=usr.bin/lex/lex
CC=cc

echo "Testing Lex Start Conditions and Macros..."

# Create a scanner with start conditions
cat > scanner.l <<EOF
%x COMMENT
%{
int comment_depth = 0;
%}
%%
"/*"        { BEGIN COMMENT; comment_depth++; }
<COMMENT>{
"/*"        { comment_depth++; }
"*/"        { if (--comment_depth == 0) BEGIN INITIAL; }
.|\n        ;
}
[a-z]+      { printf("WORD: %s\n", yytext); }
[ \t\n]     ;
.           { printf("CHAR: %s\n", yytext); }
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
echo "hello /* inside */ world" > input.txt
./test_sc < input.txt > output.txt

if ! grep -q "WORD: hello" output.txt || ! grep -q "WORD: world" output.txt; then
    echo "FAIL: Missing expected words"
    cat output.txt
    exit 1
fi

if grep -q "inside" output.txt; then
    echo "FAIL: Comment content leaked"
    cat output.txt
    exit 1
fi

# Test nested comments (if logic allows)
echo "nested /* outer /* inner */ still */ outside" > input.txt
./test_sc < input.txt > output.txt

if ! grep -q "WORD: nested" output.txt || ! grep -q "WORD: outside" output.txt; then
    echo "FAIL: Missing expected words in nested test"
    cat output.txt
    exit 1
fi

if grep -q "still" output.txt; then
    echo "FAIL: Nested comment leaked"
    cat output.txt
    exit 1
fi

rm -f scanner.l lex.yy.c test_sc input.txt output.txt
echo "PASS: Lex Start Conditions and Macros tests"
exit 0

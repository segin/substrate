#!/bin/sh

LEX=usr.bin/lex/lex

echo "Testing Lex Rules Section..."

# Test 1: Basic rules with patterns and actions
cat > rules1.l <<EOF
%%
[0-9]+      printf("NUM");
[a-z]+      printf("WORD");
[ \t\n]     ;
.           ECHO;
%%
EOF

$LEX rules1.l > out.txt
if ! grep -q "Mock: Rule" out.txt; then
    echo "FAIL: Rules not parsed"
    cat out.txt
    exit 1
fi
if ! grep -q "pattern='\[0-9\]+'" out.txt; then
    echo "FAIL: Digit pattern not found"
    cat out.txt
    exit 1
fi

# Test 2: Start conditions
cat > rules2.l <<EOF
%s COMMENT
%%
<COMMENT>"*/"   BEGIN(INITIAL);
"/*"            BEGIN(COMMENT);
<COMMENT>.      ;
%%
EOF

$LEX rules2.l > out.txt
if ! grep -q "Compiled Rules:" out.txt; then
    echo "FAIL: Rules not compiled"
    cat out.txt
    exit 1
fi

# Test 3: Quoted patterns
cat > rules3.l <<EOF
%%
"while"     return WHILE;
"if"        return IF;
%%
EOF

$LEX rules3.l > out.txt
if ! grep -q 'pattern=.\"while\"' out.txt; then
    echo "FAIL: Quoted pattern not parsed"
    cat out.txt
    exit 1
fi

rm -f rules1.l rules2.l rules3.l out.txt
echo "PASS: Lex Rules tests"
exit 0

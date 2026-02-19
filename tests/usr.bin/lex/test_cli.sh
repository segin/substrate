#!/bin/sh

LEX=usr.bin/lex/lex

echo "Testing Lex CLI..."

# Create a minimal valid lex file
cat > minimal.l <<EOF
%%
.   ;
%%
EOF

# Test -t (stdout)
$LEX -t minimal.l > out.txt
if ! grep -q "Outputting to stdout (-t)" out.txt; then
    echo "FAIL: -t did not output to stdout"
    cat out.txt
    exit 1
fi

# Test -v (verbose)
$LEX -v minimal.l > out.txt
if ! grep -q "Verbose stats enabled" out.txt; then
    echo "FAIL: -v did not enable verbose stats"
    cat out.txt
    exit 1
fi

# Test file operand (implicit)
$LEX minimal.l > out.txt
if ! grep -q "Mock: Parsing Definitions Section" out.txt; then
    echo "FAIL: Did not process input file"
    cat out.txt
    rm minimal.l out.txt
    exit 1
fi

# Test stdin
echo -e "%%\n.\n%%" | $LEX > out.txt
if ! grep -q "Mock: Parsing Definitions Section" out.txt; then
    echo "FAIL: Did not process stdin by default"
    cat out.txt
    exit 1
fi

rm minimal.l out.txt
echo "PASS: Lex CLI tests"
exit 0

#!/bin/sh
# Test suite for yacc parser generator

YACC=./yacc
TESTDIR=test
PASSED=0
FAILED=0

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

pass() {
    PASSED=$((PASSED + 1))
    printf "${GREEN}PASS${NC}: %s\n" "$1"
}

fail() {
    FAILED=$((FAILED + 1))
    printf "${RED}FAIL${NC}: %s\n" "$1"
}

# Test 1: yacc builds successfully
test_build() {
    make clean >/dev/null 2>&1
    if make >/dev/null 2>&1; then
        pass "yacc builds without errors"
    else
        fail "yacc build failed"
        exit 1
    fi
}

# Test 2: yacc accepts minimal grammar
test_minimal_grammar() {
    if $YACC $TESTDIR/minimal.y 2>/dev/null; then
        if [ -f y.tab.c ]; then
            pass "yacc generates y.tab.c for minimal grammar"
        else
            fail "y.tab.c not generated"
        fi
    else
        fail "yacc failed on minimal grammar"
    fi
}

# Test 3: yacc generates header with -d
test_header_generation() {
    rm -f y.tab.h
    if $YACC -d $TESTDIR/minimal.y 2>/dev/null; then
        if [ -f y.tab.h ]; then
            pass "yacc -d generates y.tab.h"
        else
            fail "y.tab.h not generated with -d"
        fi
    else
        fail "yacc -d failed"
    fi
}

# Test 4: yacc generates verbose output with -v
test_verbose_output() {
    rm -f y.output
    if $YACC -v $TESTDIR/minimal.y 2>/dev/null; then
        if [ -f y.output ]; then
            pass "yacc -v generates y.output"
        else
            fail "y.output not generated with -v"
        fi
    else
        fail "yacc -v failed"
    fi
}

# Test 5: token definitions in header
test_token_definitions() {
    $YACC -d $TESTDIR/minimal.y 2>/dev/null
    if grep -q "#define NUMBER" y.tab.h; then
        pass "y.tab.h contains token definitions"
    else
        fail "y.tab.h missing token definitions"
    fi
}

# Test 6: expression grammar parses
test_expr_grammar() {
    if $YACC -dv $TESTDIR/expr.y 2>/dev/null; then
        pass "yacc accepts expression grammar"
    else
        fail "yacc failed on expression grammar"
    fi
}

# Test 7: multiple tokens defined correctly
test_multiple_tokens() {
    $YACC -d $TESTDIR/expr.y 2>/dev/null
    if grep -q "#define NUMBER" y.tab.h && \
       grep -q "#define PLUS" y.tab.h; then
        pass "Multiple tokens defined in y.tab.h"
    else
        fail "Missing token definitions"
    fi
}

# Test 8: YYSTYPE defined
test_yystype() {
    $YACC -d $TESTDIR/minimal.y 2>/dev/null
    if grep -q "YYSTYPE" y.tab.h; then
        pass "YYSTYPE defined in y.tab.h"
    else
        fail "YYSTYPE not found in y.tab.h"
    fi
}

# Test 9: yyparse function in output
test_yyparse() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "yyparse" y.tab.c; then
        pass "yyparse function in y.tab.c"
    else
        fail "yyparse not found in y.tab.c"
    fi
}

# Test 10: grammar statistics in verbose output  
test_grammar_stats() {
    $YACC -v $TESTDIR/expr.y 2>/dev/null
    if grep -q "terminals" y.output && grep -q "states" y.output; then
        pass "Grammar statistics in y.output"
    else
        fail "Grammar statistics missing"
    fi
}

# Test 11: YYMAXDEPTH defined in output
test_yymaxdepth() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "YYMAXDEPTH" y.tab.c; then
        pass "YYMAXDEPTH defined in y.tab.c"
    else
        fail "YYMAXDEPTH not found"
    fi
}

# Test 12: yylex extern declaration
test_yylex_extern() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "extern int yylex" y.tab.c; then
        pass "yylex extern declaration present"
    else
        fail "yylex extern not found"
    fi
}

# Test 13: yyerror extern declaration
test_yyerror_extern() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "extern void yyerror" y.tab.c; then
        pass "yyerror extern declaration present"
    else
        fail "yyerror extern not found"
    fi
}

# Test 14: Parser has yyreduce label
test_yyreduce() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "yyreduce:" y.tab.c; then
        pass "yyreduce label in parser"
    else
        fail "yyreduce label not found"
    fi
}

# Test 15: Parser has yyshift label
test_yyshift() {
    $YACC $TESTDIR/minimal.y 2>/dev/null
    if grep -q "yyshift:" y.tab.c; then
        pass "yyshift label in parser"
    else
        fail "yyshift label not found"
    fi
}

# Run all tests
echo "=== Yacc Test Suite ==="
echo ""

test_build
test_minimal_grammar
test_header_generation
test_verbose_output
test_token_definitions
test_expr_grammar
test_multiple_tokens
test_yystype
test_yyparse
test_grammar_stats
test_yymaxdepth
test_yylex_extern
test_yyerror_extern
test_yyreduce
test_yyshift

echo ""
echo "=== Results ==="

echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi

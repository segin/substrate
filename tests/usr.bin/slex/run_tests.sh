#!/bin/sh
#
# run_tests.sh - behaviour tests for slex(1).
#
#   sh run_tests.sh [path/to/slex]
#
# Each case writes a lex source, runs slex, compiles the scanner with $CC
# (default cc) and checks what the scanner does with a given input, or
# checks slex's own output and exit status.  Everything happens in a
# scratch directory.  Prints "ok"/"FAIL" per case and a final "Result:"
# line; exits non-zero if any case failed.

HERE=$(cd "$(dirname "$0")" && pwd)
SLEX=${1:-$HERE/../../../usr.bin/slex/slex}
case $SLEX in /*) ;; *) SLEX=$(pwd)/$SLEX ;; esac
CC=${CC:-cc}

[ -x "$SLEX" ] || { echo "run_tests.sh: no slex at $SLEX" >&2; exit 2; }

WORK=${TMPDIR:-/tmp}/slex-test.$$
rm -rf "$WORK"
mkdir "$WORK" || exit 2
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK" || exit 2

passed=0
failed=0

ok()   { echo "ok: $1"; passed=$((passed + 1)); }
fail() { echo "FAIL: $1"; failed=$((failed + 1)); }

# A main() for sources whose subroutines section doesn't provide one.
cat > driver.c <<'EOF'
int yylex(void);
int main(void) { yylex(); return 0; }
EOF

# scan NAME INPUT EXPECTED [slex args...]: slex the file NAME.l, build it
# (with driver.c unless the source has its own main) and check that the
# scanner turns INPUT into exactly EXPECTED.
scan() {
    name=$1 input=$2 expected=$3
    shift 3
    rm -f lex.yy.c scanner
    if ! "$SLEX" "$@" "$name.l" > slex.out 2> slex.err; then
        fail "$name: slex failed: $(cat slex.err)"
        return
    fi
    if grep -q 'main *(' "$name.l"; then
        extra=
    else
        extra=driver.c
    fi
    if ! $CC -w -o scanner lex.yy.c $extra > cc.err 2>&1; then
        fail "$name: lex.yy.c does not compile: $(head -5 cc.err)"
        return
    fi
    actual=$(printf '%s' "$input" | ./scanner)
    if [ "$actual" = "$expected" ]; then
        ok "$name"
    else
        fail "$name: expected '$expected', got '$actual'"
    fi
}

# --- command line --------------------------------------------------------

printf '%%%%\n[a-z]+ { printf("<%%s>", yytext); }\n' > word.l

rm -f lex.yy.c
"$SLEX" word.l > out 2> err
if [ $? -eq 0 ] && [ -f lex.yy.c ] && [ ! -s out ] && [ ! -s err ]; then
    ok "default: writes lex.yy.c and nothing else"
else
    fail "default: status/lex.yy.c/stdout/stderr wrong: $(cat out err | head -3)"
fi

rm -f lex.yy.c
"$SLEX" -t word.l > t.c 2> err
if [ ! -f lex.yy.c ] && [ ! -s err ] && $CC -w -o t t.c driver.c 2>/dev/null &&
   [ "$(printf 'ab cd' | ./t)" = "<ab> <cd>" ]; then
    ok "-t: the scanner, and only the scanner, on stdout"
else
    fail "-t: output is not a working scanner, or lex.yy.c was written"
fi

"$SLEX" -v word.l > out 2> err
if grep -q '^slex: 1 rules, 1 start conditions, [0-9]* DFA states$' out &&
   [ ! -s err ]; then
    ok "-v: statistics on stdout"
else
    fail "-v: expected statistics on stdout: $(cat out err)"
fi

"$SLEX" -t -v word.l > t.c 2> err
if grep -q '^slex: 1 rules' err && ! grep -q 'slex:' t.c; then
    ok "-t -v: statistics on stderr"
else
    fail "-t -v: statistics not (only) on stderr"
fi

"$SLEX" -v -n word.l > out 2> err
if [ ! -s out ] && [ ! -s err ]; then
    ok "-v -n: last option wins, no statistics"
else
    fail "-v -n: unexpected output: $(cat out err)"
fi

"$SLEX" -q word.l > out 2> err
if [ $? -ne 0 ] && grep -q 'usage: slex' err; then
    ok "unknown option: usage and failure"
else
    fail "unknown option: expected usage and a non-zero status"
fi

rm -f lex.yy.c
if "$SLEX" < word.l > /dev/null 2>&1 && [ -f lex.yy.c ]; then
    ok "no operands: reads standard input"
else
    fail "no operands: did not read standard input"
fi

rm -f lex.yy.c
if "$SLEX" - < word.l > /dev/null 2>&1 && [ -f lex.yy.c ]; then
    ok "operand '-': reads standard input"
else
    fail "operand '-': did not read standard input"
fi

if "$SLEX" does-not-exist.l > /dev/null 2>&1; then
    fail "missing input file: expected a non-zero status"
else
    ok "missing input file: fails"
fi

# --- sections ------------------------------------------------------------

printf '%%{\n#include <stdio.h>\n%%}\n' > part1.l
printf '%%%%\n' > part2.l
printf '[0-9]+ { printf("N"); }\n' > part3.l
rm -f lex.yy.c
if "$SLEX" part1.l part2.l part3.l > /dev/null 2>&1 &&
   $CC -w -o cat3 lex.yy.c driver.c 2>/dev/null &&
   [ "$(printf 'a1b22' | ./cat3)" = "aNbN" ]; then
    ok "several files are read as one source"
else
    fail "several files are not read as one source"
fi

printf 'X [a-z]\n{X} ;\n' > nodelim.l
"$SLEX" nodelim.l > out 2> err
if [ $? -ne 0 ] && grep -q 'expected marking of rules section' err; then
    ok "missing %%: error"
else
    fail "missing %%: expected an error"
fi

# yylex() refers to count before its definition in section 3.
cat > threesect.l <<'EOF'
%{
extern int count;
%}
%%
[a-z]+  { count++; }
.|\n    ;
%%
int count;
int main(void) { yylex(); printf("%d", count); return 0; }
EOF
scan threesect "one two three" "3"

# --- definitions section -------------------------------------------------

cat > defs.l <<'EOF'
    static int indented = 40;
%{
static int block = 2;
%}
DIGIT   [0-9]
NUM     {DIGIT}+
%%
{NUM}   { printf("%d", indented + block); }
EOF
scan defs "x7y" "x42y"

printf '%%%%\n{NOPE} ;\n' > undef.l
if "$SLEX" undef.l > /dev/null 2> err; then
    fail "undefined {name}: expected an error"
elif grep -q 'undefined name {NOPE}' err; then
    ok "undefined {name}: error"
else
    fail "undefined {name}: wrong message: $(cat err)"
fi

printf '%%{\nint x;\n%%%%\n' > nobrace.l
if "$SLEX" nobrace.l > /dev/null 2> err; then
    fail "missing %}: expected an error"
elif grep -q 'missing %} delimiter' err; then
    ok "missing %}: error"
else
    fail "missing %}: wrong message: $(cat err)"
fi

printf '%%%%\na\000b ;\n' > nul.l
if "$SLEX" nul.l > /dev/null 2> err; then
    fail "NUL in input: expected an error"
else
    ok "NUL in input: error"
fi

cp "$HERE/test_2_2.l" brace_comment.l
scan brace_comment "hello" "HI"

cp "$HERE/test_trigraph.l" trigraph.l
"$SLEX" trigraph.l > /dev/null 2> err
if grep -q 'trigraph ??=' err; then
    ok "trigraph in a code block: warning"
else
    fail "trigraph in a code block: no warning"
fi

# --- start conditions ----------------------------------------------------

cat > sc_inclusive_exclusive.l <<'EOF'
%s INC
%x EXC
%%
b           { printf("[b]"); }
1           BEGIN INC;
2           BEGIN EXC;
<INC,EXC>0  BEGIN INITIAL;
<EXC>c      { printf("[c]"); }
EOF
# b is active in INITIAL and the inclusive INC, not in the exclusive EXC.
scan sc_inclusive_exclusive "b1b2bc0b" "[b][b]b[c][b]"

cat > sc_block.l <<'EOF'
%x COMMENT
%{
int depth = 0;
%}
%%
"/*"        { BEGIN COMMENT; depth++; }
<COMMENT>{
"/*"        { depth++; }
"*/"        { if (--depth == 0) BEGIN INITIAL; }
.|\n        ;
}
[a-z]+      { printf("<%s>", yytext); }
[ \t\n]     ;
EOF
scan sc_block "a /* x /* y */ z */ b" "<a><b>"

# --- patterns ------------------------------------------------------------

cat > patterns.l <<'EOF'
%%
"if"            { printf("IF "); }
[a-z][a-z0-9]*  { printf("ID(%s) ", yytext); }
[0-9]+          { printf("NUM(%s) ", yytext); }
"=="|"!="       { printf("CMP "); }
=               { printf("SET "); }
[ \t\n]         ;
EOF
# Longest match wins (iffy, ==); on a tie the earlier rule wins (if).
scan patterns "if iffy x1 == y != 42 = z" \
    "IF ID(iffy) ID(x1) CMP ID(y) CMP NUM(42) SET ID(z) "

cat > intervals.l <<'EOF'
%%
a{3}        { printf("E(%s)", yytext); }
b{2,4}      { printf("R(%s)", yytext); }
c{2,}       { printf("M(%s)", yytext); }
[ \t\n]     ;
.           { printf("O(%s)", yytext); }
EOF
scan intervals "aaa bbbb bbb bb ccc ccccc d" \
    "E(aaa)R(bbbb)R(bbb)R(bb)M(ccc)M(ccccc)O(d)"

cat > bol_anchor.l <<'EOF'
%%
^x      { printf("B"); }
x       { printf("x"); }
\n      { printf("|"); }
EOF
scan bol_anchor "xx
xx" "Bx|Bx"

cat > negated_class.l <<'EOF'
%%
[^a-z\n]+   { printf("[%s]", yytext); }
\n          ;
EOF
scan negated_class "ab12cd;;e" "ab[12]cd[;;]e"

cat > default_echo.l <<'EOF'
%%
x       { printf("Y"); }
EOF
scan default_echo "axbxc" "aYbYc"

# --- runtime -------------------------------------------------------------

cat > reject.l <<'EOF'
%%
abcd    { printf("1(%s)", yytext); REJECT; }
abc     { printf("2(%s)", yytext); REJECT; }
a       { printf("3(%s)", yytext); }
.|\n    ;
EOF
scan reject "abcd" "1(abcd)2(abc)3(a)"

cp "$HERE/test_posix.l" yymore_yyless_input_unput.l
# yymore, yyless (plus the rule matching the rest), input() and unput().
scan yymore_yyless_input_unput "helloworld foobar inputX unput" "Tests passed: 5"

# input() then unput() of the byte right after the match: it must come
# back from input(), survive the unput(), and leave yytext alone.
cat > input_unput.l <<'EOF'
%%
ab      { int c = input(); unput(c); printf("[%c%s]", c, yytext); }
c       { printf("C"); }
\n      { printf("|"); }
EOF
scan input_unput "abcab
c" "[cab]C[
ab]|C"

cat > array_yytext.l <<'EOF'
%array
%%
[a-z]+  { printf("%d", (int)sizeof(yytext) > 1000); }
EOF
scan array_yytext "abc" "1"
if grep -q '^char yytext\[' lex.yy.c; then
    ok "%array: yytext declared as an array"
else
    fail "%array: yytext is not an array"
fi

cat > pointer_yytext.l <<'EOF'
%pointer
%%
[a-z]+  { printf("%d", (int)sizeof(yytext) == (int)sizeof(char *)); }
EOF
scan pointer_yytext "abc" "1"

echo
echo "Result: $([ $failed -eq 0 ] && echo PASS || echo FAIL) ($passed passed, $failed failed)"
[ $failed -eq 0 ]

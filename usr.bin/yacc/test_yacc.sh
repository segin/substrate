#!/bin/sh
# POSIX yacc conformance test runner.

set -e

YACC=./yacc
GRAMMARS=../../tests/usr.bin/yacc

make >/dev/null

fail() { echo "FAIL: $1"; exit 1; }
pass() { echo "ok: $1"; }

run_yacc() {
    rm -f /tmp/yt.tab.c /tmp/yt.tab.h /tmp/yt.output
    "$YACC" "$@" >/tmp/yt.stderr 2>&1
}

# 1. minimal.y - smoke test, parser executes and accepts.
run_yacc -v -d -b /tmp/yt "$GRAMMARS/minimal.y"
[ -f /tmp/yt.tab.c ] || fail "minimal: missing tab.c"
[ -f /tmp/yt.tab.h ] || fail "minimal: missing tab.h"
[ -f /tmp/yt.output ] || fail "minimal: missing y.output"

cat > /tmp/yt_drv.c <<'EOF'
#include "/tmp/yt.tab.h"
#include <stdio.h>
static int once;
int yylex(void) { if (once++) return 0; yylval = 7; return NUMBER; }
void yyerror(const char *s) { (void)s; }
int main(void) { return yyparse(); }
EOF
cc -Wall -Wextra -Werror -o /tmp/yt_parser /tmp/yt.tab.c /tmp/yt_drv.c
/tmp/yt_parser
pass "minimal.y"

# 2. State output sanity.
awk '/^State 2:/{f=1;next}/^State [0-9]+:/{if(f)exit}f' /tmp/yt.output > /tmp/yt.s2
grep -F 'NUMBER  shift' /tmp/yt.output >/dev/null || fail "minimal: NUMBER shift edge"
grep -F '$end  reduce using rule 2 (expr)' /tmp/yt.output >/dev/null || \
    fail "minimal: missing reduce on \$end"
pass "state output"

# 3. Default reduction in initial state must be 0 (no reduce on entry).
first_defred=$(awk '
    /yydefred\[\] = \{/ { in_arr = 1; next }
    in_arr && /\};/ { exit }
    in_arr {
        gsub(/[ ,]/, "", $0);
        if ($0 ~ /^-?[0-9]+$/) { print $0; exit }
    }' /tmp/yt.tab.c)
[ "$first_defred" = "0" ] || fail "minimal: state 0 has unexpected default reduction"
pass "yydefred[0]==0"

# 4. -d disabled: no header file.
rm -f /tmp/yt2.tab.c /tmp/yt2.tab.h
"$YACC" -b /tmp/yt2 "$GRAMMARS/minimal.y" >/dev/null
[ -f /tmp/yt2.tab.c ] || fail "no -d: missing tab.c"
[ -f /tmp/yt2.tab.h ] && fail "no -d: should not produce tab.h"
pass "-d off skips header"

# 5. -l suppresses #line directives.
"$YACC" -l -b /tmp/yt3 "$GRAMMARS/minimal.y" >/dev/null
grep -E '^#line ' /tmp/yt3.tab.c >/dev/null && fail "-l: #line was emitted"
pass "-l suppresses #line"

# 6. Default file_prefix is "y".
( cd /tmp && rm -f y.tab.c y.tab.h y.output && \
  "$OLDPWD/$YACC" -d -v "$OLDPWD/$GRAMMARS/minimal.y" >/dev/null && \
  [ -f y.tab.c ] && [ -f y.tab.h ] && [ -f y.output ] ) || fail "default prefix y.*"
pass "default prefix y.tab.{c,h}, y.output"

# 7. -p prefix renames external symbols.
"$YACC" -p foo_ -b /tmp/yt4 "$GRAMMARS/minimal.y" >/dev/null
grep -F '#define yyparse  foo_parse' /tmp/yt4.tab.c >/dev/null || \
    fail "-p: missing yyparse rename"
grep -F '#define yylval   foo_lval' /tmp/yt4.tab.c >/dev/null || \
    fail "-p: missing yylval rename"
pass "-p prefix substitution"

# 8. Char literals get character codes, not 257+.
"$YACC" -d -b /tmp/yt5 "$GRAMMARS/charlit.y" >/dev/null
grep -F "#define NUMBER" /tmp/yt5.tab.h >/dev/null || fail "charlit: NUMBER missing"
grep -E "#define '\\+'" /tmp/yt5.tab.h >/dev/null && \
    fail "charlit: '+' should not be #define'd"
pass "char literals untouched in header"

# 9. Explicit %token values.
"$YACC" -d -b /tmp/yt6 "$GRAMMARS/explicit_token_value.y" >/dev/null
grep -F "#define APPLE                300" /tmp/yt6.tab.h >/dev/null || \
    fail "explicit value: APPLE not 300"
grep -F "#define CHERRY               305" /tmp/yt6.tab.h >/dev/null || \
    fail "explicit value: CHERRY not 305"
pass "explicit token values"

# 10. %prec rule precedence override.
"$YACC" -v -b /tmp/yt7 "$GRAMMARS/prec_override.y" >/tmp/yt7.log 2>&1
grep -F "shift/reduce" /tmp/yt7.log >/dev/null && \
    fail "prec_override: should resolve all conflicts via precedence"
pass "%prec resolves conflicts"

# 11. Error recovery grammar compiles and uses the 'error' token.
"$YACC" -b /tmp/yt8 "$GRAMMARS/error_recovery.y" >/dev/null
grep -F "yyerrok" /tmp/yt8.tab.c >/dev/null || fail "yyerrok missing"
grep -F "yyclearin" /tmp/yt8.tab.c >/dev/null || fail "yyclearin missing"
grep -F "YYRECOVERING" /tmp/yt8.tab.c >/dev/null || fail "YYRECOVERING missing"
pass "error recovery primitives"

# 12. yacc library: -ly provides main() and yyerror().
"$YACC" -d -b /tmp/yt9 "$GRAMMARS/minimal.y" >/dev/null
cat > /tmp/yt9_lex.c <<'EOF'
#include "/tmp/yt9.tab.h"
static int once;
int yylex(void) { if (once++) return 0; yylval = 99; return NUMBER; }
EOF
cc -Wall -Wextra -Werror -I../../lib/yacc -o /tmp/yt9_app \
    /tmp/yt9.tab.c /tmp/yt9_lex.c ../../lib/yacc/liby.a
/tmp/yt9_app
pass "liby.a (-ly) provides main() and yyerror()"

# 13. Generated header emits proper guards.
grep -F "extern YYSTYPE yylval" /tmp/yt.tab.h >/dev/null || \
    fail "header: extern YYSTYPE yylval missing"
grep -F "int yyparse(void)" /tmp/yt.tab.h >/dev/null || \
    fail "header: yyparse prototype missing"
grep -F "yyerror" /tmp/yt.tab.h >/dev/null && \
    fail "header: must not declare yyerror"
grep -F "yylex" /tmp/yt.tab.h >/dev/null && \
    fail "header: must not declare yylex"
pass "header file conformance"

# 14. Deterministic output across runs.
"$YACC" -v -b /tmp/ytA "$GRAMMARS/expr.y" >/dev/null
"$YACC" -v -b /tmp/ytB "$GRAMMARS/expr.y" >/dev/null
cmp -s /tmp/ytA.output /tmp/ytB.output || fail "non-deterministic output"
pass "deterministic"

# 15. Closure expansion, READS, conflict-with-prec, conflict-no-prec.
for g in closure_chain reads_nullable conflict_with_prec conflict_no_prec list precedence midrule; do
    "$YACC" -v -d -b /tmp/y_$g "$GRAMMARS/$g.y" >/tmp/y_$g.log 2>&1 || \
        fail "regression: $g.y"
done
grep -F "shift/reduce" /tmp/y_conflict_no_prec.log >/dev/null || \
    fail "conflict_no_prec: expected S/R conflict"
grep -F "shift/reduce" /tmp/y_conflict_with_prec.log >/dev/null && \
    fail "conflict_with_prec: precedence should resolve all"
pass "regression suite"

# 16. -t selects YYDEBUG=1.
"$YACC" -t -b /tmp/ytt "$GRAMMARS/minimal.y" >/dev/null
grep -F "#define YYDEBUG 1" /tmp/ytt.tab.c >/dev/null || fail "-t: YYDEBUG not set to 1"
"$YACC" -b /tmp/ytt2 "$GRAMMARS/minimal.y" >/dev/null
grep -F "#define YYDEBUG 0" /tmp/ytt2.tab.c >/dev/null || fail "no -t: YYDEBUG should be 0"
pass "-t YYDEBUG selection"

# 17. Error: no operand.
if "$YACC" 2>/dev/null; then fail "no operand should error"; fi
pass "missing operand returns nonzero"

rm -f /tmp/yt*.tab.c /tmp/yt*.tab.h /tmp/yt*.output /tmp/yt*.log /tmp/yt*.stderr
rm -f /tmp/yt*_drv.c /tmp/yt*_lex.c /tmp/yt_parser /tmp/yt9_app
rm -f /tmp/y_*.tab.c /tmp/y_*.tab.h /tmp/y_*.output /tmp/y_*.log
rm -f /tmp/yt.s2

echo "ALL TESTS PASSED"

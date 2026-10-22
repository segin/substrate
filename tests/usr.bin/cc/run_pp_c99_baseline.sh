#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
# a private directory for this run's files (the suite's Makefile passes its own)
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

"$CC_BIN" -E -I. -isystem . -iquote . -include pp_s3_force.h -imacros pp_s3_imacros.h pp_s3_main.c -o $CC_TMP/cc_pp_s3.i
grep -q "int var = 10;" $CC_TMP/cc_pp_s3.i
grep -q 'const char \*s = "hello";' $CC_TMP/cc_pp_s3.i
grep -q "int forced_visible = 7;" $CC_TMP/cc_pp_s3.i
grep -q "int imports = 1 + 2 + 7 + 5;" $CC_TMP/cc_pp_s3.i
grep -q '^#line ' $CC_TMP/cc_pp_s3.i
grep -q '#line 77 "virt.c"' $CC_TMP/cc_pp_s3.i
! grep -q "should_not_appear_from_imacros" $CC_TMP/cc_pp_s3.i

"$CC_BIN" -E -P -I. -isystem . -iquote . -include pp_s3_force.h -imacros pp_s3_imacros.h pp_s3_main.c -o $CC_TMP/cc_pp_s3_p.i
! grep -q '^#line ' $CC_TMP/cc_pp_s3_p.i

"$CC_BIN" -E -dM -DUSERDEF=9 -I. -isystem . pp_s3_main.c -o $CC_TMP/cc_pp_s3_dM.i
grep -q '^#define USERDEF 9$' $CC_TMP/cc_pp_s3_dM.i
grep -q '^#define OBJ 10$' $CC_TMP/cc_pp_s3_dM.i

"$CC_BIN" -E -M -I. -isystem . pp_s3_main.c > $CC_TMP/cc_pp_s3_m.d
grep -q 'pp_s3_main.c' $CC_TMP/cc_pp_s3_m.d
grep -q 'pp_s3_quote.h' $CC_TMP/cc_pp_s3_m.d
grep -q 'pp_s3_system.h' $CC_TMP/cc_pp_s3_m.d

"$CC_BIN" -E -MM -I. -isystem . pp_s3_main.c > $CC_TMP/cc_pp_s3_mm.d
grep -q 'pp_s3_quote.h' $CC_TMP/cc_pp_s3_mm.d
! grep -q 'pp_s3_system.h' $CC_TMP/cc_pp_s3_mm.d

"$CC_BIN" -E -MD -MF $CC_TMP/cc_pp_s3_md.d -MT out.o -MQ '$dep:target' -I. -isystem . pp_s3_main.c -o $CC_TMP/cc_pp_s3_md.i
test -s $CC_TMP/cc_pp_s3_md.d
grep -q 'out.o' $CC_TMP/cc_pp_s3_md.d
grep -q '\$\$dep\\\\:target' $CC_TMP/cc_pp_s3_md.d

# Standard input, as GCC treats it (Linux's checksyscalls.sh runs
# `cc -E -x c - -Wp,-MD,<file>` and fixdep then opens every dependency):
# named "<stdin>" in line markers and __FILE__, not a dependency, rule
# target "-".  The driver's temporary copy appeared in all three, and was
# gone by the time fixdep looked for it.
printf '#include "pp_s3_quote.h"\nconst char *f = __FILE__;\n' |
	"$CC_BIN" -E -x c - -I. -Wp,-MD,$CC_TMP/cc_pp_stdin.d -o $CC_TMP/cc_pp_stdin.i
grep -q '^#line 1 "<stdin>"' $CC_TMP/cc_pp_stdin.i
grep -q 'const char \*f = "<stdin>";' $CC_TMP/cc_pp_stdin.i
! grep -q 'ccstdin' $CC_TMP/cc_pp_stdin.i $CC_TMP/cc_pp_stdin.d
grep -q '^-:' $CC_TMP/cc_pp_stdin.d
grep -q 'pp_s3_quote.h' $CC_TMP/cc_pp_stdin.d
printf 'int x = ;\n' | "$CC_BIN" -c -x c - -o $CC_TMP/cc_pp_stdin.o 2>$CC_TMP/cc_pp_stdin.err || true
grep -q '^<stdin>:1:' $CC_TMP/cc_pp_stdin.err

"$CC_BIN" -E -v -I. -isystem . pp_s3_main.c -o $CC_TMP/cc_pp_s3_v.i 2>$CC_TMP/cc_pp_s3_v.log
grep -q 'cpp include search paths:' $CC_TMP/cc_pp_s3_v.log
grep -q '/usr/include' $CC_TMP/cc_pp_s3_v.log

DEPTH_DIR=$CC_TMP/cc_pp_depth_$$
mkdir -p "$DEPTH_DIR"
i=0
while [ $i -lt 130 ]; do
	next=$((i + 1))
	echo "#include \"h$next.h\"" > "$DEPTH_DIR/h$i.h"
	i=$next
done
echo "int depth_ok = 1;" > "$DEPTH_DIR/h130.h"
echo '#include "h0.h"' > "$DEPTH_DIR/main.c"
! "$CC_BIN" -E -I"$DEPTH_DIR" "$DEPTH_DIR/main.c" -o $CC_TMP/cc_pp_s3_depth.i
rm -rf "$DEPTH_DIR"

MACRO_DEPTH=$CC_TMP/cc_pp_macro_depth_$$.c
: > "$MACRO_DEPTH"
i=0
while [ $i -lt 40 ]; do
	next=$((i + 1))
	echo "#define M$i M$next" >> "$MACRO_DEPTH"
	i=$next
done
echo "#define M40 1" >> "$MACRO_DEPTH"
echo "int macro_depth = M0;" >> "$MACRO_DEPTH"
! "$CC_BIN" -E "$MACRO_DEPTH" -o $CC_TMP/cc_pp_s3_macro.i
! "$CC_BIN" -E -fpp-max-macro-depth=8 "$MACRO_DEPTH" -o $CC_TMP/cc_pp_s3_macro_limit.i
rm -f "$MACRO_DEPTH"

TOK_LIMIT=$CC_TMP/cc_pp_token_limit_$$.c
echo "int tok = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10 + 11 + 12;" > "$TOK_LIMIT"
! "$CC_BIN" -E -fpp-max-tokens=8 "$TOK_LIMIT" -o $CC_TMP/cc_pp_s3_token_limit.i
rm -f "$TOK_LIMIT"

! "$CC_BIN" -E -fpp-max-output-bytes=32 -I. pp_s3_main.c -o $CC_TMP/cc_pp_s3_output_limit.i

ln -sf "$CC_BIN" $CC_TMP/cpp
$CC_TMP/cpp -DTESTVAL=1 driver_pp_input.c -o $CC_TMP/cc_pp_cpp_mode.i
grep -q "int x = 1;" $CC_TMP/cc_pp_cpp_mode.i

cat > $CC_TMP/cc_pp_cpp_mode_input.h <<'EOF'
#define ANSWER2 TESTVAL + 2
int y = ANSWER2;
EOF
$CC_TMP/cpp -DTESTVAL=3 $CC_TMP/cc_pp_cpp_mode_input.h -o $CC_TMP/cc_pp_cpp_mode_h.i
grep -q "int y = 3 + 2;" $CC_TMP/cc_pp_cpp_mode_h.i

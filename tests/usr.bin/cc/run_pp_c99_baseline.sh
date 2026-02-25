#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"

"$CC_BIN" -E -I. -isystem . -iquote . -include pp_s3_force.h -imacros pp_s3_imacros.h pp_s3_main.c -o /tmp/cc_pp_s3.i
grep -q "int var = 10;" /tmp/cc_pp_s3.i
grep -q 'const char \*s = "hello";' /tmp/cc_pp_s3.i
grep -q "int forced_visible = 7;" /tmp/cc_pp_s3.i
grep -q "int imports = 1 + 2 + 7 + 5;" /tmp/cc_pp_s3.i
grep -q '^#line ' /tmp/cc_pp_s3.i
grep -q '#line 77 "virt.c"' /tmp/cc_pp_s3.i
! grep -q "should_not_appear_from_imacros" /tmp/cc_pp_s3.i

"$CC_BIN" -E -P -I. -isystem . -iquote . -include pp_s3_force.h -imacros pp_s3_imacros.h pp_s3_main.c -o /tmp/cc_pp_s3_p.i
! grep -q '^#line ' /tmp/cc_pp_s3_p.i

"$CC_BIN" -E -dM -DUSERDEF=9 -I. -isystem . pp_s3_main.c -o /tmp/cc_pp_s3_dM.i
grep -q '^#define USERDEF 9$' /tmp/cc_pp_s3_dM.i
grep -q '^#define OBJ 10$' /tmp/cc_pp_s3_dM.i

"$CC_BIN" -E -M -I. -isystem . pp_s3_main.c > /tmp/cc_pp_s3_m.d
grep -q 'pp_s3_main.c' /tmp/cc_pp_s3_m.d
grep -q 'pp_s3_quote.h' /tmp/cc_pp_s3_m.d
grep -q 'pp_s3_system.h' /tmp/cc_pp_s3_m.d

"$CC_BIN" -E -MM -I. -isystem . pp_s3_main.c > /tmp/cc_pp_s3_mm.d
grep -q 'pp_s3_quote.h' /tmp/cc_pp_s3_mm.d
! grep -q 'pp_s3_system.h' /tmp/cc_pp_s3_mm.d

"$CC_BIN" -E -MD -MF /tmp/cc_pp_s3_md.d -MT out.o -MQ '$dep:target' -I. -isystem . pp_s3_main.c -o /tmp/cc_pp_s3_md.i
test -s /tmp/cc_pp_s3_md.d
grep -q 'out.o' /tmp/cc_pp_s3_md.d
grep -q '\$\$dep\\\\:target' /tmp/cc_pp_s3_md.d

"$CC_BIN" -E -v -I. -isystem . pp_s3_main.c -o /tmp/cc_pp_s3_v.i 2>/tmp/cc_pp_s3_v.log
grep -q 'cpp include search paths:' /tmp/cc_pp_s3_v.log
grep -q '/usr/include' /tmp/cc_pp_s3_v.log

DEPTH_DIR=/tmp/cc_pp_depth_$$
mkdir -p "$DEPTH_DIR"
i=0
while [ $i -lt 130 ]; do
	next=$((i + 1))
	echo "#include \"h$next.h\"" > "$DEPTH_DIR/h$i.h"
	i=$next
done
echo "int depth_ok = 1;" > "$DEPTH_DIR/h130.h"
echo '#include "h0.h"' > "$DEPTH_DIR/main.c"
! "$CC_BIN" -E -I"$DEPTH_DIR" "$DEPTH_DIR/main.c" -o /tmp/cc_pp_s3_depth.i
rm -rf "$DEPTH_DIR"

MACRO_DEPTH=/tmp/cc_pp_macro_depth_$$.c
: > "$MACRO_DEPTH"
i=0
while [ $i -lt 40 ]; do
	next=$((i + 1))
	echo "#define M$i M$next" >> "$MACRO_DEPTH"
	i=$next
done
echo "#define M40 1" >> "$MACRO_DEPTH"
echo "int macro_depth = M0;" >> "$MACRO_DEPTH"
! "$CC_BIN" -E "$MACRO_DEPTH" -o /tmp/cc_pp_s3_macro.i
rm -f "$MACRO_DEPTH"

ln -sf "$CC_BIN" /tmp/cpp
/tmp/cpp -DTESTVAL=1 driver_pp_input.c -o /tmp/cc_pp_cpp_mode.i
grep -q "int x = 1;" /tmp/cc_pp_cpp_mode.i

cat > /tmp/cc_pp_cpp_mode_input.h <<'EOF'
#define ANSWER2 TESTVAL + 2
int y = ANSWER2;
EOF
/tmp/cpp -DTESTVAL=3 /tmp/cc_pp_cpp_mode_input.h -o /tmp/cc_pp_cpp_mode_h.i
grep -q "int y = 3 + 2;" /tmp/cc_pp_cpp_mode_h.i

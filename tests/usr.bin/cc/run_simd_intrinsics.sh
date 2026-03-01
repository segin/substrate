#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
INC_DIR="-I$ROOT/include"

"$CC_BIN" -std=gnu11 $INC_DIR native_c99_simd_intrinsics.c -o /tmp/cc_native_c99_simd_intrinsics
/tmp/cc_native_c99_simd_intrinsics

"$CC_BIN" -std=gnu11 -m32 $INC_DIR -c native_c99_simd_intrinsics.c -o /tmp/cc_native_c99_simd_intrinsics_32.o
file /tmp/cc_native_c99_simd_intrinsics_32.o | grep -q "ELF 32-bit"

cat > /tmp/cc_simd_macro_probe.c <<'EOF'
#ifdef __AVX__
int avx = 1;
#else
int avx = 0;
#endif
#ifdef __AVX2__
int avx2 = 1;
#else
int avx2 = 0;
#endif
EOF

"$CC_BIN" -E -P /tmp/cc_simd_macro_probe.c -o /tmp/cc_simd_macro_none.i
grep -q '^int avx = 0;$' /tmp/cc_simd_macro_none.i
grep -q '^int avx2 = 0;$' /tmp/cc_simd_macro_none.i

"$CC_BIN" -E -P -mavx /tmp/cc_simd_macro_probe.c -o /tmp/cc_simd_macro_avx.i
grep -q '^int avx = 1;$' /tmp/cc_simd_macro_avx.i
grep -q '^int avx2 = 0;$' /tmp/cc_simd_macro_avx.i

"$CC_BIN" -E -P -mavx2 /tmp/cc_simd_macro_probe.c -o /tmp/cc_simd_macro_avx2.i
grep -q '^int avx = 1;$' /tmp/cc_simd_macro_avx2.i
grep -q '^int avx2 = 1;$' /tmp/cc_simd_macro_avx2.i

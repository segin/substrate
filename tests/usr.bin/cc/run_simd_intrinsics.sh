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

cat > /tmp/cc_host_x86intrin_pclmul.c <<'EOF'
#include <x86intrin.h>
int main(void) {
	__m128i a = _mm_setzero_si128();
	__m128i c = _mm_set_epi64x(2, 3);
	__m128i b = _mm_set1_epi64x(1);
	a = _mm_clmulepi64_si128(a, b, 0x00);
	a = _mm_xor_si128(a, c);
	a = _mm_shuffle_epi8(a, b);
	return __builtin_cpu_supports("pclmul") ? 0 : 0;
}
EOF

"$CC_BIN" -std=gnu11 -c /tmp/cc_host_x86intrin_pclmul.c -o /tmp/cc_host_x86intrin_pclmul.o

cat > /tmp/cc_host_x86intrin_target.c <<'EOF'
#include <x86intrin.h>
#if defined __GNUC__ || defined __clang__
__attribute__((__target__("pclmul,avx")))
#endif
int probe(void) {
	__m128i a = _mm_setzero_si128();
	__m128i c = _mm_set_epi64x(2, 3);
	__m128i b = _mm_set1_epi64x(1);
	a = _mm_clmulepi64_si128(a, b, 0x00);
	a = _mm_xor_si128(a, c);
	a = _mm_shuffle_epi8(a, b);
	return __builtin_cpu_supports("pclmul");
}
EOF

"$CC_BIN" -std=gnu11 -c /tmp/cc_host_x86intrin_target.c -o /tmp/cc_host_x86intrin_target.o

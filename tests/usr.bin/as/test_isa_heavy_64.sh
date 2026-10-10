#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-isa-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/isa.c" <<'SRC'
#include <immintrin.h>
#include <stdint.h>
#include <stddef.h>

__attribute__((target("avx2,bmi2,lzcnt,popcnt")))
uint64_t isa_heavy(const uint64_t *in, size_t n) {
    __m256i acc = _mm256_setzero_si256();
    for (size_t i = 0; i + 4 <= n; i += 4) {
        __m256i v = _mm256_loadu_si256((const __m256i *)&in[i]);
        __m256i w = _mm256_slli_epi64(v, 13);
        __m256i x = _mm256_srli_epi64(v, 7);
        acc = _mm256_xor_si256(acc, _mm256_add_epi64(w, x));
    }
    uint64_t t[4];
    _mm256_storeu_si256((__m256i *)t, acc);
    uint64_t out = t[0] ^ t[1] ^ t[2] ^ t[3];
    out ^= (uint64_t)_lzcnt_u64(out | 1ULL);
    out ^= (uint64_t)_popcnt64(out);
    out = _pext_u64(out, 0x5555555555555555ULL) ^ _pdep_u64(out, 0xAAAAAAAAAAAAAAAAULL);
    return out;
}
SRC

gcc -m64 -O3 -march=x86-64-v3 -fno-plt -g -S -o "$TMP/isa64.s" "$TMP/isa.c"
"$AS" -64 -march=x86-64-v3 -o "$TMP/isa64.o" "$TMP/isa64.s"
"$LD" -m64 -r -o "$TMP/isa64.r.o" "$TMP/isa64.o"

readelf -h "$TMP/isa64.o" | grep -q "ELF64"
readelf -h "$TMP/isa64.o" | grep -q "Advanced Micro Devices X86-64"

echo "ok: ISA-heavy 64-bit assembly assembled and linked"

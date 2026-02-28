#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
AS="$ROOT/usr.bin/as/as"
LD="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/as-complex-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/complex.c" <<'SRC'
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t x;
    uint64_t y;
    uint32_t tag;
    uint32_t len;
    uint8_t data[128];
} node_t;

static uint64_t rotl64(uint64_t v, unsigned s) {
    return (v << s) | (v >> (64u - s));
}

static uint64_t mix64(uint64_t a, uint64_t b) {
    uint64_t v = a ^ (b + 0x9e3779b97f4a7c15ULL);
    v ^= rotl64(v, 25);
    v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 28;
    v *= 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

static int score(node_t *n, int k) {
    int s = 0;
    for (int i = 0; i < (int)n->len; ++i) {
        int v = (int)n->data[i];
        if (v & 1) s += (v * (k + 3));
        else s -= (v ^ (k * 11));
        if ((i & 7) == 0) s ^= (s << 3);
    }
    switch ((n->tag + (uint32_t)k) & 7u) {
        case 0: s += (int)n->x; break;
        case 1: s -= (int)n->y; break;
        case 2: s ^= (int)(n->x >> 17); break;
        case 3: s += (int)(n->y >> 19); break;
        case 4: s -= (int)(n->x ^ n->y); break;
        default: s += k * 13; break;
    }
    return s;
}

static uint64_t reduce_sum(const uint64_t *arr, size_t n) {
    uint64_t acc = 0;
    for (size_t i = 0; i < n; ++i) {
        acc += mix64(arr[i], acc + i);
    }
    return acc;
}

int complicated_main(int n, char **argv) {
    (void)argv;
    node_t nodes[32];
    uint64_t tmp[64];

    for (int i = 0; i < 32; ++i) {
        nodes[i].x = (uint64_t)i * 0x123456789ULL;
        nodes[i].y = (uint64_t)(i + 7) * 0xfedcba987ULL;
        nodes[i].tag = (uint32_t)(i * 3u);
        nodes[i].len = 128;
        for (int j = 0; j < 128; ++j) {
            nodes[i].data[j] = (uint8_t)((i * j + j * 17 + 13) & 0xff);
        }
    }

    for (int i = 0; i < 64; ++i) {
        int idx = (i * 7 + n) & 31;
        tmp[i] = (uint64_t)(score(&nodes[idx], n + i)) ^ mix64(nodes[idx].x, nodes[idx].y);
    }

    uint64_t r = reduce_sum(tmp, 64);
    return (int)(r & 0x7fffffff);
}
SRC

gcc -m64 -O3 -fPIC -fstack-protector-strong -fno-plt -fverbose-asm -g -S -o "$TMP/complex64.s" "$TMP/complex.c"
gcc -m32 -O2 -fPIC -fstack-protector-strong -fno-plt -fverbose-asm -g -S -o "$TMP/complex32.s" "$TMP/complex.c"

"$AS" -64 -march=x86-64-v2 -Wa,--gdwarf-2 -o "$TMP/complex64.o" "$TMP/complex64.s"
"$AS" -32 -march=generic -Wa,--gdwarf-2 -o "$TMP/complex32.o" "$TMP/complex32.s"

"$LD" -m64 -r -o "$TMP/complex64.r.o" "$TMP/complex64.o"
"$LD" -m32 -r -o "$TMP/complex32.r.o" "$TMP/complex32.o"

echo "ok: complex control-flow assembled and linked for 32/64-bit"

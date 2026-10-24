/*
 * The register allocator under pressure: more values live at once than
 * i386 has registers, so that some are spilled and reloaded; values live
 * across calls, which only the callee-saved registers keep; division and
 * shifts by a variable count, which need %eax, %edx and %ecx; bytes
 * stored from values that may live in %esi or %edi, which have no byte
 * register; 64-bit values in two words; regparm calls; inline asm
 * operands; a local whose address is taken, changed through a pointer
 * while the others stay in registers; structures passed and returned by
 * value; and variadic arguments.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static volatile int vzero;

static int __attribute__((noinline)) id(int x) {
    return x + vzero;
}

static long long __attribute__((noinline)) id64(long long x) {
    return x + vzero;
}

#if defined(__i386__)
#define REGPARM __attribute__((regparm(3)))
#else
#define REGPARM
#endif

static int __attribute__((noinline)) REGPARM rp3(int a, int b, int c) {
    return a * 100 + b * 10 + c;
}

static long long __attribute__((noinline)) REGPARM rp64(int a, long long b) {
    return b * a - 7;
}

/* twelve values live through the loop: some of them must be spilled */
static unsigned pressure(const unsigned *p, int n) {
    unsigned a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i2 = 9, j = 10, k = 11, l = 12;
    int i;

    for (i = 0; i < n; ++i) {
        unsigned x = p[i];

        a += x;
        b ^= x << 1;
        c -= x >> 2;
        d += a * 3;
        e ^= b + c;
        f += d >> 3;
        g -= e;
        h += f ^ g;
        i2 += h * 5;
        j ^= i2 >> 1;
        k += j + a;
        l -= k ^ b;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i2 ^ j ^ k ^ l;
}

/* values live across calls */
static int across_calls(int x, int y) {
    int a = id(x);
    int b = id(y);
    int c = id(a + b);
    int d = id(a - b);
    int e = id(c * d);

    return a + b * 3 + c * 5 + d * 7 + e * 11;
}

/* the dividend, divisor and count wherever the allocator put them */
static int divs_and_shifts(int a, int b, unsigned s, unsigned t) {
    int q = a / b;
    int r = a % b;
    unsigned uq = (unsigned)a / (unsigned)(b + 100);
    unsigned ur = (unsigned)a % (unsigned)(b + 100);
    int sl = a << (s & 31);
    int sr = a >> (t & 31);
    unsigned usr = (unsigned)a >> (s & 31);

    return q * 3 + r * 5 + (int)uq * 7 + (int)ur * 11 + (sl ^ sr ^ (int)usr);
}

/* bytes stored from many values at once, some of them in %esi or %edi */
static void bytes(unsigned char *out, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
    int i;

    for (i = 0; i < 4; ++i) {
        out[5 * i + 0] = (unsigned char)a;
        out[5 * i + 1] = (unsigned char)b;
        out[5 * i + 2] = (unsigned char)c;
        out[5 * i + 3] = (unsigned char)d;
        out[5 * i + 4] = (unsigned char)e;
        a += 3;
        b ^= 0x5a;
        c *= 7;
        d -= 9;
        e += a;
    }
}

static long long wide(long long a, long long b, int n) {
    long long s = 0;
    int i;

    for (i = 0; i < n; ++i) {
        s += id64(a) * (i + 1) - b;
        a ^= s >> 3;
        b += a << 2;
    }
    return s ^ a ^ b;
}

static int asm_ops(int x, int y) {
    int r = x;
#if defined(__i386__) || defined(__x86_64__)
    int m;

    __asm__("addl %1, %0" : "+r"(r) : "r"(y));
    __asm__("movl %1, %0" : "=m"(m) : "r"(r + 1));
    r += m;
    __asm__("imull %2, %0" : "=r"(m) : "0"(r), "r"(x + 3));
    r ^= m;
#endif
    return r;
}

static void bump(int *p) {
    *p += 5;
}

/* a local whose address is taken, written through the pointer between reads */
static int taken(int x) {
    int v = x;
    int a = x * 2, b = x * 3, c = x * 4;

    bump(&v);
    a += v;
    bump(&v);
    b += v;
    v ^= 1;
    bump(&v);
    c += v;
    return a + b + c + v;
}

struct small {
    short a;
    short b;
};

struct big {
    int w[5];
};

static struct small __attribute__((noinline)) make_small(int x) {
    struct small s;

    s.a = (short)(x + 1);
    s.b = (short)(x * 2);
    return s;
}

static struct big __attribute__((noinline)) make_big(int x) {
    struct big b;
    int i;

    for (i = 0; i < 5; ++i) {
        b.w[i] = x + i * i;
    }
    return b;
}

static int structs(int x) {
    struct small s = make_small(x);
    struct big b = make_big(x + s.a);
    int i;
    int t = s.b;

    for (i = 0; i < 5; ++i) {
        t += b.w[i] * (i + 1);
    }
    return t;
}

static int __attribute__((noinline)) by_value(struct big b, int k) {
    return b.w[0] * k + b.w[4] - b.w[2];
}

static int __attribute__((noinline)) sum_va(int n, ...) {
    va_list ap;
    int t = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        t = t * 3 + va_arg(ap, int);
    }
    va_end(ap);
    return t;
}

int main(void) {
    unsigned data[16];
    unsigned char out[20];
    int i;

    for (i = 0; i < 16; ++i) {
        data[i] = (unsigned)(i * 2654435761u) ^ (unsigned)(i << 7);
    }
    printf("pressure %u %u\n", pressure(data, 16), pressure(data + 3, 9));
    printf("across %d %d\n", across_calls(17, 5), across_calls(-3, 40));
    printf("divs %d %d\n", divs_and_shifts(1000003, 37, 5, 3), divs_and_shifts(-77777, 13, 30, 31));
    memset(out, 0, sizeof(out));
    bytes(out, 0x12345678, 0x9abcdef0, 0x0f0f0f0f, 0x31, 0xfe);
    for (i = 0; i < 20; ++i) {
        printf("%02x", out[i]);
    }
    printf("\n");
    printf("wide %lld %lld\n", wide(123456789012LL, -987654321LL, 7), wide(-5, 3, 3));
    printf("rp %d %lld\n", rp3(id(1), id(2), id(3)), rp64(id(6), id64(1LL << 40)));
    printf("asm %d %d\n", asm_ops(5, 9), asm_ops(-100, 33));
    printf("taken %d %d\n", taken(3), taken(-11));
    printf("structs %d %d\n", structs(4), structs(-9));
    printf("by value %d\n", by_value(make_big(id(6)), id(3)));
    printf("va %d %d\n", sum_va(4, 1, 2, 3, 4), sum_va(6, id(9), -8, 7, id(-6), 5, 4));
    return 0;
}

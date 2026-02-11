#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>

void exit(int status) {
    _exit(status);
}

void abort(void) {
    _exit(134);
}

void __stack_chk_fail(void) {
    _exit(127);
}

// Simple bump allocator for now, 1MB heap
static char heap[1024 * 1024];
static size_t heap_ptr = 0;

void *malloc(size_t size) {
    if (heap_ptr + size > sizeof(heap)) return NULL;
    void *ptr = &heap[heap_ptr];
    heap_ptr += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        char *p = ptr;
        for(size_t i=0; i<total; i++) p[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    return malloc(size); 
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    size_t rem = heap_ptr % alignment;
    if (rem) {
        size_t padding = alignment - rem;
        if (heap_ptr + padding > sizeof(heap)) return NULL;
        heap_ptr += padding;
    }
    return malloc(size);
}

void quick_exit(int status) {
    _exit(status);
}

int at_quick_exit(void (*func)(void)) {
    (void)func;
    return 0;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc;
    int c;
    unsigned long cutoff;
    int neg = 0, any, cutlim;

    if (base < 0 || base == 1 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }

    while (isspace(*s)) s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if ((base == 0 || base == 16) && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    }
    if (base == 0) base = *s == '0' ? 8 : 10;

    cutoff = neg ? -(unsigned long)-2147483648L : 2147483647L; // Simplified LONG_MAX/MIN for 32-bit
    cutlim = cutoff % (unsigned long)base;
    cutoff /= (unsigned long)base;
    for (acc = 0, any = 0;; c = *s++) {
        if (isdigit(c)) c -= '0';
        else if (isalpha(c)) c -= isupper(c) ? 'A' - 10 : 'a' - 10;
        else break;
        if (c >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
            any = -1;
        else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }
    if (any < 0) {
        acc = neg ? -2147483648L : 2147483647L;
    } else if (neg) {
        acc = -acc;
    }
    if (endptr != 0) *endptr = (char *)(any ? s - 1 : nptr);
    return acc;
}

int atoi(const char *nptr) {
    return (int)atol(nptr);
}

long atol(const char *nptr) {
    long res = 0;
    int sign = 1;
    while (isspace(*nptr)) nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;
    while (isdigit(*nptr)) {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    return res * sign;
}

long long atoll(const char *nptr) {
    return (long long)atol(nptr);
}

double atof(const char *nptr) {
    (void)nptr;
    return 0.0;
}

char *getenv(const char *name) {
    (void)name;
    return NULL;
}

int system(const char *command) {
    if (!command) return 1;
    // Stub
    return -1;
}

int abs(int j) { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }
long long llabs(long long j) { return j < 0 ? -j : j; }

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    (void)base; (void)nmemb; (void)size; (void)compar;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    (void)key; (void)base; (void)nmemb; (void)size; (void)compar;
    return NULL;
}

/* ChaCha20 implementation */
static uint32_t chacha_state[16];
static uint32_t chacha_block[16];
static int chacha_idx = 16;
static int seeded = 0;

static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    int i;
    uint32_t x[16];

    for (i = 0; i < 16; ++i) x[i] = in[i];

    for (i = 0; i < 20; i += 2) {
        #define QR(a,b,c,d) \
            x[a] += x[b]; x[d] ^= x[a]; x[d] = (x[d] << 16) | (x[d] >> 16); \
            x[c] += x[d]; x[b] ^= x[c]; x[b] = (x[b] << 12) | (x[b] >> 20); \
            x[a] += x[b]; x[d] ^= x[a]; x[d] = (x[d] << 8) | (x[d] >> 24); \
            x[c] += x[d]; x[b] ^= x[c]; x[b] = (x[b] << 7) | (x[b] >> 25);

        QR(0, 4, 8, 12); QR(1, 5, 9, 13); QR(2, 6, 10, 14); QR(3, 7, 11, 15);
        QR(0, 5, 10, 15); QR(1, 6, 11, 12); QR(2, 7, 8, 13); QR(3, 4, 9, 14);
        #undef QR
    }

    for (i = 0; i < 16; ++i) out[i] = x[i] + in[i];
}

void srand(unsigned int seed) {
    // "expand 32-byte k"
    chacha_state[0] = 0x61707865;
    chacha_state[1] = 0x3320646e;
    chacha_state[2] = 0x79622d32;
    chacha_state[3] = 0x6b206574;
    // Key (use seed repeated/modified)
    chacha_state[4] = seed;
    chacha_state[5] = seed ^ 0xCAFEBABE;
    chacha_state[6] = seed ^ 0xDEADBEEF;
    chacha_state[7] = seed ^ 0xFEEDFACE;
    chacha_state[8] = seed;
    chacha_state[9] = seed;
    chacha_state[10] = seed;
    chacha_state[11] = seed;
    // Counter
    chacha_state[12] = 0;
    // Nonce
    chacha_state[13] = 0;
    chacha_state[14] = 0;
    chacha_state[15] = 0;

    chacha_idx = 16;
    seeded = 1;
}

int rand(void) {
    if (!seeded) srand(1);

    if (chacha_idx >= 16) {
        chacha20_block(chacha_block, chacha_state);
        chacha_state[12]++; // Increment counter
        chacha_idx = 0;
    }
    return (int)(chacha_block[chacha_idx++] & 0x7FFFFFFF);
}

void arc4random_buf(void *buf, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        abort();
    }
    char *p = (char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r <= 0) {
             close(fd);
             abort();
        }
        p += r;
        left -= r;
    }
    close(fd);
}

uint32_t arc4random(void) {
    uint32_t v;
    arc4random_buf(&v, sizeof(v));
    return v;
}

uint32_t arc4random_uniform(uint32_t upper_bound) {
    uint32_t r, min;

    if (upper_bound < 2)
        return 0;

    /* 2**32 % x == (2**32 - x) % x */
    min = -upper_bound % upper_bound;

    for (;;) {
        r = arc4random();
        if (r >= min)
            break;
    }

    return r % upper_bound;
}

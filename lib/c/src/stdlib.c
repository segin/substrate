#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdatomic.h>

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

/* xoroshiro128++ implementation */
static uint64_t s[2] = { 0x1234567890ABCDEF, 0xFEDCBA0987654321 };

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

static uint64_t next_rand(void) {
	const uint64_t s0 = s[0];
	uint64_t s1 = s[1];
	const uint64_t result = rotl(s0 + s1, 17) + s0;

	s1 ^= s0;
	s[0] = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
	s[1] = rotl(s1, 28);

	return result;
}

int rand(void) {
    return (int)(next_rand() & 0x7FFFFFFF);
}

void srand(unsigned int seed) {
    // SplitMix64 based seeding
    uint64_t z = (uint64_t)seed;

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    s[0] = z ^ (z >> 31);

    // Changing seed for second part
    z = (uint64_t)seed + 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    s[1] = z ^ (z >> 31);

    // Ensure non-zero state
    if (s[0] == 0 && s[1] == 0) s[0] = 1;
}

void arc4random_buf(void *buf, size_t n) {
    static atomic_int urandom_fd = -1;
    int fd = atomic_load(&urandom_fd);

    if (fd == -1) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            abort();
        }
        int expected = -1;
        if (!atomic_compare_exchange_strong(&urandom_fd, &expected, fd)) {
            close(fd);
            fd = expected;
        }
    }

    char *p = (char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r <= 0) {
             abort();
        }
        p += r;
        left -= r;
    }
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

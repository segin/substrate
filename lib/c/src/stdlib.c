#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

// Allocator Implementation

#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define BLOCK_META_SIZE ALIGN(sizeof(struct block_meta))
#define MAGIC 0xDEADBEEF

struct block_meta {
    size_t size;
    struct block_meta *next;
    struct block_meta *prev;
    int free;
    uint32_t magic;
};

static struct block_meta *global_base = NULL;

static struct block_meta *request_space(struct block_meta *last, size_t size) {
    struct block_meta *block;
    size_t total_size = size + BLOCK_META_SIZE;

    // Request memory in multiples of page size (4096)
    size_t page_size = 4096;
    size_t alloc_size = (total_size + page_size - 1) & ~(page_size - 1);

    // If request is small, allocate at least a few pages to reduce syscalls
    if (alloc_size < 64 * 1024) alloc_size = 64 * 1024;

    void *ptr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    block = (struct block_meta *)ptr;
    block->size = alloc_size - BLOCK_META_SIZE;
    block->next = NULL;
    block->prev = last;
    block->free = 1; // Initially free, will be split/used by caller
    block->magic = MAGIC;

    if (last) {
        last->next = block;
    }

    // If this is the first block, update global_base
    if (!global_base) {
        global_base = block;
    }

    return block;
}

static struct block_meta *find_free_block(struct block_meta **last, size_t size) {
    struct block_meta *current = global_base;
    while (current && !(current->free && current->size >= size)) {
        *last = current;
        current = current->next;
    }
    return current;
}

static void split_block(struct block_meta *block, size_t size) {
    if (block->size >= size + BLOCK_META_SIZE + ALIGNMENT) {
        struct block_meta *new_block = (struct block_meta *)((char*)block + BLOCK_META_SIZE + size);
        new_block->size = block->size - size - BLOCK_META_SIZE;
        new_block->next = block->next;
        new_block->prev = block;
        new_block->free = 1;
        new_block->magic = MAGIC;

        if (new_block->next) {
            new_block->next->prev = new_block;
        }

        block->size = size;
        block->next = new_block;
    }
}

static void coalesce_block(struct block_meta *block) {
    // Coalesce with next
    if (block->next && block->next->free) {
        // Check adjacency
        if ((char*)block + BLOCK_META_SIZE + block->size == (char*)block->next) {
            block->size += BLOCK_META_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                block->next->prev = block;
            }
        }
    }
    // Coalesce with prev
    if (block->prev && block->prev->free) {
        // Check adjacency
        if ((char*)block->prev + BLOCK_META_SIZE + block->prev->size == (char*)block) {
            block->prev->size += BLOCK_META_SIZE + block->size;
            block->prev->next = block->next;
            if (block->next) {
                block->next->prev = block->prev;
            }
        }
    }
}

void *malloc(size_t size) {
    if (size <= 0) return NULL;

    struct block_meta *block;
    struct block_meta *last = global_base;
    size_t aligned_size = ALIGN(size);

    if (!global_base) {
        block = request_space(NULL, aligned_size);
        if (!block) return NULL;
        last = block;
    } else {
        block = find_free_block(&last, aligned_size);
        if (!block) {
            block = request_space(last, aligned_size);
            if (!block) return NULL;
        }
    }

    // If we found a free block (or created one), try to split it
    if (block->size > aligned_size) {
        split_block(block, aligned_size);
    }

    block->free = 0;
    block->magic = MAGIC;
    return (block + 1);
}

void free(void *ptr) {
    if (!ptr) return;

    struct block_meta *block = (struct block_meta*)ptr - 1;
    if (block->magic != MAGIC) {
        return;
    }

    block->free = 1;
    coalesce_block(block);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    // Check for overflow
    if (nmemb != 0 && total / nmemb != size) return NULL;

    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    struct block_meta *block = (struct block_meta*)ptr - 1;
    if (block->magic != MAGIC) return NULL;

    if (block->size >= size) {
        if (block->size >= ALIGN(size) + BLOCK_META_SIZE + ALIGNMENT) {
             split_block(block, ALIGN(size));
        }
        return ptr;
    }

    // Need to grow.
    // Check if next block is free and contiguous
    if (block->next && block->next->free &&
        ((char*)block + BLOCK_META_SIZE + block->size == (char*)block->next) &&
        (block->size + BLOCK_META_SIZE + block->next->size >= ALIGN(size))) {

        // Merge next block
        block->size += BLOCK_META_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;

        // Now split if too big
        if (block->size >= ALIGN(size) + BLOCK_META_SIZE + ALIGNMENT) {
            split_block(block, ALIGN(size));
        }
        return ptr;
    }

    // Fallback: allocate new, copy, free old
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, block->size); 
    free(ptr);
    return new_ptr;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);

    if (alignment <= ALIGNMENT) {
        if (size % alignment != 0) size = (size + alignment - 1) & ~(alignment - 1);
        return malloc(size);
    }

    return NULL;
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

/* getopt implementation */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char * const argv[], const char *optstring) {
    static int sp = 1;
    int c;
    char *cp;

    if (sp == 1) {
        if (optind >= argc ||
            argv[optind][0] != '-' || argv[optind][1] == '\0')
            return -1;
        else if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }
    }
    optopt = c = argv[optind][sp];
    if (c == ':' || (cp = strchr(optstring, c)) == NULL) {
        if (opterr)
            fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
        if (argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return '?';
    }
    if (*++cp == ':') {
        if (argv[optind][sp+1] != '\0')
            optarg = &argv[optind++][sp+1];
        else if (++optind >= argc) {
            if (opterr)
                fprintf(stderr, "%s: option requires an argument -- %c\n",
                    argv[0], c);
            sp = 1;
            return '?';
        } else
            optarg = argv[optind++];
        sp = 1;
    } else {
        if (argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }
    return c;
}

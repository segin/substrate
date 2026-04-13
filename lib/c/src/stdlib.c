#include <stdlib.h>
#ifdef HOST_TEST
#undef bsearch
#undef qsort
#endif
#include <unistd.h>
#include <ctype.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h> // For memset, memcpy
#include <stdio.h>
#include <stdatomic.h>
#include <limits.h>
#include <errno.h>
#include <sys/wait.h>

#define ATEXIT_MAX 32
static void (*__atexit_funcs[ATEXIT_MAX])(void);
static int __atexit_count = 0;

void exit(int status) {
    /* Call atexit handlers in reverse order */
    while (__atexit_count > 0) {
        __atexit_funcs[--__atexit_count]();
    }
    /* Flush all open stdio streams (POSIX requirement) */
    fflush(NULL);
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
    /* Check overflow before multiplication to prevent allocating too-small buffer. */
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return NULL;
    }

    void *ptr = malloc(nmemb * size);
    if (ptr) {
        memset(ptr, 0, nmemb * size);
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

    // If requested alignment is supported by our default allocator (<= 16 bytes),
    // we can use malloc directly after ensuring size is a multiple of alignment.
    if (alignment <= ALIGNMENT) {
        if (size % alignment != 0) size = (size + alignment - 1) & ~(alignment - 1);
        return malloc(size);
    }

    // Supporting larger alignments (>16 bytes) requires a more complex allocator
    // that can store the original pointer offset (e.g., in a preamble) so free()
    // can find the block metadata. For now, we return NULL for unsupported alignments
    // to avoid unsafe behavior or memory leaks.
    //
    // Note: The previous bump allocator supported arbitrary alignment but leaked memory.
    // This implementation prioritizes correctness and memory reclamation over rare
    // high-alignment requirements.
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
    for (acc = 0, any = 0, c = *s++;; c = *s++) {
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

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long long acc = 0;
    int any = 0;
    int neg = 0;
    int overflow = 0;
    unsigned long long cutoff;
    unsigned int cutlim;

    if (base < 0 || base == 1 || base > 36) {
        if (endptr) *endptr = (char *)nptr;
        errno = EINVAL;
        return 0;
    }

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if ((base == 0 || base == 16) &&
        s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    }
    if (base == 0) {
        base = (*s == '0') ? 8 : 10;
    }

    cutoff = ULLONG_MAX / (unsigned int)base;
    cutlim = (unsigned int)(ULLONG_MAX % (unsigned int)base);

    while (*s) {
        unsigned int digit;
        unsigned char ch = (unsigned char)*s;

        if (ch >= '0' && ch <= '9') digit = (unsigned int)(ch - '0');
        else if (ch >= 'a' && ch <= 'z') digit = (unsigned int)(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'Z') digit = (unsigned int)(ch - 'A' + 10);
        else break;

        if ((int)digit >= base) break;

        if (acc > cutoff || (acc == cutoff && digit > cutlim)) {
            overflow = 1;
            any = 1;
        } else {
            acc = (acc * (unsigned int)base) + digit;
            any = 1;
        }
        s++;
    }

    if (!any) {
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }

    if (overflow) {
        errno = ERANGE;
        acc = ULLONG_MAX;
    } else if (neg) {
        acc = 0ULL - acc;
    }

    if (endptr) *endptr = (char *)s;
    return acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    unsigned long long v = strtoull(nptr, endptr, base);
    if (v > ULONG_MAX) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return (unsigned long)v;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    int neg = 0;
    char *local_end = NULL;
    unsigned long long limit;
    unsigned long long mag;
    int saved_errno = errno;
    int over = 0;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    errno = 0;
    mag = strtoull(s, &local_end, base);
    over = (errno == ERANGE);
    if (!over) errno = saved_errno;
    if (local_end == s) {
        if (endptr) *endptr = (char *)nptr;
        return 0;
    }

    if (endptr) *endptr = local_end;

    if (neg) {
        limit = (unsigned long long)LLONG_MAX + 1ULL;
        if (mag > limit || (over && mag == ULLONG_MAX)) {
            errno = ERANGE;
            return LLONG_MIN;
        }
        if (mag == limit) return LLONG_MIN;
        return -(long long)mag;
    }

    if (mag > (unsigned long long)LLONG_MAX || (over && mag == ULLONG_MAX)) {
        errno = ERANGE;
        return LLONG_MAX;
    }
    return (long long)mag;
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
    return strtoll(nptr, NULL, 10);
}

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    double val = 0.0;
    int sign = 1;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    while (isdigit((unsigned char)*s)) {
        val = val * 10.0 + (*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double fraction = 0.1;
        while (isdigit((unsigned char)*s)) {
            val += (*s - '0') * fraction;
            fraction /= 10.0;
            s++;
        }
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') s++;
        
        int exp_val = 0;
        while (isdigit((unsigned char)*s)) {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        
        double factor = 1.0;
        double base = 10.0;
        int e = exp_val;
        while (e > 0) {
            if (e % 2 == 1) factor *= base;
            base *= base;
            e /= 2;
        }
        
        if (exp_sign == -1) val /= factor;
        else val *= factor;
    }

    if (endptr) *endptr = (char *)s;
    return val * sign;
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

extern char **environ;

char *getenv(const char *name) {
    if (!name || !environ) return NULL;
    size_t len = strlen(name);
    for (char **env = environ; *env; ++env) {
        if (strncmp(*env, name, len) == 0 && (*env)[len] == '=') {
            return *env + len + 1;
        }
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!value) value = "";

    size_t name_len = strlen(name);
    size_t value_len = strlen(value);

    size_t count = 0;
    if (environ) {
        while (environ[count]) count++;
    }

    for (size_t i = 0; i < count; i++) {
        if (strncmp(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
            if (!overwrite) return 0;

            char *entry = malloc(name_len + 1 + value_len + 1);
            if (!entry) {
                errno = ENOMEM;
                return -1;
            }
            memcpy(entry, name, name_len);
            entry[name_len] = '=';
            memcpy(entry + name_len + 1, value, value_len + 1);
            environ[i] = entry;
            return 0;
        }
    }

    char **new_env = realloc(environ, (count + 2) * sizeof(char *));
    if (!new_env) {
        errno = ENOMEM;
        return -1;
    }
    environ = new_env;

    char *entry = malloc(name_len + 1 + value_len + 1);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    memcpy(entry + name_len + 1, value, value_len + 1);

    environ[count] = entry;
    environ[count + 1] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!environ) return 0;

    size_t name_len = strlen(name);
    size_t dst = 0;
    for (size_t src = 0; environ[src]; src++) {
        if (strncmp(environ[src], name, name_len) == 0 && environ[src][name_len] == '=') {
            continue;
        }
        environ[dst++] = environ[src];
    }
    environ[dst] = NULL;
    return 0;
}

int system(const char *command) {
    if (!command) return 1;

    pid_t pid = fork();
    if (pid == -1) return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) return -1;
    return status;
}

int abs(int j) { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }
long long llabs(long long j) { return j < 0 ? -j : j; }

div_t div(int numer, int denom) {
    div_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

lldiv_t lldiv(long long numer, long long denom) {
    lldiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

int atexit(void (*func)(void)) {
    if (__atexit_count >= ATEXIT_MAX) return -1;
    __atexit_funcs[__atexit_count++] = func;
    return 0;
}

void _Exit(int status) {
    _exit(status);
}

static void swap_bytes(char *a, char *b, size_t size) {
    while (size--) {
        char tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

static void insertion_sort(char *base, size_t nmemb, size_t size,
                           int (*compar)(const void *, const void *)) {
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(base + j * size, base + (j - 1) * size) < 0) {
            swap_bytes(base + j * size, base + (j - 1) * size, size);
            j--;
        }
    }
}

static size_t med3(char *base, size_t a, size_t b, size_t c, size_t size,
                   int (*compar)(const void *, const void *)) {
    int ab = compar(base + a * size, base + b * size);
    int bc = compar(base + b * size, base + c * size);
    int ac = compar(base + a * size, base + c * size);
    if (ab <= 0) {
        if (bc <= 0) return b;
        return ac <= 0 ? c : a;
    }
    if (bc >= 0) return b;
    return ac >= 0 ? c : a;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb < 2 || size == 0) return;

    char *arr = (char *)base;

    /* Use iterative quicksort with explicit stack to avoid recursion depth issues */
    struct { size_t lo; size_t hi; } stack[64];
    int top = 0;
    stack[top].lo = 0;
    stack[top].hi = nmemb - 1;

    while (top >= 0) {
        size_t lo = stack[top].lo;
        size_t hi = stack[top].hi;
        top--;

        if (hi <= lo) continue;

        /* Use insertion sort for small partitions */
        if (hi - lo + 1 <= 16) {
            insertion_sort(arr + lo * size, hi - lo + 1, size, compar);
            continue;
        }

        /* Median-of-three pivot selection */
        size_t mid = lo + (hi - lo) / 2;
        size_t pivot_idx = med3(arr, lo, mid, hi, size, compar);
        swap_bytes(arr + pivot_idx * size, arr + lo * size, size);

        /* Partition */
        size_t i = lo + 1;
        size_t j = hi;
        while (1) {
            while (i <= hi && compar(arr + i * size, arr + lo * size) < 0) i++;
            while (j > lo && compar(arr + j * size, arr + lo * size) > 0) j--;
            if (i >= j) break;
            swap_bytes(arr + i * size, arr + j * size, size);
            i++;
            j--;
        }
        swap_bytes(arr + lo * size, arr + j * size, size);

        /* Push larger partition first so smaller is processed first (limits stack depth) */
        if (j > lo + 1 && hi > j + 1) {
            if (j - lo > hi - j) {
                top++;
                stack[top].lo = lo;
                stack[top].hi = j > 0 ? j - 1 : 0;
                top++;
                stack[top].lo = j + 1;
                stack[top].hi = hi;
            } else {
                top++;
                stack[top].lo = j + 1;
                stack[top].hi = hi;
                top++;
                stack[top].lo = lo;
                stack[top].hi = j > 0 ? j - 1 : 0;
            }
        } else if (j > lo + 1) {
            top++;
            stack[top].lo = lo;
            stack[top].hi = j > 0 ? j - 1 : 0;
        } else if (hi > j + 1) {
            top++;
            stack[top].lo = j + 1;
            stack[top].hi = hi;
        }
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    const char *p = (const char *)base;
    while (nmemb > 0) {
        size_t mid = nmemb / 2;
        const void *midp = p + mid * size;
        int cmp = compar(key, midp);
        if (cmp == 0) return (void *)midp;
        if (cmp > 0) {
            p = (const char *)midp + size;
            nmemb -= mid + 1;
        } else {
            nmemb = mid;
        }
    }
    return NULL;
}

/* ChaCha20 implementation */
static uint32_t chacha_state[16];
static uint32_t chacha_block[16];
static int chacha_idx = 16;
static int seeded = 0;

/* Process-wide entropy for srand/rand to prevent sole dependency on 32-bit seed */
static uint32_t process_secret[11];
static atomic_int secret_initialized = 0;

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
    if (!atomic_load(&secret_initialized)) {
        arc4random_buf(process_secret, sizeof(process_secret));
        atomic_store(&secret_initialized, 1);
    }

    // "expand 32-byte k"
    chacha_state[0] = 0x61707865;
    chacha_state[1] = 0x3320646e;
    chacha_state[2] = 0x79622d32;
    chacha_state[3] = 0x6b206574;
    // Key (use seed mixed with secret)
    uint32_t s = seed;
    for (int i = 0; i < 8; i++) {
        chacha_state[4 + i] = process_secret[i] ^ s;
        s = (s << 7) | (s >> 25);
    }
    // Counter
    chacha_state[12] = 0;
    // Nonce (use seed mixed with secret)
    s = seed;
    for (int i = 0; i < 3; i++) {
        chacha_state[13 + i] = process_secret[8 + i] ^ s;
        s = (s >> 7) | (s << 25);
    }

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

#define MKSTEMP_SUFFIX "XXXXXX"
#define MKSTEMP_SUFFIX_LEN (sizeof(MKSTEMP_SUFFIX) - 1)

static void fill_temp_suffix(char *suffix) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < MKSTEMP_SUFFIX_LEN; i++) {
        suffix[i] = alphabet[arc4random_uniform((uint32_t)(sizeof(alphabet) - 1))];
    }
}

int mkstemp(char *tmpl) {
    if (!tmpl) {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(tmpl);
    if (len < MKSTEMP_SUFFIX_LEN || strcmp(tmpl + len - MKSTEMP_SUFFIX_LEN, MKSTEMP_SUFFIX) != 0) {
        errno = EINVAL;
        return -1;
    }

    char *suffix = tmpl + len - MKSTEMP_SUFFIX_LEN;
    for (int attempt = 0; attempt < 256; attempt++) {
        fill_temp_suffix(suffix);
        int fd = open(tmpl, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }

    errno = EEXIST;
    return -1;
}

char *mkdtemp(char *tmpl) {
    if (!tmpl) {
        errno = EINVAL;
        return NULL;
    }

    size_t len = strlen(tmpl);
    if (len < MKSTEMP_SUFFIX_LEN || strcmp(tmpl + len - MKSTEMP_SUFFIX_LEN, MKSTEMP_SUFFIX) != 0) {
        errno = EINVAL;
        return NULL;
    }

    char *suffix = tmpl + len - MKSTEMP_SUFFIX_LEN;
    for (int attempt = 0; attempt < 256; attempt++) {
        fill_temp_suffix(suffix);
        if (mkdir(tmpl, 0700) == 0) return tmpl;
        if (errno != EEXIST) return NULL;
    }

    errno = EEXIST;
    return NULL;
}

char *realpath(const char *restrict path, char *restrict resolved_path) {
    if (!path || *path == '\0') {
        errno = EINVAL;
        return NULL;
    }

    char tmp[PATH_MAX];
    if (path[0] == '/') {
        if (strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) return NULL;
        size_t cwd_len = strlen(cwd);
        size_t path_len = strlen(path);
        if (cwd_len + 1 + path_len + 1 > sizeof(tmp)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        memcpy(tmp, cwd, cwd_len);
        tmp[cwd_len] = '/';
        memcpy(tmp + cwd_len + 1, path, path_len + 1);
    }

    char *out = resolved_path;
    if (!out) {
        out = malloc(PATH_MAX);
        if (!out) {
            errno = ENOMEM;
            return NULL;
        }
    }

    if (strlcpy(out, tmp, PATH_MAX) >= PATH_MAX) {
        if (!resolved_path) free(out);
        errno = ENAMETOOLONG;
        return NULL;
    }
    return out;
}

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
#include <sys/random.h>
#include <sys/syscall.h>
#include <termios.h>

extern int64_t _syscall3(int, uintptr_t, uintptr_t, uintptr_t);

#define ATEXIT_MAX 32
static void (*__atexit_funcs[ATEXIT_MAX])(void);
static int __atexit_count = 0;
static atomic_int __atexit_lock = 0;

static void
__atexit_lock_acquire(void)
{
    int expected;

    for (;;) {
        expected = 0;
        if (atomic_compare_exchange_weak(&__atexit_lock, &expected, 1)) {
            return;
        }
    }
}

static void
__atexit_lock_release(void)
{
    atomic_store(&__atexit_lock, 0);
}

/*
 * at_quick_exit / quick_exit (C11 §7.22.4) registration list.  This
 * is SEPARATE from atexit's list: quick_exit runs only the handlers
 * registered here.  C11 guarantees at least 32 registrations.
 */
#define ATQUICK_MAX 32
static void (*__atquick_funcs[ATQUICK_MAX])(void);
static int __atquick_count = 0;
static atomic_int __atquick_lock = 0;

static void
__atquick_lock_acquire(void)
{
    int expected;

    for (;;) {
        expected = 0;
        if (atomic_compare_exchange_weak(&__atquick_lock, &expected, 1)) {
            return;
        }
    }
}

static void
__atquick_lock_release(void)
{
    atomic_store(&__atquick_lock, 0);
}

/* Weak reference to /sbin/ld.so's destructor entry point.  Only
 * dynamically-linked binaries have it resolved; static binaries
 * see a NULL function pointer and skip the call. */
extern void __ldso_run_fini(void) __attribute__((weak));

void exit(int status) {
    int count;

    __atexit_lock_acquire();
    count = __atexit_count;
    __atexit_count = 0;
    __atexit_lock_release();

    /* Call atexit handlers in reverse order */
    while (count > 0) {
        __atexit_funcs[--count]();
    }
    /* Run DT_FINI_ARRAY / DT_FINI for every loaded .so via ld.so. */
    if (__ldso_run_fini) __ldso_run_fini();
    /* Flush all open stdio streams (POSIX requirement) */
    fflush(NULL);
    _exit(status);
}

void abort(void) {
    _exit(134);
}

/*
 * Stack-protector runtime.  GCC's -fstack-protector prologue copies
 * __stack_chk_guard into each protected frame and the epilogue verifies it,
 * branching to __stack_chk_fail on mismatch.  libc shipped __stack_chk_fail
 * but not the guard symbol, so any object compiled with -fstack-protector
 * failed to link (undefined reference to __stack_chk_guard) — which is why
 * substrate code has had to build with -fno-stack-protector.  Providing the
 * guard lets stack protection link and work.
 *
 * The initial value is a fixed "terminator canary" (low byte NUL so a naive
 * string overflow can't cleanly overwrite it, plus CR/LF/0xFF).  It is the
 * fallback; __init_stack_guard() replaces it at process startup with real
 * kernel entropy from the AT_RANDOM auxv entry.
 */
uintptr_t __stack_chk_guard = (uintptr_t)0xff0a0d00;

void __stack_chk_fail(void) {
    _exit(127);
}

/*
 * __init_stack_guard — seed __stack_chk_guard from kernel entropy.  Called
 * once from crt0 before any stack-protected function runs.  The kernel
 * supplies 16 random bytes through the AT_RANDOM auxv entry (see
 * sys/exec/formats/elf.c); we take the first pointer-sized chunk.  If
 * AT_RANDOM is absent the fixed terminator canary above stays in effect.
 *
 * Marked no_stack_protector because it MUTATES the guard: a protected
 * prologue here would save the old guard and the epilogue would compare the
 * new global against it and spuriously fail.  (libc is already built
 * -fno-stack-protector, so this is belt-and-suspenders.)
 */
#define _AT_NULL    0
#define _AT_RANDOM  25
__attribute__((no_stack_protector))
void __init_stack_guard(char **envp) {
    if (!envp) return;
    char **p = envp;
    while (*p) p++;            /* walk past the environment strings */
    p++;                      /* step over the NULL terminator -> auxv */
    uintptr_t *aux = (uintptr_t *)p;   /* { a_type, a_val } pairs */
    for (; aux[0] != _AT_NULL; aux += 2) {
        if (aux[0] == _AT_RANDOM && aux[1]) {
            uintptr_t g;
            __builtin_memcpy(&g, (const void *)aux[1], sizeof g);
            if (g) __stack_chk_guard = g;
            return;
        }
    }
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
static struct block_meta *global_tail = NULL;
static struct block_meta *search_hint = NULL;

/*
 * Heap mutex — single global spinlock around the malloc/free/realloc/
 * calloc critical sections.  The free-list (global_base, global_tail,
 * search_hint, and every block_meta's next/prev) is unsynchronized
 * shared state across threads in a multithreaded process.  Without
 * this, concurrent find_free_block + split_block / coalesce_block
 * races corrupt the list pointers, eventually producing a junk
 * `block` whose split_block dereferences a NULL/garbage pointer.
 * Same shape as the Xfbdev/links crashes (see
 * tests/lib/c/torture_malloc_threads).
 *
 * Spinlock chosen (not pthread_mutex) so the C library doesn't
 * depend on libpthread — a libc.so must be safe to use from
 * processes that don't link libpthread at all.  CAS pattern
 * matches __atexit_lock above.  On contention the spinner yields
 * via sched_yield so a preempted lock-holder can finish.
 *
 * Recursion: malloc may be reached from a signal handler.  If the
 * interrupted thread already holds the lock, the handler's
 * malloc would deadlock.  Document this as "do not call malloc
 * from an async-signal handler" (matches glibc/musl semantics) —
 * the X server's SmartScheduleTimer handler is a counter bump
 * and doesn't trigger this.
 */
extern int sched_yield(void);
static atomic_int __malloc_lock = 0;

static void __malloc_lock_acquire(void) {
    int expected;
    for (;;) {
        expected = 0;
        if (atomic_compare_exchange_weak(&__malloc_lock, &expected, 1)) return;
        sched_yield();
    }
}

static void __malloc_lock_release(void) {
    atomic_store(&__malloc_lock, 0);
}

static void *block_payload(struct block_meta *block) {
    return (void *)((char *)block + BLOCK_META_SIZE);
}

static struct block_meta *payload_block(void *ptr) {
    return (struct block_meta *)((char *)ptr - BLOCK_META_SIZE);
}

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
    global_tail = block;
    search_hint = block;

    return block;
}

static struct block_meta *find_free_block(size_t size) {
    struct block_meta *start;
    struct block_meta *current;
    int spins = 0;
    const int SPIN_CAP = 100000;

    if (global_base == NULL) {
        return NULL;
    }
    start = search_hint != NULL ? search_hint : global_base;
    if (start == NULL) return NULL;  /* defensive */
    current = start;
    do {
        /* Substrate hardening: heap corruption observed in this
         * session (search_hint or current->next set to garbage)
         * was causing this loop to deref NULL or run forever.
         * Bail safely instead. */
        if (current == NULL) {
            fprintf(stderr, "find_free_block: NULL current "
                    "(heap corrupt) — global_base=%p search_hint=%p\n",
                    (void *)global_base, (void *)search_hint);
            return NULL;
        }
        if (spins++ > SPIN_CAP) {
            fprintf(stderr, "find_free_block: spin cap exceeded "
                    "(free list cycle, heap corrupt)\n");
            return NULL;
        }

        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next != NULL ? current->next : global_base;
    } while (current != start);
    return NULL;
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
        } else {
            global_tail = new_block;
        }

        block->size = size;
        block->next = new_block;
        search_hint = new_block;
    }
}

static struct block_meta *coalesce_block(struct block_meta *block) {
    // Coalesce with next
    if (block->next && block->next->free) {
        // Check adjacency
        if ((char*)block + BLOCK_META_SIZE + block->size == (char*)block->next) {
            struct block_meta *next = block->next;
            block->size += BLOCK_META_SIZE + block->next->size;
            block->next = block->next->next;
            if (block->next) {
                block->next->prev = block;
            } else if (global_tail == next) {
                global_tail = block;
            }
        }
    }
    // Coalesce with prev
    if (block->prev && block->prev->free) {
        // Check adjacency
        if ((char*)block->prev + BLOCK_META_SIZE + block->prev->size == (char*)block) {
            struct block_meta *prev = block->prev;
            prev->size += BLOCK_META_SIZE + block->size;
            prev->next = block->next;
            if (block->next) {
                block->next->prev = prev;
            } else if (global_tail == block) {
                global_tail = prev;
            }
            block = prev;
        }
    }
    return block;
}

void *malloc(size_t size) {
    /* POSIX: malloc(0) may return either NULL or a unique pointer
     * that can be passed to free.  Most modern libcs (glibc/musl)
     * return a non-NULL pointer.  Callers like mandoc_malloc treat
     * NULL as a fatal allocation failure, so we follow the
     * non-NULL convention to avoid false-positive OOM exits. */
    if (size == 0) size = 1;

    struct block_meta *block;
    size_t aligned_size = ALIGN(size);

    __malloc_lock_acquire();

    if (!global_base) {
        block = request_space(NULL, aligned_size);
        if (!block) { __malloc_lock_release(); errno = ENOMEM; return NULL; }
    } else {
        block = find_free_block(aligned_size);
        if (!block) {
            block = request_space(global_tail, aligned_size);
            if (!block) { __malloc_lock_release(); errno = ENOMEM; return NULL; }
        }
    }

    // If we found a free block (or created one), try to split it
    if (block->size > aligned_size) {
        split_block(block, aligned_size);
    }

    block->free = 0;
    block->magic = MAGIC;
    if (search_hint == block) {
        search_hint = block->next != NULL ? block->next : global_base;
    }
    void *payload = block_payload(block);
    __malloc_lock_release();
    return payload;
}

void free(void *ptr) {
    if (!ptr) return;

    struct block_meta *block = payload_block(ptr);

    __malloc_lock_acquire();
    /* Reject bad/already-freed pointers under the lock.  The magic
     * check rejects pointers that never came from malloc; the
     * `block->free` check catches a double free — without it, freeing
     * the same block twice runs coalesce_block() again on a block that
     * may already have been merged into a neighbour, corrupting the
     * free list.  A freed block keeps its MAGIC (so realloc/free can
     * still recognise it), so the free flag is the double-free guard. */
    if (block->magic != MAGIC || block->free) {
        __malloc_lock_release();
        return;
    }
    block->free = 1;
    search_hint = coalesce_block(block);
    __malloc_lock_release();
}

void *calloc(size_t nmemb, size_t size) {
    /* Check overflow before multiplication to prevent allocating too-small buffer. */
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
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

    struct block_meta *block = payload_block(ptr);
    if (block->magic != MAGIC) { errno = EINVAL; return NULL; }

    /* In-place / grow-into-next paths happen under the heap lock.
     * The fallback (malloc + memcpy + free) must NOT hold the lock
     * across the malloc/free calls — those acquire it themselves. */
    __malloc_lock_acquire();

    if (block->size >= size) {
        if (block->size >= ALIGN(size) + BLOCK_META_SIZE + ALIGNMENT) {
             split_block(block, ALIGN(size));
        }
        __malloc_lock_release();
        return ptr;
    }

    // Need to grow.
    // Check if next block is free and contiguous
    if (block->next && block->next->free &&
        ((char*)block + BLOCK_META_SIZE + block->size == (char*)block->next) &&
        (block->size + BLOCK_META_SIZE + block->next->size >= ALIGN(size))) {
        struct block_meta *next = block->next;
        int hint_was_next = (search_hint == next);

        // Merge next block
        block->size += BLOCK_META_SIZE + next->size;
        block->next = next->next;
        if (block->next) {
            block->next->prev = block;
        } else if (global_tail == next) {
            global_tail = block;
        }
        if (hint_was_next) {
            search_hint = block->next != NULL ? block->next : global_base;
        }

        // Now split if too big
        if (block->size >= ALIGN(size) + BLOCK_META_SIZE + ALIGNMENT) {
            split_block(block, ALIGN(size));
        }
        __malloc_lock_release();
        return ptr;
    }

    /* Fallback below calls malloc + free, which take the lock
     * themselves.  Release first to avoid recursive acquire. */
    size_t old_size = block->size;
    __malloc_lock_release();

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_size);
    free(ptr);
    return new_ptr;
}

/*
 * posix_memalign — POSIX.1-2001.  Returns (via *memptr) `size` bytes
 * aligned to `alignment`, which must be a power of two and a multiple of
 * sizeof(void*).  Returns 0 on success or EINVAL/ENOMEM (it does NOT set
 * errno).  The result is released with plain free().
 *
 * Arbitrary alignment is supported, including page size — which the old
 * aligned_alloc could not do (it returned NULL above 16 bytes).  The trick
 * is to over-allocate and then carve a genuinely-aligned sub-block out of
 * the oversized allocation: a real block_meta header is planted just before
 * the aligned payload and the leading slack becomes a free block.  Because
 * the carved block is a true allocator block — correct MAGIC, size, and
 * next/prev links that honour physical order — free(), realloc(), and the
 * coalescer treat it like any block returned by malloc().
 */
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (memptr == NULL)
        return EINVAL;
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return EINVAL;

    /* malloc payloads are already ALIGNMENT(16)-aligned. */
    if (alignment <= ALIGNMENT) {
        void *p = malloc(size);
        if (!p) return ENOMEM;
        *memptr = p;
        return 0;
    }

    /* Over-allocate: worst-case alignment shift plus an inserted header. */
    void *raw = malloc(ALIGN(size ? size : 1) + alignment + BLOCK_META_SIZE);
    if (!raw) return ENOMEM;

    if (((uintptr_t)raw & (alignment - 1)) == 0) {
        /* Already aligned; the extra room is just unused tail payload. */
        *memptr = raw;
        return 0;
    }

    __malloc_lock_acquire();
    struct block_meta *orig = payload_block(raw);
    char *orig_end = (char *)raw + orig->size;            /* payload end */
    uintptr_t ap = ((uintptr_t)raw + BLOCK_META_SIZE + alignment - 1)
                   & ~((uintptr_t)alignment - 1);
    struct block_meta *nb = (struct block_meta *)(ap - BLOCK_META_SIZE);

    nb->magic = MAGIC;
    nb->free  = 0;
    nb->size  = (size_t)(orig_end - (char *)ap);
    nb->prev  = orig;
    nb->next  = orig->next;
    if (nb->next)
        nb->next->prev = nb;
    else
        global_tail = nb;

    orig->next = nb;
    orig->size = (size_t)((char *)nb - (char *)raw);      /* leading slack */
    orig->free = 1;                                       /* freed back */
    __malloc_lock_release();

    *memptr = (void *)ap;
    return 0;
}

void *aligned_alloc(size_t alignment, size_t size) {
    void *p = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    int rc = posix_memalign(&p, alignment, size);
    if (rc) { errno = rc; return NULL; }
    return p;
}

/*
 * quick_exit — C11 §7.22.4.7.  Runs every function registered with
 * at_quick_exit, in reverse order of registration, then calls
 * _Exit().  Unlike exit() it does NOT run atexit handlers, does NOT
 * run ld.so / static destructors, and does NOT flush or close stdio
 * streams — it is the minimal, "fast" termination path.
 */
void quick_exit(int status) {
    /*
     * Pop each handler under the lock and call it with the lock
     * released.  Popping (rather than snapshotting a count) means a
     * handler that itself calls at_quick_exit has the freshly
     * registered entry picked up on the next iteration and run
     * before the older remaining handlers — which is exactly the
     * ordering C11 mandates for a function registered during
     * quick_exit processing.
     */
    for (;;) {
        void (*fn)(void) = NULL;

        __atquick_lock_acquire();
        if (__atquick_count > 0) {
            fn = __atquick_funcs[--__atquick_count];
        }
        __atquick_lock_release();

        if (fn == NULL) {
            break;
        }
        fn();
    }

    _Exit(status);
}

/*
 * at_quick_exit — C11 §7.22.4.3.  Registers `func` to be called by
 * quick_exit().  Returns 0 on success, nonzero if the registration
 * could not be made (NULL function, or the list is full).
 */
int at_quick_exit(void (*func)(void)) {
    if (func == NULL) {
        return -1;
    }

    __atquick_lock_acquire();
    if (__atquick_count >= ATQUICK_MAX) {
        __atquick_lock_release();
        return -1;
    }
    __atquick_funcs[__atquick_count++] = func;
    __atquick_lock_release();
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

    cutoff = neg ? (unsigned long)(-(LONG_MIN + 1L)) + 1u : (unsigned long)LONG_MAX;
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
        /* C99 §7.22.1.4 / POSIX: on overflow, clamp to LONG_MIN/MAX
         * AND set errno = ERANGE.  strtoull/strtoll already do this;
         * strtol was silently dropping the errno report. */
        acc = neg ? (unsigned long)LONG_MIN : (unsigned long)LONG_MAX;
        errno = ERANGE;
    } else if (neg) {
        acc = -acc;
    }
    if (endptr != 0) *endptr = (char *)(any ? s - 1 : nptr);
    return (long)acc;
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
static int environ_owned = 0;

/*
 * Linked list of "NAME=VALUE" strings libc malloc'd via setenv.  The
 * crt0-supplied entries (and those installed by execve) live on the
 * stack/argument area and must NOT be freed; only entries on this
 * list are ours to free when overwritten or unset.
 */
struct env_owned_entry {
    char *str;
    struct env_owned_entry *next;
};
static struct env_owned_entry *env_owned_list = NULL;

static void env_track_owned(char *str) {
    struct env_owned_entry *e = malloc(sizeof(*e));
    if (!e) return; /* leak rather than abort */
    e->str = str;
    e->next = env_owned_list;
    env_owned_list = e;
}

/* If `str` is on the owned list, free it and remove the tracker. */
static void env_free_if_owned(char *str) {
    struct env_owned_entry **pp = &env_owned_list;
    while (*pp) {
        if ((*pp)->str == str) {
            struct env_owned_entry *gone = *pp;
            *pp = gone->next;
            free(gone->str);
            free(gone);
            return;
        }
        pp = &(*pp)->next;
    }
}

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
            char *old = environ[i];
            environ[i] = entry;
            env_track_owned(entry);
            env_free_if_owned(old);
            return 0;
        }
    }

    char **new_env;
    if (environ_owned) {
        new_env = realloc(environ, (count + 2) * sizeof(char *));
        if (!new_env) {
            errno = ENOMEM;
            return -1;
        }
    } else {
        new_env = malloc((count + 2) * sizeof(char *));
        if (!new_env) {
            errno = ENOMEM;
            return -1;
        }
        for (size_t i = 0; i < count; ++i) {
            new_env[i] = environ[i];
        }
        environ_owned = 1;
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
    env_track_owned(entry);
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
            env_free_if_owned(environ[src]);
            continue;
        }
        environ[dst++] = environ[src];
    }
    environ[dst] = NULL;
    return 0;
}

int putenv(char *string) {
    if (!string) {
        errno = EINVAL;
        return -1;
    }
    char *eq = strchr(string, '=');
    if (!eq || eq == string) {
        errno = EINVAL;
        return -1;
    }
    /* POSIX says putenv() should make `string' part of environ directly
     * (mutating it later mutates environ).  We trade strict conformance
     * for a clean lifetime model: copy into a name/value pair and route
     * through setenv().  Matches what glibc/musl effectively give callers
     * who don't poke at the string after the call. */
    size_t name_len = (size_t)(eq - string);
    char buf[256];
    if (name_len >= sizeof(buf)) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(buf, string, name_len);
    buf[name_len] = '\0';
    return setenv(buf, eq + 1, 1);
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
    if (func == NULL) return -1;

    __atexit_lock_acquire();
    if (__atexit_count >= ATEXIT_MAX) {
        __atexit_lock_release();
        return -1;
    }
    __atexit_funcs[__atexit_count++] = func;
    __atexit_lock_release();
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

/*
 * Internal: qsort core takes a context-aware comparator
 * (a, b, arg).  The plain qsort() at the bottom is a thin wrapper
 * that ignores arg and calls a 2-arg comparator via a thunk.
 */
typedef int (*compar_r_fn)(const void *, const void *, void *);

static void insertion_sort_r(char *base, size_t nmemb, size_t size,
                             compar_r_fn compar, void *arg) {
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 &&
               compar(base + j * size, base + (j - 1) * size, arg) < 0) {
            swap_bytes(base + j * size, base + (j - 1) * size, size);
            j--;
        }
    }
}

static size_t med3_r(char *base, size_t a, size_t b, size_t c, size_t size,
                     compar_r_fn compar, void *arg) {
    int ab = compar(base + a * size, base + b * size, arg);
    int bc = compar(base + b * size, base + c * size, arg);
    int ac = compar(base + a * size, base + c * size, arg);
    if (ab <= 0) {
        if (bc <= 0) return b;
        return ac <= 0 ? c : a;
    }
    if (bc >= 0) return b;
    return ac >= 0 ? c : a;
}

void qsort_r(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *, void *),
             void *arg) {
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
            insertion_sort_r(arr + lo * size, hi - lo + 1, size, compar, arg);
            continue;
        }

        /* Median-of-three pivot selection */
        size_t mid = lo + (hi - lo) / 2;
        size_t pivot_idx = med3_r(arr, lo, mid, hi, size, compar, arg);
        swap_bytes(arr + pivot_idx * size, arr + lo * size, size);

        /* Partition */
        size_t i = lo + 1;
        size_t j = hi;
        while (1) {
            while (i <= hi && compar(arr + i * size, arr + lo * size, arg) < 0) i++;
            while (j > lo && compar(arr + j * size, arr + lo * size, arg) > 0) j--;
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

/*
 * qsort(): thin wrapper around qsort_r.  The trampoline reinterprets
 * `arg` as the caller's 2-arg comparator and drops the unused thunk.
 */
static int
qsort_compat_thunk(const void *a, const void *b, void *arg)
{
    int (*compar)(const void *, const void *) =
        (int (*)(const void *, const void *))arg;
    return compar(a, b);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    qsort_r(base, nmemb, size, qsort_compat_thunk, (void *)compar);
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

/* POSIX rand_r(3): thread-safe variant.  Uses a simple LCG seeded
 * from the user-provided state — caller manages reentrancy by
 * passing a per-thread *seed in. */
int rand_r(unsigned int *seedp) {
    if (!seedp) return 0;
    *seedp = *seedp * 1103515245u + 12345u;
    return (int)((*seedp / 65536u) & 0x7FFFu);
}

/*
 * BSD random()/srandom() compatibility.  Substrate's rand() already
 * returns the full [0, 2^31-1] range that BSD random() guarantees, so
 * we route through it rather than maintain a second PRNG.  Callers
 * that need an independent stream (POSIX permits separate state) can
 * be revisited if the need arises.
 */
long random(void) {
    return (long)rand();
}

void srandom(unsigned seed) {
    srand(seed);
}

void arc4random_buf(void *buf, size_t n) {
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;

    /*
     * Primary source: the getrandom(2) syscall.  It needs no file descriptor
     * and is NOT subject to /dev/urandom's file permissions, so it works for
     * every process regardless of uid.  (Calling it directly rather than via
     * libsys — libc cannot depend on libsys.)
     */
    while (got < n) {
        int64_t r = _syscall3(SYS_GETRANDOM, (uintptr_t)(p + got),
                              (uintptr_t)(n - got), 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (got == n) return;

    /*
     * Fallback: /dev/urandom, for kernels without the syscall.  Cache the fd.
     */
    static atomic_int urandom_fd = -1;
    int fd = atomic_load(&urandom_fd);
    if (fd == -1) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            int expected = -1;
            if (!atomic_compare_exchange_strong(&urandom_fd, &expected, fd)) {
                close(fd);
                fd = expected;
            }
        }
    }
    if (fd >= 0) {
        while (got < n) {
            ssize_t r = read(fd, p + got, n - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got == n) return;
    }

    /*
     * Last resort: a software xorshift mixed from cheap, always-available
     * entropy.  This is NOT cryptographically strong, but arc4random_buf must
     * never abort or hang — bailing here (the previous behavior) killed every
     * program that could not reach /dev/urandom, e.g. a non-root login shell.
     */
    uint64_t s = (uint64_t)(uintptr_t)&s * 0x9E3779B97F4A7C15ULL;
    s ^= (uint64_t)(unsigned)getpid();
    static atomic_int ctr;
    s ^= (uint64_t)(unsigned)atomic_fetch_add(&ctr, 1) * 0x100000001B3ULL;
    if (s == 0) s = 0xDEADBEEFCAFEBABEULL;
    while (got < n) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;   /* xorshift64 */
        size_t chunk = (n - got) < sizeof(s) ? (n - got) : sizeof(s);
        memcpy(p + got, &s, chunk);
        got += chunk;
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
    /* Pull entropy from the kernel directly so the suffix is
     * unguessable even before libc has reseeded its PRNG (e.g. in a
     * just-forked child).  We call the syscall directly here rather
     * than via libsys's getrandom() — libc cannot depend on libsys.
     * Falls back to arc4random_uniform if the syscall is unavailable. */
    unsigned char rnd[MKSTEMP_SUFFIX_LEN];
    int64_t got = _syscall3(SYS_GETRANDOM,
                            (uintptr_t)rnd, (uintptr_t)sizeof(rnd), 0);
    if (got == (int64_t)sizeof(rnd)) {
        for (size_t i = 0; i < MKSTEMP_SUFFIX_LEN; i++) {
            suffix[i] = alphabet[rnd[i] % (sizeof(alphabet) - 1)];
        }
        return;
    }
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

/*
 * realpath — canonicalize an absolute path.
 *
 * Walks `path` one component at a time, expanding symlinks on every
 * step.  Maintains an "out" buffer that always holds the
 * canonicalized prefix (no symlinks, no "." or ".." remaining).
 *
 * Loop guard: at most SYMLOOP_MAX (40, matching Linux) symlink
 * dereferences over the whole resolution; anything beyond that
 * yields ELOOP.
 *
 * Errors propagated:
 *   EINVAL       — path is NULL or empty
 *   ENOMEM       — resolved_path == NULL and malloc failed
 *   ENAMETOOLONG — accumulated result exceeds PATH_MAX
 *   ELOOP        — symlink chain longer than SYMLOOP_MAX
 *   ENOENT       — a component doesn't exist
 *   ENOTDIR      — non-final component isn't a directory
 *   EACCES       — search permission denied on a directory
 */
#define REALPATH_SYMLOOP_MAX 40

char *realpath(const char *restrict path, char *restrict resolved_path) {
    if (!path || *path == '\0') {
        errno = (path == NULL) ? EINVAL : ENOENT;
        return NULL;
    }

    char *out = resolved_path;
    int owned = 0;
    if (!out) {
        out = malloc(PATH_MAX);
        if (!out) { errno = ENOMEM; return NULL; }
        owned = 1;
    }

    /* Seed `out` with the appropriate root.  Relative paths get cwd. */
    size_t out_len = 0;
    if (path[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        out_len = 1;
    } else {
        if (!getcwd(out, PATH_MAX)) {
            if (owned) free(out);
            return NULL;
        }
        out_len = strlen(out);
        if (out_len == 0 || out[out_len - 1] != '/') {
            if (out_len + 1 >= PATH_MAX) {
                if (owned) free(out);
                errno = ENAMETOOLONG;
                return NULL;
            }
            out[out_len++] = '/';
            out[out_len] = '\0';
        }
    }

    /* `remaining` holds the unresolved tail that still needs walking;
     * symlink expansion prepends the link target onto it. */
    char remaining[PATH_MAX];
    if (strlcpy(remaining, path, sizeof(remaining)) >= sizeof(remaining)) {
        if (owned) free(out);
        errno = ENAMETOOLONG;
        return NULL;
    }

    int symlinks = 0;
    char *p = remaining;
    /* Skip leading slashes — already accounted for by the seed. */
    while (*p == '/') p++;

    while (*p) {
        /* Extract next component into `comp`. */
        char *slash = p;
        while (*slash && *slash != '/') slash++;
        size_t comp_len = (size_t)(slash - p);
        char comp[PATH_MAX];
        if (comp_len >= sizeof(comp)) {
            if (owned) free(out);
            errno = ENAMETOOLONG;
            return NULL;
        }
        memcpy(comp, p, comp_len);
        comp[comp_len] = '\0';
        p = slash;
        while (*p == '/') p++;

        if (comp_len == 0) continue;
        if (comp_len == 1 && comp[0] == '.') continue;
        if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
            /* Pop the trailing '/' then the component before it. */
            if (out_len > 1) {
                out_len--;                             /* drop trailing '/' */
                while (out_len > 0 && out[out_len - 1] != '/') out_len--;
                out[out_len] = '\0';
                if (out_len == 0) {                    /* never go above '/' */
                    out[0] = '/';
                    out[1] = '\0';
                    out_len = 1;
                }
            }
            continue;
        }

        /* Append component to `out` (with separator). */
        if (out[out_len - 1] != '/') {
            if (out_len + 1 >= PATH_MAX) {
                if (owned) free(out);
                errno = ENAMETOOLONG;
                return NULL;
            }
            out[out_len++] = '/';
        }
        if (out_len + comp_len >= PATH_MAX) {
            if (owned) free(out);
            errno = ENAMETOOLONG;
            return NULL;
        }
        memcpy(out + out_len, comp, comp_len);
        out_len += comp_len;
        out[out_len] = '\0';

        /* lstat to test for a symlink at this level. */
        struct stat st;
        if (lstat(out, &st) < 0) {
            if (owned) free(out);
            return NULL;
        }
        if (!S_ISLNK(st.st_mode)) {
            /* Not a symlink — final or directory; nothing more to do
             * unless there are more components to walk.  If this is a
             * non-final component and not a directory, that's ENOTDIR. */
            if (*p != '\0' && !S_ISDIR(st.st_mode)) {
                if (owned) free(out);
                errno = ENOTDIR;
                return NULL;
            }
            continue;
        }

        /* Symlink: read the target and splice it into `remaining`. */
        if (++symlinks > REALPATH_SYMLOOP_MAX) {
            if (owned) free(out);
            errno = ELOOP;
            return NULL;
        }
        char target[PATH_MAX];
        ssize_t tlen = readlink(out, target, sizeof(target) - 1);
        if (tlen < 0) {
            if (owned) free(out);
            return NULL;
        }
        target[tlen] = '\0';

        /* Pop the symlink itself off `out`. */
        out_len -= comp_len;
        if (out_len > 0 && out[out_len - 1] == '/') out_len--;
        out[out_len] = '\0';
        /* Absolute target: start over from the root. */
        if (target[0] == '/') {
            out[0] = '/';
            out[1] = '\0';
            out_len = 1;
        } else if (out_len == 0) {
            out[0] = '/';
            out[1] = '\0';
            out_len = 1;
        }

        /* Splice: new remaining = target + '/' + p */
        char merged[PATH_MAX];
        size_t plen = strlen(p);
        size_t total_len = (size_t)tlen + (plen > 0 ? 1 + plen : 0);
        if (total_len >= sizeof(merged)) {
            if (owned) free(out);
            errno = ENAMETOOLONG;
            return NULL;
        }
        memcpy(merged, target, (size_t)tlen);
        if (plen > 0) {
            merged[tlen] = '/';
            memcpy(merged + tlen + 1, p, plen);
        }
        merged[total_len] = '\0';
        memcpy(remaining, merged, total_len + 1);
        p = remaining;
        while (*p == '/') {
            /* Absolute target consumed leading slash already. */
            if (target[0] == '/') break;
            p++;
        }
    }

    /* Strip any trailing slash unless we are exactly at "/". */
    if (out_len > 1 && out[out_len - 1] == '/') {
        out[out_len - 1] = '\0';
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Unix98 pseudo-terminal API                                         */
/* ------------------------------------------------------------------ */

#include <sys/ioctl.h>

int posix_openpt(int flags) {
    return open("/dev/ptmx", flags);
}

int grantpt(int fd) {
    /* Substrate has no traditional pty group / setuid model — every
     * /dev/pts/N is opened with the caller's credentials and chowned
     * by the kernel at allocation time.  The libc stub exists for
     * portability; nothing to do. */
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int unlockpt(int fd) {
    int unlock = 0;
    return ioctl(fd, TIOCSPTLCK, &unlock);
}

int ptsname_r(int fd, char *buf, size_t buflen) {
    int ptn;
    if (!buf || buflen == 0) {
        errno = EINVAL;
        return EINVAL;
    }
    if (ioctl(fd, TIOCGPTN, &ptn) < 0) {
        return errno;
    }
    int n = snprintf(buf, buflen, "/dev/pts/%d", ptn);
    if (n < 0 || (size_t)n >= buflen) {
        errno = ERANGE;
        return ERANGE;
    }
    return 0;
}

char *ptsname(int fd) {
    static char ptsname_buf[32];
    int err = ptsname_r(fd, ptsname_buf, sizeof(ptsname_buf));
    if (err != 0) {
        return NULL;
    }
    return ptsname_buf;
}

/*
 * openpty / login_tty — BSD-style PTY helpers.  No <pty.h> header
 * yet, telnetd and friends declare these locally with `extern int
 * openpty(int *, int *, char *, void *, void *)` until we sit down to
 * write <pty.h>.
 */
int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp,
            const struct winsize *winp) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -1;
    if (grantpt(m) < 0 || unlockpt(m) < 0) {
        close(m);
        return -1;
    }
    char path[64];
    if (ptsname_r(m, path, sizeof(path)) != 0) {
        close(m);
        return -1;
    }
    int s = open(path, O_RDWR | O_NOCTTY);
    if (s < 0) {
        close(m);
        return -1;
    }
    if (termp) {
        (void)tcsetattr(s, TCSANOW, termp);
    }
    if (winp) {
        (void)ioctl(s, TIOCSWINSZ, winp);
    }
    if (name) {
        /* Caller is responsible for ensuring `name` is large enough;
         * the BSD interface gives no length, so this is a foot-gun
         * we inherit from history.  pts paths are short. */
        strcpy(name, path);
    }
    *amaster = m;
    *aslave  = s;
    return 0;
}

/* daemon(3) — BSD/glibc helper that detaches the caller from the
 * controlling terminal so it can run in the background.  Identical
 * semantics to BSD's daemon(): fork once so the parent can exit and
 * the child is orphaned, become a new session leader so we lose the
 * tty, optionally chdir("/") and optionally redirect stdio at
 * /dev/null.  Returns 0 to the (now detached) child, -1 on failure
 * (errno set).  The parent never returns — _exit(0) after fork.
 *
 * Uses raw syscall(SYS_SETSID) for the same reason login_tty does,
 * so static linkees aren't forced to drag in libsys.  */
int daemon(int nochdir, int noclose) {
    extern long syscall(long number, ...);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) _exit(0);
    (void)syscall(SYS_SETSID);
    if (!nochdir) chdir("/");
    if (!noclose) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2) close(fd);
        }
    }
    return 0;
}

/* login_tty — make `fd` the controlling tty for this process and
 * point stdio at it.  Standard recipe: new session, TIOCSCTTY,
 * dup2 to 0/1/2, close the original if it wasn't one of those.
 *
 * We call SYS_SETSID via raw syscall() instead of setsid(3) so
 * statically-linked binaries that pull this file in via libc.a
 * don't acquire a hard libsys.a dependency.  The wrapper for
 * setsid lives in lib/sys/pgrp.c and dynamic linkees see it via
 * DT_NEEDED libsys.so.0; static linkees that don't need login_tty
 * shouldn't have to drag setsid in. */
int login_tty(int fd) {
    extern long syscall(long number, ...);
    /* Become session leader first.  Ignoring failure: if we're
     * already a session leader (unusual here, but possible) the
     * TIOCSCTTY below still does the right thing. */
    (void)syscall(SYS_SETSID);
    /*
     * Pass arg=1 to TIOCSCTTY to "steal" the tty.  Substrate's
     * tty_open() runs tty_attach_first_opener which auto-claims an
     * unowned tty for the OPENING process's session — even with
     * O_NOCTTY.  For PTYs that's painful: the parent (telnetd /
     * sshd-style daemons) opens both master and slave inside its
     * own session before forking, so by the time the child fork
     * gets here the slave already lists the PARENT's session in
     * tty->session and a non-force TIOCSCTTY would fail EPERM.
     * arg=1 tells the kernel we're aware and want it anyway —
     * matches Linux semantics for `int arg = 1`.
     */
    if (ioctl(fd, TIOCSCTTY, 1) < 0) {
        return -1;
    }
    if (dup2(fd, 0) < 0) return -1;
    if (dup2(fd, 1) < 0) return -1;
    if (dup2(fd, 2) < 0) return -1;
    if (fd > 2) close(fd);
    return 0;
}

/*
 * reallocarray(ptr, nmemb, size) — realloc with overflow-safe
 * nmemb * size multiplication.  OpenBSD extension.  Returns NULL
 * (and leaves ptr untouched) if the multiplication would overflow.
 */
void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (size != 0 && nmemb > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(ptr, nmemb * size);
}

/*
 * mkstemps(template, suffixlen) — like mkstemp but allows a
 * fixed-length suffix after the XXXXXX block.  e.g. "foo.XXXXXX.tmp"
 * with suffixlen=4 randomises the XXXXXX while leaving ".tmp" intact.
 */
int mkstemps(char *tmpl, int suffixlen) {
    if (tmpl == NULL || suffixlen < 0) {
        errno = EINVAL;
        return -1;
    }
    size_t tlen = strlen(tmpl);
    if ((size_t)suffixlen + MKSTEMP_SUFFIX_LEN > tlen) {
        errno = EINVAL;
        return -1;
    }
    char *suffix = tmpl + tlen - suffixlen - MKSTEMP_SUFFIX_LEN;
    if (strncmp(suffix, MKSTEMP_SUFFIX, MKSTEMP_SUFFIX_LEN) != 0) {
        errno = EINVAL;
        return -1;
    }
    for (int attempt = 0; attempt < 256; attempt++) {
        fill_temp_suffix(suffix);
        int fd = open(tmpl, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

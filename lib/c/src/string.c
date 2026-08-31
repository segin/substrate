#include <errno.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HOST_TEST
#undef memchr
#undef strchr
#undef strrchr
#undef strstr
#undef strpbrk
#endif

static int
string_ptr_is_null(const void *ptr)
{
    return ptr == NULL;
}

/*
 * Substrate-libc shared frame-pointer-chain backtrace.  Walks the
 * saved-ebp chain on i386 starting from the passed-in fp and prints
 * up to 16 frames to stderr.  Bounded for safety: stops on NULL ebp,
 * unaligned ebp, or a non-monotonic chain (saved_ebp <= current ebp
 * means we've fallen off the real frames into garbage).
 *
 * Each frame is printed BEFORE dereferencing the *next* one, so even
 * if the chain ends in junk we still surface what we got before a
 * recursive fault wipes us out.
 *
 * The guard caller passes __builtin_frame_address(0), so frame #0 in
 * the output is the libc function itself (memcpy / memset / strdup),
 * frame #1 is its direct caller, and so on.  Output format mirrors
 * the kernel TRAP backtrace so scripts/resolve-trap.sh resolves it
 * with no special-casing.
 */
__attribute__((noinline, used))
static void
__substrate_libc_backtrace(uintptr_t fp)
{


    fprintf(stderr, "  user backtrace:\n");
    for (int depth = 0; depth < 16; depth++) {
        if (fp == 0) {
            fprintf(stderr, "    (end of chain)\n");
            return;
        }
        if (fp & 3) {
            fprintf(stderr, "    <unaligned ebp=0x%08lx>\n",
                    (unsigned long)fp);
            return;
        }
        const uintptr_t *frame = (const uintptr_t *)fp;
        fprintf(stderr, "    #%d ebp=0x%08lx ret=0x%08lx\n",
                depth, (unsigned long)fp, (unsigned long)frame[1]);
        uintptr_t next = frame[0];
        if (next <= fp) {
            fprintf(stderr, "    (chain ends: saved_ebp=0x%08lx)\n",
                    (unsigned long)next);
            return;
        }
        fp = next;
    }
}

char *strfry(char *string) {
    if (!string) return NULL;
    size_t len = strlen(string);
    if (len == 0) return string;

    /* Fisher-Yates with arc4random_uniform — avoids modulo bias
     * (rand() % len skews when len is not a power of two and gives
     * weak randomness from the LCG underlying rand). */
    for (size_t i = len - 1; i > 0; i--) {
        size_t r = (size_t)arc4random_uniform((uint32_t)(i + 1));
        char tmp = string[i];
        string[i] = string[r];
        string[r] = tmp;
    }
    return string;
}

/*
 * Optimized memcpy - uses word-aligned transfers for speed.
 * Handles unaligned head/tail bytes separately.
 */
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;

    /* Substrate debug guard: a NULL source or destination with n>0 is
     * undefined behaviour in standard C but in practice memcpy()'s
     * word-copy loop just SIGSEGVs immediately, with the user-side
     * backtrace lost.  Instead, log the caller's return address
     * (which the kernel's TRAP log can resolve via addr2line) and
     * bail.  Caller continues with the destination unmodified — a
     * visible corruption beats a process kill. */
    if (__builtin_expect((!src || !dest) && n > 0, 0)) {

        fprintf(stderr,
                "memcpy(NULL): dest=%p src=%p n=%u\n",
                dest, src, (unsigned)n);
        __substrate_libc_backtrace((uintptr_t)__builtin_frame_address(0));
        return dest;
    }

    /* Copy byte-by-byte until destination is word-aligned */
    while (n && ((uintptr_t)d & 3)) {
        *d++ = *s++;
        n--;
    }

    /* Copy words (4 bytes at a time) */
    uint32_t *dw = (uint32_t *)d;
    const uint32_t *sw = (const uint32_t *)s;
    while (n >= 4) {
        *dw++ = *sw++;
        n -= 4;
    }

    /* Copy remaining bytes */
    d = (unsigned char *)dw;
    s = (const unsigned char *)sw;
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

/*
 * Optimized memmove - handles overlapping regions safely.
 * Uses word-aligned transfers when possible.
 */
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    
    if (d < s) {
        /* Forward copy - same as memcpy optimization */
        while (n && ((uintptr_t)d & 3)) {
            *d++ = *s++;
            n--;
        }
        uint32_t *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;
        while (n >= 4) {
            *dw++ = *sw++;
            n -= 4;
        }
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Backward copy to handle overlap */
        d += n;
        s += n;
        
        /* Copy tail bytes until destination is word-aligned */
        while (n && ((uintptr_t)d & 3)) {
            *--d = *--s;
            n--;
        }
        
        /* Copy words backward */
        uint32_t *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;
        while (n >= 4) {
            *--dw = *--sw;
            n -= 4;
        }
        
        /* Copy remaining bytes */
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

/*
 * Optimized memset - uses word-aligned fills for speed.
 */
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    unsigned char byte = (unsigned char)c;

    /* Substrate debug guard — see matching memcpy guard.  A NULL
     * destination with n > 0 is undefined behaviour; in practice
     * memset's word-fill loop SIGSEGVs immediately, hiding the
     * caller.  Log the caller's return address and bail. */
    if (__builtin_expect(!s && n > 0, 0)) {

        fprintf(stderr,
                "memset(NULL): s=%p c=%d n=%u\n",
                s, c, (unsigned)n);
        __substrate_libc_backtrace((uintptr_t)__builtin_frame_address(0));
        return s;
    }

    /* Fill byte-by-byte until word-aligned */
    while (n && ((uintptr_t)p & 3)) {
        *p++ = byte;
        n--;
    }
    
    /* Create a word with the byte replicated 4 times */
    uint32_t word = byte | (byte << 8) | (byte << 16) | (byte << 24);
    
    /* Fill words */
    uint32_t *pw = (uint32_t *)p;
    while (n >= 4) {
        *pw++ = word;
        n -= 4;
    }
    
    /* Fill remaining bytes */
    p = (unsigned char *)pw;
    while (n--) {
        *p++ = byte;
    }
    
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c) return (void*)p;
        p++;
    }
    return NULL;
}

void *memccpy(void *dest, const void *src, int c, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) {
        if ((*d++ = *s++) == (unsigned char)c)
            return d;
    }
    return NULL;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

/*
 * POSIX.1-2008.  Copies src (including the NUL) to dest and returns a pointer
 * to the terminating NUL in dest, not to its start -- which lets a caller
 * chain appends without rescanning what it just wrote.  Unbounded, exactly as
 * strcpy() is.
 */
char *stpcpy(char *dest, const char *src) {
    while ((*dest = *src++))
        dest++;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n) {
        n--;
        if ((*dest++ = *src++) == 0) break;
    }
    while (n--) *dest++ = 0;
    return ret;
}

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t src_len = strlen(src);
    if (size > 0) {
        size_t copy_len = (src_len >= size) ? size - 1 : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t strlcat(char *dst, const char *src, size_t size) {
    size_t dst_len = strnlen(dst, size);
    if (dst_len == size)
        return size + strlen(src);
    return dst_len + strlcpy(dst + dst_len, src, size - dst_len);
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (tolower(*(const unsigned char*)s1) == tolower(*(const unsigned char*)s2))) {
        s1++;
        s2++;
    }
    return tolower(*(const unsigned char*)s1) - tolower(*(const unsigned char*)s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (tolower(*(const unsigned char*)s1) == tolower(*(const unsigned char*)s2))) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return tolower(*(const unsigned char*)s1) - tolower(*(const unsigned char*)s2);
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char*)s;
}

char *strrchr(const char *s, int c) {
    const char *ret = NULL;
    do {
        if (*s == (char)c) ret = s;
    } while (*s++);
    return (char*)ret;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                h++;
                n++;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    return len;
}

char *strdup(const char *s) {
    if (string_ptr_is_null(s)) {
        errno = EINVAL;
        return NULL;
    }

    /* Substrate debug guard: reject pointers in the low-memory junk
     * range (< 0x10000).  Real strings always come from malloc / .data
     * / .rodata / stack, all well above this.  Log the actual pointer
     * value and the caller's return address so we can resolve which
     * caller is passing junk; return NULL with errno=EFAULT instead
     * of SIGSEGV'ing in strlen. */
    if ((uintptr_t)s < 0x10000) {

        fprintf(stderr, "strdup: junk arg s=%p\n", (void *)s);
        __substrate_libc_backtrace((uintptr_t)__builtin_frame_address(0));
        errno = EFAULT;
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char *new_s = malloc(len);
    if (new_s) {
        memcpy(new_s, s, len);
    }
    return new_s;
}

char *strndup(const char *s, size_t n) {
    if (string_ptr_is_null(s)) {
        errno = EINVAL;
        return NULL;
    }

    size_t len = strnlen(s, n);
    char *new_s = malloc(len + 1);
    if (!new_s) return NULL;
    memcpy(new_s, s, len);
    new_s[len] = '\0';
    return new_s;
}

size_t strspn(const char *s, const char *accept) {
    const char *p = s;
    const char *a;
    while (*p) {
        for (a = accept; *a; a++) {
            if (*p == *a) break;
        }
        if (*a == 0) return p - s;
        p++;
    }
    return p - s;
}

size_t strcspn(const char *s, const char *reject) {
    const char *p = s;
    const char *r;
    while (*p) {
        for (r = reject; *r; r++) {
            if (*p == *r) return p - s;
        }
        p++;
    }
    return p - s;
}

char *strtok(char *str, const char *delim) {
    /*
     * Match the current native libc reality: there is no target TLS bootstrap
     * yet, so keep strtok's legacy internal state process-global for now.
     */
    static char *saveptr;
    if (str) saveptr = str;
    else if (!saveptr) return NULL;
    
    str = saveptr + strspn(saveptr, delim);
    if (*str == 0) {
        saveptr = NULL;
        return NULL;
    }
    
    char *end = str + strcspn(str, delim);
    if (*end) {
        *end = 0;
        saveptr = end + 1;
    } else {
        saveptr = NULL;
    }
    return str;
}

char *strpbrk(const char *s1, const char *s2) {
    while (*s1) {
        if (strchr(s2, *s1++)) return (char *)s1 - 1;
    }
    return NULL;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s = str;
    if (!s) s = *saveptr;
    if (!s) return NULL;

    s += strspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *token = s;
    s += strcspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
    } else {
        *s = '\0';
        *saveptr = s + 1;
    }
    return token;
}

static char *lookup_error_string(int errnum) {
    switch (errnum) {
    case 0: return "Success";
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case ESRCH: return "No such process";
    case EINTR: return "Interrupted system call";
    case EIO: return "I/O error";
    case ENXIO: return "No such device or address";
    case E2BIG: return "Argument list too long";
    case ENOEXEC: return "Exec format error";
    case EBADF: return "Bad file descriptor";
    case ECHILD: return "No child processes";
    case EAGAIN: return "Resource temporarily unavailable";
    case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied";
    case EFAULT: return "Bad address";
    case ENOTBLK: return "Block device required";
    case EBUSY: return "Device or resource busy";
    case EEXIST: return "File exists";
    case EXDEV: return "Cross-device link";
    case ENODEV: return "No such device";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case ENFILE: return "File table overflow";
    case EMFILE: return "Too many open files";
    case ENOTTY: return "Inappropriate ioctl for device";
    case ETXTBSY: return "Text file busy";
    case EFBIG: return "File too large";
    case ENOSPC: return "No space left on device";
    case ESPIPE: return "Illegal seek";
    case EROFS: return "Read-only file system";
    case EMLINK: return "Too many links";
    case EPIPE: return "Broken pipe";
    case EDOM: return "Math argument out of domain";
    case ERANGE: return "Result too large";
    case EDEADLK: return "Resource deadlock would occur";
    case ENOSYS: return "Function not implemented";
    case ENOTEMPTY: return "Directory not empty";
    case ENAMETOOLONG: return "File name too long";
    case EOVERFLOW: return "Value too large for defined data type";
    case ETIMEDOUT: return "Connection timed out";
    case EOWNERDEAD: return "Owner died";
    case ENOTRECOVERABLE: return "State not recoverable";
    /* Socket / network errnos — added so daemon failures stop
     * showing as the unhelpful "Unknown error" when their bind /
     * connect / accept paths fail.  */
    case ENOTSOCK: return "Socket operation on non-socket";
    case EDESTADDRREQ: return "Destination address required";
    case EMSGSIZE: return "Message too long";
    case EPROTOTYPE: return "Protocol wrong type for socket";
    case ENOPROTOOPT: return "Protocol not available";
    case EPROTONOSUPPORT: return "Protocol not supported";
    case ESOCKTNOSUPPORT: return "Socket type not supported";
    case EOPNOTSUPP: return "Operation not supported";
    case EPFNOSUPPORT: return "Protocol family not supported";
    case EAFNOSUPPORT: return "Address family not supported by protocol";
    case EADDRINUSE: return "Address already in use";
    case EADDRNOTAVAIL: return "Cannot assign requested address";
    case ENETDOWN: return "Network is down";
    case ENETUNREACH: return "Network is unreachable";
    case ENETRESET: return "Network dropped connection on reset";
    case ECONNABORTED: return "Software caused connection abort";
    case ECONNRESET: return "Connection reset by peer";
    case ENOBUFS: return "No buffer space available";
    case EISCONN: return "Transport endpoint is already connected";
    case ENOTCONN: return "Transport endpoint is not connected";
    case ESHUTDOWN: return "Cannot send after transport endpoint shutdown";
    case ETOOMANYREFS: return "Too many references: cannot splice";
    case ECONNREFUSED: return "Connection refused";
    case EHOSTDOWN: return "Host is down";
    case EHOSTUNREACH: return "No route to host";
    case EALREADY: return "Operation already in progress";
    case EINPROGRESS: return "Operation now in progress";
    default: return "Unknown error";
    }
}

char *strerror(int errnum) {
    return lookup_error_string(errnum);
}

char *geterror(int errnum) {
    return lookup_error_string(errnum);
}

/*
 * POSIX strerror_r: copy the canonical message for errnum into the
 * caller's buffer.  Returns 0 on success or ERANGE if the buffer is
 * too small to hold the full string (in that case `buf` still gets
 * the truncated message, NUL-terminated).
 */
int strerror_r(int errnum, char *buf, size_t buflen) {
    const char *src;
    size_t      n;
    if (buf == NULL || buflen == 0) {
        return ERANGE;
    }
    src = lookup_error_string(errnum);
    n = 0;
    while (n + 1 < buflen && src[n] != '\0') {
        buf[n] = src[n];
        n++;
    }
    buf[n] = '\0';
    return (src[n] == '\0') ? 0 : ERANGE;
}

/* POSIX strerror_l(3) — locale-aware variant.  Substrate is C-locale
 * only today, so this returns the same string as strerror(). */
/* locale_t comes from <locale.h> -- substrate's declares it, and so does
 * glibc's when this file is compiled natively for the host tests.  It used
 * to be typedef'd right here, which conflicted with glibc's declaration of
 * strerror_l() in terms of its own locale_t. */
char *strerror_l(int errnum, locale_t locale) {
    (void)locale;
    return (char *)lookup_error_string(errnum);
}
/* Shift the UNSIGNED value: a signed >>= 1 on a value with the sign
 * bit set (e.g. INT_MIN / LONG_MIN) sign-extends and the loop spins
 * forever.  ffs(INT_MIN) must return 32. */
int ffs(int i) {
    unsigned u = (unsigned)i;
    if (u == 0) return 0;
    int bit = 1;
    while (!(u & 1)) { u >>= 1; bit++; }
    return bit;
}

int ffsl(long i) {
    unsigned long u = (unsigned long)i;
    if (u == 0) return 0;
    int bit = 1;
    while (!(u & 1)) { u >>= 1; bit++; }
    return bit;
}

int ffsll(long long i) {
    unsigned long long u = (unsigned long long)i;
    if (u == 0) return 0;
    int bit = 1;
    while (!(u & 1)) { u >>= 1; bit++; }
    return bit;
}

void bzero(void *s, size_t n) { memset(s, 0, n); }
void bcopy(const void *src, void *dst, size_t n) { memmove(dst, src, n); }
int  bcmp(const void *s1, const void *s2, size_t n) { return memcmp(s1, s2, n); }
char *index(const char *s, int c)  { return strchr(s, c); }
char *rindex(const char *s, int c) { return strrchr(s, c); }

/* In the C/POSIX locale, strcoll is identical to strcmp */
int strcoll(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

/* In the C/POSIX locale, strxfrm copies the string unchanged */
size_t strxfrm(char *dest, const char *src, size_t n) {
    size_t len = strlen(src);
    if (n > 0) {
        size_t copy = len < n ? len : n - 1;
        memcpy(dest, src, copy);
        dest[copy] = '\0';
    }
    return len;
}

/*
 * strsep — 4.3BSD-style tokeniser.  Like strtok but doesn't
 * conflate adjacent delimiters and uses a caller-provided
 * pointer-to-pointer for state instead of a static.  Returns
 * the next token or NULL when *stringp is NULL.
 */
char *strsep(char **stringp, const char *delim) {
    char *s = *stringp;
    if (s == NULL) return NULL;
    for (char *p = s; *p != '\0'; p++) {
        for (const char *d = delim; *d != '\0'; d++) {
            if (*p == *d) {
                *p = '\0';
                *stringp = p + 1;
                return s;
            }
        }
    }
    *stringp = NULL;
    return s;
}

/*
 * strcasestr — case-insensitive strstr.  GNU extension.
 */
char *strcasestr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p != '\0'; p++) {
        if (strncasecmp(p, needle, nlen) == 0) {
            return (char *)p;
        }
    }
    return NULL;
}

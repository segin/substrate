/*
 * memcpy and memset for the regparm ABI test, built with -mregparm=3 by
 * gcc.  Under -mregparm a compiler calls even its own library helpers
 * with register arguments -- gcc's structure copies call memcpy with the
 * destination in %eax -- so the host's C library, built without, cannot
 * be the one these programs reach.
 */
typedef unsigned int rp_size_t;

void *memcpy(void *d, const void *s, rp_size_t n) {
    unsigned char *dp = d;
    const unsigned char *sp = s;

    while (n-- > 0) {
        *dp++ = *sp++;
    }
    return d;
}

void *memset(void *d, int c, rp_size_t n) {
    unsigned char *dp = d;

    while (n-- > 0) {
        *dp++ = (unsigned char)c;
    }
    return d;
}

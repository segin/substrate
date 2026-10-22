/*
 * A designated initializer may reach a union through a member that is
 * anonymous and not the union's first: `.ubuf` below lives in an
 * anonymous struct, itself the second member of an anonymous union, as
 * in Linux's struct iov_iter (<linux/uio.h>).  cc stopped with
 * "unsupported initializer: an anonymous union member other than the
 * first", for locals, compound literals and static objects alike.
 */
#include <stdio.h>

struct iovec { void *base; unsigned long len; };

struct it {
    unsigned char type;
    union {
        struct iovec ubuf_iovec;
        struct {
            union { const struct iovec *iov; void *ubuf; };
            unsigned long count;
        };
    };
    union { unsigned long nr_segs; long x; };
};

/* a union whose chosen member is an anonymous union, two deep */
union deep {
    int a;
    union {
        short b;
        struct { char c; long d; };
    };
};

static char buf[4];
static struct it file_scope = { .type = 4, .ubuf = buf + 3, .count = 11, .x = -8 };
static union deep file_deep = { .d = 77 };

int main(void) {
    struct it a = { .type = 2, .ubuf = buf, .count = 7, .nr_segs = 1 };
    struct it b;
    static struct it c = { .ubuf = buf + 2, .count = 5 };
    union deep d = { .c = 'q', .d = -5 };

    b = (struct it){ .ubuf = buf + 1, .count = 9, .x = -3 };
    printf("%d %d %lu %lu\n", a.type, (int)((char *)a.ubuf - buf), a.count, a.nr_segs);
    printf("%d %lu %ld\n", (int)((char *)b.ubuf - buf), b.count, b.x);
    printf("%d %lu %d\n", (int)((char *)c.ubuf - buf), c.count, c.type);
    printf("%d %d %lu %ld\n", file_scope.type, (int)((char *)file_scope.ubuf - buf), file_scope.count,
           file_scope.x);
    printf("%c %ld %ld\n", d.c, d.d, file_deep.d);
    return 0;
}

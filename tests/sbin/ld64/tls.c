/*
 * tls.c - a __thread variable in a dynamically linked 64-bit executable
 * (local-exec, %fs-relative), plus errno set by the shared libc.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static __thread int tls_init = 0x12345678;      /* .tdata */
static __thread long tls_zero;                  /* .tbss */
__thread char tls_buf[24] = "thread-local";

int main(void) {
    int bad = 0;

    printf("tls_init=%#x tls_zero=%ld tls_buf=%s\n",
           tls_init, tls_zero, tls_buf);
    if (tls_init != 0x12345678 || tls_zero != 0) bad = 1;

    tls_init += 1;
    tls_zero = -5;
    if (tls_init != 0x12345679 || tls_zero != -5) bad = 1;

    /* The thread pointer points at itself (variant II). */
    uintptr_t tp, self;
    __asm__("movq %%fs:0, %0" : "=r"(tp));
    self = *(uintptr_t *)tp;
    printf("tp=%#lx self=%#lx &tls_init=%p\n",
           (unsigned long)tp, (unsigned long)self, (void *)&tls_init);
    if (tp == 0 || tp != self || (uintptr_t)&tls_init >= tp) bad = 1;

    errno = 0;
    int fd = open("/nonexistent/ld64-test", O_RDONLY);
    printf("open -> %d errno=%d (ENOENT=%d)\n", fd, errno, ENOENT);
    if (fd != -1 || errno != ENOENT) bad = 1;

    errno = 0;
    if (close(-1) != -1 || errno != EBADF) bad = 1;
    printf("close(-1) errno=%d (EBADF=%d)\n", errno, EBADF);

    printf("tls: %s\n", bad ? "FAIL" : "OK");
    return bad;
}

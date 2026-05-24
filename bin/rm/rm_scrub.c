#include "rm_scrub.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int pass(int fd, off_t len, uint8_t byte)
{
    static const size_t BUF = 64 * 1024;
    uint8_t *buf;
    off_t remaining;

    if (len <= 0) {
        return 0;
    }
    buf = (uint8_t *)malloc(BUF);
    if (buf == NULL) {
        return -1;
    }
    memset(buf, byte, BUF);

    if (lseek(fd, 0, SEEK_SET) < 0) {
        free(buf);
        return -1;
    }
    remaining = len;
    while (remaining > 0) {
        size_t chunk = (remaining > (off_t)BUF) ? BUF : (size_t)remaining;
        ssize_t w = write(fd, buf, chunk);
        if (w < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return -1;
        }
        remaining -= w;
    }
    free(buf);
    if (fsync(fd) != 0 && errno != EINVAL) {
        /* EINVAL on certain pseudo-fs is non-fatal */
        return -1;
    }
    return 0;
}

int rm_scrub_file(int fd, off_t len)
{
    if (pass(fd, len, 0xFFu) != 0) return -1;
    if (pass(fd, len, 0x00u) != 0) return -1;
    if (pass(fd, len, 0xFFu) != 0) return -1;
    return 0;
}

#include <errno.h>
#include <unistd.h>

#include "echo_write.h"

int
echo_write_all(int fd, const unsigned char *data, size_t len)
{
    size_t written;

    written = 0;
    while (written < len) {
        ssize_t rv;

        rv = write(fd, data + written, len - written);
        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (rv == 0) {
            errno = EIO;
            return -1;
        }
        written += (size_t)rv;
    }

    return 0;
}
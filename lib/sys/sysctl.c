#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>


int sysctl(const int *name, unsigned int namelen, void *oldp, size_t *oldlenp, const void *newp, size_t newlen) {
    int ret = syscall(SYS_SYSCTL, name, namelen, oldp, oldlenp, newp, newlen);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

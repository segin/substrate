#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern int64_t _syscall6(int, int, int, int, int, int, int);
#define SYS_SYSCTL 243 // Matches sys/syscall.h syscall table

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    if (!name || namelen < 1 || namelen > CTL_MAXNAME) {
        errno = EINVAL;
        return -1;
    }
    if (newp && newlen == 0) {
        errno = EINVAL;
        return -1;
    }
    int ret = (int)_syscall6(SYS_SYSCTL, (int)name, (int)namelen, (int)oldp, (int)oldlenp, (int)newp, (int)newlen);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return 0;
}

int sysctlnametomib(const char *name, int *mibp, size_t *sizep) {
    int mib[] = { CTL_SYSCTL, CTL_SYSCTL_NAME2OID };
    size_t namesz = name ? (strlen(name) + 1) : 0;
    
    if (!name || !sizep) {
        errno = EINVAL;
        return -1;
    }

    if (sysctl(mib, 2, mibp, sizep, (void *)name, namesz) == -1) {
        return -1;
    }
    return 0;
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    if (!name) {
        errno = EINVAL;
        return -1;
    }
    size_t mibsize = 0;
    if (sysctlnametomib(name, NULL, &mibsize) == -1) {
        return -1;
    }
    int *mib = malloc(mibsize);
    if (!mib) {
        errno = ENOMEM;
        return -1;
    }
    if (sysctlnametomib(name, mib, &mibsize) == -1) {
        int saved_errno = errno;
        free(mib);
        errno = saved_errno;
        return -1;
    }
    int ret = sysctl(mib, mibsize / sizeof(int), oldp, oldlenp, newp, newlen);
    int saved_errno = errno;
    free(mib);
    errno = saved_errno;
    return ret;
}

void *sysctl_get_buf(const int *name, unsigned int namelen, size_t *lenp) {
    size_t len = 0;
    int retries = 5;
    void *buf = NULL;

    do {
        if (sysctl((int *)name, namelen, NULL, &len, NULL, 0) == -1) {
            free(buf);
            return NULL;
        }
        
        len += len / 10 + 1024; // buffer growth pattern
        void *newbuf = realloc(buf, len);
        if (!newbuf) {
            free(buf);
            errno = ENOMEM;
            return NULL;
        }
        buf = newbuf;

        if (sysctl((int *)name, namelen, buf, &len, NULL, 0) == 0) {
            if (lenp) *lenp = len;
            return buf;
        }
        
        if (errno != ENOMEM) {
            int saved = errno;
            free(buf);
            errno = saved;
            return NULL;
        }
    } while (--retries > 0);

    free(buf);
    errno = ENOMEM;
    return NULL;
}

void *sysctlbyname_get_buf(const char *name, size_t *lenp) {
    size_t mibsize = 0;
    if (!name || sysctlnametomib(name, NULL, &mibsize) == -1) return NULL;
    int *mib = malloc(mibsize);
    if (!mib) return NULL;
    if (sysctlnametomib(name, mib, &mibsize) == -1) {
        int saved_errno = errno;
        free(mib);
        errno = saved_errno;
        return NULL;
    }
    void *ret = sysctl_get_buf(mib, mibsize / sizeof(int), lenp);
    int saved_errno = errno;
    free(mib);
    errno = saved_errno;
    return ret;
}

int sysctl_int(const int *name, unsigned int namelen, int *oldp, int *newp) {
    size_t oldlen = oldp ? sizeof(int) : 0;
    size_t newlen = newp ? sizeof(int) : 0;
    if (sysctl((int *)name, namelen, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(int)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctlbyname_int(const char *name, int *oldp, int *newp) {
    size_t oldlen = oldp ? sizeof(int) : 0;
    size_t newlen = newp ? sizeof(int) : 0;
    if (sysctlbyname(name, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(int)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctl_uint(const int *name, unsigned int namelen, unsigned int *oldp, unsigned int *newp) {
    size_t oldlen = oldp ? sizeof(unsigned int) : 0;
    size_t newlen = newp ? sizeof(unsigned int) : 0;
    if (sysctl((int *)name, namelen, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(unsigned int)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctlbyname_uint(const char *name, unsigned int *oldp, unsigned int *newp) {
    size_t oldlen = oldp ? sizeof(unsigned int) : 0;
    size_t newlen = newp ? sizeof(unsigned int) : 0;
    if (sysctlbyname(name, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(unsigned int)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctl_quad(const int *name, unsigned int namelen, uint64_t *oldp, uint64_t *newp) {
    size_t oldlen = oldp ? sizeof(uint64_t) : 0;
    size_t newlen = newp ? sizeof(uint64_t) : 0;
    if (sysctl((int *)name, namelen, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(uint64_t)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctlbyname_quad(const char *name, uint64_t *oldp, uint64_t *newp) {
    size_t oldlen = oldp ? sizeof(uint64_t) : 0;
    size_t newlen = newp ? sizeof(uint64_t) : 0;
    if (sysctlbyname(name, oldp, oldp ? &oldlen : NULL, newp, newlen) == -1) return -1;
    if (oldp && oldlen != sizeof(uint64_t)) { errno = EINVAL; return -1; }
    return 0;
}

int sysctl_string(const int *name, unsigned int namelen, char *oldp, size_t *oldlenp, const char *newp) {
    size_t newlen = newp ? (strlen(newp) + 1) : 0;
    return sysctl((int *)name, namelen, oldp, oldlenp, (void *)newp, newlen);
}

int sysctlbyname_string(const char *name, char *oldp, size_t *oldlenp, const char *newp) {
    size_t newlen = newp ? (strlen(newp) + 1) : 0;
    return sysctlbyname(name, oldp, oldlenp, (void *)newp, newlen);
}

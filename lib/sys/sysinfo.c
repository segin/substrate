#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <unistd.h>

/*
 * sysinfo() - Returns information on overall system statistics
 */
#ifndef SYS_SYSINFO
#define SYS_SYSINFO 116
#endif


int sysinfo(struct sysinfo *info) {
    return syscall(SYS_SYSINFO, info);
}

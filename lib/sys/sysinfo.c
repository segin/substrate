#include <sys/sysinfo.h>
#include <sys/syscall.h>

/*
 * sysinfo() - Returns information on overall system statistics
 */
int sysinfo(struct sysinfo *info) {
    return syscall(SYS_SYSINFO, info);
}

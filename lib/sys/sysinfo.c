#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/*
 * sysinfo() - Returns information on overall system statistics
 */
#ifndef SYS_SYSINFO
#define SYS_SYSINFO 116
#endif

int sysinfo(struct sysinfo *info) {
    return syscall(SYS_SYSINFO, info);
}

/*
 * Memory Statistics API - Stub implementations
 */
#ifndef SYS_VM_STATS
#define SYS_VM_STATS 255
#endif

int sys_vm_stats(sys_vmstat_t *stats) {
    if (!stats) {
        errno = EINVAL;
        return -1;
    }
    return syscall(SYS_VM_STATS, stats);
}

int sys_vm_info(sys_vminfo_t *info) {
    if (!info) {
        errno = EINVAL;
        return -1;
    }
    memset(info, 0, sizeof(*info));
    errno = ENOSYS;
    return -1;
}

int sys_vm_swap(sys_swapinfo_t *swap, size_t *count) {
    if (!swap || !count) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    errno = ENOSYS;
    return -1;
}

int sys_vm_buffers(sys_bufinfo_t *buf) {
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    errno = ENOSYS;
    return -1;
}

int sys_vm_slabs(sys_slabinfo_t *slabs, size_t *count) {
    if (!slabs || !count) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    errno = ENOSYS;
    return -1;
}

/*
 * CPU Information API
 */
int sys_cpu_count(void) {
    /* Return 1 for now (single CPU assumed) */
    return 1;
}

int sys_cpu_info(int cpu, sys_cpuinfo_t *info) {
    if (!info || cpu < 0) {
        errno = EINVAL;
        return -1;
    }
    memset(info, 0, sizeof(*info));
    strcpy(info->vendor, "Unknown");
    strcpy(info->model, "Unknown");
    errno = ENOSYS;
    return -1;
}

int sys_cpu_times(int cpu, sys_cputimes_t *times) {
    if (!times || cpu < 0) {
        errno = EINVAL;
        return -1;
    }
    memset(times, 0, sizeof(*times));
    errno = ENOSYS;
    return -1;
}

int sys_cpu_loadavg(double *avg1, double *avg5, double *avg15) {
    if (!avg1 || !avg5 || !avg15) {
        errno = EINVAL;
        return -1;
    }
    *avg1 = 0.0;
    *avg5 = 0.0;
    *avg15 = 0.0;
    errno = ENOSYS;
    return -1;
}

/*
 * System Information API
 */
int sys_uptime(struct timespec *ts) {
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
    errno = ENOSYS;
    return -1;
}

int sys_boottime(struct timespec *ts) {
    if (!ts) {
        errno = EINVAL;
        return -1;
    }
    ts->tv_sec = 0;
    ts->tv_nsec = 0;
    errno = ENOSYS;
    return -1;
}

int sys_hostname(char *buf, size_t len) {
    if (!buf || len == 0) {
        errno = EINVAL;
        return -1;
    }
    /* Fallback to gethostname if available */
    return gethostname(buf, len);
}

int sys_domainname(char *buf, size_t len) {
    if (!buf || len == 0) {
        errno = EINVAL;
        return -1;
    }
    buf[0] = '\0';
    errno = ENOSYS;
    return -1;
}

int sys_kernel_version(sys_version_t *ver) {
    if (!ver) {
        errno = EINVAL;
        return -1;
    }
    ver->major = 0;
    ver->minor = 1;
    ver->patch = 0;
    strcpy(ver->release, "0.1.0-dev");
    strcpy(ver->version, "Substrate 0.1.0-dev");
    return 0;  /* This one we can implement statically */
}

/*
 * Network Information API - Stub implementations
 */
int sys_net_interfaces(sys_netif_t *ifs, size_t *count) {
    if (!ifs || !count) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    errno = ENOSYS;
    return -1;
}

int sys_net_addrs(const char *ifname, sys_netaddr_t *addrs, size_t *count) {
    if (!ifname || !addrs || !count) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    errno = ENOSYS;
    return -1;
}

int sys_net_stats(const char *ifname, sys_netstats_t *stats) {
    if (!ifname || !stats) {
        errno = EINVAL;
        return -1;
    }
    memset(stats, 0, sizeof(*stats));
    errno = ENOSYS;
    return -1;
}

int sys_net_routes(sys_route_t *routes, size_t *count) {
    if (!routes || !count) {
        errno = EINVAL;
        return -1;
    }
    *count = 0;
    errno = ENOSYS;
    return -1;
}

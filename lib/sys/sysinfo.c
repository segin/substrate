/*
 * lib/sys/sysinfo.c — userspace surface for the system-information
 * APIs declared in <sys/sysinfo.h>.
 *
 * Two implementation strategies coexist here:
 *
 *   1. "Direct syscall" — for things the kernel has a dedicated entry
 *      for (sysinfo(2), the SYS_VM_* family, the SYS_PROC_* family in
 *      proc.c next door).  Thin wrappers, ENOSYS bubbles up from the
 *      kernel if the call hasn't landed.
 *
 *   2. "/proc parser" — for aggregate views that map 1-1 to a
 *      kernel-generated /proc file (meminfo, uptime, loadavg, mounts,
 *      version, vmstat).  Substrate's procfs is the authoritative
 *      datasource for these and reading the text is significantly
 *      faster than wiring a new syscall + ABI for each one.  All
 *      parsers are stateless and reentrant.
 *
 * Permission model (REQ-06-1006 "cross-process visibility"):
 *   System-wide reads (vm / cpu / net / disk / mount aggregates) are
 *   unrestricted.  Per-process reads (proc.c) honour the kernel's own
 *   /proc/<pid>/ ACL — non-root sees only same-uid processes.
 *
 * Two-pass sizing contract (REQ-06-1006):
 *   Every variable-length API takes `(T *out, size_t *count)` where
 *   *count is BOTH input (buffer capacity) and output (entries
 *   actually populated).  Calling with out=NULL is the size query;
 *   the function fills *count and returns 0.  Otherwise it copies
 *   min(*count, available) entries and updates *count.
 *
 * Units:
 *   - VM byte counters (sys_vmstat_t, sys_vminfo_t, sys_swapinfo_t)
 *     are in bytes.
 *   - sys_cputimes_t / sys_procinfo_t::*time are in scheduler jiffies
 *     (substrate: 1 ms each — see CLOCK_TICK_HZ).
 *   - Network and disk byte counters are in bytes.
 *   - Load averages are unscaled doubles (Linux's >>16 normalization
 *     already done by the parser).
 */

#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

/* ===========================================================
 * sysinfo(2): legacy Linux-style aggregate.
 * =========================================================== */
int sysinfo(struct sysinfo *info) {
    return syscall(SYS_SYSINFO, info);
}

/* ===========================================================
 * Small /proc parser helpers.
 * =========================================================== */

/* Read up to `bufsz - 1` bytes from `path` into `buf`, NUL-terminate,
 * return bytes read or -1 (errno set).  Reentrant. */
static long read_proc_file(const char *path, char *buf, size_t bufsz) {
    if (bufsz == 0) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, bufsz - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (long)n;
}

/* Find the value following "Key:" in a meminfo-style key/value blob.
 * Returns the value (in the buffer's number representation, decimal)
 * or 0 if the key isn't found. */
static uint64_t parse_kv_kib(const char *buf, const char *key) {
    size_t keylen = strlen(key);
    const char *p = buf;
    while (p && *p) {
        if (strncmp(p, key, keylen) == 0 && p[keylen] == ':') {
            p += keylen + 1;
            while (*p == ' ' || *p == '\t') p++;
            return (uint64_t)strtoull(p, NULL, 10);
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* ===========================================================
 * Memory Statistics API
 * =========================================================== */
int sys_vm_stats(sys_vmstat_t *stats) {
    if (!stats) { errno = EINVAL; return -1; }
    memset(stats, 0, sizeof(*stats));

    /* Substrate /proc/meminfo emits Linux-style "Key:  N kB" lines.
     * Parse them and convert kB → bytes. */
    char buf[2048];
    if (read_proc_file("/proc/meminfo", buf, sizeof(buf)) < 0) {
        /* Fall back to the kernel syscall, if registered. */
        return syscall(SYS_VM_STATS, stats);
    }
    stats->total       = parse_kv_kib(buf, "MemTotal")     * 1024ULL;
    stats->free        = parse_kv_kib(buf, "MemFree")      * 1024ULL;
    stats->available   = parse_kv_kib(buf, "MemAvailable") * 1024ULL;
    if (stats->available == 0) stats->available = stats->free;
    stats->buffers     = parse_kv_kib(buf, "Buffers")      * 1024ULL;
    stats->cached      = parse_kv_kib(buf, "Cached")       * 1024ULL;
    stats->swap_total  = parse_kv_kib(buf, "SwapTotal")    * 1024ULL;
    stats->swap_free   = parse_kv_kib(buf, "SwapFree")     * 1024ULL;
    stats->swap_cached = parse_kv_kib(buf, "SwapCached")   * 1024ULL;
    return 0;
}

int sys_vm_info(sys_vminfo_t *info) {
    if (!info) { errno = EINVAL; return -1; }
    memset(info, 0, sizeof(*info));
    return syscall(SYS_VM_INFO, info);
}

int sys_vm_swap(sys_swapinfo_t *swap, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    /* No swap subsystem on substrate yet. */
    *count = 0;
    (void)swap;
    return 0;
}

int sys_vm_buffers(sys_bufinfo_t *buf) {
    if (!buf) { errno = EINVAL; return -1; }
    memset(buf, 0, sizeof(*buf));
    return syscall(SYS_VM_BUFFERS, buf);
}

int sys_vm_slabs(sys_slabinfo_t *slabs, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)slabs;
    errno = ENOSYS;
    return -1;
}

/* ===========================================================
 * CPU Information API
 * =========================================================== */
int sys_cpu_count(void) {
    /* TODO: read /proc/cpuinfo and count processor entries.  Substrate
     * exposes a single CPU today; SMP support lands per-CPU stats
     * via /proc/stat. */
    return 1;
}

int sys_cpu_info(int cpu, sys_cpuinfo_t *info) {
    if (!info || cpu < 0) { errno = EINVAL; return -1; }
    memset(info, 0, sizeof(*info));

    /* Pull what we can from CPUID via the inline-asm path.  Substrate
     * runs on i486+ so eax=0 (vendor) and eax=1 (family/model/stepping)
     * are always valid. */
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    memcpy(&info->vendor[0], &ebx, 4);
    memcpy(&info->vendor[4], &edx, 4);
    memcpy(&info->vendor[8], &ecx, 4);
    info->vendor[12] = '\0';

    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    info->stepping = eax & 0xF;
    info->model_id = (eax >> 4) & 0xF;
    info->family   = (eax >> 8) & 0xF;
    info->flags    = edx;       /* feature bits, classic mapping */

    /* Brand string via eax=0x80000002..4 if supported. */
    uint32_t leaf_eax;
    __asm__ __volatile__("cpuid" : "=a"(leaf_eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (leaf_eax >= 0x80000004) {
        uint32_t *m = (uint32_t *)&info->model[0];
        for (int i = 0; i < 3; i++) {
            __asm__ __volatile__("cpuid"
                : "=a"(m[i*4]), "=b"(m[i*4+1]), "=c"(m[i*4+2]), "=d"(m[i*4+3])
                : "a"(0x80000002U + (uint32_t)i));
        }
        info->model[48] = '\0';
    } else {
        strcpy(info->model, "Generic x86");
    }
    (void)cpu;
    /* MHz / cache sizes are tougher without ACPI/MP-table parsing.
     * Leave zero; consumers should treat as "unknown". */
    return 0;
}

int sys_cpu_times(int cpu, sys_cputimes_t *times) {
    if (!times || cpu < 0) { errno = EINVAL; return -1; }
    memset(times, 0, sizeof(*times));
    /* TODO: parse /proc/stat once substrate procfs emits it. */
    errno = ENOSYS;
    return -1;
}

int sys_cpu_loadavg(double *avg1, double *avg5, double *avg15) {
    if (!avg1 || !avg5 || !avg15) { errno = EINVAL; return -1; }
    *avg1 = *avg5 = *avg15 = 0.0;

    char buf[128];
    if (read_proc_file("/proc/loadavg", buf, sizeof(buf)) < 0) return -1;

    /* Format: "0.05 0.03 0.00 1/47 1234\n" */
    double a, b, c;
    if (sscanf(buf, "%lf %lf %lf", &a, &b, &c) != 3) {
        errno = EIO;
        return -1;
    }
    *avg1 = a; *avg5 = b; *avg15 = c;
    return 0;
}

int sys_cpu_topology(int cpu, sys_cputopo_t *topo) {
    if (!topo || cpu < 0) { errno = EINVAL; return -1; }
    /* Single-socket, single-core, single-thread for now.  When SMP
     * lands, this will read from per-CPU MADT / MP-table info. */
    topo->socket_id = 0;
    topo->core_id   = cpu;
    topo->thread_id = 0;
    topo->numa_node = -1;
    return 0;
}

/* ===========================================================
 * System Information API
 * =========================================================== */
int sys_uptime(struct timespec *ts) {
    if (!ts) { errno = EINVAL; return -1; }
    ts->tv_sec = 0; ts->tv_nsec = 0;

    char buf[64];
    if (read_proc_file("/proc/uptime", buf, sizeof(buf)) < 0) return -1;

    /* Format: "12345.67 9876.54\n" — total then idle. */
    double total;
    if (sscanf(buf, "%lf", &total) != 1) { errno = EIO; return -1; }
    ts->tv_sec  = (time_t)total;
    ts->tv_nsec = (long)((total - (double)ts->tv_sec) * 1e9);
    return 0;
}

int sys_boottime(struct timespec *ts) {
    if (!ts) { errno = EINVAL; return -1; }
    struct timespec now, up;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return -1;
    if (sys_uptime(&up) != 0) return -1;
    ts->tv_sec  = now.tv_sec  - up.tv_sec;
    ts->tv_nsec = now.tv_nsec - up.tv_nsec;
    if (ts->tv_nsec < 0) { ts->tv_sec--; ts->tv_nsec += 1000000000; }
    return 0;
}

int sys_hostname(char *buf, size_t len) {
    if (!buf || len == 0) { errno = EINVAL; return -1; }
    /* gethostname() goes through SYS_GETHOSTNAME — the canonical
     * source.  Falling back through /etc/hostname doesn't belong
     * here; that's a sysctl-conf level concern. */
    return gethostname(buf, len);
}

int sys_domainname(char *buf, size_t len) {
    if (!buf || len == 0) { errno = EINVAL; return -1; }
    /* NIS/YP domain isn't a substrate concept yet; return the empty
     * string + success so getdomainname-using code degrades gracefully
     * (matches Linux on a never-set-it install). */
    buf[0] = '\0';
    return 0;
}

int sys_kernel_version(sys_version_t *ver) {
    if (!ver) { errno = EINVAL; return -1; }
    memset(ver, 0, sizeof(*ver));

    /* Prefer /proc/version (kernel-generated, canonical) over compiled
     * constants. */
    char buf[256];
    long n = read_proc_file("/proc/version", buf, sizeof(buf));
    if (n > 0) {
        /* Strip trailing newline. */
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
        strncpy(ver->version, buf, sizeof(ver->version) - 1);
        /* Try to extract X.Y.Z from somewhere in the string. */
        for (const char *p = buf; *p; p++) {
            int M, m, p2;
            if (sscanf(p, "%d.%d.%d", &M, &m, &p2) == 3) {
                ver->major = M; ver->minor = m; ver->patch = p2;
                snprintf(ver->release, sizeof(ver->release), "%d.%d.%d", M, m, p2);
                return 0;
            }
        }
    }
    /* Static fallback. */
    ver->major = 0; ver->minor = 1; ver->patch = 0;
    strcpy(ver->release, "0.1.0-dev");
    strcpy(ver->version, "Substrate 0.1.0-dev");
    return 0;
}

/* ===========================================================
 * Network Information API
 *
 * No socket layer is in place yet (the libc shims at
 * lib/c/src/socket_stubs.c all return ENOSYS).  The userspace surface
 * is stubbed out as a placeholder so callers compile; switch to
 * /proc/net/{dev,route,arp} parsing when the netstack lands.
 * =========================================================== */
int sys_net_interfaces(sys_netif_t *ifs, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)ifs;
    return 0;        /* zero interfaces; not an error */
}

int sys_net_addrs(const char *ifname, sys_netaddr_t *addrs, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)ifname; (void)addrs;
    return 0;
}

int sys_net_stats(const char *ifname, sys_netstats_t *stats) {
    if (!ifname || !stats) { errno = EINVAL; return -1; }
    memset(stats, 0, sizeof(*stats));
    errno = ENODEV;     /* no such interface — substrate has no netstack */
    return -1;
}

int sys_net_routes(sys_route_t *routes, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)routes;
    return 0;
}

int sys_net_arp(sys_arpentry_t *entries, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)entries;
    return 0;
}

/* ===========================================================
 * Disk / Storage Information API
 *
 * sys_mount_list() reads /proc/mounts (substrate's procfs has a
 * gen_mounts handler).  sys_disk_list / sys_disk_stats need
 * /proc/diskstats which isn't emitted yet — return ENOSYS.
 * =========================================================== */
int sys_disk_list(sys_diskinfo_t *disks, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }
    *count = 0;
    (void)disks;
    errno = ENOSYS;
    return -1;
}

int sys_disk_stats(const char *dev, sys_diskstat_t *stats) {
    if (!dev || !stats) { errno = EINVAL; return -1; }
    memset(stats, 0, sizeof(*stats));
    errno = ENOSYS;
    return -1;
}

int sys_mount_list(sys_mountinfo_t *mounts, size_t *count) {
    if (!count) { errno = EINVAL; return -1; }

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) { errno = ENOENT; return -1; }

    size_t want = *count;
    size_t have = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        /* Format: "device mountpoint fstype options dump pass\n" */
        sys_mountinfo_t m;
        memset(&m, 0, sizeof(m));
        char dump[16], pass[16];
        int n = sscanf(line, "%63s %255s %31s %127s %15s %15s",
                       m.device, m.mountpoint, m.fstype, m.options,
                       dump, pass);
        (void)dump; (void)pass;
        if (n < 4) continue;
        if (mounts && have < want) mounts[have] = m;
        have++;
    }
    fclose(f);

    *count = have;
    return 0;
}

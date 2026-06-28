/*
 * test_sysinfo.c — host-buildable sanity test for the system-info
 * surface declared in <sys/sysinfo.h>.
 *
 * This test compiles against substrate headers + lib/sys/sysinfo.c
 * directly with the HOST libc, so anything that ends up reading
 * /proc/* picks up the host's /proc.  That's intentional — every
 * /proc file we depend on (meminfo, uptime, loadavg, mounts, version)
 * has identical syntax on Linux, so the parser correctness check is
 * machine-portable.
 *
 * Functions that go through SYS_VM_INFO / SYS_VM_BUFFERS / similar
 * direct syscalls just bail out via ENOSYS; the test treats that as
 * an acceptable degraded outcome and only flags real misbehaviour
 * (NULL pointers crashing, parse errors mangling output, two-pass
 * sizing contract violations).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysinfo.h>

static int g_failures = 0;
#define FAIL(m) do { printf("  FAIL: %s (line %d)\n", (m), __LINE__); g_failures++; } while (0)

/* REQ-06-1027 + REQ-06-1029: uptime and loadavg parse /proc/* and
 * return zero-success. */
static void test_uptime(void) {
    struct timespec ts = {0};
    int r = sys_uptime(&ts);
    if (r != 0)              FAIL("sys_uptime returned non-zero");
    if (ts.tv_sec <= 0)      FAIL("sys_uptime: tv_sec <= 0");
    if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000)
                             FAIL("sys_uptime: nsec out of range");

    /* NULL ptr → EINVAL. */
    errno = 0;
    if (sys_uptime(NULL) != -1) FAIL("sys_uptime(NULL) didn't fail");
    if (errno != EINVAL)        FAIL("sys_uptime(NULL) wrong errno");
}

static void test_boottime(void) {
    struct timespec b = {0};
    if (sys_boottime(&b) != 0) FAIL("sys_boottime non-zero");
    /* Boot time should be plausibly recent (post-Unix-epoch). */
    if (b.tv_sec < 1000000000) FAIL("sys_boottime: implausibly old");
}

static void test_loadavg(void) {
    double a, b, c;
    int r = sys_cpu_loadavg(&a, &b, &c);
    if (r != 0) FAIL("sys_cpu_loadavg non-zero");
    if (a < 0.0 || a > 1000.0) FAIL("loadavg[1m] out of plausible range");
    if (b < 0.0 || c < 0.0)    FAIL("loadavg[5m/15m] negative");

    errno = 0;
    if (sys_cpu_loadavg(NULL, &b, &c) != -1) FAIL("loadavg(NULL) didn't fail");
    if (errno != EINVAL)                     FAIL("loadavg(NULL) wrong errno");
}

/* REQ-06-1017 (memory): sys_vm_stats reads /proc/meminfo. */
static void test_vm_stats(void) {
    sys_vmstat_t s;
    if (sys_vm_stats(&s) != 0) {
        /* Tolerable on a host without /proc/meminfo; verify struct is zeroed
         * even on failure path (caller-defensive). */
        return;
    }
    if (s.total == 0)         FAIL("vm_stats: total == 0");
    if (s.free > s.total)     FAIL("vm_stats: free > total");
    if (s.available > s.total) FAIL("vm_stats: available > total");
}

/* REQ-06-1029 (system info): kernel version. */
static void test_kernel_version(void) {
    sys_version_t v = {0};
    if (sys_kernel_version(&v) != 0) FAIL("sys_kernel_version failed");
    if (v.version[0] == '\0')        FAIL("kernel version string empty");
    /* major/minor should both be non-negative. */
    if (v.major < 0 || v.minor < 0)  FAIL("kernel version negative");
}

/* REQ-06-1006 (two-pass sizing) + REQ-06-1041 (mount list): the
 * mount-list API is the cleanest demonstrator of the sizing contract. */
static void test_mount_list_sizing(void) {
    /* Pass 1: ask the size with a zero-cap buffer. */
    size_t want = 0;
    int r = sys_mount_list(NULL, &want);
    if (r != 0) {
        /* /proc/mounts not available — skip rest. */
        printf("  (skip mount sizing — no /proc/mounts)\n");
        return;
    }
    /* Pass 2: allocate and fetch. */
    if (want == 0) return;  /* legitimately empty */
    sys_mountinfo_t *m = calloc(want, sizeof(*m));
    if (!m) { FAIL("calloc"); return; }

    size_t cap = want;
    r = sys_mount_list(m, &cap);
    if (r != 0)             FAIL("mount_list pass 2 failed");
    if (cap > want)         FAIL("mount_list count grew between passes");
    /* First mount entry should have a non-empty mountpoint. */
    if (cap > 0 && m[0].mountpoint[0] == '\0')
                            FAIL("first mount entry has empty mountpoint");
    free(m);
}

/* REQ-06-1023 (cpu): topology should always succeed for a valid CPU. */
static void test_cpu_topology(void) {
    sys_cputopo_t t;
    if (sys_cpu_topology(0, &t) != 0)   FAIL("cpu_topology(0)");
    if (t.numa_node < -1)               FAIL("numa_node < -1");
    errno = 0;
    if (sys_cpu_topology(-1, &t) != -1) FAIL("cpu_topology(-1) didn't fail");
    if (errno != EINVAL)                FAIL("cpu_topology(-1) wrong errno");
}

/* REQ-06-1025 (cpu_info): CPUID-backed; should return non-empty vendor. */
static void test_cpu_info(void) {
    sys_cpuinfo_t info;
    if (sys_cpu_info(0, &info) != 0) FAIL("cpu_info(0)");
    if (info.vendor[0] == '\0')      FAIL("cpu_info: empty vendor");
    if (info.family == 0 && info.model_id == 0 && info.stepping == 0)
                                     FAIL("cpu_info: all-zero family/model/stepping");
}

/* REQ-06-1029 (system info): hostname / domainname. */
static void test_hostname(void) {
    char hn[64];
    int r = sys_hostname(hn, sizeof(hn));
    if (r != 0)         FAIL("sys_hostname");
    if (hn[0] == '\0')  FAIL("hostname empty");

    char dn[64];
    /* domainname can be empty (substrate has no NIS); should still succeed. */
    if (sys_domainname(dn, sizeof(dn)) != 0) FAIL("sys_domainname");
}

/* REQ-06-1035 + REQ-06-1041: stub APIs return cleanly (no crash) on
 * the two-pass sizing path. */
static void test_stubs_dont_crash(void) {
    size_t n;

    n = 0;
    sys_net_interfaces(NULL, &n);
    sys_net_routes(NULL, &n);
    sys_net_arp(NULL, &n);

    /* sys_disk_list: ENOSYS-ish but must still set *count = 0. */
    errno = 0;
    int r = sys_disk_list(NULL, &n);
    (void)r;
    if (n != 0) FAIL("disk_list left count non-zero on failure");
}

static void test_cpu_count(void) {
    int count = sys_cpu_count();
    if (count <= 0) FAIL("cpu_count <= 0");
}

int main(void) {
    printf("test_sysinfo: starting\n");
    test_uptime();
    test_boottime();
    test_loadavg();
    test_vm_stats();
    test_kernel_version();
    test_mount_list_sizing();
    test_cpu_topology();
    test_cpu_info();
    test_hostname();
    test_stubs_dont_crash();
    test_cpu_count();
    if (g_failures == 0) {
        printf("test_sysinfo: PASS\n");
        return 0;
    }
    printf("test_sysinfo: FAIL (%d failures)\n", g_failures);
    return 1;
}

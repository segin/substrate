#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

// Mock kernel types and headers
// We need to define some types before including kernel headers if they are missing
// or rely on kernel-specific environment.

// For sys/lock.h
#include <stdbool.h>

// Mocks for kernel functions
void *kmalloc(size_t size) {
    return calloc(1, size); // Use calloc to zero memory (like kzalloc, but safer for mocks)
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock get_time
static uint32_t mock_time = 123456;
uint32_t get_time(void) {
    return mock_time;
}

// Mock cmdline_get
static char mock_cmdline[1024] = "kernel_test_cmdline root=/dev/ram0";
void cmdline_get(char *buf, size_t buf_len) {
    strncpy(buf, mock_cmdline, buf_len);
    buf[buf_len - 1] = '\0';
}

// Mock PMM
static uint32_t mock_total_mem = 1024 * 1024 * 1024; // 1GB
static uint32_t mock_free_mem = 512 * 1024 * 1024;   // 512MB
uint32_t pmm_get_total_memory(void) {
    return mock_total_mem;
}
uint32_t pmm_get_free_memory(void) {
    return mock_free_mem;
}

// Mock VFS Filesystems
#include <vfs/vfs.h>

static filesystem_t mock_fs_proc = { .name = "procfs", .next = NULL };
static filesystem_t mock_fs_ext2 = { .name = "ext2", .next = &mock_fs_proc };

filesystem_t *vfs_get_filesystems(void) {
    return &mock_fs_ext2;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
    // No-op for test
}

// Mock Scheduler
void sched_get_loadavg(unsigned long *loads) {
    // 1.5, 0.5, 0.1
    // LOAD_INT/FRAC macros depend on fixed point math usually.
    // Assuming FSHIFT=11 (2048).
    // 1.5 * 2048 = 3072
    // 0.5 * 2048 = 1024
    // 0.1 * 2048 = 204
    loads[0] = 3072;
    loads[1] = 1024;
    loads[2] = 204;
}

uint32_t sched_count_runnable(void) {
    return 2;
}

uint32_t sched_count_threads(void) {
    return 100;
}

int proc_get_last_pid(void) {
    return 999;
}

// Mock PMAP Stats
#include <arch/i386/pmap.h>

int sys_pmap_stats(struct pmap_stats *out) {
    out->faults = 100;
    out->cow_faults = 10;
    out->zero_fills = 50;
    out->protection_upgrades = 5;
    out->protection_downgrades = 2;
    out->cow_pages_mapped = 20;
    out->cow_duplications = 5;
    out->pages_saved_by_cow = 15;
    out->tlb_invlpg_count = 1000;
    out->tlb_full_flush_count = 10;
    out->total_pmaps = 5;
    return 0;
}

// Mock Personality
#include <exec/perso/personality.h>
struct personality personality_linux = { .name = "Linux", .id = PERS_LINUX };
struct personality personality_native = { .name = "Native", .id = PERS_NATIVE };

struct personality *perso_lookup(int id) {
    if (id == PERS_LINUX) return &personality_linux;
    return &personality_native;
}

// Mock Process
// Define AC_COMM_LEN manually as host system <sys/acct.h> might not provide it
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif
#include <sys/proc.h>
#include <pm/pm.h> // For MAX_PROCS definition

// Define processes array as needed by extern
process_t processes[MAX_PROCS];
process_t *current_process = &processes[0];
mutex_t proctree_lock;

process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

// Needed by sys/proc.h -> sys/lock.h
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
bool spinlock_try_acquire(spinlock_t *lock) { (void)lock; return true; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
bool spinlock_is_held(spinlock_t *lock) { (void)lock; return true; }

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
bool mutex_trylock(mutex_t *m) { (void)m; return true; }
void mutex_unlock(mutex_t *m) { (void)m; }
bool mutex_is_held(mutex_t *m) { (void)m; return true; }


// Include the source file under test
#include "../../sys/fs/procfs.c"

// Tests

void test_gen_cpuinfo() {
    printf("Test: gen_cpuinfo\n");
    char buf[1024];
    uint32_t len = gen_cpuinfo(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "processor\t: 0"));
    assert(strstr(buf, "vendor_id\t: GenuineIntel"));
    assert(strstr(buf, "model name\t: Substrate Virtual CPU"));
    assert(strstr(buf, "cpu MHz\t\t: 1000.000"));

    printf("Output:\n%s\n", buf);
    printf("PASS\n");
}

void test_gen_meminfo() {
    printf("Test: gen_meminfo\n");
    char buf[1024];
    uint32_t len = gen_meminfo(buf, sizeof(buf));

    printf("Output:\n%s\n", buf);

    assert(len > 0);
    // 1GB = 1048576 kB
    // "MemTotal:    %8u kB\n" -> 4 spaces + padding
    // 1048576 is 7 digits. %8u pads 1 space.
    // Total spaces: 4 + 1 = 5.
    assert(strstr(buf, "MemTotal:     1048576 kB") || strstr(buf, "MemTotal:    1048576 kB"));

    // 512MB = 524288 kB
    // "MemFree:     %8u kB\n" -> 5 spaces + padding
    // 524288 is 6 digits. %8u pads 2 spaces.
    // Total spaces: 5 + 2 = 7.
    assert(strstr(buf, "MemFree:       524288 kB") || strstr(buf, "MemFree:      524288 kB"));

    // Used = Total - Free = 524288 kB
    assert(strstr(buf, "MemUsed:       524288 kB") || strstr(buf, "MemUsed:      524288 kB"));

    printf("PASS\n");
}

void test_gen_uptime() {
    printf("Test: gen_uptime\n");
    char buf[1024];
    uint32_t len = gen_uptime(buf, sizeof(buf));

    assert(len > 0);
    // 123456.00 0.00
    assert(strstr(buf, "123456.00 0.00"));

    printf("Output:\n%s\n", buf);
    printf("PASS\n");
}

void test_gen_cmdline() {
    printf("Test: gen_cmdline\n");
    char buf[1024];
    uint32_t len = gen_cmdline(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "kernel_test_cmdline root=/dev/ram0"));
    assert(buf[len-1] == '\n'); // Should end with newline

    printf("Output:\n%s", buf);
    printf("PASS\n");
}

void test_gen_version() {
    printf("Test: gen_version\n");
    char buf[1024];
    uint32_t len = gen_version(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "Substrate version 0.1.0"));

    printf("Output:\n%s", buf);
    printf("PASS\n");
}

void test_gen_loadavg() {
    printf("Test: gen_loadavg\n");
    char buf[1024];
    uint32_t len = gen_loadavg(buf, sizeof(buf));

    assert(len > 0);
    // 1.50 0.50 0.10 2/100 999
    // Wait, snprintf format is %lu.%02lu
    // LOAD_INT(3072) = 3072 >> 11 = 1
    // LOAD_FRAC(3072) = ((3072 & 2047) * 100) >> 11 = (1024 * 100) >> 11 = 102400 / 2048 = 50
    // So 1.50. Correct.
    assert(strstr(buf, "1.50 0.50 0.09 2/100 999") || strstr(buf, "1.50 0.50 0.10 2/100 999"));
    // 0.1 -> 204.
    // LOAD_FRAC(204) = (204 * 100) / 2048 = 20400 / 2048 = 9.96 -> 9
    // So 0.09.

    printf("Output:\n%s", buf);
    printf("PASS\n");
}

void test_proc_pmap_stats_read() {
    printf("Test: proc_pmap_stats_read\n");
    char buf[1024];
    uint32_t len = proc_pmap_stats_read(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "Faults: 100"));
    assert(strstr(buf, "COW Faults: 10"));

    printf("Output:\n%s", buf);
    printf("PASS\n");
}

void test_gen_filesystems() {
    printf("Test: gen_filesystems\n");
    char buf[1024];
    uint32_t len = gen_filesystems(buf, sizeof(buf));

    assert(len > 0);
    // ext2 is first in mock list
    assert(strstr(buf, "\text2\n"));
    // procfs is nodev
    assert(strstr(buf, "nodev\tprocfs\n"));

    printf("Output:\n%s", buf);
    printf("PASS\n");
}

void test_procfs_init() {
    printf("Test: procfs_init\n");
    procfs_init();

    // Verify static nodes are initialized
    // Accessing private procfs_static_nodes requires us to know about it.
    // Since we included the .c file, we can access it.

    assert(strcmp(procfs_static_nodes[0].name, "cpuinfo") == 0);
    assert(procfs_static_nodes[0].flags == FS_FILE);
    assert(procfs_static_nodes[0].read == &procfs_generic_read);

    printf("PASS\n");
}

int main() {
    printf("Running host_test_procfs...\n");

    // Initialize processes array
    memset(processes, 0, sizeof(processes));

    test_gen_cpuinfo();
    test_gen_meminfo();
    test_gen_uptime();
    test_gen_cmdline();
    test_gen_version();
    test_gen_loadavg();
    test_proc_pmap_stats_read();
    test_gen_filesystems();
    test_procfs_init();

    printf("All tests passed!\n");
    return 0;
}

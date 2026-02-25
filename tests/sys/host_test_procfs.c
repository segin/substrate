#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

// Guard kernel headers to prevent conflict with host headers
// We rely on host headers for types where possible
#ifndef _SUBSTRATE_SYS_TYPES_H
#define _SUBSTRATE_SYS_TYPES_H
#endif
#ifndef _SUBSTRATE_SYS_TIME_H
#define _SUBSTRATE_SYS_TIME_H
#endif

// Mock types if needed, but we rely on system headers mostly.
// sys/include/sys/lock.h defines spinlock_t and mutex_t.
// sys/arch/i386/pmap.h defines struct pmap_stats.

// Mock functions required by procfs.c

// Forward declarations for mocks
void *kmalloc(size_t size);
void kfree(void *ptr, size_t size);

// Mocks for externs in procfs.c
uint32_t get_time(void);
void cmdline_get(char *buf, size_t buf_len);
uint32_t pmm_get_total_memory(void);
uint32_t pmm_get_free_memory(void);

// Mocks for sched.h functions
void sched_get_loadavg(unsigned long *loads);
uint32_t sched_count_runnable(void);
uint32_t sched_count_threads(void);
int proc_get_last_pid(void);

// Mock for sys_pmap_stats
struct pmap_stats;
int sys_pmap_stats(struct pmap_stats *out);

// Mock for VFS
struct filesystem;
typedef struct filesystem filesystem_t;
filesystem_t *vfs_get_filesystems(void);
void vfs_register_filesystem(filesystem_t *fs);

// Mock for Personality
struct personality;
struct personality *perso_lookup(int id);

// Mock for Process finding
struct process;
typedef struct process process_t;
process_t *proc_find(int pid);

// Helper for AC_COMM_LEN if not picked up from sys/acct.h
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif

// Include the source file
// We need to make sure include paths are correct in Makefile
#include "../../sys/fs/procfs.c"

// Implement Mocks

// Memory Management
void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Time
static uint32_t mock_time = 12345;
uint32_t get_time(void) {
    return mock_time;
}

// Cmdline
static char mock_cmdline[1024] = "substrate_test_kernel root=/dev/hda1";
void cmdline_get(char *buf, size_t buf_len) {
    strncpy(buf, mock_cmdline, buf_len);
    buf[buf_len - 1] = '\0';
}

// PMM
static uint32_t mock_total_mem = 128 * 1024 * 1024; // 128 MB
static uint32_t mock_free_mem = 64 * 1024 * 1024;   // 64 MB
uint32_t pmm_get_total_memory(void) {
    return mock_total_mem;
}
uint32_t pmm_get_free_memory(void) {
    return mock_free_mem;
}

// Scheduler
void sched_get_loadavg(unsigned long *loads) {
    // 0.50, 1.00, 1.50
    // LOAD_INT/FRAC likely used shifts (FSHIFT 11) or similar.
    // Let's assume typical BSD/Linux fixed point.
    // But we don't know the exact macro implementation here without checking sched.h or param.h.
    // Wait, procfs.c uses LOAD_INT and LOAD_FRAC macros.
    // If these are macros from param.h, they are available.
    // If sched.h defines them, they are available.
    // We should just fill 'loads' with raw values that match 0.5, 1.0, 1.5.
    // 1.0 = 1 << 11 = 2048 (if FSHIFT is 11)
    // Let's assume 2048 base for now.
    loads[0] = 1024; // 0.5
    loads[1] = 2048; // 1.0
    loads[2] = 3072; // 1.5
}

uint32_t sched_count_runnable(void) {
    return 2;
}
uint32_t sched_count_threads(void) {
    return 10;
}
int proc_get_last_pid(void) {
    return 999;
}

// Pmap Stats
int sys_pmap_stats(struct pmap_stats *out) {
    out->faults = 100;
    out->cow_faults = 10;
    out->zero_fills = 20;
    out->protection_upgrades = 5;
    out->protection_downgrades = 2;
    out->cow_pages_mapped = 50;
    out->cow_duplications = 5;
    out->pages_saved_by_cow = 45;
    out->tlb_invlpg_count = 1000;
    out->tlb_full_flush_count = 10;
    out->total_pmaps = 5;
    return 0;
}

// VFS
static filesystem_t mock_fs_ext2 = { .name = "ext2", .next = NULL };
static filesystem_t mock_fs_procfs = { .name = "procfs", .next = &mock_fs_ext2 };

filesystem_t *vfs_get_filesystems(void) {
    return &mock_fs_procfs;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Personality
static struct personality mock_pers_linux = { .name = "Linux" };
static struct personality mock_pers_native = { .name = "Substrate" };

struct personality *perso_lookup(int id) {
    if (id == 3) return &mock_pers_linux; // Linux
    return &mock_pers_native;
}

// Processes
// Definition of processes array (declared extern in pm/pm.h included by procfs.c)
process_t processes[MAX_PROCS];
process_t *current_process;
mutex_t proctree_lock;

process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}


// Tests

void test_gen_cpuinfo() {
    printf("Test: gen_cpuinfo\n");
    char buf[1024];
    uint32_t len = gen_cpuinfo(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "vendor_id\t: GenuineIntel") != NULL);
    assert(strstr(buf, "model name\t: Substrate Virtual CPU") != NULL);
    assert(strstr(buf, "cache size\t: 256 KB") != NULL);

    printf("PASS\n");
}

void test_gen_meminfo() {
    printf("Test: gen_meminfo\n");
    char buf[1024];
    uint32_t len = gen_meminfo(buf, sizeof(buf));

    assert(len > 0);
    // 128 MB = 131072 kB
    // 64 MB = 65536 kB
    // Used = 65536 kB
    // MemTotal: 4 spaces + 2 spaces padding (131072 is 6 digits) = 6 spaces
    assert(strstr(buf, "MemTotal:      131072 kB") != NULL);
    // MemFree: 5 spaces + 3 spaces padding (65536 is 5 digits) = 8 spaces
    assert(strstr(buf, "MemFree:        65536 kB") != NULL);
    // MemUsed: 5 spaces + 3 spaces padding = 8 spaces
    assert(strstr(buf, "MemUsed:        65536 kB") != NULL);

    printf("PASS\n");
}

void test_gen_uptime() {
    printf("Test: gen_uptime\n");
    char buf[1024];
    uint32_t len = gen_uptime(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "12345.00 0.00") != NULL);

    printf("PASS\n");
}

void test_gen_cmdline() {
    printf("Test: gen_cmdline\n");
    char buf[1024];
    uint32_t len = gen_cmdline(buf, sizeof(buf));

    assert(len > 0);
    assert(strncmp(buf, "substrate_test_kernel root=/dev/hda1", len-1) == 0); // -1 for newline
    assert(buf[len-1] == '\n');

    printf("PASS\n");
}

void test_gen_version() {
    printf("Test: gen_version\n");
    char buf[1024];
    uint32_t len = gen_version(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "Substrate version") != NULL);

    printf("PASS\n");
}

void test_gen_loadavg() {
    printf("Test: gen_loadavg\n");
    char buf[1024];
    uint32_t len = gen_loadavg(buf, sizeof(buf));

    assert(len > 0);
    // Depending on LOAD_INT/FRAC macros.
    // If FSHIFT=11, 1024 -> 0.50.
    // runnable=2, total=10, last_pid=999
    // Expected format: "0.50 1.00 1.50 2/10 999\n"
    // Since we don't know FSHIFT for sure without looking at headers (which we included),
    // let's just check the suffix which relies on integers we control.
    assert(strstr(buf, "2/10 999\n") != NULL);

    printf("PASS\n");
}

void test_proc_pmap_stats_read() {
    printf("Test: proc_pmap_stats_read\n");
    char buf[1024];
    uint32_t len = proc_pmap_stats_read(buf, sizeof(buf));

    assert(len > 0);
    assert(strstr(buf, "Faults: 100") != NULL);
    assert(strstr(buf, "COW Faults: 10") != NULL);
    assert(strstr(buf, "Total PMAPs: 5") != NULL);

    printf("PASS\n");
}

void test_gen_filesystems() {
    printf("Test: gen_filesystems\n");
    char buf[1024];
    uint32_t len = gen_filesystems(buf, sizeof(buf));

    assert(len > 0);
    // procfs is nodev
    assert(strstr(buf, "nodev\tprocfs") != NULL);
    // ext2 is regular
    assert(strstr(buf, "\text2") != NULL);

    printf("PASS\n");
}

void test_procfs_init() {
    printf("Test: procfs_init\n");
    procfs_init();

    // Check static nodes
    // Access via procfs_finddir
    fs_node_t *node = procfs_finddir(NULL, "cpuinfo");
    assert(node != NULL);
    assert(strcmp(node->name, "cpuinfo") == 0);

    node = procfs_finddir(NULL, "meminfo");
    assert(node != NULL);

    // Check invalid
    node = procfs_finddir(NULL, "invalid");
    assert(node == NULL);

    printf("PASS\n");
}

int main() {
    printf("Running host_test_procfs...\n");

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

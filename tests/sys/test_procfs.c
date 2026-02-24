#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Mock kernel functions/types for host testing
#define kmalloc malloc
#define kfree(ptr, size) free(ptr)

// Headers
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <pm/pm.h>
#include <vfs/vfs.h>
#include <arch/i386/pmap.h>

// Forward declarations for mocks if not in headers
struct personality;

// Mock spinlock functions
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

// Define processes array (declared extern in pm.h)
process_t processes[MAX_PROCS];
process_t *current_process = NULL;

// Mock other externs
uint32_t get_time(void) { return 1000; }
void cmdline_get(char *buf, size_t buf_len) { strncpy(buf, "test_cmdline", buf_len); }
uint32_t pmm_get_total_memory(void) { return 1024 * 1024; }
uint32_t pmm_get_free_memory(void) { return 512 * 1024; }
filesystem_t *vfs_get_filesystems(void) { return NULL; }
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Match signature: void sched_get_loadavg(unsigned long *loads);
void sched_get_loadavg(unsigned long *loads) { loads[0]=0; loads[1]=0; loads[2]=0; }

uint32_t sched_count_runnable(void) { return 1; }
uint32_t sched_count_threads(void) { return 1; }
int proc_get_last_pid(void) { return 100; }

// Match signature: int sys_pmap_stats(struct pmap_stats *out);
int sys_pmap_stats(struct pmap_stats *stats) { (void)stats; return -1; }

process_t *proc_find(int pid) {
    for(int i=0; i<MAX_PROCS; i++) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}

struct personality *perso_lookup(int id) { (void)id; return NULL; }

// Include the source file directly
#include "../../sys/fs/procfs.c"

// Test Helper Macros
#define ASSERT_NOT_NULL(ptr, msg) do { \
    if ((ptr) == NULL) { \
        printf("FAIL: %s (got NULL)\n", msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_NULL(ptr, msg) do { \
    if ((ptr) != NULL) { \
        printf("FAIL: %s (got %p, expected NULL)\n", msg, (void*)(ptr)); \
        exit(1); \
    } \
} while(0)

#define ASSERT_STREQ(a, b, msg) do { \
    if (strcmp(a, b) != 0) { \
        printf("FAIL: %s\n  Expected: '%s'\n  Actual:   '%s'\n", msg, b, a); \
        exit(1); \
    } \
} while(0)

#define ASSERT_INT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s\n  Expected: %d\n  Actual:   %d\n", msg, (int)(b), (int)(a)); \
        exit(1); \
    } \
} while(0)

void test_procfs_finddir_static(void) {
    printf("Running test_procfs_finddir_static...\n");

    fs_node_t root = {0};
    fs_node_t *node;

    // Initialize procfs static nodes
    procfs_init();

    // Test "cpuinfo"
    node = procfs_finddir(&root, "cpuinfo");
    ASSERT_NOT_NULL(node, "Find cpuinfo");
    ASSERT_STREQ(node->name, "cpuinfo", "cpuinfo name");

    // Test "meminfo"
    node = procfs_finddir(&root, "meminfo");
    ASSERT_NOT_NULL(node, "Find meminfo");
    ASSERT_STREQ(node->name, "meminfo", "meminfo name");

    // Test "uptime"
    node = procfs_finddir(&root, "uptime");
    ASSERT_NOT_NULL(node, "Find uptime");

    // Test "version"
    node = procfs_finddir(&root, "version");
    ASSERT_NOT_NULL(node, "Find version");

    printf("PASS\n");
}

void test_procfs_finddir_pid(void) {
    printf("Running test_procfs_finddir_pid...\n");

    // Setup processes
    memset(processes, 0, sizeof(processes));

    // PID 1
    processes[0].pid = 1;
    strcpy(processes[0].comm, "init");

    // PID 100
    processes[1].pid = 100;
    strcpy(processes[1].comm, "shell");

    // Test lookup PID 1
    fs_node_t root = {0};
    fs_node_t *node = procfs_finddir(&root, "1");
    ASSERT_NOT_NULL(node, "Find PID 1");
    ASSERT_STREQ(node->name, "1", "PID 1 name");
    ASSERT_INT_EQ(node->inode, 1, "PID 1 inode");
    ASSERT_INT_EQ(node->flags, FS_DIRECTORY, "PID 1 is directory");

    // Test lookup PID 100
    node = procfs_finddir(&root, "100");
    ASSERT_NOT_NULL(node, "Find PID 100");
    ASSERT_STREQ(node->name, "100", "PID 100 name");
    ASSERT_INT_EQ(node->inode, 100, "PID 100 inode");

    printf("PASS\n");
}

void test_procfs_finddir_missing(void) {
    printf("Running test_procfs_finddir_missing...\n");

    // Clear processes
    memset(processes, 0, sizeof(processes));
    // Set PID 1
    processes[0].pid = 1;

    fs_node_t root = {0};

    // Lookup non-existent PID
    fs_node_t *node = procfs_finddir(&root, "999");
    ASSERT_NULL(node, "Find missing PID 999");

    // Lookup invalid name
    node = procfs_finddir(&root, "invalid");
    ASSERT_NULL(node, "Find invalid name");

    // Lookup mixed valid/invalid
    node = procfs_finddir(&root, "1a");
    ASSERT_NULL(node, "Find 1a");

    // Lookup empty
    node = procfs_finddir(&root, "");
    ASSERT_NULL(node, "Find empty string");

    printf("PASS\n");
}

int main(void) {
    printf("Starting ProcFS Tests...\n");

    test_procfs_finddir_static();
    test_procfs_finddir_pid();
    test_procfs_finddir_missing();

    printf("All ProcFS Tests Passed!\n");
    return 0;
}

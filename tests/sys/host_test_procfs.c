#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// Mock kernel headers
#include <sys/types.h>
#include <vfs/vfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <sys/lock.h>

// Mock kmalloc/kfree
void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock processes array
process_t processes[MAX_PROCS];
process_t *current_process;

// Mock external functions
uint32_t get_time(void) {
    return 1000;
}

void cmdline_get(char *buf, size_t buf_len) {
    snprintf(buf, buf_len, "mock_kernel_cmdline");
}

uint32_t pmm_get_total_memory(void) {
    return 1024 * 1024 * 1024; // 1GB
}

uint32_t pmm_get_free_memory(void) {
    return 512 * 1024 * 1024; // 512MB
}

filesystem_t *vfs_get_filesystems(void) {
    return NULL;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

void sched_get_loadavg(unsigned long *loads) {
    loads[0] = 100;
    loads[1] = 100;
    loads[2] = 100;
}

uint32_t sched_count_runnable(void) {
    return 1;
}

uint32_t sched_count_threads(void) {
    return 10;
}

int proc_get_last_pid(void) {
    return 100;
}

int sys_pmap_stats(struct pmap_stats *stats) {
    memset(stats, 0, sizeof(struct pmap_stats));
    return 0;
}

struct personality *perso_lookup(int id) {
    (void)id;
    return NULL;
}

process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

// Include the source file to test static functions
#include "sys/fs/procfs.c"

int main() {
    printf("Running procfs tests...\n");

    // Initialize procfs
    procfs_init();

    // Test 1: Find static entry
    fs_node_t *node = procfs_finddir(NULL, "cpuinfo");
    assert(node != NULL);
    assert(strcmp(node->name, "cpuinfo") == 0);
    printf("Test 1 Passed: Found static entry 'cpuinfo'\n");

    // Test 2: Find valid PID
    // Setup a mock process
    memset(processes, 0, sizeof(processes));
    processes[0].pid = 123;
    strcpy(processes[0].comm, "test_proc");

    node = procfs_finddir(NULL, "123");
    assert(node != NULL);
    assert(strcmp(node->name, "123") == 0);
    assert(node->inode == 123);
    assert(node->flags == FS_DIRECTORY);
    printf("Test 2 Passed: Found PID entry '123'\n");

    // Test 3: Find invalid PID (not in processes array)
    node = procfs_finddir(NULL, "999");
    assert(node == NULL);
    printf("Test 3 Passed: Correctly did not find invalid PID '999'\n");

    // Test 4: Find non-existent name
    node = procfs_finddir(NULL, "nonexistent");
    assert(node == NULL);
    printf("Test 4 Passed: Correctly did not find 'nonexistent'\n");

    // Test 5: Find malformed PID string
    node = procfs_finddir(NULL, "123a");
    assert(node == NULL);
    printf("Test 5 Passed: Correctly did not find malformed PID '123a'\n");

    // Additional Test: proc_pid_finddir (inside a PID directory)
    // We can simulate getting a PID directory node and looking up files inside it
    fs_node_t *pid_dir = procfs_finddir(NULL, "123");
    assert(pid_dir != NULL);

    // Look for "status"
    fs_node_t *status_node = pid_dir->finddir(pid_dir, "status");
    assert(status_node != NULL);
    assert(strcmp(status_node->name, "status") == 0);
    assert(status_node->inode == 123);
    kfree(status_node, sizeof(fs_node_t)); // Cleanup
    printf("Test 6 Passed: Found 'status' in PID directory\n");

    // Look for "cmdline"
    fs_node_t *cmdline_node = pid_dir->finddir(pid_dir, "cmdline");
    assert(cmdline_node != NULL);
    assert(strcmp(cmdline_node->name, "cmdline") == 0);
    kfree(cmdline_node, sizeof(fs_node_t)); // Cleanup
    printf("Test 7 Passed: Found 'cmdline' in PID directory\n");

    // Look for invalid file inside PID directory
    fs_node_t *invalid_pid_file = pid_dir->finddir(pid_dir, "invalid");
    assert(invalid_pid_file == NULL);
    printf("Test 8 Passed: Correctly did not find 'invalid' in PID directory\n");

    // Cleanup PID dir node (it was dynamically allocated by procfs_finddir)
    if (pid_dir->close) {
        pid_dir->close(pid_dir);
    } else {
        kfree(pid_dir, sizeof(fs_node_t));
    }

    printf("All tests passed!\n");
    return 0;
}

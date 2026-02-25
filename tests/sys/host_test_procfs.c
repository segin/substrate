/* tests/sys/host_test_procfs.c */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

/* Mock kernel dependencies */
#define MAX_FD 32
#define NSIG 32

/* Forward declarations for structs used in headers */
struct fs_node;
typedef struct fs_node fs_node_t;
struct runqueue;
struct file;
typedef struct file file_t;
struct pmap;
struct pgrp;
struct session;
struct registers;
struct vm_area;
struct vm_map;
struct tty;

/* Include kernel headers */
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif

#include <sys/proc.h>
/* sys/proc.h includes sys/acct.h which defines AC_COMM_LEN */

#include <pm/pm.h>
/* sys/pm/pm.h defines MAX_PROCS */

#include <arch/i386/pmap.h>
#include <exec/perso/personality.h>
#include <vfs/vfs.h>

/* Mock functions */
void *kmalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

/* Mock other kernel functions used by procfs.c */
uint32_t get_time(void) { return 1000; }
void cmdline_get(char *buf, size_t buf_len) { snprintf(buf, buf_len, "kernel_test_cmdline"); }
uint32_t pmm_get_total_memory(void) { return 1024 * 1024 * 1024; }
uint32_t pmm_get_free_memory(void) { return 512 * 1024 * 1024; }
struct filesystem;
struct filesystem *vfs_get_filesystems(void) { return NULL; }
void vfs_register_filesystem(struct filesystem *fs) { (void)fs; }
void sched_get_loadavg(unsigned long *loads) { loads[0] = 0; loads[1] = 0; loads[2] = 0; }
uint32_t sched_count_runnable(void) { return 1; }
uint32_t sched_count_threads(void) { return 10; }
int proc_get_last_pid(void) { return 100; }
int sys_pmap_stats(struct pmap_stats *out) {
    memset(out, 0, sizeof(*out));
    return 0;
}

struct personality mock_perso_native = { .name = "Native" };
struct personality mock_perso_linux = { .name = "Linux" };

struct personality *perso_lookup(int id) {
    if (id == 0) return &mock_perso_native;
    if (id == 3) return &mock_perso_linux; /* PERS_LINUX */
    return NULL;
}

/* Mock proctree_lock */
mutex_t proctree_lock = {0};

/* Mock current_process */
process_t *current_process = NULL;

/* Mock processes array */
process_t processes[MAX_PROCS];

/* Mock proc_find because procfs calls it */
process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}

/* Include the source file to test */
/* We need to define static so that we can access static functions */
/* But wait, including .c gives access to static functions automatically */
#include "sys/fs/procfs.c"

/* Test function */
void test_procfs_finddir(void) {
    /* Initialize static entries */
    procfs_init();

    /* Test static entries */
    fs_node_t *node;

    printf("Testing static entries...\n");
    node = procfs_finddir(NULL, "cpuinfo");
    assert(node != NULL);
    assert(strcmp(node->name, "cpuinfo") == 0);

    node = procfs_finddir(NULL, "meminfo");
    assert(node != NULL);
    assert(strcmp(node->name, "meminfo") == 0);

    /* Test PID lookup */
    printf("Testing PID lookup...\n");

    /* Set up a process */
    memset(processes, 0, sizeof(processes));

    /* PID 123 */
    processes[0].pid = 123;
    processes[0].state = SRUN;
    strcpy(processes[0].comm, "test_proc");

    node = procfs_finddir(NULL, "123");
    assert(node != NULL);
    assert(node->inode == 123);
    assert(strcmp(node->name, "123") == 0);
    assert(node->flags == FS_DIRECTORY);
    kfree(node, sizeof(fs_node_t));

    /* Test PID 0 (usually not valid for lookup unless process exists with PID 0) */
    /* If we have a process with PID 0 */
    processes[1].pid = 0;
    /* But '0' string check in finddir: while loop parses 0. Then 'if (pid > 0)' check prevents it? */
    /* Let's see the code: */
    /* if (pid > 0 && *p == '\0') */
    /* So PID 0 is explicitly excluded. */
    node = procfs_finddir(NULL, "0");
    assert(node == NULL);

    /* Test non-existent PID */
    node = procfs_finddir(NULL, "999");
    assert(node == NULL);

    /* Test invalid strings */
    printf("Testing invalid strings...\n");
    node = procfs_finddir(NULL, "abc");
    assert(node == NULL);

    node = procfs_finddir(NULL, "12a");
    assert(node == NULL);

    node = procfs_finddir(NULL, "");
    assert(node == NULL);

    /* Test -1 */
    node = procfs_finddir(NULL, "-1");
    assert(node == NULL);

    printf("test_procfs_finddir passed\n");
}

int main(void) {
    /* Need to set current_process for some internal checks if any */
    process_t main_proc;
    memset(&main_proc, 0, sizeof(main_proc));
    main_proc.perso_id = 0;
    current_process = &main_proc;

    test_procfs_finddir();
    return 0;
}

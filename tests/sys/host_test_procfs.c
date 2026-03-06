#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

/* Mock Defines */
/* HOST_TEST is defined via compiler flag */

/* Include mocks first */
#include <vfs/vfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <vm/vm_kmem.h>
#include <sys/lock.h>

/* Mocks for externals used in procfs.c */

process_t processes[MAX_PROCS];
process_t *current_process;
mutex_t proctree_lock;
int kmalloc_should_fail = 0;

void *kmalloc(size_t size) {
    if (kmalloc_should_fail) return NULL;
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

uint32_t get_time(void) { return 123456789; }
int cmdline_get(const char *key, char *buf, size_t buf_len) {
    if (!key || !buf || buf_len == 0) return -1;
    if (strcmp(key, "root") == 0) {
        snprintf(buf, buf_len, "/dev/storage/ide0");
        return 0;
    }
    return -1;
}
int cmdline_get_full(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return -1;
    snprintf(buf, buf_len, "serial_debug root=/dev/storage/ide0 init=/bin/native_test");
    return 0;
}
uint32_t pmm_get_total_memory(void) { return 1024 * 1024 * 1024; }
uint32_t pmm_get_free_memory(void) { return 512 * 1024 * 1024; }
filesystem_t *vfs_get_filesystems(void) { return NULL; }
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

void sched_get_loadavg(unsigned long loads[3]) {
    loads[0] = 0; loads[1] = 0; loads[2] = 0;
}
uint32_t sched_count_runnable(void) { return 1; }
uint32_t sched_count_threads(void) { return 10; }
int proc_get_last_pid(void) { return 100; }

int sys_pmap_stats(struct pmap_stats *stats) {
    memset(stats, 0, sizeof(*stats));
    return 0;
}

process_t *proc_find(int pid) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) return &processes[i];
    }
    return NULL;
}

struct personality default_pers = { "Substrate" };
struct personality linux_pers = { "Linux" };

struct personality *perso_lookup(int id) {
    if (id == 1) return &linux_pers;
    return &default_pers;
}

/* Include the source file */
#include "../../sys/fs/procfs.c"

/* Test Helpers */

static uint32_t mock_cpuinfo_gen(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)snprintf(buf, size,
                              "processor\t: 0\n"
                              "vendor_id\t: TestVendor\n");
}

void setup_processes() {
    memset(processes, 0, sizeof(processes));
    for(int i=0; i<MAX_PROCS; i++) processes[i].pid = -1;

    processes[0].pid = 1;
    strcpy(processes[0].comm, "init");
    processes[0].uid = 0;
    processes[0].gid = 0;

    processes[1].pid = 123;
    strcpy(processes[1].comm, "testproc");
    processes[1].uid = 1000;
    processes[1].gid = 1000;

    current_process = &processes[0];
}

/* Tests */

void test_procfs_finddir_static(void) {
    printf("Test: procfs_finddir static entries...\n");
    fs_node_t *node = procfs_finddir(NULL, "cpuinfo");
    assert(node != NULL);
    assert(strcmp(node->name, "cpuinfo") == 0);
    if (node->close) node->close(node);

    node = procfs_finddir(NULL, "meminfo");
    assert(node != NULL);
    assert(strcmp(node->name, "meminfo") == 0);

    node = procfs_finddir(NULL, "cow_stats");
    assert(node != NULL);
    assert(strcmp(node->name, "cow_stats") == 0);

    node = procfs_finddir(NULL, "self");
    assert(node != NULL);
    assert(strcmp(node->name, "self") == 0);
    assert(node->flags == FS_SYMLINK);
    if (node->close) node->close(node);

    node = procfs_finddir(NULL, "nonexistent");
    assert(node == NULL);
    printf("PASS\n");
}

void test_procfs_self_dynamic_target(void) {
    printf("Test: procfs /proc/self dynamic target...\n");

    current_process = &processes[0]; /* PID 1 */
    fs_node_t *self = procfs_finddir(NULL, "self");
    assert(self != NULL);
    assert(self->readlink != NULL);
    char target[64];
    int len = self->readlink(self, target, sizeof(target));
    assert(len > 0);
    target[len] = '\0';
    assert(strcmp(target, "/proc/1/") == 0);
    if (self->close) self->close(self);

    current_process = &processes[1]; /* PID 123 */
    self = procfs_finddir(NULL, "self");
    assert(self != NULL);
    len = self->readlink(self, target, sizeof(target));
    assert(len > 0);
    target[len] = '\0';
    char expected[64];
    snprintf(expected, sizeof(expected), "/proc/%d/", current_process->pid);
    assert(strcmp(target, expected) == 0);
    if (self->close) self->close(self);

    printf("PASS\n");
}

void test_procfs_finddir_pid(void) {
    printf("Test: procfs_finddir PID entries...\n");

    fs_node_t *node = procfs_finddir(NULL, "1");
    assert(node != NULL);
    assert(strcmp(node->name, "1") == 0);
    assert(node->inode == 1);
    assert(node->flags == FS_DIRECTORY);
    if (node->close) node->close(node);

    node = procfs_finddir(NULL, "123");
    assert(node != NULL);
    assert(strcmp(node->name, "123") == 0);
    assert(node->inode == 123);
    if (node->close) node->close(node);

    node = procfs_finddir(NULL, "999");
    assert(node == NULL);

    node = procfs_finddir(NULL, "0");
    assert(node == NULL);

    node = procfs_finddir(NULL, "01");
    assert(node != NULL);
    assert(node->inode == 1);
    assert(strcmp(node->name, "1") == 0);
    if (node->close) node->close(node);

    printf("PASS\n");
}

void test_proc_pid_finddir(void) {
    printf("Test: proc_pid_finddir entries...\n");

    fs_node_t *node = procfs_finddir(NULL, "1");
    assert(node != NULL);

    // Test finding status
    fs_node_t *status = node->finddir(node, "status");
    assert(status != NULL);
    assert(strcmp(status->name, "status") == 0);
    assert(status->flags == FS_FILE);
    if (status->close) status->close(status);

    // Test finding cmdline
    fs_node_t *cmdline = node->finddir(node, "cmdline");
    assert(cmdline != NULL);
    assert(strcmp(cmdline->name, "cmdline") == 0);
    assert(cmdline->flags == FS_FILE);
    if (cmdline->close) cmdline->close(cmdline);

    // Test finding invalid
    fs_node_t *invalid = node->finddir(node, "invalid");
    assert(invalid == NULL);

    if (node->close) node->close(node);

    printf("PASS\n");
}

void test_procfs_finddir_mixed(void) {
    printf("Test: procfs_finddir mixed input...\n");

    fs_node_t *node = procfs_finddir(NULL, "123foo");
    assert(node == NULL);

    node = procfs_finddir(NULL, "foo123");
    assert(node == NULL);

    printf("PASS\n");
}

void test_procfs_kmalloc_fail(void) {
    printf("Test: procfs_finddir with kmalloc failure...\n");
    kmalloc_should_fail = 1;

    // Attempt to find existing PID, should fail gracefully (return NULL)
    // Note: If procfs.c doesn't check for NULL, this will crash.
    fs_node_t *node = procfs_finddir(NULL, "1");

    if (node != NULL) {
        printf("FAIL: procfs_finddir returned node despite kmalloc failure\n");
    } else {
        printf("PASS: procfs_finddir returned NULL on kmalloc failure\n");
    }

    kmalloc_should_fail = 0;
}

void test_proc_status_injection(void) {
    printf("Test: proc_status_injection...\n");

    // Set up a process with injected characters
    processes[1].pid = 999;
    strcpy(processes[1].comm, "fake\nUid:\t0");
    processes[1].uid = 1000;
    processes[1].gid = 1000;

    char buffer[1024];
    proc_generate_status(buffer, sizeof(buffer), &processes[1]);

    // Verify that the newline and tab were sanitized to '_'
    if (strstr(buffer, "fake_Uid:_0") == NULL) {
        printf("Buffer content:\n%s\n", buffer);
        fflush(stdout);
    }
    assert(strstr(buffer, "fake_Uid:_0") != NULL);
    assert(strstr(buffer, "Uid:\t0\n") == NULL);
    assert(strstr(buffer, "\nUid:\t0") == NULL);

    printf("PASS\n");
}

void test_procfs_cow_stats_read(void) {
    printf("Test: procfs cow_stats read...\n");

    fs_node_t *node = procfs_finddir(NULL, "cow_stats");
    assert(node != NULL);
    assert(node->read != NULL);

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    size_t n = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    assert(n > 0);
    assert(strstr(buffer, "cow_faults:") != NULL);
    assert(strstr(buffer, "cow_pages_mapped:") != NULL);
    assert(strstr(buffer, "cow_duplications:") != NULL);
    assert(strstr(buffer, "pages_saved_by_cow:") != NULL);

    printf("PASS\n");
}

void test_procfs_cmdline_read(void) {
    printf("Test: procfs cmdline read...\n");

    fs_node_t *node = procfs_finddir(NULL, "cmdline");
    assert(node != NULL);
    assert(node->read != NULL);

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    size_t n = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    assert(n > 0);
    assert(strstr(buffer, "serial_debug root=/dev/storage/ide0 init=/bin/native_test") != NULL);

    printf("PASS\n");
}

int main() {
    procfs_init();
    assert(procfs_register_entry("cpuinfo", mock_cpuinfo_gen, NULL) == 0);
    setup_processes();

    test_procfs_finddir_static();
    test_procfs_finddir_pid();
    test_proc_pid_finddir();
    test_procfs_finddir_mixed();
    test_proc_status_injection();
    test_procfs_self_dynamic_target();
    test_procfs_cow_stats_read();
    test_procfs_cmdline_read();
    // Uncomment to test crash
    test_procfs_kmalloc_fail();

    printf("All tests passed!\n");
    return 0;
}

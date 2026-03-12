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
#include <vm/vm_page.h>
#include <sys/lock.h>
#include <sys/mount.h>
#include <kern/resource.h>

/* Mocks for externals used in procfs.c */

process_t processes[MAX_PROCS];
process_t *current_process;
mutex_t proctree_lock;
struct mountlist mountlist;
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

void sched_get_loadavg(unsigned long *loads) {
    loads[0] = 0; loads[1] = 0; loads[2] = 0;
}
uint32_t sched_count_runnable(void) { return 1; }
uint32_t sched_count_threads(void) { return 10; }
int proc_get_last_pid(void) { return 100; }

void vm_page_get_vmstat(vm_vmstat_t *stats) {
    memset(stats, 0, sizeof(*stats));
}

int sys_pmap_stats(struct pmap_stats *stats) {
    memset(stats, 0, sizeof(*stats));
    return 0;
}

size_t resource_dump(uint32_t type, char *buf, size_t size) {
    const char *text = (type == RES_IO) ? "3f8-3ff : com1\n" : "fec00000-fec00fff : ioapic\n";
    return (size_t)snprintf(buf, size, "%s", text);
}

size_t bus_dump_tree(char *buf, size_t size) {
    return (size_t)snprintf(buf, size, "pci\n  pci00:07.0 1af4:1001 class=01 subclass=00 progif=00\n");
}

size_t pci_dump_devices(char *buf, size_t size) {
    return (size_t)snprintf(buf, size, "00:07.0 1af4:1001 class=0100 irq=11\n");
}

size_t kobject_uevent_dump(char *buf, size_t size) {
    return (size_t)snprintf(buf, size, "add pci pci00:07.0\nbind pci virtio-blk0\n");
}

size_t proc_emit_cmdline(const process_t *proc, char *buf, size_t buf_len, size_t *argc_out) {
    size_t len = 0;

    if (argc_out) {
        *argc_out = 1;
    }
    if (!proc || !buf || buf_len == 0) {
        return 0;
    }

    len = (size_t)snprintf(buf, buf_len, "%s", proc->comm);
    if (len + 1 < buf_len) {
        buf[len++] = '\0';
    }
    return len;
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

void setup_mounts(void) {
    static struct mount root_mount;
    static struct mount proc_mount;
    static struct mount weird_mount;

    TAILQ_INIT(&mountlist);

    memset(&root_mount, 0, sizeof(root_mount));
    snprintf(root_mount.mnt_stat.f_mntfromname, sizeof(root_mount.mnt_stat.f_mntfromname), "/dev/storage/ide0p1");
    snprintf(root_mount.mnt_stat.f_mntonname, sizeof(root_mount.mnt_stat.f_mntonname), "/");
    snprintf(root_mount.mnt_stat.f_fstypename, sizeof(root_mount.mnt_stat.f_fstypename), "ext2");
    TAILQ_INSERT_TAIL(&mountlist, &root_mount, mnt_list);

    memset(&proc_mount, 0, sizeof(proc_mount));
    snprintf(proc_mount.mnt_stat.f_mntfromname, sizeof(proc_mount.mnt_stat.f_mntfromname), "procfs");
    snprintf(proc_mount.mnt_stat.f_mntonname, sizeof(proc_mount.mnt_stat.f_mntonname), "/proc");
    snprintf(proc_mount.mnt_stat.f_fstypename, sizeof(proc_mount.mnt_stat.f_fstypename), "procfs");
    TAILQ_INSERT_TAIL(&mountlist, &proc_mount, mnt_list);

    memset(&weird_mount, 0, sizeof(weird_mount));
    snprintf(weird_mount.mnt_stat.f_mntfromname, sizeof(weird_mount.mnt_stat.f_mntfromname), "/dev/storage/USB Stick");
    snprintf(weird_mount.mnt_stat.f_mntonname, sizeof(weird_mount.mnt_stat.f_mntonname), "/media/USB Stick");
    snprintf(weird_mount.mnt_stat.f_fstypename, sizeof(weird_mount.mnt_stat.f_fstypename), "fat");
    TAILQ_INSERT_TAIL(&mountlist, &weird_mount, mnt_list);
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

    node = procfs_finddir(NULL, "ioports");
    assert(node != NULL);
    assert(strcmp(node->name, "ioports") == 0);

    node = procfs_finddir(NULL, "iomem");
    assert(node != NULL);
    assert(strcmp(node->name, "iomem") == 0);

    node = procfs_finddir(NULL, "pci");
    assert(node != NULL);
    assert(strcmp(node->name, "pci") == 0);

    node = procfs_finddir(NULL, "devtree");
    assert(node != NULL);
    assert(strcmp(node->name, "devtree") == 0);

    node = procfs_finddir(NULL, "device-events");
    assert(node != NULL);
    assert(strcmp(node->name, "device-events") == 0);

    node = procfs_finddir(NULL, "self");
    assert(node != NULL);
    assert(strcmp(node->name, "self") == 0);
    assert(node->flags == FS_SYMLINK);
    if (node->close) node->close(node);

    node = procfs_finddir(NULL, "nonexistent");
    assert(node == NULL);
    printf("PASS\n");
}

void test_procfs_driver_model_entries(void) {
    char buffer[256];
    fs_node_t *node;
    uint32_t len;

    printf("Test: procfs driver-model entries...\n");

    node = procfs_finddir(NULL, "ioports");
    assert(node != NULL);
    memset(buffer, 0, sizeof(buffer));
    len = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    buffer[len] = '\0';
    assert(strstr(buffer, "3f8-3ff : com1") != NULL);

    node = procfs_finddir(NULL, "iomem");
    assert(node != NULL);
    memset(buffer, 0, sizeof(buffer));
    len = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    buffer[len] = '\0';
    assert(strstr(buffer, "fec00000-fec00fff : ioapic") != NULL);

    node = procfs_finddir(NULL, "pci");
    assert(node != NULL);
    memset(buffer, 0, sizeof(buffer));
    len = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    buffer[len] = '\0';
    assert(strstr(buffer, "00:07.0 1af4:1001") != NULL);

    node = procfs_finddir(NULL, "devtree");
    assert(node != NULL);
    memset(buffer, 0, sizeof(buffer));
    len = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    buffer[len] = '\0';
    assert(strstr(buffer, "pci00:07.0 1af4:1001") != NULL);

    node = procfs_finddir(NULL, "device-events");
    assert(node != NULL);
    memset(buffer, 0, sizeof(buffer));
    len = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    buffer[len] = '\0';
    assert(strstr(buffer, "add pci pci00:07.0") != NULL);
    assert(strstr(buffer, "bind pci virtio-blk0") != NULL);

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

    /* procfs uses static nodes for core entries; lookup should still work. */
    fs_node_t *node = procfs_finddir(NULL, "1");
    assert(node != NULL);
    assert(node->inode == 1);

    kmalloc_should_fail = 0;
    printf("PASS\n");
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

void test_procfs_mounts_read(void) {
    printf("Test: procfs mounts read...\n");

    fs_node_t *node = procfs_finddir(NULL, "mounts");
    assert(node != NULL);
    assert(node->read != NULL);

    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    size_t n = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
    assert(n > 0);
    assert(strstr(buffer, "/dev/storage/ide0p1 / ext2 rw 0 0\n") != NULL);
    assert(strstr(buffer, "procfs /proc procfs rw 0 0\n") != NULL);
    assert(strstr(buffer, "/dev/storage/USB\\040Stick /media/USB\\040Stick fat rw 0 0\n") != NULL);

    {
        static struct mount tmp_mount;

        memset(&tmp_mount, 0, sizeof(tmp_mount));
        snprintf(tmp_mount.mnt_stat.f_mntfromname, sizeof(tmp_mount.mnt_stat.f_mntfromname), "tmpfs");
        snprintf(tmp_mount.mnt_stat.f_mntonname, sizeof(tmp_mount.mnt_stat.f_mntonname), "/tmp");
        snprintf(tmp_mount.mnt_stat.f_fstypename, sizeof(tmp_mount.mnt_stat.f_fstypename), "tmpfs");
        TAILQ_INSERT_TAIL(&mountlist, &tmp_mount, mnt_list);

        memset(buffer, 0, sizeof(buffer));
        n = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
        assert(n > 0);
        assert(strstr(buffer, "tmpfs /tmp tmpfs rw 0 0\n") != NULL);

        TAILQ_REMOVE(&mountlist, &tmp_mount, mnt_list);
        memset(buffer, 0, sizeof(buffer));
        n = node->read(node, 0, sizeof(buffer) - 1, (uint8_t *)buffer);
        assert(n > 0);
        assert(strstr(buffer, "tmpfs /tmp tmpfs rw 0 0\n") == NULL);
    }

    printf("PASS\n");
}

int main() {
    procfs_init();
    assert(procfs_register_entry("cpuinfo", mock_cpuinfo_gen, NULL) == 0);
    setup_processes();
    setup_mounts();

    test_procfs_finddir_static();
    test_procfs_finddir_pid();
    test_proc_pid_finddir();
    test_procfs_finddir_mixed();
    test_proc_status_injection();
    test_procfs_self_dynamic_target();
    test_procfs_cow_stats_read();
    test_procfs_cmdline_read();
    test_procfs_mounts_read();
    test_procfs_driver_model_entries();
    test_procfs_kmalloc_fail();

    printf("All tests passed!\n");
    return 0;
}

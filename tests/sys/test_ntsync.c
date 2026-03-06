/*
 * test_ntsync.c - Behavioral tests for ntsync driver
 */

#include <kern/console.h>
#include <kern/file.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ntsync.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <stdint.h>
#include <string.h>

extern process_t *current_process;

extern int kern_open(const char *path, int flags, int mode);
extern int kern_close(int fd);
extern int kern_ioctl(int fd, uint32_t request, void *arg);

static int tests_passed;
static int tests_failed;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("  [NTSYNC] FAIL: "); \
        kprint(msg); \
        kprint("\n"); \
        tests_failed++; \
        return; \
    } \
} while (0)

static void test_open_isolation(void) {
    int fd1 = kern_open("/dev/ntsync", O_RDWR, 0);
    int fd2 = kern_open("/dev/ntsync", O_RDWR, 0);

    TEST_ASSERT(fd1 >= 0, "open #1 failed");
    TEST_ASSERT(fd2 >= 0, "open #2 failed");
    TEST_ASSERT(fd1 != fd2, "duplicate fd allocation");

    file_t *f1 = current_process->fds[fd1];
    file_t *f2 = current_process->fds[fd2];
    TEST_ASSERT(f1 && f1->f_data, "fd1 missing file data");
    TEST_ASSERT(f2 && f2->f_data, "fd2 missing file data");

    fs_node_t *n1 = (fs_node_t *)f1->f_data;
    fs_node_t *n2 = (fs_node_t *)f2->f_data;

    TEST_ASSERT(n1 != n2, "shared fs_node between opens");
    TEST_ASSERT(n1->impl != 0, "fd1 instance not initialized");
    TEST_ASSERT(n2->impl != 0, "fd2 instance not initialized");
    TEST_ASSERT(n1->impl != n2->impl, "shared ntsync instance between opens");

    kern_close(fd2);
    kern_close(fd1);
    tests_passed++;
}

static void test_wait_arg_validation(void) {
    int inst_fd = kern_open("/dev/ntsync", O_RDWR, 0);
    TEST_ASSERT(inst_fd >= 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int event_fd = kern_ioctl(inst_fd, NTSYNC_IOC_CREATE_EVENT, &eargs);
    TEST_ASSERT(event_fd >= 0, "event creation failed");

    int objs[1];
    objs[0] = event_fd;

    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = 0;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 1;

    wait.pad = 1;
    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ANY, &wait) == -EINVAL,
                "WAIT_ANY accepted non-zero pad");

    wait.pad = 0;
    wait.flags = 0x80000000U;
    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ANY, &wait) == -EINVAL,
                "WAIT_ANY accepted unknown flags");

    wait.flags = 0x80000000U;
    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ALL, &wait) == -EINVAL,
                "WAIT_ALL accepted unknown flags");

    kern_close(event_fd);
    kern_close(inst_fd);
    tests_passed++;
}

static void test_auto_event_wait_consumes_signal(void) {
    int inst_fd = kern_open("/dev/ntsync", O_RDWR, 0);
    TEST_ASSERT(inst_fd >= 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int event_fd = kern_ioctl(inst_fd, NTSYNC_IOC_CREATE_EVENT, &eargs);
    TEST_ASSERT(event_fd >= 0, "event creation failed");

    uint32_t prev = 0xFFFFFFFFU;
    TEST_ASSERT(kern_ioctl(event_fd, NTSYNC_IOC_SET_EVENT, &prev) == 0,
                "SET_EVENT failed");
    TEST_ASSERT(prev == 0, "SET_EVENT previous state incorrect");

    int objs[1];
    objs[0] = event_fd;

    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = UINT64_MAX;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 7;

    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ANY, &wait) == 0,
                "WAIT_ANY failed to acquire signaled auto event");
    TEST_ASSERT(wait.index == 0, "WAIT_ANY returned wrong index");

    struct ntsync_event_args state;
    memset(&state, 0, sizeof(state));
    TEST_ASSERT(kern_ioctl(event_fd, NTSYNC_IOC_READ_EVENT, &state) == 0,
                "READ_EVENT failed");
    TEST_ASSERT(state.signaled == 0, "auto-reset event remained signaled after acquire");

    wait.timeout = 0;
    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ANY, &wait) == -ETIMEDOUT,
                "WAIT_ANY did not time out after auto-reset consume");

    kern_close(event_fd);
    kern_close(inst_fd);
    tests_passed++;
}

static void test_alert_not_spurious_success(void) {
    int inst_fd = kern_open("/dev/ntsync", O_RDWR, 0);
    TEST_ASSERT(inst_fd >= 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int wait_event_fd = kern_ioctl(inst_fd, NTSYNC_IOC_CREATE_EVENT, &eargs);
    TEST_ASSERT(wait_event_fd >= 0, "wait object creation failed");

    int alert_event_fd = kern_ioctl(inst_fd, NTSYNC_IOC_CREATE_EVENT, &eargs);
    TEST_ASSERT(alert_event_fd >= 0, "alert object creation failed");

    int objs[1];
    objs[0] = wait_event_fd;

    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = 0;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 42;
    wait.alert = (uint32_t)alert_event_fd;

    TEST_ASSERT(kern_ioctl(inst_fd, NTSYNC_IOC_WAIT_ANY, &wait) == -ETIMEDOUT,
                "WAIT_ANY reported alert without signal");

    kern_close(alert_event_fd);
    kern_close(wait_event_fd);
    kern_close(inst_fd);
    tests_passed++;
}

void test_ntsync(void) {
    tests_passed = 0;
    tests_failed = 0;

    kprint("  [NTSYNC] Running ntsync tests...\n");

    /*
     * Kernel tests run before VFS mount setup in current boot order.
     * If /dev is not ready yet, skip and rely on host behavioral tests.
     */
    int probe_fd = kern_open("/dev/ntsync", O_RDWR, 0);
    if (probe_fd < 0) {
        kprint("  [NTSYNC] Skipping (device path unavailable at this boot stage)\n");
        return;
    }
    kern_close(probe_fd);

    test_open_isolation();
    test_wait_arg_validation();
    test_auto_event_wait_consumes_signal();
    test_alert_not_spurious_success();

    if (tests_failed == 0) {
        kprint("  [NTSYNC] All tests passed\n");
    } else {
        kprint("  [NTSYNC] Some tests failed\n");
    }
}

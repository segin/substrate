#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

/*
 * test_ntsync_fd.c - Host verification tests for ntsync core behavior.
 */

#ifndef _KERNEL
#define _KERNEL
#endif
#ifndef HOST_TEST
#define HOST_TEST
#endif

typedef struct process process_t;
typedef struct thread thread_t;
typedef struct file file_t;
typedef struct fs_node fs_node_t;

#include <sys/types.h>
#include <sys/proc.h>
#include <kern/file.h>
#include <vfs/vfs.h>

process_t *current_process;
thread_t *current_thread;

int mock_fd_counter = 10;
file_t *mock_files[MAX_FD];
static int mock_sleep_until_ret = -ETIMEDOUT;

int proc_alloc_fd(process_t *p) {
    (void)p;
    if (mock_fd_counter >= MAX_FD) return -EMFILE;
    return mock_fd_counter++;
}

void proc_set_fd(process_t *p, int fd, file_t *f) {
    if (p && fd >= 0 && fd < MAX_FD) {
        p->fds[fd] = f;
    }
    if (fd >= 0 && fd < MAX_FD) {
        mock_files[fd] = f;
    }
}

void proc_clear_fd(process_t *p, int fd) {
    if (p && fd >= 0 && fd < MAX_FD) {
        p->fds[fd] = NULL;
    }
    if (fd >= 0 && fd < MAX_FD) {
        mock_files[fd] = NULL;
    }
}

file_t *file_alloc(void) {
    return calloc(1, sizeof(file_t));
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

int copyin(const void *uaddr, void *kaddr, size_t len) {
    memcpy(kaddr, uaddr, len);
    return 0;
}

int copyout(const void *kaddr, void *uaddr, size_t len) {
    memcpy(uaddr, kaddr, len);
    return 0;
}

void kprint(const char *s) {
    printf("%s", s);
}

void devfs_register_device(fs_node_t *node) {
    (void)node;
}

void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }
int sched_sleep_until(void *chan, uint64_t deadline) { (void)chan; (void)deadline; return mock_sleep_until_ret; }
time_t get_time(void) { return 0; }
time_t get_uptime(void) { return 0; }
uint64_t get_ticks(void) { return 0; }

#include "../../sys/drivers/devices/ntsync.c"

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while (0)

static void reset_mock_state(void) {
    memset(mock_files, 0, sizeof(mock_files));
    mock_fd_counter = 10;
    mock_sleep_until_ret = -ETIMEDOUT;
}

static int test_fd_allocation(void) {
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));

    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "failed to open ntsync instance");

    struct ntsync_sem_args args;
    args.count = 1;
    args.max = 5;

    int fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);
    ASSERT_TRUE(fd == 10, "expected first fd from proc_alloc_fd");
    ASSERT_TRUE(mock_files[fd] != NULL, "proc_set_fd was not called");

    fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);
    ASSERT_TRUE(fd == 11, "expected second fd from proc_alloc_fd");

    ntsync_close(&inst_node);
    return 0;
}

static int test_wait_arg_validation(void) {
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int event_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    ASSERT_TRUE(event_fd >= 0, "event creation failed");

    int objs[1] = { event_fd };
    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = 0;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 1;

    wait.pad = 1;
    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == -EINVAL,
                "WAIT_ANY accepted non-zero pad");

    wait.pad = 0;
    wait.flags = 0x40000000U;
    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == -EINVAL,
                "WAIT_ANY accepted unknown flags");

    wait.flags = 0x40000000U;
    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ALL, &wait) == -EINVAL,
                "WAIT_ALL accepted unknown flags");

    ntsync_close(&inst_node);
    return 0;
}

static int test_auto_reset_wait_any_consumes(void) {
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int event_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    ASSERT_TRUE(event_fd >= 0, "event creation failed");

    file_t *f = mock_files[event_fd];
    ASSERT_TRUE(f != NULL, "event file missing");

    uint32_t prev = 0xFFFFFFFFU;
    ASSERT_TRUE(ntsync_obj_ioctl((fs_node_t *)f->f_data, NTSYNC_IOC_SET_EVENT, &prev) == 0,
                "SET_EVENT failed");
    ASSERT_TRUE(prev == 0, "SET_EVENT previous state mismatch");

    int objs[1] = { event_fd };
    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = UINT64_MAX;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 123;

    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == 0,
                "WAIT_ANY failed on signaled auto event");
    ASSERT_TRUE(wait.index == 0, "WAIT_ANY returned wrong index");

    struct ntsync_event_args state;
    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl((fs_node_t *)f->f_data, NTSYNC_IOC_READ_EVENT, &state) == 0,
                "READ_EVENT failed");
    ASSERT_TRUE(state.signaled == 0, "auto-reset event remained signaled after acquire");

    wait.timeout = 0;
    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == -ETIMEDOUT,
                "WAIT_ANY should time out after event consumption");

    ntsync_close(&inst_node);
    return 0;
}

static int test_alert_not_spurious(void) {
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    struct ntsync_event_args eargs;
    eargs.signaled = 0;
    eargs.manual = 0;

    int wait_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    int alert_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    ASSERT_TRUE(wait_fd >= 0 && alert_fd >= 0, "event creation failed");

    int objs[1] = { wait_fd };
    struct ntsync_wait_args wait;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = 0;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 1;
    wait.owner = 9;
    wait.alert = (uint32_t)alert_fd;

    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == -ETIMEDOUT,
                "WAIT_ANY incorrectly returned alert success");

    ntsync_close(&inst_node);
    return 0;
}

int main(void) {
    printf("Running ntsync host tests...\n");

    current_process = calloc(1, sizeof(process_t));
    current_thread = calloc(1, sizeof(thread_t));
    if (!current_process || !current_thread) {
        fprintf(stderr, "FAIL: allocation failure\n");
        return 1;
    }

    ntsync_init();

    reset_mock_state();
    if (test_fd_allocation() != 0) return 1;

    reset_mock_state();
    if (test_wait_arg_validation() != 0) return 1;

    reset_mock_state();
    if (test_auto_reset_wait_any_consumes() != 0) return 1;

    reset_mock_state();
    if (test_alert_not_spurious() != 0) return 1;

    printf("PASS: ntsync host tests\n");
    return 0;
}

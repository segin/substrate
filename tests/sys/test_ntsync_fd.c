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

static fs_node_t *mock_obj_node(int fd) {
    if (fd < 0 || fd >= MAX_FD) {
        fprintf(stderr, "FAIL: fd out of range\n");
        return NULL;
    }
    if (!mock_files[fd]) {
        fprintf(stderr, "FAIL: fd missing file\n");
        return NULL;
    }
    if (!mock_files[fd]->f_data) {
        fprintf(stderr, "FAIL: fd missing node\n");
        return NULL;
    }
    return (fs_node_t *)mock_files[fd]->f_data;
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

static int test_semaphore_lifecycle(void) {
    fs_node_t inst_node;
    struct ntsync_sem_args args;
    struct ntsync_sem_args state;
    fs_node_t *sem_node;
    uint32_t post;
    int sem_fd;

    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    args.count = 2;
    args.max = 5;
    sem_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);
    ASSERT_TRUE(sem_fd >= 0, "semaphore creation failed");

    sem_node = mock_obj_node(sem_fd);
    ASSERT_TRUE(sem_node != NULL, "semaphore node lookup failed");
    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(sem_node, NTSYNC_IOC_READ_SEM, &state) == 0,
                "READ_SEM failed");
    ASSERT_TRUE(state.count == 2 && state.max == 5, "READ_SEM returned wrong state");

    post = 2;
    ASSERT_TRUE(ntsync_obj_ioctl(sem_node, NTSYNC_IOC_SEM_POST, &post) == 0,
                "SEM_POST failed");
    ASSERT_TRUE(post == 2, "SEM_POST previous count mismatch");

    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(sem_node, NTSYNC_IOC_READ_SEM, &state) == 0,
                "READ_SEM after post failed");
    ASSERT_TRUE(state.count == 4 && state.max == 5, "SEM_POST did not update count");

    ntsync_close(&inst_node);
    return 0;
}

static int test_mutex_lifecycle(void) {
    fs_node_t inst_node;
    struct ntsync_mutex_args args;
    struct ntsync_mutex_args state;
    fs_node_t *mutex_node;
    uint32_t owner;
    int mutex_fd;

    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    args.owner = 123;
    args.count = 1;
    mutex_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_MUTEX, &args);
    ASSERT_TRUE(mutex_fd >= 0, "mutex creation failed");

    mutex_node = mock_obj_node(mutex_fd);
    ASSERT_TRUE(mutex_node != NULL, "mutex node lookup failed");
    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(mutex_node, NTSYNC_IOC_READ_MUTEX, &state) == 0,
                "READ_MUTEX failed");
    ASSERT_TRUE(state.owner == 123 && state.count == 1, "READ_MUTEX returned wrong state");

    args.owner = 123;
    args.count = 0;
    ASSERT_TRUE(ntsync_obj_ioctl(mutex_node, NTSYNC_IOC_MUTEX_UNLOCK, &args) == 0,
                "MUTEX_UNLOCK failed");
    ASSERT_TRUE(args.count == 1, "MUTEX_UNLOCK previous recursion count mismatch");

    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(mutex_node, NTSYNC_IOC_READ_MUTEX, &state) == 0,
                "READ_MUTEX after unlock failed");
    ASSERT_TRUE(state.owner == 0 && state.count == 0, "mutex remained owned after unlock");

    args.owner = 777;
    args.count = 2;
    mutex_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_MUTEX, &args);
    ASSERT_TRUE(mutex_fd >= 0, "owned mutex creation failed");

    mutex_node = mock_obj_node(mutex_fd);
    ASSERT_TRUE(mutex_node != NULL, "owned mutex node lookup failed");
    owner = 777;
    ASSERT_TRUE(ntsync_obj_ioctl(mutex_node, NTSYNC_IOC_KILL_OWNER, &owner) == 0,
                "KILL_OWNER failed");
    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(mutex_node, NTSYNC_IOC_READ_MUTEX, &state) == -EOWNERDEAD,
                "READ_MUTEX did not report abandoned mutex");
    ASSERT_TRUE(state.owner == 0 && state.count == 0, "abandoned mutex returned wrong state");

    ntsync_close(&inst_node);
    return 0;
}

static int test_event_lifecycle(void) {
    fs_node_t inst_node;
    struct ntsync_event_args args;
    struct ntsync_event_args state;
    fs_node_t *event_node;
    uint32_t prev;
    int event_fd;

    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    args.signaled = 0;
    args.manual = 1;
    event_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &args);
    ASSERT_TRUE(event_fd >= 0, "event creation failed");

    event_node = mock_obj_node(event_fd);
    ASSERT_TRUE(event_node != NULL, "event node lookup failed");
    prev = 0xFFFFFFFFU;
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_SET_EVENT, &prev) == 0,
                "SET_EVENT failed");
    ASSERT_TRUE(prev == 0, "SET_EVENT previous state mismatch");

    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_READ_EVENT, &state) == 0,
                "READ_EVENT after set failed");
    ASSERT_TRUE(state.signaled == 1 && state.manual == 1, "event state after set incorrect");

    prev = 0xFFFFFFFFU;
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_RESET_EVENT, &prev) == 0,
                "RESET_EVENT failed");
    ASSERT_TRUE(prev == 1, "RESET_EVENT previous state mismatch");

    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_READ_EVENT, &state) == 0,
                "READ_EVENT after reset failed");
    ASSERT_TRUE(state.signaled == 0, "event remained signaled after reset");

    prev = 0xFFFFFFFFU;
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_PULSE_EVENT, &prev) == 0,
                "PULSE_EVENT failed");
    ASSERT_TRUE(prev == 0, "PULSE_EVENT previous state mismatch");

    memset(&state, 0, sizeof(state));
    ASSERT_TRUE(ntsync_obj_ioctl(event_node, NTSYNC_IOC_READ_EVENT, &state) == 0,
                "READ_EVENT after pulse failed");
    ASSERT_TRUE(state.signaled == 0, "pulse left event signaled");

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

static int test_wait_any_mixed_objects(void) {
    fs_node_t inst_node;
    struct ntsync_sem_args sargs;
    struct ntsync_mutex_args margs;
    struct ntsync_event_args eargs;
    struct ntsync_wait_args wait;
    int objs[3];
    int sem_fd;
    int mutex_fd;
    int event_fd;

    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    sargs.count = 1;
    sargs.max = 2;
    sem_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &sargs);
    ASSERT_TRUE(sem_fd >= 0, "semaphore creation failed");

    margs.owner = 0;
    margs.count = 0;
    mutex_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_MUTEX, &margs);
    ASSERT_TRUE(mutex_fd >= 0, "mutex creation failed");

    eargs.signaled = 0;
    eargs.manual = 0;
    event_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    ASSERT_TRUE(event_fd >= 0, "event creation failed");

    objs[0] = sem_fd;
    objs[1] = mutex_fd;
    objs[2] = event_fd;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = UINT64_MAX;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 3;
    wait.owner = 321;

    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ANY, &wait) == 0,
                "WAIT_ANY on mixed object types failed");
    ASSERT_TRUE(wait.index == 0, "WAIT_ANY did not pick the first signaled object");

    ntsync_close(&inst_node);
    return 0;
}

static int test_wait_all_atomicity(void) {
    fs_node_t inst_node;
    struct ntsync_sem_args sargs;
    struct ntsync_event_args eargs;
    struct ntsync_wait_args wait;
    struct ntsync_sem_args sem_state;
    int objs[2];
    int sem_fd;
    int event_fd;

    memset(&inst_node, 0, sizeof(inst_node));
    ntsync_open_callback(&inst_node);
    ASSERT_TRUE(inst_node.impl != 0, "instance open failed");

    sargs.count = 1;
    sargs.max = 1;
    sem_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &sargs);
    ASSERT_TRUE(sem_fd >= 0, "semaphore creation failed");

    eargs.signaled = 0;
    eargs.manual = 1;
    event_fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_EVENT, &eargs);
    ASSERT_TRUE(event_fd >= 0, "event creation failed");

    objs[0] = sem_fd;
    objs[1] = event_fd;
    memset(&wait, 0, sizeof(wait));
    wait.timeout = 0;
    wait.objs = (uint64_t)(uintptr_t)objs;
    wait.count = 2;
    wait.owner = 555;

    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ALL, &wait) == -ETIMEDOUT,
                "WAIT_ALL should time out when not all objects are signaled");

    memset(&sem_state, 0, sizeof(sem_state));
    ASSERT_TRUE(ntsync_obj_ioctl(mock_obj_node(sem_fd), NTSYNC_IOC_READ_SEM, &sem_state) == 0,
                "READ_SEM after WAIT_ALL timeout failed");
    ASSERT_TRUE(sem_state.count == 1, "WAIT_ALL consumed semaphore despite timeout");

    {
        uint32_t prev = 0;
        ASSERT_TRUE(ntsync_obj_ioctl(mock_obj_node(event_fd), NTSYNC_IOC_SET_EVENT, &prev) == 0,
                    "SET_EVENT before successful WAIT_ALL failed");
    }

    wait.timeout = UINT64_MAX;
    ASSERT_TRUE(ntsync_ioctl(&inst_node, NTSYNC_IOC_WAIT_ALL, &wait) == 0,
                "WAIT_ALL failed when all objects were signaled");

    memset(&sem_state, 0, sizeof(sem_state));
    ASSERT_TRUE(ntsync_obj_ioctl(mock_obj_node(sem_fd), NTSYNC_IOC_READ_SEM, &sem_state) == 0,
                "READ_SEM after successful WAIT_ALL failed");
    ASSERT_TRUE(sem_state.count == 0, "WAIT_ALL did not consume semaphore on success");

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
    if (test_semaphore_lifecycle() != 0) return 1;

    reset_mock_state();
    if (test_mutex_lifecycle() != 0) return 1;

    reset_mock_state();
    if (test_event_lifecycle() != 0) return 1;

    reset_mock_state();
    if (test_auto_reset_wait_any_consumes() != 0) return 1;

    reset_mock_state();
    if (test_wait_any_mixed_objects() != 0) return 1;

    reset_mock_state();
    if (test_wait_all_atomicity() != 0) return 1;

    reset_mock_state();
    if (test_alert_not_spurious() != 0) return 1;

    printf("PASS: ntsync host tests\n");
    return 0;
}

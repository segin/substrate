#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

/*
 * test_ntsync_fd.c - Verification test for NTSync FD Allocation
 *
 * This test verifies that the NTSync driver correctly allocates real kernel
 * file descriptors using proc_alloc_fd() rather than returning raw object indices.
 * It mocks the kernel environment to simulate ioctl calls and checks the returned FDs.
 */

// Mock kernel headers
#ifndef _KERNEL
#define _KERNEL
#endif
#ifndef HOST_TEST
#define HOST_TEST
#endif

// Mock types
typedef struct process process_t;
typedef struct thread thread_t;
typedef struct file file_t;
typedef struct fs_node fs_node_t;

#include <sys/types.h>
#include <sys/proc.h>
#include <kern/file.h>
#include <vfs/vfs.h>

// Globals
process_t *current_process;
thread_t *current_thread;

// Mock implementations
int mock_fd_counter = 10; // Start from 10 to avoid 0-2 and to detect if indices are used (which would start from 0)
file_t *mock_files[MAX_FD];

int proc_alloc_fd(process_t *p) {
    (void)p;
    if (mock_fd_counter >= MAX_FD) return -EMFILE;
    return mock_fd_counter++;
}

void proc_set_fd(process_t *p, int fd, file_t *f) {
    (void)p;
    if (fd >= 0 && fd < MAX_FD) {
        mock_files[fd] = f;
    }
}

void proc_clear_fd(process_t *p, int fd) {
    (void)p;
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

// Stubs for other dependencies
void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }
int sched_sleep_until(void *chan, uint64_t deadline) { (void)chan; (void)deadline; return 0; }
time_t get_time(void) { return 0; }
time_t get_uptime(void) { return 0; }
uint64_t get_ticks(void) { return 0; }

// Include the source directly to test static functions
#include "../../sys/drivers/devices/ntsync.c"

int main() {
    printf("Running ntsync FD allocation test...\n");

    // Setup
    current_process = calloc(1, sizeof(process_t));
    current_thread = calloc(1, sizeof(thread_t));

    // Init driver
    ntsync_init();

    // Open device instance
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));

    // We simulate open() by calling the open callback directly.
    // The driver sets node->impl to the new instance.
    ntsync_open_callback(&inst_node);

    if (inst_node.impl == 0) {
        printf("FAIL: Failed to open ntsync instance\n");
        return 1;
    }

    // Create Semaphore
    struct ntsync_sem_args args;
    args.count = 1;
    args.max = 5;

    // We simulate ioctl() by calling the ioctl callback directly.
    int fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);

    if (fd < 0) {
        printf("FAIL: ioctl returned error %d\n", fd);
        return 1;
    }

    printf("Got FD: %d\n", fd);

    // Check if it used our mock allocator (expecting 10)
    // If it used internal index, it would likely be 0 (first object)
    if (fd != 10) {
        printf("FAIL: Expected FD 10 (from proc_alloc_fd), got %d. Possible raw index returned?\n", fd);
        return 1;
    }

    if (mock_files[fd] == NULL) {
        printf("FAIL: File object not set in mock table via proc_set_fd\n");
        return 1;
    }

    // Create another one
    fd = ntsync_ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);
    printf("Got FD: %d\n", fd);
    if (fd != 11) {
        printf("FAIL: Expected FD 11, got %d\n", fd);
        return 1;
    }

    printf("PASS: FD allocation works correctly and uses kernel allocator\n");

    // Cleanup
    free(current_process);
    free(current_thread);

    return 0;
}

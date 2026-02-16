#define HOST_TEST

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/mman.h>

#ifndef MAP_32BIT
#define MAP_32BIT 0x40
#endif

// Force define AC_COMM_LEN if missing
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif

// Mocks for copyin/copyout tracking
int copyin_called = 0;
int copyout_called = 0;

int copyin(const void *src, void *dst, size_t size) {
    copyin_called++;
    if (!src) return -14; // EFAULT
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    copyout_called++;
    if (!dst) return -14; // EFAULT
    memcpy(dst, src, size);
    return 0;
}

// Mocks for kernel functions
void *kmalloc(size_t size) {
    // For 32-bit pointers on 64-bit host
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
    return ptr;
}
void kfree(void *ptr) {
    // munmap size unknown, leak for test simplicity
}

// Stub spinlocks - removed as ntsync.c provides static inline implementation
// void spinlock_acquire(volatile int *lock) { *lock = 1; }
// void spinlock_release(volatile int *lock) { *lock = 0; }

// kprint stub
void kprint(const char *str) {
    printf("%s", str);
}

int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    return 0;
}

// Stub sched
void sched_wakeup(void *t) {}
void sched_sleep(void *chan) {}
int sched_sleep_until(void *t, uint64_t deadline) { return 0; }

// Stub time
uint64_t get_ticks(void) { return 0; }
long get_time(void) { return 0; }
long get_uptime(void) { return 0; }

// Stub devfs
struct fs_node;
void devfs_register_device(struct fs_node *node) {}

// Include necessary headers to fix types before including ntsync.c
// sys/proc.h needs thread_t and AC_COMM_LEN
// We rely on headers in sys/include.

#include "../../sys/drivers/devices/ntsync.c"

// Thread stub - since we included ntsync.c which includes sys/proc.h,
// thread_t should be defined.
// We need to define current_thread variable.
thread_t *current_thread = NULL;

// Test case
int main() {
    setbuf(stdout, NULL);
    printf("Initializing ntsync driver...\n");
    ntsync_init();

    // Create an instance
    fs_node_t inst_node;
    memset(&inst_node, 0, sizeof(inst_node));
    // Simulate open
    printf("Opening device...\n");
    ntsync_device.open(&inst_node);

    // In a real VFS, the open call would return a file pointing to a node.
    // Here we need to manually hook up the ioctl handler for our test node
    // because ntsync_open_callback doesn't update the passed node's ioctl (it creates a new one inside inst).
    inst_node.ioctl = ntsync_ioctl;

    ntsync_instance_t *inst = (ntsync_instance_t *)(uintptr_t)inst_node.impl;
    if (!inst) {
        printf("Failed to create instance\n");
        return 1;
    }
    printf("Instance created at %p\n", inst);

    // Create a semaphore via ioctl
    struct ntsync_sem_args args;
    args.count = 0;
    args.max = 10;

    printf("Creating semaphore...\n");
    // This call passes a pointer to args. In real system, this is user pointer.
    int sem_fd = inst_node.ioctl(&inst_node, NTSYNC_IOC_CREATE_SEM, &args);

    if (sem_fd < 0) {
        printf("Failed to create semaphore: %d\n", sem_fd);
        return 1;
    }

    printf("Semaphore created with FD %d\n", sem_fd);

    // Get the semaphore object node
    ntsync_object_t *sem_obj = inst->objects[sem_fd];
    fs_node_t *sem_node = &sem_obj->node;

    // Try to post to semaphore
    uint32_t post_count = 1;
    printf("Posting to semaphore...\n");

    // Reset counters before checking vulnerability
    copyin_called = 0;
    copyout_called = 0;

    int ret = sem_node->ioctl(sem_node, NTSYNC_IOC_SEM_POST, &post_count);

    if (ret != 0) {
        printf("Failed to post semaphore: %d\n", ret);
        return 1;
    }

    printf("Post successful. copyin_called = %d\n", copyin_called);

    // Check vulnerability fix
    if (copyin_called > 0) {
        printf("SUCCESS: copyin was called!\n");
    } else {
        printf("FAILURE: copyin was NOT called (fix failed)\n");
        return 1;
    }

    // Also check copyout
    if (copyout_called > 0) {
        printf("SUCCESS: copyout was called!\n");
    } else {
        printf("FAILURE: copyout was NOT called (fix failed)\n");
        return 1;
    }

    // Cleanup
    ntsync_device.close(&inst_node);

    return 0;
}

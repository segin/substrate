#define _KERNEL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

/* Mock headers will be included via -I */
#include <sys/proc.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <vfs/vfs.h>

/* Global state definitions (declared extern in headers) */
process_t *current_process = NULL;
thread_t *current_thread = NULL;
process_t mock_process;
thread_t mock_thread;

/* Mock kernel functions */
void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

int proc_alloc_fd(process_t *p) {
    (void)p;
    /* Find first free slot */
    for (int i = 0; i < MAX_FD; i++) {
        if (p->fds[i] == NULL) {
            return i;
        }
    }
    return -EMFILE;
}

file_t *file_alloc(void) {
    file_t *f = calloc(1, sizeof(file_t));
    return f;
}

void proc_set_fd(process_t *p, int fd, file_t *f) {
    if (fd >= 0 && fd < MAX_FD) {
        p->fds[fd] = f;
    }
}

void proc_clear_fd(process_t *p, int fd) {
    if (fd >= 0 && fd < MAX_FD) {
        p->fds[fd] = NULL;
    }
}

void file_close(file_t *f) {
    free(f);
}

int copyin(const void *uaddr, void *kaddr, size_t len) {
    memcpy(kaddr, uaddr, len);
    return 0;
}

int copyout(const void *kaddr, void *uaddr, size_t len) {
    memcpy(uaddr, kaddr, len);
    return 0;
}

void sched_wakeup(thread_t *t) {
    (void)t;
}

void sched_sleep(thread_t *t) {
    (void)t;
}

int sched_sleep_until(thread_t *t, uint64_t deadline) {
    (void)t;
    (void)deadline;
    return 0;
}

int64_t get_time(void) { return 0; }
uint64_t get_uptime(void) { return 0; }
uint64_t get_ticks(void) { return 0; }

void devfs_register_device(fs_node_t *node) {
    (void)node;
}

/* Include ntsync.c directly */
#include "../../sys/drivers/devices/ntsync.c"

int main() {
    printf("Starting NTSync FD Allocation Test...\n");

    /* Setup mock process */
    memset(&mock_process, 0, sizeof(mock_process));
    current_process = &mock_process;

    memset(&mock_thread, 0, sizeof(mock_thread));
    current_thread = &mock_thread;

    /* Initialize driver */
    ntsync_init();

    /* Create an instance via open callback */
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));

    /* We access ntsync_device from the included C file */
    if (ntsync_device.open) {
        ntsync_device.open(&device_node);
    } else {
        printf("FAIL: ntsync_device.open is NULL\n");
        return 1;
    }

    if (device_node.impl == 0) {
        printf("FAIL: Failed to create ntsync instance\n");
        return 1;
    }

    ntsync_instance_t *inst = (ntsync_instance_t *)(uintptr_t)device_node.impl;
    printf("Instance created at %p\n", inst);

    /* Create a semaphore object */
    struct ntsync_sem_args args;
    args.count = 1;
    args.max = 5;

    /* Call the static function directly */
    int fd = ntsync_create_object(inst, NTSYNC_OBJ_SEM, &args);

    printf("ntsync_create_object returned FD: %d\n", fd);

    if (fd < 0) {
        printf("FAIL: Failed to create object, error %d\n", fd);
        return 1;
    }

    /* Verify FD is valid */
    if (fd >= 32) {
        printf("FAIL: FD %d is out of range (>= 32)\n", fd);
        return 1;
    }

    /* Verify FD points to a file */
    if (current_process->fds[fd] == NULL) {
        printf("FAIL: FD %d is not allocated in process\n", fd);
        return 1;
    }

    file_t *f = current_process->fds[fd];
    if (f->f_data == NULL) {
        printf("FAIL: File data is NULL\n");
        return 1;
    }

    /* Verify file data points to an object */
    fs_node_t *node = (fs_node_t *)f->f_data;
    if (node->ioctl != ntsync_obj_ioctl) {
        printf("FAIL: File ioctl handler mismatch\n");
        return 1;
    }

    ntsync_object_t *obj = (ntsync_object_t *)(uintptr_t)node->impl;
    if (obj->type != NTSYNC_OBJ_SEM) {
        printf("FAIL: Object type mismatch (expected SEM)\n");
        return 1;
    }

    if (obj->sem.count != 1 || obj->sem.max != 5) {
        printf("FAIL: Semaphore args mismatch\n");
        return 1;
    }

    printf("SUCCESS: FD allocation verified.\n");

    /* Cleanup */
    if (node->close) node->close(node);
    proc_clear_fd(current_process, fd);
    file_close(f);

    /* Close instance */
    if (device_node.close) device_node.close(&device_node);

    return 0;
}

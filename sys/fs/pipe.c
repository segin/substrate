#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <sys/lock.h>
#include <kern/sleepq.h>

#define PIPE_SIZE 4096

typedef struct {
    uint8_t  buffer[PIPE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    void    *wait_read;
    void    *wait_write;
    mutex_t lock;
} pipe_t;

static void pipe_wait(void *chan, mutex_t *m) {
    sleepq_add(chan, current_thread);
    mutex_unlock(m);
    sched_yield();
    mutex_lock(m);
}

static size_t pipe_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    pipe_t *p = (pipe_t *)(uintptr_t)node->impl;
    (void)offset;

    mutex_lock(&p->lock);

    while (p->count == 0) {
        pipe_wait(p->wait_read, &p->lock);
    }

    size_t i = 0;
    while (i < size && p->count > 0) {
        buffer[i++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
        p->count--;
    }

    sleepq_wake_all(p->wait_write);
    mutex_unlock(&p->lock);
    return i;
}

static size_t pipe_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    pipe_t *p = (pipe_t *)(uintptr_t)node->impl;
    (void)offset;
    size_t written = 0;

    mutex_lock(&p->lock);

    while (written < size) {
        while (p->count == PIPE_SIZE) {
            pipe_wait(p->wait_write, &p->lock);
        }

        while (written < size && p->count < PIPE_SIZE) {
            p->buffer[p->head] = buffer[written++];
            p->head = (p->head + 1) % PIPE_SIZE;
            p->count++;
        }

        sleepq_wake_all(p->wait_read);
    }

    mutex_unlock(&p->lock);
    return written;
}

void pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(pipe_t));
    p->wait_read = &p->head;
    p->wait_write = &p->tail;
    mutex_init(&p->lock, "pipe_lock");

    fs_node_t *rn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(rn, 0, sizeof(fs_node_t));
    strcpy(rn->name, "pipe_read");
    rn->flags = FS_PIPE;
    rn->read = &pipe_read;
    rn->impl = (uintptr_t)p;

    fs_node_t *wn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(wn, 0, sizeof(fs_node_t));
    strcpy(wn->name, "pipe_write");
    wn->flags = FS_PIPE;
    wn->write = &pipe_write;
    wn->impl = (uintptr_t)p;

    *read_node = rn;
    *write_node = wn;
}

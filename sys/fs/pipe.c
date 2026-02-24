#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <sys/wait_queue.h>
#include <sys/poll.h>

#define PIPE_SIZE 4096

typedef struct {
    uint8_t  buffer[PIPE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    void    *wait_read;
    void    *wait_write;
    wait_queue_head_t wq;
} pipe_t;

static int pipe_poll(fs_node_t *node, void *waiter) {
    pipe_t *p = (pipe_t *)(uintptr_t)node->impl;

    poll_wait(node, &p->wq, waiter);

    int mask = 0;
    if (p->count > 0) mask |= POLLIN | POLLRDNORM;
    if (p->count < PIPE_SIZE) mask |= POLLOUT | POLLWRNORM;

    return mask;
}

static size_t pipe_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    pipe_t *p = (pipe_t *)(uintptr_t)node->impl;
    (void)offset;

    while (p->count == 0) {
        sched_sleep(p->wait_read);
    }

    size_t i = 0;
    while (i < size && p->count > 0) {
        buffer[i++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
        p->count--;
    }

    sched_wakeup(p->wait_write);
    wake_up_all(&p->wq);
    return i;
}

static size_t pipe_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    pipe_t *p = (pipe_t *)(uintptr_t)node->impl;
    (void)offset;

    while (p->count + size > PIPE_SIZE) {
        sched_sleep(p->wait_write);
    }

    size_t i = 0;
    while (i < size) {
        p->buffer[p->head] = buffer[i++];
        p->head = (p->head + 1) % PIPE_SIZE;
        p->count++;
    }

    sched_wakeup(p->wait_read);
    wake_up_all(&p->wq);
    return i;
}

void pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(pipe_t));
    p->wait_read = &p->head;
    p->wait_write = &p->tail;
    init_waitqueue_head(&p->wq);

    fs_node_t *rn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(rn, 0, sizeof(fs_node_t));
    strcpy(rn->name, "pipe_read");
    rn->flags = FS_PIPE;
    rn->read = &pipe_read;
    rn->poll = &pipe_poll;
    rn->impl = (uintptr_t)p;

    fs_node_t *wn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(wn, 0, sizeof(fs_node_t));
    strcpy(wn->name, "pipe_write");
    wn->flags = FS_PIPE;
    wn->write = &pipe_write;
    wn->poll = &pipe_poll;
    wn->impl = (uintptr_t)p;

    *read_node = rn;
    *write_node = wn;
}

#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <sys/lock.h>
#include <sys/errno.h>
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
    uint32_t readers_open;
    uint32_t writers_open;
} pipe_t;

typedef struct {
    pipe_t *pipe;
    uint8_t is_writer;
} pipe_endpoint_t;

static void pipe_wait(void *chan, mutex_t *m) {
    sleepq_add(chan, current_thread);
    mutex_unlock(m);
    sched_yield();
    mutex_lock(m);
}

static size_t pipe_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    pipe_t *p = ep ? ep->pipe : NULL;
    (void)offset;
    if (!p) {
        return 0;
    }

    mutex_lock(&p->lock);

    while (p->count == 0) {
        if (p->writers_open == 0) {
            mutex_unlock(&p->lock);
            return 0;
        }
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
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    pipe_t *p = ep ? ep->pipe : NULL;
    (void)offset;
    size_t written = 0;
    if (!p) {
        return 0;
    }

    mutex_lock(&p->lock);

    while (written < size) {
        if (p->readers_open == 0) {
            mutex_unlock(&p->lock);
            return written;
        }

        while (p->count == PIPE_SIZE) {
            if (p->readers_open == 0) {
                mutex_unlock(&p->lock);
                return written;
            }
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

static void pipe_close(fs_node_t *node) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    if (!ep || !ep->pipe) {
        return;
    }

    pipe_t *p = ep->pipe;

    mutex_lock(&p->lock);
    if (ep->is_writer) {
        if (p->writers_open > 0) {
            p->writers_open--;
        }
        sleepq_wake_all(p->wait_read);
    } else {
        if (p->readers_open > 0) {
            p->readers_open--;
        }
        sleepq_wake_all(p->wait_write);
    }
    int free_pipe = (p->readers_open == 0 && p->writers_open == 0);
    mutex_unlock(&p->lock);

    node->impl = 0;
    kfree(ep, sizeof(*ep));
    kfree(node, sizeof(*node));

    if (free_pipe) {
        kfree(p, sizeof(*p));
    }
}

void pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(pipe_t));
    p->wait_read = &p->head;
    p->wait_write = &p->tail;
    mutex_init(&p->lock, "pipe_lock");
    p->readers_open = 1;
    p->writers_open = 1;

    pipe_endpoint_t *read_ep = (pipe_endpoint_t *)kmalloc(sizeof(pipe_endpoint_t));
    pipe_endpoint_t *write_ep = (pipe_endpoint_t *)kmalloc(sizeof(pipe_endpoint_t));
    memset(read_ep, 0, sizeof(*read_ep));
    memset(write_ep, 0, sizeof(*write_ep));
    read_ep->pipe = p;
    write_ep->pipe = p;
    write_ep->is_writer = 1;

    fs_node_t *rn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(rn, 0, sizeof(fs_node_t));
    strncpy(rn->name, "pipe_read", sizeof(rn->name) - 1);
    rn->name[sizeof(rn->name) - 1] = '\0';
    rn->flags = FS_PIPE;
    rn->read = &pipe_read;
    rn->close = &pipe_close;
    rn->impl = (uintptr_t)read_ep;

    fs_node_t *wn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(wn, 0, sizeof(fs_node_t));
    strncpy(wn->name, "pipe_write", sizeof(wn->name) - 1);
    wn->name[sizeof(wn->name) - 1] = '\0';
    wn->flags = FS_PIPE;
    wn->write = &pipe_write;
    wn->close = &pipe_close;
    wn->impl = (uintptr_t)write_ep;

    *read_node = rn;
    *write_node = wn;
}

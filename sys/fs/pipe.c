#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>

#define PIPE_SIZE 4096

typedef struct {
    uint8_t  buffer[PIPE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    void    *wait_read;
    void    *wait_write;
} pipe_t;

static uint32_t pipe_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    pipe_t *p = (pipe_t *)node->impl;
    (void)offset;

    while (p->count == 0) {
        sched_sleep(p->wait_read);
    }

    uint32_t i = 0;
    while (i < size && p->count > 0) {
        buffer[i++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
        p->count--;
    }

    sched_wakeup(p->wait_write);
    return i;
}

static uint32_t pipe_write(fs_node_t *node, off_t offset, uint32_t size, const uint8_t *buffer) {
    pipe_t *p = (pipe_t *)node->impl;
    (void)offset;

    while (p->count + size > PIPE_SIZE) {
        sched_sleep(p->wait_write);
    }

    uint32_t i = 0;
    while (i < size) {
        p->buffer[p->head] = buffer[i++];
        p->head = (p->head + 1) % PIPE_SIZE;
        p->count++;
    }

    sched_wakeup(p->wait_read);
    return i;
}

void pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    memset(p, 0, sizeof(pipe_t));
    p->wait_read = &p->head;
    p->wait_write = &p->tail;

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

#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <vfs/vfs.h>
#include <string.h>
#include <stdio.h>

#define LPT_PORT_COUNT 3
#define LPT_MAJOR      6

static const uint16_t lpt_ports[LPT_PORT_COUNT] = { 0x378, 0x278, 0x3BC };
static fs_node_t lpt_nodes[LPT_PORT_COUNT];
static int lpt_nodes_registered = 0;

static uint16_t lpt_node_port(const fs_node_t *node) {
    return (uint16_t)(node ? node->impl : 0);
}

static int lpt_ready(uint16_t port) {
    /* Busy is inverted in status bit 7: set means not busy. */
    return (inb(port + 1) & 0x80) != 0;
}

static void lpt_strobe(uint16_t port) {
    uint8_t control = inb(port + 2);
    outb(port + 2, control | 0x01);
    io_wait();
    outb(port + 2, control & (uint8_t)~0x01);
}

static void lpt_open(fs_node_t *node) {
    (void)node;
}

static void lpt_close(fs_node_t *node) {
    (void)node;
}

static size_t lpt_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)offset;
    uint16_t port = lpt_node_port(node);
    size_t count = 0;

    while (count < size) {
        buffer[count++] = inb(port + 1);
    }

    return count;
}

static size_t lpt_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)offset;
    uint16_t port = lpt_node_port(node);
    size_t count = 0;

    while (count < size) {
        uint32_t spins = 0;
        while (!lpt_ready(port)) {
            if (++spins > 100000) {
                return count;
            }
        }

        outb(port + 0, buffer[count++]);
        lpt_strobe(port);
    }

    return count;
}

static int lpt_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    (void)request;
    (void)arg;
    return -ENOTTY;
}

static int lpt_poll(fs_node_t *node, void *waiter) {
    (void)waiter;
    uint16_t port = lpt_node_port(node);
    int events = POLLIN | POLLRDNORM;

    if (lpt_ready(port)) {
        events |= POLLOUT | POLLWRNORM;
    }

    return events;
}

void lpt_init(void) {
    if (lpt_nodes_registered) return;

    for (uint32_t i = 0; i < LPT_PORT_COUNT; i++) {
        fs_node_t *node = &lpt_nodes[i];
        uint16_t port = lpt_ports[i];

        /* Control: IRQ disabled, selected, init high. */
        outb(port + 2, 0x0C);

        memset(node, 0, sizeof(*node));
        snprintf(node->name, sizeof(node->name), "comm/parallel%u", i);
        node->flags = FS_CHARDEVICE;
        node->mask = 0660;
        node->uid = 0;
        node->gid = 0;
        node->rdev = (LPT_MAJOR << 8) | i;
        node->impl = (uintptr_t)port;
        node->open = lpt_open;
        node->close = lpt_close;
        node->read = lpt_read;
        node->write = lpt_write;
        node->ioctl = lpt_ioctl;
        node->poll = lpt_poll;

        devfs_register_device(node);
        kprintf("parallel: /dev/%s registered (LPT%u @ 0x%x)\n",
                node->name, i + 1, port);
    }

    lpt_nodes_registered = 1;
}

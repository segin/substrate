#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/device.h>
#include <kern/isapnp.h>
#include <kern/resource.h>
#include <drivers/console/uart/uart.h>
#include <sys/termios.h>
#include <vfs/vfs.h>

static uint8_t mock_ports[0x10000];
static int legacy_uart_present = 1;
static struct device *fake_isa_devices;

#define _IO_H
static int mock_port_present(uint16_t port) {
    if (legacy_uart_present) {
        if ((port >= UART_COM1 && port < UART_COM1 + 8) ||
            (port >= UART_COM2 && port < UART_COM2 + 8) ||
            (port >= UART_COM3 && port < UART_COM3 + 8) ||
            (port >= UART_COM4 && port < UART_COM4 + 8)) {
            return 1;
        }
    }

    for (struct device *dev = fake_isa_devices; dev != NULL; dev = dev->bus_next) {
        struct resource *res = dev->resources;

        while (res != NULL) {
            if (res->type == RES_IO && port >= res->start && port <= res->end) {
                return 1;
            }
            res = res->sibling;
        }
    }

    return 0;
}

static inline uint8_t inb(uint16_t port) {
    return mock_ports[port];
}

static inline void outb(uint16_t port, uint8_t value) {
    if (mock_port_present(port)) {
        mock_ports[port] = value;
    }
}

int isa_device_present(const char *name) {
    (void)name;
    return legacy_uart_present;
}

struct device *isa_first_device(void) {
    return fake_isa_devices;
}

struct device *isa_next_device(struct device *dev) {
    return dev ? dev->bus_next : NULL;
}

struct resource *isa_device_resource(struct device *dev, uint32_t type, unsigned index) {
    struct resource *res = dev ? dev->resources : NULL;

    while (res != NULL) {
        if (res->type == type) {
            if (index == 0) {
                return res;
            }
            index--;
        }
        res = res->sibling;
    }
    return NULL;
}

void console_push_char(char c) {
    (void)c;
}

void sysrq_handle(int key) {
    (void)key;
}

void devfs_register_device(fs_node_t *node) {
    (void)node;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int copyin(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

int copyout(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

/*
 * The uart driver takes its port lock through spinlock_acquire_irq(), which
 * is inline in sys/include/sys/lock.h and calls these.  Single-threaded host
 * test: taking the lock is a no-op, but the symbols must exist.
 */
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

#include "../../sys/drivers/console/uart/uart.c"

static void reset_state(void) {
    memset(mock_ports, 0, sizeof(mock_ports));
    legacy_uart_present = 1;
    fake_isa_devices = NULL;
    memset(uart_detected_ports, 0, sizeof(uart_detected_ports));
    uart_ports_scanned = 0;
    uart_nodes_registered = 0;
    uart_base_port = UART_COM1;
    uart_base_index = 0;
}

static void test_uart_tiocmget_reports_modem_status_lines(void) {
    fs_node_t node;
    int bits;

    reset_state();
    memset(&node, 0, sizeof(node));
    node.impl = UART_COM1;

    mock_ports[UART_COM1 + 4] = UART_MCR_DTR | UART_MCR_RTS;
    mock_ports[UART_COM1 + 6] = UART_MSR_CTS | UART_MSR_DSR | UART_MSR_DCD | UART_MSR_RI;

    bits = 0;
    assert(uart_node_ioctl(&node, TIOCMGET, &bits) == 0);
    assert((bits & TIOCM_DTR) != 0);
    assert((bits & TIOCM_RTS) != 0);
    assert((bits & TIOCM_CTS) != 0);
    assert((bits & TIOCM_DSR) != 0);
    assert((bits & TIOCM_CD) != 0);
    assert((bits & TIOCM_RI) != 0);
}

static void test_uart_select_port_uses_pnp_assigned_io_port(void) {
    static struct resource io_res;
    static struct device dev;

    reset_state();
    legacy_uart_present = 0;
    memset(&dev, 0, sizeof(dev));
    memset(&io_res, 0, sizeof(io_res));

    dev.vendor_id = ISAPNP_VENDOR('P', 'N', 'P');
    dev.device_id = 0x0501;
    io_res.type = RES_IO;
    io_res.start = 0x2A0;
    io_res.end = 0x2A7;
    dev.resources = &io_res;
    fake_isa_devices = &dev;

    assert(uart_select_port(0) == 0);
    assert(uart_init() == 0);
    assert(uart_base_port == 0x2A0);
    assert(mock_ports[0x2A0 + 1] == (UART_IER_RDA | UART_IER_RLS));
}

int main(void) {
    test_uart_tiocmget_reports_modem_status_lines();
    test_uart_select_port_uses_pnp_assigned_io_port();
    puts("host_test_uart: PASS");
    return 0;
}

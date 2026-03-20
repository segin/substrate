#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/termios.h>
#include <vfs/vfs.h>

static uint8_t mock_ports[0x10000];

#define _IO_H
static inline uint8_t inb(uint16_t port) {
    return mock_ports[port];
}

static inline void outb(uint16_t port, uint8_t value) {
    mock_ports[port] = value;
}

int isa_device_present(const char *name) {
    (void)name;
    return 1;
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

#include "../../sys/drivers/console/uart/uart.c"

static void reset_state(void) {
    memset(mock_ports, 0, sizeof(mock_ports));
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

int main(void) {
    test_uart_tiocmget_reports_modem_status_lines();
    puts("host_test_uart: PASS");
    return 0;
}

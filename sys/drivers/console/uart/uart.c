#include <kern/console.h>
#include <kern/sysrq.h>
#include <drivers/console/uart/uart.h>
#include <arch/x86-common/io.h>
#include <arch/i386/idt.h>
#include <kern/isa.h>
#include <sys/termios.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <vfs/vfs.h>
#include <string.h>
#include <stdio.h>

int uart_received(void);

#define UART_PORT_COUNT 4

/* LSR (Line Status Register) bits — offset +5 */
#define UART_LSR_DR         0x01  /* Data Ready */
#define UART_LSR_OE         0x02  /* Overrun Error */
#define UART_LSR_PE         0x04  /* Parity Error */
#define UART_LSR_FE         0x08  /* Framing Error */
#define UART_LSR_BI         0x10  /* Break Indicator */
#define UART_LSR_THRE       0x20  /* Transmitter Holding Register Empty */
#define UART_LSR_TEMT       0x40  /* Transmitter Empty */
#define UART_LSR_FIFO_ERR   0x80  /* Error in FIFO */

/* MCR (Modem Control Register) bits — offset +4 */
#define UART_MCR_DTR        0x01
#define UART_MCR_RTS        0x02
#define UART_MCR_OUT1       0x04
#define UART_MCR_OUT2       0x08
#define UART_MCR_LOOP       0x10
#define UART_MCR_AFE        0x20  /* Auto Flow Control Enable (16550A+) */

/* MSR (Modem Status Register) bits — offset +6 */
#define UART_MSR_DCTS       0x01  /* Delta CTS */
#define UART_MSR_DDSR       0x02  /* Delta DSR */
#define UART_MSR_TERI       0x04  /* Trailing Edge RI */
#define UART_MSR_DDCD       0x08  /* Delta DCD */
#define UART_MSR_CTS        0x10
#define UART_MSR_DSR        0x20
#define UART_MSR_RI         0x40
#define UART_MSR_DCD        0x80

/* IER (Interrupt Enable Register) bits — offset +1 */
#define UART_IER_RDA        0x01  /* Received Data Available */
#define UART_IER_THRE       0x02  /* Transmitter Holding Register Empty */
#define UART_IER_RLS        0x04  /* Receiver Line Status */
#define UART_IER_MS         0x08  /* Modem Status */

/* TX output buffer */
#define UART_TX_BUF_SIZE    256

static struct {
    uint8_t buf[UART_TX_BUF_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int count;
    int xoff_held;       /* 1 = XON/XOFF flow stopped */
} uart_tx;

/* SysRq state for serial break detection */
static int uart_sysrq_pending = 0;

/* Error counters */
static uint32_t uart_err_overrun;
static uint32_t uart_err_parity;
static uint32_t uart_err_framing;
static uint32_t uart_err_break;

#define UART_PORT_COUNT 4

static const uint16_t uart_ports[UART_PORT_COUNT] = {
    UART_COM1, UART_COM2, UART_COM3, UART_COM4
};

static uint16_t uart_base_port = UART_COM1;
static uint32_t uart_base_index;
static fs_node_t uart_nodes[UART_PORT_COUNT];
static int uart_nodes_registered = 0;

static int uart_probe_port(uint16_t port) {
    uint8_t old;
    uint8_t probe;

    old = inb(port + 7);
    outb(port + 7, 0x5A);
    probe = inb(port + 7);
    outb(port + 7, old);
    return probe == 0x5A;
}

static int uart_port_present(uint32_t serial_index) {
    char name[16];

    if (serial_index >= UART_PORT_COUNT) {
        return 0;
    }

    snprintf(name, sizeof(name), "serial%u", serial_index);
    if (isa_device_present(name)) {
        return 1;
    }

    return uart_probe_port(uart_ports[serial_index]);
}

static void uart_program_port(uint16_t port, int enable_rx_irq) {
    outb(port + 1, 0x00);    // Disable all interrupts
    outb(port + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(port + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(port + 1, 0x00);    //                  (hi byte)
    outb(port + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(port + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(port + 4, 0x0B);    // RTS/DSR set
    outb(port + 1, enable_rx_irq ? 0x01 : 0x00);
}

static uint16_t uart_node_port(const fs_node_t *node) {
    return (uint16_t)(node ? node->impl : 0);
}

static int uart_port_received(uint16_t port) {
    return inb(port + 5) & 1;
}

static int uart_port_is_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

static size_t uart_node_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)offset;
    uint16_t port = uart_node_port(node);
    size_t count = 0;

    while (count < size) {
        if (!uart_port_received(port)) {
            break;
        }
        buffer[count++] = inb(port + 0);
    }

    return count;
}

static size_t uart_node_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)offset;
    uint16_t port = uart_node_port(node);
    size_t count = 0;

    while (count < size) {
        uint32_t spins = 0;
        while (!uart_port_is_transmit_empty(port)) {
            if (++spins > 100000) {
                return count;
            }
        }
        outb(port + 0, buffer[count++]);
    }

    return count;
}

static int uart_node_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    uint16_t port = uart_node_port(node);
    if (!port)
        return -ENOTTY;

    switch (request) {
    case TIOCMGET: {
        /* Read modem control/status lines */
        uint8_t mcr = inb(port + 4);
        uint8_t msr = inb(port + 6);
        int bits = 0;
        if (mcr & UART_MCR_DTR) bits |= TIOCM_DTR;
        if (mcr & UART_MCR_RTS) bits |= TIOCM_RTS;
        if (msr & UART_MSR_CTS) bits |= TIOCM_CTS;
        if (msr & UART_MSR_DSR) bits |= TIOCM_DSR;
        if (msr & UART_MSR_DCD) bits |= TIOCM_CD;
        if (msr & UART_MSR_RI)  bits |= TIOCM_RI;
        if (arg)
            *(int *)arg = bits;
        return 0;
    }
    case TIOCMSET: {
        /* Set modem control lines */
        if (!arg) return -EINVAL;
        int bits = *(int *)arg;
        uint8_t mcr = inb(port + 4) & ~(UART_MCR_DTR | UART_MCR_RTS);
        if (bits & TIOCM_DTR) mcr |= UART_MCR_DTR;
        if (bits & TIOCM_RTS) mcr |= UART_MCR_RTS;
        outb(port + 4, mcr);
        return 0;
    }
    case TIOCMBIS: {
        /* Set indicated modem bits */
        if (!arg) return -EINVAL;
        int bits = *(int *)arg;
        uint8_t mcr = inb(port + 4);
        if (bits & TIOCM_DTR) mcr |= UART_MCR_DTR;
        if (bits & TIOCM_RTS) mcr |= UART_MCR_RTS;
        outb(port + 4, mcr);
        return 0;
    }
    case TIOCMBIC: {
        /* Clear indicated modem bits */
        if (!arg) return -EINVAL;
        int bits = *(int *)arg;
        uint8_t mcr = inb(port + 4);
        if (bits & TIOCM_DTR) mcr &= ~UART_MCR_DTR;
        if (bits & TIOCM_RTS) mcr &= ~UART_MCR_RTS;
        outb(port + 4, mcr);
        return 0;
    }
    case TIOCSBRK: {
        /* Start break */
        uint8_t lcr = inb(port + 3);
        outb(port + 3, lcr | 0x40);
        return 0;
    }
    case TIOCCBRK: {
        /* Clear break */
        uint8_t lcr = inb(port + 3);
        outb(port + 3, lcr & ~0x40);
        return 0;
    }
    case TIOCOUTQ: {
        /* Report TX bytes pending */
        if (arg)
            *(int *)arg = (int)uart_tx.count;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int uart_node_poll(fs_node_t *node, void *waiter) {
    (void)waiter;
    uint16_t port = uart_node_port(node);
    int events = POLLOUT | POLLWRNORM;

    if (uart_port_received(port)) {
        events |= POLLIN | POLLRDNORM;
    }

    return events;
}

static void uart_node_open(fs_node_t *node) {
    (void)node;
}

static void uart_node_close(fs_node_t *node) {
    (void)node;
}

int uart_select_port(uint32_t serial_index) {
    if (serial_index >= UART_PORT_COUNT) {
        return -1;
    }
    uart_base_port = uart_ports[serial_index];
    uart_base_index = serial_index;
    return 0;
}

static void uart_console_write(const char *data, size_t len) {
    uart_write(data, len);
}

static void uart_set_termios(struct termios *t) {
    uint32_t baud = 9600; // Default
    if (!t) return;

    // Calculate divisor
    switch (t->c_ospeed) {
        case B50: baud = 50; break;
        case B75: baud = 75; break;
        case B110: baud = 110; break;
        case B134: baud = 134; break;
        case B150: baud = 150; break;
        case B200: baud = 200; break;
        case B300: baud = 300; break;
        case B600: baud = 600; break;
        case B1200: baud = 1200; break;
        case B1800: baud = 1800; break;
        case B2400: baud = 2400; break;
        case B4800: baud = 4800; break;
        case B9600: baud = 9600; break;
        case B19200: baud = 19200; break;
        case B38400: baud = 38400; break;
        case B57600: baud = 57600; break;
        case B115200: baud = 115200; break;
        default: baud = 9600; break;
    }
    
    uint16_t divisor = 115200 / baud;

    // Line Control Register
    uint8_t lcr = 0;

    // Word Length
    if ((t->c_cflag & CSIZE) == CS8) lcr |= 0x03;
    else if ((t->c_cflag & CSIZE) == CS7) lcr |= 0x02;
    else if ((t->c_cflag & CSIZE) == CS6) lcr |= 0x01;
    else lcr |= 0x00; // CS5

    // Stop Bits
    if (t->c_cflag & CSTOPB) lcr |= 0x04; // 2 stop bits

    // Parity
    if (t->c_cflag & PARENB) {
        lcr |= 0x08; // Enable Parity
        if (!(t->c_cflag & PARODD)) lcr |= 0x10; // Even Parity
    }

    // Apply (DLAB sequence)
    // Note: Interrupts should be masked preferably
    outb(uart_base_port + 3, lcr | 0x80); // Enable DLAB
    outb(uart_base_port + 0, divisor & 0xFF);
    outb(uart_base_port + 1, (divisor >> 8) & 0xFF);
    outb(uart_base_port + 3, lcr); // Disable DLAB, set params

    // Modem Control (AFE/Flow Control)
    uint8_t mcr = inb(uart_base_port + 4); 
    if (t->c_cflag & CRTSCTS) {
        mcr |= 0x20; // Auto Flow Control (AFE) on 16550A+
    } else {
        mcr &= ~0x20;
    }
    outb(uart_base_port + 4, mcr);
}

static console_backend_t uart_console = {
    .name = "uart",
    .write = uart_console_write,
    .putchar = uart_putc,
    .clear = NULL,
    .next = NULL,
    .set_termios = uart_set_termios
};

console_backend_t *uart_get_console(void) {
    return &uart_console;
}

void uart_devfs_init(void) {
    if (uart_nodes_registered) return;

    for (uint32_t i = 0; i < UART_PORT_COUNT; i++) {
        fs_node_t *node = &uart_nodes[i];
        uint16_t port = uart_ports[i];

        if (!uart_port_present(i)) {
            continue;
        }

        uart_program_port(port, 0);

        memset(node, 0, sizeof(*node));
        snprintf(node->name, sizeof(node->name), "comm/serial%u", i);
        node->flags = FS_CHARDEVICE;
        node->mask = 0660;
        node->uid = 0;
        node->gid = 0;
        node->rdev = (4 << 8) | i;
        node->impl = (uintptr_t)port;
        node->open = uart_node_open;
        node->close = uart_node_close;
        node->read = uart_node_read;
        node->write = uart_node_write;
        node->ioctl = uart_node_ioctl;
        node->poll = uart_node_poll;

        devfs_register_device(node);
        kprintf("uart: /dev/%s registered (COM%u @ 0x%x)\n",
                node->name, i + 1, port);
    }

    uart_nodes_registered = 1;
}

int uart_init(void) {
    if (!uart_port_present(uart_base_index)) {
        return -1;
    }
    uart_tx.head = 0;
    uart_tx.tail = 0;
    uart_tx.count = 0;
    uart_tx.xoff_held = 0;
    uart_sysrq_pending = 0;
    uart_err_overrun = 0;
    uart_err_parity = 0;
    uart_err_framing = 0;
    uart_err_break = 0;
    /* Enable RDA + RLS interrupts (THRE enabled on demand) */
    uart_program_port(uart_base_port, 1);
    outb(uart_base_port + 1, UART_IER_RDA | UART_IER_RLS);
    return 0;
}

/*
 * uart_tx_drain - Transmit bytes from the TX ring buffer.
 * Called from THRE interrupt or when XOFF is released.
 */
static void uart_tx_drain(void) {
    while (uart_tx.count > 0 && !uart_tx.xoff_held) {
        if (!(inb(uart_base_port + 5) & UART_LSR_THRE))
            break;
        outb(uart_base_port + 0, uart_tx.buf[uart_tx.tail]);
        uart_tx.tail = (uart_tx.tail + 1) % UART_TX_BUF_SIZE;
        uart_tx.count--;
    }
    /* Disable THRE interrupt if buffer is empty */
    if (uart_tx.count == 0) {
        uint8_t ier = inb(uart_base_port + 1);
        outb(uart_base_port + 1, ier & ~UART_IER_THRE);
    }
}

/*
 * uart_tx_enqueue - Buffer a byte for interrupt-driven transmission.
 * Falls back to polling if the buffer is full.
 */
static void uart_tx_enqueue(uint8_t byte) {
    if (uart_tx.count >= UART_TX_BUF_SIZE) {
        /* Buffer full — poll-wait and send directly */
        while (!(inb(uart_base_port + 5) & UART_LSR_THRE))
            ;
        outb(uart_base_port + 0, byte);
        return;
    }
    uart_tx.buf[uart_tx.head] = byte;
    uart_tx.head = (uart_tx.head + 1) % UART_TX_BUF_SIZE;
    uart_tx.count++;
    /* Enable THRE interrupt so the buffer gets drained */
    uint8_t ier = inb(uart_base_port + 1);
    if (!(ier & UART_IER_THRE))
        outb(uart_base_port + 1, ier | UART_IER_THRE);
}

/*
 * uart_send_break - Transmit a break signal (~200ms).
 */
void uart_send_break(void) {
    uint8_t lcr = inb(uart_base_port + 3);
    outb(uart_base_port + 3, lcr | 0x40);  /* Set Break Enable (bit 6) */
    /* Busy-wait ~200ms (at 100MHz+ this is conservative) */
    for (volatile int i = 0; i < 2000000; i++)
        ;
    outb(uart_base_port + 3, lcr);         /* Clear Break Enable */
}

void uart_handler(registers_t *regs) {
    (void)regs;
    
    uint8_t iir;
    while (1) {
        iir = inb(uart_base_port + 2);
        if (iir & 1) {
            break; /* No more pending interrupts */
        }

        uint8_t type = (iir >> 1) & 7;

        if (type == 2 || type == 6) {
            /* Received Data Available / Character Timeout */
            while (inb(uart_base_port + 5) & UART_LSR_DR) {
                char c = inb(uart_base_port + 0);

                /* SysRq over serial: break sets flag, next char = command */
                if (uart_sysrq_pending) {
                    uart_sysrq_pending = 0;
                    sysrq_handle(c);
                    continue;
                }

                /* XON/XOFF flow control for TX */
                if (c == 0x13) {        /* XOFF (Ctrl+S) */
                    uart_tx.xoff_held = 1;
                    continue;
                } else if (c == 0x11) { /* XON (Ctrl+Q) */
                    uart_tx.xoff_held = 0;
                    /* Kick TX if buffered data waiting */
                    uart_tx_drain();
                    continue;
                }

                console_push_char(c);
            }
        } else if (type == 1) {
            /* Transmitter Holding Register Empty — drain TX buffer */
            uart_tx_drain();
        } else if (type == 3) {
            /* Receiver Line Status — error conditions */
            uint8_t lsr = inb(uart_base_port + 5);

            if (lsr & UART_LSR_BI) {
                uart_err_break++;
                /* Break condition: set SysRq pending flag */
                uart_sysrq_pending = 1;
                /* Consume the NUL byte that accompanies break */
                if (lsr & UART_LSR_DR)
                    (void)inb(uart_base_port + 0);
            }
            if (lsr & UART_LSR_FE) {
                uart_err_framing++;
                /* Discard the bad byte */
                if (lsr & UART_LSR_DR)
                    (void)inb(uart_base_port + 0);
            }
            if (lsr & UART_LSR_PE) {
                uart_err_parity++;
                /* Discard the bad byte */
                if (lsr & UART_LSR_DR)
                    (void)inb(uart_base_port + 0);
            }
            if (lsr & UART_LSR_OE) {
                uart_err_overrun++;
                /* Data already lost; just note it */
            }
        } else if (type == 0) {
            /* Modem Status — clear by reading MSR */
            (void)inb(uart_base_port + 6);
        }
    }
}

int uart_received(void) {
    return uart_port_received(uart_base_port);
}

char uart_getc(void) {
    uint32_t spins = 0;
    while (uart_received() == 0) {
        spins++;
        if (spins > 100000) return 0; // Avoid hang
    }
    return inb(uart_base_port + 0);
}

int uart_is_transmit_empty(void) {
    return uart_port_is_transmit_empty(uart_base_port);
}

void uart_putc(char c) {
    uart_tx_enqueue((uint8_t)c);
}

void uart_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        uart_putc(data[i]);
    }
}

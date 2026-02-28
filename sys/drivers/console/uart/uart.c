#include <kern/console.h>
#include <drivers/console/uart/uart.h>
#include <arch/x86-common/io.h>
#include <arch/i386/idt.h>
#include <sys/termios.h>

int uart_received(void);

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
    outb(UART_COM1 + 3, lcr | 0x80); // Enable DLAB
    outb(UART_COM1 + 0, divisor & 0xFF);
    outb(UART_COM1 + 1, (divisor >> 8) & 0xFF);
    outb(UART_COM1 + 3, lcr); // Disable DLAB, set params

    // Modem Control (AFE/Flow Control)
    uint8_t mcr = inb(UART_COM1 + 4); 
    if (t->c_cflag & CRTSCTS) {
        mcr |= 0x20; // Auto Flow Control (AFE) on 16550A+
    } else {
        mcr &= ~0x20;
    }
    outb(UART_COM1 + 4, mcr);
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

void uart_init(void) {
    outb(UART_COM1 + 1, 0x01);    // Enable Received Data Available Interrupt (0x1)
    outb(UART_COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(UART_COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(UART_COM1 + 1, 0x00);    //                  (hi byte)
    outb(UART_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(UART_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(UART_COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

void uart_handler(registers_t *regs) {
    (void)regs;
    // Check IIR (Interrupt Identity Register) to confirm it's RDA
    // but usually we just check LSR bit 0
    uart_received(); // To update status? 
    
    // Read while data available
    while (inb(UART_COM1 + 5) & 1) {
        char c = inb(UART_COM1 + 0);

        // Debug triggers for Serial Console
        if (c == 0x10) { // Ctrl+P - Process Dump
            extern void debug_dump_processes(void);
            debug_dump_processes();
        } else if (c == 0x09) { // Ctrl+I - Info/State (Alternative)
             // ...
             // Pass through for now
             extern void console_push_char(char c);
             console_push_char(c);
        } else {
            // Push to TTY
            extern void console_push_char(char c);
            console_push_char(c);
        }
    }
}

int uart_received(void) {
    return inb(UART_COM1 + 5) & 1;
}

char uart_getc(void) {
    while (uart_received() == 0);
    return inb(UART_COM1 + 0);
}

int uart_is_transmit_empty(void) {
    return inb(UART_COM1 + 5) & 0x20;
}

void uart_putc(char c) {
    while (uart_is_transmit_empty() == 0);
    outb(UART_COM1 + 0, c);
}

void uart_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++)
        uart_putc(data[i]);
}

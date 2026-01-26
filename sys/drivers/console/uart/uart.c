#include <kern/console.h>
#include <drivers/console/uart/uart.h>
#include <arch/x86-common/include/io.h>
#include <arch/i386/idt.h>

int uart_received(void);

static void uart_console_write(const char *data, size_t len) {
    uart_write(data, len);
}

static console_backend_t uart_console = {
    .name = "uart",
    .write = uart_console_write,
    .putchar = uart_putc,
    .clear = NULL,
    .next = NULL
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

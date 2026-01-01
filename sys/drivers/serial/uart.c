#include "uart.h"
#include "../../arch/i386/io.h"

void uart_init(void) {
    outb(UART_COM1 + 1, 0x00);    // Disable all interrupts
    outb(UART_COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(UART_COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(UART_COM1 + 1, 0x00);    //                  (hi byte)
    outb(UART_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(UART_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(UART_COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
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

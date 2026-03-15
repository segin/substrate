#ifndef _UART_H
#define _UART_H

#include <stdint.h>
#include <stddef.h>

#define UART_COM1 0x3F8
#define UART_COM2 0x2F8
#define UART_COM3 0x3E8
#define UART_COM4 0x2E8

int uart_init(void);
void uart_devfs_init(void);
int uart_select_port(uint32_t serial_index);
struct console_backend; // Forward declaration
struct console_backend *uart_get_console(void);
void uart_putc(char c);
void uart_write(const char* data, size_t size);
struct registers;
void uart_handler(struct registers *regs);
char uart_getc(void);

#endif

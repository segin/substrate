#ifndef _UART_H
#define _UART_H

#include <stdint.h>
#include <stddef.h>

#define UART_COM1 0x3F8

void uart_init(void);
struct console_backend; // Forward declaration
struct console_backend *uart_get_console(void);
void uart_putc(char c);
void uart_write(const char* data, size_t size);
char uart_getc(void);

#endif

/*
 * cons.c - x86_64 early console: COM1 at 115200 8N1, polled
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/x86-common/io.h>
#include <arch/x86_64/cons.h>

#define COM1            0x3F8
#define UART_THR        0       /* transmit holding */
#define UART_IER        1
#define UART_DLL        0       /* divisor latch, with DLAB */
#define UART_DLH        1
#define UART_FCR        2
#define UART_LCR        3
#define UART_MCR        4
#define UART_LSR        5
#define LSR_THRE        0x20    /* transmit holding register empty */
#define LCR_DLAB        0x80
#define LCR_8N1         0x03

void econs_init(void)
{
	outb(COM1 + UART_IER, 0x00);
	outb(COM1 + UART_LCR, LCR_DLAB);
	outb(COM1 + UART_DLL, 1);           /* 115200 baud */
	outb(COM1 + UART_DLH, 0);
	outb(COM1 + UART_LCR, LCR_8N1);
	outb(COM1 + UART_FCR, 0xC7);        /* FIFO on, cleared, 14-byte trigger */
	outb(COM1 + UART_MCR, 0x03);        /* DTR, RTS */
}

void econs_putc(char c)
{
	unsigned spins;

	if (c == '\n') {
		econs_putc('\r');
	}
	/* Bounded: a missing UART must not hang the boot. */
	for (spins = 0; spins < 100000; spins++) {
		if (inb(COM1 + UART_LSR) & LSR_THRE) {
			break;
		}
	}
	outb(COM1 + UART_THR, (uint8_t)c);
}

void econs_puts(const char *s)
{
	while (*s) {
		econs_putc(*s++);
	}
}

static int put_num(uint64_t v, unsigned base, int upper, int width, char pad)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char buf[24];
	int n = 0, out = 0;

	do {
		buf[n++] = digits[v % base];
		v /= base;
	} while (v != 0 && n < (int)sizeof(buf));
	for (; width > n; width--, out++) {
		econs_putc(pad);
	}
	while (n > 0) {
		econs_putc(buf[--n]);
		out++;
	}
	return out;
}

int econs_vprintf(const char *fmt, va_list ap)
{
	int out = 0;

	for (; *fmt; fmt++) {
		int width = 0, lng = 0;
		char pad = ' ';
		uint64_t u;
		int64_t s;

		if (*fmt != '%') {
			econs_putc(*fmt);
			out++;
			continue;
		}
		fmt++;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'l' || *fmt == 'h') {
			if (*fmt == 'l') {
				lng++;
			}
			fmt++;
		}
		switch (*fmt) {
		case 'c':
			econs_putc((char)va_arg(ap, int));
			out++;
			break;
		case 's': {
			const char *str = va_arg(ap, const char *);

			if (str == NULL) {
				str = "(null)";
			}
			while (*str) {
				econs_putc(*str++);
				out++;
			}
			break;
		}
		case 'd':
		case 'i':
			s = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
			if (s < 0) {
				econs_putc('-');
				out++;
				u = (uint64_t)-s;
			} else {
				u = (uint64_t)s;
			}
			out += put_num(u, 10, 0, width, pad);
			break;
		case 'u':
			u = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
			out += put_num(u, 10, 0, width, pad);
			break;
		case 'x':
		case 'X':
			u = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
			out += put_num(u, 16, *fmt == 'X', width, pad);
			break;
		case 'p':
			econs_puts("0x");
			out += 2 + put_num((uint64_t)(uintptr_t)va_arg(ap, void *),
			                   16, 0, 16, '0');
			break;
		case '%':
			econs_putc('%');
			out++;
			break;
		default:
			econs_putc('%');
			econs_putc(*fmt);
			out += 2;
			break;
		}
	}
	return out;
}

int econs_printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = econs_vprintf(fmt, ap);
	va_end(ap);
	return n;
}

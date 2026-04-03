#ifndef _IO_H
#define _IO_H

#include <stdint.h>

uint8_t mock_inb(uint16_t port);
uint16_t mock_inw(uint16_t port);
uint32_t mock_inl(uint16_t port);
void mock_outb(uint16_t port, uint8_t value);
void mock_outw(uint16_t port, uint16_t value);
void mock_outl(uint16_t port, uint32_t value);
void mock_insw(uint16_t port, void *addr, uint32_t count);
void mock_outsw(uint16_t port, const void *addr, uint32_t count);

#define inb(port) mock_inb(port)
#define inw(port) mock_inw(port)
#define inl(port) mock_inl(port)
#define outb(port, value) mock_outb((port), (value))
#define outw(port, value) mock_outw((port), (value))
#define outl(port, value) mock_outl((port), (value))
#define insw(port, addr, count) mock_insw((port), (addr), (count))
#define outsw(port, addr, count) mock_outsw((port), (addr), (count))

static inline void io_wait(void) {
    mock_outb(0x80, 0);
}

#endif

#ifndef _IO_H
#define _IO_H
#include <stdint.h>
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);
#endif

#ifndef _ARCH_I386_LAPIC_H
#define _ARCH_I386_LAPIC_H

#include <stdint.h>

// LAPIC Registers (offsets)
#define LAPIC_ID            0x0020
#define LAPIC_VER           0x0030
#define LAPIC_TPR           0x0080
#define LAPIC_EOI           0x00B0
#define LAPIC_SVR           0x00F0
#define LAPIC_ESR           0x0280
#define LAPIC_ICRLO         0x0300
#define LAPIC_ICRHI         0x0310
#define LAPIC_TIMER         0x0320
#define LAPIC_PCINT         0x0340
#define LAPIC_LINT0         0x0350
#define LAPIC_LINT1         0x0360
#define LAPIC_ERROR         0x0370
#define LAPIC_TICF          0x0380
#define LAPIC_TCCF          0x0390
#define LAPIC_TDCR          0x03E0

#define LAPIC_SVR_ENABLE    0x0100

void lapic_init(void);
void lapic_send_eoi(void);
uint32_t lapic_get_id(void);

#endif

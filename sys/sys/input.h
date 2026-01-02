#ifndef _SYS_INPUT_H
#define _SYS_INPUT_H

#include <stdint.h>

#define EV_KEY 1
#define EV_REL 2
#define EV_ABS 3

typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_t;

void input_enqueue(uint16_t type, uint16_t code, int32_t value);
void input_init(void);

#endif

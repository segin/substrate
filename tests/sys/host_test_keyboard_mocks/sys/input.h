#ifndef _SYS_INPUT_H
#define _SYS_INPUT_H
#include <stdint.h>
#define EV_KEY 0x01
typedef struct input_dev { char name[64]; uint32_t caps; } input_dev_t;
int input_register_device(input_dev_t *dev);
void input_report_key(input_dev_t *dev, uint16_t code, int32_t value);
void input_sync(input_dev_t *dev);
#endif

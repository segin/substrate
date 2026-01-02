#ifndef _PS2_H
#define _PS2_H

#include <stdint.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_CMD_DISABLE_P1 0xAD
#define PS2_CMD_DISABLE_P2 0xA7
#define PS2_CMD_ENABLE_P1  0xAE
#define PS2_CMD_ENABLE_P2  0xA8
#define PS2_CMD_READ_CONFIG 0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_SELF_TEST  0xAA
#define PS2_CMD_P1_TEST    0xAB

void ps2_init(void);
uint8_t ps2_read_data(void);
void ps2_write_command(uint8_t cmd);
void ps2_write_data(uint8_t data);
void ps2_write_aux(uint8_t data);

#endif

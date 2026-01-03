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
#define PS2_CMD_P2_TEST    0xA9

#define PS2_STATUS_OUTPUT_BUFFER_FULL 1
#define PS2_STATUS_INPUT_BUFFER_FULL  2
#define PS2_STATUS_SYSTEM             4
#define PS2_STATUS_CMD_DATA           8
#define PS2_STATUS_KEY_LOCK           16
#define PS2_STATUS_TIMEOUT            64
#define PS2_STATUS_PARITY             128

// Mouse Commands
#define PS2_MOUSE_RESET       0xFF
#define PS2_MOUSE_SET_DEFAULTS 0xF6
#define PS2_MOUSE_ENABLE_DATA 0xF4

void ps2_init(void);
uint8_t ps2_read_data(void);
void ps2_write_command(uint8_t cmd);
void ps2_write_data(uint8_t data);
void ps2_write_aux(uint8_t data);
uint8_t ps2_read_data_timeout(uint32_t timeout); // New helper

#endif

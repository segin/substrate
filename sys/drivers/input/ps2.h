#ifndef _PS2_H
#define _PS2_H

#include <stdint.h>

/* I/O Ports */
#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

/* Status Register Bits */
#define PS2_STATUS_OUTPUT_BUFFER_FULL (1 << 0)
#define PS2_STATUS_INPUT_BUFFER_FULL  (1 << 1)
#define PS2_STATUS_SYSTEM             (1 << 2)
#define PS2_STATUS_CMD_DATA           (1 << 3) /* 0=Data, 1=Command */
#define PS2_STATUS_KEY_LOCK           (1 << 4)
#define PS2_STATUS_TIMEOUT            (1 << 6)
#define PS2_STATUS_PARITY             (1 << 7)

/* Configuration Byte Bits */
#define PS2_CFG_FIRST_PORT_INT        (1 << 0)
#define PS2_CFG_SECOND_PORT_INT       (1 << 1)
#define PS2_CFG_SYSTEM_FLAG           (1 << 2)
#define PS2_CFG_FIRST_PORT_CLOCK      (1 << 4) /* 1 = Disabled */
#define PS2_CFG_SECOND_PORT_CLOCK     (1 << 5) /* 1 = Disabled */
#define PS2_CFG_TRANSLATION           (1 << 6)

/* Commands */
#define PS2_CMD_READ_CONFIG           0x20
#define PS2_CMD_WRITE_CONFIG          0x60
#define PS2_CMD_DISABLE_P2            0xA7
#define PS2_CMD_ENABLE_P2             0xA8
#define PS2_CMD_TEST_P2               0xA9
#define PS2_CMD_SELF_TEST             0xAA
#define PS2_CMD_TEST_P1               0xAB
#define PS2_CMD_DUMP_RAM              0xAC
#define PS2_CMD_DISABLE_P1            0xAD
#define PS2_CMD_ENABLE_P1             0xAE
#define PS2_CMD_READ_INPUT_PORT       0xC0
#define PS2_CMD_READ_OUTPUT_PORT      0xD0
#define PS2_CMD_WRITE_OUTPUT_PORT     0xD1
#define PS2_CMD_WRITE_P1_OUTPUT       0xD2 // Write next byte to Port 1 output buffer (acting like data from device)
#define PS2_CMD_WRITE_P2_OUTPUT       0xD3 // Write next byte to Port 2 output buffer
#define PS2_CMD_WRITE_P2_INPUT        0xD4 // Write next byte to Port 2 input (send to aux device)

/* Return Codes */
#define PS2_TEST_PASSED               0x55
#define PS2_TEST_FAILED               0xFC
#define PS2_PORT_TEST_PASSED          0x00

/* Device Commands */
#define PS2_DEV_RESET                 0xFF
#define PS2_DEV_RESEND                0xFE
#define PS2_DEV_ACK                   0xFA
#define PS2_DEV_ECHO                  0xEE
#define PS2_DEV_SCAN_ON               0xF4
#define PS2_DEV_SCAN_OFF              0xF5
#define PS2_DEV_IDENTIFY              0xF2

// Mouse Specific
#define PS2_MOUSE_SET_DEFAULTS        0xF6
#define PS2_MOUSE_ENABLE_DATA         0xF4

#define PS2_TIMEOUT_LOOPS             100000U
#define PS2_MOUSE_TIMEOUT_LOOPS       500000U

/* Public API */
int ps2_init(void);
int ps2_write_command(uint8_t cmd);
int ps2_write_data(uint8_t data);
int ps2_write_aux(uint8_t data);
uint8_t ps2_read_data(void);
int ps2_read_data_timeout(uint8_t *data, uint32_t loop_count); /* Returns 0 on success, -1 on timeout */
int ps2_wait_write(void);
int ps2_wait_read(void);

#endif

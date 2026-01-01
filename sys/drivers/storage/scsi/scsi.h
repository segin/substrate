#ifndef _SCSI_H
#define _SCSI_H

#include <stdint.h>

// SCSI Opcodes
#define SCSI_CMD_TEST_UNIT_READY  0x00
#define SCSI_CMD_REQUEST_SENSE    0x03
#define SCSI_CMD_INQUIRY          0x12
#define SCSI_CMD_READ_10          0x28
#define SCSI_CMD_WRITE_10         0x2A
#define SCSI_CMD_READ_CAPACITY_10 0x25

// SCSI Status
#define SCSI_STATUS_GOOD          0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02

// Command Descriptor Block (CDB) Generic
typedef struct {
    uint8_t opcode;
    uint8_t flags;
    uint32_t lba; // Big Endian in actual packet usually, simplified here
    uint8_t group;
    uint16_t length;
    uint8_t control;
} __attribute__((packed)) scsi_cdb_10_t;

void scsi_init(void);

#endif

#ifndef _CRC16_H
#define _CRC16_H

#include <stdint.h>
#include <stddef.h>

/* CRC-16 (IBM/ANSI, polynomial 0x8005, bit-reflected implementation
 * 0xA001) — the variant Linux's lib/crc16.c implements and ext4's
 * uninit_bg/GDT_CSUM group-descriptor checksum uses.  Seed the first
 * call with 0xFFFF (~0), chain the return value through subsequent
 * calls. */
uint16_t crc16_update(uint16_t crc, const void *data, size_t len);

#endif

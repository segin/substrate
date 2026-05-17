/*
 * crc32c.h — CRC-32C (Castagnoli polynomial 0x1EDC6F41) primitive.
 *
 * Used by ext4 metadata_csum, btrfs, iSCSI digests.  ext4 stores
 * truncated csum values: superblock uses the full 32-bit, inode
 * uses lo16/hi16 split, group descriptor and extent tail use 32-bit.
 *
 * Implementation is a software table lookup — substrate's i486
 * target predates SSE4.2's CRC32 instruction, so we don't bother
 * autodetecting it.  ~50ns/byte on a 1 GHz Pentium-class CPU,
 * which is fast enough for mount-time verify of every group
 * descriptor + every inode read.
 */
#ifndef _SYS_CRC32C_H
#define _SYS_CRC32C_H

#include <stdint.h>
#include <stddef.h>

/* Continue an in-progress checksum.  Pass 0xFFFFFFFF as the seed
 * for a fresh checksum, then XOR the result with 0xFFFFFFFF at
 * the end if you want the conventional "final XOR" form.  ext4
 * notably does NOT XOR-out the final value — match it.  */
uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len);

/* One-shot: feeds 0xFFFFFFFF in, no final XOR — ext4 convention.  */
static inline uint32_t crc32c(const void *buf, size_t len) {
    return crc32c_update(0xFFFFFFFFu, buf, len);
}

#endif

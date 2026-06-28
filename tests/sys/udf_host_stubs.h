/*
 * udf_host_stubs.h — stubs for the UDF write-side helpers (udf_write.c)
 * that the read-side host tests reference through udf.c but don't compile.
 *
 * These used to be copy-pasted into every UDF host test, where they bit-
 * rotted independently when the driver's signatures changed (they took a
 * fs_node_t* and a separate partition_start; they now take a struct udf_fs*
 * context).  Keeping them here makes the stubs track udf.h in one place.
 *
 * Include AFTER <fs/udf/udf.h> (for struct udf_fs / udf_fe / udf_long_ad).
 */
#ifndef UDF_HOST_STUBS_H
#define UDF_HOST_STUBS_H

#include <drivers/storage/blkdev.h>

/* udf_read_label() reads the raw device via the block layer; the read-side
 * descriptor tests don't exercise it, so a zero-returning stub satisfies
 * the link. */
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size,
                         void *buffer) { return 0; }

int udf_read_space_bitmap(struct udf_fs *fs, uint32_t bitmap_loc,
                          uint32_t bitmap_len) { return 0; }
uint32_t udf_alloc_block(struct udf_fs *fs) { return 0; }
void udf_free_block(struct udf_fs *fs, uint32_t block) { }
int udf_create_fe(struct udf_fs *fs, uint32_t block, uint8_t file_type,
                  uint32_t uid, uint32_t gid, uint32_t permissions) { return 0; }
int udf_add_fid(struct udf_fs *fs, struct udf_fe *dir_fe, uint32_t dir_block,
                const char *name, struct udf_long_ad *icb,
                uint8_t characteristics) { return 0; }

#endif /* UDF_HOST_STUBS_H */

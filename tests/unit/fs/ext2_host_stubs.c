/*
 * Shared stubs for the standalone ext2 host tests.
 *
 * Each of unit/fs/*ext2*_host.c builds sys/fs/ext2/ext2.c natively and links
 * it alone -- no mocks.o, no kernel.  ext2.c has since grown references to
 * the block layer, the kernel log, the command line, the checksum helpers
 * and the xattr/htree entry points, none of which those tests supply, so
 * every one of them failed to link.
 *
 * Kept in one file rather than copied into each test: there are eight of
 * them, and the last thing this tree needs is eight more stub sets drifting
 * out of sync with the headers they stand in for.  Signatures are taken from
 * the declaring headers -- keep them that way.
 *
 * kprintf is deliberately absent: bench_ext2_readdir_host.c defines its own,
 * and a second definition here would collide.  Tests that want to see ext2's
 * chatter define it; the rest get the weak no-op below.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../../sys/vfs/vfs.h"
#include "../../../sys/drivers/storage/blkdev.h"

/* Weak so a test that wants ext2's diagnostics can override it. */
__attribute__((weak)) int kprintf(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

/* sys/drivers/storage/blkdev.h */
void blkdev_invalidate_node(fs_node_t *node) { (void)node; }
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer)
{
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

/* sys/kern/cmdline.h */
int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }

/* sys/include/crc16.h, sys/include/crc32c.h */
uint16_t crc16_update(uint16_t crc, const void *data, size_t len)
{
    (void)data; (void)len;
    return crc;
}
uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len)
{
    (void)buf; (void)len;
    return crc;
}

/* ext2's own optional features, not under test in the perf/rename/readdir
 * harnesses; sys/fs/ext2/ext2.h declares all three. */
int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major, uint32_t *hash_minor)
{
    (void)name; (void)len; (void)hash_seed; (void)hash_version;
    if (hash_major) *hash_major = 0;
    if (hash_minor) *hash_minor = 0;
    return -1;
}
int ext2_xattr_get(fs_node_t *node, const char *full_name, void *out,
                   size_t out_size, size_t *result_size)
{
    (void)node; (void)full_name; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return -1;
}
int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size, size_t *result_size)
{
    (void)node; (void)out; (void)out_size;
    if (result_size) *result_size = 0;
    return -1;
}

/* Globals ext2.c touches for accounting and for permission checks. */
struct process *current_process;
unsigned long fs_open_count;
unsigned long fs_close_count;

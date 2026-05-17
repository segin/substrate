/*
 * ext2_xattr.c — extended-attribute read path.
 *
 * Substrate's ext2/4 driver speaks read-only xattr today (set / remove
 * land as ENOSYS up the stack).  Two storage locations are supported:
 *
 *   In-inode (EXT4_FEATURE_COMPAT_EXT_ATTR + i_extra_isize+4 bytes
 *   after the 128-byte legacy area carry magic 0xEA020000 followed by
 *   entries).  Substrate's read_inode loads 148 bytes of inode struct;
 *   to access the inline xattr area we re-read the raw inode bytes
 *   when needed.
 *
 *   Block-at-i_file_acl (one filesystem block whose first 32 bytes
 *   are struct ext2fs_extattr_header { magic, refcount, blocks, hash,
 *   checksum, reserved[3] }, followed by entries).
 *
 * Entries:
 *   struct ext2fs_extattr_entry {
 *       u8  e_name_len;
 *       u8  e_name_index;       // 1=user, 2=ACL, 4=trusted, 6=security, 7=system
 *       u16 e_value_offs;       // byte offset of value within the block
 *       u32 e_value_block;      // 0 (legacy "value in same block")
 *       u32 e_value_size;
 *       u32 e_hash;
 *       char e_name[];          // not NUL-terminated
 *   };
 * 4-byte aligned via EXT2_EXTATTR_LEN().  Last entry is signalled by
 * the first 4 bytes of the would-be-next-entry being zero.
 *
 * User-facing names are namespaced: "user.foo", "security.selinux",
 * etc.  ext2_xattr_split_name() turns that into (index, suffix);
 * ext2_xattr_join_name() reconstructs.
 */
#include <stdint.h>
#include <string.h>
#include <ext2/ext2.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include <errno.h>

void *kmalloc(size_t);
void  kfree(void *, size_t);

/* Linux namespace indices.  */
#define EXT4_XATTR_INDEX_USER             1
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS 2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT 3
#define EXT4_XATTR_INDEX_TRUSTED          4
#define EXT4_XATTR_INDEX_SECURITY         6
#define EXT4_XATTR_INDEX_SYSTEM           7

#define EXT2_XATTR_MAGIC                  0xEA020000

/* Entry packing: 16-byte fixed header + variable name, padded to 4. */
#pragma pack(push, 1)
struct ext2_xattr_entry {
    uint8_t  e_name_len;
    uint8_t  e_name_index;
    uint16_t e_value_offs;
    uint32_t e_value_block;
    uint32_t e_value_size;
    uint32_t e_hash;
    char     e_name[];
};

struct ext2_xattr_block_header {
    uint32_t h_magic;
    uint32_t h_refcount;
    uint32_t h_blocks;
    uint32_t h_hash;
    uint32_t h_checksum;
    uint32_t h_reserved[3];
};
#pragma pack(pop)

#define EXT2_XATTR_PAD             4
#define EXT2_XATTR_ROUND           (EXT2_XATTR_PAD - 1)
#define EXT2_XATTR_LEN(name_len) \
    (((name_len) + EXT2_XATTR_ROUND + sizeof(struct ext2_xattr_entry)) \
        & ~EXT2_XATTR_ROUND)

/* Map ("user", "trusted", "security", ...) → namespace index, and
 * split off the suffix.  Returns 0 / index on success, -1 on
 * malformed.  *suffix points into the original buffer.  */
int ext2_xattr_split_name(const char *full, const char **suffix) {
    if (!full || !suffix) return -1;
    const char *dot = strchr(full, '.');
    if (!dot || dot == full || dot[1] == '\0') return -1;
    size_t prefix_len = dot - full;
    *suffix = dot + 1;
    if (prefix_len == 4 && memcmp(full, "user", 4) == 0)
        return EXT4_XATTR_INDEX_USER;
    if (prefix_len == 7 && memcmp(full, "trusted", 7) == 0)
        return EXT4_XATTR_INDEX_TRUSTED;
    if (prefix_len == 8 && memcmp(full, "security", 8) == 0)
        return EXT4_XATTR_INDEX_SECURITY;
    if (prefix_len == 6 && memcmp(full, "system", 6) == 0)
        return EXT4_XATTR_INDEX_SYSTEM;
    /* POSIX ACLs are stored under prefix "system.posix_acl_access"
     * / ".._default" but the index distinguishes them from generic
     * "system.*"; let the namespace check above swallow the prefix
     * and use ACL-namespace-aware suffix recognition.  */
    return -1;
}

static const char *ext2_xattr_index_prefix(uint8_t idx) {
    switch (idx) {
    case EXT4_XATTR_INDEX_USER:     return "user.";
    case EXT4_XATTR_INDEX_TRUSTED:  return "trusted.";
    case EXT4_XATTR_INDEX_SECURITY: return "security.";
    case EXT4_XATTR_INDEX_SYSTEM:   return "system.";
    case EXT4_XATTR_INDEX_POSIX_ACL_ACCESS:
        return "system.posix_acl_access:";
    case EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT:
        return "system.posix_acl_default:";
    }
    return NULL;
}

/* Walk a packed xattr region from `entries` to `region_end` looking
 * for (name_index, suffix).  On hit, writes the entry's value to
 * `out` (capped at out_size) and returns the actual value size in
 * *result_size.  If `out` is NULL or out_size < value size, only
 * fills *result_size (a libc-style "tell me how big to make the
 * buffer" query).
 *
 * Returns 0 on hit, -ENODATA on miss, -ERANGE if the value didn't
 * fit and a non-NULL `out` was provided.  `value_base` is the byte
 * pointer that e_value_offs is measured from (block start for both
 * inline and block-stored xattrs).  */
static int ext2_xattr_walk_get(const uint8_t *entries,
                               const uint8_t *region_end,
                               const uint8_t *value_base,
                               size_t value_max_off,
                               uint8_t want_index,
                               const char *want_name,
                               size_t want_name_len,
                               void *out, size_t out_size,
                               size_t *result_size) {
    const uint8_t *p = entries;
    while (p + sizeof(struct ext2_xattr_entry) <= region_end) {
        const struct ext2_xattr_entry *e =
            (const struct ext2_xattr_entry *)p;
        /* Sentinel: first 4 bytes all zero => end of list.  */
        uint32_t first4;
        memcpy(&first4, p, 4);
        if (first4 == 0) break;
        if (p + sizeof(*e) + e->e_name_len > region_end) return -EINVAL;
        if (e->e_name_index == want_index &&
            e->e_name_len   == want_name_len &&
            memcmp(e->e_name, want_name, want_name_len) == 0) {
            uint32_t vsize = e->e_value_size;
            uint32_t voff  = e->e_value_offs;
            if (e->e_value_block != 0) return -ENOTSUP;
            if ((size_t)voff + vsize > value_max_off) return -EINVAL;
            *result_size = vsize;
            if (out != NULL) {
                if (out_size < vsize) return -ERANGE;
                memcpy(out, value_base + voff, vsize);
            }
            return 0;
        }
        p += EXT2_XATTR_LEN(e->e_name_len);
    }
    return -ENODATA;
}

/* List variant — concatenate "<prefix><suffix>\0" tokens into out.
 * Returns total size needed (always), writes only what fits.  */
static int ext2_xattr_walk_list(const uint8_t *entries,
                                const uint8_t *region_end,
                                void *out, size_t out_size,
                                size_t *result_size) {
    const uint8_t *p = entries;
    size_t total = 0;
    char *o = (char *)out;
    while (p + sizeof(struct ext2_xattr_entry) <= region_end) {
        const struct ext2_xattr_entry *e =
            (const struct ext2_xattr_entry *)p;
        uint32_t first4;
        memcpy(&first4, p, 4);
        if (first4 == 0) break;
        if (p + sizeof(*e) + e->e_name_len > region_end) return -EINVAL;
        const char *prefix = ext2_xattr_index_prefix(e->e_name_index);
        if (prefix) {
            size_t plen = strlen(prefix);
            size_t name_total = plen + e->e_name_len + 1;
            if (out && total + name_total <= out_size) {
                memcpy(o + total, prefix, plen);
                memcpy(o + total + plen, e->e_name, e->e_name_len);
                o[total + plen + e->e_name_len] = '\0';
            }
            total += name_total;
        }
        p += EXT2_XATTR_LEN(e->e_name_len);
    }
    *result_size = total;
    if (out && total > out_size) return -ERANGE;
    return 0;
}

extern uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer);

/* Block-stored xattr fetch (i_file_acl != 0).  Allocates a scratch
 * block, reads it, validates the header, walks for (idx, suffix).  */
static int ext2_xattr_block_get(ext2_fs_t *fs, uint32_t block_no,
                                uint8_t idx, const char *suffix,
                                size_t suffix_len, void *out,
                                size_t out_size, size_t *result_size) {
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;
    if (ext2_read_block(fs, block_no, buf) != fs->block_size) {
        kfree(buf, fs->block_size);
        return -EIO;
    }
    struct ext2_xattr_block_header *h = (void *)buf;
    if (h->h_magic != EXT2_XATTR_MAGIC) {
        kfree(buf, fs->block_size);
        return -EINVAL;
    }
    int rc = ext2_xattr_walk_get(buf + sizeof(*h),
                                 buf + fs->block_size,
                                 buf, fs->block_size,
                                 idx, suffix, suffix_len,
                                 out, out_size, result_size);
    kfree(buf, fs->block_size);
    return rc;
}

static int ext2_xattr_block_list(ext2_fs_t *fs, uint32_t block_no,
                                 void *out, size_t out_size,
                                 size_t *result_size) {
    uint8_t *buf = kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;
    if (ext2_read_block(fs, block_no, buf) != fs->block_size) {
        kfree(buf, fs->block_size);
        return -EIO;
    }
    struct ext2_xattr_block_header *h = (void *)buf;
    if (h->h_magic != EXT2_XATTR_MAGIC) {
        kfree(buf, fs->block_size);
        return -EINVAL;
    }
    int rc = ext2_xattr_walk_list(buf + sizeof(*h),
                                  buf + fs->block_size,
                                  out, out_size, result_size);
    kfree(buf, fs->block_size);
    return rc;
}

/* Re-read the on-disk inode bytes 128..inode_size into `out` (caller
 * sized).  Substrate's read_inode normally caches only 148 bytes of
 * the typed struct, but xattr storage may extend further; we need
 * the raw bytes.  Returns the byte count actually fetched, or 0
 * on any read failure (caller treats that as "no inline xattr").  */
static uint32_t ext2_xattr_read_inline(ext2_fs_t *fs, uint32_t inode_num,
                                       uint8_t *out_buf, uint32_t out_size) {
    if (fs->inode_size <= EXT2_GOOD_OLD_INODE_SIZE) return 0;
    if (inode_num == 0) return 0;
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    if (group >= fs->group_count) return 0;
    uint32_t inode_table_block = fs->bgd[group].bg_inode_table;
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;

    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return 0;
    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf)
        != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return 0;
    }
    uint32_t avail = fs->inode_size;
    uint32_t copy  = (avail < out_size) ? avail : out_size;
    memcpy(out_buf, block_buf + inode_offset, copy);
    kfree(block_buf, fs->block_size);
    return copy;
}

/* Inline (in-inode) xattr lookup.  Memory layout on a 256-byte inode:
 *
 *   raw[0..127]  - legacy inode fields
 *   raw[128..129] = i_extra_isize  (how many post-128 bytes are
 *                                    defined extension fields)
 *   raw[128 + i_extra_isize ..] = optional inline xattr area:
 *       u32 magic 0xEA020000 followed by entries packed up to the
 *       end of the inode (raw[inode_size-1]).  Values are stored
 *       past the entry headers, growing downward from the end of
 *       the inode; e_value_offs is measured from the start of the
 *       inline xattr area (i.e. just past the magic).
 */
static int ext2_xattr_inline_get(ext2_fs_t *fs, uint32_t inum,
                                 uint8_t idx, const char *suffix,
                                 size_t suffix_len, void *out,
                                 size_t out_size, size_t *result_size) {
    if (fs->inode_size <= EXT2_GOOD_OLD_INODE_SIZE) return -ENODATA;
    uint8_t *raw = kmalloc(fs->inode_size);
    if (!raw) return -ENOMEM;
    uint32_t got = ext2_xattr_read_inline(fs, inum, raw, fs->inode_size);
    if (got != fs->inode_size) { kfree(raw, fs->inode_size); return -ENODATA; }
    uint16_t extra = *(uint16_t *)(raw + 128);
    if (extra < 4) { kfree(raw, fs->inode_size); return -ENODATA; }
    uint32_t xa_off = 128 + extra;
    if (xa_off + 4 > fs->inode_size) { kfree(raw, fs->inode_size); return -ENODATA; }
    if (*(uint32_t *)(raw + xa_off) != EXT2_XATTR_MAGIC) {
        kfree(raw, fs->inode_size);
        return -ENODATA;
    }
    int rc = ext2_xattr_walk_get(raw + xa_off + 4,
                                 raw + fs->inode_size,
                                 raw + xa_off + 4,
                                 fs->inode_size - xa_off - 4,
                                 idx, suffix, suffix_len,
                                 out, out_size, result_size);
    kfree(raw, fs->inode_size);
    return rc;
}

static int ext2_xattr_inline_list(ext2_fs_t *fs, uint32_t inum,
                                  void *out, size_t out_size,
                                  size_t *partial) {
    if (fs->inode_size <= EXT2_GOOD_OLD_INODE_SIZE) { *partial = 0; return 0; }
    uint8_t *raw = kmalloc(fs->inode_size);
    if (!raw) { *partial = 0; return -ENOMEM; }
    uint32_t got = ext2_xattr_read_inline(fs, inum, raw, fs->inode_size);
    if (got != fs->inode_size) { kfree(raw, fs->inode_size); *partial = 0; return 0; }
    uint16_t extra = *(uint16_t *)(raw + 128);
    if (extra < 4) { kfree(raw, fs->inode_size); *partial = 0; return 0; }
    uint32_t xa_off = 128 + extra;
    if (xa_off + 4 > fs->inode_size) { kfree(raw, fs->inode_size); *partial = 0; return 0; }
    if (*(uint32_t *)(raw + xa_off) != EXT2_XATTR_MAGIC) {
        kfree(raw, fs->inode_size); *partial = 0; return 0;
    }
    int rc = ext2_xattr_walk_list(raw + xa_off + 4,
                                  raw + fs->inode_size,
                                  out, out_size, partial);
    kfree(raw, fs->inode_size);
    return rc;
}

/* Public entry: combined inline-then-block lookup.  Called by the
 * syscall layer.  On hit, *result_size is the value byte count.
 * Inline xattrs are checked first — Linux/FreeBSD precedence.  */
int ext2_xattr_get(fs_node_t *node, const char *full_name,
                   void *out, size_t out_size, size_t *result_size) {
    if (!node || !full_name || !result_size) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    const char *suffix = NULL;
    int idx = ext2_xattr_split_name(full_name, &suffix);
    if (idx < 0) return -ENOTSUP;
    size_t slen = strlen(suffix);
    if (slen == 0 || slen > 255) return -ERANGE;

    int rc = ext2_xattr_inline_get(ctx->fs, ctx->inode_num,
                                   (uint8_t)idx, suffix, slen,
                                   out, out_size, result_size);
    if (rc != -ENODATA) return rc;
    if (ctx->inode.i_file_acl == 0) return -ENODATA;
    return ext2_xattr_block_get(ctx->fs, ctx->inode.i_file_acl,
                                (uint8_t)idx, suffix, slen,
                                out, out_size, result_size);
}

int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size,
                    size_t *result_size) {
    if (!node || !result_size) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;
    *result_size = 0;
    /* Inline first, then block, accumulating into the same buffer.
     * The walker writes "<prefix><suffix>\0" tokens; for the block
     * pass we adjust the output pointer + capacity by the inline
     * partial so the two regions concatenate without overlap.  */
    size_t inline_n = 0;
    int rc = ext2_xattr_inline_list(ctx->fs, ctx->inode_num,
                                    out, out_size, &inline_n);
    if (rc != 0 && rc != -ERANGE) return rc;
    *result_size = inline_n;
    if (ctx->inode.i_file_acl != 0) {
        size_t block_n = 0;
        void *block_out = NULL;
        size_t block_cap = 0;
        if (out && out_size > inline_n) {
            block_out = (char *)out + inline_n;
            block_cap = out_size - inline_n;
        }
        int br = ext2_xattr_block_list(ctx->fs, ctx->inode.i_file_acl,
                                       block_out, block_cap, &block_n);
        if (br != 0 && br != -ERANGE) return br;
        *result_size += block_n;
    }
    if (out && *result_size > out_size) return -ERANGE;
    return 0;
}

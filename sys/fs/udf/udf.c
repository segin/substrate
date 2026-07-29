/*
 * udf.c - Universal Disk Format (UDF) Filesystem Driver
 *
 * Read-write UDF implementation based on ECMA-167 and OSTA UDF 2.60.
 */

#include <string.h>

#include <drivers/storage/blkdev.h>
#include <fs/udf/udf.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <vm/vm_kmem.h>

/* UDF filesystem structure registration */
static filesystem_t udf_filesystem;

/* Forward declarations */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data);

/*
 * Calculate tag checksum (sum of bytes 0-3 and 5-15)
 */
uint8_t udf_tag_checksum(struct udf_tag *tag) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t *)tag;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    return sum;
}

/*
 * CRC-CCITT (0x1021 polynomial) for descriptor CRC
 */
static uint16_t udf_crc_table[256];
static int udf_crc_initialized = 0;

static void udf_crc_init(void) {
    if (udf_crc_initialized) return;
    for (int i = 0; i < 256; i++) {
        uint16_t crc = 0;
        uint16_t c = (uint16_t)i << 8;
        for (int j = 0; j < 8; j++) {
            if ((crc ^ c) & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
            c = c << 1;
        }
        udf_crc_table[i] = crc;
    }
    udf_crc_initialized = 1;
}

uint16_t udf_crc(const uint8_t *data, uint32_t len) {
    udf_crc_init();
    uint16_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc = udf_crc_table[((crc >> 8) ^ data[i]) & 0xFF] ^ (crc << 8);
    }
    return crc;
}

/*
 * Read and verify a descriptor tag
 * Returns 0 on success, -1 on failure
 */
int udf_read_tag(fs_node_t *dev, uint32_t sector, struct udf_tag *tag, 
                 void *buffer, uint32_t size) {
    if (!dev || !dev->read) return -1;
    
    /* Read the sector */
    off_t offset = (off_t)sector * UDF_SECTOR_SIZE;
    uint32_t read = dev->read(dev, offset, size, (uint8_t *)buffer);
    if (read != size) return -1;
    
    /* Copy tag from buffer */
    memcpy(tag, buffer, sizeof(struct udf_tag));
    
    /* Verify tag checksum */
    if (udf_tag_checksum(tag) != tag->tag_checksum) {
        kprint("UDF: Tag checksum mismatch\n");
        return -1;
    }
    
    /* Verify tag location */
    if (tag->tag_location != sector) {
        kprint("UDF: Tag location mismatch\n");
        return -1;
    }
    
    /* Verify CRC if length > 0.  tag->desc_crc_len is an untrusted
     * on-disk uint16 (up to 65535) but the CRC data lives in the
     * caller's `size`-byte buffer after the 16-byte tag.  A crafted
     * descriptor could claim a length far past the buffer, driving
     * udf_crc to read tens of KiB of out-of-bounds kernel memory.
     * Clamp to the bytes actually present in the buffer. */
    if (tag->desc_crc_len > 0 && size > sizeof(struct udf_tag)) {
        uint8_t *data = (uint8_t *)buffer + sizeof(struct udf_tag);
        uint32_t crc_len = tag->desc_crc_len;
        uint32_t avail = size - (uint32_t)sizeof(struct udf_tag);
        if (crc_len > avail) crc_len = avail;
        uint16_t crc = udf_crc(data, crc_len);
        if (crc != tag->desc_crc) {
            kprint("UDF: Descriptor CRC mismatch\n");
            return -1;
        }
    }
    
    return 0;
}

/*
 * Find the Anchor Volume Descriptor Pointer
 * Tries sector 256, then last sector, then last-256
 */
int udf_find_avdp(fs_node_t *dev, struct udf_avdp *avdp) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return -1;
    struct udf_tag tag;
    
    /* Try sector 256 first (most common) */
    if (udf_read_tag(dev, UDF_AVDP_SECTOR, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
        if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
            memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
            kfree(sector_buf, UDF_SECTOR_SIZE);
            return 0;
        }
    }
    
    /* Try last sector and last-256 for completeness */
    if (dev->length >= UDF_SECTOR_SIZE) {
        uint32_t last_sector = (uint32_t)((uint64_t)dev->length / UDF_SECTOR_SIZE) - 1;

        /* Try last sector */
        if (last_sector > UDF_AVDP_SECTOR) {
            if (udf_read_tag(dev, last_sector, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
                if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
                    memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
                    kfree(sector_buf, UDF_SECTOR_SIZE);
                    return 0;
                }
            }
        }

        /* Try last-256 sector */
        if (last_sector > UDF_AVDP_SECTOR + 256) {
            if (udf_read_tag(dev, last_sector - 256, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
                if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
                    memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
                    kfree(sector_buf, UDF_SECTOR_SIZE);
                    return 0;
                }
            }
        }
    }
    
    kfree(sector_buf, UDF_SECTOR_SIZE);
    kprint("UDF: AVDP not found\n");
    return -1;
}

/*
 * Read Volume Descriptor Sequence
 * Parses PVD, PD, and LVD from the VDS extent
 */
int udf_read_vds(fs_node_t *dev, struct udf_extent_ad *vds_extent,
                 struct udf_pvd *pvd, struct udf_pd *pd, struct udf_lvd *lvd) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return -1;
    struct udf_tag tag;
    
    uint32_t start = vds_extent->location;
    uint32_t count = vds_extent->length / UDF_SECTOR_SIZE;

    /* length comes from the AVDP with no bound: 0xFFFFFFFF asks for two
     * million device reads at mount time, with the mount lock held.  A VDS
     * is a handful of descriptors; udf_read_label() already caps this at 64
     * for exactly this reason. */
    if (count > UDF_VDS_MAX_SECTORS)
        count = UDF_VDS_MAX_SECTORS;
    if (start > 0xFFFFFFFFU - count)
        return -1;
    
    int found_pvd = 0, found_pd = 0, found_lvd = 0;
    
    for (uint32_t i = 0; i < count && !(found_pvd && found_pd && found_lvd); i++) {
        if (udf_read_tag(dev, start + i, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
            continue;  /* Skip invalid sectors */
        }
        
        switch (tag.tag_id) {
        case UDF_TAG_PRIMARY_VD:
            memcpy(pvd, sector_buf, sizeof(struct udf_pvd));
            found_pvd = 1;
            break;
        case UDF_TAG_PARTITION_D:
            memcpy(pd, sector_buf, sizeof(struct udf_pd));
            found_pd = 1;
            break;
        case UDF_TAG_LOGICAL_VD:
            memcpy(lvd, sector_buf, sizeof(struct udf_lvd));
            found_lvd = 1;
            break;
        case UDF_TAG_TERMINATING:
            i = count;  /* Stop at terminating descriptor */
            break;
        }
    }
    
    if (!found_pvd || !found_pd || !found_lvd) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        kprint("UDF: VDS incomplete\n");
        return -1;
    }
    
    kfree(sector_buf, UDF_SECTOR_SIZE);
    return 0;
}

/*
 * Read File Set Descriptor from the location in LVD
 */
int udf_read_fsd(fs_node_t *dev, struct udf_fs *fs, struct udf_lvd *lvd, 
                 struct udf_fsd *fsd) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return -1;
    struct udf_tag tag;
    
    /* FSD location is relative to partition start */
    uint32_t fsd_block = lvd->fsd_location.block;
    uint32_t fsd_sector = fs->partition_start + fsd_block;
    
    if (udf_read_tag(dev, fsd_sector, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        kprint("UDF: Failed to read FSD\n");
        return -1;
    }
    
    if (tag.tag_id != UDF_TAG_FSD) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        kprint("UDF: Invalid FSD tag\n");
        return -1;
    }
    
    memcpy(fsd, sector_buf, sizeof(struct udf_fsd));
    kfree(sector_buf, UDF_SECTOR_SIZE);
    return 0;
}

/*
 * Read File Entry (or Extended File Entry) from ICB location
 */
int udf_read_fe(struct udf_fs *fs, struct udf_long_ad *icb, struct udf_fe *fe) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return -1;
    struct udf_tag tag;
    
    uint32_t sector = fs->partition_start + icb->block;
    
    if (udf_read_tag(fs->device, sector, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return -1;
    }
    
    if (tag.tag_id != UDF_TAG_FE && tag.tag_id != UDF_TAG_EFE) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        kprint("UDF: Invalid FE/EFE tag\n");
        return -1;
    }
    
    memcpy(fe, sector_buf, sizeof(struct udf_fe));
    kfree(sector_buf, UDF_SECTOR_SIZE);
    return 0;
}

/*
 * Read full File Entry sector into caller-provided buffer
 * Returns 0 on success, -1 on failure
 */
static int udf_read_fe_sector(struct udf_fs *fs, struct udf_long_ad *icb, uint8_t *sector_out) {
    struct udf_tag tag;
    uint32_t sector = fs->partition_start + icb->block;

    if (udf_read_tag(fs->device, sector, &tag, sector_out, UDF_SECTOR_SIZE) != 0) {
        return -1;
    }

    if (tag.tag_id != UDF_TAG_FE && tag.tag_id != UDF_TAG_EFE) {
        kprint("UDF: Invalid FE/EFE tag\n");
        return -1;
    }

    return 0;
}

/*
 * Read file data via allocation descriptors
 * For now: only handles inline (embedded) data and short_ad
 */
uint32_t udf_read_file(struct udf_fs *fs, struct udf_fe *fe, 
                       uint32_t offset, uint32_t size, uint8_t *buffer) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return 0;
    
    uint8_t ad_type = fe->icb_tag.flags & 0x7;

    /* Validate ext_attr_length + alloc_desc_length fit within sector */
    if (sizeof(struct udf_fe) + fe->ext_attr_length > UDF_SECTOR_SIZE) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return 0;
    }
    if (sizeof(struct udf_fe) + fe->ext_attr_length + fe->alloc_desc_length > UDF_SECTOR_SIZE) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return 0;
    }

    uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
    
    if (ad_type == UDF_ICB_FLAG_AD_INLINE) {
        /* Inline (embedded) data lives directly in the allocation area
         * of the FE sector.  fe->info_length and offset/size are all
         * untrusted: the original `fe->info_length - offset` underflowed
         * to a near-4 GiB count when offset exceeded info_length, and
         * `offset + size` could wrap, either way driving an enormous
         * out-of-bounds memcpy out of the 2048-byte FE sector buffer
         * and into the caller's buffer.  Bound everything to the bytes
         * actually embedded in the sector. */
        uint32_t header = (uint32_t)sizeof(struct udf_fe) + fe->ext_attr_length;
        uint32_t avail  = (header < UDF_SECTOR_SIZE) ? (UDF_SECTOR_SIZE - header) : 0;
        uint32_t inline_len = fe->info_length;
        if (inline_len > avail) inline_len = avail;   /* clamp to sector */

        if (offset >= inline_len) {                   /* nothing to read */
            kfree(sector_buf, UDF_SECTOR_SIZE);
            return 0;
        }
        uint32_t remain = inline_len - offset;        /* underflow-safe */
        if (size > remain) size = remain;
        memcpy(buffer, alloc_area + offset, size);
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return size;
    }
    
    if (ad_type == UDF_ICB_FLAG_AD_SHORT) {
        /* Short allocation descriptors */
        struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;
        uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
        uint32_t total_read = 0;
        uint32_t file_pos = 0;
        
        for (uint32_t i = 0; i < num_ads && size > 0; i++) {
            uint32_t ext_len = ads[i].length & 0x3FFFFFFF;
            uint32_t type = (ads[i].length >> 30) & 0x3;
            uint32_t ext_start = fs->partition_start + ads[i].position;
            
            if (offset >= file_pos + ext_len) {
                file_pos += ext_len;
                continue;
            }
            
            /* Read from this extent */
            uint32_t ext_off = (offset > file_pos) ? offset - file_pos : 0;
            if (ext_off > ext_len) {
                file_pos += ext_len;
                continue;
            }
            uint32_t ext_read = ext_len - ext_off;
            if (ext_read > size) ext_read = size;
            
            if (type == 1 || type == 2) {
                memset(buffer, 0, ext_read);
                buffer += ext_read;
                total_read += ext_read;
                size -= ext_read;
                offset += ext_read;
                ext_read = 0;
            } else if (type == 3) {
                return total_read;
            }

            /* Read sector by sector */
            while (ext_read > 0) {
                uint32_t sector = ext_start + (ext_off / UDF_SECTOR_SIZE);
                uint32_t sec_off = ext_off % UDF_SECTOR_SIZE;
                uint32_t to_read = UDF_SECTOR_SIZE - sec_off;
                if (to_read > ext_read) to_read = ext_read;
                
                fs->device->read(fs->device, (off_t)sector * UDF_SECTOR_SIZE, 
                                 UDF_SECTOR_SIZE, sector_buf);
                memcpy(buffer, sector_buf + sec_off, to_read);
                
                buffer += to_read;
                total_read += to_read;
                ext_off += to_read;
                ext_read -= to_read;
                size -= to_read;
                offset += to_read;
            }
            
            file_pos += ext_len;
        }
        return total_read;
    }
    
    if (ad_type == UDF_ICB_FLAG_AD_LONG) {
        /* Long allocation descriptors */
        struct udf_long_ad *ads = (struct udf_long_ad *)alloc_area;
        uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_long_ad);
        uint32_t total_read = 0;
        uint32_t file_pos = 0;

        for (uint32_t i = 0; i < num_ads && size > 0; i++) {
            uint32_t ext_len = ads[i].length & 0x3FFFFFFF;
            uint32_t type = (ads[i].length >> 30) & 0x3;
            /* Note: ignoring partition reference, assuming single partition */
            uint32_t ext_start = fs->partition_start + ads[i].block;

            if (offset >= file_pos + ext_len) {
                file_pos += ext_len;
                continue;
            }

            /* Read from this extent */
            uint32_t ext_off = (offset > file_pos) ? offset - file_pos : 0;
            if (ext_off > ext_len) {
                file_pos += ext_len;
                continue;
            }
            uint32_t ext_read = ext_len - ext_off;
            if (ext_read > size) ext_read = size;

            if (type == 1 || type == 2) {
                memset(buffer, 0, ext_read);
                buffer += ext_read;
                total_read += ext_read;
                size -= ext_read;
                offset += ext_read;
                ext_read = 0;
            } else if (type == 3) {
                return total_read;
            }

            /* Read sector by sector */
            while (ext_read > 0) {
                uint32_t sector = ext_start + (ext_off / UDF_SECTOR_SIZE);
                uint32_t sec_off = ext_off % UDF_SECTOR_SIZE;
                uint32_t to_read = UDF_SECTOR_SIZE - sec_off;
                if (to_read > ext_read) to_read = ext_read;

                fs->device->read(fs->device, (off_t)sector * UDF_SECTOR_SIZE,
                                 UDF_SECTOR_SIZE, sector_buf);
                memcpy(buffer, sector_buf + sec_off, to_read);

                buffer += to_read;
                total_read += to_read;
                ext_off += to_read;
                ext_read -= to_read;
                size -= to_read;
                offset += to_read;
            }

            file_pos += ext_len;
        }
        return total_read;
    }

    return 0;
}

/* UDF node context for VFS integration */
typedef struct {
    struct udf_fs *fs;
    struct udf_long_ad icb;
    uint8_t fe_sector[UDF_SECTOR_SIZE]; /* Full on-disk FE sector (includes alloc descriptors) */
} udf_node_t;

#define UDF_NODE_CACHE_SIZE 64
static udf_node_t udf_node_cache[UDF_NODE_CACHE_SIZE];
static fs_node_t udf_fs_node_cache[UDF_NODE_CACHE_SIZE];
static int udf_node_cache_idx = 0;
static struct dirent udf_dirent;

/* Forward declarations for VFS operations */
/* Forward declarations for VFS operations */
static size_t udf_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *udf_vfs_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *udf_vfs_finddir(fs_node_t *node, char *name);
static int udf_vfs_mkdir(fs_node_t *parent, const char *name, uint16_t permission);

/* Vnode Operations for UDF */
static int udf_vop_mkdir(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap);

struct vnodeops udf_vnodeops = {
    .vop_mkdir = udf_vop_mkdir,
};

/* External functions from udf_write.c */

/* UDF context defined in udf.c, declared in udf.h */

static fs_node_t *udf_alloc_node(struct udf_fs *fs, struct udf_long_ad *icb, uint8_t *fe_sector) {
    int idx = udf_node_cache_idx++ % UDF_NODE_CACHE_SIZE;
    
    udf_node_t *ctx = &udf_node_cache[idx];
    fs_node_t *node = &udf_fs_node_cache[idx];
    struct udf_fe *fe = (struct udf_fe *)fe_sector;
    
    ctx->fs = fs;
    memcpy(&ctx->icb, icb, sizeof(struct udf_long_ad));
    memcpy(ctx->fe_sector, fe_sector, UDF_SECTOR_SIZE);
    
    memset(node, 0, sizeof(fs_node_t));
    node->length = (uint32_t)fe->info_length;
    node->uid = fe->uid;
    node->gid = fe->gid;
    node->impl = (uintptr_t)ctx;
    
    if (fe->icb_tag.file_type == UDF_FILETYPE_DIR) {
        node->flags = FS_DIRECTORY;
        node->readdir = udf_vfs_readdir;
        node->finddir = udf_vfs_finddir;
        node->mkdir = udf_vfs_mkdir;
    } else {
        node->flags = FS_FILE;
        node->read = udf_vfs_read;
    }
    
    return node;
}

static size_t udf_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    return udf_read_file(ctx->fs, (struct udf_fe *)ctx->fe_sector, (uint32_t)offset, size, buffer);
}

/*
 * Iterate directory entries (FIDs)
 */
static struct dirent *udf_vfs_readdir(fs_node_t *node, uint64_t index) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    struct udf_fe *fe = (struct udf_fe *)ctx->fe_sector;

    /* Read directory data.  The previous implementation used
     * sizeof(dir_buf) where dir_buf is a uint8_t * (= 4 bytes on i386,
     * not the buffer length), so the loop below saw only the first
     * four bytes of the directory and returned no entries at all.
     *
     * Allocate the actual directory size, capped at 1 MiB to bound a
     * pathological filesystem. */
    uint32_t dir_size = (uint32_t)fe->info_length;
    if (dir_size > (1U << 20)) dir_size = 1U << 20;
    uint32_t buf_size = dir_size > 0 ? dir_size : 4096;
    uint8_t *dir_buf = kmalloc(buf_size);
    if (!dir_buf) return NULL;

    uint32_t read_size = dir_size;
    if (read_size > 0)
        udf_read_file(ctx->fs, fe, 0, read_size, dir_buf);
    
    /* Iterate FIDs */
    uint32_t pos = 0;
    uint32_t cur_idx = 0;
    
    while (pos + 38 <= read_size) {
        struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
        
        /* Calculate FID size (38 + impl_use + file_id, padded to 4 bytes) */
        uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
        fid_size = (fid_size + 3) & ~3;
        if (fid_size < 40 || pos + fid_size > read_size) break;
        
        if (!(fid->characteristics & UDF_FID_DELETED)) {
            if (cur_idx == index) {
                if (fid->characteristics & UDF_FID_PARENT) {
                    strlcpy(udf_dirent.d_name, "..", sizeof(udf_dirent.d_name));
                    udf_dirent.d_name[sizeof(udf_dirent.d_name) - 1] = '\0';
                } else {
                    /* Extract filename (after impl_use) */
                    char *name = (char *)fid + 38 + fid->impl_use_length;
                    uint32_t len = fid->file_id_length;
                    /* Handle OSTA compressed unicode (type byte + chars) */
                    if (len > 0 && name[0] == 8) {
                    /* Ensure len fits in udf_dirent.d_name to prevent buffer overflow */
                    if (len > sizeof(udf_dirent.d_name)) {
                        len = sizeof(udf_dirent.d_name);
                    }
                    memcpy(udf_dirent.d_name, name + 1, len - 1);
                    udf_dirent.d_name[len - 1] = '\0';
                    } else {
                    /* Ensure len fits in udf_dirent.d_name to prevent buffer overflow */
                    if (len > sizeof(udf_dirent.d_name) - 1) {
                        len = sizeof(udf_dirent.d_name) - 1;
                    }
                    memcpy(udf_dirent.d_name, name, len);
                    udf_dirent.d_name[len] = '\0';
                    }
                }
                udf_dirent.d_ino = fid->icb.block;
                kfree(dir_buf, buf_size);
                return &udf_dirent;
            }
            cur_idx++;
        }
        
        pos += fid_size;
    }

    kfree(dir_buf, buf_size);
    return NULL;
}

/*
 * Find entry by name in directory
 */
static fs_node_t *udf_vfs_finddir(fs_node_t *node, char *name) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    struct udf_fe *fe = (struct udf_fe *)ctx->fe_sector;

    /* Same sizeof(uint8_t *) bug as udf_vfs_readdir — see comment there.
     * Allocate the actual directory size, capped at 1 MiB. */
    uint32_t dir_size = (uint32_t)fe->info_length;
    if (dir_size > (1U << 20)) dir_size = 1U << 20;
    uint32_t buf_size = dir_size > 0 ? dir_size : 4096;
    uint8_t *dir_buf = kmalloc(buf_size);
    if (!dir_buf) return NULL;

    uint32_t read_size = dir_size;
    if (read_size > 0)
        udf_read_file(ctx->fs, fe, 0, read_size, dir_buf);
    
    uint32_t pos = 0;
    while (pos + 38 <= read_size) {
        struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
        
        uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
        fid_size = (fid_size + 3) & ~3;
        if (fid_size < 40 || pos + fid_size > read_size) break;
        
        if (!(fid->characteristics & UDF_FID_DELETED)) {
            char fname[256];
            char *src = (char *)fid + 38 + fid->impl_use_length;
            uint8_t len = fid->file_id_length;
            
            if (len > 0 && src[0] == 8) {
                memcpy(fname, src + 1, len - 1);
                fname[len - 1] = '\0';
            } else {
                memcpy(fname, src, len);
                fname[len] = '\0';
            }
            
            if (strcmp(fname, name) == 0) {
                /* Found - read full file entry sector */
                uint8_t fe_sec[UDF_SECTOR_SIZE];
                if (udf_read_fe_sector(ctx->fs, &fid->icb, fe_sec) == 0) {
                    fs_node_t *result = udf_alloc_node(ctx->fs, &fid->icb, fe_sec);
                    /* Ensure null-termination and prevent buffer overflow by truncating if necessary */
                    strlcpy(result->name, fname, sizeof(result->name));
                    result->name[sizeof(result->name) - 1] = '\0';
                    kfree(dir_buf, buf_size);
                    return result;
                }
            }
        }
        
        pos += fid_size;
    }

    kfree(dir_buf, buf_size);
    return NULL;
}

static int udf_vfs_mkdir(fs_node_t *parent, const char *name, uint16_t permission) {
    if (!parent || !name) return -1;
    
    udf_node_t *pctx = (udf_node_t *)(uintptr_t)parent->impl;
    struct udf_fs *fs = pctx->fs;
    
    /* Allocate block for new directory */
    uint32_t block = udf_alloc_block(fs);
    if (block == 0) return -1; // No space

    // Get current process credentials
    uint32_t uid = current_process ? current_process->uid : 0;
    uint32_t gid = current_process ? current_process->gid : 0;

    /* Create new directory FE */
    if (udf_create_fe(fs, block, UDF_FILETYPE_DIR, uid, gid, permission) != 0) {
        udf_free_block(fs, block);
        return -1;
    }
    
    /* Create ICB to point to new directory */
    struct udf_long_ad new_icb;
    new_icb.length = 2048; // Length of FE
    new_icb.block = block;
    new_icb.partition = pctx->icb.partition;
    memset(new_icb.impl_use, 0, 6);
    
    /* Add entry to parent directory */
    struct udf_fe *pfe = (struct udf_fe *)pctx->fe_sector;
    if (udf_add_fid(fs, pfe, pctx->icb.block, name, &new_icb, UDF_FID_DIRECTORY) != 0) {
        udf_free_block(fs, block);
        return -1;
    }
    
    /* Add . and .. to new directory */
    struct udf_fe new_fe;
    struct udf_long_ad fe_icb = new_icb;
    // Read the newly created FE so we can add to it
    if (udf_read_fe(fs, &fe_icb, &new_fe) != 0) return -1;
    
    // Add .. (parent)
    if (udf_add_fid(fs, &new_fe, block, "..", &pctx->icb, UDF_FID_DIRECTORY | UDF_FID_PARENT) != 0) {
        // Warning?
    }
    
    // Update parent node size
    parent->length = (uint32_t)pfe->info_length;
    
    return 0;
}

static int udf_vop_mkdir(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap) {
    int error;

    if (dvp->v_type != VDIR)
        return ENOTDIR;

    /* Call the internal legacy-style mkdir for now as it handles the UDF specific logic */
    /* Note: conversion of cnp->cn_nameptr and vap->va_mode */
    error = udf_vfs_mkdir((fs_node_t *)dvp, cnp->cn_nameptr, (uint16_t)vap->va_mode);
    if (error)
        return error;

    /* After successful creation, we need to return the new vnode */
    /* We lookup the newly created directory to get its ICB/FE */
    fs_node_t *new_node = udf_vfs_finddir((fs_node_t *)dvp, cnp->cn_nameptr);
    if (!new_node)
        return EIO;

    udf_node_t *nctx = (udf_node_t *)new_node->impl;
    
    error = getnewvnode("udf", dvp->v_mount, &udf_vnodeops, vpp);
    if (error)
        return error;

    (*vpp)->v_type = VDIR;
    (*vpp)->v_data = nctx;
    (*vpp)->v_ino = nctx->icb.block;

    return 0;
}

static int udf_unmount(fs_node_t *root) {
    if (!root) return -1;
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)root->impl;
    if (!ctx) return -1;
    struct udf_fs *fs = ctx->fs;
    if (!fs) return -1;

    // Invalidate node cache for this filesystem
    for (int i = 0; i < UDF_NODE_CACHE_SIZE; i++) {
        if (udf_node_cache[i].fs == fs) {
            memset(&udf_node_cache[i], 0, sizeof(udf_node_t));
            memset(&udf_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }

    kfree(fs, sizeof(struct udf_fs));
    return 0;
}

/* VFS filesystem structure */
/*
 * Decode a UDF dstring (ECMA-167 1/7.2.12) into an ASCII label.  The
 * field's last byte holds the used length (the compression-id byte plus
 * the character bytes); byte 0 is the compression id (8 = 8-bit, 16 =
 * 16-bit big-endian).  Non-ASCII code points become '?'.
 */
static int udf_decode_dstring(const char *field, size_t field_len,
                              char *out, size_t out_len) {
    if (out_len == 0) return -1;
    out[0] = '\0';
    uint8_t complen = (uint8_t)field[field_len - 1];
    if (complen < 1 || complen > field_len) return -1;
    uint8_t comp = (uint8_t)field[0];
    size_t n = 0;
    if (comp == 8) {
        for (size_t i = 1; i < complen && n + 1 < out_len; i++) {
            unsigned char c = (unsigned char)field[i];
            out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
    } else if (comp == 16) {
        for (size_t i = 1; i + 1 < complen && n + 1 < out_len; i += 2) {
            unsigned int c = ((unsigned char)field[i] << 8) |
                             (unsigned char)field[i + 1];
            out[n++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
        }
    } else {
        return -1;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    return n > 0 ? 0 : -1;
}

/*
 * Read a UDF volume label off the raw device without mounting: locate the
 * Anchor VDP at sector 256, walk the main Volume Descriptor Sequence to the
 * Logical Volume Descriptor, and decode its logical_volume_id.  Tag
 * checksums gate recognition so a non-UDF device is rejected.
 */
int udf_read_label(blkdev_t *dev, char *label, size_t len) {
    if (!dev || !label || len == 0) return -1;
    uint8_t *buf = kmalloc(UDF_SECTOR_SIZE);
    if (!buf) return -1;
    int rc = -1;
    struct udf_avdp *avdp;
    uint32_t vds_start, vds_count, i;

    if (blkdev_read_bytes(dev, (uint64_t)UDF_AVDP_SECTOR * UDF_SECTOR_SIZE,
                          UDF_SECTOR_SIZE, buf) != UDF_SECTOR_SIZE)
        goto out;
    avdp = (struct udf_avdp *)buf;
    if (avdp->tag.tag_id != UDF_TAG_ANCHOR_VDP ||
        avdp->tag.tag_location != UDF_AVDP_SECTOR ||
        udf_tag_checksum(&avdp->tag) != avdp->tag.tag_checksum)
        goto out;   /* not a UDF volume */

    vds_start = avdp->main_vds_extent.location;
    vds_count = avdp->main_vds_extent.length / UDF_SECTOR_SIZE;
    if (vds_count > 64) vds_count = 64;   /* bound the scan */

    for (i = 0; i < vds_count; i++) {
        struct udf_tag *tag;
        if (blkdev_read_bytes(dev, (uint64_t)(vds_start + i) * UDF_SECTOR_SIZE,
                              UDF_SECTOR_SIZE, buf) != UDF_SECTOR_SIZE)
            continue;
        tag = (struct udf_tag *)buf;
        if (tag->tag_location != vds_start + i ||
            udf_tag_checksum(tag) != tag->tag_checksum)
            continue;
        if (tag->tag_id == UDF_TAG_TERMINATING) break;
        if (tag->tag_id == UDF_TAG_LOGICAL_VD) {
            struct udf_lvd *lvd = (struct udf_lvd *)buf;
            rc = udf_decode_dstring(lvd->logical_volume_id,
                                    sizeof(lvd->logical_volume_id), label, len);
            break;
        }
    }

out:
    kfree(buf, UDF_SECTOR_SIZE);
    return rc;
}

static filesystem_t udf_filesystem = {
    .name = "udf",
    .mount = udf_mount,
    .read_label = udf_read_label,
};

/*
 * Mount UDF filesystem
 */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("UDF: No device or read function\n");
        return NULL;
    }

    struct udf_fs *fs = kmalloc(sizeof(struct udf_fs));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(struct udf_fs));
    fs->device = dev;
    
    /* Find AVDP */
    struct udf_avdp avdp;
    if (udf_find_avdp(dev, &avdp) != 0) {
        kfree(fs, sizeof(struct udf_fs));
        return NULL;
    }
    
    /* Parse VDS */
    struct udf_pvd pvd;
    struct udf_pd pd;
    struct udf_lvd lvd;
    if (udf_read_vds(dev, &avdp.main_vds_extent, &pvd, &pd, &lvd) != 0) {
        kfree(fs, sizeof(struct udf_fs));
        return NULL;
    }
    
    /* Set up filesystem context */
    fs->device = dev;
    fs->sector_size = UDF_SECTOR_SIZE;
    fs->partition_start = pd.partition_start;
    fs->partition_length = pd.partition_length;
    fs->logical_block_size = lvd.logical_block_size;
    
    /* Initialize Space Bitmap */
    struct udf_partition_header_desc *phd = (struct udf_partition_header_desc *)pd.contents_use;
    if (phd->unalloc_space_bitmap.length > 0) {
        // Bitmap exists
        udf_read_space_bitmap(fs,
                              phd->unalloc_space_bitmap.position,
                              phd->unalloc_space_bitmap.length);
    } else {
        kprint("UDF: No space bitmap found (ReadOnly?)\n");
    }
    
    /* Read File Set Descriptor */
    struct udf_fsd fsd;
    if (udf_read_fsd(dev, fs, &lvd, &fsd) != 0) {
        kfree(fs, sizeof(struct udf_fs));
        return NULL;
    }
    
    /* Save root ICB */
    memcpy(&fs->root_icb, &fsd.root_dir_icb, sizeof(struct udf_long_ad));
    
    /* Read root directory file entry (full sector) */
    uint8_t root_fe_sector[UDF_SECTOR_SIZE];
    if (udf_read_fe_sector(fs, &fs->root_icb, root_fe_sector) != 0) {
        kprint("UDF: Failed to read root directory\n");
        kfree(fs, sizeof(struct udf_fs));
        return NULL;
    }
    /* Set up root node */
    fs_node_t *root_node = udf_alloc_node(fs, &fs->root_icb, root_fe_sector);
    if (!root_node) {
        kfree(fs, sizeof(struct udf_fs));
        return NULL;
    }
    
    strlcpy(root_node->name, "/", sizeof(root_node->name));
    root_node->name[sizeof(root_node->name) - 1] = '\0';
    root_node->unmount = udf_unmount;
    
    kprint("UDF: Mounted successfully\n");
    return root_node;
}

void udf_init(void) {
    kprint("Initializing UDF Driver...\n");
    udf_crc_init();
    vfs_register_filesystem(&udf_filesystem);
}

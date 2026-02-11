/*
 * udf_write.c - UDF Filesystem Write Support
 *
 * Block allocation, file creation, directory operations.
 */

#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <string.h>

/* External context from udf.c */
extern struct udf_fs udf_ctx;

static uint8_t *space_bitmap = NULL;
static uint32_t space_bitmap_size = 0;
static uint32_t space_bitmap_sector = 0;

/*
 * Write space bitmap back to disk
 */
static void udf_write_space_bitmap(void) {
    if (!space_bitmap || !udf_ctx.device) return;

    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)(space_bitmap - sizeof(struct udf_space_bitmap));

    /* Recalculate CRC of the bitmap data */
    if (sbm->tag.desc_crc_len > 0) {
        sbm->tag.desc_crc = udf_crc(space_bitmap, sbm->tag.desc_crc_len);
    }

    /* Recalculate tag checksum */
    sbm->tag.tag_checksum = udf_tag_checksum(&sbm->tag);

    /* Calculate number of sectors */
    uint32_t total_size = sizeof(struct udf_space_bitmap) + sbm->num_bytes;
    uint32_t sectors = (total_size + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;

    /* Write to disk */
    for (uint32_t i = 0; i < sectors; i++) {
        off_t offset = (off_t)(space_bitmap_sector + i) * UDF_SECTOR_SIZE;
        uint8_t *buf = (uint8_t *)sbm + (i * UDF_SECTOR_SIZE);
        udf_ctx.device->write(udf_ctx.device, offset, UDF_SECTOR_SIZE, buf);
    }
}

/*
 * Read and parse space bitmap from partition header
 */
int udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, 
                          uint32_t bitmap_loc, uint32_t bitmap_len) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE * 4];
    
    uint32_t sector = partition_start + bitmap_loc;
    uint32_t sectors = (bitmap_len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;
    
    if (sectors > 4) {
        kprint("UDF: Space bitmap too large\n");
        return -1;
    }
    
    for (uint32_t i = 0; i < sectors; i++) {
        off_t offset = (off_t)(sector + i) * UDF_SECTOR_SIZE;
        dev->read(dev, offset, UDF_SECTOR_SIZE, sector_buf + i * UDF_SECTOR_SIZE);
    }
    
    struct udf_space_bitmap *sbm = (struct udf_space_bitmap *)sector_buf;
    
    if (sbm->tag.tag_id != UDF_TAG_SBD) {
        kprint("UDF: Invalid space bitmap tag\n");
        return -1;
    }
    
    space_bitmap = sector_buf + sizeof(struct udf_space_bitmap);
    space_bitmap_size = sbm->num_bits;
    space_bitmap_sector = sector;
    
    return 0;
}

/*
 * Allocate a free block from the space bitmap
 * Returns block number (relative to partition) or 0 on failure
 */
uint32_t udf_alloc_block(void) {
    if (!space_bitmap) return 0;
    
    /* Find first zero bit in bitmap */
    for (uint32_t byte = 0; byte < space_bitmap_size / 8; byte++) {
        if (space_bitmap[byte] != 0xFF) {
            /* Found byte with free bit */
            for (int bit = 0; bit < 8; bit++) {
                if (!(space_bitmap[byte] & (1 << bit))) {
                    /* Mark as allocated */
                    space_bitmap[byte] |= (1 << bit);
                    udf_write_space_bitmap();
                    return byte * 8 + bit;
                }
            }
        }
    }
    
    kprint("UDF: No free blocks\n");
    return 0;
}

/*
 * Free a block back to the space bitmap
 */
void udf_free_block(uint32_t block) {
    if (!space_bitmap || block >= space_bitmap_size) return;
    
    uint32_t byte = block / 8;
    uint8_t bit = block % 8;
    
    space_bitmap[byte] &= ~(1 << bit);
    udf_write_space_bitmap();
}

/*
 * Create a new File Entry for a file
 */
int udf_create_fe(fs_node_t *dev, uint32_t block, uint8_t file_type,
                  uint32_t uid, uint32_t gid, uint32_t permissions) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    
    memset(sector_buf, 0, UDF_SECTOR_SIZE);
    
    struct udf_fe *fe = (struct udf_fe *)sector_buf;
    
    /* Fill in tag */
    fe->tag.tag_id = UDF_TAG_FE;
    fe->tag.desc_version = 2;
    fe->tag.tag_location = udf_ctx.partition_start + block;
    
    /* ICB tag */
    fe->icb_tag.file_type = file_type;
    fe->icb_tag.flags = UDF_ICB_FLAG_AD_SHORT;  /* Use short ADs */
    
    /* Permissions */
    fe->uid = uid;
    fe->gid = gid;  
    fe->permissions = permissions;
    fe->file_link_count = 1;
    
    /* Zero size initially */
    fe->info_length = 0;
    fe->logical_blocks = 0;
    
    /* Calculate tag checksum */
    uint8_t *p = (uint8_t *)&fe->tag;
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    fe->tag.tag_checksum = sum;
    
    /* Write to disk */
    off_t offset = (off_t)(udf_ctx.partition_start + block) * UDF_SECTOR_SIZE;
    uint32_t written = dev->write(dev, offset, UDF_SECTOR_SIZE, sector_buf);
    
    return (written == UDF_SECTOR_SIZE) ? 0 : -1;
}

/*
 * Write data to a file
 * Handles both inline data and extent allocation (short ADs)
 */
int udf_write_file(fs_node_t *dev, struct udf_fe *fe, uint32_t fe_block,
                   uint32_t offset, uint32_t size, const uint8_t *data) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    
    /* Read existing FE */
    off_t disk_off = (off_t)(udf_ctx.partition_start + fe_block) * UDF_SECTOR_SIZE;
    dev->read(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
    
    struct udf_fe *disk_fe = (struct udf_fe *)sector_buf;
    
    uint64_t total_size = disk_fe->info_length;
    if (offset + size > total_size) total_size = offset + size;

    int is_inline = (disk_fe->icb_tag.flags & 0x7) == UDF_ICB_FLAG_AD_INLINE;
    int fits_inline = (total_size <= UDF_SECTOR_SIZE - sizeof(struct udf_fe) - disk_fe->ext_attr_length - 100);

    /* Case 1: Write fits in INLINE */
    if (is_inline && fits_inline) {
        disk_fe->icb_tag.flags = (disk_fe->icb_tag.flags & ~0x7) | UDF_ICB_FLAG_AD_INLINE;
        
        uint8_t *alloc_area = sector_buf + sizeof(struct udf_fe) + disk_fe->ext_attr_length;
        memcpy(alloc_area + offset, data, size);
        
        disk_fe->info_length = total_size;
        disk_fe->alloc_desc_length = (uint32_t)disk_fe->info_length;
        
        disk_fe->tag.tag_checksum = udf_tag_checksum(&disk_fe->tag);
        
        if (dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf) != UDF_SECTOR_SIZE) {
            return -1;
        }
        memcpy(fe, disk_fe, sizeof(struct udf_fe));
        return 0;
    }
    
    /* Case 2: Convert INLINE to SHORT_AD */
    if (is_inline) {
        uint32_t new_block = udf_alloc_block();
        if (new_block == 0) return -1;

        uint8_t *alloc_area = sector_buf + sizeof(struct udf_fe) + disk_fe->ext_attr_length;
        uint32_t inline_len = (uint32_t)disk_fe->info_length;

        /* Write existing inline data to new block */
        if (inline_len > 0) {
            if (dev->write(dev, (off_t)(udf_ctx.partition_start + new_block) * UDF_SECTOR_SIZE, inline_len, alloc_area) != inline_len) {
                udf_free_block(new_block);
                return -1;
            }
        }

        disk_fe->icb_tag.flags = (disk_fe->icb_tag.flags & ~0x7) | UDF_ICB_FLAG_AD_SHORT;
        disk_fe->alloc_desc_length = sizeof(struct udf_short_ad);

        struct udf_short_ad *ad = (struct udf_short_ad *)alloc_area;
        ad->length = inline_len > 0 ? UDF_SECTOR_SIZE : UDF_SECTOR_SIZE; /* Alloc full block */
        ad->position = new_block;
    }

    /* Case 3: Write using SHORT_ADs */
    struct udf_short_ad *ads = (struct udf_short_ad *)(sector_buf + sizeof(struct udf_fe) + disk_fe->ext_attr_length);
    uint32_t num_ads = disk_fe->alloc_desc_length / sizeof(struct udf_short_ad);

    uint32_t written = 0;

    while (written < size) {
        uint32_t target_offset = offset + written;
        uint32_t target_len = size - written;

        int ad_idx = -1;
        uint32_t ad_pos = 0;

        /* Find AD covering target_offset */
        for (uint32_t i = 0; i < num_ads; i++) {
            uint32_t len = ads[i].length & 0x3FFFFFFF;
            if (target_offset >= ad_pos && target_offset < ad_pos + len) {
                ad_idx = i;
                break;
            }
            ad_pos += len;
        }

        if (ad_idx != -1) {
            /* Write to existing extent */
            struct udf_short_ad *ad = &ads[ad_idx];
            uint32_t ad_len = ad->length & 0x3FFFFFFF;
            uint32_t ad_offset = target_offset - ad_pos;

            uint32_t space_in_ad = ad_len - ad_offset;
            uint32_t chunk = (target_len > space_in_ad) ? space_in_ad : target_len;

            uint32_t block_off = ad_offset / UDF_SECTOR_SIZE;
            uint32_t byte_off = ad_offset % UDF_SECTOR_SIZE;

            off_t phys_addr = (off_t)(udf_ctx.partition_start + ad->position + block_off) * UDF_SECTOR_SIZE + byte_off;
            if (dev->write(dev, phys_addr, chunk, data + written) != chunk) {
                return -1;
            }

            written += chunk;
        } else {
            /* Append new extent */
            /* Calculate current end of file based on ADs */
            ad_pos = 0;
            for (uint32_t i = 0; i < num_ads; i++) ad_pos += (ads[i].length & 0x3FFFFFFF);

            if (target_offset > ad_pos) {
                kprint("UDF: Sparse write not implemented\n");
                return -1;
            }

            uint32_t new_block = udf_alloc_block();
            if (new_block == 0) return -1;

            /* Try to merge with last AD */
            int merged = 0;
            if (num_ads > 0) {
                struct udf_short_ad *last_ad = &ads[num_ads - 1];
                uint32_t len = last_ad->length & 0x3FFFFFFF;
                uint32_t last_block_start = last_ad->position;
                uint32_t last_block_count = (len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;

                if (last_block_start + last_block_count == new_block) {
                    last_ad->length += UDF_SECTOR_SIZE;
                    merged = 1;
                }
            }

            if (!merged) {
                if (sizeof(struct udf_fe) + disk_fe->ext_attr_length + (num_ads + 1) * sizeof(struct udf_short_ad) > UDF_SECTOR_SIZE) {
                    kprint("UDF: File Entry full\n");
                    udf_free_block(new_block);
                    return -1;
                }

                ads[num_ads].position = new_block;
                ads[num_ads].length = UDF_SECTOR_SIZE;
                num_ads++;
                disk_fe->alloc_desc_length += sizeof(struct udf_short_ad);
            }
        }
    }

    if (total_size > disk_fe->info_length) {
        disk_fe->info_length = total_size;
    }

    disk_fe->tag.tag_checksum = udf_tag_checksum(&disk_fe->tag);

    if (dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf) != UDF_SECTOR_SIZE) {
        return -1;
    }
    memcpy(fe, disk_fe, sizeof(struct udf_fe));
    return 0;
}

/*
 * Add a File Identifier Descriptor to a directory
 */
int udf_add_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block,
                const char *name, struct udf_long_ad *icb, uint8_t characteristics) {
    static uint8_t dir_buf[4096];
    
    /* Read directory data */
    off_t disk_off = (off_t)(udf_ctx.partition_start + dir_block) * UDF_SECTOR_SIZE;
    dev->read(dev, disk_off, UDF_SECTOR_SIZE, dir_buf);
    
    uint32_t dir_size = (uint32_t)dir_fe->info_length;
    
    /* Calculate new FID size */
    uint8_t name_len = strlen(name);
    uint32_t fid_size = 38 + 0 + (name_len + 1);  /* +1 for compression type */
    fid_size = (fid_size + 3) & ~3;  /* Pad to 4 bytes */
    
    /* Create FID at end of directory */
    struct udf_fid *fid = (struct udf_fid *)(dir_buf + dir_size);
    memset(fid, 0, fid_size);
    
    fid->tag.tag_id = UDF_TAG_FID;
    fid->tag.tag_location = udf_ctx.partition_start + dir_block;
    fid->file_version = 1;
    fid->characteristics = characteristics;
    fid->file_id_length = name_len + 1;  /* Include compression byte */
    memcpy(&fid->icb, icb, sizeof(struct udf_long_ad));
    
    /* Copy name with OSTA compression type prefix */
    char *name_ptr = (char *)fid + 38;
    name_ptr[0] = 8;  /* Type 8 = 8-bit characters */
    memcpy(name_ptr + 1, name, name_len);
    
    /* Calculate tag checksum */
    uint8_t *p = (uint8_t *)&fid->tag;
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    fid->tag.tag_checksum = sum;
    
    /* Update directory size */
    dir_fe->info_length = dir_size + fid_size;
    
    /* Write back */
    dev->write(dev, disk_off, UDF_SECTOR_SIZE, dir_buf);
    
    return 0;
}

/*
 * Remove a File Identifier Descriptor from a directory
 */
int udf_remove_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block,
                   const char *name) {
    static uint8_t dir_buf[4096];
    
    off_t disk_off = (off_t)(udf_ctx.partition_start + dir_block) * UDF_SECTOR_SIZE;
    dev->read(dev, disk_off, UDF_SECTOR_SIZE, dir_buf);
    
    uint32_t dir_size = (uint32_t)dir_fe->info_length;
    uint32_t pos = 0;
    
    while (pos < dir_size) {
        struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
        
        uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
        fid_size = (fid_size + 3) & ~3;
        
        /* Extract and compare name */
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
            /* Mark as deleted */
            fid->characteristics |= UDF_FID_DELETED;
            
            /* Write back */
            dev->write(dev, disk_off, UDF_SECTOR_SIZE, dir_buf);
            return 0;
        }
        
        pos += fid_size;
    }
    
    return -1;  /* Not found */
}

/*
 * Truncate or extend a file
 */
int udf_truncate(fs_node_t *dev, struct udf_fe *fe, uint32_t fe_block,
                 uint64_t new_size) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    
    off_t disk_off = (off_t)(udf_ctx.partition_start + fe_block) * UDF_SECTOR_SIZE;
    dev->read(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
    
    struct udf_fe *disk_fe = (struct udf_fe *)sector_buf;
    
    /* For inline files, just update size */
    if ((disk_fe->icb_tag.flags & 0x7) == UDF_ICB_FLAG_AD_INLINE) {
        if (new_size > UDF_SECTOR_SIZE - sizeof(struct udf_fe) - 100) {
            kprint("UDF: Cannot extend inline file beyond sector\n");
            return -1;
        }
        
        /* Zero out area between old size and new size if extending */
        if (new_size > disk_fe->info_length) {
            uint8_t *alloc_area = sector_buf + sizeof(struct udf_fe);
            memset(alloc_area + disk_fe->info_length, 0, 
                   new_size - disk_fe->info_length);
        }
        
        disk_fe->info_length = new_size;
        disk_fe->alloc_desc_length = (uint32_t)new_size;
        
        /* Recalculate checksum */
        uint8_t *p = (uint8_t *)&disk_fe->tag;
        uint8_t sum = 0;
        for (int i = 0; i < 4; i++) sum += p[i];
        for (int i = 5; i < 16; i++) sum += p[i];
        disk_fe->tag.tag_checksum = sum;
        
        dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
        memcpy(fe, disk_fe, sizeof(struct udf_fe));
        return 0;
    }
    
    /* TODO: Handle extent-based files */
    return -1;
}

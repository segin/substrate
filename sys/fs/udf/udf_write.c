/*
 * udf_write.c - UDF Filesystem Write Support
 *
 * Block allocation, file creation, directory operations.
 */

#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <string.h>
#include <vm/vm_kmem.h>

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
 * Convert inline data to Short Allocation Descriptor
 */
static int udf_convert_inline_to_short_ad(fs_node_t *dev, struct udf_fe *fe) {
    uint32_t len = (uint32_t)fe->info_length;
    uint8_t *inline_data = (uint8_t *)fe + sizeof(struct udf_fe) + fe->ext_attr_length;

    /* Allocate new block */
    uint32_t block = udf_alloc_block();
    if (block == 0) return -1;

    /* Write inline data to new block */
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) {
        udf_free_block(block);
        return -1;
    }
    memset(sector_buf, 0, UDF_SECTOR_SIZE);

    if (len > 0) {
        memcpy(sector_buf, inline_data, len);
    }

    off_t disk_off = (off_t)(udf_ctx.partition_start + block) * UDF_SECTOR_SIZE;
    dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
    kfree(sector_buf, UDF_SECTOR_SIZE);

    /* Update FE to use Short AD */
    fe->icb_tag.flags = (fe->icb_tag.flags & ~0x7) | UDF_ICB_FLAG_AD_SHORT;

    /* Create first AD */
    struct udf_short_ad *ad = (struct udf_short_ad *)inline_data;
    ad->length = UDF_SECTOR_SIZE; /* Allocated size */
    ad->position = block;

    fe->alloc_desc_length = sizeof(struct udf_short_ad);
    fe->logical_blocks = 1;

    return 0;
}

/*
 * Write data to a file using Short Allocation Descriptors
 */
static int udf_write_extent_data(fs_node_t *dev, struct udf_fe *fe,
                                 uint32_t offset, uint32_t size, const uint8_t *data) {
    uint8_t *ad_area = (uint8_t *)fe + sizeof(struct udf_fe) + fe->ext_attr_length;
    struct udf_short_ad *ads = (struct udf_short_ad *)ad_area;
    
    uint32_t written = 0;
    uint32_t rem_size = size;
    uint32_t file_offset = offset;

    uint8_t *data_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!data_buf) return -1;

    uint32_t current_extent_idx = 0;
    uint32_t logical_pos = 0;

    /* Loop until all data written */
    while (rem_size > 0) {
        uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
        ads = (struct udf_short_ad *)ad_area;

        /* Check if we need to append a new extent */
        if (current_extent_idx >= num_ads) {
            uint32_t new_block = udf_alloc_block();
            if (new_block == 0) {
                kfree(data_buf, UDF_SECTOR_SIZE);
                return -1;
            }

            /* Check if we can merge with previous extent */
            int merged = 0;
            uint32_t last_len = 0;

            if (num_ads > 0) {
                struct udf_short_ad *last = &ads[num_ads - 1];
                last_len = last->length & 0x3FFFFFFF;
                uint32_t last_type = last->length >> 30;

                /* Check contiguity and max length (~1GB) */
                if (last_type == 0 &&
                    last->position + (last_len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE == new_block &&
                    last_len + UDF_SECTOR_SIZE < 0x3FFFFFFF) {

                    last->length += UDF_SECTOR_SIZE;
                    merged = 1;
                }
            }

            if (merged) {
                /* We extended the previous extent.
                 * Adjust indices to point to the previous extent.
                 */
                current_extent_idx--;
                logical_pos -= last_len;
            } else {
                /* Check space in FE */
                if (fe->alloc_desc_length + sizeof(struct udf_short_ad) >
                    UDF_SECTOR_SIZE - sizeof(struct udf_fe) - fe->ext_attr_length) {
                    kprint("UDF: File Entry full, AED not implemented\n");
                    udf_free_block(new_block);
                    kfree(data_buf, UDF_SECTOR_SIZE);
                    return -1;
                }

                struct udf_short_ad *new_ad = &ads[num_ads];
                new_ad->length = UDF_SECTOR_SIZE;
                new_ad->position = new_block;
                fe->alloc_desc_length += sizeof(struct udf_short_ad);
            }

            fe->logical_blocks++;
        }

        struct udf_short_ad *cur_ad = &ads[current_extent_idx];
        uint32_t ext_len = cur_ad->length & 0x3FFFFFFF;

        /* Does this extent cover our current file_offset? */
        if (file_offset >= logical_pos && file_offset < logical_pos + ext_len) {
            uint32_t rel_off = file_offset - logical_pos;
            uint32_t available = ext_len - rel_off;
            uint32_t to_write = (rem_size < available) ? rem_size : available;

            uint32_t sec_idx = rel_off / UDF_SECTOR_SIZE;
            uint32_t sec_off = rel_off % UDF_SECTOR_SIZE;
            uint32_t sector = cur_ad->position + sec_idx;

            off_t disk_addr = (off_t)(udf_ctx.partition_start + sector) * UDF_SECTOR_SIZE;

            /* Partial sector write: read-modify-write */
            if (sec_off != 0 || to_write < UDF_SECTOR_SIZE) {
                uint32_t chunk = UDF_SECTOR_SIZE - sec_off;
                if (chunk > to_write) chunk = to_write;

                dev->read(dev, disk_addr, UDF_SECTOR_SIZE, data_buf);
                memcpy(data_buf + sec_off, data + written, chunk);
                dev->write(dev, disk_addr, UDF_SECTOR_SIZE, data_buf);

                written += chunk;
                rem_size -= chunk;
                file_offset += chunk;
            } else {
                /* Write full sectors */
                dev->write(dev, disk_addr, UDF_SECTOR_SIZE, data + written);
                written += UDF_SECTOR_SIZE;
                rem_size -= UDF_SECTOR_SIZE;
                file_offset += UDF_SECTOR_SIZE;
            }
        } else {
            /* Advance to next extent */
            logical_pos += ext_len;
            current_extent_idx++;
        }
    }

    if (file_offset > fe->info_length) {
        fe->info_length = file_offset;
    }

    kfree(data_buf, UDF_SECTOR_SIZE);
    return 0;
}

/*
 * Write data to a file
 */
int udf_write_file(fs_node_t *dev, struct udf_fe *fe, uint32_t fe_block,
                   uint32_t offset, uint32_t size, const uint8_t *data) {
    uint8_t *sector_buf = kmalloc(UDF_SECTOR_SIZE);
    if (!sector_buf) return -1;
    
    /* Read existing FE */
    off_t disk_off = (off_t)(udf_ctx.partition_start + fe_block) * UDF_SECTOR_SIZE;
    if (dev->read(dev, disk_off, UDF_SECTOR_SIZE, sector_buf) != UDF_SECTOR_SIZE) {
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return -1;
    }
    
    struct udf_fe *disk_fe = (struct udf_fe *)sector_buf;
    uint32_t total_needed = offset + size;
    uint8_t ad_type = disk_fe->icb_tag.flags & 0x7;

    /* For small files, try to use inline data */
    if (ad_type == UDF_ICB_FLAG_AD_INLINE &&
        total_needed <= UDF_SECTOR_SIZE - sizeof(struct udf_fe) - disk_fe->ext_attr_length - 40) {
        
        uint8_t *alloc_area = sector_buf + sizeof(struct udf_fe) + disk_fe->ext_attr_length;
        memcpy(alloc_area + offset, data, size);
        
        if (total_needed > disk_fe->info_length) {
            disk_fe->info_length = total_needed;
            disk_fe->alloc_desc_length = (uint32_t)disk_fe->info_length;
        }
        
        /* Recalculate tag checksum */
        uint8_t *p = (uint8_t *)&disk_fe->tag;
        uint8_t sum = 0;
        for (int i = 0; i < 4; i++) sum += p[i];
        for (int i = 5; i < 16; i++) sum += p[i];
        disk_fe->tag.tag_checksum = sum;
        
        /* Write back */
        dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
        memcpy(fe, disk_fe, sizeof(struct udf_fe));
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return 0;
    }
    
    /* If still inline but doesn't fit, convert to Short AD */
    if (ad_type == UDF_ICB_FLAG_AD_INLINE) {
        if (udf_convert_inline_to_short_ad(dev, disk_fe) != 0) {
            kfree(sector_buf, UDF_SECTOR_SIZE);
            return -1;
        }
        ad_type = UDF_ICB_FLAG_AD_SHORT;
    }

    /* Handle Short AD writes */
    if (ad_type == UDF_ICB_FLAG_AD_SHORT) {
        if (udf_write_extent_data(dev, disk_fe, offset, size, data) != 0) {
            kfree(sector_buf, UDF_SECTOR_SIZE);
            return -1;
        }

        /* Recalculate tag checksum */
        uint8_t *p = (uint8_t *)&disk_fe->tag;
        uint8_t sum = 0;
        for (int i = 0; i < 4; i++) sum += p[i];
        for (int i = 5; i < 16; i++) sum += p[i];
        disk_fe->tag.tag_checksum = sum;

        /* Write back FE */
        dev->write(dev, disk_off, UDF_SECTOR_SIZE, sector_buf);
        memcpy(fe, disk_fe, sizeof(struct udf_fe));
        kfree(sector_buf, UDF_SECTOR_SIZE);
        return 0;
    }
    
    kprint("UDF: Unsupported allocation type or error\n");
    kfree(sector_buf, UDF_SECTOR_SIZE);
    return -1;
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
    if ((disk_fe->icb_tag.flags & 0x7) == UDF_ICB_FLAG_AD_SHORT) {
        if (new_size > disk_fe->info_length) {
            /* Extension not fully implemented */
            kprint("UDF: Extending extent-based files not implemented\n");
            return -1;
        }

        uint8_t *alloc_area = sector_buf + sizeof(struct udf_fe) + disk_fe->ext_attr_length;
        struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;
        uint32_t num_ads = disk_fe->alloc_desc_length / sizeof(struct udf_short_ad);

        uint64_t current_offset = 0;
        uint32_t new_num_ads = 0;

        for (uint32_t i = 0; i < num_ads; i++) {
            uint32_t len = ads[i].length & 0x3FFFFFFF;
            uint32_t type = (ads[i].length >> 30) & 0x3;
            uint32_t block = ads[i].position;

            if (current_offset >= new_size) {
                /* This extent is fully beyond new_size, remove it and free blocks */
                uint32_t blocks = (len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;
                /* Free blocks if allocated (type 0 or 1, not 2) */
                if (type != 2) {
                    for (uint32_t b = 0; b < blocks; b++) {
                        udf_free_block(block + b);
                    }
                }
                /* Don't increment new_num_ads */
                continue;
            }

            if (current_offset + len > new_size) {
                /* Partial extent - truncate it */
                uint32_t new_len = (uint32_t)(new_size - current_offset);

                /* Calculate blocks to keep */
                uint32_t old_blocks = (len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;
                uint32_t new_blocks = (new_len + UDF_SECTOR_SIZE - 1) / UDF_SECTOR_SIZE;

                /* Free excess blocks */
                if (type != 2 && old_blocks > new_blocks) {
                    for (uint32_t b = new_blocks; b < old_blocks; b++) {
                        udf_free_block(block + b);
                    }
                }

                /* Update AD in place (or copy to new position if needed, but we shrink so it fits) */
                ads[new_num_ads].length = new_len | (type << 30);
                ads[new_num_ads].position = block;
                new_num_ads++;
            } else {
                /* Keep full extent */
                if (new_num_ads != i) {
                    ads[new_num_ads] = ads[i];
                }
                new_num_ads++;
            }

            current_offset += len;
        }

        disk_fe->info_length = new_size;
        disk_fe->alloc_desc_length = new_num_ads * sizeof(struct udf_short_ad);

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

    return -1;
}

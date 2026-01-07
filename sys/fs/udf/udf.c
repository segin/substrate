/*
 * udf.c - Universal Disk Format (UDF) Filesystem Driver
 *
 * Read-write UDF implementation based on ECMA-167 and OSTA UDF 2.60.
 */

#include "udf.h"
#include "../../vfs/vfs.h"
#include "../../kern/console.h"
#include <string.h>

/* UDF filesystem context (single mount for now) */
static struct udf_fs {
    fs_node_t *device;              /* Block device */
    uint32_t sector_size;           /* Usually 2048 */
    uint32_t partition_start;       /* First sector of partition */
    uint32_t partition_length;      /* Partition length in sectors */
    uint32_t logical_block_size;    /* From LVD */
    struct udf_long_ad root_icb;    /* Root directory location */
} udf_ctx;

/* Forward declarations */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data);

/*
 * Calculate tag checksum (sum of bytes 0-3 and 5-15)
 */
static uint8_t udf_tag_checksum(struct udf_tag *tag) {
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

static uint16_t udf_crc(const uint8_t *data, uint32_t len) {
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
    
    /* Verify CRC if length > 0 */
    if (tag->desc_crc_len > 0) {
        uint8_t *data = (uint8_t *)buffer + sizeof(struct udf_tag);
        uint16_t crc = udf_crc(data, tag->desc_crc_len);
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
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    /* Try sector 256 first (most common) */
    if (udf_read_tag(dev, UDF_AVDP_SECTOR, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
        if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
            memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
            return 0;
        }
    }
    
    /* TODO: Try last sector and last-256 for completeness */
    
    kprint("UDF: AVDP not found\n");
    return -1;
}

/*
 * Read Volume Descriptor Sequence
 * Parses PVD, PD, and LVD from the VDS extent
 */
int udf_read_vds(fs_node_t *dev, struct udf_extent_ad *vds_extent,
                 struct udf_pvd *pvd, struct udf_pd *pd, struct udf_lvd *lvd) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    uint32_t start = vds_extent->location;
    uint32_t count = vds_extent->length / UDF_SECTOR_SIZE;
    
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
        kprint("UDF: VDS incomplete\n");
        return -1;
    }
    
    return 0;
}

/*
 * Read File Set Descriptor from the location in LVD
 */
int udf_read_fsd(fs_node_t *dev, struct udf_fs *fs, struct udf_lvd *lvd, 
                 struct udf_fsd *fsd) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    /* FSD location is relative to partition start */
    uint32_t fsd_block = lvd->fsd_location.block;
    uint32_t fsd_sector = fs->partition_start + fsd_block;
    
    if (udf_read_tag(dev, fsd_sector, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
        kprint("UDF: Failed to read FSD\n");
        return -1;
    }
    
    if (tag.tag_id != UDF_TAG_FSD) {
        kprint("UDF: Invalid FSD tag\n");
        return -1;
    }
    
    memcpy(fsd, sector_buf, sizeof(struct udf_fsd));
    return 0;
}

/* VFS filesystem structure */
static filesystem_t udf_filesystem = {
    .name = "udf",
    .mount = udf_mount,
};

/*
 * Mount stub (will be completed later)
 */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("UDF: No device or read function\n");
        return NULL;
    }
    
    /* Find AVDP */
    struct udf_avdp avdp;
    if (udf_find_avdp(dev, &avdp) != 0) {
        return NULL;
    }
    
    kprint("UDF: Found AVDP, Main VDS at sector ");
    /* TODO: Continue mounting... */
    
    return NULL;  /* Not fully implemented yet */
}

void udf_init(void) {
    kprint("Initializing UDF Driver...\n");
    vfs_register_filesystem(&udf_filesystem);
}

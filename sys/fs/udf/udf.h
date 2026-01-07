/*
 * udf.h - Universal Disk Format (UDF) On-Disk Structures
 *
 * Based on ECMA-167 and OSTA UDF 2.60 specifications.
 * Sector size: 2048 bytes (standard optical media)
 */

#ifndef _FS_UDF_UDF_H
#define _FS_UDF_UDF_H

#include <stdint.h>

/* UDF Constants */
#define UDF_SECTOR_SIZE     2048
#define UDF_AVDP_SECTOR     256     /* Anchor at sector 256 */

/* Descriptor Tag Identifiers (ECMA-167 3/7.2) */
#define UDF_TAG_PRIMARY_VD      1
#define UDF_TAG_ANCHOR_VDP      2
#define UDF_TAG_VOLUME_PTR      3
#define UDF_TAG_IMPL_USE_VD     4
#define UDF_TAG_PARTITION_D     5
#define UDF_TAG_LOGICAL_VD      6
#define UDF_TAG_UNALLOC_SD      7
#define UDF_TAG_TERMINATING     8
#define UDF_TAG_LVID            9
#define UDF_TAG_FSD             256
#define UDF_TAG_FID             257
#define UDF_TAG_AED             258
#define UDF_TAG_IE              259
#define UDF_TAG_TE              260
#define UDF_TAG_FE              261     /* File Entry */
#define UDF_TAG_EAHD            262
#define UDF_TAG_USE             263
#define UDF_TAG_SBD             264
#define UDF_TAG_PIE             265
#define UDF_TAG_EFE             266     /* Extended File Entry */

/*
 * Descriptor Tag (ECMA-167 3/7.2)
 * All UDF descriptors begin with this 16-byte tag.
 */
struct udf_tag {
    uint16_t tag_id;            /* Tag identifier */
    uint16_t desc_version;      /* Descriptor version */
    uint8_t  tag_checksum;      /* Checksum of bytes 0-3 and 5-15 */
    uint8_t  reserved;
    uint16_t tag_serial;        /* Tag serial number */
    uint16_t desc_crc;          /* CRC of descriptor data */
    uint16_t desc_crc_len;      /* Length of CRC'd data */
    uint32_t tag_location;      /* Sector number of this descriptor */
} __attribute__((packed));

#endif /* _FS_UDF_UDF_H */

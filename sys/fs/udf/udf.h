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

/*
 * Extent Descriptor (ECMA-167 3/7.1)
 * Describes location and length of an extent.
 */
struct udf_extent_ad {
    uint32_t length;            /* Extent length in bytes */
    uint32_t location;          /* Logical block number */
} __attribute__((packed));

/*
 * Entity Identifier / Registration ID (ECMA-167 1/7.4)
 */
struct udf_regid {
    uint8_t  flags;
    char     identifier[23];
    uint8_t  suffix[8];
} __attribute__((packed));

/*
 * Character Set Specification (ECMA-167 1/7.2.1)
 */
struct udf_charspec {
    uint8_t  type;              /* 0 = CS0 (OSTA Compressed Unicode) */
    uint8_t  info[63];
} __attribute__((packed));

/*
 * Timestamp (ECMA-167 1/7.3)
 */
struct udf_timestamp {
    uint16_t type_and_tz;       /* Type (bits 12-15), timezone (bits 0-11) */
    int16_t  year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  centiseconds;
    uint8_t  hundreds_usec;
    uint8_t  microseconds;
} __attribute__((packed));

/*
 * Anchor Volume Descriptor Pointer (ECMA-167 3/10.2)
 * Located at sector 256 (and last sector, and N-256).
 */
struct udf_avdp {
    struct udf_tag tag;
    struct udf_extent_ad main_vds_extent;    /* Main VDS location */
    struct udf_extent_ad reserve_vds_extent; /* Reserve VDS location */
    uint8_t reserved[480];
} __attribute__((packed));

#endif /* _FS_UDF_UDF_H */

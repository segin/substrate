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
 * Short Allocation Descriptor (ECMA-167 4/14.14.1)
 */
struct udf_short_ad {
    uint32_t length;                /* Extent length + type in upper 2 bits */
    uint32_t position;              /* Logical block position */
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

/*
 * Primary Volume Descriptor (ECMA-167 3/10.1)
 */
struct udf_pvd {
    struct udf_tag tag;
    uint32_t vds_number;
    uint32_t pvd_number;
    char     volume_id[32];         /* dstring */
    uint16_t volume_seq_number;
    uint16_t max_volume_seq_number;
    uint16_t interchange_level;
    uint16_t max_interchange_level;
    uint32_t charset_list;
    uint32_t max_charset_list;
    char     volume_set_id[128];    /* dstring */
    struct udf_charspec desc_charset;
    struct udf_charspec expl_charset;
    struct udf_extent_ad volume_abstract;
    struct udf_extent_ad volume_copyright;
    struct udf_regid app_id;
    struct udf_timestamp recording_time;
    struct udf_regid impl_id;
    uint8_t  impl_use[64];
    uint32_t predecessor_vds_location;
    uint16_t flags;
    uint8_t  reserved[22];
} __attribute__((packed));

/*
 * Partition Descriptor (ECMA-167 3/10.5)
 */
/*
 * Partition Header Descriptor (ECMA-167 4/14.3)
 * Found in contents_use field of Partition Descriptor
 */
struct udf_partition_header_desc {
    struct udf_short_ad unalloc_space_table;
    struct udf_short_ad unalloc_space_bitmap;
    struct udf_short_ad partition_integrity_table;
    struct udf_short_ad freed_space_table;
    struct udf_short_ad freed_space_bitmap;
    uint8_t  reserved[88];
} __attribute__((packed));

/*
 * Partition Descriptor (ECMA-167 3/10.5)
 */
struct udf_pd {
    struct udf_tag tag;
    uint32_t vds_number;
    uint16_t partition_flags;
    uint16_t partition_number;
    struct udf_regid partition_contents;
    uint8_t  contents_use[128];     /* Contains udf_partition_header_desc */
    uint32_t access_type;           /* 1=read-only, 3=rewritable */
    uint32_t partition_start;       /* First sector of partition */
    uint32_t partition_length;      /* Length in sectors */
    struct udf_regid impl_id;
    uint8_t  impl_use[128];
    uint8_t  reserved[156];
} __attribute__((packed));

/*
 * Long Allocation Descriptor (ECMA-167 4/14.14.2)
 */
struct udf_long_ad {
    uint32_t length;                /* Extent length + flags in upper 2 bits */
    uint32_t block;                 /* Logical block (relative to partition) */
    uint16_t partition;             /* Partition reference number */
    uint8_t  impl_use[6];
} __attribute__((packed));

/*
 * Logical Volume Descriptor (ECMA-167 3/10.6)
 */
struct udf_lvd {
    struct udf_tag tag;
    uint32_t vds_number;
    struct udf_charspec desc_charset;
    char     logical_volume_id[128]; /* dstring */
    uint32_t logical_block_size;
    struct udf_regid domain_id;
    struct udf_long_ad fsd_location; /* File Set Descriptor location */
    uint32_t map_table_length;
    uint32_t num_partition_maps;
    struct udf_regid impl_id;
    uint8_t  impl_use[128];
    struct udf_extent_ad integrity_seq_extent;
    /* Followed by partition maps */
} __attribute__((packed));

/*
 * File Set Descriptor (ECMA-167 4/14.1)
 */
struct udf_fsd {
    struct udf_tag tag;
    struct udf_timestamp recording_time;
    uint16_t interchange_level;
    uint16_t max_interchange_level;
    uint32_t charset_list;
    uint32_t max_charset_list;
    uint32_t fileset_number;
    uint32_t fileset_desc_number;
    struct udf_charspec logical_vol_charset;
    char     logical_vol_id[128];
    struct udf_charspec fileset_charset;
    char     fileset_id[32];
    char     copyright_id[32];
    char     abstract_id[32];
    struct udf_long_ad root_dir_icb;   /* Root directory location */
    struct udf_regid domain_id;
    struct udf_long_ad next_extent;
    struct udf_long_ad stream_dir_icb;
    uint8_t  reserved[32];
} __attribute__((packed));



/* ICB Tag (ECMA-167 4/14.6) - Common header for file entries */
struct udf_icb_tag {
    uint32_t prior_entries;
    uint16_t strategy_type;
    uint16_t strategy_param;
    uint16_t max_entries;
    uint8_t  reserved;
    uint8_t  file_type;             /* 4=directory, 5=file, 12=symlink */
    uint32_t parent_icb_block;
    uint16_t parent_icb_partition;
    uint16_t flags;                 /* Allocation descriptor type in bits 0-2 */
} __attribute__((packed));

/* Allocation descriptor types (in ICB flags bits 0-2) */
#define UDF_ICB_FLAG_AD_SHORT   0
#define UDF_ICB_FLAG_AD_LONG    1
#define UDF_ICB_FLAG_AD_EXT     2
#define UDF_ICB_FLAG_AD_INLINE  3   /* Data embedded in allocation area */

/* File types */
#define UDF_FILETYPE_DIR        4
#define UDF_FILETYPE_FILE       5
#define UDF_FILETYPE_SYMLINK    12

/*
 * File Entry (ECMA-167 4/14.9) - UDF "inode"
 */
struct udf_fe {
    struct udf_tag tag;
    struct udf_icb_tag icb_tag;
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    uint16_t file_link_count;
    uint8_t  record_format;
    uint8_t  record_display_attrs;
    uint32_t record_length;
    uint64_t info_length;           /* File size in bytes */
    uint64_t logical_blocks;
    struct udf_timestamp access_time;
    struct udf_timestamp modify_time;
    struct udf_timestamp attr_time;
    uint32_t checkpoint;
    struct udf_long_ad ext_attr_icb;
    struct udf_regid impl_id;
    uint64_t unique_id;
    uint32_t ext_attr_length;
    uint32_t alloc_desc_length;
    /* Followed by: extended attributes, then allocation descriptors */
} __attribute__((packed));

/*
 * Extended File Entry (ECMA-167 4/14.17) - Larger version with more timestamps
 */
struct udf_efe {
    struct udf_tag tag;
    struct udf_icb_tag icb_tag;
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    uint16_t file_link_count;
    uint8_t  record_format;
    uint8_t  record_display_attrs;
    uint32_t record_length;
    uint64_t info_length;
    uint64_t object_size;
    uint64_t logical_blocks;
    struct udf_timestamp access_time;
    struct udf_timestamp modify_time;
    struct udf_timestamp create_time;   /* EFE has creation time */
    struct udf_timestamp attr_time;
    uint32_t checkpoint;
    uint32_t reserved;
    struct udf_long_ad ext_attr_icb;
    struct udf_long_ad stream_dir_icb;
    struct udf_regid impl_id;
    uint64_t unique_id;
    uint32_t ext_attr_length;
    uint32_t alloc_desc_length;
    /* Followed by: extended attributes, then allocation descriptors */
} __attribute__((packed));

/*
 * File Identifier Descriptor (ECMA-167 4/14.4) - Directory entry
 */
struct udf_fid {
    struct udf_tag tag;
    uint16_t file_version;
    uint8_t  characteristics;       /* Bit 1=hidden, bit 2=directory, bit 3=parent */
    uint8_t  file_id_length;        /* Length of filename */
    struct udf_long_ad icb;         /* Location of file entry */
    uint16_t impl_use_length;
    /* Followed by: impl_use[], file_id[], padding to 4-byte boundary */
} __attribute__((packed));

/* FID characteristics flags */
#define UDF_FID_HIDDEN      (1 << 0)
#define UDF_FID_DIRECTORY   (1 << 1)
#define UDF_FID_DELETED     (1 << 2)
#define UDF_FID_PARENT      (1 << 3)

#endif /* _FS_UDF_UDF_H */

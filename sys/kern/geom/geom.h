/*
 * geom.h - GEOM Disk Partitioning Subsystem
 *
 * Provides partition table detection (MBR, GPT, BSD Disklabel) and
 * partition device registration for block device drivers.
 *
 * Architecture:
 *   - geom_disk_t: Represents a physical or virtual disk
 *   - geom_partition_t: Represents a detected partition (slice)
 *   - geom_class_t: Partition table parser (MBR, GPT, BSD)
 *
 * Naming Convention:
 *   - Primary disk: ide0, sata0, nvme0
 *   - MBR partition: ide0p1, ide0p2, ide0p3, ide0p4
 *   - Extended: ide0p5, ide0p6, ...
 *   - BSD slice: ide0p1a, ide0p1b, ... ide0p1h
 */

#ifndef _KERN_GEOM_H
#define _KERN_GEOM_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct geom_disk;
struct geom_partition;
struct geom_class;

/*
 * ============================================================
 * Partition Type Identifiers
 * ============================================================
 */

/* MBR partition types */
#define GEOM_MBR_EMPTY          0x00
#define GEOM_MBR_FAT12          0x01
#define GEOM_MBR_FAT16_SMALL    0x04
#define GEOM_MBR_EXTENDED       0x05
#define GEOM_MBR_FAT16          0x06
#define GEOM_MBR_NTFS           0x07
#define GEOM_MBR_FAT32          0x0B
#define GEOM_MBR_FAT32_LBA      0x0C
#define GEOM_MBR_FAT16_LBA      0x0E
#define GEOM_MBR_EXTENDED_LBA   0x0F
#define GEOM_MBR_LINUX_SWAP     0x82
#define GEOM_MBR_LINUX          0x83
#define GEOM_MBR_LINUX_LVM      0x8E
#define GEOM_MBR_FREEBSD        0xA5
#define GEOM_MBR_OPENBSD        0xA6
#define GEOM_MBR_NETBSD         0xA9
#define GEOM_MBR_GPT_PROTECTIVE 0xEE
#define GEOM_MBR_EFI_SYSTEM     0xEF

/* BSD fstype values */
#define GEOM_BSD_FS_UNUSED      0
#define GEOM_BSD_FS_SWAP        1
#define GEOM_BSD_FS_V6          2
#define GEOM_BSD_FS_V7          3
#define GEOM_BSD_FS_SYSV        4
#define GEOM_BSD_FS_V71K        5
#define GEOM_BSD_FS_V8          6
#define GEOM_BSD_FS_BSDFFS      7
#define GEOM_BSD_FS_MSDOS       8
#define GEOM_BSD_FS_BSDLFS      9
#define GEOM_BSD_FS_OTHER       10
#define GEOM_BSD_FS_HPFS        11
#define GEOM_BSD_FS_ISO9660     12
#define GEOM_BSD_FS_BOOT        13
#define GEOM_BSD_FS_VINUM       14
#define GEOM_BSD_FS_RAID        15
#define GEOM_BSD_FS_ZFS         27

/*
 * ============================================================
 * Core Structures
 * ============================================================
 */

/* Maximum partitions per disk */
#define GEOM_MAX_PARTITIONS     64

/* Maximum recursion depth for nested partition tables */
#define GEOM_MAX_RECURSION      8

/*
 * Disk Provider - represents a physical or virtual disk
 */
typedef struct geom_disk {
    char name[32];              /* Device name (e.g., "ide0") */
    void *priv;                 /* Driver private data */
    
    /* Block I/O operations */
    int (*read)(struct geom_disk *d, uint64_t lba, size_t count, void *buf);
    int (*write)(struct geom_disk *d, uint64_t lba, size_t count, const void *buf);
    
    /* Disk geometry */
    uint64_t total_sectors;     /* Total sector count */
    uint32_t sector_size;       /* Bytes per sector (usually 512) */
    
    /* Partition list (populated by scanners) */
    struct geom_partition *partitions;
    int partition_count;
    
    /* Link for disk list */
    struct geom_disk *next;
} geom_disk_t;

/*
 * Partition Entry - represents a detected partition/slice
 */
typedef struct geom_partition {
    char name[32];              /* Device name (e.g., "ide0p1a") */
    
    /* Location on parent disk */
    geom_disk_t *disk;          /* Parent disk */
    uint64_t start_lba;         /* Start LBA relative to disk */
    uint64_t size_sectors;      /* Size in sectors */
    
    /* Type information */
    uint8_t mbr_type;           /* MBR partition type (if from MBR) */
    uint8_t bsd_fstype;         /* BSD fstype (if from disklabel) */
    uint8_t guid[16];           /* GPT partition type GUID */
    char type_name[32];         /* Human-readable type name */
    
    /* Flags */
    uint32_t flags;
#define GEOM_PART_BOOTABLE      0x0001
#define GEOM_PART_ACTIVE        0x0002
#define GEOM_PART_CONTAINER     0x0004  /* Contains nested partition table */
    
    /* Link for partition list */
    struct geom_partition *next;
} geom_partition_t;

/*
 * GEOM Class - partition table parser
 */
typedef struct geom_class {
    const char *name;           /* Class name (e.g., "MBR", "GPT", "BSD") */
    int priority;               /* Higher priority checked first (GPT > MBR) */
    
    /*
     * Sniff function - detect and parse partition table
     *
     * disk: Target disk
     * offset: LBA offset to check (0 for root, partition start for nested)
     * depth: Recursion depth (0 for root)
     * prefix: Name prefix for partitions (e.g., "ide0p1" for BSD inside MBR)
     *
     * Returns: 0 on success (found and parsed), -1 if not this format
     */
    int (*sniff)(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix);
    
    /* Link for class list */
    struct geom_class *next;
} geom_class_t;

/*
 * ============================================================
 * API Functions
 * ============================================================
 */

/* Initialization */
void geom_init(void);

/* Class registration */
void geom_register_class(geom_class_t *cls);

/* Disk registration - triggers partition scan */
void geom_register_disk(geom_disk_t *disk);

/* Partition registration - called by scanners */
geom_partition_t *geom_add_partition(geom_disk_t *disk, const char *name,
                                      uint64_t start, uint64_t size,
                                      uint8_t mbr_type, uint8_t bsd_fstype,
                                      const uint8_t *guid, const char *type_name,
                                      uint32_t flags);

/* Recursive scan - for nested partition tables */
void geom_scan(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix);

/* Helper - read single sector from disk */
int geom_read_sector(geom_disk_t *disk, uint64_t lba, void *buf);

/* Helper - read multiple sectors from disk */
int geom_read_sectors(geom_disk_t *disk, uint64_t lba, size_t count, void *buf);

/* Lookup partition by name */
geom_partition_t *geom_find_partition(const char *name);

/* Get partition count for a disk */
int geom_get_partition_count(geom_disk_t *disk);

/* GUID comparison helpers */
int geom_guid_equal(const uint8_t *a, const uint8_t *b);
int geom_guid_is_zero(const uint8_t *guid);

/*
 * ============================================================
 * On-Disk Structures (packed for correct memory layout)
 * ============================================================
 */

/* MBR Partition Entry */
struct geom_mbr_entry {
    uint8_t  status;            /* 0x80 = bootable */
    uint8_t  chs_start[3];      /* CHS start address */
    uint8_t  type;              /* Partition type */
    uint8_t  chs_end[3];        /* CHS end address */
    uint32_t lba_start;         /* LBA start sector */
    uint32_t lba_size;          /* Size in sectors */
} __attribute__((packed));

/* Master Boot Record */
struct geom_mbr {
    uint8_t  boot_code[446];    /* Boot code */
    struct geom_mbr_entry entries[4];  /* Partition entries */
    uint16_t signature;         /* 0xAA55 */
} __attribute__((packed));

/* GPT Header */
struct geom_gpt_header {
    uint8_t  signature[8];      /* "EFI PART" */
    uint32_t revision;          /* GPT revision */
    uint32_t header_size;       /* Header size (usually 92) */
    uint32_t header_crc32;      /* CRC32 of header */
    uint32_t reserved;          /* Must be zero */
    uint64_t my_lba;            /* LBA of this header */
    uint64_t alternate_lba;     /* LBA of alternate header */
    uint64_t first_usable_lba;  /* First usable LBA */
    uint64_t last_usable_lba;   /* Last usable LBA */
    uint8_t  disk_guid[16];     /* Disk GUID */
    uint64_t partition_lba;     /* LBA of partition entry array */
    uint32_t num_entries;       /* Number of partition entries */
    uint32_t entry_size;        /* Size of each entry (usually 128) */
    uint32_t entries_crc32;     /* CRC32 of partition entries */
} __attribute__((packed));

/* GPT Partition Entry */
struct geom_gpt_entry {
    uint8_t  type_guid[16];     /* Partition type GUID */
    uint8_t  part_guid[16];     /* Unique partition GUID */
    uint64_t start_lba;         /* Starting LBA */
    uint64_t end_lba;           /* Ending LBA (inclusive) */
    uint64_t attributes;        /* Attribute flags */
    uint16_t name[36];          /* Partition name (UTF-16LE) */
} __attribute__((packed));

/* BSD Disklabel Magic */
#define GEOM_BSD_DISKMAGIC      0x82564557
#define GEOM_BSD_LABEL_OFFSET   0       /* Offset in sector 1 */
#define GEOM_BSD_LABEL_OFFSET2  64      /* Alternate offset */

/* BSD Partition Entry */
struct geom_bsd_partition {
    uint32_t p_size;            /* Number of sectors */
    uint32_t p_offset;          /* Starting sector */
    uint32_t p_fsize;           /* Filesystem fragment size */
    uint8_t  p_fstype;          /* Filesystem type */
    uint8_t  p_frag;            /* Fragments per block */
    uint16_t p_cpg;             /* Cylinders per group */
} __attribute__((packed));

/* BSD Disklabel */
struct geom_bsd_disklabel {
    uint32_t d_magic;           /* Magic number */
    uint16_t d_type;            /* Drive type */
    uint16_t d_subtype;         /* Subtype */
    char     d_typename[16];    /* Type name */
    char     d_packname[16];    /* Pack identifier */
    uint32_t d_secsize;         /* Bytes per sector */
    uint32_t d_nsectors;        /* Sectors per track */
    uint32_t d_ntracks;         /* Tracks per cylinder */
    uint32_t d_ncylinders;      /* Cylinders per unit */
    uint32_t d_secpercyl;       /* Sectors per cylinder */
    uint32_t d_secperunit;      /* Sectors per unit */
    uint16_t d_sparespertrack;  /* Spare sectors per track */
    uint16_t d_sparespercyl;    /* Spare sectors per cylinder */
    uint32_t d_acylinders;      /* Alternate cylinders */
    uint16_t d_rpm;             /* Rotational speed */
    uint16_t d_interleave;      /* Hardware sector interleave */
    uint16_t d_trackskew;       /* Sector 0 skew per track */
    uint16_t d_cylskew;         /* Sector 0 skew per cylinder */
    uint32_t d_headswitch;      /* Head switch time (usec) */
    uint32_t d_trkseek;         /* Track-to-track seek (usec) */
    uint32_t d_flags;           /* Generic flags */
    uint32_t d_drivedata[5];    /* Drive-type specific data */
    uint32_t d_spare[5];        /* Reserved */
    uint32_t d_magic2;          /* Magic number (again) */
    uint16_t d_checksum;        /* Checksum */
    uint16_t d_npartitions;     /* Number of partitions */
    uint32_t d_bbsize;          /* Boot block size */
    uint32_t d_sbsize;          /* Superblock size */
    struct geom_bsd_partition d_partitions[8];  /* Partitions a-h */
} __attribute__((packed));

/*
 * ============================================================
 * Well-Known GPT Type GUIDs
 * ============================================================
 */

/* EFI System Partition */
extern const uint8_t GEOM_GPT_TYPE_EFI_SYSTEM[16];
/* Microsoft Basic Data */
extern const uint8_t GEOM_GPT_TYPE_MS_BASIC_DATA[16];
/* Linux Filesystem */
extern const uint8_t GEOM_GPT_TYPE_LINUX_FS[16];
/* Linux Swap */
extern const uint8_t GEOM_GPT_TYPE_LINUX_SWAP[16];
/* FreeBSD UFS */
extern const uint8_t GEOM_GPT_TYPE_FREEBSD_UFS[16];
/* FreeBSD ZFS */
extern const uint8_t GEOM_GPT_TYPE_FREEBSD_ZFS[16];

#endif /* _KERN_GEOM_H */

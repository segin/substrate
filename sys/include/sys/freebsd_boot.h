#ifndef _SYS_FREEBSD_BOOT_H
#define _SYS_FREEBSD_BOOT_H

#include <stdint.h>

/*
 * FreeBSD bootinfo structure (i386)
 * From FreeBSD sys/i386/include/bootinfo.h
 */

/* Only change the version number if you break compatibility. */
#define BOOTINFO_VERSION        1

#define N_BIOS_GEOM_OLD         8

struct bootinfo {
    uint32_t bi_version;           /* Must be 1 */
    uint32_t bi_kernelname;        /* represents a char * */
    uint32_t bi_nfs_diskless;      /* struct nfs_diskless * */
    uint32_t _was_bi_n_bios_used;  /* obsolete */
    uint32_t _was_bi_bios_geom[N_BIOS_GEOM_OLD]; /* obsolete BIOS geometry */
    uint32_t bi_size;              /* size of this structure */
    uint8_t  bi_memsizes_valid;    /* are basemem/extmem valid? */
    uint8_t  bi_bios_dev;          /* bootdev BIOS unit number */
    uint8_t  bi_pad[2];            /* alignment padding */
    uint32_t bi_basemem;           /* base memory in KB */
    uint32_t bi_extmem;            /* extended memory in KB */
    uint32_t bi_symtab;            /* struct symtab * (kernel symbols) */
    uint32_t bi_esymtab;           /* end of kernel symbols */
    /* Items below only from advanced bootloader */
    uint32_t bi_kernend;           /* end of kernel space */
    uint32_t bi_envp;              /* environment pointer */
    uint32_t bi_modulep;           /* preloaded modules */
};

/*
 * Constants for converting boot-style device number to type,
 * adaptor, unit number and partition number.
 * Format:
 *    (4)   (8)   (4)  (8)     (8)
 *   --------------------------------
 *   |MA | SLICE | UN| PART  | TYPE |
 *   --------------------------------
 */
#define B_SLICESHIFT            20
#define B_SLICEMASK             0xff
#define B_SLICE(val)            (((val) >> B_SLICESHIFT) & B_SLICEMASK)
#define B_UNITSHIFT             16
#define B_UNITMASK              0xf
#define B_UNIT(val)             (((val) >> B_UNITSHIFT) & B_UNITMASK)
#define B_PARTITIONSHIFT        8
#define B_PARTITIONMASK         0xff
#define B_PARTITION(val)        (((val) >> B_PARTITIONSHIFT) & B_PARTITIONMASK)
#define B_TYPESHIFT             0
#define B_TYPEMASK              0xff
#define B_TYPE(val)             (((val) >> B_TYPESHIFT) & B_TYPEMASK)

#define B_MAGICMASK             0xf0000000
#define B_DEVMAGIC              0xa0000000

#define MAKEBOOTDEV(type, slice, unit, partition) \
    (((type) << B_TYPESHIFT) | ((slice) << B_SLICESHIFT) | \
     ((unit) << B_UNITSHIFT) | ((partition) << B_PARTITIONSHIFT) | \
     B_DEVMAGIC)

#define BASE_SLICE              2
#define COMPATIBILITY_SLICE     0
#define MAX_SLICES              32
#define WHOLE_DISK_SLICE        1

#define BOOTINFO_VERSION	1
#define BOOTINFO_MAGIC		0x12345678
#define FREEBSD_LOADER_MAGIC    0xF8EEB5D0

#endif

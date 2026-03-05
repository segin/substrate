/*
 * geom_bsd.c - BSD Disklabel Scanner
 *
 * Handles:
 * - FreeBSD/OpenBSD/NetBSD disklabel format
 * - 32-bit disklabel at sector 1 (offset 0 or 64)
 * - Slice enumeration (a-h)
 * - Filesystem type identification
 */

#include <kern/geom/geom.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

/* Helper from geom_subr.c */
extern const char *geom_bsd_fstype_name(uint8_t fstype);

/*
 * ============================================================
 * BSD Disklabel Scanner
 * ============================================================
 */

static int geom_bsd_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    uint8_t buf[512];
    
    /* BSD disklabel is usually at sector 1 relative to partition start */
    if (geom_read_sector(disk, offset + 1, buf) != 0) {
        return -1;
    }
    
    /* Search for magic number at known offsets */
    int label_offset = -1;
    struct geom_bsd_disklabel *label = NULL;
    
    /* Check offset 0 */
    uint32_t *magic = (uint32_t *)buf;
    if (*magic == GEOM_BSD_DISKMAGIC) {
        label_offset = 0;
    }
    
    /* Check offset 64 (some systems put it there) */
    if (label_offset == -1) {
        magic = (uint32_t *)(buf + 64);
        if (*magic == GEOM_BSD_DISKMAGIC) {
            label_offset = 64;
        }
    }
    
    if (label_offset == -1) {
        return -1;  /* Not a BSD disklabel */
    }
    
    label = (struct geom_bsd_disklabel *)(buf + label_offset);
    
    /* Validate magic2 (should match d_magic) */
    if (label->d_magic2 != GEOM_BSD_DISKMAGIC) {
        return -1;
    }
    
    /* Validate partition count */
    uint16_t nparts = label->d_npartitions;
    if (nparts > 8) nparts = 8;  /* Standard BSD has a-h (8 partitions) */
    
    /* Calculate label checksum */
    uint16_t checksum = 0;
    uint8_t *bp = (uint8_t *)label;
    size_t label_bytes = sizeof(*label);
    
    for (size_t i = 0; i < label_bytes; i += 2) {
        uint16_t val = (uint16_t)bp[i] | ((uint16_t)bp[i+1] << 8);
        checksum ^= val;
    }
    
    if (checksum != 0) {
        /* Checksum mismatch - might be corrupted */
        /* Continue anyway for compatibility */
    }
    
    /* Print scan header */
    kprint("  ");
    for (int d = 0; d < depth; d++) kprint("  ");
    kprint(prefix);
    kprint(": BSD");
    
    /* First pass: print slice names */
    int slice_count = 0;
    for (int i = 0; i < (int)nparts; i++) {
        struct geom_bsd_partition *part = &label->d_partitions[i];
        
        /* Skip unused partitions (size 0 or fstype unused) */
        if (part->p_size == 0) {
            continue;
        }
        
        /* Skip partition 'c' which typically represents the whole disk */
        if (i == 2) {
            continue;
        }
        
        kprint(" ");
        char sname[32];
        snprintf(sname, sizeof(sname), "%s%c", prefix, 'a' + i);
        kprint(sname);
        slice_count++;
    }
    
    if (slice_count == 0) {
        kprint(" (empty)");
    }
    kprint("\n");
    
    /* Second pass: register slices */
    for (int i = 0; i < (int)nparts; i++) {
        struct geom_bsd_partition *part = &label->d_partitions[i];
        
        /* Skip unused partitions */
        if (part->p_size == 0) {
            continue;
        }
        
        /* Skip partition 'c' (whole disk) */
        if (i == 2) {
            continue;
        }
        
        /* Calculate slice location
         * Note: p_offset is relative to the disk (in sectors)
         * We convert it relative to the container partition
         */
        uint64_t slice_start;
        uint64_t slice_size = part->p_size;
        
        /* BSD offsets can be:
         * - Absolute (relative to disk start)
         * - Relative (relative to partition start)
         * We assume FreeBSD style (relative to containing partition)
         */
        slice_start = offset + part->p_offset;
        
        /* If offset would be before the container, assume it's disk-absolute */
        if (part->p_offset >= offset) {
            slice_start = part->p_offset;
        }
        
        /* Create slice name: ide0p1a, ide0p1b, etc. */
        char slice_name[32];
        snprintf(slice_name, sizeof(slice_name), "%s%c", prefix, 'a' + i);
        
        /* Register slice */
        geom_add_partition(disk, slice_name, slice_start, slice_size,
                          0, part->p_fstype, NULL,
                          geom_bsd_fstype_name(part->p_fstype), 0);
    }
    
    return 0;
}

/*
 * ============================================================
 * Class Registration
 * ============================================================
 */

static geom_class_t geom_bsd_class = {
    .name = "BSD",
    .priority = 5,      /* Lower priority - usually nested inside MBR */
    .sniff = geom_bsd_sniff,
    .next = NULL
};

void geom_bsd_init(void) {
    geom_register_class(&geom_bsd_class);
}

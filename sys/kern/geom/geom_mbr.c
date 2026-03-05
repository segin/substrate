/*
 * geom_mbr.c - MBR (Master Boot Record) Partition Table Scanner
 *
 * Handles:
 * - Standard MBR with 4 primary partitions
 * - Extended partitions (type 0x05/0x0F) with linked EBR chain
 * - BSD disklabel containers (type 0xA5)
 * - GPT protective MBR detection (type 0xEE)
 */

#include <kern/geom/geom.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration for recursive scan */
extern void geom_scan(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix);
extern const char *geom_mbr_type_name(uint8_t type);

/*
 * ============================================================
 * Extended Partition Chain Parsing
 * ============================================================
 */

/*
 * Parse Extended Boot Record (EBR) chain
 *
 * Extended partitions form a linked list of EBRs:
 * - Entry 0: Logical partition (relative to this EBR)
 * - Entry 1: Next EBR in chain (relative to extended partition start)
 * - Entries 2-3: Must be empty
 */
static int parse_extended(geom_disk_t *disk, uint64_t ext_start, uint64_t ext_size,
                          const char *base_name, int *part_num) {
    uint64_t ebr_lba = ext_start;
    uint8_t buf[512];
    int logical_count = 0;
    
    while (ebr_lba < ext_start + ext_size && logical_count < 60) {
        /* Read EBR */
        if (geom_read_sector(disk, ebr_lba, buf) != 0) {
            kprintf("GEOM: failed to read EBR at LBA %llu\n", (unsigned long long)ebr_lba);
            break;
        }
        
        struct geom_mbr *ebr = (struct geom_mbr *)buf;
        
        /* Validate signature */
        if (ebr->signature != 0xAA55) {
            break;
        }
        
        /* Entry 0: Logical partition */
        struct geom_mbr_entry *entry = &ebr->entries[0];
        if (entry->type != 0 && entry->lba_size > 0) {
            /* Logical partition LBA is relative to this EBR */
            uint64_t part_start = ebr_lba + entry->lba_start;
            uint64_t part_size = entry->lba_size;
            
            /* Create partition name: ide0p5, ide0p6, etc. */
            char part_name[32];
            snprintf(part_name, sizeof(part_name), "%sp%d", base_name, *part_num);
            (*part_num)++;
            
            /* Determine if this is a container type */
            uint32_t flags = 0;
            if (entry->type == GEOM_MBR_FREEBSD ||
                entry->type == GEOM_MBR_OPENBSD ||
                entry->type == GEOM_MBR_NETBSD) {
                flags = GEOM_PART_CONTAINER;
            }
            
            /* Register partition */
            geom_add_partition(disk, part_name, part_start, part_size,
                              entry->type, 0, NULL,
                              geom_mbr_type_name(entry->type), flags);
            
            logical_count++;
            
            /* For BSD containers, scan for nested disklabel */
            if (flags & GEOM_PART_CONTAINER) {
                geom_scan(disk, part_start, 1, part_name);
            }
        }
        
        /* Entry 1: Link to next EBR */
        entry = &ebr->entries[1];
        if (entry->type == GEOM_MBR_EXTENDED || 
            entry->type == GEOM_MBR_EXTENDED_LBA) {
            /* Next EBR is relative to extended partition start */
            ebr_lba = ext_start + entry->lba_start;
        } else {
            break;  /* End of chain */
        }
    }
    
    return logical_count;
}

/*
 * ============================================================
 * MBR Scanner
 * ============================================================
 */

static int geom_mbr_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    uint8_t buf[512];
    
    /* Read sector at offset */
    if (geom_read_sector(disk, offset, buf) != 0) {
        return -1;
    }
    
    struct geom_mbr *mbr = (struct geom_mbr *)buf;
    
    /* Validate MBR signature */
    if (mbr->signature != 0xAA55) {
        return -1;
    }
    
    /* Check for GPT Protective MBR - if found, defer to GPT parser */
    for (int i = 0; i < 4; i++) {
        if (mbr->entries[i].type == GEOM_MBR_GPT_PROTECTIVE) {
            /* This is a GPT disk with protective MBR - let GPT parser handle it */
            return -1;
        }
    }
    
    /* Validate that at least one entry is non-empty */
    int has_partitions = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr->entries[i].type != 0) {
            has_partitions = 1;
            break;
        }
    }
    
    if (!has_partitions && depth == 0) {
        /* Empty MBR - might be uninitialized disk */
        return -1;
    }
    
    /* Print scan header */
    if (depth == 0) {
        kprint("  ");
        kprint(disk->name);
        kprint(": MBR");
    } else {
        kprint("  ");
        for (int d = 0; d < depth; d++) kprint("  ");
        kprint(prefix);
        kprint(": MBR");
    }
    
    /* Track extended partition for later processing */
    int extended_found = 0;
    uint64_t extended_start = 0;
    uint64_t extended_size = 0;
    
    /* First pass: Print all partition names */
    int first = 1;
    for (int i = 0; i < 4; i++) {
        struct geom_mbr_entry *entry = &mbr->entries[i];
        
        if (entry->type != 0 && entry->lba_size > 0) {
            if (!first) kprint(" ");
            first = 0;
            
            char pname[32];
            snprintf(pname, sizeof(pname), "%sp%d", prefix, i + 1);
            kprint(pname);
        }
    }
    kprint("\n");
    
    /* Second pass: Register partitions and handle special types */
    for (int i = 0; i < 4; i++) {
        struct geom_mbr_entry *entry = &mbr->entries[i];
        
        if (entry->type == 0 || entry->lba_size == 0) {
            continue;
        }
        
        uint64_t part_start = offset + entry->lba_start;
        uint64_t part_size = entry->lba_size;
        
        char part_name[32];
        snprintf(part_name, sizeof(part_name), "%sp%d", prefix, i + 1);
        
        /* Determine flags */
        uint32_t flags = 0;
        if (entry->status == 0x80) {
            flags |= GEOM_PART_BOOTABLE | GEOM_PART_ACTIVE;
        }
        
        if (entry->type == GEOM_MBR_EXTENDED ||
            entry->type == GEOM_MBR_EXTENDED_LBA) {
            /* Extended partition - save for later */
            extended_found = 1;
            extended_start = part_start;
            extended_size = part_size;
            flags |= GEOM_PART_CONTAINER;
        } else if (entry->type == GEOM_MBR_FREEBSD ||
                   entry->type == GEOM_MBR_OPENBSD ||
                   entry->type == GEOM_MBR_NETBSD) {
            flags |= GEOM_PART_CONTAINER;
        }
        
        /* Register the partition */
        geom_add_partition(disk, part_name, part_start, part_size,
                          entry->type, 0, NULL,
                          geom_mbr_type_name(entry->type), flags);
        
        /* For BSD partitions, recursively scan for disklabel */
        if ((entry->type == GEOM_MBR_FREEBSD ||
             entry->type == GEOM_MBR_OPENBSD ||
             entry->type == GEOM_MBR_NETBSD) &&
            depth < GEOM_MAX_RECURSION) {
            geom_scan(disk, part_start, depth + 1, part_name);
        }
    }
    
    /* Process extended partition chain */
    if (extended_found) {
        int next_part = 5;  /* Logical partitions start at 5 */
        parse_extended(disk, extended_start, extended_size, prefix, &next_part);
    }
    
    return 0;
}

/*
 * ============================================================
 * Class Registration
 * ============================================================
 */

static geom_class_t geom_mbr_class = {
    .name = "MBR",
    .priority = 10,     /* Lower priority than GPT */
    .sniff = geom_mbr_sniff,
    .next = NULL
};

void geom_mbr_init(void) {
    geom_register_class(&geom_mbr_class);
}

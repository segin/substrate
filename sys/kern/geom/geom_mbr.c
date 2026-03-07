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

static void geom_summary_append(char *buf, size_t buf_size, int *first, const char *name) {
    size_t len;

    if (!buf || !buf_size || !first || !name || !name[0]) return;
    len = strlen(buf);
    if (len >= buf_size - 1) return;

    if (!*first) {
        snprintf(buf + len, buf_size - len, " %s", name);
    } else {
        snprintf(buf + len, buf_size - len, "%s", name);
        *first = 0;
    }
}

typedef struct mbr_logical_part {
    char name[32];
    uint64_t start;
    uint64_t size;
    uint8_t type;
    uint32_t flags;
} mbr_logical_part_t;

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
                          const char *base_name, int *part_num,
                          mbr_logical_part_t *logicals, size_t logical_cap,
                          char *summary, size_t summary_size, int *summary_first) {
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
            char part_name[sizeof(logicals[0].name)];
            mbr_logical_part_t *logical = NULL;

            if ((size_t)logical_count >= logical_cap) {
                break;
            }

            logical = &logicals[logical_count];
            memset(logical, 0, sizeof(*logical));
            snprintf(part_name, sizeof(part_name), "%sp%d", base_name, *part_num);
            (*part_num)++;
            geom_summary_append(summary, summary_size, summary_first, part_name);
            
            /* Determine if this is a container type */
            logical->flags = 0;
            if (entry->type == GEOM_MBR_FREEBSD ||
                entry->type == GEOM_MBR_OPENBSD ||
                entry->type == GEOM_MBR_NETBSD) {
                logical->flags = GEOM_PART_CONTAINER;
            }

            strncpy(logical->name, part_name, sizeof(logical->name) - 1);
            logical->start = part_start;
            logical->size = part_size;
            logical->type = entry->type;
            logical_count++;
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
    mbr_logical_part_t logicals[60];
    
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
    
    /* Track extended partition for later processing */
    int extended_found = 0;
    uint64_t extended_start = 0;
    uint64_t extended_size = 0;
    char summary[256] = {0};
    int first = 1;
    int next_part = 5;
    int logical_count = 0;

    memset(logicals, 0, sizeof(logicals));

    /* First pass: collect visible primary partition names and extended metadata. */
    for (int i = 0; i < 4; i++) {
        struct geom_mbr_entry *entry = &mbr->entries[i];
        
        if (entry->type != 0 && entry->lba_size > 0) {
            char pname[32];
            snprintf(pname, sizeof(pname), "%sp%d", prefix, i + 1);
            geom_summary_append(summary, sizeof(summary), &first, pname);

            if (entry->type == GEOM_MBR_EXTENDED ||
                entry->type == GEOM_MBR_EXTENDED_LBA) {
                extended_found = 1;
                extended_start = offset + entry->lba_start;
                extended_size = entry->lba_size;
            }
        }
    }

    if (extended_found) {
        logical_count = parse_extended(disk, extended_start, extended_size, prefix, &next_part,
                                      logicals, sizeof(logicals) / sizeof(logicals[0]),
                                      summary, sizeof(summary), &first);
    }

    if (summary[0]) {
        kprint("  ");
        for (int d = 0; d < depth; d++) kprint("  ");
        kprint(prefix);
        kprint(": ");
        kprint(summary);
        kprint("\n");
    }

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

    for (int i = 0; i < logical_count; i++) {
        geom_add_partition(disk, logicals[i].name, logicals[i].start, logicals[i].size,
                          logicals[i].type, 0, NULL,
                          geom_mbr_type_name(logicals[i].type), logicals[i].flags);

        if ((logicals[i].flags & GEOM_PART_CONTAINER) && depth < GEOM_MAX_RECURSION) {
            geom_scan(disk, logicals[i].start, depth + 1, logicals[i].name);
        }
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

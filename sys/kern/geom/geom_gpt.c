/*
 * geom_gpt.c - GPT (GUID Partition Table) Scanner
 *
 * Handles:
 * - GPT header validation at LBA 1
 * - CRC32 checksum verification
 * - Partition entry enumeration
 * - GUID-based type identification
 */

#include <kern/geom/geom.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>
#include <sys/crc32.h>

/*
 * ============================================================
 * GPT Type GUID to Name Mapping
 * ============================================================
 */

/* Helper from geom_subr.c */
extern int geom_guid_equal(const uint8_t *a, const uint8_t *b);
extern int geom_guid_is_zero(const uint8_t *guid);

static const char *gpt_type_name(const uint8_t *guid) {
    /* Check for well-known GUIDs */
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_EFI_SYSTEM)) {
        return "EFI System";
    }
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_MS_BASIC_DATA)) {
        return "Basic Data";
    }
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_LINUX_FS)) {
        return "Linux";
    }
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_LINUX_SWAP)) {
        return "Linux Swap";
    }
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_FREEBSD_UFS)) {
        return "FreeBSD UFS";
    }
    if (geom_guid_equal(guid, GEOM_GPT_TYPE_FREEBSD_ZFS)) {
        return "FreeBSD ZFS";
    }
    
    return "Unknown";
}

/*
 * ============================================================
 * GPT Scanner
 * ============================================================
 */

static int geom_gpt_sniff(geom_disk_t *disk, uint64_t offset, int depth, const char *prefix) {
    /* GPT is only valid at disk start (offset 0) */
    if (offset != 0) {
        return -1;
    }
    
    /* GPT doesn't nest inside other partition tables */
    if (depth > 0) {
        return -1;
    }
    
    uint8_t buf[512];
    
    /* Read GPT header at LBA 1 */
    if (geom_read_sector(disk, 1, buf) != 0) {
        return -1;
    }
    
    struct geom_gpt_header *hdr = (struct geom_gpt_header *)buf;
    
    /* Validate GPT signature: "EFI PART" */
    if (memcmp(hdr->signature, "EFI PART", 8) != 0) {
        return -1;
    }
    
    /* Validate revision (should be 0x00010000 for 1.0) */
    if (hdr->revision < 0x00010000) {
        return -1;
    }
    
    /* Validate header size (minimum 92 bytes, usually exactly 92) */
    if (hdr->header_size < 92 || hdr->header_size > 512) {
        return -1;
    }
    
    /* Validate header CRC32 */
    uint32_t saved_crc = hdr->header_crc32;
    hdr->header_crc32 = 0;
    uint32_t computed_crc = crc32(hdr, hdr->header_size);
    hdr->header_crc32 = saved_crc;
    
    if (computed_crc != saved_crc) {
        kprint("  ");
        kprint(disk->name);
        kprint(": GPT header CRC mismatch\n");
        return -1;
    }
    
    /* Validate my_lba points to LBA 1 */
    if (hdr->my_lba != 1) {
        return -1;
    }
    
    /* Validate partition entry parameters */
    if (hdr->num_entries == 0 || hdr->entry_size < 128) {
        return -1;
    }
    
    /* Limit entries to prevent excessive scanning */
    uint32_t max_entries = hdr->num_entries;
    if (max_entries > 128) max_entries = 128;
    
    /* Read partition entries */
    uint32_t entries_per_sector = 512 / hdr->entry_size;
    uint32_t entry_sectors = (max_entries + entries_per_sector - 1) / entries_per_sector;
    
    /* Allocate buffer for partition entries (max 16KB for 128 entries @ 128 bytes) */
    static uint8_t entry_buf[512 * 32];  /* Up to 32 sectors */
    if (entry_sectors > 32) entry_sectors = 32;
    
    if (geom_read_sectors(disk, hdr->partition_lba, entry_sectors, entry_buf) != 0) {
        kprint("  ");
        kprint(disk->name);
        kprint(": failed to read GPT entries\n");
        return -1;
    }
    
    /* Validate entries CRC32 */
    uint32_t entries_crc = crc32(entry_buf, max_entries * hdr->entry_size);
    if (entries_crc != hdr->entries_crc32) {
        kprint("  ");
        kprint(disk->name);
        kprint(": GPT entries CRC mismatch\n");
        /* Continue anyway - some tools don't update CRC properly */
    }
    
    /* Print header */
    kprint("  ");
    kprint(disk->name);
    kprint(": GPT");
    
    /* First pass: count and print partition names */
    int part_count = 0;
    for (uint32_t i = 0; i < max_entries; i++) {
        struct geom_gpt_entry *entry = (struct geom_gpt_entry *)(entry_buf + i * hdr->entry_size);
        
        /* Skip empty entries */
        if (geom_guid_is_zero(entry->type_guid)) {
            continue;
        }
        
        kprint(" ");
        char pname[32];
        sprintf("%sp%d", prefix, part_count + 1);
        kprint(pname);
        part_count++;
    }
    
    if (part_count == 0) {
        kprint(" (empty)");
    }
    kprint("\n");
    
    /* Second pass: register partitions */
    int part_num = 1;
    for (uint32_t i = 0; i < max_entries; i++) {
        struct geom_gpt_entry *entry = (struct geom_gpt_entry *)(entry_buf + i * hdr->entry_size);
        
        /* Skip empty entries */
        if (geom_guid_is_zero(entry->type_guid)) {
            continue;
        }
        
        /* Calculate partition size (end_lba is inclusive) */
        uint64_t start = entry->start_lba;
        uint64_t size = entry->end_lba - entry->start_lba + 1;
        
        /* Create partition name */
        char part_name[32];
        sprintf("%sp%d", prefix, part_num);
        part_num++;
        
        /* Register partition */
        geom_add_partition(disk, part_name, start, size,
                          0, 0, entry->type_guid,
                          gpt_type_name(entry->type_guid), 0);
    }
    
    return 0;
}

/*
 * ============================================================
 * Class Registration
 * ============================================================
 */

static geom_class_t geom_gpt_class = {
    .name = "GPT",
    .priority = 20,     /* Higher priority than MBR */
    .sniff = geom_gpt_sniff,
    .next = NULL
};

void geom_gpt_init(void) {
    geom_register_class(&geom_gpt_class);
}

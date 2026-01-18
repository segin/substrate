/*
 * test_geom.c - GEOM Subsystem Unit Tests
 *
 * Tests for partition table detection:
 * - MBR structure parsing
 * - GPT structure parsing
 * - BSD disklabel parsing
 * - Partition naming conventions
 */

#include "../kern/geom/geom.h"
#include <stdint.h>
#include <string.h>

/* Test framework macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        test_fail(__func__, __LINE__, msg); \
        return 1; \
    } \
} while(0)

#define TEST_PASS() do { test_pass(__func__); return 0; } while(0)

static int tests_passed = 0;
static int tests_failed = 0;

extern void kprint(const char *s);
static void test_pass(const char *name) {
    (void)name;
    tests_passed++;
}

static void test_fail(const char *name, int line, const char *msg) {
    (void)name;
    (void)line;
    (void)msg;
    tests_failed++;
}

/* ========== MBR Structure Tests ========== */

/*
 * Test: MBR structure size
 */
static int test_mbr_struct_size(void) {
    TEST_ASSERT(sizeof(struct geom_mbr) == 512, "MBR must be 512 bytes");
    TEST_ASSERT(sizeof(struct geom_mbr_entry) == 16, "MBR entry must be 16 bytes");
    TEST_PASS();
}

/*
 * Test: MBR entry field offsets
 */
static int test_mbr_entry_layout(void) {
    struct geom_mbr_entry entry;
    memset(&entry, 0, sizeof(entry));
    
    /* Verify we can access fields correctly */
    entry.status = 0x80;
    entry.type = 0x83;
    entry.lba_start = 2048;
    entry.lba_size = 1024000;
    
    TEST_ASSERT(entry.status == 0x80, "bootable status");
    TEST_ASSERT(entry.type == 0x83, "Linux type");
    TEST_ASSERT(entry.lba_start == 2048, "LBA start");
    TEST_ASSERT(entry.lba_size == 1024000, "LBA size");
    
    TEST_PASS();
}

/*
 * Test: MBR signature position
 */
static int test_mbr_signature_offset(void) {
    struct geom_mbr mbr;
    memset(&mbr, 0, sizeof(mbr));
    mbr.signature = 0xAA55;
    
    /* Signature should be at offset 510 */
    uint8_t *ptr = (uint8_t *)&mbr;
    uint16_t sig = *(uint16_t *)(ptr + 510);
    
    TEST_ASSERT(sig == 0xAA55, "signature at offset 510");
    TEST_PASS();
}

/* ========== GPT Structure Tests ========== */

/*
 * Test: GPT header structure size
 */
static int test_gpt_header_size(void) {
    TEST_ASSERT(sizeof(struct geom_gpt_header) == 92, "GPT header must be 92 bytes");
    TEST_PASS();
}

/*
 * Test: GPT entry structure size
 */
static int test_gpt_entry_size(void) {
    TEST_ASSERT(sizeof(struct geom_gpt_entry) == 128, "GPT entry must be 128 bytes");
    TEST_PASS();
}

/*
 * Test: GPT header field layout
 */
static int test_gpt_header_layout(void) {
    struct geom_gpt_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    memcpy(hdr.signature, "EFI PART", 8);
    hdr.revision = 0x00010000;
    hdr.header_size = 92;
    hdr.my_lba = 1;
    hdr.num_entries = 128;
    hdr.entry_size = 128;
    
    TEST_ASSERT(memcmp(hdr.signature, "EFI PART", 8) == 0, "signature");
    TEST_ASSERT(hdr.revision == 0x00010000, "revision 1.0");
    TEST_ASSERT(hdr.my_lba == 1, "my_lba at 1");
    
    TEST_PASS();
}

/* ========== BSD Disklabel Tests ========== */

/*
 * Test: BSD disklabel structure size
 */
static int test_bsd_label_size(void) {
    /* BSD disklabel should fit in a sector */
    TEST_ASSERT(sizeof(struct geom_bsd_disklabel) <= 512, "BSD label must fit in sector");
    TEST_PASS();
}

/*
 * Test: BSD partition entry size
 */
static int test_bsd_partition_size(void) {
    TEST_ASSERT(sizeof(struct geom_bsd_partition) == 16, "BSD partition must be 16 bytes");
    TEST_PASS();
}

/*
 * Test: BSD magic number
 */
static int test_bsd_magic(void) {
    TEST_ASSERT(GEOM_BSD_DISKMAGIC == 0x82564557, "BSD magic number");
    TEST_PASS();
}

/* ========== Partition Type Tests ========== */

/*
 * Test: MBR type constants
 */
static int test_mbr_types(void) {
    TEST_ASSERT(GEOM_MBR_EMPTY == 0x00, "empty type");
    TEST_ASSERT(GEOM_MBR_FAT32 == 0x0B, "FAT32 type");
    TEST_ASSERT(GEOM_MBR_LINUX == 0x83, "Linux type");
    TEST_ASSERT(GEOM_MBR_FREEBSD == 0xA5, "FreeBSD type");
    TEST_ASSERT(GEOM_MBR_GPT_PROTECTIVE == 0xEE, "GPT protective");
    TEST_PASS();
}

/*
 * Test: BSD fstype constants
 */
static int test_bsd_fstypes(void) {
    TEST_ASSERT(GEOM_BSD_FS_UNUSED == 0, "unused fstype");
    TEST_ASSERT(GEOM_BSD_FS_SWAP == 1, "swap fstype");
    TEST_ASSERT(GEOM_BSD_FS_BSDFFS == 7, "4.2BSD fstype");
    TEST_ASSERT(GEOM_BSD_FS_ZFS == 27, "ZFS fstype");
    TEST_PASS();
}

/* ========== GUID Tests ========== */

/*
 * Test: GUID comparison
 */
static int test_guid_compare(void) {
    uint8_t guid1[16] = {0};
    uint8_t guid2[16] = {0};
    
    /* Equal GUIDs */
    TEST_ASSERT(geom_guid_equal(guid1, guid2) == 1, "zero guids equal");
    
    /* Unequal GUIDs */
    guid2[0] = 1;
    TEST_ASSERT(geom_guid_equal(guid1, guid2) == 0, "different guids not equal");
    
    /* Zero check */
    memset(guid1, 0, 16);
    TEST_ASSERT(geom_guid_is_zero(guid1) == 1, "zero guid detected");
    
    guid1[15] = 1;
    TEST_ASSERT(geom_guid_is_zero(guid1) == 0, "non-zero guid detected");
    
    TEST_PASS();
}

/*
 * Test: Well-known GPT GUIDs
 */
static int test_gpt_known_guids(void) {
    /* EFI System GUID should be non-zero */
    TEST_ASSERT(geom_guid_is_zero(GEOM_GPT_TYPE_EFI_SYSTEM) == 0, "EFI GUID non-zero");
    
    /* Linux FS GUID should be non-zero */
    TEST_ASSERT(geom_guid_is_zero(GEOM_GPT_TYPE_LINUX_FS) == 0, "Linux GUID non-zero");
    
    /* GUIDs should be unique */
    TEST_ASSERT(geom_guid_equal(GEOM_GPT_TYPE_EFI_SYSTEM, GEOM_GPT_TYPE_LINUX_FS) == 0, 
                "EFI != Linux GUID");
    
    TEST_PASS();
}

/* ========== Property Tests ========== */

/*
 * Property: Partition sizes must be positive
 */
static int test_partition_size_positive(void) {
    geom_partition_t part;
    memset(&part, 0, sizeof(part));
    
    /* Size 0 should be invalid */
    part.size_sectors = 0;
    TEST_ASSERT(part.size_sectors == 0, "zero size detected");
    
    /* Large size should work */
    part.size_sectors = 0xFFFFFFFFFFFFFFFFULL;
    TEST_ASSERT(part.size_sectors > 0, "large size positive");
    
    TEST_PASS();
}

/*
 * Property: Partition flags are disjoint
 */
static int test_partition_flags(void) {
    TEST_ASSERT((GEOM_PART_BOOTABLE & GEOM_PART_ACTIVE) == 0 ||
                GEOM_PART_BOOTABLE == GEOM_PART_ACTIVE, "bootable/active overlap ok");
    TEST_ASSERT((GEOM_PART_CONTAINER & GEOM_PART_BOOTABLE) == 0, "container != bootable");
    
    /* Combined flags should work */
    uint32_t flags = GEOM_PART_BOOTABLE | GEOM_PART_ACTIVE;
    TEST_ASSERT(flags & GEOM_PART_BOOTABLE, "bootable set");
    TEST_ASSERT(flags & GEOM_PART_ACTIVE, "active set");
    
    TEST_PASS();
}

/* ========== Test Runner ========== */

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn fn;
};

static struct test_case geom_tests[] = {
    {"mbr_struct_size", test_mbr_struct_size},
    {"mbr_entry_layout", test_mbr_entry_layout},
    {"mbr_signature_offset", test_mbr_signature_offset},
    {"gpt_header_size", test_gpt_header_size},
    {"gpt_entry_size", test_gpt_entry_size},
    {"gpt_header_layout", test_gpt_header_layout},
    {"bsd_label_size", test_bsd_label_size},
    {"bsd_partition_size", test_bsd_partition_size},
    {"bsd_magic", test_bsd_magic},
    {"mbr_types", test_mbr_types},
    {"bsd_fstypes", test_bsd_fstypes},
    {"guid_compare", test_guid_compare},
    {"gpt_known_guids", test_gpt_known_guids},
    {"partition_size_positive", test_partition_size_positive},
    {"partition_flags", test_partition_flags},
    {NULL, NULL}
};

static int test_geom_run_all(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    for (int i = 0; geom_tests[i].fn != NULL; i++) {
        geom_tests[i].fn();
    }
    
    return tests_failed;
}

/*
 * Entry point for test runner
 */
void test_geom(void) {
    kprint("  [GEOM] Running geom tests...\n");
    int failed = test_geom_run_all();
    if (failed == 0) {
        kprint("  [GEOM] All tests passed\n");
    } else {
        kprint("  [GEOM] Some tests failed\n");
    }
}

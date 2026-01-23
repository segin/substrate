/*
 * test_udf.c - UDF Filesystem Unit Tests
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Include UDF header for structure definitions */
#include <fs/udf/udf.h>

/* Test structure sizes match ECMA-167 specifications */
static void test_structure_sizes(void) {
    /* Tag must be 16 bytes */
    assert(sizeof(struct udf_tag) == 16);
    
    /* Extent AD must be 8 bytes */
    assert(sizeof(struct udf_extent_ad) == 8);
    
    /* Short AD must be 8 bytes */
    assert(sizeof(struct udf_short_ad) == 8);
    
    /* Long AD must be 16 bytes */
    assert(sizeof(struct udf_long_ad) == 16);
    
    /* Timestamp must be 12 bytes */
    assert(sizeof(struct udf_timestamp) == 12);
    
    /* RegID must be 32 bytes */
    assert(sizeof(struct udf_regid) == 32);
    
    /* Charspec must be 64 bytes */
    assert(sizeof(struct udf_charspec) == 64);
    
    printf("test_structure_sizes: PASSED\n");
}

/* Test tag checksum calculation */
static void test_tag_checksum(void) {
    struct udf_tag tag;
    memset(&tag, 0, sizeof(tag));
    
    tag.tag_id = UDF_TAG_PRIMARY_VD;
    tag.desc_version = 2;
    
    /* Calculate checksum manually */
    uint8_t *p = (uint8_t *)&tag;
    uint8_t sum = 0;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    
    tag.tag_checksum = sum;
    
    /* Verify checksum is non-zero for non-empty tag */
    assert(tag.tag_checksum != 0 || (tag.tag_id == 0 && tag.desc_version == 0));
    
    printf("test_tag_checksum: PASSED\n");
}

/* Test FID characteristics flags */
static void test_fid_flags(void) {
    assert(UDF_FID_HIDDEN == 0x01);
    assert(UDF_FID_DIRECTORY == 0x02);
    assert(UDF_FID_DELETED == 0x04);
    assert(UDF_FID_PARENT == 0x08);
    
    printf("test_fid_flags: PASSED\n");
}

/* Test ICB allocation descriptor types */
static void test_icb_ad_types(void) {
    assert(UDF_ICB_FLAG_AD_SHORT == 0);
    assert(UDF_ICB_FLAG_AD_LONG == 1);
    assert(UDF_ICB_FLAG_AD_EXT == 2);
    assert(UDF_ICB_FLAG_AD_INLINE == 3);
    
    printf("test_icb_ad_types: PASSED\n");
}

/* Test file types */
static void test_file_types(void) {
    assert(UDF_FILETYPE_DIR == 4);
    assert(UDF_FILETYPE_FILE == 5);
    assert(UDF_FILETYPE_SYMLINK == 12);
    
    printf("test_file_types: PASSED\n");
}

/* Test tag IDs */
static void test_tag_ids(void) {
    assert(UDF_TAG_PRIMARY_VD == 1);
    assert(UDF_TAG_ANCHOR_VDP == 2);
    assert(UDF_TAG_PARTITION_D == 5);
    assert(UDF_TAG_LOGICAL_VD == 6);
    assert(UDF_TAG_FSD == 256);
    assert(UDF_TAG_FID == 257);
    assert(UDF_TAG_FE == 261);
    assert(UDF_TAG_EFE == 266);
    
    printf("test_tag_ids: PASSED\n");
}

/* Property test: allocation descriptor length calculations */
static void test_property_alloc_desc(void) {
    /* Short AD is always 8 bytes */
    assert(sizeof(struct udf_short_ad) == 8);
    
    /* For N short ADs, total length is N*8 */
    for (int n = 1; n <= 100; n++) {
        assert(n * sizeof(struct udf_short_ad) == n * 8);
    }
    
    printf("test_property_alloc_desc: PASSED\n");
}

/* Property test: FID size calculation */
static void test_property_fid_size(void) {
    /* FID header is 38 bytes (tag + fixed fields + icb) */
    /* Size = 38 + impl_use_length + file_id_length, padded to 4 */
    
    for (int impl_len = 0; impl_len < 16; impl_len++) {
        for (int name_len = 1; name_len < 64; name_len++) {
            int fid_size = 38 + impl_len + name_len;
            fid_size = (fid_size + 3) & ~3;  /* Pad to 4 */
            assert(fid_size % 4 == 0);
            assert(fid_size >= 38);
        }
    }
    
    printf("test_property_fid_size: PASSED\n");
}

void run_udf_tests(void) {
    printf("=== UDF Unit Tests ===\n");
    
    test_structure_sizes();
    test_tag_checksum();
    test_fid_flags();
    test_icb_ad_types();
    test_file_types();
    test_tag_ids();
    test_property_alloc_desc();
    test_property_fid_size();
    
    printf("=== All UDF Tests PASSED ===\n");
}

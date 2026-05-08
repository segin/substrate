/*
 * Host-side tests for the a.out classifier and validator.  Drives the
 * pure-C path inside aout.c (compiled with -DHOST_TEST so the kernel-
 * only loader body is excluded).
 */

#include <exec/formats/aout.h>
#include <stdio.h>
#include <string.h>

static int fail_count = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        fail_count++; \
    } \
} while (0)

/* Build a midmag word from magic/MID/flags. */
static uint32_t midmag(uint32_t magic, uint32_t mid, uint32_t flags) {
    return (magic & 0xFFFFu) | ((mid & 0x3FFu) << 16) | ((flags & 0x3Fu) << 26);
}

static void make_hdr(struct aout_exec *h, uint32_t mm,
                     uint32_t text, uint32_t data, uint32_t bss,
                     uint32_t entry, uint32_t trsize, uint32_t drsize) {
    memset(h, 0, sizeof(*h));
    h->a_midmag = mm;
    h->a_text = text;
    h->a_data = data;
    h->a_bss = bss;
    h->a_entry = entry;
    h->a_trsize = trsize;
    h->a_drsize = drsize;
}

static void test_classify_qmagic(void) {
    struct aout_exec h;

    /* QMAGIC + MID=0 -> Linux. */
    make_hdr(&h, midmag(AOUT_QMAGIC_VAL, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_LINUX, "QMAGIC not Linux");

    /* QMAGIC even with a BSD MID still classifies Linux — QMAGIC is
     * unique to Linux per spec. */
    make_hdr(&h, midmag(AOUT_QMAGIC_VAL, AOUT_MID_I386, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_LINUX, "QMAGIC+I386MID not Linux");
}

static void test_classify_sunos(void) {
    struct aout_exec h;

    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, AOUT_MID_SUN386, 0),
             0x1000, 0x100, 0x100, 0x10000, 0x40, 0x40);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_SUNOS, "Sun386i MID not SunOS");
}

static void test_classify_netbsd(void) {
    struct aout_exec h;

    /* NetBSD i386 MID + non-empty relocs -> NetBSD. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, AOUT_MID_I386, 0),
             0x1000, 0x100, 0x100, 0x1020, 0x40, 0x20);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_NETBSD, "NetBSD MID+relocs misclassified");
}

static void test_classify_freebsd(void) {
    struct aout_exec h;

    /* MID=0 + relocs present -> FreeBSD (old). */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0x40, 0x20);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_FREEBSD,
           "MID=0 with relocs not FreeBSD");

    /* AOUT_MID_I386_BSD with relocs -> FreeBSD lineage. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, AOUT_MID_I386_BSD, 0),
             0x1000, 0x100, 0x100, 0x1020, 0x40, 0x20);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_FREEBSD,
           "I386_BSD with relocs not FreeBSD");
}

static void test_classify_linux_default(void) {
    struct aout_exec h;

    /* No relocations + recognised magic -> Linux fallback. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_LINUX,
           "no-reloc ZMAGIC didn't fall back to Linux");

    make_hdr(&h, midmag(AOUT_OMAGIC_VAL, 0, 0),
             0x100, 0x100, 0, 0x40, 0, 0);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_LINUX,
           "OMAGIC w/o relocs not Linux");
}

static void test_classify_unknown(void) {
    struct aout_exec h;

    /* Garbage magic. */
    make_hdr(&h, midmag(0xBEEF, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    EXPECT(aout_classify(&h) == AOUT_FLAVOR_UNKNOWN,
           "garbage magic accepted");

    EXPECT(aout_classify(NULL) == AOUT_FLAVOR_UNKNOWN, "NULL accepted");
}

static void test_validate_basic(void) {
    struct aout_exec h;

    /* Well-formed ZMAGIC: header + text + data + symtab + relocs all
     * fit in the file. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    h.a_syms = 0;
    EXPECT(aout_validate_header(&h, AOUT_HEADER_SIZE + 0x1000 + 0x100) == 0,
           "well-formed ZMAGIC rejected");

    /* Garbage magic. */
    make_hdr(&h, midmag(0x4242, 0, 0),
             0x1000, 0x100, 0x100, 0x1020, 0, 0);
    EXPECT(aout_validate_header(&h, 0x10000) != 0,
           "garbage magic validated");

    /* ZMAGIC entry == 0 must be rejected (text is page-aligned, not zero). */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x1000, 0x100, 0x100, 0, 0, 0);
    EXPECT(aout_validate_header(&h, 0x10000) != 0,
           "ZMAGIC entry==0 accepted");

    /* OMAGIC entry == 0 is allowed (text starts at offset 0). */
    make_hdr(&h, midmag(AOUT_OMAGIC_VAL, 0, 0),
             0x100, 0x100, 0x100, 0, 0, 0);
    EXPECT(aout_validate_header(&h, AOUT_HEADER_SIZE + 0x200) == 0,
           "OMAGIC entry==0 rejected");

    EXPECT(aout_validate_header(NULL, 1024) != 0, "NULL header validated");
}

static void test_validate_size_caps(void) {
    struct aout_exec h;

    /* a_text > 1 GiB -> reject. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x80000000, 0x100, 0x100, 0x80000020, 0, 0);
    EXPECT(aout_validate_header(&h, 0xFFFFFFFEU) != 0,
           "huge a_text accepted");

    /* One byte over the per-segment cap is rejected. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x40000001, 0x100, 0, 0x40000010, 0, 0);
    EXPECT(aout_validate_header(&h, 0xFFFFFFFEU) != 0,
           "cap+1 a_text accepted");

    /* Exactly at the per-segment cap is allowed (cap is inclusive). */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x40000000, 0x100, 0, 0x40000010, 0, 0);
    EXPECT(aout_validate_header(&h, 0x80000000U) == 0,
           "exact-cap a_text rejected");
}

static void test_validate_truncated(void) {
    struct aout_exec h;

    /* Header claims more file content than present. */
    make_hdr(&h, midmag(AOUT_ZMAGIC_VAL, 0, 0),
             0x1000, 0x1000, 0, 0x1020, 0, 0);
    EXPECT(aout_validate_header(&h, AOUT_HEADER_SIZE + 0x100) != 0,
           "truncated file validated");

    /* File exactly matches header claim. */
    EXPECT(aout_validate_header(&h, AOUT_HEADER_SIZE + 0x1000 + 0x1000) == 0,
           "exact-fit file rejected");
}

int main(void) {
    test_classify_qmagic();
    test_classify_sunos();
    test_classify_netbsd();
    test_classify_freebsd();
    test_classify_linux_default();
    test_classify_unknown();
    test_validate_basic();
    test_validate_size_caps();
    test_validate_truncated();

    if (fail_count == 0) {
        printf("host_test_aout: ok\n");
        return 0;
    }
    fprintf(stderr, "host_test_aout: %d failures\n", fail_count);
    return 1;
}

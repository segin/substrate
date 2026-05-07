/*
 * Host-side tests for the COFF file-header and a.out-style optional-header
 * validators.  Drives the pure-C path inside coff.c (compiled with
 * -DHOST_TEST so all kernel-only loader code is excluded).
 */
#include <exec/formats/coff.h>
#include <stdio.h>
#include <string.h>

static int fail_count = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        fail_count++; \
    } \
} while (0)

static void make_filehdr(coff_filehdr_t *fh, uint16_t magic, uint16_t nscns,
                         uint16_t opthdr) {
    memset(fh, 0, sizeof(*fh));
    fh->f_magic = magic;
    fh->f_nscns = nscns;
    fh->f_opthdr = opthdr;
}

static void make_aouthdr(coff_aouthdr_t *opt, uint16_t magic,
                         int32_t tsize, int32_t dsize, int32_t bsize,
                         int32_t entry, int32_t text_start, int32_t data_start) {
    memset(opt, 0, sizeof(*opt));
    opt->magic = magic;
    opt->tsize = tsize;
    opt->dsize = dsize;
    opt->bsize = bsize;
    opt->entry = entry;
    opt->text_start = text_start;
    opt->data_start = data_start;
}

static void test_filehdr(void) {
    coff_filehdr_t fh;
    uint32_t hdr_bytes = (uint32_t)sizeof(fh);

    make_filehdr(&fh, COFF_MAGIC_I386, 1, sizeof(coff_aouthdr_t));
    EXPECT(coff_validate_filehdr(&fh, hdr_bytes + sizeof(coff_aouthdr_t)
                                       + sizeof(coff_scnhdr_t)) == 0,
           "well-formed header rejected");

    /* Wrong magic */
    make_filehdr(&fh, 0x1234, 1, 0);
    EXPECT(coff_validate_filehdr(&fh, hdr_bytes + sizeof(coff_scnhdr_t)) != 0,
           "wrong magic accepted");

    /* Zero sections */
    make_filehdr(&fh, COFF_MAGIC_I386, 0, 0);
    EXPECT(coff_validate_filehdr(&fh, hdr_bytes) != 0, "zero sections accepted");

    /* Section table exceeds file size */
    make_filehdr(&fh, COFF_MAGIC_I386, 256, 0);
    EXPECT(coff_validate_filehdr(&fh, hdr_bytes + 16) != 0,
           "truncated section table accepted");

    /* NULL */
    EXPECT(coff_validate_filehdr(NULL, 1024) != 0, "NULL header accepted");

    /* opthdr larger than file */
    make_filehdr(&fh, COFF_MAGIC_I386, 1, 4096);
    EXPECT(coff_validate_filehdr(&fh, hdr_bytes + 100) != 0,
           "opthdr overflowing file accepted");
}

static void test_aouthdr_zmagic(void) {
    coff_aouthdr_t opt;

    /* Canonical SVR3 layout: 8 KiB text at 0x08000000, entry just inside,
     * 4 KiB data at 0x08010000, no BSS. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) == 0, "valid ZMAGIC rejected");

    /* OMAGIC must be rejected outright. */
    make_aouthdr(&opt, AOUT_OMAGIC,
                 0x2000, 0x1000, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "OMAGIC accepted");

    /* NMAGIC must be rejected outright. */
    make_aouthdr(&opt, AOUT_NMAGIC,
                 0x2000, 0x1000, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "NMAGIC accepted");

    /* Garbage magic. */
    make_aouthdr(&opt, 0xABCD,
                 0x2000, 0x1000, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "garbage magic accepted");

    EXPECT(coff_validate_aouthdr(NULL) != 0, "NULL aouthdr accepted");
}

static void test_aouthdr_sizes(void) {
    coff_aouthdr_t opt;

    /* Negative tsize. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 -1, 0x1000, 0,
                 0x08000000, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "negative tsize accepted");

    /* Page-misaligned tsize for ZMAGIC. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x1FFF, 0x1000, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "misaligned tsize accepted");

    /* Excessive tsize ( > 1 GiB ). */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x80000000, 0x1000, 0,
                 0x08000010, 0x08000000, 0x80010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "huge tsize accepted");

    /* Negative dsize. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, -8, 0,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "negative dsize accepted");

    /* Negative bsize. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, -1,
                 0x08000010, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "negative bsize accepted");
}

static void test_aouthdr_entry(void) {
    coff_aouthdr_t opt;

    /* Entry below text_start. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x07FFFFFC, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "entry below text accepted");

    /* Entry at text_end (one past last byte) — out of range. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x08002000, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "entry at text_end accepted");

    /* Entry exactly at text_start (allowed). */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x08000000, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) == 0, "entry == text_start rejected");

    /* Entry one byte before text_end (allowed). */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x08001FFF, 0x08000000, 0x08010000);
    EXPECT(coff_validate_aouthdr(&opt) == 0, "entry just below text_end rejected");
}

static void test_aouthdr_layout(void) {
    coff_aouthdr_t opt;

    /* data_start before text_end — overlaps. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0x2000, 0x1000, 0,
                 0x08000000, 0x08000000, 0x08001000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "overlapping data accepted");

    /* BSS-only image (no text, no data, just bss).  Per spec, entry must
     * be 0 because there is no .text to anchor execution in. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0, 0, 0x1000,
                 0, 0, 0x08000000);
    EXPECT(coff_validate_aouthdr(&opt) == 0, "valid BSS-only rejected");

    /* BSS-only with non-zero entry — no text segment, must reject. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0, 0, 0x1000,
                 0x08000000, 0, 0x08000000);
    EXPECT(coff_validate_aouthdr(&opt) != 0, "BSS-only entry!=0 accepted");

    /* Data-only image (no text, no bss, just data) with entry 0 — accepted. */
    make_aouthdr(&opt, AOUT_ZMAGIC,
                 0, 0x1000, 0,
                 0, 0, 0x08000000);
    EXPECT(coff_validate_aouthdr(&opt) == 0, "valid data-only rejected");
}

int main(void) {
    test_filehdr();
    test_aouthdr_zmagic();
    test_aouthdr_sizes();
    test_aouthdr_entry();
    test_aouthdr_layout();

    if (fail_count == 0) {
        printf("host_test_coff_aouthdr: ok\n");
        return 0;
    }
    fprintf(stderr, "host_test_coff_aouthdr: %d failures\n", fail_count);
    return 1;
}

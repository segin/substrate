#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

static int test_valid_type(uint32_t type) {
    struct elks_exec hdr;

    memset(&hdr, 0, sizeof(hdr));
    hdr.type = type;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 32;

    return elks_header_recognized(&hdr, sizeof(hdr));
}

int main(void) {
    struct elks_exec hdr;

    memset(&hdr, 0, sizeof(hdr));
    hdr.type = ELKS_COMBID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 16;
    if (!elks_header_recognized(&hdr, sizeof(hdr))) {
        fprintf(stderr, "FAIL: combined ELKS header rejected\n");
        return 1;
    }

    if (!test_valid_type(ELKS_SPLITID)) {
        fprintf(stderr, "FAIL: split ELKS header rejected\n");
        return 1;
    }

    if (!test_valid_type(ELKS_SPLITID_AHISTORICAL)) {
        fprintf(stderr, "FAIL: historical split ELKS header rejected\n");
        return 1;
    }

    hdr.type = 0x00000000u;
    if (elks_header_recognized(&hdr, sizeof(hdr))) {
        fprintf(stderr, "FAIL: bad ELKS type accepted\n");
        return 1;
    }

    hdr.type = ELKS_COMBID;
    hdr.hlen = (uint8_t)(ELKS_MINIX_HDR_SIZE - 1);
    if (elks_header_recognized(&hdr, sizeof(hdr))) {
        fprintf(stderr, "FAIL: bad ELKS header length accepted\n");
        return 1;
    }

    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 2;
    if (elks_header_recognized(&hdr, sizeof(hdr))) {
        fprintf(stderr, "FAIL: unsupported ELKS version accepted\n");
        return 1;
    }

    hdr.version = 1;
    hdr.tseg = 0;
    if (elks_header_recognized(&hdr, sizeof(hdr))) {
        fprintf(stderr, "FAIL: zero text segment accepted\n");
        return 1;
    }

    hdr.tseg = 16;
    if (elks_header_recognized(&hdr, sizeof(hdr) - 1)) {
        fprintf(stderr, "FAIL: truncated ELKS header accepted\n");
        return 1;
    }

    if (elks_read_exact_status((int)sizeof(hdr), sizeof(hdr)) != 0) {
        fprintf(stderr, "FAIL: exact ELKS read did not pass\n");
        return 1;
    }
    if (elks_read_exact_status((int)sizeof(hdr) - 1, sizeof(hdr)) != -ENOEXEC) {
        fprintf(stderr, "FAIL: truncated ELKS read did not map to ENOEXEC\n");
        return 1;
    }
    if (elks_read_exact_status(-EIO, sizeof(hdr)) != -EIO) {
        fprintf(stderr, "FAIL: ELKS read helper lost hard read error\n");
        return 1;
    }

    hdr.hlen = ELKS_FARTEXT_HDR_SIZE;
    if (!elks_header_recognized(&hdr, ELKS_FARTEXT_HDR_SIZE)) {
        fprintf(stderr, "FAIL: far-text header variant rejected\n");
        return 1;
    }

    puts("host_test_elks_aout: ok");
    return 0;
}

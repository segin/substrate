/*
 * host_test_usb_msc_cbw.c - USB Mass Storage CBW/CSW wire-format unit test
 *
 * Verifies that struct packing and field offsets match the USB Mass Storage
 * Class Bulk-Only Transport specification.  CBW is exactly 31 bytes, CSW is
 * exactly 13 bytes, and a constructed READ(10) CBW must serialise to the
 * documented byte sequence.  Catches accidental compiler padding or field
 * reordering before the regression reaches a real device.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#define CBW_SIGNATURE   0x43425355U    /* 'USBC' */
#define CSW_SIGNATURE   0x53425355U    /* 'USBS' */
#define CBW_SIZE        31
#define CSW_SIZE        13

struct usb_msc_cbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed));

struct usb_msc_csw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed));

#define RUN(name) do {                                  \
    bool r = name();                                    \
    printf("%s: %s\n", r ? "PASS" : "FAIL", #name);     \
    if (!r) failures++;                                 \
} while (0)

static bool test_cbw_size_matches_spec(void)
{
    return sizeof(struct usb_msc_cbw) == CBW_SIZE;
}

static bool test_csw_size_matches_spec(void)
{
    return sizeof(struct usb_msc_csw) == CSW_SIZE;
}

static bool test_cbw_field_offsets(void)
{
    return offsetof(struct usb_msc_cbw, dCBWSignature)         == 0  &&
           offsetof(struct usb_msc_cbw, dCBWTag)               == 4  &&
           offsetof(struct usb_msc_cbw, dCBWDataTransferLength) == 8 &&
           offsetof(struct usb_msc_cbw, bmCBWFlags)            == 12 &&
           offsetof(struct usb_msc_cbw, bCBWLUN)               == 13 &&
           offsetof(struct usb_msc_cbw, bCBWCBLength)          == 14 &&
           offsetof(struct usb_msc_cbw, CBWCB)                 == 15;
}

static bool test_csw_field_offsets(void)
{
    return offsetof(struct usb_msc_csw, dCSWSignature)   == 0  &&
           offsetof(struct usb_msc_csw, dCSWTag)         == 4  &&
           offsetof(struct usb_msc_csw, dCSWDataResidue) == 8  &&
           offsetof(struct usb_msc_csw, bCSWStatus)      == 12;
}

/*
 * Build a READ(10) CBW for LBA=0x12345678, length=8 sectors (4096 bytes
 * for 512 B/sector), tag=0xDEADBEEF, LUN=0.  Compare the resulting byte
 * stream against the bytes a packet capture would show.  Little-endian.
 */
static bool test_cbw_read10_byte_layout(void)
{
    struct usb_msc_cbw cbw;
    memset(&cbw, 0, sizeof(cbw));

    cbw.dCBWSignature           = CBW_SIGNATURE;
    cbw.dCBWTag                 = 0xDEADBEEFU;
    cbw.dCBWDataTransferLength  = 4096U;
    cbw.bmCBWFlags              = 0x80;  /* IN */
    cbw.bCBWLUN                 = 0;
    cbw.bCBWCBLength            = 10;

    /* READ(10): opcode 0x28, LBA in bytes 2-5 (big-endian per SCSI),
     * transfer length in bytes 7-8 (big-endian, in blocks). */
    cbw.CBWCB[0] = 0x28;
    cbw.CBWCB[2] = 0x12;
    cbw.CBWCB[3] = 0x34;
    cbw.CBWCB[4] = 0x56;
    cbw.CBWCB[5] = 0x78;
    cbw.CBWCB[7] = 0x00;
    cbw.CBWCB[8] = 0x08;

    static const uint8_t expected[CBW_SIZE] = {
        /* signature 'USBC' little-endian */
        0x55, 0x53, 0x42, 0x43,
        /* tag 0xDEADBEEF little-endian */
        0xEF, 0xBE, 0xAD, 0xDE,
        /* dCBWDataTransferLength = 4096 little-endian */
        0x00, 0x10, 0x00, 0x00,
        /* flags, LUN, CBLength */
        0x80, 0x00, 0x0A,
        /* CBWCB[0..15] */
        0x28, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    return memcmp(&cbw, expected, CBW_SIZE) == 0;
}

/* Property-style test: round-trip 256 random CBWs through pack/unpack
 * and verify every field survives.  Mostly a guardrail for future
 * struct changes. */
static bool test_cbw_roundtrip_property(void)
{
    uint32_t s = 0xC0FFEEU;
    for (int i = 0; i < 256; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;

        struct usb_msc_cbw a, b;
        memset(&a, 0, sizeof(a));
        a.dCBWSignature          = CBW_SIGNATURE;
        a.dCBWTag                = s;
        a.dCBWDataTransferLength = (s ^ 0x5A5A5A5AU);
        a.bmCBWFlags             = (s >> 24) & 0x80;
        a.bCBWLUN                = (s >> 16) & 0x0F;
        a.bCBWCBLength           = (s >> 8) & 0x0F;  /* 0..15, no overflow */
        for (int j = 0; j < 16; j++)
            a.CBWCB[j] = (uint8_t)(s + j);

        uint8_t wire[CBW_SIZE];
        memcpy(wire, &a, CBW_SIZE);
        memset(&b, 0, sizeof(b));
        memcpy(&b, wire, CBW_SIZE);

        if (memcmp(&a, &b, sizeof(a)) != 0)
            return false;
    }
    return true;
}

int main(void)
{
    int failures = 0;
    RUN(test_cbw_size_matches_spec);
    RUN(test_csw_size_matches_spec);
    RUN(test_cbw_field_offsets);
    RUN(test_csw_field_offsets);
    RUN(test_cbw_read10_byte_layout);
    RUN(test_cbw_roundtrip_property);
    if (failures == 0)
        printf("All usb_msc CBW tests PASSED\n");
    else
        printf("%d usb_msc CBW test(s) FAILED\n", failures);
    return failures ? 1 : 0;
}

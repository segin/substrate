#include <kern/console.h>
#include <kern/random.h>
#include <string.h>
#include <stdint.h>

/*
 * Test Vector #1 from RFC 7539 Section 2.3.2
 */
static const uint8_t test_key[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t test_nonce[12] = {
    0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x4a,
    0x00, 0x00, 0x00, 0x00
};

static const uint32_t test_block_counter = 1;

static const uint8_t test_keystream[64] = {
    0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15,
    0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
    0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03,
    0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
    0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xc4, 0xd9,
    0x06, 0xd1, 0x83, 0x3d, 0x3f, 0x56, 0x38, 0xdd,
    0xb8, 0x77, 0x89, 0xe7, 0x8c, 0xc0, 0x9b, 0x9e,
    0x3f, 0x14, 0xca, 0xd0, 0x9d, 0xec, 0xa8, 0xdb
};

void run_chacha20_tests(void) {
    struct chacha20_ctx ctx;
    int pass = 1;

    kprint("\n=== RUNNING CHACHA20 TESTS ===\n");

    /* Initialize with key and nonce */
    chacha20_init(&ctx, test_key, test_nonce);

    /* Set block counter to 1 (RFC 7539 Test Vector #1) */
    /* Note: chacha20_init sets counter to 0 */
    ctx.state[12] = test_block_counter;

    /* Generate block */
    chacha20_block(&ctx);

    /* Verify output against keystream */
    if (memcmp(ctx.block, test_keystream, sizeof(test_keystream)) != 0) {
        pass = 0;
        kprint("FAIL: ChaCha20 Output mismatch!\n");

        kprint("Expected first 16 bytes: ");
        for (int i = 0; i < 16; i++) {
            kprintf("%02x ", test_keystream[i]);
        }
        kprint("\nActual first 16 bytes:   ");
        for (int i = 0; i < 16; i++) {
            kprintf("%02x ", ctx.block[i]);
        }
        kprint("\n");
    } else {
        kprint("PASS: ChaCha20 RFC 7539 Test Vector #1 Matched\n");
    }

    if (pass) {
        kprint("=== CHACHA20 TESTS PASSED ===\n");
    } else {
        kprint("=== CHACHA20 TESTS FAILED ===\n");
    }
}

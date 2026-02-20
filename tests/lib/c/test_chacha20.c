#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

// Rename standard library functions to avoid conflicts with host libc
#define exit tested_exit
#define abort tested_abort
#define __stack_chk_fail tested_stack_chk_fail
#define malloc tested_malloc
#define free tested_free
#define calloc tested_calloc
#define realloc tested_realloc
#define aligned_alloc tested_aligned_alloc
#define quick_exit tested_quick_exit
#define at_quick_exit tested_at_quick_exit
#define strtol tested_strtol
#define atoi tested_atoi
#define atol tested_atol
#define atoll tested_atoll
#define atof tested_atof
#define getenv tested_getenv
#define system tested_system
#define abs tested_abs
#define labs tested_labs
#define llabs tested_llabs
#define qsort tested_qsort
#define bsearch tested_bsearch
#define rand tested_rand
#define srand tested_srand
#define arc4random_buf tested_arc4random_buf
#define arc4random tested_arc4random
#define arc4random_uniform tested_arc4random_uniform

// Mock arc4random_buf to provide a fixed secret for testing
static uint8_t mock_secret_data[44]; // sizeof(process_secret) in stdlib.c is 11 * 4 = 44
static int use_fixed_mock_secret = 0;

// Use a different name for the mock to avoid macro recursion issues if any,
// but we want to override the one used in stdlib.c.
// Since we #define arc4random_buf tested_arc4random_buf AND then #include stdlib.c,
// and stdlib.c defines arc4random_buf, we might have a conflict.

// Actually, in stdlib.c:
// void arc4random_buf(void *buf, size_t n) { ... }
// If we #define it before, it becomes:
// void tested_arc4random_buf(void *buf, size_t n) { ... }
// This is what we want.

// But we want to PROVIDE our own version of arc4random_buf to be used by srand.
// Wait, srand calls arc4random_buf.
// If we want to mock it, we should NOT let stdlib.c define it, or we should override it.

// In test_stdlib.c, they don't seem to mock arc4random_buf.
// In test_rand_security.c, they just test determinism within a run.

// Let's try to override it by defining it AFTER including stdlib.c? No, that's multiple definition.
// Maybe we can use a macro to skip the definition in stdlib.c? No such macro exists.

// Alternative: mock the open/read calls? stdlib.c's arc4random_buf uses open("/dev/urandom").
// We can mock 'open' and 'read'.

#define open mock_open
#define read mock_read
#define close mock_close

int mock_open(const char *pathname, int flags, ...) {
    if (strcmp(pathname, "/dev/urandom") == 0) return 42;
    return -1;
}

ssize_t mock_read(int fd, void *buf, size_t count) {
    if (fd == 42) {
        uint8_t *p = (uint8_t *)buf;
        for (size_t i = 0; i < count; i++) {
            if (use_fixed_mock_secret) {
                p[i] = mock_secret_data[i % 44];
            } else {
                p[i] = (uint8_t)(i & 0xFF);
            }
        }
        return count;
    }
    return -1;
}

int mock_close(int fd) {
    return 0;
}

// Include the source file directly
#include "../../../lib/c/src/stdlib.c"

// We need to access QR macro, but it's inside chacha20_block.
// Let's define it here for testing.
#define QR(x, a,b,c,d) \
    x[a] += x[b]; x[d] ^= x[a]; x[d] = (x[d] << 16) | (x[d] >> 16); \
    x[c] += x[d]; x[b] ^= x[c]; x[b] = (x[b] << 12) | (x[b] >> 20); \
    x[a] += x[b]; x[d] ^= x[a]; x[d] = (x[d] << 8) | (x[d] >> 24); \
    x[c] += x[d]; x[b] ^= x[c]; x[b] = (x[b] << 7) | (x[b] >> 25);

void test_chacha20_qr_rfc8439(void) {
    printf("Testing ChaCha20 Quarter Round with RFC 8439 test vector...\n");
    uint32_t state[16] = {
        0x879531e0, 0xc5ecf37d, 0x516461b1, 0xc9a62f8a,
        0x44c20ef3, 0x3390af7f, 0xd9fc690b, 0x2a5f714c,
        0x53372767, 0xb00a5631, 0x974c541a, 0x359e9963,
        0x5c971061, 0x3d631689, 0x2098d9d6, 0x91dbd320
    };

    QR(state, 2, 7, 8, 13);

    assert(state[2] == 0xbdb886dc);
    assert(state[7] == 0xcfacafd2);
    assert(state[8] == 0xe46bea80);
    assert(state[13] == 0xccc07c79);
    printf("ChaCha20 Quarter Round test passed\n");
}

void test_chacha20_block_rfc8439(void) {
    printf("Testing chacha20_block with RFC 8439 test vector...\n");

    uint32_t state[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514, 0x1b1a1918, 0x1f1e1d1c,
        0x00000001, 0x00000000, 0x4a000000, 0x00000000
    };

    uint32_t expected[16] = {
        0xf3514f22, 0xe1d91b40, 0x6f27de2f, 0xed1d63b8,
        0x821f138c, 0xe2062c3d, 0xecca4f7e, 0x78cff39e,
        0xa30a3b8a, 0x920a6072, 0xcd7479b5, 0x34932bed,
        0x40ba4c79, 0xcd343ec6, 0x4c2c21ea, 0xb7417df0
    };

    uint32_t out[16];
    chacha20_block(out, state);

    for (int i = 0; i < 16; i++) {
        if (out[i] != expected[i]) {
            printf("stdlib Mismatch at index %d: expected 0x%08x, got 0x%08x\n", i, expected[i], out[i]);
            fflush(stdout);
        }
    }
    for (int i = 0; i < 16; i++) {
        assert(out[i] == expected[i]);
    }
    printf("chacha20_block RFC 8439 test passed\n");
}

void test_rand_determinism(void) {
    printf("Testing rand/srand determinism...\n");

    // Reset state for test reproducibility if possible.
    // Since seeded and chacha_idx are static, we can just call srand.

    tested_srand(12345);
    int r1 = tested_rand();
    int r2 = tested_rand();

    tested_srand(12345);
    int r1_again = tested_rand();
    int r2_again = tested_rand();

    assert(r1 == r1_again);
    assert(r2 == r2_again);
    printf("rand/srand determinism test passed\n");
}

void test_rand_block_transition(void) {
    printf("Testing rand block transition and counter increment...\n");

    tested_srand(54321);

    // Initial state after srand:
    // chacha_idx = 16
    // chacha_state[12] = 0

    // First call to rand() should trigger chacha20_block
    // It will increment chacha_state[12] to 1.
    // chacha_idx will become 1.

    tested_rand();
    assert(chacha_idx == 1);
    assert(chacha_state[12] == 1);

    // Call rand 15 more times (total 16)
    for (int i = 0; i < 15; i++) {
        tested_rand();
    }
    assert(chacha_idx == 16);
    assert(chacha_state[12] == 1);

    // Next call should trigger another block and increment counter
    tested_rand();
    assert(chacha_idx == 1);
    assert(chacha_state[12] == 2);

    printf("rand block transition test passed\n");
}

int main(void) {
    test_chacha20_qr_rfc8439();
    test_chacha20_block_rfc8439();
    test_rand_determinism();
    test_rand_block_transition();

    printf("All ChaCha20 tests passed!\n");
    return 0;
}

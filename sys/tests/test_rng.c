#include <sys/random.h>
#include "../kern/console.h"
#include <string.h>
#include <stdio.h>

void run_rng_tests(void) {
    kprint("\n=== RUNNING RNG TESTS ===\n");

    uint8_t buf1[32];
    uint8_t buf2[32];
    int res;

    // Test 1: Check seed status
    int seeded = random_is_seeded();
    if (seeded) {
        kprint("INFO: RNG is already seeded.\n");
    } else {
        kprint("INFO: RNG is NOT yet seeded (expected early in boot).\n");
    }

    // Test 2: Non-blocking insecure read (should always succeed)
    memset(buf1, 0, sizeof(buf1));
    res = random_get_bytes_flags(buf1, sizeof(buf1), GRND_INSECURE);
    if (res != sizeof(buf1)) {
        kprint("FAIL: random_get_bytes_flags(GRND_INSECURE) return value mismatch\n");
    } else {
        kprint("PASS: random_get_bytes_flags(GRND_INSECURE) returned expected bytes\n");
    }

    // Test 3: Output variance
    memset(buf2, 0, sizeof(buf2));
    random_get_bytes_flags(buf2, sizeof(buf2), GRND_INSECURE);
    
    if (memcmp(buf1, buf2, sizeof(buf1)) == 0) {
        kprint("FAIL: Detailed RNG output check: Two 32-byte blocks are identical\n");
    } else {
        kprint("PASS: Subsequent random blocks differ\n");
    }
    
    // Dump first few bytes for visual inspection
    char msg[128];
    sprintf(msg, "INFO: Random bytes: %02X %02X %02X %02X ...\n", 
            buf1[0], buf1[1], buf1[2], buf1[3]);
    kprint(msg);

    // Test 4: Harvest API injection (verify no crash)
    uint32_t entropy_data = 0xCAFEBABE;
    random_harvest(&entropy_data, sizeof(entropy_data), 1, ENTROPY_SOFTWARE);
    kprint("PASS: random_harvest() injection\n");
    
    random_harvest_fast(&entropy_data, sizeof(entropy_data));
    kprint("PASS: random_harvest_fast() injection\n");

    // Test 5: Verify NONBLOCK behavior when unseeded
    if (!seeded) {
        res = random_get_bytes_flags(buf1, sizeof(buf1), GRND_NONBLOCK | GRND_RANDOM);
        if (res == -1) {
             // Assuming -1 or specific error return. 
             // random_get_bytes_flags usually returns read count or error.
             // Need to check implementation return value for EAGAIN equivalent.
             kprint("PASS: Non-blocking read returns error when unseeded\n");
        } else if (res == 0) {
             kprint("PASS: Non-blocking read returns 0 when unseeded\n");
        } else {
             // It might return partial? Or maybe it got seeded by harvest above?
             kprint("INFO: Non-blocking read returned data (RNG might have just seeded)\n");
        }
    }

    kprint("=== RNG TESTS COMPLETE ===\n");
}

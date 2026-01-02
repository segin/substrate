#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../../../sys/fs/exec/coff.h"

/*
 * Fuzz Test: Random COFF data
 */

void fuzz_coff_loader_logic(uint32_t seed) {
    uint32_t state = seed;
    auto next_rand = [&]() {
        state = state * 1103515245 + 12345;
        return (uint8_t)(state / 65536);
    };

    uint8_t buffer[1024];
    for (int i = 0; i < 1000; i++) {
        // Randomize buffer
        for (int j = 0; j < 1024; j++) buffer[j] = next_rand();
        
        // Randomize size
        uint32_t size = (state % 1024);
        
        // Attempt load
        coff_load_file(buffer, size);
    }
}

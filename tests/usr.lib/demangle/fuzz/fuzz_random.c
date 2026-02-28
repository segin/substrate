#include <demangle.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t g_state = 0x12345678u;

static uint32_t
xorshift32(void)
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    return g_state;
}

int
main(void)
{
    enum { CASES = 200000, MAX_LEN = 96 };
    char buf[MAX_LEN + 1];

    for (int i = 0; i < CASES; i++) {
        size_t len = (size_t)(xorshift32() % MAX_LEN);
        for (size_t j = 0u; j < len; j++) {
            uint32_t r = xorshift32() % 75u;
            if (r < 26u) {
                buf[j] = (char)('a' + r);
            } else if (r < 52u) {
                buf[j] = (char)('A' + (r - 26u));
            } else if (r < 62u) {
                buf[j] = (char)('0' + (r - 52u));
            } else {
                static const char punct[] = "_.$:@!#%^&*()[]{}+-=/<>?";
                buf[j] = punct[xorshift32() % (sizeof(punct) - 1u)];
            }
        }
        buf[len] = '\0';

        for (int mode = 0; mode < 4; mode++) {
            int opt = DEMANGLE_AUTO;
            if (mode == 1) opt = DEMANGLE_ITANIUM;
            if (mode == 2) opt = DEMANGLE_RUST;
            if (mode == 3) opt = DEMANGLE_DLANG;
            char *out = demangle(buf, opt);
            free(out);
        }
    }

    puts("random fuzz: ok");
    return 0;
}

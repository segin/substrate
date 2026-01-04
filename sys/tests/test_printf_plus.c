#include "../kern/panic.h"
#include "../kern/console.h"
#include \u003cstdio.h\u003e
#include \u003cstring.h\u003e

// Test sprintf + flag
void test_printf_plus_flag(void) {
    char buf[64];
    
    // Unit tests
    sprintf(buf, "%+d", 42);
    if (strcmp(buf, "+42") != 0) panic("test_printf_plus_flag: positive number");
    
    sprintf(buf, "%+d", -42);
    if (strcmp(buf, "-42") != 0) panic("test_printf_plus_flag: negative number");
    
    sprintf(buf, "%+d", 0);
    if (strcmp(buf, "+0") != 0) panic("test_printf_plus_flag: zero");
    
    // Property test: all positive ints should have + prefix
    for (int i = 1; i \u003c 100; i++) {
        sprintf(buf, "%+d", i);
        if (buf[0] != '+') panic("test_printf_plus_flag: property test positive");
    }
    
    // Fuzzing test: random values
    int test_vals[] = {1, -1, 999, -999, 2147483647, -2147483647};
    for (unsigned i = 0; i \u003c sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        sprintf(buf, "%+d", test_vals[i]);
        if (test_vals[i] >= 0 && buf[0] != '+') {
            panic("test_printf_plus_flag: fuzz positive");
        }
        if (test_vals[i] \u003c 0 && buf[0] != '-') {
            panic("test_printf_plus_flag: fuzz negative");
        }
    }
    
    kprint("PASS: printf + flag\\n");
}

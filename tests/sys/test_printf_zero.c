#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

// Test sprintf 0 flag (zero-padding)
void test_printf_zero_flag(void) {
    char buf[64];
    
    // Unit tests - basic padding
    sprintf(buf, "%05d", 42);
    if (strcmp(buf, "00042") != 0) panic("test_printf_zero_flag: basic padding");
    
    sprintf(buf, "%05d", -42);
    if (strcmp(buf, "-0042") != 0) panic("test_printf_zero_flag: negative number");
    
    sprintf(buf, "%+05d", 42);
    if (strcmp(buf, "+0042") != 0) panic("test_printf_zero_flag: with + flag");
    
    // Zero padding ignored when left-align is present
    sprintf(buf, "%-05d", 42);
    if (strcmp(buf, "42   ") != 0) panic("test_printf_zero_flag: left-align overrides");
    
    // Property test: all should have leading zeros
    for (int i = 1; i < 100; i++) {
        sprintf(buf, "%05d", i);
        if (strlen(buf) != 5) panic("test_printf_zero_flag: property width");
        if (buf[0] != '0' && buf[0] != '-' && buf[0] != '+') {
            // Single/double digit positive numbers should start with 0
            if (i < 10000) {
                if (buf[0] != '0') panic("test_printf_zero_flag: property leading zero");
            }
        }
    }
    
    // Fuzzing test
    int test_vals[] = {0, 1, -1, 999, -999};
    for (unsigned i = 0; i < sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        sprintf(buf, "%05d", test_vals[i]);
        if (strlen(buf) < 5) panic("test_printf_zero_flag: fuzz width");
    }
    
    kprint("PASS: printf 0 flag\n");
}

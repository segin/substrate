#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

// Test sprintf space flag
void test_printf_space_flag(void) {
    char buf[64];
    
    // Unit tests
    sprintf(buf, "% d", 42);
    if (strcmp(buf, " 42") != 0) panic("test_printf_space_flag: positive number");
    
    sprintf(buf, "% d", -42);
    if (strcmp(buf, "-42") != 0) panic("test_printf_space_flag: negative number");
    
    sprintf(buf, "% d", 0);
    if (strcmp(buf, " 0") != 0) panic("test_printf_space_flag: zero");
    
    // Test that + overrides space
    sprintf(buf, "%+ d", 42);
    if (strcmp(buf, "+42") != 0) panic("test_printf_space_flag: + overrides space");
    
    // Property test: all positive ints should have space prefix
    for (int i = 1; i < 100; i++) {
        sprintf(buf, "% d", i);
        if (buf[0] != ' ') panic("test_printf_space_flag: property test positive");
    }
    
    kprint("PASS: printf space flag\n");
}

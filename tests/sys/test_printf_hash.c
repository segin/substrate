#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

// Test sprintf # flag (alternate form)
void test_printf_hash_flag(void) {
    char buf[64];
    
    // Unit tests for hex
    sprintf(buf, "%#x", 0x42);
    if (strcmp(buf, "0x42") != 0) panic("test_printf_hash_flag: hex lowercase");
    
    sprintf(buf, "%#X", 0x42);
    if (strcmp(buf, "0X42") != 0) panic("test_printf_hash_flag: hex uppercase");
    
    // Zero should not get prefix
    sprintf(buf, "%#x", 0);
    if (strcmp(buf, "0") != 0) panic("test_printf_hash_flag: zero no prefix");
    
    // Property test: all non-zero should have 0x prefix
    for (unsigned i = 1; i < 100; i++) {
        sprintf(buf, "%#x", i);
        if (buf[0] != '0' || buf[1] != 'x') {
            panic("test_printf_hash_flag: property test");
        }
    }
    
    kprint("PASS: printf # flag\n");
}

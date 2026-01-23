#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

// Test sprintf octal conversion
void test_printf_octal(void) {
    char buf[64];
    
    // Unit tests
    sprintf(buf, "%o", 64);
    if (strcmp(buf, "100") != 0) panic("test_printf_octal: basic");
    
    sprintf(buf, "%o", 0);
    if (strcmp(buf, "0") != 0) panic("test_printf_octal: zero");
    
    sprintf(buf, "%o", 0777);
    if (strcmp(buf, "777") != 0) panic("test_printf_octal: 0777");
    
    // Alternate form with # flag
    sprintf(buf, "%#o", 64);
    if (strcmp(buf, "0100") != 0) panic("test_printf_octal: alternate form");
    
    // Zero should not get prefix
    sprintf(buf, "%#o", 0);
    if (strcmp(buf, "0") != 0) panic("test_printf_octal: zero no prefix");
    
    // Property test: verify octal digits only
    for (unsigned i = 1; i < 100; i++) {
        sprintf(buf, "%o", i);
        for (char *p = buf; *p; p++) {
            if (*p < '0' || *p > '7') {
                panic("test_printf_octal: property octal digits");
            }
        }
    }
    
    // Fuzzing test
    unsigned test_vals[] = {0, 1, 7, 8, 63, 64, 0777, 01777, 0xFFFF};
    for (unsigned i = 0; i < sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        sprintf(buf, "%o", test_vals[i]);
        if (strlen(buf) == 0) panic("test_printf_octal: fuzz empty");
    }
    
    kprint("PASS: printf octal\\n");
}

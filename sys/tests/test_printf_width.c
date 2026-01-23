#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

// Test sprintf numeric width
void test_printf_width(void) {
    char buf[64];
    
    // Unit tests - right-align (default)
    sprintf(buf, "%5d", 42);
    if (strcmp(buf, "   42") != 0) panic("test_printf_width: right-align");
    
    sprintf(buf, "%5d", -42);
    if (strcmp(buf, "  -42") != 0) panic("test_printf_width: negative");
    
    // Left-align with width
    sprintf(buf, "%-5d", 42);
    if (strcmp(buf, "42   ") != 0) panic("test_printf_width: left-align");
    
    // Zero-padding (already tested, but verify interaction)
    sprintf(buf, "%05d", 42);
    if (strcmp(buf, "00042") != 0) panic("test_printf_width: zero-pad");
    
    // Width smaller than value
    sprintf(buf, "%2d", 12345);
    if (strcmp(buf, "12345") != 0) panic("test_printf_width: no truncation");
    
    // Property test: all should have correct width
    for (int i = 0; i < 10; i++) {
        sprintf(buf, "%5d", i);
        if (strlen(buf) != 5) panic("test_printf_width: property width");
        // Should be right-aligned with spaces
        if (buf[0] != ' ') panic("test_printf_width: property padding");
    }
    
    // Fuzzing test
    int test_vals[] = {0, 1, -1, 999, -999, 12345};
    for (unsigned i = 0; i < sizeof(test_vals)/sizeof(test_vals[0]); i++) {
        sprintf(buf, "%8d", test_vals[i]);
        if (strlen(buf) < 5) panic("test_printf_width: fuzz min width");
    }
    
    kprint("PASS: printf numeric width\\n");
}

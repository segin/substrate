#include <kern/panic.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

void test_printf_star(void) {
    char buf[64];
    
    // Dynamic width
    sprintf(buf, "%*d", 5, 42);
    if (strcmp(buf, "   42") != 0) panic("test_printf_star: dynamic width positive");
    
    sprintf(buf, "%*d", -5, 42);
    if (strcmp(buf, "42   ") != 0) panic("test_printf_star: dynamic width negative");
    
    // Dynamic precision
    sprintf(buf, "%.*s", 3, "hello");
    if (strcmp(buf, "hel") != 0) panic("test_printf_star: dynamic precision positive");
    
    sprintf(buf, "%.*s", -1, "hello");
    if (strcmp(buf, "hello") != 0) panic("test_printf_star: dynamic precision negative");
    
    // Combined dynamic width and precision
    sprintf(buf, "%*.*s", 5, 3, "hello");
    if (strcmp(buf, "  hel") != 0) panic("test_printf_star: combined positive");
    
    sprintf(buf, "%*.*s", -5, 3, "hello");
    if (strcmp(buf, "hel  ") != 0) panic("test_printf_star: combined negative width");
    
    // Length modifiers and 64-bit
    sprintf(buf, "%lld", 1234567890123LL);
    if (strcmp(buf, "1234567890123") != 0) panic("test_printf_star: ll decimal");
    
    sprintf(buf, "%llx", 0x123456789ABCDEF0LL);
    if (strcmp(buf, "123456789abcdef0") != 0) panic("test_printf_star: ll hex");
    
    sprintf(buf, "%hhd", 257); // Truncates to 1
    if (strcmp(buf, "1") != 0) panic("test_printf_star: hh truncate");
    
    sprintf(buf, "%zu", (size_t)12345);
    if (strcmp(buf, "12345") != 0) panic("test_printf_star: zu size_t");
    
    sprintf(buf, "%td", (ptrdiff_t)12345);
    if (strcmp(buf, "12345") != 0) panic("test_printf_star: td ptrdiff_t");

    sprintf(buf, "%jd", (intmax_t)-1);
    // intmax_t should be 64-bit in this implementation
    if (strcmp(buf, "-1") != 0) panic("test_printf_star: jd intmax");

    // Floating point
    sprintf(buf, "%f", 3.14159);
    // Our ftoa rounds, so let's see. 3.141590
    if (strncmp(buf, "3.14159", 7) != 0) panic("test_printf_star: f basic");

    kprint("PASS: printf dynamic width/precision (*), modifiers, and float\\n");
}

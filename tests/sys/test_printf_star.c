#include <kern/panic.h>
#include <kern/console.h>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

void test_printf_star(void) {
    char buf[64];
    
    // Dynamic width
    snprintf(buf, sizeof(buf), "%*d", 5, 42);
    if (strcmp(buf, "   42") != 0) panic("test_printf_star: dynamic width positive");
    
    snprintf(buf, sizeof(buf), "%*d", -5, 42);
    if (strcmp(buf, "42   ") != 0) panic("test_printf_star: dynamic width negative");
    
    // Dynamic precision
    snprintf(buf, sizeof(buf), "%.*s", 3, "hello");
    if (strcmp(buf, "hel") != 0) panic("test_printf_star: dynamic precision positive");
    
    snprintf(buf, sizeof(buf), "%.*s", -1, "hello");
    if (strcmp(buf, "hello") != 0) panic("test_printf_star: dynamic precision negative");
    
    // Combined dynamic width and precision
    snprintf(buf, sizeof(buf), "%*.*s", 5, 3, "hello");
    if (strcmp(buf, "  hel") != 0) panic("test_printf_star: combined positive");
    
    snprintf(buf, sizeof(buf), "%*.*s", -5, 3, "hello");
    if (strcmp(buf, "hel  ") != 0) panic("test_printf_star: combined negative width");
    
    // Length modifiers and 64-bit
    snprintf(buf, sizeof(buf), "%lld", 1234567890123LL);
    if (strcmp(buf, "1234567890123") != 0) panic("test_printf_star: ll decimal");
    
    snprintf(buf, sizeof(buf), "%llx", 0x123456789ABCDEF0LL);
    if (strcmp(buf, "123456789abcdef0") != 0) panic("test_printf_star: ll hex");
    
    snprintf(buf, sizeof(buf), "%hhd", 257); // Truncates to 1
    if (strcmp(buf, "1") != 0) panic("test_printf_star: hh truncate");
    
    snprintf(buf, sizeof(buf), "%zu", (size_t)12345);
    if (strcmp(buf, "12345") != 0) panic("test_printf_star: zu size_t");
    
    snprintf(buf, sizeof(buf), "%td", (ptrdiff_t)12345);
    if (strcmp(buf, "12345") != 0) panic("test_printf_star: td ptrdiff_t");

    snprintf(buf, sizeof(buf), "%jd", (intmax_t)-1);
    // intmax_t should be 64-bit in this implementation
    if (strcmp(buf, "-1") != 0) panic("test_printf_star: jd intmax");

    // Floating point
    snprintf(buf, sizeof(buf), "%f", 3.14159);
    // Our ftoa rounds, so let's see. 3.141590
    if (strncmp(buf, "3.14159", 7) != 0) panic("test_printf_star: f basic");

    // Scientific notation
    snprintf(buf, sizeof(buf), "%e", 1234.5); // 1.234500e+03
    if (strncmp(buf, "1.234500e+03", 12) != 0) panic("test_printf_star: e notation");

    // Significant digits
    snprintf(buf, sizeof(buf), "%g", 0.00001); // 1e-05
    if (strcmp(buf, "1e-05") != 0) panic("test_printf_star: g notation small");
    snprintf(buf, sizeof(buf), "%g", 123.456); // 123.456
    if (strncmp(buf, "123.456", 7) != 0) panic("test_printf_star: g notation normal");

    // Wide characters and strings
    snprintf(buf, sizeof(buf), "%lc %ls", (int)'A', "Wide");
    if (strcmp(buf, "A Wide") != 0) panic("test_printf_star: wide chars");

    // %n count
    int count = 0;
    snprintf(buf, sizeof(buf), "Hello%nWorld", &count);
    if (count != 5) panic("test_printf_star: n count");

    kprint("PASS: printf all remaining features (e, g, wide, n)\n");
}

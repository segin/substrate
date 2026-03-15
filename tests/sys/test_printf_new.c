#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <stdarg.h>

static char *call_kvasprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *ret = kvasprintf(fmt, ap);
    va_end(ap);
    return ret;
}

void test_printf_new(void) {
    kprint("Testing snprintf, kasprintf, and kvasprintf...\n");

    // Test snprintf
    char buf[32];
    int ret;

    // Normal case
    ret = snprintf(buf, sizeof(buf), "Hello %d", 123);
    if (strcmp(buf, "Hello 123") != 0 || ret != 9) {
        kprint("FAIL: snprintf normal case\n");
        kprintf("Expected 'Hello 123' (ret 9), got '%s' (ret %d)\n", buf, ret);
    } else {
        kprint("PASS: snprintf normal case\n");
    }

    // Truncation
    ret = snprintf(buf, 5, "Hello %d", 123); // "Hell\0"
    if (strcmp(buf, "Hell") != 0 || ret != 9) {
        kprint("FAIL: snprintf truncation\n");
        kprintf("Expected 'Hell' (ret 9), got '%s' (ret %d)\n", buf, ret);
    } else {
        kprint("PASS: snprintf truncation\n");
    }

    // kasprintf
    char *kstr = kasprintf("Dynamic %s %d", "String", 42);
    if (kstr) {
        if (strcmp(kstr, "Dynamic String 42") == 0) {
            kprint("PASS: kasprintf\n");
        } else {
            kprint("FAIL: kasprintf content\n");
            kprintf("Expected 'Dynamic String 42', got '%s'\n", kstr);
        }
        kfree(kstr, strlen(kstr) + 1);
    } else {
        kprint("FAIL: kasprintf returned NULL\n");
    }

    // kvasprintf
    char *kvstr = call_kvasprintf("Variadic %s %d", "Test", 99);
    if (kvstr) {
        if (strcmp(kvstr, "Variadic Test 99") == 0) {
            kprint("PASS: kvasprintf\n");
        } else {
            kprint("FAIL: kvasprintf content\n");
            kprintf("Expected 'Variadic Test 99', got '%s'\n", kvstr);
        }
        kfree(kvstr, strlen(kvstr) + 1);
    } else {
        kprint("FAIL: kvasprintf returned NULL\n");
    }

    // kvasprintf empty
    char *kvemp = call_kvasprintf("");
    if (kvemp) {
        if (strcmp(kvemp, "") == 0) {
            kprint("PASS: kvasprintf empty\n");
        } else {
            kprint("FAIL: kvasprintf empty content\n");
            kprintf("Expected '', got '%s'\n", kvemp);
        }
        kfree(kvemp, strlen(kvemp) + 1);
    } else {
        kprint("FAIL: kvasprintf empty returned NULL\n");
    }
}

#include <kern/console.h>
#include <stdio.h>
#include <string.h>
#include <vm/vm_kmem.h>

void test_printf_new(void) {
    kprint("Testing snprintf and kasprintf...\n");

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
}

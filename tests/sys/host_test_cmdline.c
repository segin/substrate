#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Mock kprint
void kprint(const char *msg) {
    printf("%s", msg);
}

// Include the source file directly
#include "../../sys/kern/cmdline.c"

void test_cmdline_init(void) {
    printf("Running test_cmdline_init...\n");

    // Empty
    cmdline_init("");
    assert(kernel_cmdline[0] == 0);
    assert(initialized == 1);

    // Normal
    cmdline_init("foo=bar");
    assert(strcmp(kernel_cmdline, "foo=bar") == 0);

    // Null
    cmdline_init(NULL);
    assert(kernel_cmdline[0] == 0);
    assert(initialized == 1);

    // Long
    char long_cmd[2000];
    memset(long_cmd, 'a', sizeof(long_cmd));
    long_cmd[sizeof(long_cmd)-1] = 0;
    cmdline_init(long_cmd);
    assert(strlen(kernel_cmdline) == sizeof(kernel_cmdline) - 1);
    assert(kernel_cmdline[sizeof(kernel_cmdline)-1] == 0);

    printf("PASS\n");
}

void test_cmdline_has(void) {
    printf("Running test_cmdline_has...\n");

    cmdline_init("quiet serial_debug rw root=/dev/sda1");

    assert(cmdline_has("quiet"));
    assert(cmdline_has("serial_debug"));
    assert(cmdline_has("rw"));
    // root is present as a key=value pair, so cmdline_has should return true
    assert(cmdline_has("root"));

    assert(!cmdline_has("quie")); // Prefix
    assert(!cmdline_has("iet"));  // Suffix
    assert(!cmdline_has("serial")); // Prefix
    assert(!cmdline_has("debug")); // Suffix (if not at start of word)

    // Edge cases
    cmdline_init("foo");
    assert(cmdline_has("foo"));

    cmdline_init(" foo ");
    assert(cmdline_has("foo"));

    cmdline_init("foo=bar");
    assert(cmdline_has("foo"));

    printf("PASS\n");
}

void test_cmdline_get(void) {
    printf("Running test_cmdline_get...\n");
    char buf[64];

    cmdline_init("root=/dev/sda1 console=ttyS0 debug");

    // Normal extraction
    assert(cmdline_get("root", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "/dev/sda1") == 0);

    assert(cmdline_get("console", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "ttyS0") == 0);

    // Key exists but no value (flag)
    // cmdline_get expects '=', so this should return -1
    assert(cmdline_get("debug", buf, sizeof(buf)) == -1);

    // Not found
    assert(cmdline_get("foo", buf, sizeof(buf)) == -1);

    // Truncation
    assert(cmdline_get("root", buf, 6) == 0); // buffer size 6 -> 5 chars + null
    assert(strcmp(buf, "/dev/") == 0); // /dev/ is 5 chars.

    // Empty value
    cmdline_init("foo=");
    assert(cmdline_get("foo", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "") == 0);

    // Multiple spaces
    cmdline_init("  foo=bar   baz=qux  ");
    assert(cmdline_get("foo", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "bar") == 0);
    assert(cmdline_get("baz", buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "qux") == 0);

    // Boundary checks
    cmdline_init("foobar=1");
    assert(cmdline_get("foo", buf, sizeof(buf)) == -1); // Prefix match failure

    cmdline_init("bar=1");
    assert(cmdline_get("ar", buf, sizeof(buf)) == -1); // Suffix match failure

    printf("PASS\n");
}

int main(void) {
    printf("Starting host_test_cmdline...\n");
    test_cmdline_init();
    test_cmdline_has();
    test_cmdline_get();
    printf("All tests passed!\n");
    return 0;
}

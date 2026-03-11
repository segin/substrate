#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

// Mock kprint
void kprint(const char *msg) {
    printf("%s", msg);
}

#include <kern/cmdline.h>

void test_cmdline_init(void) {
    printf("Running test_cmdline_init...\n");

    char buf[1024];

    // Empty
    cmdline_init("");
    assert(cmdline_get_full(buf, sizeof(buf)) == 0);
    assert(buf[0] == 0);

    // Normal
    cmdline_init("foo=bar");
    assert(cmdline_get_full(buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "foo=bar") == 0);

    // Null
    cmdline_init(NULL);
    assert(cmdline_get_full(buf, sizeof(buf)) == 0);
    assert(buf[0] == 0);

    // Long
    char long_cmd[2000];
    memset(long_cmd, 'a', sizeof(long_cmd));
    long_cmd[sizeof(long_cmd)-1] = 0;
    cmdline_init(long_cmd);
    assert(cmdline_get_full(buf, sizeof(buf)) == 0);
    assert(strlen(buf) == 1023); // 1024 - 1

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

void test_cmdline_debug_enabled(void) {
    printf("Running test_cmdline_debug_enabled...\n");

    cmdline_init("debug=syscall,elf,perso:elks");
    assert(cmdline_debug_enabled("syscall"));
    assert(cmdline_debug_enabled("elf"));
    assert(cmdline_debug_enabled("perso:elks"));
    assert(cmdline_debug_enabled("perso:elks:aout"));
    assert(!cmdline_debug_enabled("perso:linux"));
    assert(!cmdline_debug_enabled("vm:brk"));

    cmdline_init("debug");
    assert(cmdline_debug_enabled("syscall"));
    assert(cmdline_debug_enabled("perso:linux"));

    cmdline_init("foo=bar debug=vm:brk,trap debug=perso:linux");
    assert(cmdline_debug_enabled("vm:brk"));
    assert(cmdline_debug_enabled("trap"));
    assert(cmdline_debug_enabled("perso:linux"));
    assert(!cmdline_debug_enabled("perso:freebsd"));

    printf("PASS\n");
}

int main(void) {
    printf("Starting host_test_cmdline...\n");
    test_cmdline_init();
    test_cmdline_has();
    test_cmdline_get();
    test_cmdline_debug_enabled();
    printf("All tests passed!\n");
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

// We need to mock console.h/kprint
void kprint(const char *s) {} 
#include <kern/cmdline.h>

void test_cmdline_parsing() {
    // Test 1: Simple key=value
    cmdline_init("foo=bar");
    char buf[32];
    assert(cmdline_get("foo", buf, 32) == 0);
    assert(strcmp(buf, "bar") == 0);
    
    // Test 2: Multiple keys
    cmdline_init("alpha=1 beta=2 gamma=3");
    assert(cmdline_get("beta", buf, 32) == 0);
    assert(strcmp(buf, "2") == 0);
    
    // Test 3: Boolean flags
    cmdline_init("quiet splash nosmp");
    assert(cmdline_has("quiet") == 1);
    assert(cmdline_has("nosmp") == 1);
    assert(cmdline_has("verbose") == 0);
    
    // Test 4: Truncation
    cmdline_init("long=abcdefghijklmnopqrstuvwxyz");
    assert(cmdline_get("long", buf, 10) == 0);
    assert(strlen(buf) == 9);
    assert(strcmp(buf, "abcdefghi") == 0);
    
    // Test 5: Not found
    cmdline_init("key=value");
    assert(cmdline_get("missing", buf, 32) == -1);
}

void property_test_random_strings() {
    srand(time(NULL));
    char cmd[1024];
    char key[16];
    char val[16];
    char out[32];
    
    for (int i = 0; i < 100; i++) {
        // Generate random key/value
        sprintf(key, "k%d", rand() % 100);
        sprintf(val, "v%d", rand() % 100);
        sprintf(cmd, "junk=1 %s=%s other=2", key, val);
        
        cmdline_init(cmd);
        
        // Verify we can find it
        if (cmdline_get(key, out, 32) != 0) {
            printf("FAIL: Could not find key '%s' in '%s'\n", key, cmd);
            assert(0);
        }
        if (strcmp(out, val) != 0) {
            printf("FAIL: Value mismatch. Expected '%s', got '%s'\n", val, out);
            assert(0);
        }
    }
    printf("Property test passed (100 iterations).\n");
}

int main() {
    test_cmdline_parsing();
    property_test_random_strings();
    return 0;
}

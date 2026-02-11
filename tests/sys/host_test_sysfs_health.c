#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

// Define HOST_TEST to prevent redefinition of types in sys/types.h
#define HOST_TEST

// Include host headers first if needed (already done above)

// Mock things needed by sysfs.c

// We need fs_node_t and dirent
// But sysfs.c includes <vfs/vfs.h>
// We must ensure vfs/vfs.h can be found and compiled.

// We need to define types used in vfs.h that might be skipped by HOST_TEST in sys/types.h
// or provided by host headers.
// Host headers provide: pid_t, uid_t, gid_t, off_t, size_t, ssize_t.
// Kernel sys/types.h defines:
// typedef int64_t off_t; (unconditional) -> CONFLICT with host potentially?
// If host is 64-bit, off_t is usually 64-bit (long). int64_t is long. So it matches.
// If host is 32-bit, off_t is 32-bit (long). int64_t is long long. CONFLICT.
// The environment seems to be 64-bit Linux (based on previous ls output showing x86_64-linux-gnu).

// However, we can use a trick: Include kernel headers with -I, but force include host headers first.
// The conflict happens if kernel header is included.

// Let's try to mock the minimal parts required by sysfs.c if we can avoid including full vfs.h
// sysfs.c includes <vfs/vfs.h> and <sys/kobject.h>.
// We can create mock headers or just include the real ones.
// The real ones are best to ensure we match the struct layout if possible, but for this test we only access `name`.

// Let's try to compile against real headers first.

// We need to mock vfs_register_filesystem
typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// We need to include sysfs.c
// But first we need to make sure its includes work.
// We will compile with -I../../sys/include -I../../include
// So <vfs/vfs.h> will be found.

#include "../../sys/fs/sysfs.c"

int main() {
    printf("Running SysFS Health Test...\n");

    // Initialize sysfs (optional, but good practice)
    sysfs_init();

    // 1. Test valid inputs
    // sysfs_finddir uses a static buffer for sub_node.
    // We expect it to return a node for "bus", "class", "devices".

    const char *valid_names[] = {"bus", "class", "devices"};
    for (int i = 0; i < 3; i++) {
        fs_node_t *node = sysfs_finddir(NULL, (char*)valid_names[i]);
        assert(node != NULL);
        assert(strcmp(node->name, valid_names[i]) == 0);
        printf("PASS: sysfs_finddir(\"%s\") returned correct node.\n", valid_names[i]);
    }

    // 2. Test invalid inputs
    fs_node_t *node = sysfs_finddir(NULL, "invalid");
    assert(node == NULL);
    printf("PASS: sysfs_finddir(\"invalid\") returned NULL.\n");

    // 3. Test long input (should be invalid and return NULL)
    // sysfs_finddir logic compares string literals, so long input fails strcmp check immediately.
    // It doesn't reach strcpy.
    char long_name[200];
    memset(long_name, 'A', 199);
    long_name[199] = '\0';
    node = sysfs_finddir(NULL, long_name);
    assert(node == NULL);
    printf("PASS: sysfs_finddir(long_name) returned NULL.\n");

    // We cannot easily test the buffer overflow fix because it's unreachable with current logic.
    // But we verified the logic is correct.

    printf("All tests passed.\n");
    return 0;
}

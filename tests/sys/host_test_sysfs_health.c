#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#define HOST_TEST

typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Add strlcpy implementation for host test
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t srclen = strlen(src);
    if (size > 0) {
        size_t len = (srclen >= size) ? size - 1 : srclen;
        memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return srclen;
}

#include "../../sys/fs/sysfs.c"

int main() {
    printf("Running SysFS Health Test...\n");

    sysfs_init();

    // 1. Test valid inputs
    // sysfs_finddir uses a static buffer for sub_node.
    // We expect it to return a node for "bus", "class", "devices".

    const char *valid_names[] = {"bus", "class", "devices"};
    for (int i = 0; i < 3; i++) {
        fs_node_t *node = sysfs_finddir(NULL, (char*)valid_names[i]);
        if (node == NULL) {
             printf("FAIL: sysfs_finddir(\"%s\") returned NULL.\n", valid_names[i]);
             exit(1);
        }
        if (strcmp(node->name, valid_names[i]) != 0) {
             printf("FAIL: sysfs_finddir(\"%s\") returned name \"%s\".\n", valid_names[i], node->name);
             exit(1);
        }
        printf("PASS: sysfs_finddir(\"%s\") returned correct node.\n", valid_names[i]);
    }

    // 2. Test invalid inputs
    fs_node_t *node = sysfs_finddir(NULL, "invalid");
    if (node != NULL) {
        printf("FAIL: sysfs_finddir(\"invalid\") returned non-NULL.\n");
        exit(1);
    }
    printf("PASS: sysfs_finddir(\"invalid\") returned NULL.\n");

    // 3. Test long input (should be invalid and return NULL)
    char long_name[200];
    memset(long_name, 'A', 199);
    long_name[199] = '\0';
    node = sysfs_finddir(NULL, long_name);
    if (node != NULL) {
        printf("FAIL: sysfs_finddir(long_name) returned non-NULL.\n");
        exit(1);
    }
    printf("PASS: sysfs_finddir(long_name) returned NULL.\n");

    printf("All tests passed.\n");
    return 0;
}

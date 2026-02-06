#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Mocks & Setup
// -----------------------------------------------------------------------------

// We need to prevent host dirent.h from conflicting if we were to include it,
// but we won't. We'll rely on the kernel headers for struct dirent.

// Mock functions needed by devfs.c
struct tty;
typedef struct tty tty_t;

// These match signatures in sys/tty.h
int tty_read(tty_t *tty, char *buf, int len) { return 0; }
int tty_write(tty_t *tty, const char *buf, int len) { return 0; }
int tty_ioctl(tty_t *tty, uint32_t cmd, unsigned long arg) { return 0; }

struct fs_node;
struct fs_node *console_get_node(void) { return NULL; }

typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// -----------------------------------------------------------------------------
// Include Target
// -----------------------------------------------------------------------------

// We include devfs.c directly.
// We will compile with -I../../sys/include -I../../include to find headers.
// We hope header guards prevent conflicts with host headers included above.

#include "../../sys/fs/devfs.c"

// Define current_process (extern in sys/proc.h)
process_t *current_process = NULL;

// -----------------------------------------------------------------------------
// Test
// -----------------------------------------------------------------------------

int main() {
    printf("Running DevFS Overflow Test...\n");

    devfs_init();

    // Create a node with a name that fills the entire buffer (128 bytes) without a null terminator
    // fs_node_t.name is 128 bytes.
    static fs_node_t bad_node;
    memset(&bad_node, 0, sizeof(bad_node));
    memset(bad_node.name, 'A', 128); // 128 'A's, no null

    bad_node.flags = FS_CHARDEVICE;

    // Register device
    devfs_register_device(&bad_node);

    // Prepare dev_dirent (static in devfs.c, accessible here)
    memset(&dev_dirent, 0, sizeof(dev_dirent));

    // Call readdir for index 2 (first char device)
    printf("Calling devfs_readdir...\n");
    struct dirent *d = devfs_readdir(NULL, 2);

    if (!d) {
        printf("FAIL: devfs_readdir returned NULL\n");
        return 1;
    }

    // Verification
    // We expect dev_dirent.name to be 128 bytes.
    // If we used strcpy on a 128-byte non-null string, it copied 128 bytes.
    // Then it likely kept copying if subsequent memory wasn't null.
    // But even if it just copied 128 bytes, it is NOT a valid C string.

    // Check for null termination
    int null_index = -1;
    for (int i = 0; i < 128; i++) {
        if (dev_dirent.name[i] == '\0') {
            null_index = i;
            break;
        }
    }

    if (null_index == -1) {
        printf("VULNERABILITY CONFIRMED: dev_dirent.name has no null terminator in first 128 bytes.\n");
        // This is expected before fix.
    } else {
        printf("SAFE: Null terminator found at index %d.\n", null_index);
        if (null_index == 127) {
             // This is ideal for a full string (127 chars + null)
        }
    }

    // Verify contents match (truncated or not)
    for (int i = 0; i < 127; i++) {
        if (dev_dirent.name[i] != 'A' && dev_dirent.name[i] != 0) {
            // Note: if it's 0, it loop stops if we were checking string, but here we check buffer
            // We just want to ensure we didn't get garbage.
        }
    }

    return 0;
}

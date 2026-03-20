#include <kern/console.h>
#include <vfs/vfs.h>
#include <string.h>
#include <vm/vm_kmem.h>

static int mock_mknod_called = 0;
static const char *mock_mknod_name = NULL;
static uint16_t mock_mknod_mode = 0;
static uint32_t mock_mknod_dev = 0;

static int mock_mknod_fn(struct fs_node *node, const char *name, uint16_t mode, uint32_t dev) {
    (void)node;
    mock_mknod_called = 1;
    mock_mknod_name = name;
    mock_mknod_mode = mode;
    mock_mknod_dev = dev;
    return 0; // Return success
}

void run_mknod_fs_tests(void) {
    kprint("TEST: mknod_fs_tests starting...\n");

    // Test 1: NULL node
    kprint("TEST: mknod_fs with NULL node... ");
    int ret = mknod_fs(NULL, "test", 0, 0);
    if (ret == -1) {
        kprint("PASS\n");
    } else {
        kprint("FAIL (expected -1)\n");
    }

    // Test 2: Node without mknod function
    fs_node_t *node_no_mknod = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(node_no_mknod, 0, sizeof(fs_node_t));

    kprint("TEST: mknod_fs with node lacking mknod pointer... ");
    ret = mknod_fs(node_no_mknod, "test", 0, 0);
    if (ret == -1) {
        kprint("PASS\n");
    } else {
        kprint("FAIL (expected -1)\n");
    }
    kfree(node_no_mknod, sizeof(fs_node_t));

    // Test 3: Node with mknod function
    fs_node_t *node_with_mknod = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(node_with_mknod, 0, sizeof(fs_node_t));
    node_with_mknod->mknod = mock_mknod_fn;

    mock_mknod_called = 0;
    mock_mknod_name = NULL;
    mock_mknod_mode = 0;
    mock_mknod_dev = 0;

    kprint("TEST: mknod_fs with valid node and mknod pointer... ");
    ret = mknod_fs(node_with_mknod, "test_file", 0644, 123);
    if (ret == 0 && mock_mknod_called &&
        strcmp(mock_mknod_name, "test_file") == 0 &&
        mock_mknod_mode == 0644 && mock_mknod_dev == 123) {
        kprint("PASS\n");
    } else {
        kprint("FAIL\n");
    }
    kfree(node_with_mknod, sizeof(fs_node_t));

    kprint("TEST: mknod_fs_tests finished.\n");
}

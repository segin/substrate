#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/fb.h>
#include <vm/vm_kmem.h>
#include <string.h>

extern fs_node_t *fs_root;

void test_fb_modes(void) {
    kprint("\n=== Framebuffer Modes Test ===\n");

    fs_node_t *node = vfs_lookup(fs_root, "/dev/fb0");
    if (!node) {
        kprint("FAIL: Could not lookup /dev/fb0 (ensure devfs is mounted)\n");
        return;
    }

    if (!node->ioctl) {
        kprint("FAIL: /dev/fb0 has no ioctl handler\n");
        return;
    }

    struct video_mode_query query;
    memset(&query, 0, sizeof(query));

    /* First pass: Get count */
    query.count = 0;
    query.modes = NULL;

    kprint("Checking mode count (query.modes=NULL)...\n");
    int ret = node->ioctl(node, FBIOGET_VIDEO_MODES, &query);

    if (ret != 0) {
        kprint("FAIL: ioctl(FBIOGET_VIDEO_MODES) returned error\n");
        return;
    }

    extern int kprintf(const char *fmt, ...);
    kprintf("Mode count reported: %d\n", query.count);

    if (query.count == 0) {
        kprint("WARN: No video modes reported (count=0)\n");
    } else {
        /* Second pass: Get modes */
        size_t alloc_size = query.count * sizeof(struct video_mode_info);
        struct video_mode_info *modes = kmalloc(alloc_size);
        if (!modes) {
            kprint("FAIL: Allocation failed\n");
            return;
        }

        query.modes = modes;
        /* query.count is already set to total from previous call */

        kprint("Retrieving modes...\n");
        ret = node->ioctl(node, FBIOGET_VIDEO_MODES, &query);
        if (ret != 0) {
            kprint("FAIL: ioctl(FBIOGET_VIDEO_MODES) with buffer returned error\n");
            kfree(modes, alloc_size);
            return;
        }

        kprint("Modes retrieved:\n");
        for (uint32_t i = 0; i < query.count; i++) {
            kprintf("  Mode %d: %dx%dx%d (ID: %d)\n", i, modes[i].width, modes[i].height, modes[i].bpp, modes[i].mode_id);
        }

        kfree(modes, alloc_size);
        kprint("PASS: Modes retrieved successfully.\n");
    }
}

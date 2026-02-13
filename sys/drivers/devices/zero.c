/*
 * /dev/zero - Source of zeroed memory
 *
 * Implements a character device that:
 * - Returns an infinite stream of zero bytes on read.
 * - Accepts and discards any data written to it.
 * - Supports mmap() for zero-filled memory mappings.
 *
 * This implementation follows BSD-like semantics used by Substrate.
 */

#include <vfs/vfs.h>
#include <sys/errno.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <sys/proc.h>
#include <kern/console.h>

#define KERNEL_DIRECT_MAP_BASE 0xC0000000U

static inline uint32_t zero_phys_from_virt(void *virt) {
    return (uint32_t)(uintptr_t)virt - KERNEL_DIRECT_MAP_BASE;
}

static inline void *zero_virt_from_phys(uint32_t phys) {
    return (void *)(uintptr_t)(phys + KERNEL_DIRECT_MAP_BASE);
}

static fs_node_t zero_node;

static void zero_open(fs_node_t *node) {
    (void)node;
}

static void zero_close(fs_node_t *node) {
    (void)node;
}

/*
 * zero_read - Returns zeros to the caller.
 */
static size_t zero_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;

    memset(buffer, 0, size);
    return size;
}

/*
 * zero_write - Discards all input.
 */
static size_t zero_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    (void)offset;
    (void)buffer;
    return size;
}

/*
 * zero_ioctl - No ioctls supported.
 */
static int zero_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    (void)request;
    (void)arg;
    return -ENOTTY;
}

/*
 * zero_poll - Always ready for read and write.
 */
static int zero_poll(fs_node_t *node, void *waiter) {
    (void)node;
    (void)waiter;
    return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
}

/*
 * zero_mmap - Map zero-filled pages.
 *
 * The current VM path eagerly maps pages for device-backed mappings,
 * so this implementation allocates and zeros pages up front.
 */
static void *zero_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node;
    (void)prot;
    (void)flags;
    (void)offset;

    uint32_t start = (uint32_t)addr;
    uint32_t end;

    if (length == 0) {
        return (void *)-1;
    }

    length = (length + 0xFFFU) & ~0xFFFU;
    end = start + (uint32_t)length;
    if (end < start) {
        return (void *)-1;
    }

    extern process_t *current_process;

    for (uint32_t virt = start; virt < end; virt += 4096) {
        void *page_virt = pmm_alloc_block();
        if (!page_virt) {
            for (uint32_t cleanup_virt = start; cleanup_virt < virt; cleanup_virt += 4096) {
                uint32_t phys = pmap_extract(current_process->pmap, cleanup_virt);
                if (phys) {
                    pmm_free_block(zero_virt_from_phys(phys));
                    pmap_remove(current_process->pmap, cleanup_virt);
                }
            }
            kprint("zero_mmap: OOM\n");
            return (void *)-1;
        }

        memset(page_virt, 0, 4096);

        if (pmap_enter(current_process->pmap, virt, zero_phys_from_virt(page_virt), prot, 0) < 0) {
            pmm_free_block(page_virt);

            for (uint32_t cleanup_virt = start; cleanup_virt < virt; cleanup_virt += 4096) {
                uint32_t phys = pmap_extract(current_process->pmap, cleanup_virt);
                if (phys) {
                    pmm_free_block(zero_virt_from_phys(phys));
                    pmap_remove(current_process->pmap, cleanup_virt);
                }
            }
            return (void *)-1;
        }
    }

    return addr;
}

/*
 * zero_init - Initialize and register the device.
 */
void zero_init(void) {
    memset(&zero_node, 0, sizeof(fs_node_t));
    strcpy(zero_node.name, "zero");
    zero_node.flags = FS_CHARDEVICE;
    zero_node.mask = 0666;
    zero_node.uid = 0;
    zero_node.gid = 0;
    zero_node.open = &zero_open;
    zero_node.close = &zero_close;
    zero_node.read = &zero_read;
    zero_node.write = &zero_write;
    zero_node.ioctl = &zero_ioctl;
    zero_node.poll = &zero_poll;
    zero_node.mmap = &zero_mmap;
    zero_node.rdev = (1 << 8) | 5;

    devfs_register_device(&zero_node);
    kprint("/dev/zero initialized\n");
}

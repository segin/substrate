/*
 * /dev/zero - Source of zeroed memory
 *
 * Implements a character device that:
 * - Returns an infinite stream of zero bytes on read.
 * - Accepts and discards any data written to it.
 * - Supports mmap() for zero-filled memory mappings.
 *
 * This implementation follows BSD semantics.
 */

#include <vfs/vfs.h>
#include <sys/errno.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <sys/vm/vm_area.h>
#include <kern/console.h>

#define PHYSICAL_d(x) ((uint32_t)(x) - 0xC0000000)
#define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))

static fs_node_t zero_node;

/*
 * zero_read - Returns zeros to the caller.
 *
 * Use memset for efficiency. For very large reads (>1MB),
 * we could map the zero page, but memset is generally highly optimized
 * and avoids VM complexity.
 */
static size_t zero_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;

    // Use efficient memset for zeroing the user buffer.
    // Note: buffer is a user pointer, but in this kernel (Substrate),
    // user memory is accessible (or mapped) in kernel context during syscalls.
    // If strict SMAP were enabled, we'd need copyout/memset_user.
    // Assuming buffer is valid and accessible.

    memset(buffer, 0, size);
    return size;
}

/*
 * zero_write - Discards all input.
 *
 * Returns the number of bytes written (always successful).
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
 * In a fully demand-paged system, this would register a VM object
 * backed by the zero page (COW).
 *
 * However, the current kernel's mmap implementation (sys_mmap) expects
 * device drivers to eagerly map pages if they support mmap.
 * (See sys/vm/vm_mmap.c:sys_mmap implementation).
 *
 * Thus, we eagerly allocate and zero pages for the requested range.
 */
static void *zero_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node;
    (void)prot;
    (void)flags;
    (void)offset;

    uint32_t start = (uint32_t)addr;
    uint32_t end = start + length;

    extern process_t *current_process;

    // Iterate over the range page by page
    for (uint32_t virt = start; virt < end; virt += 4096) {
        // Allocate a physical page
        void *page_virt = pmm_alloc_block();
        if (!page_virt) {
            // Allocation failed. Cleanup partial mapping.
            for (uint32_t cleanup_virt = start; cleanup_virt < virt; cleanup_virt += 4096) {
                uint32_t phys = pmap_extract(current_process->pmap, cleanup_virt);
                if (phys) {
                    void *cleanup_page_virt = VIRTUAL_d(phys);
                    pmm_free_block(cleanup_page_virt);
                    pmap_remove(current_process->pmap, cleanup_virt);
                }
            }
            kprint("zero_mmap: OOM\n");
            return (void *)-1;
        }

        // Zero the page (page_virt is the kernel virtual address of the page)
        memset(page_virt, 0, 4096);

        // Calculate physical address
        uint32_t phys = PHYSICAL_d(page_virt);

        // Map it into the process address space at 'virt'
        if (pmap_enter(current_process->pmap, virt, phys, prot, 0) < 0) {
            kprint("zero_mmap: pmap_enter failed\n");
            // Free the page we just allocated
            pmm_free_block(page_virt);

            // Cleanup partial mapping
            for (uint32_t cleanup_virt = start; cleanup_virt < virt; cleanup_virt += 4096) {
                uint32_t p = pmap_extract(current_process->pmap, cleanup_virt);
                if (p) {
                    void *cpv = VIRTUAL_d(p);
                    pmm_free_block(cpv);
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
    zero_node.read = &zero_read;
    zero_node.write = &zero_write;
    zero_node.ioctl = &zero_ioctl;
    zero_node.poll = &zero_poll;
    zero_node.mmap = &zero_mmap;
    zero_node.rdev = (1 << 8) | 5; // Major 1, Minor 5 (Standard for /dev/zero in this OS)

    // Permissions: rw-rw-rw- (0666) or similar.
    // devfs_register_device usually sets defaults, but we can set mask/uid/gid if needed.
    // zero_node.mask = 0666;

    devfs_register_device(&zero_node);
    kprint("/dev/zero initialized\n");
}

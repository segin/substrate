/*
 * sys/kern/file.c - Kernel File Structure Management
 *
 * Implements dynamic allocation for file structures using kmalloc/kfree.
 * Replaces the old static array implementation.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <kern/file.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <vfs/vfs.h>

/*
 * file_alloc - Allocate a file structure from the system pool.
 * Returns a zero-initialized file structure with f_count set to 1.
 */
file_t *file_alloc(void) {
    file_t *f = kmalloc(sizeof(file_t));
    if (f) {
        memset(f, 0, sizeof(file_t));
        f->f_count = 1;
    }
    return f;
}

/*
 * file_free - Free a file structure back to the system pool.
 */
void file_free(file_t *f) {
    if (!f) return;

    // Clear fields to help debugging use-after-free
    f->f_data = NULL;
    f->f_count = 0;

    kfree(f, sizeof(file_t));
}

/*
 * file_close_ptr - Close a file pointer (decrement refcount, call close_fs if 0)
 */
void file_close_ptr(file_t *f) {
    if (!f) return;

    f->f_count--;

    if (f->f_count <= 0) {
        if (f->f_data) {
            close_fs((fs_node_t*)f->f_data);
        }
        file_free(f);
    }
}

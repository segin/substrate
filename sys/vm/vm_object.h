#ifndef _VM_OBJECT_H
#define _VM_OBJECT_H

#include <stdint.h>
#include <stdbool.h>
#include <vm/vm_page.h>

// VM Object types
typedef enum {
    VM_OBJ_TYPE_DEFAULT,    // Anonymous zero-filled memory
    VM_OBJ_TYPE_VNODE,      // Backed by a file (vnode)
    VM_OBJ_TYPE_DEVICE,     // Device memory mapping
    VM_OBJ_TYPE_PHYS,       // Direct physical memory mapping
    VM_OBJ_TYPE_SWAP,       // Swapped out backing
    VM_OBJ_TYPE_DEAD        // Object being destroyed
} vm_object_type_t;

// Forward declaration for pager
struct vm_pager;

// VM Object: Abstract representation of a data source for memory mappings.
typedef struct vm_object {
    vm_page_t *pages;           // List of resident pages
    uint32_t page_count;        // Number of resident pages
    uint32_t resident_count;    // Resident page count for statistics

    vm_object_type_t type;
    size_t size;                // Object size in bytes
    int ref_count;              // Reference count

    void *handle;               // Vnode pointer or other backing handle
    struct vm_pager *pager;     // Pager for page-in/page-out

    struct vm_object *shadow;   // Shadow object for COW
    uint64_t shadow_offset;     // Offset into shadow object

    struct vm_object *next;     // Global object list
    struct vm_object *prev;

    uint16_t flags;             // Object flags

    uint32_t magic;             // Use-after-free canary (VM_OBJECT_MAGIC)
} vm_object_t;

#define VM_OBJECT_MAGIC  0x564D4F42u   /* "VMOB" — live object */
#define VM_OBJECT_DEAD   0xDEAD0B7Au   /* stamped just before free */

// Object flags
#define VM_OBJ_INTERNAL     0x01    // Internal (not file-backed)
#define VM_OBJ_DEAD         0x02    // Being destroyed
#define VM_OBJ_COPY         0x04    // Copy-on-write source

// API
void vm_object_init(void);
vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size);
void vm_object_reference(vm_object_t *object);
void vm_object_deallocate(vm_object_t *object);

/*
 * vm_object_try_reference - bump ref_count IFF the object is still live
 * (magic is correct AND ref_count > 0).  Used by weak-reference caches
 * (e.g. shared_file_objects in vm_syscalls.c) to promote a raw cache
 * pointer to a real reference without racing against a concurrent
 * deallocate that is between "dec to 0" and "tear down".  Returns 1
 * on success, 0 if the object is dying or dead.
 */
int  vm_object_try_reference(vm_object_t *object);

/*
 * vm_syscalls_evict_shared_obj - if `object` is currently in the
 * shared-file-object cache, unlink the entry.  Called from
 * vm_object_deallocate at ref_count == 0 so the cache holds a weak
 * reference (entry but no extra refcount) and concurrent lookups
 * can't revive a dying object.
 */
void vm_syscalls_evict_shared_obj(vm_object_t *object);

/*
 * An entry of that cache (vm_syscalls.c).  Its key MUST be a stable file
 * identity, not the fs_node_t pointer: ext2 (and other filesystems) hand
 * back freshly allocated / cache-recycled fs_node_t instances on each open,
 * so keying on the pointer makes every process miss the cache and build its
 * own private backing object -- which is why each program got a private
 * physical copy of every shared library (≈10 MB per xterm).  (mount, inode)
 * is stable across opens and unique across mounts, so two processes mapping
 * the same file share one backing object and therefore the same read-only
 * physical pages.
 */
struct mount;
typedef struct shared_file_object_entry {
    struct mount *mp;
    uint64_t inode;
    uint64_t page_offset;
    vm_object_t *object;
    struct shared_file_object_entry *next;
} shared_file_object_entry_t;

vm_object_t *vm_object_shadow(vm_object_t *source);
int vm_object_collapse(vm_object_t *object);

/* The page-fault path's object lock (vm_fault.c), for page-list and
 * shadow-chain changes made outside a fault.  Recursive for its holder;
 * taken after any vm_map lock, never before one.  A mutex: it may sleep. */
void vm_object_chain_lock(void);
void vm_object_chain_unlock(void);

// Page management
void vm_object_add_page(vm_object_t *object, vm_page_t *page);
void vm_object_remove_page(vm_object_t *object, vm_page_t *page);
/* Drop `page` from the page-lookup hash if it is still there.  Called
 * when a page is freed, as a backstop to vm_object_remove_page(). */
void vm_object_forget_page(vm_page_t *page);
#ifdef HOST_TEST
/* Empty the page-lookup hash (host unit tests only; see vm_object.c). */
void vm_object_hash_reset(void);
#endif
vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex);

/*
 * Get (or create-and-cache) a shared vnode-backed vm_object for the
 * given file region.  Used by both sys_mmap(MAP_SHARED, fd, ...) and
 * the ELF loader so that `.text` pages of an executable are shared
 * across every process exec'ing the same binary.
 *
 * Returns a referenced vm_object — caller must vm_object_deallocate
 * when its mapping reference goes away.
 */
struct fs_node;
vm_object_t *mmap_get_shared_backing_object(struct fs_node *node, size_t length,
                                             uint32_t vm_prot, uint64_t offset);

#endif

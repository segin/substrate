/*
 * sys/exec.h - Binary Execution Subsystem
 *
 * Defines the interface for plugging in binary format handlers (ELF, ELKS, Script, etc).
 */

#ifndef _SYS_EXEC_H
#define _SYS_EXEC_H

#include <sys/types.h>

/*
 * exec_binary_handler
 *
 * A handler for a specific binary format.
 */
struct exec_binary_handler {
    const char *name;
    
    /* 
     * Check if the file corresponds to this format.
     * Returns: 0 on success (format matches), non-zero otherwise.
     * Often checks the first few bytes (magic number).
     */
    int (*check)(const char *path, const char *header_buf, size_t len);
    
    /*
     * Load the binary.
     * Returns: 0 on success, negative error code on failure.
     */
    int (*load)(int fd, const char *path, char *const argv[], char *const envp[]);
    
    struct exec_binary_handler *next;
};

/*
 * Register a new binary format handler.
 * Handlers are tried in the order registered (LIFO or priority based in future).
 */
void exec_register_handler(struct exec_binary_handler *handler);
void exec_init(void);

/*
 * Dispatch execution to a matching handler.
 * Reads the header once and iterates through handlers.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]);
/* Entry `index` of an argv/envp vector, read at the calling process's
 * pointer width when the vector is still in user memory.  0 or EFAULT. */
int exec_vec_ptr(char *const array[], int index, char **out);
void exec_pin_current_thread(void);
void exec_unpin_current_thread(void);
void exec_maybe_unpin_current_thread(int from_user);

/*
 * Per-thread exec-arg cleanup list.  Personalities that kmalloc argv/envp
 * out of segmented user memory (e.g. ELKS) push their kbufs here before
 * calling kern_execve.  Format handlers drain the list immediately before
 * the userspace jump on success; kern_execve drains it on failure.
 *
 * exec_cleanup_push: returns 0 on success, -1 if the list is full.
 * exec_cleanup_drain: idempotent — safe to call when count==0.
 */
int exec_cleanup_push(void (*free_fn)(void *), void *ptr);
void exec_cleanup_drain(void);

/*
 * A loader's new address space, kept beside the caller's until the loader
 * commits.  exec_space_begin() creates a pmap and a vm_map over [lo, hi),
 * makes them the process's and switches to them.  Every step that can fail
 * runs before exec_space_commit(), which destroys the caller's address
 * space; a failure before that calls exec_space_abort(), which switches
 * back to the caller's and destroys the new one, so the failed exec
 * returns an error to the old image instead of leaving it on a half-built
 * address space (or none).  The stack and argument bounds a loader records
 * while building the new stack are put back too.
 *
 * exec_space_begin: 0, or -ENOMEM with nothing changed.
 */
struct pmap;
struct vm_map;
struct exec_space {
    struct pmap *old_pmap;
    struct vm_map *old_map;
    struct pmap *pmap;
    struct vm_map *map;
    uintptr_t old_ustack_top, old_ustack_limit;
    uint32_t old_arg_start, old_arg_end;
};
int exec_space_begin(struct exec_space *es, uintptr_t lo, uintptr_t hi);
void exec_space_abort(struct exec_space *es);
void exec_space_commit(struct exec_space *es);

#ifdef HOST_TEST
int exec_handler_registered(const char *name);
#endif

#endif /* _SYS_EXEC_H */

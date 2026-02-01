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
    int (*load)(const char *path, char *const argv[], char *const envp[]);
    
    struct exec_binary_handler *next;
};

/*
 * Register a new binary format handler.
 * Handlers are tried in the order registered (LIFO or priority based in future).
 */
void exec_register_handler(struct exec_binary_handler *handler);

/*
 * Dispatch execution to a matching handler.
 * Reads the header once and iterates through handlers.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]);

#endif /* _SYS_EXEC_H */

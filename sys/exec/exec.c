/*
 * sys/exec/exec.c - Binary Format Dispatcher
 *
 * Manages registration and invocation of binary format handlers.
 */

#include <sys/exec.h>
#include <stddef.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <kern/console.h>
struct thr_param;
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/fcntl.h>

static struct exec_binary_handler *exec_handlers = NULL;

void exec_register_handler(struct exec_binary_handler *handler) {
    if (!handler) return;
    
    // Add to head of list (LIFO, so newer handlers can override if needed)
    handler->next = exec_handlers;
    exec_handlers = handler;
}

/*
 * exec_dispatch
 *
 * Reads the first chunk of the file to determine the format, then calls the
 * appropriate loader.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]) {
    if (!path) return -ENOENT;

    // 0. Check execute permissions
    if (kern_access(path, X_OK) != 0) {
        return -EACCES;
    }

    // 1. Open the file to read the header
    int fd = kern_open(path, O_RDONLY, 0);
    if (fd < 0) return fd; // Propagate error (ENOENT, EACCES)

    // 3. Read the header (magic bytes)
    char header_buf[256];
    int len = kern_read(fd, header_buf, sizeof(header_buf));
    
    kern_close(fd);

    if (len < 0) return len;
    
    // 4. Iterate through handlers
    struct exec_binary_handler *h = exec_handlers;
    while (h) {
        if (h->check && h->check(path, header_buf, len) == 0) {
            // Match found!
            if (h->load) {
                return h->load(path, argv, envp);
            }
        }
        h = h->next;
    }

    return -ENOEXEC;
}

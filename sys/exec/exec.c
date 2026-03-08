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
#include <sys/proc.h>
#include <sys/stat.h>
#include <kern/console.h>
#include <exec/formats/elf.h>
struct thr_param;
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/fcntl.h>
#include <kern/sched.h>

static struct exec_binary_handler *exec_handlers = NULL;

void exec_register_handler(struct exec_binary_handler *handler) {
    if (!handler) return;
    
    // Add to head of list (LIFO, so newer handlers can override if needed)
    handler->next = exec_handlers;
    exec_handlers = handler;
}

void exec_pin_current_thread(void) {
    if (!current_thread || current_thread->exec_pin_active) {
        return;
    }

    int cpu_id = smp_get_cpu_id();
    if (cpu_id < 0) {
        cpu_id = 0;
    }

    current_thread->exec_saved_bound_cpu = current_thread->bound_cpu;
    current_thread->exec_saved_no_preempt =
        (current_thread->flags & THREAD_F_NO_PREEMPT) ? 1 : 0;
    current_thread->bound_cpu = (int16_t)cpu_id;
    current_thread->flags |= THREAD_F_NO_PREEMPT;
    current_thread->exec_pin_active = 1;
}

void exec_unpin_current_thread(void) {
    if (!current_thread || !current_thread->exec_pin_active) {
        return;
    }

    current_thread->bound_cpu = current_thread->exec_saved_bound_cpu;
    if (!current_thread->exec_saved_no_preempt) {
        current_thread->flags &= ~THREAD_F_NO_PREEMPT;
    }
    current_thread->exec_saved_bound_cpu = -1;
    current_thread->exec_saved_no_preempt = 0;
    current_thread->exec_pin_active = 0;
}

/*
 * exec_dispatch
 *
 * Reads the first chunk of the file to determine the format, then calls the
 * appropriate loader.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]) {
    if (!path) return -ENOENT;

    // 1. Open the file to read the header
    int fd = kern_open(path, O_RDONLY, 0);
    if (fd < 0) return fd; // Propagate error (ENOENT, EACCES)

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) {
        kern_close(fd);
        return -ENOENT;
    }

    fs_node_t *node = (fs_node_t *)f->f_data;
    if (vfs_check_permissions(node, current_process->uid, current_process->gid, X_OK) != 0) {
        kern_close(fd);
        return -EACCES;
    }

    // 3. Read the header (magic bytes)
    char header_buf[256];
    int len = kern_read(fd, header_buf, sizeof(header_buf));
    
    if (len < 0) {
        kern_close(fd);
        return len;
    }
    
    // 4. Iterate through handlers
    struct exec_binary_handler *h = exec_handlers;
    while (h) {
        if (h->check && h->check(path, header_buf, len) == 0) {
            // Match found!
            if (h->load) {
                return h->load(fd, path, argv, envp);
            }
        }
        h = h->next;
    }

    /*
     * Fallback for the current in-tree loader wiring:
     * userland execve() must still be able to execute ELF even when
     * no explicit handler registration has occurred yet.
     */
    if (len >= 4 &&
        (unsigned char)header_buf[0] == 0x7f &&
        header_buf[1] == 'E' &&
        header_buf[2] == 'L' &&
        header_buf[3] == 'F') {
        return elf_execve(fd, path, argv, envp);
    }

    kern_close(fd);
    return -ENOEXEC;
}

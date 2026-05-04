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
#include <exec/formats/elks_aout.h>
#include <exec/formats/script.h>
struct thr_param;
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/fcntl.h>
#include <kern/sched.h>
#include <stdio.h>

static struct exec_binary_handler *exec_handlers = NULL;

void exec_init(void) {
    elks_init_handler();
    script_init_handler();
}

void exec_register_handler(struct exec_binary_handler *handler) {
    if (!handler) return;
    
    // Add to head of list (LIFO, so newer handlers can override if needed)
    handler->next = exec_handlers;
    exec_handlers = handler;
}

#ifdef HOST_TEST
int exec_handler_registered(const char *name) {
    struct exec_binary_handler *h;

    if (!name) {
        return 0;
    }
    for (h = exec_handlers; h; h = h->next) {
        if (h->name && strcmp(h->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

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

void exec_maybe_unpin_current_thread(int from_user) {
    if (!from_user) {
        return;
    }
    exec_unpin_current_thread();
}

/*
 * exec_dispatch
 *
 * Reads the first chunk of the file to determine the format, then calls the
 * appropriate loader.
 */
/*
 * NOTE(2026-05): Removing the [edis]/[exve] kprints below causes init exec
 * to hang on first invocation under single-CPU configs.  The prints are
 * inadvertently providing memory/serialization barriers that paper over a
 * timing race somewhere between exec_pin_current_thread() and the new
 * program actually running.  Keeping them in for now until the underlying
 * race is identified — they are cheap enough relative to a full process
 * exec, and the kernel already prints during exec under syscall_trace.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]) {
    kprint("[edis] enter\n");
    if (!path) return -ENOENT;

    kprint("[edis] open\n");
    int fd = kern_open(path, O_RDONLY, 0);
    if (fd < 0) return fd;
    kprint("[edis] open ok\n");

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) {
        kern_close(fd);
        return -ENOENT;
    }

    fs_node_t *node = (fs_node_t *)f->f_data;
    if (vfs_check_permissions(node, current_process->euid, current_process->egid, X_OK) != 0) {
        kern_close(fd);
        return -EACCES;
    }

    kprint("[edis] read\n");
    char header_buf[256];
    int len = kern_read(fd, header_buf, sizeof(header_buf));
    if (len < 0) {
        kern_close(fd);
        return len;
    }
    kprint("[edis] read ok\n");

    // Iterate through handlers
    struct exec_binary_handler *h = exec_handlers;
    while (h) {
        kprint("[edis] try handler\n");
        if (h->check && h->check(path, header_buf, len) == 0) {
            kprint("[edis] match\n");
            if (h->load) {
                int ret = h->load(fd, path, argv, envp);
                if (ret != 0) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "exec: handler %s failed for %s (%d)\n",
                             h->name ? h->name : "(unnamed)", path, ret);
                    kprint(buf);
                }
                return ret;
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
        kprint("[edis] fallback elf_execve\n");
        return elf_execve(fd, path, argv, envp);
    }

    {
        char buf[96];
        snprintf(buf, sizeof(buf), "exec: no handler matched %s\n", path);
        kprint(buf);
    }
    kern_close(fd);
    return -ENOEXEC;
}

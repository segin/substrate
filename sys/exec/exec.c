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
    /* exec no longer suppresses preemption.  With real kernel preemption
     * (preempt_count gating spinlocks) exec can be safely preempted: the
     * address-space rebuild touches only this thread's own pmap, sibling
     * threads are already terminated, and migration is still prevented by
     * the bound_cpu pin above.  Keeping NO_PREEMPT made every program load
     * a multi-millisecond non-preemptible window (mapping the binary +
     * ld.so) -- the "loading a program janks the mouse" stall.
     *
     * The old race this pin worked around ("interrupt-context reader of
     * needs_resched seeing a stale value, reliably hangs init exec") was a
     * symptom of the broken non-preemptive scheduler; bisecting with the
     * fork+exec torture (tests/lib/c/torture_exec) shows that with kernel
     * preemption present, removing NO_PREEMPT survives 1000 fork+exec
     * cycles on UP.  (SMP bringup is independently broken under the qemu32
     * CPU model -- "no local APIC" -- and panics identically with or
     * without this change, so it isn't a regression here.) */
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

int exec_cleanup_push(void (*free_fn)(void *), void *ptr) {
    if (!current_thread || !free_fn) {
        return -1;
    }
    if (current_thread->exec_cleanup_count >= EXEC_CLEANUP_MAX) {
        return -1;
    }
    uint8_t i = current_thread->exec_cleanup_count;
    current_thread->exec_cleanup[i].free_fn = free_fn;
    current_thread->exec_cleanup[i].ptr = ptr;
    current_thread->exec_cleanup_count = (uint8_t)(i + 1);
    return 0;
}

void exec_cleanup_drain(void) {
    if (!current_thread) {
        return;
    }
    uint8_t n = current_thread->exec_cleanup_count;
    /* Reset the count first so a recursive drain (or an exec that re-enters
     * via a free_fn — none today, but defensive) sees an empty list. */
    current_thread->exec_cleanup_count = 0;
    for (uint8_t i = 0; i < n; i++) {
        void (*free_fn)(void *) = current_thread->exec_cleanup[i].free_fn;
        void *ptr = current_thread->exec_cleanup[i].ptr;
        current_thread->exec_cleanup[i].free_fn = NULL;
        current_thread->exec_cleanup[i].ptr = NULL;
        if (free_fn) {
            free_fn(ptr);
        }
    }
}

/*
 * exec_dispatch
 *
 * Reads the first chunk of the file to determine the format, then calls the
 * appropriate loader.
 */
int exec_dispatch(const char *path, char *const argv[], char *const envp[]) {
    /* Historically this barrier was required: removing it reliably hung
     * init exec, blamed on an interrupt-context reader seeing a stale
     * needs_resched/bound_cpu.  That race was a symptom of the broken
     * non-preemptive scheduler (timer-in-kernel only flagged needs_resched
     * and never switched, so visibility/ordering of those fields was
     * load-bearing).  With real kernel preemption the underlying machinery
     * is fixed; bisecting with tests/lib/c/torture_exec shows the barrier
     * is no longer load-bearing (1000 fork+exec cycles pass without it).
     * Kept as a cheap, conservative compiler/memory fence at the exec
     * boundary -- it costs one mfence and removes a class of latent
     * ordering surprises. */
    __sync_synchronize();
    if (!path) return -ENOENT;

    int fd = kern_open(path, O_RDONLY, 0);
    if (fd < 0) return fd;

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) {
        kern_close(fd);
        return -ENOENT;
    }

    fs_node_t *node = (fs_node_t *)f->f_data;
    if (vfs_check_permissions_groups(node,
            current_process->euid, current_process->egid,
            current_process->supp_groups,
            current_process->n_supp_groups,
            X_OK) != 0) {
        kern_close(fd);
        return -EACCES;
    }

    char header_buf[256];
    int len = kern_read(fd, header_buf, sizeof(header_buf));
    if (len < 0) {
        kern_close(fd);
        return len;
    }

    // Iterate through handlers
    struct exec_binary_handler *h = exec_handlers;
    while (h) {
        if (h->check && h->check(path, header_buf, len) == 0) {
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

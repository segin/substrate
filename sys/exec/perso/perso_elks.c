#include <exec/perso/personality.h>
#include <exec/perso/elks_syscall_table.h>
#include <exec/formats/elks_aout.h>
#include <sys/errno.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/ldt.h>
#include <arch/i386/idt.h>
#include <kern/console.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>
#include <stdio.h>
#include <string.h>

static int elks_ds_pointer(uint32_t offset, uintptr_t *linear_out) {
    if (!current_process || !current_process->ldt) {
        return -EFAULT;
    }
    return ldt_translate_selector_offset(current_process->ldt,
                                         (unsigned int)current_process->ldt_entry_count,
                                         (uint16_t)((ELKS_LDT_DS_INDEX << 3) | 4U | 3U),
                                         (uint16_t)offset,
                                         linear_out);
}

static int elks_to_native_signal(int sig) {
    switch (sig) {
        case 1:  return SIGHUP;
        case 2:  return SIGINT;
        case 3:  return SIGQUIT;
        case 4:  return SIGWINCH;
        case 5:  return SIGSTOP;
        case 6:  return SIGABRT;
        case 7:  return SIGTSTP;
        case 8:  return SIGCONT;
        case 9:  return SIGKILL;
        case 10: return SIGUSR1;
        case 11: return SIGSEGV;
        case 12: return SIGCHLD;
        case 13: return SIGPIPE;
        case 14: return SIGALRM;
        case 15: return SIGTERM;
        case 16: return 23; /* kernel urgent-I/O slot (native SIGURG semantics) */
        default: return -EINVAL;
    }
}

struct elks_signal_frame {
    uint16_t ret_ip;
    uint16_t ret_cs;
    uint16_t sig;
};

static int elks_decode_softint(registers_t *regs, uint8_t *vector, uintptr_t *addr) {
    uintptr_t linear_ip;
    uint8_t *ip;

    if (!regs || !current_process || current_process->perso_id != PERS_ELKS ||
        !current_process->ldt) {
        return 0;
    }
    if (ldt_translate_selector_offset(current_process->ldt,
                                      (unsigned int)current_process->ldt_entry_count,
                                      (uint16_t)regs->cs,
                                      (uint16_t)regs->eip,
                                      &linear_ip) != 0) {
        return 0;
    }
    if (linear_ip >= 0xC0000000U) {
        return 0;
    }

    ip = (uint8_t *)(uintptr_t)linear_ip;
    if (ip[0] != 0xCD) {
        return 0;
    }

    if (vector) {
        *vector = ip[1];
    }
    if (addr) {
        *addr = linear_ip;
    }
    return 1;
}

static int elks_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint8_t softint = 0;
    uintptr_t softint_addr = 0;
    char msg[96];

    if (!regs || regs->int_no != 13 || !current_process ||
        current_process->perso_id != PERS_ELKS) {
        return 0;
    }
    if (!elks_decode_softint(regs, &softint, &softint_addr) || softint != 0x20) {
        return 0;
    }

    sprintf(msg, "ELKS: trapped Minix-86 syscall attempt via INT 0x20 at 0x%08X\n",
            (unsigned int)softint_addr);
    kprint(msg);
    if (current_thread && current_thread->proc == current_process) {
        current_thread->trap_addr = softint_addr;
    }
    trapsignal(current_process, SIGSYS, SI_KERNEL);
    return 1;
}

static int elks_linear_to_far_code(uintptr_t linear, uint16_t *selector_out, uint16_t *offset_out) {
    const gdt_entry_t *ldt;
    unsigned int i;

    if (!current_process || !current_process->ldt || !selector_out || !offset_out) {
        return -EINVAL;
    }

    ldt = (const gdt_entry_t *)current_process->ldt;
    for (i = 0; i < (unsigned int)current_process->ldt_entry_count; i++) {
        const gdt_entry_t *entry = &ldt[i];
        uint32_t base;
        uint32_t limit;

        if ((entry->access & 0x80U) == 0 || (entry->access & 0x10U) == 0 ||
            (entry->access & 0x08U) == 0) {
            continue;
        }

        base = ldt_entry_base(entry);
        limit = ldt_entry_limit(entry);
        if (linear < (uintptr_t)base || linear > (uintptr_t)(base + limit)) {
            continue;
        }

        *selector_out = (uint16_t)((i << 3) | 4U | 3U);
        *offset_out = (uint16_t)(linear - (uintptr_t)base);
        return 0;
    }

    return -EINVAL;
}

static int elks_sys_unimplemented(uint32_t unused0, uint32_t unused1, uint32_t unused2,
                                  uint32_t unused3, uint32_t unused4, uint32_t unused5,
                                  uint32_t unused6, uint32_t unused7) {
    char buf[96];
    unsigned int nr = current_thread ? current_thread->syscall_num : 0U;

    (void)unused0; (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    sprintf(buf, "ELKS: unsupported syscall %u\n", nr);
    kprint(buf);
    return -ENOSYS;
}

static int elks_ds_base_limit(uintptr_t *base_out, uint32_t *limit_out) {
    gdt_entry_t *ldt;

    if (!current_process || !current_process->ldt ||
        current_process->ldt_entry_count <= (int)ELKS_LDT_DS_INDEX) {
        return -EFAULT;
    }

    ldt = (gdt_entry_t *)current_process->ldt;
    if (base_out) {
        *base_out = (uintptr_t)ldt_entry_base(&ldt[ELKS_LDT_DS_INDEX]);
    }
    if (limit_out) {
        *limit_out = ldt_entry_limit(&ldt[ELKS_LDT_DS_INDEX]);
    }
    return 0;
}

static int elks_ds_span(uint32_t offset, size_t size, uintptr_t *linear_out) {
    uintptr_t base = 0;
    uint32_t limit = 0;
    uint32_t max_offset;
    int ret;

    ret = elks_ds_base_limit(&base, &limit);
    if (ret != 0) {
        return ret;
    }

    max_offset = limit + 1U;
    if (offset > max_offset) {
        return -EFAULT;
    }
    if (size > 0) {
        if (offset == max_offset) {
            return -EFAULT;
        }
        if (size - 1U > (size_t)(limit - offset)) {
            return -EFAULT;
        }
    }

    if (linear_out) {
        *linear_out = base + (uintptr_t)(uint16_t)offset;
    }
    return 0;
}

static void elks_free_vector(char **vec) {
    size_t i;
    size_t count;

    if (!vec) {
        return;
    }

    count = 0;
    while (vec[count]) {
        count++;
    }
    for (i = 0; i < count; i++) {
        size_t len = strlen(vec[i]) + 1U;
        kfree(vec[i], len);
    }
    kfree(vec, (count + 1U) * sizeof(char *));
}

static int elks_copy_exec_image_string(const uint8_t *base, uint32_t bytes,
                                       uint16_t rel, char **out) {
    size_t len = 0;
    char *copy;

    if (!base || !out || rel >= bytes) {
        return -EFAULT;
    }

    while ((uint32_t)(rel + len) < bytes && base[rel + len] != '\0') {
        len++;
    }
    if ((uint32_t)(rel + len) >= bytes) {
        return -EFAULT;
    }

    copy = kmalloc(len + 1U);
    if (!copy) {
        return -ENOMEM;
    }
    memcpy(copy, base + rel, len + 1U);
    *out = copy;
    return 0;
}

static int elks_copy_ds_string(uint32_t offset, char **out) {
    uintptr_t linear = 0;
    uintptr_t base = 0;
    uint32_t limit = 0;
    size_t maxlen;
    size_t len = 0;
    const char *src;
    char *copy;
    int ret;

    if (!out) {
        return -EINVAL;
    }

    ret = elks_ds_span(offset, 1, &linear);
    if (ret != 0) {
        return ret;
    }
    ret = elks_ds_base_limit(&base, &limit);
    if (ret != 0) {
        return ret;
    }

    maxlen = (size_t)(limit - offset) + 1U;
    src = (const char *)(uintptr_t)linear;
    while (len < maxlen && src[len] != '\0') {
        len++;
    }
    if (len == maxlen) {
        return -EFAULT;
    }

    copy = kmalloc(len + 1U);
    if (!copy) {
        return -ENOMEM;
    }
    memcpy(copy, src, len + 1U);
    *out = copy;
    return 0;
}

static int elks_unpack_exec_stack(uint32_t stack_off, uint32_t stack_bytes,
                                  char ***argv_out, char ***envp_out) {
    uintptr_t linear = 0;
    const uint8_t *base;
    uint16_t argc = 0;
    size_t cursor = 0;
    size_t i;
    size_t envc = 0;
    char **argv = NULL;
    char **envp = NULL;
    int ret;

    if (!argv_out || !envp_out) {
        return -EINVAL;
    }
    *argv_out = NULL;
    *envp_out = NULL;

    if (stack_off == 0 || stack_bytes == 0) {
        argv = kmalloc(sizeof(char *));
        envp = kmalloc(sizeof(char *));
        if (!argv || !envp) {
            if (argv) {
                kfree(argv, sizeof(char *));
            }
            if (envp) {
                kfree(envp, sizeof(char *));
            }
            return -ENOMEM;
        }
        argv[0] = NULL;
        envp[0] = NULL;
        *argv_out = argv;
        *envp_out = envp;
        return 0;
    }

    ret = elks_ds_span(stack_off, stack_bytes, &linear);
    if (ret != 0) {
        return ret;
    }

    base = (const uint8_t *)(uintptr_t)linear;
    if (stack_bytes < sizeof(uint16_t)) {
        return -EFAULT;
    }

    memcpy(&argc, base, sizeof(argc));
    cursor = sizeof(uint16_t);
    if (cursor + ((size_t)argc + 1U) * sizeof(uint16_t) > stack_bytes) {
        return -EFAULT;
    }

    argv = kmalloc(((size_t)argc + 1U) * sizeof(char *));
    if (!argv) {
        return -ENOMEM;
    }
    memset(argv, 0, ((size_t)argc + 1U) * sizeof(char *));

    for (i = 0; i < argc; i++, cursor += sizeof(uint16_t)) {
        uint16_t rel = 0;

        memcpy(&rel, base + cursor, sizeof(rel));
        ret = elks_copy_exec_image_string(base, stack_bytes, rel, &argv[i]);
        if (ret != 0) {
            elks_free_vector(argv);
            return ret;
        }
    }

    {
        uint16_t terminator = 0;

        memcpy(&terminator, base + cursor, sizeof(terminator));
        cursor += sizeof(uint16_t);
        if (terminator != 0) {
            elks_free_vector(argv);
            return -EFAULT;
        }
    }

    while (cursor + sizeof(uint16_t) <= stack_bytes) {
        uint16_t rel = 0;

        memcpy(&rel, base + cursor, sizeof(rel));
        cursor += sizeof(uint16_t);
        if (rel == 0) {
            break;
        }
        envc++;
    }
    if (cursor > stack_bytes) {
        elks_free_vector(argv);
        return -EFAULT;
    }

    envp = kmalloc((envc + 1U) * sizeof(char *));
    if (!envp) {
        elks_free_vector(argv);
        return -ENOMEM;
    }
    memset(envp, 0, (envc + 1U) * sizeof(char *));

    cursor = sizeof(uint16_t) + ((size_t)argc + 1U) * sizeof(uint16_t);
    for (i = 0; i < envc; i++, cursor += sizeof(uint16_t)) {
        uint16_t rel = 0;

        memcpy(&rel, base + cursor, sizeof(rel));
        ret = elks_copy_exec_image_string(base, stack_bytes, rel, &envp[i]);
        if (ret != 0) {
            elks_free_vector(argv);
            elks_free_vector(envp);
            return ret;
        }
    }

    *argv_out = argv;
    *envp_out = envp;
    return 0;
}

static int elks_sys_exit(uint32_t status, uint32_t unused1, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;
    return sys_exit((int)status);
}

static int elks_sys_read(uint32_t fd, uint32_t buf_off, uint32_t count,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(buf_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_read((int)fd, (char *)(uintptr_t)linear, (int)count);
}

static int elks_sys_write(uint32_t fd, uint32_t buf_off, uint32_t count,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(buf_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_write((int)fd, (const char *)(uintptr_t)linear, (int)count);
}

static int elks_sys_open(uint32_t path_off, uint32_t flags, uint32_t mode,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    char *path = NULL;
    int ret;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }
    ret = kern_open(path, (int)flags, (int)mode);
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_creat(uint32_t path_off, uint32_t mode, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    char *path = NULL;
    int ret;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }
    ret = kern_open(path, 0x40 | 0x01 | 0x08, (int)mode);
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;
    return sys_close((int)fd);
}

static int elks_sys_unlink(uint32_t path_off, uint32_t unused1, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    char *path = NULL;
    int ret;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }
    ret = kern_unlink(path);
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_waitpid(uint32_t pid, uint32_t status_off, uint32_t options,
                            uint32_t unused3, uint32_t unused4, uint32_t unused5,
                            uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    int *status_ptr = NULL;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (status_off != 0) {
        if (elks_ds_pointer(status_off, &linear) != 0) {
            return -EFAULT;
        }
        status_ptr = (int *)(uintptr_t)linear;
    }
    return sys_waitpid((int)pid, status_ptr, (int)options);
}

static int elks_sys_brk(uint32_t brk_off, uint32_t unused1, uint32_t unused2,
                        uint32_t unused3, uint32_t unused4, uint32_t unused5,
                        uint32_t unused6, uint32_t unused7) {
    uintptr_t base = 0;
    uint32_t limit = 0;
    uintptr_t linear = 0;
    uintptr_t current = 0;
    int ret;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    ret = elks_ds_base_limit(&base, &limit);
    if (ret != 0) {
        return ret;
    }
    if (brk_off > (limit + 1U)) {
        return -ENOMEM;
    }

    linear = base + (uintptr_t)(uint16_t)brk_off;
    current = (uintptr_t)sys_brk((uint32_t)linear);
    if (current < base) {
        return -ENOMEM;
    }
    return (int)(current - base);
}

static int elks_sys_fork(uint32_t unused0, uint32_t unused1, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    (void)unused0; (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6; (void)unused7;
    return sys_fork();
}

static int elks_sys_execve(uint32_t path_off, uint32_t stack_off, uint32_t stack_bytes,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    char *path = NULL;
    char **argv = NULL;
    char **envp = NULL;
    int ret;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }

    ret = elks_unpack_exec_stack(stack_off, stack_bytes, &argv, &envp);
    if (ret != 0) {
        kfree(path, strlen(path) + 1U);
        return ret;
    }

    ret = kern_execve(path, argv, envp);
    elks_free_vector(argv);
    elks_free_vector(envp);
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_kill(uint32_t pid, uint32_t sig, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    int native_sig;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    native_sig = elks_to_native_signal((int)sig);
    if (native_sig < 0) {
        return native_sig;
    }
    return sys_kill((int)pid, native_sig);
}

static int elks_sys_signal(uint32_t sig, uint32_t handler_off, uint32_t handler_sel,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    struct sigaction act;
    struct sigaction oldact;
    uintptr_t linear = 0;
    int native_sig;
    int ret;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    native_sig = elks_to_native_signal((int)sig);
    if (native_sig < 0) {
        return native_sig;
    }

    memset(&act, 0, sizeof(act));
    memset(&oldact, 0, sizeof(oldact));
    if (handler_off == 0 && handler_sel == 0) {
        act.sa_handler = SIG_DFL;
    } else if (handler_off == 1 && handler_sel == 0) {
        act.sa_handler = SIG_IGN;
    } else {
        ret = ldt_translate_selector_offset(current_process->ldt,
                                            (unsigned int)current_process->ldt_entry_count,
                                            (uint16_t)handler_sel,
                                            (uint16_t)handler_off,
                                            &linear);
        if (ret != 0) {
            return ret;
        }
        act.sa_handler = (sig_t)(uintptr_t)linear;
    }

    ret = kern_sigaction(native_sig, &act, &oldact);
    if (ret != 0) {
        return ret;
    }
    if (oldact.sa_handler == SIG_DFL) {
        return 0;
    }
    if (oldact.sa_handler == SIG_IGN) {
        return 1;
    }
    return 2;
}

static void elks_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct elks_signal_frame frame;
    uintptr_t stack_linear = 0;
    uint16_t sp;
    uint16_t handler_sel = 0;
    uint16_t handler_off = 0;
    int ret;

    (void)mask;
    (void)flags;

    if (!regs || !current_process || !current_process->ldt) {
        return;
    }

    ret = elks_linear_to_far_code((uintptr_t)handler, &handler_sel, &handler_off);
    if (ret != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    sp = (uint16_t)regs->useresp;
    if (sp < sizeof(frame)) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    sp = (uint16_t)(sp - sizeof(frame));

    ret = ldt_translate_selector_offset(current_process->ldt,
                                        (unsigned int)current_process->ldt_entry_count,
                                        (uint16_t)regs->ss,
                                        sp,
                                        &stack_linear);
    if (ret != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    frame.ret_ip = (uint16_t)regs->eip;
    frame.ret_cs = (uint16_t)regs->cs;
    frame.sig = (uint16_t)sig;
    memcpy((void *)(uintptr_t)stack_linear, &frame, sizeof(frame));

    regs->useresp = sp;
    regs->eip = handler_off;
    regs->cs = handler_sel;
    regs->eflags &= ~(1U << 10);
}

/* ELKS Syscall Table */
static void *elks_syscall_table[ELKS_SYS_MAX] = {
    [ELKS_SYS_exit]    = (void *)&elks_sys_exit,
    [ELKS_SYS_fork]    = (void *)&elks_sys_fork,
    [ELKS_SYS_read]    = (void *)&elks_sys_read,
    [ELKS_SYS_write]   = (void *)&elks_sys_write,
    [ELKS_SYS_open]    = (void *)&elks_sys_open,
    [ELKS_SYS_close]   = (void *)&elks_sys_close,
    [ELKS_SYS_waitpid] = (void *)&elks_sys_waitpid,
    [ELKS_SYS_creat]   = (void *)&elks_sys_creat,
    [ELKS_SYS_link]    = (void *)&sys_link,
    [ELKS_SYS_unlink]  = (void *)&elks_sys_unlink,
    [ELKS_SYS_execve]  = (void *)&elks_sys_execve,
    [ELKS_SYS_chdir]   = (void *)&sys_chdir,
    [ELKS_SYS_time]    = (void *)&sys_time,
    [ELKS_SYS_mknod]   = (void *)&sys_mknod,
    [ELKS_SYS_chmod]   = (void *)&sys_chmod,
    [ELKS_SYS_chown]   = (void *)&sys_lchown,
    [ELKS_SYS_lseek]   = (void *)&sys_lseek,
    [ELKS_SYS_getpid]  = (void *)&sys_getpid,
    [ELKS_SYS_mount]   = (void *)&sys_mount,
    [ELKS_SYS_umount]  = (void *)&sys_umount,
    [ELKS_SYS_setuid]  = (void *)&sys_setuid,
    [ELKS_SYS_getuid]  = (void *)&sys_getuid,
    [ELKS_SYS_stime]   = (void *)&sys_stime,
    [ELKS_SYS_alarm]   = (void *)&sys_alarm,
    [ELKS_SYS_fstat]   = (void *)&sys_fstat,
    [ELKS_SYS_pause]   = (void *)&sys_pause,
    [ELKS_SYS_access]  = (void *)&sys_access,
    [ELKS_SYS_sync]    = (void *)&sys_sync,
    [ELKS_SYS_kill]    = (void *)&elks_sys_kill,
    [ELKS_SYS_mkdir]   = (void *)&sys_mkdir,
    [ELKS_SYS_rmdir]   = (void *)&sys_rmdir,
    [ELKS_SYS_dup]     = (void *)&sys_dup,
    [ELKS_SYS_pipe]    = (void *)&sys_pipe,
    [ELKS_SYS_times]   = (void *)&sys_times,
    [ELKS_SYS_brk]     = (void *)&elks_sys_brk,
    [ELKS_SYS_setgid]  = (void *)&sys_setgid,
    [ELKS_SYS_getgid]  = (void *)&sys_getgid,
    [ELKS_SYS_signal]  = (void *)&elks_sys_signal,
    [ELKS_SYS_ioctl]   = (void *)&sys_ioctl,
    [ELKS_SYS_fcntl]   = (void *)&sys_fcntl,
    [ELKS_SYS_umask]   = (void *)&sys_umask,
    [ELKS_SYS_stat]    = (void *)&sys_stat,
    [ELKS_SYS_dup2]    = (void *)&sys_dup2,
    [ELKS_SYS_getppid] = (void *)&sys_getppid,
    [ELKS_SYS_getpgrp] = (void *)&sys_getpgrp,
};

void elks_personality_init(void) {
    static int initialized = 0;
    unsigned int i;

    if (initialized) {
        return;
    }
    for (i = 0; i < ELKS_SYS_MAX; i++) {
        if (!elks_syscall_table[i]) {
            elks_syscall_table[i] = (void *)&elks_sys_unimplemented;
        }
    }
    initialized = 1;
}

static const char *elks_syscall_names[ELKS_SYS_MAX] = {
    [ELKS_SYS_exit]    = "exit",
    [ELKS_SYS_fork]    = "fork",
    [ELKS_SYS_read]    = "read",
    [ELKS_SYS_write]   = "write",
    [ELKS_SYS_open]    = "open",
    [ELKS_SYS_close]   = "close",
    [ELKS_SYS_waitpid] = "waitpid",
    [ELKS_SYS_execve]  = "execve",
    [ELKS_SYS_alarm]   = "alarm",
    [ELKS_SYS_kill]    = "kill",
};

struct personality personality_elks = {
    .name = "ELKS",
    .id = PERS_ELKS,
    .syscall_table = elks_syscall_table,
    .syscall_names = elks_syscall_names,
    .syscall_count = ELKS_SYS_MAX,
    .sendsig = elks_sendsig,
    .sigreturn = NULL,
    .rt_sigreturn = NULL,
    .handle_trap = elks_handle_trap
};

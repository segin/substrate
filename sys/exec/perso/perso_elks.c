#include <exec/perso/personality.h>
#include <exec/perso/elks_syscall_table.h>
#include <exec/formats/elks_aout.h>
#include <sys/errno.h>
#include <sys/syscall_impl.h>
#include <sys/ldt.h>
#include <kern/console.h>
#include <sys/proc.h>
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
    uintptr_t linear = 0;
    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_open((const char *)(uintptr_t)linear, (int)flags, (int)mode);
}

static int elks_sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;
    return sys_close((int)fd);
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

/* ELKS Syscall Table */
static void *elks_syscall_table[ELKS_SYS_MAX] = {
    [ELKS_SYS_exit]    = (void *)&elks_sys_exit,
    [ELKS_SYS_fork]    = (void *)&sys_fork,
    [ELKS_SYS_read]    = (void *)&elks_sys_read,
    [ELKS_SYS_write]   = (void *)&elks_sys_write,
    [ELKS_SYS_open]    = (void *)&elks_sys_open,
    [ELKS_SYS_close]   = (void *)&elks_sys_close,
    [ELKS_SYS_waitpid] = (void *)&elks_sys_waitpid,
    [ELKS_SYS_creat]   = (void *)&sys_creat,
    [ELKS_SYS_link]    = (void *)&sys_link,
    [ELKS_SYS_unlink]  = (void *)&sys_unlink,
    [ELKS_SYS_execve]  = (void *)&sys_execve,
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
    [ELKS_SYS_kill]    = (void *)&sys_kill,
    [ELKS_SYS_mkdir]   = (void *)&sys_mkdir,
    [ELKS_SYS_rmdir]   = (void *)&sys_rmdir,
    [ELKS_SYS_dup]     = (void *)&sys_dup,
    [ELKS_SYS_pipe]    = (void *)&sys_pipe,
    [ELKS_SYS_times]   = (void *)&sys_times,
    [ELKS_SYS_brk]     = (void *)&elks_sys_brk,
    [ELKS_SYS_setgid]  = (void *)&sys_setgid,
    [ELKS_SYS_getgid]  = (void *)&sys_getgid,
    [ELKS_SYS_signal]  = (void *)&sys_signal,
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
    .sendsig = NULL, // To be implemented
    .sigreturn = NULL,
    .rt_sigreturn = NULL
};

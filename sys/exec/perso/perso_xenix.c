/*
 * perso_xenix.c - SCO Xenix/386 personality.
 *
 * Xenix 386 is a System V.3 derivative.  Its executables are x.out segmented
 * images (see exec/formats/xout.c) and make system calls through the SysV/386
 * call gate:
 *
 *     mov  $nr, %eax
 *     lcall $0x0007, $0          ; 9A 00 00 00 00 07 00
 *
 * Substrate installs no ring-0 call gate at LDT selector 0x0007, so the lcall
 * faults (#NP/#GP).  We trap that fault in handle_trap, decode the lcall, and
 * emulate the syscall: the number is in %eax and the arguments are the cdecl
 * words the C stub's caller pushed, i.e. on the user stack just past the stub
 * return address (SS:ESP+4) -- the same stack ABI the BSD personalities use.
 *
 * The SysV/386 error convention is: on return, carry-flag clear means %eax is
 * the result; carry-flag set means %eax is the (positive) errno.
 *
 * Syscall numbering matches System V.3 (shared with perso_svr3.c).
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/idt.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr3/svr3_syscalls.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/syscall_impl.h>

#define XENIX_EFLAGS_CF   0x00000001U   /* carry flag */
#define XENIX_LCALL_LEN   7U            /* 9A off32 sel16 */
#define XENIX_GATE_SEL    0x0007U       /* SysV/386 syscall gate selector */

static void *xenix_syscalls[MAX_SYSCALLS] = {
    [SVR3_SYS_exit]     = &sys_exit,
    [SVR3_SYS_fork]     = &sys_fork,
    [SVR3_SYS_read]     = &sys_read,
    [SVR3_SYS_write]    = &sys_write,
    [SVR3_SYS_open]     = &sys_open,
    [SVR3_SYS_close]    = &sys_close,
    [SVR3_SYS_wait]     = &sys_waitpid,
    [SVR3_SYS_link]     = &sys_link,
    [SVR3_SYS_unlink]   = &sys_unlink,
    [SVR3_SYS_chdir]    = &sys_chdir,
    [SVR3_SYS_time]     = &sys_time,
    [SVR3_SYS_mknod]    = &sys_mknod,
    [SVR3_SYS_chmod]    = &sys_chmod,
    [SVR3_SYS_chown]    = &sys_lchown,
    [SVR3_SYS_stat]     = &sys_stat,
    [SVR3_SYS_lseek]    = &sys_lseek,
    [SVR3_SYS_getpid]   = &sys_getpid,
    [SVR3_SYS_mount]    = &sys_mount,
    [SVR3_SYS_umount]   = &sys_umount,
    [SVR3_SYS_setuid]   = &sys_setuid,
    [SVR3_SYS_getuid]   = &sys_getuid,
    [SVR3_SYS_access]   = &sys_access,
    [SVR3_SYS_nice]     = &sys_nice,
    [SVR3_SYS_sync]     = &sys_sync,
    [SVR3_SYS_kill]     = &sys_kill,
    [SVR3_SYS_dup]      = &sys_dup,
    [SVR3_SYS_pipe]     = &sys_pipe,
    [SVR3_SYS_setgid]   = &sys_setgid,
    [SVR3_SYS_getgid]   = &sys_getgid,
    [SVR3_SYS_acct]     = &sys_acct,
    [SVR3_SYS_ioctl]    = &sys_ioctl,
    [SVR3_SYS_execve]   = &sys_execve,
    [SVR3_SYS_chroot]   = &sys_chroot,
    [SVR3_SYS_fcntl]    = &sys_fcntl,
    [SVR3_SYS_ulimit]   = &sys_ulimit,
    [SVR3_SYS_rmdir]    = &sys_rmdir,
    [SVR3_SYS_mkdir]    = &sys_mkdir,
    [SVR3_SYS_getdents] = &sys_getdents,
    [SVR3_SYS_getcwd]   = &sys_getcwd,
};

static const char *xenix_names[MAX_SYSCALLS] = {
    [SVR3_SYS_exit]     = "exit",
    [SVR3_SYS_fork]     = "fork",
    [SVR3_SYS_read]     = "read",
    [SVR3_SYS_write]    = "write",
    [SVR3_SYS_open]     = "open",
    [SVR3_SYS_close]    = "close",
    [SVR3_SYS_wait]     = "wait",
    [SVR3_SYS_link]     = "link",
    [SVR3_SYS_unlink]   = "unlink",
    [SVR3_SYS_chdir]    = "chdir",
    [SVR3_SYS_time]     = "time",
    [SVR3_SYS_mknod]    = "mknod",
    [SVR3_SYS_chmod]    = "chmod",
    [SVR3_SYS_chown]    = "chown",
    [SVR3_SYS_stat]     = "stat",
    [SVR3_SYS_lseek]    = "lseek",
    [SVR3_SYS_getpid]   = "getpid",
    [SVR3_SYS_mount]    = "mount",
    [SVR3_SYS_umount]   = "umount",
    [SVR3_SYS_setuid]   = "setuid",
    [SVR3_SYS_getuid]   = "getuid",
    [SVR3_SYS_access]   = "access",
    [SVR3_SYS_nice]     = "nice",
    [SVR3_SYS_sync]     = "sync",
    [SVR3_SYS_kill]     = "kill",
    [SVR3_SYS_dup]      = "dup",
    [SVR3_SYS_pipe]     = "pipe",
    [SVR3_SYS_setgid]   = "setgid",
    [SVR3_SYS_getgid]   = "getgid",
    [SVR3_SYS_acct]     = "acct",
    [SVR3_SYS_ioctl]    = "ioctl",
    [SVR3_SYS_execve]   = "exece",
    [SVR3_SYS_chroot]   = "chroot",
    [SVR3_SYS_fcntl]    = "fcntl",
    [SVR3_SYS_ulimit]   = "ulimit",
    [SVR3_SYS_rmdir]    = "rmdir",
    [SVR3_SYS_mkdir]    = "mkdir",
    [SVR3_SYS_getdents] = "getdents",
    [SVR3_SYS_getcwd]   = "getcwd",
};

static int xenix_trace_enabled(void) {
    return cmdline_debug_enabled("perso:xenix:syscall");
}

/* Translate a segmented selector:offset to a linear address using the current
 * process LDT, honouring 32-bit offsets (the shared ldt.h helper truncates to
 * 16 bits, which is fine for ELKS but not for a multi-megabyte Xenix text). */
static int xenix_seg_to_linear(uint16_t selector, uint32_t offset,
                               uintptr_t *linear_out) {
    const gdt_entry_t *ldt;
    const gdt_entry_t *entry;
    unsigned int index;

    if (!current_process || !current_process->ldt || !linear_out) {
        return -EINVAL;
    }
    if ((selector & 0x4U) == 0) {
        return -EINVAL;   /* not an LDT selector */
    }
    index = (unsigned int)(selector >> 3);
    if (index >= (unsigned int)current_process->ldt_entry_count) {
        return -EINVAL;
    }
    ldt = (const gdt_entry_t *)current_process->ldt;
    entry = &ldt[index];
    if ((entry->access & 0x80U) == 0 || (entry->access & 0x10U) == 0) {
        return -EINVAL;   /* not present, or not a code/data segment */
    }
    if (offset > ldt_entry_limit(entry)) {
        return -EFAULT;
    }
    *linear_out = (uintptr_t)ldt_entry_base(entry) + (uintptr_t)offset;
    return 0;
}

/* Decode the faulting instruction; return 1 if it is `lcall $0x0007,$0`. */
static int xenix_is_syscall_lcall(registers_t *regs) {
    uintptr_t linear_ip;
    uint8_t insn[XENIX_LCALL_LEN];

    if (xenix_seg_to_linear((uint16_t)regs->cs, regs->eip, &linear_ip) != 0) {
        return 0;
    }
    if (linear_ip >= 0xC0000000U) {
        return 0;
    }
    if (copyin((const void *)linear_ip, insn, sizeof(insn)) != 0) {
        return 0;
    }
    /* 0x9A = far CALL ptr16:32; trailing selector word must be the gate. */
    if (insn[0] != 0x9AU) {
        return 0;
    }
    if (((uint16_t)insn[5] | ((uint16_t)insn[6] << 8)) != XENIX_GATE_SEL) {
        return 0;
    }
    return 1;
}

static int xenix_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t nr;
    uint32_t args[8];
    uintptr_t linear_sp;
    int64_t ret;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_XENIX ||
        !current_process->ldt) {
        return 0;
    }
    /* Only segment/protection faults can come from an lcall to an absent gate. */
    if (regs->int_no != 11 && regs->int_no != 13) {
        return 0;
    }
    if (!xenix_is_syscall_lcall(regs)) {
        return 0;
    }

    nr = regs->eax;
    if (current_thread && current_thread->proc == current_process) {
        current_thread->syscall_num = nr;
    }

    /* Arguments: the cdecl words past the stub return address at SS:ESP+4. */
    memset(args, 0, sizeof(args));
    if (xenix_seg_to_linear((uint16_t)regs->ss, regs->useresp, &linear_sp) == 0) {
        const void *ap = (const void *)(linear_sp + sizeof(uint32_t));
        if (copyin(ap, args, sizeof(args)) != 0) {
            for (int i = 0; i < 8; i++) {
                if (copyin((const char *)ap + (size_t)i * sizeof(uint32_t),
                           &args[i], sizeof(uint32_t)) != 0) {
                    break;
                }
            }
        }
    }

    /* Dispatch. */
    if (nr < MAX_SYSCALLS && xenix_syscalls[nr]) {
        typedef int64_t (*sys_func_t)(uint32_t, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t, uint32_t, uint32_t);
        sys_func_t fn = (sys_func_t)xenix_syscalls[nr];
        ret = fn(args[0], args[1], args[2], args[3],
                 args[4], args[5], args[6], args[7]);
    } else {
        ret = -ENOSYS;
    }

    if (xenix_trace_enabled()) {
        char buf[128];
        const char *name = (nr < MAX_SYSCALLS && xenix_names[nr]) ?
                           xenix_names[nr] : NULL;
        if (name) {
            snprintf(buf, sizeof(buf),
                     "XENIX: %s(0x%x, 0x%x, 0x%x) = %lld\n",
                     name, args[0], args[1], args[2], (long long)ret);
        } else {
            snprintf(buf, sizeof(buf),
                     "XENIX: sys%u(0x%x, 0x%x, 0x%x) = %lld\n",
                     nr, args[0], args[1], args[2], (long long)ret);
        }
        kprint(buf);
    }

    /* SysV/386 return convention: CF set + %eax=errno on error. */
    if (ret < 0) {
        regs->eax = (uint32_t)(-ret);
        regs->eflags |= XENIX_EFLAGS_CF;
    } else {
        regs->eax = (uint32_t)ret;
        regs->eflags &= ~XENIX_EFLAGS_CF;
    }

    /* Step past the faulting lcall. */
    regs->eip += XENIX_LCALL_LEN;
    return 1;
}

struct personality personality_xenix = {
    .name = "SCO Xenix/386",
    .id = PERS_XENIX,
    .syscall_table = xenix_syscalls,
    .syscall_names = xenix_names,
    .syscall_fmts = NULL,
    .syscall_count = MAX_SYSCALLS,
    .path_prefix = "/perso/xenix",
    .handle_trap = xenix_handle_trap,
};

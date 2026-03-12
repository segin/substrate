#include <exec/perso/personality.h>
#include <exec/perso/elks_syscall_table.h>
#include <exec/perso/elks_kmem.h>
#include <exec/formats/elks_aout.h>
#include <sys/errno.h>
#include <sys/core.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/compiler.h>
#include <sys/ldt.h>
#include <arch/i386/idt.h>
#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/dirent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/poll.h>
#include <pm/pm.h>
#include <kern/time.h>
#include <kern/cmdline.h>
#include <vm/vm_kmem.h>
#include <vm/phys_mem.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int SUB_NODISCARD SUB_NONNULL(2)
elks_ds_pointer(uint32_t offset, uintptr_t *linear_out) {
    if (!current_process || !current_process->ldt) {
        return -EFAULT;
    }
    return ldt_translate_selector_offset(current_process->ldt,
                                         (unsigned int)current_process->ldt_entry_count,
                                         (uint16_t)((ELKS_LDT_DS_INDEX << 3) | 4U | 3U),
                                         (uint16_t)offset,
                                         linear_out);
}

static int elks_debug_enabled(const char *channel) {
    if (cmdline_debug_enabled("perso:elks")) {
        return 1;
    }
    return channel && cmdline_debug_enabled(channel);
}

static int elks_first_set_bit(uint32_t mask) {
    int bit = 0;

    while (mask != 0U) {
        if ((mask & 1U) != 0U) {
            return bit;
        }
        mask >>= 1;
        bit++;
    }
    return -1;
}

static void elks_copy_cstr(char *dst, size_t dst_size, const char *src) {
    size_t len;

    if (!dst || dst_size == 0U) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strnlen(src, dst_size - 1U);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#define ELKS_UF_NOFREESPACE 1U
#define ELKS_FST_MINIX      1
#define ELKS_FST_MSDOS      2
#define ELKS_FST_ROMFS      3
#define ELKS_FST_OTHER      4

static int elks_mount_fstype(const struct mount *mp) {
    const char *type;

    if (!mp) {
        return ELKS_FST_OTHER;
    }
    type = mp->mnt_stat.f_fstypename;
    if (strcmp(type, "minix") == 0) {
        return ELKS_FST_MINIX;
    }
    if (strcmp(type, "fat") == 0 || strcmp(type, "exfat") == 0 || strcmp(type, "msdos") == 0) {
        return ELKS_FST_MSDOS;
    }
    if (strcmp(type, "romfs") == 0) {
        return ELKS_FST_ROMFS;
    }
    return ELKS_FST_OTHER;
}

static struct mount *elks_mount_by_index(unsigned int index) {
    struct mount *mp;
    unsigned int i = 0;

    TAILQ_FOREACH(mp, &mountlist, mnt_list) {
        if (i == index) {
            return mp;
        }
        i++;
    }
    return NULL;
}

static int SUB_PURE elks_to_native_signal(int sig) {
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

#define ELKS_NCCS 17
#define ELKS_TERMIOS_BYTES offsetof(struct termios, c_cc[ELKS_NCCS])
#define ELKS_MAXNAMLEN 26

struct elks_stat {
    uint16_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    int32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
} __attribute__((packed, aligned(2)));

struct elks_dirent {
    uint32_t d_ino;
    int32_t d_offset;
    uint16_t d_namlen;
    char d_name[ELKS_MAXNAMLEN + 1];
} __attribute__((packed, aligned(2)));

struct elks_timeval {
    int32_t tv_sec;
    int32_t tv_usec;
} __attribute__((packed, aligned(2)));

typedef int32_t elks_time_t;

struct elks_timezone {
    int16_t tz_minuteswest;
    int16_t tz_dsttime;
} __attribute__((packed, aligned(2)));

struct elks_utsname {
    char sysname[8];
    char nodename[16];
    char release[12];
    char version[48];
    char machine[16];
} __attribute__((packed, aligned(2)));

struct elks_statfs {
    int16_t  f_type;
    uint16_t f_flags;
    uint16_t f_dev;
    int32_t  f_bsize;
    int32_t  f_blocks;
    int32_t  f_bfree;
    int32_t  f_bavail;
    int32_t  f_files;
    int32_t  f_ffree;
    char     f_mntonname[32];
} __attribute__((packed, aligned(2)));

#define ELKS_KMEM_RDEV               (((1U << 8) | 2U))
#define ELKS_KMEM_IMAGE_CAP          32768U
#define ELKS_KMEM_JIFFIES_OFFSET     0x0000U
#define ELKS_KMEM_TASKS_OFFSET       0x0100U
#define ELKS_KMEM_TASK_SIZE          0x033CU
#define ELKS_KMEM_TASK_SLOT_SIZE     0x0372U
#define ELKS_KMEM_LIST_SIZE          0x0004U
#define ELKS_KMEM_LIST_PREV          0x0000U
#define ELKS_KMEM_LIST_NEXT          0x0002U
#define ELKS_KMEM_SEGALL_OFFSET      (ELKS_KMEM_TASKS_OFFSET + (MAX_PROCS * ELKS_KMEM_TASK_SLOT_SIZE))
#define ELKS_KMEM_HEAPALL_OFFSET     (ELKS_KMEM_SEGALL_OFFSET + ELKS_KMEM_LIST_SIZE)
#define ELKS_KMEM_DYNAMIC_OFFSET     (ELKS_KMEM_HEAPALL_OFFSET + ELKS_KMEM_LIST_SIZE)
#define ELKS_KMEM_TASK_STATE         0x0000U
#define ELKS_KMEM_TASK_STATE_LEGACY  0x0022U
#define ELKS_KMEM_TASK_PID           0x0002U
#define ELKS_KMEM_TASK_PID_LEGACY    0x000EU
#define ELKS_KMEM_TASK_PPID          0x0004U
#define ELKS_KMEM_TASK_PPID_LEGACY   0x0010U
#define ELKS_KMEM_TASK_PGRP          0x0006U
#define ELKS_KMEM_TASK_PGRP_LEGACY   0x0012U
#define ELKS_KMEM_TASK_UID           0x000AU
#define ELKS_KMEM_TASK_UID_LEGACY    0x0016U
#define ELKS_KMEM_TASK_TTY           0x001CU
#define ELKS_KMEM_TASK_TTY_LEGACY    0x0076U
#define ELKS_KMEM_TASK_T_INODE       0x0026U
#define ELKS_KMEM_TASK_MM            0x0044U
#define ELKS_KMEM_TASK_MM_LEGACY     0x006CU
#define ELKS_KMEM_TASK_MM_ALT        0x003EU
#define ELKS_KMEM_TASK_AVERAGE       0x009CU
#define ELKS_KMEM_TASK_AVERAGE_LEGACY 0x0094U
#define ELKS_KMEM_TASK_AVERAGE_ALT   0x0096U
#define ELKS_KMEM_TASK_T_ENDDATA     0x0092U
#define ELKS_KMEM_TASK_T_ENDDATA_LEGACY 0x0004U
#define ELKS_KMEM_TASK_T_ENDDATA_ALT 0x008CU
#define ELKS_KMEM_TASK_T_ENDBRK      0x0094U
#define ELKS_KMEM_TASK_T_ENDBRK_LEGACY 0x0006U
#define ELKS_KMEM_TASK_T_ENDBRK_ALT  0x008EU
#define ELKS_KMEM_TASK_T_BEGSTACK    0x0096U
#define ELKS_KMEM_TASK_T_BEGSTACK_LEGACY 0x0008U
#define ELKS_KMEM_TASK_T_BEGSTACK_ALT 0x0090U
#define ELKS_KMEM_TASK_KSTACK_MAGIC  0x00A4U
#define ELKS_KMEM_TASK_KSTACK_MAGIC_ALT 0x009EU
#define ELKS_KMEM_TASK_T_REGS_SP     0x0338U
#define ELKS_KMEM_TASK_T_REGS_SP_LEGACY 0x036EU
#define ELKS_KMEM_TASK_T_REGS_SP_ALT 0x036EU
#define ELKS_KMEM_TASK_T_REGS_SS     0x033AU
#define ELKS_KMEM_TASK_T_REGS_SS_LEGACY 0x0370U
#define ELKS_KMEM_TASK_T_REGS_SS_ALT 0x0370U
#define ELKS_KMEM_SEG_SIZE           0x0010U
#define ELKS_KMEM_SEG_ALL            0x0000U
#define ELKS_KMEM_SEG_FREE           0x0004U
#define ELKS_KMEM_SEG_BASE           0x0008U
#define ELKS_KMEM_SEG_SIZE_OFF       0x000AU
#define ELKS_KMEM_SEG_FLAGS          0x000CU
#define ELKS_KMEM_SEG_REFCOUNT       0x000DU
#define ELKS_KMEM_SEG_PID            0x000EU
#define ELKS_KMEM_HEAP_SIZE          0x000CU
#define ELKS_KMEM_HEAP_ALL           0x0000U
#define ELKS_KMEM_HEAP_FREE          0x0004U
#define ELKS_KMEM_HEAP_SIZE_OFF      0x0008U
#define ELKS_KMEM_HEAP_TAG           0x000AU
#define ELKS_KMEM_CMDLINE_SIZE       0x0050U
#define ELKS_KMEM_TTY_SIZE           0x0080U
#define ELKS_KMEM_TTY_MINOR_LEGACY   0x0002U
#define ELKS_KMEM_TTY_MINOR          0x0018U
#define ELKS_KSTACK_MAGIC            0x5476U
#define ELKS_TASK_RUNNING            0U
#define ELKS_TASK_INTERRUPTIBLE      1U
#define ELKS_TASK_UNINTERRUPTIBLE    2U
#define ELKS_TASK_WAITING            3U
#define ELKS_TASK_STOPPED            4U
#define ELKS_TASK_ZOMBIE             5U
#define ELKS_TASK_EXITING            6U
#define ELKS_TASK_UNUSED             7U
#define ELKS_SEG_CODE                0U
#define ELKS_SEG_DATA                1U
#define ELKS_HEAP_TAG_FREE           0x00U
#define ELKS_HEAP_TAG_SEG            0x01U
#define ELKS_SEG_FLAG_USED           0x80U
#define ELKS_SEG_FLAG_CSEG           0x01U
#define ELKS_SEG_FLAG_DSEG           0x02U
#define ELKS_PAGE_KB                 4U
#define ELKS_MEMINFO_KB_MAX          0x7FFFU

struct elks_mem_usage {
    uint16_t main_free;
    uint16_t main_used;
    uint16_t xms_free;
    uint16_t xms_used;
} __attribute__((packed, aligned(2)));

struct elks_kmem_task_refs {
    uint16_t code_seg_off;
    uint16_t data_seg_off;
    uint16_t code_heap_off;
    uint16_t data_heap_off;
};

static void elks_kmem_put16(uint8_t *buf, uint32_t off, uint16_t value) {
    buf[off + 0U] = (uint8_t)(value & 0xFFU);
    buf[off + 1U] = (uint8_t)((value >> 8) & 0xFFU);
}

static void elks_kmem_put32(uint8_t *buf, uint32_t off, uint32_t value) {
    buf[off + 0U] = (uint8_t)(value & 0xFFU);
    buf[off + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    buf[off + 2U] = (uint8_t)((value >> 16) & 0xFFU);
    buf[off + 3U] = (uint8_t)((value >> 24) & 0xFFU);
}

static void elks_kmem_put_list(uint8_t *buf, uint32_t off, uint16_t prev, uint16_t next) {
    elks_kmem_put16(buf, off + ELKS_KMEM_LIST_PREV, prev);
    elks_kmem_put16(buf, off + ELKS_KMEM_LIST_NEXT, next);
}

static int elks_is_kmem_fd(int fd) {
    file_t *f;
    fs_node_t *node;

    if (!current_process || fd < 0 || fd >= MAX_FD) {
        return 0;
    }
    f = current_process->fds[fd];
    if (!f || !f->f_data) {
        return 0;
    }
    node = (fs_node_t *)f->f_data;
    if ((node->flags & 0x7U) != FS_CHARDEVICE) {
        return 0;
    }
    return (uint32_t)node->rdev == ELKS_KMEM_RDEV;
}

static process_t *elks_active_process(void) {
    if (current_thread && current_thread->proc && current_thread->proc->pid >= 0) {
        return current_thread->proc;
    }
    if (current_process && current_process->pid >= 0) {
        return current_process;
    }
    return NULL;
}

static process_t *elks_swapper_process(void) {
    int i;

    for (i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == 0) {
            return &processes[i];
        }
    }
    return NULL;
}

static int elks_proc_visible(const process_t *proc) {
    if (!proc || proc->pid < 0) {
        return 0;
    }
    if (proc == elks_active_process()) {
        return 1;
    }
    if (proc->is_kernel_task || proc->pmap || proc->p_parent ||
        proc->comm[0] != '\0' || proc->exec_path[0] != '\0') {
        return 1;
    }
    return 0;
}

static uint16_t elks_map_proc_state(const process_t *proc) {
    if (!elks_proc_visible(proc)) {
        return ELKS_TASK_UNUSED;
    }

    switch (proc->state) {
        case SSTOP:
            return ELKS_TASK_STOPPED;
        case SZOMB:
            return ELKS_TASK_ZOMBIE;
        case SDYING:
            return ELKS_TASK_EXITING;
        case SSLEEP:
            return ELKS_TASK_INTERRUPTIBLE;
        case SIDL:
            return ELKS_TASK_WAITING;
        case SRUN:
        default:
            return ELKS_TASK_RUNNING;
    }
}

static int elks_kmem_append_region(uint32_t *cursor, uint32_t size, uint32_t align,
                                   uint32_t *off_out) {
    uint32_t off;

    if (!cursor || !off_out || align == 0U) {
        return -EINVAL;
    }
    off = (*cursor + (align - 1U)) & ~(align - 1U);
    if (off > ELKS_KMEM_IMAGE_CAP || size > (ELKS_KMEM_IMAGE_CAP - off)) {
        return -ENOMEM;
    }
    *cursor = off + size;
    *off_out = off;
    return 0;
}

static void elks_kmem_emit_task(uint8_t *buf, uint32_t task_off, const process_t *proc,
                                uint32_t *cursor, struct elks_kmem_task_refs *refs) {
    char cmdline[PROC_CMDLINE_MAX];
    size_t cmdline_bytes;
    size_t cmd_argc;
    uint32_t code_seg_off = 0;
    uint32_t data_seg_off = 0;
    uint32_t code_heap_off = 0;
    uint32_t data_heap_off = 0;
    uint32_t tty_off = 0;
    uint32_t stack_off = 0;
    uint16_t t_enddata = 0x0080U;
    uint16_t t_endbrk = 0x00A0U;
    uint16_t t_sp = 0;
    uint16_t dseg_size_paras;
    uint16_t cseg_size_paras = 0x0040U;
    uint16_t avg = 0;
    uint16_t code_base_paras;
    uint16_t data_base_paras;

    if (refs) {
        memset(refs, 0, sizeof(*refs));
    }

    memset(buf + task_off, 0, ELKS_KMEM_TASK_SLOT_SIZE);
    if (!elks_proc_visible(proc)) {
        buf[task_off + ELKS_KMEM_TASK_STATE] = (uint8_t)ELKS_TASK_UNUSED;
        buf[task_off + ELKS_KMEM_TASK_STATE_LEGACY] = (uint8_t)ELKS_TASK_UNUSED;
        return;
    }

    if (elks_kmem_append_region(cursor, ELKS_KMEM_HEAP_SIZE + ELKS_KMEM_SEG_SIZE, 2U,
                                &code_heap_off) != 0 ||
        elks_kmem_append_region(cursor, ELKS_KMEM_HEAP_SIZE + ELKS_KMEM_SEG_SIZE, 2U,
                                &data_heap_off) != 0) {
        buf[task_off + ELKS_KMEM_TASK_STATE] = (uint8_t)ELKS_TASK_UNUSED;
        buf[task_off + ELKS_KMEM_TASK_STATE_LEGACY] = (uint8_t)ELKS_TASK_UNUSED;
        return;
    }
    code_seg_off = code_heap_off + ELKS_KMEM_HEAP_SIZE;
    data_seg_off = data_heap_off + ELKS_KMEM_HEAP_SIZE;

    if (proc->tty != NULL &&
        elks_kmem_append_region(cursor, ELKS_KMEM_TTY_SIZE, 2U, &tty_off) != 0) {
        tty_off = 0;
    }

    cmdline_bytes = proc_emit_cmdline(proc, cmdline, sizeof(cmdline), &cmd_argc);
    if (cmdline_bytes < ELKS_KMEM_CMDLINE_SIZE) {
        cmdline_bytes = ELKS_KMEM_CMDLINE_SIZE;
    }
    if (elks_kmem_append_region(cursor,
                                (uint32_t)(((cmd_argc + 2U) * sizeof(uint16_t)) + cmdline_bytes),
                                2U, &stack_off) != 0) {
        buf[task_off + ELKS_KMEM_TASK_STATE] = (uint8_t)ELKS_TASK_UNUSED;
        buf[task_off + ELKS_KMEM_TASK_STATE_LEGACY] = (uint8_t)ELKS_TASK_UNUSED;
        return;
    }

    dseg_size_paras = (uint16_t)(((stack_off +
                                   ((uint32_t)(cmd_argc + 2U) * sizeof(uint16_t)) +
                                   (uint32_t)cmdline_bytes + 15U) >> 4) + 1U);
    t_sp = (uint16_t)stack_off;
    avg = (uint16_t)((proc->utime + proc->stime) & 0xFFFFU);
    code_base_paras = (uint16_t)(code_seg_off >> 4);
    data_base_paras = (uint16_t)(data_seg_off >> 4);

    buf[task_off + ELKS_KMEM_TASK_STATE] = (uint8_t)elks_map_proc_state(proc);
    buf[task_off + ELKS_KMEM_TASK_STATE_LEGACY] = (uint8_t)elks_map_proc_state(proc);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PID, (uint16_t)proc->pid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PID_LEGACY, (uint16_t)proc->pid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PPID, (uint16_t)proc->ppid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PPID_LEGACY, (uint16_t)proc->ppid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PGRP,
                    (uint16_t)(proc->p_pgrp ? proc->p_pgrp->pg_id : proc->pid));
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_PGRP_LEGACY,
                    (uint16_t)(proc->p_pgrp ? proc->p_pgrp->pg_id : proc->pid));
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_UID, (uint16_t)proc->uid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_UID_LEGACY, (uint16_t)proc->uid);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_TTY, (uint16_t)tty_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_TTY_LEGACY, (uint16_t)tty_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_INODE, (uint16_t)(proc->pid & 0xFFFFU));
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM + (ELKS_SEG_CODE * 2U),
                    (uint16_t)code_seg_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM + (ELKS_SEG_DATA * 2U),
                    (uint16_t)data_seg_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM_ALT + (ELKS_SEG_CODE * 2U),
                    (uint16_t)code_seg_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM_ALT + (ELKS_SEG_DATA * 2U),
                    (uint16_t)data_seg_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM_LEGACY + (ELKS_SEG_CODE * 2U),
                    (uint16_t)code_seg_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_MM_LEGACY + (ELKS_SEG_DATA * 2U),
                    (uint16_t)data_seg_off);
    elks_kmem_put32(buf, task_off + ELKS_KMEM_TASK_AVERAGE, (uint32_t)avg);
    elks_kmem_put32(buf, task_off + ELKS_KMEM_TASK_AVERAGE_LEGACY, (uint32_t)avg);
    elks_kmem_put32(buf, task_off + ELKS_KMEM_TASK_AVERAGE_ALT, (uint32_t)avg);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDDATA, t_enddata);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDDATA_LEGACY, t_enddata);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDDATA_ALT, t_enddata);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDBRK, t_endbrk);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDBRK_LEGACY, t_endbrk);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_ENDBRK_ALT, t_endbrk);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_BEGSTACK, (uint16_t)stack_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY, (uint16_t)stack_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_BEGSTACK_ALT, (uint16_t)stack_off);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_KSTACK_MAGIC, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_KSTACK_MAGIC_ALT, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x009AU, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x00A0U, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x00A2U, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x00A6U, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x00A8U, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + 0x00AAU, ELKS_KSTACK_MAGIC);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SP, t_sp);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SP_LEGACY, t_sp);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SP_ALT, t_sp);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SS, 0U);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SS_LEGACY, 0U);
    elks_kmem_put16(buf, task_off + ELKS_KMEM_TASK_T_REGS_SS_ALT, 0U);

    elks_kmem_put_list(buf, code_heap_off + ELKS_KMEM_HEAP_ALL, 0U, 0U);
    elks_kmem_put_list(buf, code_heap_off + ELKS_KMEM_HEAP_FREE, 0U, 0U);
    elks_kmem_put16(buf, code_heap_off + ELKS_KMEM_HEAP_SIZE_OFF, ELKS_KMEM_SEG_SIZE);
    buf[code_heap_off + ELKS_KMEM_HEAP_TAG] = ELKS_HEAP_TAG_SEG;
    elks_kmem_put_list(buf, code_seg_off + ELKS_KMEM_SEG_ALL, 0U, 0U);
    elks_kmem_put_list(buf, code_seg_off + ELKS_KMEM_SEG_FREE, 0U, 0U);
    elks_kmem_put16(buf, code_seg_off + ELKS_KMEM_SEG_BASE, code_base_paras);
    elks_kmem_put16(buf, code_seg_off + ELKS_KMEM_SEG_SIZE_OFF, cseg_size_paras);
    buf[code_seg_off + ELKS_KMEM_SEG_FLAGS] = ELKS_SEG_FLAG_USED | ELKS_SEG_FLAG_CSEG;
    buf[code_seg_off + ELKS_KMEM_SEG_REFCOUNT] = 1U;
    elks_kmem_put16(buf, code_seg_off + ELKS_KMEM_SEG_PID, (uint16_t)proc->pid);

    elks_kmem_put_list(buf, data_heap_off + ELKS_KMEM_HEAP_ALL, 0U, 0U);
    elks_kmem_put_list(buf, data_heap_off + ELKS_KMEM_HEAP_FREE, 0U, 0U);
    elks_kmem_put16(buf, data_heap_off + ELKS_KMEM_HEAP_SIZE_OFF, ELKS_KMEM_SEG_SIZE);
    buf[data_heap_off + ELKS_KMEM_HEAP_TAG] = ELKS_HEAP_TAG_SEG;
    elks_kmem_put_list(buf, data_seg_off + ELKS_KMEM_SEG_ALL, 0U, 0U);
    elks_kmem_put_list(buf, data_seg_off + ELKS_KMEM_SEG_FREE, 0U, 0U);
    elks_kmem_put16(buf, data_seg_off + ELKS_KMEM_SEG_BASE, data_base_paras);
    elks_kmem_put16(buf, data_seg_off + ELKS_KMEM_SEG_SIZE_OFF, dseg_size_paras);
    buf[data_seg_off + ELKS_KMEM_SEG_FLAGS] = ELKS_SEG_FLAG_USED | ELKS_SEG_FLAG_DSEG;
    buf[data_seg_off + ELKS_KMEM_SEG_REFCOUNT] = 1U;
    elks_kmem_put16(buf, data_seg_off + ELKS_KMEM_SEG_PID, (uint16_t)proc->pid);

    if (tty_off != 0U && proc->tty != NULL) {
        elks_kmem_put16(buf, tty_off + ELKS_KMEM_TTY_MINOR_LEGACY, 0U);
        elks_kmem_put16(buf, tty_off + ELKS_KMEM_TTY_MINOR, 0U);
    }

    {
        uint32_t ptr_off = stack_off + 2U;
        uint32_t str_off = stack_off + ((uint32_t)(cmd_argc + 2U) * sizeof(uint16_t));
        size_t i;
        size_t str_cursor = 0;

        elks_kmem_put16(buf, stack_off, (uint16_t)cmd_argc);
        for (i = 0; i < cmd_argc; i++) {
            size_t arg_len = strnlen(cmdline + str_cursor, cmdline_bytes - str_cursor) + 1U;

            elks_kmem_put16(buf, ptr_off + (uint32_t)(i * sizeof(uint16_t)),
                            (uint16_t)(str_off + str_cursor));
            memcpy(buf + str_off + str_cursor, cmdline + str_cursor, arg_len);
            str_cursor += arg_len;
        }
        elks_kmem_put16(buf, ptr_off + (uint32_t)(cmd_argc * sizeof(uint16_t)), 0U);
    }

    if (refs) {
        refs->code_seg_off = (uint16_t)code_seg_off;
        refs->data_seg_off = (uint16_t)data_seg_off;
        refs->code_heap_off = (uint16_t)code_heap_off;
        refs->data_heap_off = (uint16_t)data_heap_off;
    }
}

static void elks_kmem_link_ring(uint8_t *buf, uint16_t root_off, const uint16_t *nodes, size_t count) {
    size_t i;

    if (count == 0U) {
        elks_kmem_put_list(buf, root_off, root_off, root_off);
        return;
    }

    elks_kmem_put_list(buf, root_off, nodes[count - 1U], nodes[0]);
    for (i = 0; i < count; i++) {
        uint16_t prev = (i == 0U) ? root_off : nodes[i - 1U];
        uint16_t next = (i + 1U == count) ? root_off : nodes[i + 1U];

        elks_kmem_put_list(buf, nodes[i], prev, next);
    }
}

static uint16_t elks_kmem_meminfo_kb(size_t pages) {
    size_t kb = pages * ELKS_PAGE_KB;

    if (kb > ELKS_MEMINFO_KB_MAX) {
        kb = ELKS_MEMINFO_KB_MAX;
    }
    return (uint16_t)kb;
}

static int elks_kmem_build_snapshot(uint8_t **buf_out, size_t *size_out) {
    uint8_t *buf;
    uint32_t cursor = ELKS_KMEM_DYNAMIC_OFFSET;
    const process_t *exported[MAX_PROCS];
    struct elks_kmem_task_refs refs[MAX_PROCS];
    uint16_t seg_nodes[MAX_PROCS * 2U];
    uint16_t heap_nodes[MAX_PROCS * 2U];
    process_t *active = elks_active_process();
    process_t *swapper = elks_swapper_process();
    int exported_count = 0;
    size_t seg_count = 0;
    size_t heap_count = 0;
    int i;

    if (!buf_out || !size_out) {
        return -EINVAL;
    }

    buf = kmalloc(ELKS_KMEM_IMAGE_CAP);
    if (!buf) {
        return -ENOMEM;
    }
    memset(buf, 0, ELKS_KMEM_IMAGE_CAP);
    elks_kmem_put32(buf, ELKS_KMEM_JIFFIES_OFFSET, (uint32_t)get_ticks());
    elks_kmem_put_list(buf, ELKS_KMEM_SEGALL_OFFSET, ELKS_KMEM_SEGALL_OFFSET, ELKS_KMEM_SEGALL_OFFSET);
    elks_kmem_put_list(buf, ELKS_KMEM_HEAPALL_OFFSET, ELKS_KMEM_HEAPALL_OFFSET, ELKS_KMEM_HEAPALL_OFFSET);

    memset(exported, 0, sizeof(exported));
    memset(refs, 0, sizeof(refs));
    /*
     * Older installed ELKS userland expects the first task slot to be the
     * reserved idle/swapper slot and starts scanning at slot 1.
     */
    if (elks_proc_visible(swapper)) {
        exported[exported_count++] = swapper;
    }
    if (active && active != swapper && elks_proc_visible(active)) {
        exported[exported_count++] = active;
    }
    for (i = 0; i < MAX_PROCS && exported_count < MAX_PROCS; i++) {
        const process_t *proc = &processes[i];
        int seen = 0;
        int j;

        if (!elks_proc_visible(proc)) {
            continue;
        }
        for (j = 0; j < exported_count; j++) {
            if (exported[j] == proc) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            exported[exported_count++] = proc;
        }
    }

    if (elks_debug_enabled("perso:elks:kmem")) {
        char msg[128];

        sprintf(msg,
                "ELKS kmem: current pid=%d comm=%s thread=%d active=%d count=%d\n",
                current_process ? current_process->pid : -1,
                current_process ? current_process->comm : "(null)",
                current_thread ? current_thread->tid : -1,
                active ? active->pid : -1,
                exported_count);
        kprint(msg);
        for (i = 0; i < exported_count; i++) {
            sprintf(msg,
                    "ELKS kmem: slot %d pid=%d state=%d comm=%s kernel=%d\n",
                    i,
                    exported[i] ? exported[i]->pid : -1,
                    exported[i] ? exported[i]->state : -1,
                    exported[i] ? exported[i]->comm : "(null)",
                    exported[i] ? exported[i]->is_kernel_task : 0);
            kprint(msg);
        }
    }

    for (i = 0; i < MAX_PROCS; i++) {
        elks_kmem_emit_task(buf, ELKS_KMEM_TASKS_OFFSET + (i * ELKS_KMEM_TASK_SLOT_SIZE),
                            i < exported_count ? exported[i] : NULL, &cursor, &refs[i]);
        if (refs[i].code_seg_off != 0U) {
            seg_nodes[seg_count++] = refs[i].code_seg_off;
            seg_nodes[seg_count++] = refs[i].data_seg_off;
            heap_nodes[heap_count++] = refs[i].code_heap_off;
            heap_nodes[heap_count++] = refs[i].data_heap_off;
        }
    }

    elks_kmem_link_ring(buf, (uint16_t)ELKS_KMEM_SEGALL_OFFSET, seg_nodes, seg_count);
    elks_kmem_link_ring(buf, (uint16_t)ELKS_KMEM_HEAPALL_OFFSET, heap_nodes, heap_count);

    *buf_out = buf;
    *size_out = (size_t)cursor;
    return 0;
}

static void elks_translate_stat(struct elks_stat *dst, const struct stat *src) {
    if (!dst || !src) {
        return;
    }

    memset(dst, 0, sizeof(*dst));
    dst->st_dev = (uint16_t)src->st_dev;
    dst->st_ino = (uint32_t)src->st_ino;
    dst->st_mode = (uint16_t)src->st_mode;
    dst->st_nlink = (uint16_t)src->st_nlink;
    dst->st_uid = (uint16_t)src->st_uid;
    dst->st_gid = (uint16_t)src->st_gid;
    dst->st_rdev = (uint16_t)src->st_rdev;
    dst->st_size = (int32_t)src->st_size;
    dst->st_atime = (uint32_t)src->st_atime;
    dst->st_mtime = (uint32_t)src->st_mtime;
    dst->st_ctime = (uint32_t)src->st_ctime;
}

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

    if (elks_debug_enabled("perso:elks:trap")) {
        sprintf(msg, "ELKS: trapped Minix-86 syscall attempt via INT 0x20 at 0x%08X\n",
                (unsigned int)softint_addr);
        kprint(msg);
    }
    if (current_thread && current_thread->proc == current_process) {
        current_thread->trap_addr = softint_addr;
    }
    trapsignal(current_process, SIGSYS, SI_KERNEL);
    return 1;
}

static int SUB_NODISCARD SUB_NONNULL(2, 3)
elks_linear_to_far_code(uintptr_t linear, uint16_t *selector_out, uint16_t *offset_out) {
    const gdt_entry_t *ldt;
    unsigned int i;

    if (!current_process || !current_process->ldt) {
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

    if (elks_debug_enabled("perso:elks:syscall")) {
        sprintf(buf, "ELKS: unsupported syscall %u\n", nr);
        kprint(buf);
    }
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

static int SUB_NODISCARD SUB_NONNULL(1, 4)
elks_copy_exec_image_string(const uint8_t *base, uint32_t bytes,
                            uint16_t rel, char **out) {
    size_t len = 0;
    char *copy;

    if (rel >= bytes) {
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

static int SUB_NODISCARD SUB_NONNULL(2)
elks_copy_ds_string(uint32_t offset, char **out) {
    uintptr_t linear = 0;
    uintptr_t base = 0;
    uint32_t limit = 0;
    size_t maxlen;
    size_t len = 0;
    const char *src;
    char *copy;
    int ret;

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

static int SUB_NODISCARD SUB_NONNULL(3, 4)
elks_unpack_exec_stack(uint32_t stack_off, uint32_t stack_bytes,
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
    uint8_t *kmem = NULL;
    size_t kmem_size = 0;
    size_t avail;
    size_t to_copy;
    int ret;
    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(buf_off, &linear) != 0) {
        return -EFAULT;
    }
    if (elks_is_kmem_fd((int)fd)) {
        ret = elks_kmem_build_snapshot(&kmem, &kmem_size);
        if (ret != 0) {
            return ret;
        }
        if (elks_debug_enabled("perso:elks:kmem")) {
            char msg[128];

            sprintf(msg, "ELKS kmem: read fd=%u off=%u count=%u size=%u\n",
                    (unsigned int)fd,
                    (unsigned int)(current_process->fds[fd] ? current_process->fds[fd]->f_offset : 0),
                    (unsigned int)count,
                    (unsigned int)kmem_size);
            kprint(msg);
        }
        if (current_process->fds[fd]->f_offset < 0 ||
            (size_t)current_process->fds[fd]->f_offset >= kmem_size) {
            kfree(kmem, ELKS_KMEM_IMAGE_CAP);
            return 0;
        }
        avail = kmem_size - (size_t)current_process->fds[fd]->f_offset;
        to_copy = (size_t)count < avail ? (size_t)count : avail;
        memcpy((void *)(uintptr_t)linear, kmem + current_process->fds[fd]->f_offset, to_copy);
        current_process->fds[fd]->f_offset += (off_t)to_copy;
        kfree(kmem, ELKS_KMEM_IMAGE_CAP);
        return (int)to_copy;
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

static int elks_sys_link(uint32_t old_off, uint32_t new_off, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    uintptr_t old_linear = 0;
    uintptr_t new_linear = 0;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (elks_ds_pointer(old_off, &old_linear) != 0 ||
        elks_ds_pointer(new_off, &new_linear) != 0) {
        return -EFAULT;
    }
    return sys_link((const char *)(uintptr_t)old_linear, (const char *)(uintptr_t)new_linear);
}

static int elks_sys_close(uint32_t fd, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;
    return sys_close((int)fd);
}

static int elks_sys_lseek(uint32_t fd, uint32_t pos_off, uint32_t whence,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    file_t *f;
    uint8_t *kmem = NULL;
    size_t kmem_size = 0;
    uintptr_t linear = 0;
    int32_t pos = 0;
    off_t off;
    int64_t result;
    int ret;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_span(pos_off, sizeof(pos), &linear) != 0) {
        return -EFAULT;
    }
    memcpy(&pos, (const void *)(uintptr_t)linear, sizeof(pos));
    off = (off_t)pos;

    if (!elks_is_kmem_fd((int)fd)) {
        result = sys_lseek((int)fd, (uint32_t)off, (uint32_t)(((uint64_t)off) >> 32), (int)whence);
        if (result < 0) {
            return (int)result;
        }
        pos = (int32_t)result;
        memcpy((void *)(uintptr_t)linear, &pos, sizeof(pos));
        return 0;
    }
    if (fd >= MAX_FD || !current_process || !(f = current_process->fds[fd])) {
        return -EBADF;
    }

    ret = elks_kmem_build_snapshot(&kmem, &kmem_size);
    if (ret != 0) {
        return ret;
    }

    switch (whence) {
        case 0:
            break;
        case 1:
            off += f->f_offset;
            break;
        case 2:
            off += (off_t)kmem_size;
            break;
        default:
            kfree(kmem, ELKS_KMEM_IMAGE_CAP);
            return -EINVAL;
    }
    if (off < 0) {
        kfree(kmem, ELKS_KMEM_IMAGE_CAP);
        return -EINVAL;
    }
    f->f_offset = off;
    if (elks_debug_enabled("perso:elks:kmem")) {
        char msg[128];

        sprintf(msg, "ELKS kmem: lseek fd=%u whence=%u -> off=%u size=%u\n",
                (unsigned int)fd,
                (unsigned int)whence,
                (unsigned int)f->f_offset,
                (unsigned int)kmem_size);
        kprint(msg);
    }
    pos = (int32_t)f->f_offset;
    memcpy((void *)(uintptr_t)linear, &pos, sizeof(pos));
    kfree(kmem, ELKS_KMEM_IMAGE_CAP);
    return 0;
}

static int elks_sys_chdir(uint32_t path_off, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_chdir((const char *)(uintptr_t)linear);
}

static int elks_sys_time(uint32_t tloc_off, uint32_t unused1, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    elks_time_t elks_time;
    uintptr_t linear = 0;
    time_t native_time;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    native_time = sys_time(NULL);
    if (native_time < (time_t)INT32_MIN || native_time > (time_t)INT32_MAX) {
        return -EOVERFLOW;
    }
    elks_time = (elks_time_t)native_time;

    if (tloc_off != 0U) {
        if (elks_ds_span(tloc_off, sizeof(elks_time), &linear) != 0) {
            return -EFAULT;
        }
        memcpy((void *)(uintptr_t)linear, &elks_time, sizeof(elks_time));
    }
    return (int)elks_time;
}

static int elks_sys_mknod(uint32_t path_off, uint32_t mode, uint32_t dev,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_mknod((const char *)(uintptr_t)linear, (int)mode, (int)dev);
}

static int elks_sys_chmod(uint32_t path_off, uint32_t mode, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_chmod((const char *)(uintptr_t)linear, (int)mode);
}

static int elks_sys_chown(uint32_t path_off, uint32_t uid, uint32_t gid,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_lchown((const char *)(uintptr_t)linear, (int)uid, (int)gid);
}

static int elks_sys_mount(uint32_t source_off, uint32_t target_off, uint32_t fstype_off,
                          uint32_t flags, uint32_t data_off, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t source_linear = 0;
    uintptr_t target_linear = 0;
    uintptr_t fstype_linear = 0;
    uintptr_t data_linear = 0;
    void *data = NULL;

    (void)unused5; (void)unused6; (void)unused7;

    if (source_off != 0U && elks_ds_pointer(source_off, &source_linear) != 0) {
        return -EFAULT;
    }
    if (elks_ds_pointer(target_off, &target_linear) != 0 ||
        elks_ds_pointer(fstype_off, &fstype_linear) != 0) {
        return -EFAULT;
    }
    if (data_off != 0U) {
        if (elks_ds_pointer(data_off, &data_linear) != 0) {
            return -EFAULT;
        }
        data = (void *)(uintptr_t)data_linear;
    }
    return sys_mount(source_off ? (const char *)(uintptr_t)source_linear : NULL,
                     (const char *)(uintptr_t)target_linear,
                     (const char *)(uintptr_t)fstype_linear,
                     (unsigned long)flags, data);
}

static int elks_sys_umount(uint32_t target_off, uint32_t unused1, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(target_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_umount((const char *)(uintptr_t)linear);
}

static int elks_sys_stime(uint32_t time_off, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    elks_time_t elks_time;
    time_t native_time;
    uintptr_t linear = 0;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_span(time_off, sizeof(elks_time), &linear) != 0) {
        return -EFAULT;
    }
    memcpy(&elks_time, (const void *)(uintptr_t)linear, sizeof(elks_time));
    native_time = (time_t)elks_time;
    return sys_stime(&native_time);
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

static int elks_sys_getpid(uint32_t ppid_off, uint32_t unused1, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    int pid;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    pid = sys_getpid();
    if (pid < 0) {
        return pid;
    }
    if (ppid_off != 0) {
        if (elks_ds_pointer(ppid_off, &linear) != 0) {
            return -EFAULT;
        }
        *(uint16_t *)(uintptr_t)linear = (uint16_t)sys_getppid();
    }
    return pid;
}

static int elks_sys_getuid(uint32_t euid_off, uint32_t unused1, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    int uid;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    uid = sys_getuid();
    if (uid < 0) {
        return uid;
    }
    if (euid_off != 0) {
        if (elks_ds_pointer(euid_off, &linear) != 0) {
            return -EFAULT;
        }
        *(uint16_t *)(uintptr_t)linear = (uint16_t)sys_geteuid();
    }
    return uid;
}

static int elks_sys_getgid(uint32_t egid_off, uint32_t unused1, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    int gid;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    gid = sys_getgid();
    if (gid < 0) {
        return gid;
    }
    if (egid_off != 0) {
        if (elks_ds_pointer(egid_off, &linear) != 0) {
            return -EFAULT;
        }
        *(uint16_t *)(uintptr_t)linear = (uint16_t)sys_getegid();
    }
    return gid;
}

static int elks_sys_access(uint32_t path_off, uint32_t mode, uint32_t unused2,
                           uint32_t unused3, uint32_t unused4, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_access((const char *)(uintptr_t)linear, (int)mode);
}

static int elks_sys_mkdir(uint32_t path_off, uint32_t mode, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_mkdir((const char *)(uintptr_t)linear, (int)mode);
}

static int elks_sys_rmdir(uint32_t path_off, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_pointer(path_off, &linear) != 0) {
        return -EFAULT;
    }
    return sys_rmdir((const char *)(uintptr_t)linear);
}

static int elks_sys_pipe(uint32_t fds_off, uint32_t unused1, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    int kfds[2];
    uint16_t elks_fds[2];
    int ret;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (elks_ds_span(fds_off, sizeof(elks_fds), &linear) != 0) {
        return -EFAULT;
    }
    ret = kern_pipe(kfds);
    if (ret != 0) {
        return ret;
    }
    elks_fds[0] = (uint16_t)kfds[0];
    elks_fds[1] = (uint16_t)kfds[1];
    memcpy((void *)(uintptr_t)linear, elks_fds, sizeof(elks_fds));
    return 0;
}

static int elks_sys_times(uint32_t buf_off, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    struct tms *buf = NULL;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (buf_off != 0U) {
        if (elks_ds_span(buf_off, sizeof(struct tms), &linear) != 0) {
            return -EFAULT;
        }
        buf = (struct tms *)(uintptr_t)linear;
    }
    return (int)sys_times(buf);
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
    if (current_process->brk == 0 && current_process->brk_start != 0) {
        current_process->brk = current_process->brk_start;
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

static int elks_sys_vfork(uint32_t unused0, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    (void)unused0; (void)unused1; (void)unused2; (void)unused3;
    (void)unused4; (void)unused5; (void)unused6; (void)unused7;
    return sys_vfork();
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

static int elks_do_stat_path(uint32_t path_off, uint32_t stat_off, int follow_links) {
    char *path = NULL;
    uintptr_t linear = 0;
    struct stat native;
    struct elks_stat elks;
    int ret;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }
    if (elks_ds_span(stat_off, sizeof(elks), &linear) != 0) {
        kfree(path, strlen(path) + 1U);
        return -EFAULT;
    }

    ret = follow_links ? kern_stat(path, &native) : kern_lstat(path, &native);
    if (ret == 0) {
        elks_translate_stat(&elks, &native);
        memcpy((void *)(uintptr_t)linear, &elks, sizeof(elks));
    }
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_stat(uint32_t path_off, uint32_t stat_off, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;
    return elks_do_stat_path(path_off, stat_off, 1);
}

static int elks_sys_lstat(uint32_t path_off, uint32_t stat_off, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;
    return elks_do_stat_path(path_off, stat_off, 0);
}

static int elks_sys_fstat(uint32_t fd, uint32_t stat_off, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    struct stat native;
    struct elks_stat elks;
    int ret;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (elks_ds_span(stat_off, sizeof(elks), &linear) != 0) {
        return -EFAULT;
    }
    ret = kern_fstat((int)fd, &native);
    if (ret == 0) {
        elks_translate_stat(&elks, &native);
        memcpy((void *)(uintptr_t)linear, &elks, sizeof(elks));
    }
    return ret;
}

static int elks_sys_readlink(uint32_t path_off, uint32_t buf_off, uint32_t bufsiz,
                             uint32_t unused3, uint32_t unused4, uint32_t unused5,
                             uint32_t unused6, uint32_t unused7) {
    char *path = NULL;
    char *buf = NULL;
    uintptr_t linear = 0;
    int ret;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    ret = elks_copy_ds_string(path_off, &path);
    if (ret != 0) {
        return ret;
    }
    if (bufsiz == 0) {
        kfree(path, strlen(path) + 1U);
        return 0;
    }
    if (elks_ds_span(buf_off, (size_t)bufsiz, &linear) != 0) {
        kfree(path, strlen(path) + 1U);
        return -EFAULT;
    }

    buf = kmalloc((size_t)bufsiz);
    if (!buf) {
        kfree(path, strlen(path) + 1U);
        return -ENOMEM;
    }
    ret = kern_readlink(path, buf, (size_t)bufsiz);
    if (ret >= 0) {
        memcpy((void *)(uintptr_t)linear, buf, (size_t)ret);
    }
    kfree(buf, (size_t)bufsiz);
    kfree(path, strlen(path) + 1U);
    return ret;
}

static int elks_sys_readdir(uint32_t fd, uint32_t buf_off, uint32_t count,
                            uint32_t unused3, uint32_t unused4, uint32_t unused5,
                            uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    file_t *f;
    fs_node_t *node;
    struct dirent *d;
    struct elks_dirent out;
    size_t name_len = 0;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (count == 0) {
        return 0;
    }
    if (fd >= MAX_FD) {
        return -EBADF;
    }
    f = current_process->fds[fd];
    if (!f || !f->f_data) {
        return -EBADF;
    }
    node = (fs_node_t *)f->f_data;
    if (!node->readdir) {
        return -ENOTDIR;
    }
    if (elks_ds_span(buf_off, sizeof(out), &linear) != 0) {
        return -EFAULT;
    }

    d = readdir_fs(node, f->f_offset);
    if (!d) {
        return 0;
    }

    memset(&out, 0, sizeof(out));
    out.d_ino = (uint32_t)d->d_ino;
    out.d_offset = (int32_t)(f->f_offset + 1);
    while (d->d_name[name_len] != '\0' && name_len < ELKS_MAXNAMLEN) {
        out.d_name[name_len] = d->d_name[name_len];
        name_len++;
    }
    out.d_name[name_len] = '\0';
    out.d_namlen = (uint16_t)name_len;
    memcpy((void *)(uintptr_t)linear, &out, sizeof(out));
    f->f_offset++;
    return 1;
}

static int elks_sys_sbrk(uint32_t increment, uint32_t oldbrk_off, uint32_t unused2,
                         uint32_t unused3, uint32_t unused4, uint32_t unused5,
                         uint32_t unused6, uint32_t unused7) {
    uintptr_t base = 0;
    uint32_t limit = 0;
    uintptr_t old_brk;
    uintptr_t new_brk;
    uintptr_t linear = 0;
    int32_t delta = (int16_t)increment;
    int ret;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    ret = elks_ds_base_limit(&base, &limit);
    if (ret != 0) {
        return ret;
    }
    if (current_process->brk == 0 && current_process->brk_start != 0) {
        current_process->brk = current_process->brk_start;
    }
    if (oldbrk_off == 0 || elks_ds_span(oldbrk_off, sizeof(uint16_t), &linear) != 0) {
        return -EFAULT;
    }

    old_brk = (uintptr_t)current_process->brk;
    new_brk = old_brk + delta;
    if (new_brk < base || (new_brk - base) > (uintptr_t)(limit + 1U)) {
        return -ENOMEM;
    }
    (void)sys_brk((uint32_t)new_brk);
    if ((uintptr_t)current_process->brk != new_brk) {
        return -ENOMEM;
    }

    *(uint16_t *)(uintptr_t)linear = (uint16_t)(old_brk - base);
    return 0;
}

static int elks_sys_settimeofday(uint32_t tv_off, uint32_t tz_off, uint32_t unused2,
                                 uint32_t unused3, uint32_t unused4, uint32_t unused5,
                                 uint32_t unused6, uint32_t unused7) {
    struct elks_timeval etv;
    struct timeval ntv;
    uintptr_t linear = 0;
    int ret;

    (void)tz_off;
    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    if (tv_off == 0) {
        return -EFAULT;
    }
    ret = elks_ds_span(tv_off, sizeof(etv), &linear);
    if (ret != 0) {
        return ret;
    }

    memcpy(&etv, (const void *)(uintptr_t)linear, sizeof(etv));
    ntv.tv_sec = (time_t)etv.tv_sec;
    ntv.tv_usec = (suseconds_t)etv.tv_usec;
    return kern_stime(&ntv.tv_sec);
}

static int elks_sys_gettimeofday(uint32_t tv_off, uint32_t tz_off, uint32_t unused2,
                                 uint32_t unused3, uint32_t unused4, uint32_t unused5,
                                 uint32_t unused6, uint32_t unused7) {
    struct timeval ntv;
    struct timezone ntz;
    struct elks_timeval etv;
    struct elks_timezone etz;
    uintptr_t linear = 0;
    int ret;

    (void)unused2; (void)unused3; (void)unused4; (void)unused5;
    (void)unused6; (void)unused7;

    memset(&ntv, 0, sizeof(ntv));
    memset(&ntz, 0, sizeof(ntz));
    ret = kern_gettimeofday(&ntv, tz_off ? &ntz : NULL);
    if (ret != 0) {
        return ret;
    }

    if (tv_off != 0) {
        ret = elks_ds_span(tv_off, sizeof(etv), &linear);
        if (ret != 0) {
            return ret;
        }
        etv.tv_sec = (int32_t)ntv.tv_sec;
        etv.tv_usec = (int32_t)ntv.tv_usec;
        memcpy((void *)(uintptr_t)linear, &etv, sizeof(etv));
    }

    if (tz_off != 0) {
        ret = elks_ds_span(tz_off, sizeof(etz), &linear);
        if (ret != 0) {
            return ret;
        }
        etz.tz_minuteswest = (int16_t)ntz.tz_minuteswest;
        etz.tz_dsttime = (int16_t)ntz.tz_dsttime;
        memcpy((void *)(uintptr_t)linear, &etz, sizeof(etz));
    }

    return 0;
}

static int elks_sys_ustatfs(uint32_t dev, uint32_t statfs_off, uint32_t flags,
                            uint32_t unused3, uint32_t unused4, uint32_t unused5,
                            uint32_t unused6, uint32_t unused7) {
    struct mount *mp;
    struct elks_statfs elks;
    uintptr_t linear = 0;
    int fstype;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    mp = elks_mount_by_index((unsigned int)dev);
    if (!mp) {
        return -EINVAL;
    }
    fstype = elks_mount_fstype(mp);
    if (statfs_off == 0) {
        return fstype;
    }
    if (elks_ds_span(statfs_off, sizeof(elks), &linear) != 0) {
        return -EFAULT;
    }

    memset(&elks, 0, sizeof(elks));
    elks.f_type = (int16_t)fstype;
    elks.f_flags = (uint16_t)mp->mnt_flag;
    elks.f_dev = (uint16_t)dev;
    elks.f_bsize = mp->mnt_stat.f_bsize > 0 ? (int32_t)mp->mnt_stat.f_bsize : 1024;
    elks.f_blocks = (int32_t)mp->mnt_stat.f_blocks;
    if ((flags & ELKS_UF_NOFREESPACE) == 0U) {
        elks.f_bfree = (int32_t)mp->mnt_stat.f_bfree;
        elks.f_bavail = (int32_t)mp->mnt_stat.f_bavail;
    }
    elks.f_files = (int32_t)mp->mnt_stat.f_files;
    elks.f_ffree = (int32_t)mp->mnt_stat.f_ffree;
    elks_copy_cstr(elks.f_mntonname, sizeof(elks.f_mntonname),
                   mp->mnt_stat.f_mntonname[0] ? mp->mnt_stat.f_mntonname : mp->mnt_stat_path);
    memcpy((void *)(uintptr_t)linear, &elks, sizeof(elks));
    return 0;
}

static int elks_sys_select(uint32_t nfds, uint32_t readfds_off, uint32_t writefds_off,
                           uint32_t exceptfds_off, uint32_t timeout_off, uint32_t unused5,
                           uint32_t unused6, uint32_t unused7) {
    uint32_t read_mask = 0;
    uint32_t write_mask = 0;
    uint32_t except_mask = 0;
    uint32_t combined;
    struct pollfd pfds[32];
    struct elks_timeval timeout_copy;
    uintptr_t linear = 0;
    size_t poll_count = 0;
    int timeout_ms = -1;
    int ready;

    (void)unused5; (void)unused6; (void)unused7;

    if (nfds > 32U) {
        return -EINVAL;
    }
    if (readfds_off != 0U) {
        if (elks_ds_span(readfds_off, sizeof(read_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy(&read_mask, (const void *)(uintptr_t)linear, sizeof(read_mask));
    }
    if (writefds_off != 0U) {
        if (elks_ds_span(writefds_off, sizeof(write_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy(&write_mask, (const void *)(uintptr_t)linear, sizeof(write_mask));
    }
    if (exceptfds_off != 0U) {
        if (elks_ds_span(exceptfds_off, sizeof(except_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy(&except_mask, (const void *)(uintptr_t)linear, sizeof(except_mask));
    }
    if (timeout_off != 0U) {
        if (elks_ds_span(timeout_off, sizeof(timeout_copy), &linear) != 0) {
            return -EFAULT;
        }
        memcpy(&timeout_copy, (const void *)(uintptr_t)linear, sizeof(timeout_copy));
        if (timeout_copy.tv_sec < 0 || timeout_copy.tv_usec < 0 || timeout_copy.tv_usec >= 1000000) {
            return -EINVAL;
        }
        if (timeout_copy.tv_sec > (INT32_MAX / 1000)) {
            timeout_ms = INT32_MAX;
        } else {
            timeout_ms = (int)(timeout_copy.tv_sec * 1000);
            timeout_ms += (int)(timeout_copy.tv_usec / 1000);
        }
    }

    combined = read_mask | write_mask | except_mask;
    while (combined != 0U) {
        int bit = elks_first_set_bit(combined);
        short events = 0;

        if (bit < 0 || (uint32_t)bit >= nfds) {
            break;
        }
        if ((read_mask & (1U << bit)) != 0U) {
            events |= POLLIN;
        }
        if ((write_mask & (1U << bit)) != 0U) {
            events |= POLLOUT;
        }
        if ((except_mask & (1U << bit)) != 0U) {
            events |= POLLPRI;
        }
        pfds[poll_count].fd = bit;
        pfds[poll_count].events = events;
        pfds[poll_count].revents = 0;
        poll_count++;
        combined &= ~(1U << bit);
    }

    ready = kern_poll(pfds, (unsigned int)poll_count, timeout_ms);
    if (ready < 0) {
        return ready;
    }

    read_mask = 0U;
    write_mask = 0U;
    except_mask = 0U;
    for (size_t i = 0; i < poll_count; i++) {
        uint32_t bit = 1U << (uint32_t)pfds[i].fd;
        short revents = pfds[i].revents;

        if ((revents & (POLLIN | POLLRDNORM | POLLERR | POLLHUP)) != 0) {
            read_mask |= bit;
        }
        if ((revents & (POLLOUT | POLLWRNORM | POLLERR)) != 0) {
            write_mask |= bit;
        }
        if ((revents & (POLLPRI | POLLERR | POLLHUP)) != 0) {
            except_mask |= bit;
        }
    }

    if (readfds_off != 0U) {
        if (elks_ds_span(readfds_off, sizeof(read_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy((void *)(uintptr_t)linear, &read_mask, sizeof(read_mask));
    }
    if (writefds_off != 0U) {
        if (elks_ds_span(writefds_off, sizeof(write_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy((void *)(uintptr_t)linear, &write_mask, sizeof(write_mask));
    }
    if (exceptfds_off != 0U) {
        if (elks_ds_span(exceptfds_off, sizeof(except_mask), &linear) != 0) {
            return -EFAULT;
        }
        memcpy((void *)(uintptr_t)linear, &except_mask, sizeof(except_mask));
    }
    return ready;
}

static int elks_sys_uname(uint32_t uts_off, uint32_t unused1, uint32_t unused2,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    struct utsname native;
    struct elks_utsname elks;
    uintptr_t linear = 0;
    int ret;

    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    (void)unused5; (void)unused6; (void)unused7;

    if (uts_off == 0) {
        return -EFAULT;
    }
    ret = elks_ds_span(uts_off, sizeof(elks), &linear);
    if (ret != 0) {
        return ret;
    }
    ret = kern_uname(&native);
    if (ret != 0) {
        return ret;
    }

    memset(&elks, 0, sizeof(elks));
    elks_copy_cstr(elks.sysname, sizeof(elks.sysname), native.sysname);
    elks_copy_cstr(elks.nodename, sizeof(elks.nodename), native.nodename);
    elks_copy_cstr(elks.release, sizeof(elks.release), native.release);
    elks_copy_cstr(elks.version, sizeof(elks.version), native.version);
    elks_copy_cstr(elks.machine, sizeof(elks.machine), native.machine);
    memcpy((void *)(uintptr_t)linear, &elks, sizeof(elks));
    return 0;
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

static int elks_sys_ioctl(uint32_t fd, uint32_t request, uint32_t arg_off,
                          uint32_t unused3, uint32_t unused4, uint32_t unused5,
                          uint32_t unused6, uint32_t unused7) {
    uintptr_t linear = 0;
    void *arg = NULL;

    (void)unused3; (void)unused4; (void)unused5; (void)unused6; (void)unused7;

    if (arg_off != 0) {
        if (elks_ds_pointer(arg_off, &linear) != 0) {
            return -EFAULT;
        }
        arg = (void *)(uintptr_t)linear;
    }

    if (elks_is_kmem_fd((int)fd)) {
        switch (request) {
            case ELKS_MEM_GETDS:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = 0U;
                return 0;
            case ELKS_MEM_GETHEAP:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = (uint16_t)ELKS_KMEM_HEAPALL_OFFSET;
                return 0;
            case ELKS_MEM_GETSEGALL:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = (uint16_t)ELKS_KMEM_SEGALL_OFFSET;
                return 0;
            case ELKS_MEM_GETTASK:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = (uint16_t)ELKS_KMEM_TASKS_OFFSET;
                return 0;
            case ELKS_MEM_GETMAXTASKS:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = (uint16_t)MAX_PROCS;
                return 0;
            case ELKS_MEM_GETUSAGE:
                if (!arg) {
                    return -EFAULT;
                }
                {
                    struct elks_mem_usage *usage = (struct elks_mem_usage *)arg;
                    uint16_t main_used = elks_kmem_meminfo_kb(vm_phys_get_used());
                    uint16_t main_free = elks_kmem_meminfo_kb(vm_phys_get_free());

                    if ((uint32_t)main_used + (uint32_t)main_free > ELKS_MEMINFO_KB_MAX) {
                        main_free = (uint16_t)(ELKS_MEMINFO_KB_MAX - main_used);
                    }
                    usage->main_free = main_free;
                    usage->main_used = main_used;
                    usage->xms_free = 0U;
                    usage->xms_used = 0U;
                }
                return 0;
            case ELKS_MEM_GETUPTIME:
            case ELKS_MEM_GETJIFFADDR:
                if (!arg) {
                    return -EFAULT;
                }
                *(uint16_t *)arg = (uint16_t)ELKS_KMEM_JIFFIES_OFFSET;
                return 0;
            default:
                return -EINVAL;
        }
    }

    switch (request) {
        case TCGETS: {
            struct termios native;
            int ret;

            if (!arg) {
                return -EFAULT;
            }
            ret = kern_ioctl((int)fd, request, &native);
            if (ret != 0) {
                return ret;
            }
            memcpy(arg, &native, ELKS_TERMIOS_BYTES);
            return 0;
        }
        case TCSETS:
        case TCSETSW:
        case TCSETSF: {
            struct termios native;
            int ret;

            if (!arg) {
                return -EFAULT;
            }
            ret = kern_ioctl((int)fd, TCGETS, &native);
            if (ret != 0) {
                return ret;
            }
            memcpy(&native, arg, ELKS_TERMIOS_BYTES);
            return kern_ioctl((int)fd, request, &native);
        }
        default:
            return kern_ioctl((int)fd, request, arg);
    }
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
        current_thread->trap_signo = SIGSEGV;
        current_thread->trap_code = SI_KERNEL;
        current_thread->trap_addr = regs->eip;
        core_capture_trapframe(current_process, regs);
        sigexit(current_process, SIGSEGV);
        return;
    }

    sp = (uint16_t)regs->useresp;
    if (sp < sizeof(frame)) {
        current_thread->trap_signo = SIGSEGV;
        current_thread->trap_code = SI_KERNEL;
        current_thread->trap_addr = regs->useresp;
        core_capture_trapframe(current_process, regs);
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
        current_thread->trap_signo = SIGSEGV;
        current_thread->trap_code = SI_KERNEL;
        current_thread->trap_addr = regs->useresp;
        core_capture_trapframe(current_process, regs);
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
    [ELKS_SYS_vfork]   = (void *)&elks_sys_vfork,
    [ELKS_SYS_read]    = (void *)&elks_sys_read,
    [ELKS_SYS_write]   = (void *)&elks_sys_write,
    [ELKS_SYS_open]    = (void *)&elks_sys_open,
    [ELKS_SYS_close]   = (void *)&elks_sys_close,
    [ELKS_SYS_waitpid] = (void *)&elks_sys_waitpid,
    [ELKS_SYS_creat]   = (void *)&elks_sys_creat,
    [ELKS_SYS_link]    = (void *)&elks_sys_link,
    [ELKS_SYS_unlink]  = (void *)&elks_sys_unlink,
    [ELKS_SYS_execve]  = (void *)&elks_sys_execve,
    [ELKS_SYS_chdir]   = (void *)&elks_sys_chdir,
    [ELKS_SYS_time]    = (void *)&elks_sys_time,
    [ELKS_SYS_mknod]   = (void *)&elks_sys_mknod,
    [ELKS_SYS_chmod]   = (void *)&elks_sys_chmod,
    [ELKS_SYS_chown]   = (void *)&elks_sys_chown,
    [ELKS_SYS_lseek]   = (void *)&elks_sys_lseek,
    [ELKS_SYS_getpid]  = (void *)&elks_sys_getpid,
    [ELKS_SYS_mount]   = (void *)&elks_sys_mount,
    [ELKS_SYS_umount]  = (void *)&elks_sys_umount,
    [ELKS_SYS_setuid]  = (void *)&sys_setuid,
    [ELKS_SYS_getuid]  = (void *)&elks_sys_getuid,
    [ELKS_SYS_stime]   = (void *)&elks_sys_stime,
    [ELKS_SYS_settimeofday] = (void *)&elks_sys_settimeofday,
    [ELKS_SYS_alarm]   = (void *)&sys_alarm,
    [ELKS_SYS_fstat]   = (void *)&elks_sys_fstat,
    [ELKS_SYS_pause]   = (void *)&sys_pause,
    [ELKS_SYS_access]  = (void *)&elks_sys_access,
    [ELKS_SYS_sync]    = (void *)&sys_sync,
    [ELKS_SYS_kill]    = (void *)&elks_sys_kill,
    [ELKS_SYS_mkdir]   = (void *)&elks_sys_mkdir,
    [ELKS_SYS_rmdir]   = (void *)&elks_sys_rmdir,
    [ELKS_SYS_dup]     = (void *)&sys_dup,
    [ELKS_SYS_pipe]    = (void *)&elks_sys_pipe,
    [ELKS_SYS_times]   = (void *)&elks_sys_times,
    [ELKS_SYS_brk]     = (void *)&elks_sys_brk,
    [ELKS_SYS_setgid]  = (void *)&sys_setgid,
    [ELKS_SYS_getgid]  = (void *)&elks_sys_getgid,
    [ELKS_SYS_signal]  = (void *)&elks_sys_signal,
    [ELKS_SYS_fcntl]   = (void *)&sys_fcntl,
    [ELKS_SYS_ioctl]   = (void *)&elks_sys_ioctl,
    [ELKS_SYS_lstat]   = (void *)&elks_sys_lstat,
    [ELKS_SYS_readlink] = (void *)&elks_sys_readlink,
    [ELKS_SYS_gettimeofday] = (void *)&elks_sys_gettimeofday,
    [ELKS_SYS_select]  = (void *)&elks_sys_select,
    [ELKS_SYS_ustatfs] = (void *)&elks_sys_ustatfs,
    [ELKS_SYS_uname]   = (void *)&elks_sys_uname,
    [ELKS_SYS_umask]   = (void *)&sys_umask,
    [ELKS_SYS_stat]    = (void *)&elks_sys_stat,
    [ELKS_SYS_dup2]    = (void *)&sys_dup2,
    [ELKS_SYS_readdir] = (void *)&elks_sys_readdir,
    [ELKS_SYS_sbrk]    = (void *)&elks_sys_sbrk,
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
    [ELKS_SYS_vfork]   = "vfork",
    [ELKS_SYS_read]    = "read",
    [ELKS_SYS_write]   = "write",
    [ELKS_SYS_open]    = "open",
    [ELKS_SYS_close]   = "close",
    [ELKS_SYS_waitpid] = "waitpid",
    [ELKS_SYS_execve]  = "execve",
    [ELKS_SYS_alarm]   = "alarm",
    [ELKS_SYS_kill]    = "kill",
    [ELKS_SYS_select]  = "select",
    [ELKS_SYS_ustatfs] = "ustatfs",
    [ELKS_SYS_uname]   = "uname",
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

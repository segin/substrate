/*
 * procfs.c - Process Filesystem Implementation
 *
 * Table-driven design for extensibility. Each static entry is defined
 * in procfs_entries[] . Per-process directories are handled separately.
 */

#include <vfs/vfs.h>
#include <fs/procfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <kern/cmdline.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <sys/session.h>
#include <sys/mount.h>
#include <sys/tty.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <vm/vm_kmem.h>
#include <vm/vm_page.h>
#include <sys/lock.h>

/* External declarations */
extern uint32_t get_time(void);
extern uint32_t pmm_get_total_memory(void);    /* from PMM */
extern uint32_t pmm_get_free_memory(void);     /* from PMM */
extern filesystem_t *vfs_get_filesystems(void); /* from VFS */
extern struct mountlist mountlist;

/* Forward declarations */
static size_t procfs_generic_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *procfs_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *procfs_finddir(fs_node_t *node, char *name);
static int proc_self_readlink(fs_node_t *node, char *buf, size_t size);
static int proc_pid_exe_readlink(fs_node_t *node, char *buf, size_t size);
static int proc_pid_cwd_readlink(fs_node_t *node, char *buf, size_t size);

/*
 * ProcFS Entry Structure
 * Each static /proc entry is defined by a generator function that fills
 * a buffer with the file contents.
 */
struct procfs_runtime_entry {
    const char *name;
    procfs_entry_generator_t generator;
    void *opaque;
};

struct procfs_driver_entry {
    char name[64];
    struct procfs_runtime_entry runtime;
};

/* Generator functions for each /proc entry */

static uint32_t gen_meminfo(char *buf, size_t size, void *opaque) {
    (void)opaque;
    /* Get real values from PMM when available */
    uint32_t total_kb = pmm_get_total_memory() / 1024;
    uint32_t free_kb = pmm_get_free_memory() / 1024;
    uint32_t used_kb = total_kb - free_kb;
    
    return snprintf(buf, size,
        "MemTotal:    %8u kB\n"
        "MemFree:     %8u kB\n"
        "MemUsed:     %8u kB\n"
        "Buffers:            0 kB\n"
        "Cached:             0 kB\n"
        "SwapTotal:          0 kB\n"
        "SwapFree:           0 kB\n",
        total_kb, free_kb, used_kb);
}

static uint32_t gen_uptime(char *buf, size_t size, void *opaque) {
    (void)opaque;
    uint32_t t = get_time();
    return snprintf(buf, size, "%u.00 0.00\n", t);
}

static uint32_t gen_cmdline(char *buf, size_t size, void *opaque) {
    (void)opaque;
    if (!buf || size == 0) return 0;

    if (cmdline_get_full(buf, size) != 0) {
        buf[0] = '\0';
        return 0;
    }

    size_t len = strnlen(buf, size);
    if (len < size - 1) {
        buf[len] = '\n';
        buf[len + 1] = '\0';
        len++;
    }
    return len;
}

static uint32_t gen_version(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return snprintf(buf, size,
        "Substrate version 0.1.0 (gcc) #1 SMP PREEMPT %s\n",
        __DATE__);
}


static uint32_t gen_loadavg(char *buf, size_t size, void *opaque) {
    (void)opaque;
    unsigned long loads[3];
    sched_get_loadavg(loads);

    uint32_t runnable = sched_count_runnable();
    uint32_t total = sched_count_threads();
    int last_pid = proc_get_last_pid();

    return snprintf(buf, size,
        "%lu.%02lu %lu.%02lu %lu.%02lu %u/%u %d\n",
        LOAD_INT(loads[0]), LOAD_FRAC(loads[0]),
        LOAD_INT(loads[1]), LOAD_FRAC(loads[1]),
        LOAD_INT(loads[2]), LOAD_FRAC(loads[2]),
        runnable, total, last_pid);
}

static uint32_t gen_vmstat(char *buf, size_t size, void *opaque) {
    (void)opaque;
    vm_vmstat_t stats;

    vm_page_get_vmstat(&stats);
    return snprintf(buf, size,
        "free_count %u\n"
        "active_count %u\n"
        "inactive_count %u\n"
        "wire_count %u\n"
        "laundry_count %u\n"
        "pageins %u\n"
        "pageouts %u\n"
        "faults %u\n"
        "cow_faults %u\n"
        "reactivations %u\n"
        "zero_fill_pages %u\n",
        stats.free_count,
        stats.active_count,
        stats.inactive_count,
        stats.wire_count,
        stats.laundry_count,
        stats.pageins,
        stats.pageouts,
        stats.faults,
        stats.cow_faults,
        stats.reactivations,
        stats.zero_fill_pages);
}

static uint32_t proc_pmap_stats_read(char *buf, size_t size, void *opaque) {
    (void)opaque;
    struct pmap_stats stats;
    // sys_pmap_stats is declared in pmap.h
    if (sys_pmap_stats(&stats) != 0) {
        return snprintf(buf, size, "error: could not get stats\n");
    }
    
    return snprintf(buf, size,
        "Faults: %u\n"
        "COW Faults: %u\n"
        "Zero Fills: %u\n"
        "Prot Upgrades: %u\n"
        "Prot Downgrades: %u\n"
        "COW Pages Mapped: %u\n"
        "COW Duplications: %u\n"
        "Pages Saved by COW: %u\n"
        "TLB Single Invalidations: %u\n"
        "TLB Full Flushes: %u\n"
        "Total PMAPs: %u\n"
        "Active PMAPs: %u\n",
        stats.faults,
        stats.cow_faults,
        stats.zero_fills,
        stats.protection_upgrades,
        stats.protection_downgrades,
        stats.cow_pages_mapped,
        stats.cow_duplications,
        stats.pages_saved_by_cow,
        stats.tlb_invlpg_count,
        stats.tlb_full_flush_count,
        stats.total_pmaps,
        stats.active_pmaps
    );
}

static uint32_t gen_cow_stats(char *buf, size_t size, void *opaque) {
    (void)opaque;
    struct pmap_stats stats;
    if (sys_pmap_stats(&stats) != 0) {
        return snprintf(buf, size, "error: could not get COW stats\n");
    }

    return snprintf(buf, size,
        "cow_faults: %u\n"
        "cow_pages_mapped: %u\n"
        "cow_duplications: %u\n"
        "pages_saved_by_cow: %u\n",
        stats.cow_faults,
        stats.cow_pages_mapped,
        stats.cow_duplications,
        stats.pages_saved_by_cow
    );
}

static uint32_t gen_filesystems(char *buf, size_t size, void *opaque) {
    (void)opaque;
    /* Dynamically generate from VFS registry */
    uint32_t off = 0;
    filesystem_t *fs = vfs_get_filesystems();
    
    while (fs) {
        /* Check if this is a pseudo-filesystem (no device required) */
        int nodev = (strcmp(fs->name, "procfs") == 0 ||
                    strcmp(fs->name, "devfs") == 0 ||
                    strcmp(fs->name, "sysfs") == 0 ||
                    strcmp(fs->name, "tmpfs") == 0);
        
        size_t space = (off < size) ? (size - off) : 0;
        char *target = (off < size) ? (buf + off) : NULL;

        int ret;
        if (nodev) {
            ret = snprintf(target, space, "nodev\t%s\n", fs->name);
        } else {
            ret = snprintf(target, space, "\t%s\n", fs->name);
        }

        if (ret > 0) off += ret;
        fs = fs->next;
    }
    return off;
}

static size_t procfs_append_mount_field(char *buf, size_t size, size_t off, const char *field) {
    const char *src = (field && field[0]) ? field : "none";

    while (*src) {
        const char *emit = NULL;
        char ch[2] = { 0, 0 };

        switch (*src) {
            case ' ': emit = "\\040"; break;
            case '\t': emit = "\\011"; break;
            case '\n': emit = "\\012"; break;
            case '\\': emit = "\\\\"; break;
            default:
                ch[0] = *src;
                emit = ch;
                break;
        }

        if (off < size) {
            int ret = snprintf(buf + off, size - off, "%s", emit);
            if (ret > 0) {
                off += (size_t)ret;
            }
        } else {
            off += strlen(emit);
        }
        src++;
    }

    return off;
}

static uint32_t gen_mounts(char *buf, size_t size, void *opaque) {
    (void)opaque;

    size_t off = 0;
    struct mount *mp;

    TAILQ_FOREACH(mp, &mountlist, mnt_list) {
        const char *from = mp->mnt_stat.f_mntfromname[0] ? mp->mnt_stat.f_mntfromname : mp->mnt_stat.f_fstypename;
        const char *to = mp->mnt_stat.f_mntonname[0] ? mp->mnt_stat.f_mntonname : mp->mnt_stat_path;
        const char *type = mp->mnt_stat.f_fstypename[0] ? mp->mnt_stat.f_fstypename : "unknown";

        off = procfs_append_mount_field(buf, size, off, from);
        if (off < size) off += (size_t)snprintf(buf + off, size - off, " ");
        else off += 1;

        off = procfs_append_mount_field(buf, size, off, to);
        if (off < size) off += (size_t)snprintf(buf + off, size - off, " ");
        else off += 1;

        off = procfs_append_mount_field(buf, size, off, type);
        if (off < size) off += (size_t)snprintf(buf + off, size - off, " rw 0 0\n");
        else off += strlen(" rw 0 0\n");
    }

    return (uint32_t)off;
}

/*
 * Entry table - Static /proc entries
 * Add new entries here for automatic registration.
 */
static struct procfs_runtime_entry procfs_entries[] = {
    { "meminfo",     gen_meminfo,       NULL },
    { "uptime",      gen_uptime,        NULL },
    { "cmdline",     gen_cmdline,       NULL },
    { "vmstat",      gen_vmstat,        NULL },
    { "version",     gen_version,       NULL },
    { "loadavg",     gen_loadavg,       NULL },
    { "cow_stats",   gen_cow_stats,     NULL },
    { "pmap_stats",  proc_pmap_stats_read, NULL },
    { "filesystems", gen_filesystems,   NULL },
    { "mounts",      gen_mounts,        NULL },
    { NULL, NULL, NULL }  /* Sentinel */
};

#define PROCFS_MAX_DRIVER_ENTRIES 32
#define PROCFS_STATIC_COUNT (sizeof(procfs_entries) / sizeof(procfs_entries[0]) - 1)
#define PROCFS_SELF_INO 0xFFFFFFFFFFFFFF00ULL
#define PROCFS_DRIVER_INO_BASE 0xFFFFFFFFFFFF1000ULL

/* Static nodes for permanent entries to avoid dynamic allocation/races */
static fs_node_t procfs_static_nodes[PROCFS_STATIC_COUNT];
static struct procfs_driver_entry procfs_driver_entries[PROCFS_MAX_DRIVER_ENTRIES];
static fs_node_t procfs_driver_nodes[PROCFS_MAX_DRIVER_ENTRIES];
static fs_node_t procfs_self_node;
static fs_node_t procfs_pid_dir_nodes[MAX_PROCS];
static fs_node_t procfs_pid_status_nodes[MAX_PROCS];
static fs_node_t procfs_pid_cmdline_nodes[MAX_PROCS];
static fs_node_t procfs_pid_stat_nodes[MAX_PROCS];
static fs_node_t procfs_pid_exe_nodes[MAX_PROCS];
static fs_node_t procfs_pid_cwd_nodes[MAX_PROCS];
static size_t procfs_driver_entry_count = 0;

static struct procfs_runtime_entry *procfs_find_driver_entry(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < procfs_driver_entry_count; i++) {
        if (strcmp(procfs_driver_entries[i].name, name) == 0) {
            return &procfs_driver_entries[i].runtime;
        }
    }
    return NULL;
}

int procfs_register_entry(const char *name, procfs_entry_generator_t generator, void *opaque) {
    if (!name || !name[0] || !generator) return -1;

    for (const char *p = name; *p; p++) {
        if (*p == '/') return -1;
    }
    if (strlen(name) >= sizeof(procfs_driver_entries[0].name)) return -1;

    struct procfs_runtime_entry *existing = procfs_find_driver_entry(name);
    if (existing) {
        existing->generator = generator;
        existing->opaque = opaque;
        return 0;
    }

    if (procfs_driver_entry_count >= PROCFS_MAX_DRIVER_ENTRIES) {
        return -1;
    }

    struct procfs_driver_entry *slot = &procfs_driver_entries[procfs_driver_entry_count++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->name, name, sizeof(slot->name) - 1);
    slot->name[sizeof(slot->name) - 1] = '\0';
    slot->runtime.name = slot->name;
    slot->runtime.generator = generator;
    slot->runtime.opaque = opaque;
    return 0;
}

static int procfs_find_process_slot(int pid) {
    if (pid <= 0) return -1;

    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            return i;
        }
    }

    return -1;
}

static fs_node_t *procfs_get_self_node(void) {
    return &procfs_self_node;
}

static fs_node_t *procfs_get_driver_node(struct procfs_runtime_entry *entry) {
    if (!entry || !entry->name || !entry->generator) return NULL;

    for (size_t i = 0; i < procfs_driver_entry_count; i++) {
        if (&procfs_driver_entries[i].runtime == entry) {
            strncpy(procfs_driver_nodes[i].name, entry->name, sizeof(procfs_driver_nodes[i].name) - 1);
            procfs_driver_nodes[i].name[sizeof(procfs_driver_nodes[i].name) - 1] = '\0';
            procfs_driver_nodes[i].inode = PROCFS_DRIVER_INO_BASE + i;
            procfs_driver_nodes[i].impl = (uintptr_t)entry;
            return &procfs_driver_nodes[i];
        }
    }

    return NULL;
}

static int proc_self_readlink(fs_node_t *node, char *buf, size_t size) {
    (void)node;
    if (!buf || size == 0) return -1;

    int pid = (current_process && current_process->pid > 0) ? current_process->pid : 0;
    char target[32];
    int len = snprintf(target, sizeof(target), "/proc/%d/", pid);
    if (len < 0) return -1;

    size_t copy_len = (size_t)len;
    if (copy_len >= size) copy_len = size - 1;

    memcpy(buf, target, copy_len);
    buf[copy_len] = '\0';
    return (int)copy_len;
}

static int proc_copy_process_link_target(process_t *p, const char *src, char *buf, size_t size) {
    size_t copy_len;

    if (!p || !buf || size == 0) return -1;
    if (!src || src[0] == '\0') return -1;

    copy_len = strnlen(src, size);
    if (copy_len >= size) copy_len = size - 1;
    memcpy(buf, src, copy_len);
    buf[copy_len] = '\0';
    return (int)copy_len;
}

static int proc_pid_exe_readlink(fs_node_t *node, char *buf, size_t size) {
    process_t *p = proc_find((int)node->inode);
    if (!p) return -1;
    return proc_copy_process_link_target(p, p->exec_path, buf, size);
}

static int proc_pid_cwd_readlink(fs_node_t *node, char *buf, size_t size) {
    process_t *p = proc_find((int)node->inode);
    if (!p) return -1;
    if (!p->cwd_path[0]) {
        return proc_copy_process_link_target(p, "/", buf, size);
    }
    return proc_copy_process_link_target(p, p->cwd_path, buf, size);
}

/*
 * Generic read function for table-driven entries
 * The entry pointer is stored in node->impl
 */
static size_t procfs_generic_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    struct procfs_runtime_entry *entry = (struct procfs_runtime_entry *)(uintptr_t)node->impl;
    if (!entry || !entry->generator) return 0;
    
    char tmp[1024];
    uint32_t len = entry->generator(tmp, sizeof(tmp), entry->opaque);
    
    char *buf = tmp;
    char *alloc_buf = NULL;
    size_t alloc_size = 0;

    /* Check for truncation and retry with larger buffer if needed */
    if (len >= sizeof(tmp)) {
        alloc_size = len + 1;
        alloc_buf = kmalloc(alloc_size);
        if (alloc_buf) {
            len = entry->generator(alloc_buf, alloc_size, entry->opaque);
            buf = alloc_buf;
        }
    }
    
    size_t read_len = 0;
    if (offset < len) {
        read_len = size;
        if (offset + read_len > len) {
            read_len = len - offset;
        }
        memcpy(buffer, buf + offset, read_len);
    }

    if (alloc_buf) {
        kfree(alloc_buf, alloc_size);
    }

    return read_len;
}

/* Per-process directory support */
static struct dirent proc_dirent;
static fs_node_t procfs_root_node;

/* Helper to generate status string */
static int proc_generate_status(char *b, size_t s, process_t *proc) {
    char comm_safe[AC_COMM_LEN + 1];
    strncpy(comm_safe, proc->comm, AC_COMM_LEN);
    comm_safe[AC_COMM_LEN] = '\0';

    /* Sanitize comm to prevent procfs line injection */
    for (int i = 0; comm_safe[i] != '\0'; i++) {
        if ((unsigned char)comm_safe[i] < 32 || (unsigned char)comm_safe[i] > 126) {
            comm_safe[i] = '_';
        }
    }

    struct personality *pers = perso_lookup(current_process->perso_id);
    if (pers && strcmp(pers->name, "Linux") == 0) {
        return snprintf(b, s,
            "Name:\t%s\n"
            "State:\tR (running)\n"
            "Tgid:\t%d\n"
            "Pid:\t%d\n"
            "Uid:\t%d\t%d\t%d\t%d\n"
            "Gid:\t%d\t%d\t%d\t%d\n",
            comm_safe, proc->pid, proc->pid,
            proc->uid, proc->uid, proc->uid, proc->uid,
            proc->gid, proc->gid, proc->gid, proc->gid);
    } else {
        return snprintf(b, s,
            "Name:\t%s\n"
            "Pid:\t%d\n"
            "Uid:\t%d\n"
            "Gid:\t%d\n"
            "State:\tRunning\n",
            comm_safe, proc->pid, proc->uid, proc->gid);
    }
}

static size_t proc_pid_status_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    int pid = node->inode;
    process_t *p = NULL;
    
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid == pid) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return 0;

    char buf[1024];
    int len;

    len = proc_generate_status(buf, sizeof(buf), p);

    char *source_buf = buf;
    char *alloc_buf = NULL;
    size_t alloc_size = 0;

    if (len >= (int)sizeof(buf)) {
        alloc_size = len + 1;
        alloc_buf = kmalloc(alloc_size);
        if (alloc_buf) {
            len = proc_generate_status(alloc_buf, alloc_size, p);
            source_buf = alloc_buf;
        }
    }

    if (len < 0) len = 0;
    if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
    
    size_t read_len = 0;
    if (offset < (uint32_t)len) {
        read_len = size;
        if (offset + read_len > (uint32_t)len) {
            read_len = len - offset;
        }
        memcpy(buffer, source_buf + offset, read_len);
    }

    if (alloc_buf) {
        kfree(alloc_buf, alloc_size);
    }

    return read_len;
}

static char proc_linux_state(process_t *proc) {
    if (!proc) return 'R';
    switch (proc->state) {
        case SRUN:   return 'R';
        case SSLEEP: return 'S';
        case SSTOP:  return 'T';
        case SZOMB:  return 'Z';
        case SDYING: return 'X';
        default:     return 'R';
    }
}

static int proc_generate_stat(char *b, size_t s, process_t *proc) {
    char comm_safe[AC_COMM_LEN + 1];
    strncpy(comm_safe, proc->comm, AC_COMM_LEN);
    comm_safe[AC_COMM_LEN] = '\0';

    for (int i = 0; comm_safe[i] != '\0'; i++) {
        unsigned char c = (unsigned char)comm_safe[i];
        if (c < 32 || c > 126 || c == ')' || c == '(') {
            comm_safe[i] = '_';
        }
    }

    int ppid = proc->ppid;
    int pgrp = proc->p_pgrp ? proc->p_pgrp->pg_id : proc->pid;
    int session = (proc->p_pgrp && proc->p_pgrp->pg_session)
                      ? proc->p_pgrp->pg_session->s_sid
                      : pgrp;
    int tty_nr = 0;
    int tpgid = (proc->tty) ? proc->tty->pgrp : -1;

    /*
     * Linux-style /proc/<pid>/stat requires a long, fixed-order field list.
     * Userland parsers (e.g. BusyBox ps) often skip deep into the tail fields.
     * Provide a full-shaped record with conservative defaults for unsupported
     * accounting values so field-skipping never runs past end-of-string.
     */
    return snprintf(b, s,
        "%d (%s) %c %d %d %d %d %d "
        "0 0 0 0 0 "                   /* flags..cmajflt */
        "%u %u 0 0 20 0 1 0 %u 0 0 "   /* utime..rss */
        "0 0 0 0 0 0 0 0 0 0 "         /* rsslim..cnswap */
        "0 0 0 0 0 0 0 0 0 0 "         /* exit_signal..arg_end */
        "0 0 0 0 0 0 0 0 0 0 "         /* env_start..exit_code */
        "0 0 0 0 0 0 0 0 0 0\n",       /* extra tail padding */
        proc->pid, comm_safe, proc_linux_state(proc), ppid, pgrp, session, tty_nr, tpgid,
        proc->utime, proc->stime, proc->start_time);
}

static size_t proc_pid_stat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    int pid = node->inode;
    process_t *p = proc_find(pid);
    if (!p) return 0;

    char tmp[512];
    int len = proc_generate_stat(tmp, sizeof(tmp), p);
    if (len < 0) return 0;

    char *source_buf = tmp;
    char *alloc_buf = NULL;
    size_t alloc_size = 0;
    if (len >= (int)sizeof(tmp)) {
        alloc_size = (size_t)len + 1;
        alloc_buf = kmalloc(alloc_size);
        if (alloc_buf) {
            len = proc_generate_stat(alloc_buf, alloc_size, p);
            source_buf = alloc_buf;
        } else {
            len = (int)sizeof(tmp) - 1;
        }
    }

    if ((size_t)offset >= (size_t)len) return 0;
    size_t read_len = size;
    if ((size_t)offset + read_len > (size_t)len) {
        read_len = (size_t)len - (size_t)offset;
    }
    memcpy(buffer, source_buf + offset, read_len);
    if (alloc_buf) {
        kfree(alloc_buf, alloc_size);
    }
    return read_len;
}

static size_t proc_pid_cmdline_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    int pid = node->inode;
    process_t *p = proc_find(pid);
    char cmdline[PROC_CMDLINE_MAX];
    size_t total_len;

    if (!p) return 0;
    total_len = proc_emit_cmdline(p, cmdline, sizeof(cmdline), NULL);

    if ((size_t)offset >= total_len) return 0;

    size_t read_len = size;
    if ((size_t)offset + read_len > total_len) {
        read_len = total_len - (size_t)offset;
    }
    memcpy(buffer, cmdline + (size_t)offset, read_len);
    return read_len;
}

static struct dirent *proc_pid_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    const char *entries[] = { ".", "..", "status", "cmdline", "stat", "exe", "cwd", NULL };
    if (index >= 7) return NULL;
    strncpy(proc_dirent.d_name, entries[index], sizeof(proc_dirent.d_name) - 1);
    proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
    proc_dirent.d_ino = node->inode;
    return &proc_dirent;
}

static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name) {
    if (strcmp(name, ".") == 0) return node;
    if (strcmp(name, "..") == 0) return &procfs_root_node;

    int slot = procfs_find_process_slot((int)node->inode);
    if (slot < 0) return NULL;

    if (strcmp(name, "status") == 0) {
        procfs_pid_status_nodes[slot].inode = node->inode;
        return &procfs_pid_status_nodes[slot];
    }
    if (strcmp(name, "cmdline") == 0) {
        procfs_pid_cmdline_nodes[slot].inode = node->inode;
        return &procfs_pid_cmdline_nodes[slot];
    }
    if (strcmp(name, "stat") == 0) {
        procfs_pid_stat_nodes[slot].inode = node->inode;
        return &procfs_pid_stat_nodes[slot];
    }
    if (strcmp(name, "exe") == 0) {
        procfs_pid_exe_nodes[slot].inode = node->inode;
        return &procfs_pid_exe_nodes[slot];
    }
    if (strcmp(name, "cwd") == 0) {
        procfs_pid_cwd_nodes[slot].inode = node->inode;
        return &procfs_pid_cwd_nodes[slot];
    }
    return NULL;
}

/* Root /proc directory operations */

static struct procfs_runtime_entry *procfs_driver_entry_by_index(uint64_t index) {
    if (index >= procfs_driver_entry_count) return NULL;
    return &procfs_driver_entries[index].runtime;
}

static struct dirent *procfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    
    /* . and .. */
    if (index == 0) {
        strncpy(proc_dirent.d_name, ".", sizeof(proc_dirent.d_name) - 1);
        proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
        proc_dirent.d_ino = node->inode;
        return &proc_dirent;
    }
    if (index == 1) {
        strncpy(proc_dirent.d_name, "..", sizeof(proc_dirent.d_name) - 1);
        proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
        proc_dirent.d_ino = node->inode;
        return &proc_dirent;
    }
    
    uint64_t entry_idx = index - 2;

    /* Static entries from table */
    if (entry_idx < PROCFS_STATIC_COUNT) {
        strncpy(proc_dirent.d_name, procfs_entries[entry_idx].name, sizeof(proc_dirent.d_name) - 1);
        proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
        proc_dirent.d_ino = entry_idx + 1; // Assign a unique inode for static entries
        return &proc_dirent;
    }
    entry_idx -= PROCFS_STATIC_COUNT;

    /* Driver-provided entries */
    if (entry_idx < procfs_driver_entry_count) {
        struct procfs_runtime_entry *dyn = procfs_driver_entry_by_index(entry_idx);
        if (!dyn || !dyn->name) return NULL;
        strncpy(proc_dirent.d_name, dyn->name, sizeof(proc_dirent.d_name) - 1);
        proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
        proc_dirent.d_ino = PROCFS_DRIVER_INO_BASE + entry_idx;
        return &proc_dirent;
    }
    entry_idx -= procfs_driver_entry_count;

    if (entry_idx == 0) {
        strncpy(proc_dirent.d_name, "self", sizeof(proc_dirent.d_name) - 1);
        proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
        proc_dirent.d_ino = PROCFS_SELF_INO;
        return &proc_dirent;
    }
    entry_idx -= 1;
    
    /* Process directories */
    uint64_t proc_idx = entry_idx;
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid > 0) {
            if (count == proc_idx) {
                snprintf(proc_dirent.d_name, sizeof(proc_dirent.d_name), "%d", processes[i].pid);
                proc_dirent.d_ino = processes[i].pid;
                return &proc_dirent;
            }
            count++;
        }
    }
    return NULL;
}

static fs_node_t *procfs_finddir(fs_node_t *node, char *name) {
    if (strcmp(name, ".") == 0) return node;
    if (strcmp(name, "..") == 0) return node;
    
    /* Search static entries table */
    for (int i = 0; procfs_entries[i].name != NULL; i++) {
        if (strcmp(name, procfs_entries[i].name) == 0) {
            return &procfs_static_nodes[i];
        }
    }

    struct procfs_runtime_entry *dyn = procfs_find_driver_entry(name);
    if (dyn) {
        return procfs_get_driver_node(dyn);
    }

    if (strcmp(name, "self") == 0) {
        return procfs_get_self_node();
    }

    /* Parse numeric PID */
    int pid = 0;
    char *p = name;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }

    if (pid > 0 && *p == '\0') {
        int slot = procfs_find_process_slot(pid);
        if (slot >= 0) {
            procfs_pid_dir_nodes[slot].inode = pid;
            snprintf(procfs_pid_dir_nodes[slot].name, sizeof(procfs_pid_dir_nodes[slot].name), "%d", pid);
            return &procfs_pid_dir_nodes[slot];
        }
    }
    return NULL;
}

/* Mount and initialization */

static fs_node_t *procfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    return &procfs_root_node;
}

static filesystem_t procfs_fs = {
    .name = "procfs",
    .mount = &procfs_mount,
};

void procfs_init(void) {
    // Initialize static nodes for race-free access
    for (size_t i = 0; i < PROCFS_STATIC_COUNT; i++) {
        memset(&procfs_static_nodes[i], 0, sizeof(fs_node_t));
        // Use strncpy to prevent overflow, although names are known to be short
        strncpy(procfs_static_nodes[i].name, procfs_entries[i].name, sizeof(procfs_static_nodes[i].name) - 1);
        procfs_static_nodes[i].name[sizeof(procfs_static_nodes[i].name) - 1] = '\0';
        procfs_static_nodes[i].flags = FS_FILE;
        procfs_static_nodes[i].mask = 0444;
        procfs_static_nodes[i].uid = 0;
        procfs_static_nodes[i].gid = 0;
        procfs_static_nodes[i].impl = (uintptr_t)&procfs_entries[i];
        procfs_static_nodes[i].read = &procfs_generic_read;
    }

    for (size_t i = 0; i < PROCFS_MAX_DRIVER_ENTRIES; i++) {
        memset(&procfs_driver_nodes[i], 0, sizeof(fs_node_t));
        procfs_driver_nodes[i].flags = FS_FILE;
        procfs_driver_nodes[i].mask = 0444;
        procfs_driver_nodes[i].uid = 0;
        procfs_driver_nodes[i].gid = 0;
        procfs_driver_nodes[i].read = &procfs_generic_read;
    }

    memset(&procfs_self_node, 0, sizeof(fs_node_t));
    strncpy(procfs_self_node.name, "self", sizeof(procfs_self_node.name) - 1);
    procfs_self_node.name[sizeof(procfs_self_node.name) - 1] = '\0';
    procfs_self_node.flags = FS_SYMLINK;
    procfs_self_node.mask = 0777;
    procfs_self_node.uid = 0;
    procfs_self_node.gid = 0;
    procfs_self_node.inode = PROCFS_SELF_INO;
    procfs_self_node.readlink = &proc_self_readlink;
    procfs_self_node.length = 8;

    for (int i = 0; i < MAX_PROCS; i++) {
        memset(&procfs_pid_dir_nodes[i], 0, sizeof(fs_node_t));
        procfs_pid_dir_nodes[i].flags = FS_DIRECTORY;
        procfs_pid_dir_nodes[i].mask = 0555;
        procfs_pid_dir_nodes[i].uid = 0;
        procfs_pid_dir_nodes[i].gid = 0;
        procfs_pid_dir_nodes[i].readdir = &proc_pid_readdir;
        procfs_pid_dir_nodes[i].finddir = &proc_pid_finddir;

        memset(&procfs_pid_status_nodes[i], 0, sizeof(fs_node_t));
        strncpy(procfs_pid_status_nodes[i].name, "status", sizeof(procfs_pid_status_nodes[i].name) - 1);
        procfs_pid_status_nodes[i].name[sizeof(procfs_pid_status_nodes[i].name) - 1] = '\0';
        procfs_pid_status_nodes[i].flags = FS_FILE;
        procfs_pid_status_nodes[i].mask = 0444;
        procfs_pid_status_nodes[i].uid = 0;
        procfs_pid_status_nodes[i].gid = 0;
        procfs_pid_status_nodes[i].read = &proc_pid_status_read;

        memset(&procfs_pid_cmdline_nodes[i], 0, sizeof(fs_node_t));
        strncpy(procfs_pid_cmdline_nodes[i].name, "cmdline", sizeof(procfs_pid_cmdline_nodes[i].name) - 1);
        procfs_pid_cmdline_nodes[i].name[sizeof(procfs_pid_cmdline_nodes[i].name) - 1] = '\0';
        procfs_pid_cmdline_nodes[i].flags = FS_FILE;
        procfs_pid_cmdline_nodes[i].mask = 0444;
        procfs_pid_cmdline_nodes[i].uid = 0;
        procfs_pid_cmdline_nodes[i].gid = 0;
        procfs_pid_cmdline_nodes[i].read = &proc_pid_cmdline_read;

        memset(&procfs_pid_stat_nodes[i], 0, sizeof(fs_node_t));
        strncpy(procfs_pid_stat_nodes[i].name, "stat", sizeof(procfs_pid_stat_nodes[i].name) - 1);
        procfs_pid_stat_nodes[i].name[sizeof(procfs_pid_stat_nodes[i].name) - 1] = '\0';
        procfs_pid_stat_nodes[i].flags = FS_FILE;
        procfs_pid_stat_nodes[i].mask = 0444;
        procfs_pid_stat_nodes[i].uid = 0;
        procfs_pid_stat_nodes[i].gid = 0;
        procfs_pid_stat_nodes[i].read = &proc_pid_stat_read;

        memset(&procfs_pid_exe_nodes[i], 0, sizeof(fs_node_t));
        strncpy(procfs_pid_exe_nodes[i].name, "exe", sizeof(procfs_pid_exe_nodes[i].name) - 1);
        procfs_pid_exe_nodes[i].name[sizeof(procfs_pid_exe_nodes[i].name) - 1] = '\0';
        procfs_pid_exe_nodes[i].flags = FS_SYMLINK;
        procfs_pid_exe_nodes[i].mask = 0777;
        procfs_pid_exe_nodes[i].uid = 0;
        procfs_pid_exe_nodes[i].gid = 0;
        procfs_pid_exe_nodes[i].readlink = &proc_pid_exe_readlink;

        memset(&procfs_pid_cwd_nodes[i], 0, sizeof(fs_node_t));
        strncpy(procfs_pid_cwd_nodes[i].name, "cwd", sizeof(procfs_pid_cwd_nodes[i].name) - 1);
        procfs_pid_cwd_nodes[i].name[sizeof(procfs_pid_cwd_nodes[i].name) - 1] = '\0';
        procfs_pid_cwd_nodes[i].flags = FS_SYMLINK;
        procfs_pid_cwd_nodes[i].mask = 0777;
        procfs_pid_cwd_nodes[i].uid = 0;
        procfs_pid_cwd_nodes[i].gid = 0;
        procfs_pid_cwd_nodes[i].readlink = &proc_pid_cwd_readlink;
    }

    memset(&procfs_root_node, 0, sizeof(fs_node_t));
    strncpy(procfs_root_node.name, "proc", sizeof(procfs_root_node.name) - 1);
    procfs_root_node.name[sizeof(procfs_root_node.name) - 1] = '\0';
    procfs_root_node.flags = FS_DIRECTORY;
    procfs_root_node.mask = 0555;
    procfs_root_node.uid = 0;
    procfs_root_node.gid = 0;
    procfs_root_node.readdir = &procfs_readdir;
    procfs_root_node.finddir = &procfs_finddir;

    vfs_register_filesystem(&procfs_fs);
}

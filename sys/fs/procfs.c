/*
 * procfs.c - Process Filesystem Implementation
 *
 * Table-driven design for extensibility. Each static entry is defined
 * in procfs_entries[] . Per-process directories are handled separately.
 */

#include <vfs/vfs.h>
#include <arch/i386/pmm.h>
#include <fs/procfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/cmdline.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <sys/session.h>
#include <sys/mount.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <vm/vm_kmem.h>
#include <vm/vm_page.h>
#include <sys/lock.h>
#include <kern/bus.h>
#include <kern/pci.h>
#include <kern/resource.h>
#include <sys/kobject.h>

/* Forward declarations */
static size_t procfs_generic_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *procfs_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *procfs_finddir(fs_node_t *node, char *name);
static int proc_self_readlink(fs_node_t *node, char *buf, size_t size);
static int proc_pid_exe_readlink(fs_node_t *node, char *buf, size_t size);
static int proc_pid_cwd_readlink(fs_node_t *node, char *buf, size_t size);
static int proc_pid_fd_readlink(fs_node_t *node, char *buf, size_t size);
static struct dirent *proc_pid_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name);
static struct dirent *proc_pid_fd_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *proc_pid_fd_finddir(fs_node_t *node, char *name);
static struct dirent *proc_pid_task_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *proc_pid_task_finddir(fs_node_t *node, char *name);
static struct dirent *proc_pid_task_tid_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *proc_pid_task_tid_finddir(fs_node_t *node, char *name);
static size_t proc_pid_task_comm_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t proc_pid_task_status_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t proc_pid_task_stat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t proc_pid_status_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t proc_pid_stat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t proc_pid_cmdline_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);

static void procfs_refresh_timestamps(fs_node_t *node) {
    time_t now;

    if (!node) {
        return;
    }

    now = get_time();
    node->atime = now;
    node->mtime = now;
    node->ctime = get_boot_time();
}

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

/* Diagnostic counters from pmap.c — defined there, declared here */
extern uint64_t pmap_destroy_anon_freed;
extern uint64_t pmap_destroy_anon_skipped;
extern uint64_t pmap_destroy_skip_obj;
extern uint64_t pmap_destroy_skip_wired;
extern uint64_t pmap_destroy_skip_refcnt;
extern uint64_t pmap_destroy_calls;
extern uint64_t pmap_destroy_anon_rc0;
extern uint64_t pmap_destroy_anon_rc2;
extern uint64_t pmap_destroy_anon_rc_big;

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
        "Active PMAPs: %u\n"
        "Destroy Calls: %llu\n"
        "Destroy Anon Freed: %llu\n"
        "Destroy Anon Skipped: %llu\n"
        "  skip reason obj!=NULL: %llu\n"
        "  skip reason wired:     %llu\n"
        "  skip reason refcnt!=1: %llu\n"
        "  anon refcnt==0:        %llu\n"
        "  anon refcnt==2:        %llu\n"
        "  anon refcnt>=3:        %llu\n",
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
        stats.active_pmaps,
        (unsigned long long)pmap_destroy_calls,
        (unsigned long long)pmap_destroy_anon_freed,
        (unsigned long long)pmap_destroy_anon_skipped,
        (unsigned long long)pmap_destroy_skip_obj,
        (unsigned long long)pmap_destroy_skip_wired,
        (unsigned long long)pmap_destroy_skip_refcnt,
        (unsigned long long)pmap_destroy_anon_rc0,
        (unsigned long long)pmap_destroy_anon_rc2,
        (unsigned long long)pmap_destroy_anon_rc_big
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
    /* Dynamically generate from VFS registry.  Format follows the Linux
     * /proc/filesystems convention: "nodev\t<name>" for filesystems
     * that don't need a backing device (VFS_CAP_VIRTUAL or no
     * VFS_CAP_NEEDS_DEV), "\t<name>" otherwise. */
    uint32_t off = 0;
    filesystem_t *fs = vfs_get_filesystems();

    while (fs) {
        int nodev;
        if (fs->caps != 0) {
            nodev = (fs->caps & VFS_CAP_VIRTUAL) ||
                    !(fs->caps & VFS_CAP_NEEDS_DEV);
        } else {
            /* Pre-existing filesystems without caps fall back to a
             * name-based heuristic until they're updated. */
            nodev = (strcmp(fs->name, "procfs") == 0 ||
                     strcmp(fs->name, "devfs") == 0 ||
                     strcmp(fs->name, "sysfs") == 0 ||
                     strcmp(fs->name, "tmpfs") == 0);
        }

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

static uint32_t gen_ioports(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)resource_dump(RES_IO, buf, size);
}

static uint32_t gen_iomem(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)resource_dump(RES_MEM, buf, size);
}

static uint32_t gen_pci(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)pci_dump_devices(buf, size);
}

static uint32_t gen_devtree(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)bus_dump_tree(buf, size);
}

static uint32_t gen_device_events(char *buf, size_t size, void *opaque) {
    (void)opaque;
    return (uint32_t)kobject_uevent_dump(buf, size);
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
    { "ioports",     gen_ioports,       NULL },
    { "iomem",       gen_iomem,         NULL },
    { "pci",         gen_pci,           NULL },
    { "devtree",     gen_devtree,       NULL },
    { "device-events", gen_device_events, NULL },
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
static size_t procfs_driver_entry_count = 0;

typedef struct procfs_pid_nodes {
    int pid;
    fs_node_t dir;
    fs_node_t status;
    fs_node_t cmdline;
    fs_node_t stat;
    fs_node_t exe;
    fs_node_t cwd;
    fs_node_t fd_dir;
    fs_node_t fd_links[MAX_FD];
    fs_node_t task_dir;
} procfs_pid_nodes_t;

static procfs_pid_nodes_t **procfs_pid_nodes = NULL;
static size_t procfs_pid_nodes_count = 0;
static size_t procfs_pid_nodes_capacity = 0;
/* Serializes the (procfs_pid_nodes, count, capacity) triple — without
 * it concurrent procfs_get_pid_nodes() from two threads can both grow
 * the array, both kfree the old pointer, double-free or use-after-free. */
static spinlock_t procfs_pid_nodes_lock = { 0 };
static int procfs_pid_nodes_lock_init = 0;

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

static procfs_pid_nodes_t *procfs_get_pid_nodes(int pid) {
    procfs_pid_nodes_t **new_nodes;
    procfs_pid_nodes_t *nodes;
    size_t old_capacity;
    size_t new_capacity;
    size_t old_bytes;
    size_t new_bytes;
    process_t *target;

    target = (pid > 0) ? proc_find(pid) : NULL;
    if (!target) {
        return NULL;
    }

    if (!procfs_pid_nodes_lock_init) {
        spinlock_init(&procfs_pid_nodes_lock, "procfs_pid");
        procfs_pid_nodes_lock_init = 1;
    }
    spinlock_acquire(&procfs_pid_nodes_lock);

    for (size_t i = 0; i < procfs_pid_nodes_count; i++) {
        if (procfs_pid_nodes[i] && procfs_pid_nodes[i]->pid == pid) {
            procfs_pid_nodes_t *r = procfs_pid_nodes[i];
            spinlock_release(&procfs_pid_nodes_lock);
            return r;
        }
    }

    if (procfs_pid_nodes_count == procfs_pid_nodes_capacity) {
        old_capacity = procfs_pid_nodes_capacity;
        new_capacity = old_capacity ? old_capacity * 2U : 16U;
        old_bytes = old_capacity * sizeof(*procfs_pid_nodes);
        new_bytes = new_capacity * sizeof(*procfs_pid_nodes);

        new_nodes = kmalloc(new_bytes);
        if (!new_nodes) {
            spinlock_release(&procfs_pid_nodes_lock);
            return NULL;
        }
        if (procfs_pid_nodes && old_bytes != 0) {
            memcpy(new_nodes, procfs_pid_nodes, old_bytes);
            kfree(procfs_pid_nodes, old_bytes);
        }
        memset(new_nodes + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*new_nodes));
        procfs_pid_nodes = new_nodes;
        procfs_pid_nodes_capacity = new_capacity;
    }

    nodes = kmalloc(sizeof(*nodes));
    if (!nodes) {
        spinlock_release(&procfs_pid_nodes_lock);
        return NULL;
    }
    memset(nodes, 0, sizeof(*nodes));
    procfs_pid_nodes[procfs_pid_nodes_count++] = nodes;
    nodes->pid = pid;

    nodes->dir.flags = FS_DIRECTORY;
    nodes->dir.mask = 0555;
    nodes->dir.uid = target->uid;
    nodes->dir.gid = target->gid;
    nodes->dir.readdir = &proc_pid_readdir;
    nodes->dir.finddir = &proc_pid_finddir;
    procfs_refresh_timestamps(&nodes->dir);

    strncpy(nodes->status.name, "status", sizeof(nodes->status.name) - 1);
    nodes->status.flags = FS_FILE;
    nodes->status.mask = 0444;
    nodes->status.uid = target->uid;
    nodes->status.gid = target->gid;
    nodes->status.read = &proc_pid_status_read;
    procfs_refresh_timestamps(&nodes->status);

    strncpy(nodes->cmdline.name, "cmdline", sizeof(nodes->cmdline.name) - 1);
    nodes->cmdline.flags = FS_FILE;
    nodes->cmdline.mask = 0444;
    nodes->cmdline.uid = target->uid;
    nodes->cmdline.gid = target->gid;
    nodes->cmdline.read = &proc_pid_cmdline_read;
    procfs_refresh_timestamps(&nodes->cmdline);

    strncpy(nodes->stat.name, "stat", sizeof(nodes->stat.name) - 1);
    nodes->stat.flags = FS_FILE;
    nodes->stat.mask = 0444;
    nodes->stat.uid = target->uid;
    nodes->stat.gid = target->gid;
    nodes->stat.read = &proc_pid_stat_read;
    procfs_refresh_timestamps(&nodes->stat);

    strncpy(nodes->exe.name, "exe", sizeof(nodes->exe.name) - 1);
    nodes->exe.flags = FS_SYMLINK;
    nodes->exe.mask = 0777;
    nodes->exe.uid = target->uid;
    nodes->exe.gid = target->gid;
    nodes->exe.readlink = &proc_pid_exe_readlink;
    procfs_refresh_timestamps(&nodes->exe);

    strncpy(nodes->cwd.name, "cwd", sizeof(nodes->cwd.name) - 1);
    nodes->cwd.flags = FS_SYMLINK;
    nodes->cwd.mask = 0777;
    nodes->cwd.uid = target->uid;
    nodes->cwd.gid = target->gid;
    nodes->cwd.readlink = &proc_pid_cwd_readlink;
    procfs_refresh_timestamps(&nodes->cwd);

    strncpy(nodes->fd_dir.name, "fd", sizeof(nodes->fd_dir.name) - 1);
    nodes->fd_dir.flags = FS_DIRECTORY;
    nodes->fd_dir.mask = 0500;
    nodes->fd_dir.uid = target->uid;
    nodes->fd_dir.gid = target->gid;
    nodes->fd_dir.readdir = &proc_pid_fd_readdir;
    nodes->fd_dir.finddir = &proc_pid_fd_finddir;
    procfs_refresh_timestamps(&nodes->fd_dir);

    for (int fd = 0; fd < MAX_FD; fd++) {
        fs_node_t *link = &nodes->fd_links[fd];

        memset(link, 0, sizeof(*link));
        snprintf(link->name, sizeof(link->name), "%d", fd);
        link->flags = FS_SYMLINK;
        link->mask = 0700;
        link->uid = target->uid;
        link->gid = target->gid;
        link->inode = pid;
        link->impl = (uintptr_t)fd;
        link->readlink = &proc_pid_fd_readlink;
        procfs_refresh_timestamps(link);
    }

    strncpy(nodes->task_dir.name, "task", sizeof(nodes->task_dir.name) - 1);
    nodes->task_dir.flags = FS_DIRECTORY;
    nodes->task_dir.mask = 0555;
    nodes->task_dir.uid = target->uid;
    nodes->task_dir.gid = target->gid;
    nodes->task_dir.inode = pid;
    nodes->task_dir.readdir = &proc_pid_task_readdir;
    nodes->task_dir.finddir = &proc_pid_task_finddir;
    procfs_refresh_timestamps(&nodes->task_dir);

    spinlock_release(&procfs_pid_nodes_lock);
    return nodes;
}

static fs_node_t *procfs_get_self_node(void) {
    procfs_refresh_timestamps(&procfs_self_node);
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
            procfs_refresh_timestamps(&procfs_driver_nodes[i]);
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

static int proc_pid_fd_readlink(fs_node_t *node, char *buf, size_t size) {
    process_t *p = proc_find((int)node->inode);
    int fd = (int)node->impl;
    file_t *file;
    const char *target;

    if (!p || fd < 0 || fd >= MAX_FD) {
        return -1;
    }

    file = p->fds[fd];
    if (!file) {
        return -1;
    }

    target = file->f_path[0] ? file->f_path : NULL;
    if (!target) {
        if (file->f_type == DTYPE_PIPE) {
            target = "pipe:";
        } else if (file->f_type == DTYPE_SOCKET) {
            target = "socket:";
        } else {
            target = "unknown:";
        }
    }

    return proc_copy_process_link_target(p, target, buf, size);
}

/*
 * Release the procfs_pid_nodes_t for `pid` if one was lazily created
 * by procfs_get_pid_nodes().  Called from wait_reap() when the kernel
 * is about to free the process slot — without this, every fresh pid
 * permanently leaks ~10 KB of synthesised /proc/<pid> entries.
 */
void procfs_release_pid_nodes(int pid) {
    if (pid <= 0) return;

    if (!procfs_pid_nodes_lock_init) {
        return;
    }
    spinlock_acquire(&procfs_pid_nodes_lock);
    for (size_t i = 0; i < procfs_pid_nodes_count; i++) {
        procfs_pid_nodes_t *nodes = procfs_pid_nodes[i];
        if (nodes && nodes->pid == pid) {
            /* Compact the array by moving the last entry into this slot.
             * Order doesn't matter — procfs_get_pid_nodes does linear scan. */
            procfs_pid_nodes[i] = procfs_pid_nodes[procfs_pid_nodes_count - 1];
            procfs_pid_nodes[procfs_pid_nodes_count - 1] = NULL;
            procfs_pid_nodes_count--;
            spinlock_release(&procfs_pid_nodes_lock);
            kfree(nodes, sizeof(*nodes));
            return;
        }
    }
    spinlock_release(&procfs_pid_nodes_lock);
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

    /* Check for truncation and retry with larger buffer if needed.
     * Cap at 1 MiB so a buggy or hostile generator can't DoS the
     * kernel by claiming it needs hundreds of MB. */
    if (len >= sizeof(tmp)) {
        if (len > (1U << 20)) {
            len = sizeof(tmp) - 1;
        } else {
            alloc_size = len + 1;
            alloc_buf = kmalloc(alloc_size);
            if (alloc_buf) {
                len = entry->generator(alloc_buf, alloc_size, entry->opaque);
                buf = alloc_buf;
            } else {
                len = sizeof(tmp) - 1;
            }
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

    if (proc->perso_id == PERS_LINUX) {
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
    process_t *p = proc_find(pid);
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
    const char *entries[] = { ".", "..", "status", "cmdline", "stat", "exe", "cwd", "fd", "task", NULL };
    if (index >= 9) return NULL;
    strncpy(proc_dirent.d_name, entries[index], sizeof(proc_dirent.d_name) - 1);
    proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
    proc_dirent.d_ino = node->inode;
    return &proc_dirent;
}

static struct dirent *proc_pid_fd_readdir(fs_node_t *node, uint64_t index) {
    process_t *p = proc_find((int)node->inode);
    uint64_t fd_index;

    if (!p) {
        return NULL;
    }

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

    fd_index = index - 2;
    for (int fd = 0; fd < MAX_FD; fd++) {
        if (!p->fds[fd]) {
            continue;
        }
        if (fd_index == 0) {
            snprintf(proc_dirent.d_name, sizeof(proc_dirent.d_name), "%d", fd);
            proc_dirent.d_ino = node->inode;
            return &proc_dirent;
        }
        fd_index--;
    }

    return NULL;
}

static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name) {
    procfs_pid_nodes_t *nodes;

    if (strcmp(name, ".") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        procfs_refresh_timestamps(&procfs_root_node);
        return &procfs_root_node;
    }

    nodes = procfs_get_pid_nodes((int)node->inode);
    if (!nodes) return NULL;

    if (strcmp(name, "status") == 0) {
        nodes->status.inode = node->inode;
        procfs_refresh_timestamps(&nodes->status);
        return &nodes->status;
    }
    if (strcmp(name, "cmdline") == 0) {
        nodes->cmdline.inode = node->inode;
        procfs_refresh_timestamps(&nodes->cmdline);
        return &nodes->cmdline;
    }
    if (strcmp(name, "stat") == 0) {
        nodes->stat.inode = node->inode;
        procfs_refresh_timestamps(&nodes->stat);
        return &nodes->stat;
    }
    if (strcmp(name, "exe") == 0) {
        nodes->exe.inode = node->inode;
        procfs_refresh_timestamps(&nodes->exe);
        return &nodes->exe;
    }
    if (strcmp(name, "cwd") == 0) {
        nodes->cwd.inode = node->inode;
        procfs_refresh_timestamps(&nodes->cwd);
        return &nodes->cwd;
    }
    if (strcmp(name, "fd") == 0) {
        nodes->fd_dir.inode = node->inode;
        procfs_refresh_timestamps(&nodes->fd_dir);
        return &nodes->fd_dir;
    }
    if (strcmp(name, "task") == 0) {
        nodes->task_dir.inode = node->inode;
        procfs_refresh_timestamps(&nodes->task_dir);
        return &nodes->task_dir;
    }
    return NULL;
}

static fs_node_t *proc_pid_fd_finddir(fs_node_t *node, char *name) {
    procfs_pid_nodes_t *nodes;
    process_t *p;
    int fd = 0;
    char *scan;

    if (strcmp(name, ".") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        nodes = procfs_get_pid_nodes((int)node->inode);
        if (!nodes) {
            return NULL;
        }
        nodes->dir.inode = node->inode;
        procfs_refresh_timestamps(&nodes->dir);
        return &nodes->dir;
    }

    p = proc_find((int)node->inode);
    if (!p) {
        return NULL;
    }

    if (!name[0]) {
        return NULL;
    }
    scan = name;
    while (*scan >= '0' && *scan <= '9') {
        fd = (fd * 10) + (*scan - '0');
        if (fd >= MAX_FD) {
            return NULL;
        }
        scan++;
    }
    if (*scan != '\0' || !p->fds[fd]) {
        return NULL;
    }

    nodes = procfs_get_pid_nodes((int)node->inode);
    if (!nodes) {
        return NULL;
    }

    nodes->fd_links[fd].inode = node->inode;
    procfs_refresh_timestamps(&nodes->fd_links[fd]);
    return &nodes->fd_links[fd];
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
    FOREACH_PROC(proc) {
        if (proc->pid > 0) {
            if (count == proc_idx) {
                snprintf(proc_dirent.d_name, sizeof(proc_dirent.d_name), "%d", proc->pid);
                proc_dirent.d_ino = proc->pid;
                return &proc_dirent;
            }
            count++;
        }
    }
    return NULL;
}

static fs_node_t *procfs_finddir(fs_node_t *node, char *name) {
    if (strcmp(name, ".") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    
    /* Search static entries table */
    for (int i = 0; procfs_entries[i].name != NULL; i++) {
        if (strcmp(name, procfs_entries[i].name) == 0) {
            procfs_refresh_timestamps(&procfs_static_nodes[i]);
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
        procfs_pid_nodes_t *nodes = procfs_get_pid_nodes(pid);
        if (nodes) {
            nodes->dir.inode = pid;
            snprintf(nodes->dir.name, sizeof(nodes->dir.name), "%d", pid);
            procfs_refresh_timestamps(&nodes->dir);
            return &nodes->dir;
        }
    }
    return NULL;
}

/* Mount and initialization */

static fs_node_t *procfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    procfs_refresh_timestamps(&procfs_root_node);
    return &procfs_root_node;
}

/* ============================================================
 * /proc/<pid>/task/ — per-thread directory tree.
 *
 * Layout:
 *   /proc/<pid>/task/                  directory (one entry per thread)
 *   /proc/<pid>/task/<tid>/            directory (one per live thread)
 *   /proc/<pid>/task/<tid>/comm        thread name (writable counterpart NYI)
 *   /proc/<pid>/task/<tid>/status      multi-line key:value status
 *   /proc/<pid>/task/<tid>/stat        Linux-style /proc/<pid>/stat line
 *
 * Thread identity is encoded into the static per-tid nodes as
 *   inode = pid    (so proc_find still works)
 *   impl  = tid    (consumed by the read callbacks).
 *
 * Like other procfs subtrees, lookups use shared static node
 * scratch space; concurrent readdir/finddir from multiple threads
 * already share proc_dirent globally, so no new race is introduced.
 * ============================================================ */

static fs_node_t proc_task_tid_dir_node;
static fs_node_t proc_task_tid_comm_node;
static fs_node_t proc_task_tid_status_node;
static fs_node_t proc_task_tid_stat_node;

struct procfs_task_index {
    int       pid;
    uint64_t  want_index;
    int       found_tid;
    uint64_t  cur_index;
};

static void procfs_task_index_visit(thread_t *t, void *arg) {
    struct procfs_task_index *it = arg;
    if (!t || t->tid == -1 || !t->proc || t->proc->pid != it->pid) return;
    if (it->found_tid != -1) return;
    if (it->cur_index == it->want_index) it->found_tid = t->tid;
    it->cur_index++;
}

struct procfs_tid_lookup {
    int pid;
    int want_tid;
    int found;
    char name[16];
    int state;
    int priority;
    uint32_t sig_pending;
    uint32_t sig_mask;
    int bound_cpu;
    uint32_t flags;
};

static void procfs_tid_lookup_visit(thread_t *t, void *arg) {
    struct procfs_tid_lookup *l = arg;
    if (!t || t->tid == -1 || !t->proc) return;
    if (t->proc->pid != l->pid || t->tid != l->want_tid) return;
    l->found = 1;
    /* Copy fields while under sched_iterate_threads' implicit guard. */
    size_t i = 0;
    while (i < sizeof(l->name) - 1 && t->name[i]) { l->name[i] = t->name[i]; i++; }
    l->name[i] = '\0';
    l->state       = (int)t->state;
    l->priority    = t->priority;
    l->sig_pending = t->sig_pending;
    l->sig_mask    = t->sig_mask;
    l->bound_cpu   = t->bound_cpu;
    l->flags       = t->flags;
}

/* Linux-style single-character thread state. */
static char procfs_thread_state_char(int state) {
    switch (state) {
        case THREAD_READY:   return 'R';
        case THREAD_RUNNING: return 'R';
        case THREAD_BLOCKED: return 'S';
        case THREAD_ZOMBIE:  return 'Z';
        case THREAD_STOPPED: return 'T';
        default:             return 'R';
    }
}

static struct dirent *proc_pid_task_readdir(fs_node_t *node, uint64_t index) {
    int pid = (int)node->inode;
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
    struct procfs_task_index it = {
        .pid = pid, .want_index = index - 2, .found_tid = -1, .cur_index = 0
    };
    sched_iterate_threads(procfs_task_index_visit, &it);
    if (it.found_tid == -1) return NULL;
    snprintf(proc_dirent.d_name, sizeof(proc_dirent.d_name), "%d", it.found_tid);
    proc_dirent.d_ino = node->inode;
    return &proc_dirent;
}

static fs_node_t *proc_pid_task_finddir(fs_node_t *node, char *name) {
    int pid = (int)node->inode;

    if (strcmp(name, ".") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        procfs_pid_nodes_t *nodes = procfs_get_pid_nodes(pid);
        if (!nodes) return NULL;
        procfs_refresh_timestamps(&nodes->dir);
        return &nodes->dir;
    }

    /* Parse name as decimal TID. */
    if (!name[0]) return NULL;
    int tid = 0;
    for (char *p = name; *p; p++) {
        if (*p < '0' || *p > '9') return NULL;
        tid = tid * 10 + (*p - '0');
        if (tid > SUBSTRATE_TID_MAX) return NULL;
    }

    struct procfs_tid_lookup l = { .pid = pid, .want_tid = tid, .found = 0 };
    sched_iterate_threads(procfs_tid_lookup_visit, &l);
    if (!l.found) return NULL;

    process_t *p = proc_find(pid);
    memset(&proc_task_tid_dir_node, 0, sizeof(proc_task_tid_dir_node));
    snprintf(proc_task_tid_dir_node.name, sizeof(proc_task_tid_dir_node.name), "%d", tid);
    proc_task_tid_dir_node.flags = FS_DIRECTORY;
    proc_task_tid_dir_node.mask = 0555;
    if (p) {
        proc_task_tid_dir_node.uid = p->uid;
        proc_task_tid_dir_node.gid = p->gid;
    }
    proc_task_tid_dir_node.inode = pid;
    proc_task_tid_dir_node.impl  = (uintptr_t)tid;
    proc_task_tid_dir_node.readdir = &proc_pid_task_tid_readdir;
    proc_task_tid_dir_node.finddir = &proc_pid_task_tid_finddir;
    procfs_refresh_timestamps(&proc_task_tid_dir_node);
    return &proc_task_tid_dir_node;
}

static struct dirent *proc_pid_task_tid_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    const char *entries[] = { ".", "..", "comm", "status", "stat", NULL };
    if (index >= 5) return NULL;
    strncpy(proc_dirent.d_name, entries[index], sizeof(proc_dirent.d_name) - 1);
    proc_dirent.d_name[sizeof(proc_dirent.d_name) - 1] = '\0';
    proc_dirent.d_ino = node->inode;
    return &proc_dirent;
}

static fs_node_t *proc_pid_task_tid_finddir(fs_node_t *node, char *name) {
    if (strcmp(name, ".") == 0) {
        procfs_refresh_timestamps(node);
        return node;
    }
    if (strcmp(name, "..") == 0) {
        procfs_pid_nodes_t *nodes = procfs_get_pid_nodes((int)node->inode);
        if (!nodes) return NULL;
        procfs_refresh_timestamps(&nodes->task_dir);
        return &nodes->task_dir;
    }

    fs_node_t *out = NULL;
    if (strcmp(name, "comm") == 0) {
        out = &proc_task_tid_comm_node;
        memset(out, 0, sizeof(*out));
        strncpy(out->name, "comm", sizeof(out->name) - 1);
        out->read = &proc_pid_task_comm_read;
    } else if (strcmp(name, "status") == 0) {
        out = &proc_task_tid_status_node;
        memset(out, 0, sizeof(*out));
        strncpy(out->name, "status", sizeof(out->name) - 1);
        out->read = &proc_pid_task_status_read;
    } else if (strcmp(name, "stat") == 0) {
        out = &proc_task_tid_stat_node;
        memset(out, 0, sizeof(*out));
        strncpy(out->name, "stat", sizeof(out->name) - 1);
        out->read = &proc_pid_task_stat_read;
    } else {
        return NULL;
    }

    out->flags = FS_FILE;
    out->mask  = 0444;
    out->inode = node->inode;
    out->impl  = node->impl;
    procfs_refresh_timestamps(out);
    return out;
}

/* Common helper: collect a thread snapshot and return -1 if not found. */
static int procfs_task_collect(fs_node_t *node, struct procfs_tid_lookup *l) {
    l->pid      = (int)node->inode;
    l->want_tid = (int)node->impl;
    l->found    = 0;
    sched_iterate_threads(procfs_tid_lookup_visit, l);
    return l->found ? 0 : -1;
}

/* Bounded slice-from-buffer copy used by all the per-tid file readers. */
static size_t procfs_emit_slice(const char *src, int len, off_t offset,
                                size_t size, uint8_t *buffer) {
    if (len < 0) return 0;
    if (offset >= (off_t)len) return 0;
    size_t n = size;
    if ((off_t)(offset + n) > (off_t)len) n = (size_t)(len - offset);
    memcpy(buffer, src + offset, n);
    return n;
}

static size_t proc_pid_task_comm_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    struct procfs_tid_lookup l;
    if (procfs_task_collect(node, &l) < 0) return 0;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%s\n", l.name);
    return procfs_emit_slice(buf, len, offset, size, buffer);
}

static size_t proc_pid_task_status_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    struct procfs_tid_lookup l;
    if (procfs_task_collect(node, &l) < 0) return 0;
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
        "Name:\t%s\n"
        "Tgid:\t%d\n"
        "Pid:\t%d\n"
        "State:\t%c\n"
        "Prio:\t%d\n"
        "SigPnd:\t%08x\n"
        "SigBlk:\t%08x\n"
        "Cpus_allowed:\t%08x\n"
        "BoundCpu:\t%d\n"
        "Flags:\t%08x\n",
        l.name, l.pid, l.want_tid,
        procfs_thread_state_char(l.state),
        l.priority, l.sig_pending, l.sig_mask,
        0u, l.bound_cpu, l.flags);
    return procfs_emit_slice(buf, len, offset, size, buffer);
}

static size_t proc_pid_task_stat_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    struct procfs_tid_lookup l;
    if (procfs_task_collect(node, &l) < 0) return 0;
    char name_safe[17];
    size_t i = 0;
    while (i < sizeof(name_safe) - 1 && l.name[i]) {
        unsigned char c = (unsigned char)l.name[i];
        name_safe[i] = (c < 32 || c > 126 || c == '(' || c == ')') ? '_' : (char)c;
        i++;
    }
    name_safe[i] = '\0';
    char buf[256];
    /* Minimal Linux-style line: tid (comm) state ppid pgrp session tty tpgid
     * + zero-padded fields so ps doesn't run past end-of-string. */
    int len = snprintf(buf, sizeof(buf),
        "%d (%s) %c %d 0 0 0 -1 "
        "0 0 0 0 0 "
        "0 0 0 0 %d 0 1 0 0 0 0 "
        "0 0 0 0 0 0 0 0 0 0 "
        "0 0 0 0 0 0 0 0 0 0\n",
        l.want_tid, name_safe,
        procfs_thread_state_char(l.state),
        l.pid, l.priority);
    return procfs_emit_slice(buf, len, offset, size, buffer);
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
        procfs_refresh_timestamps(&procfs_static_nodes[i]);
    }

    for (size_t i = 0; i < PROCFS_MAX_DRIVER_ENTRIES; i++) {
        memset(&procfs_driver_nodes[i], 0, sizeof(fs_node_t));
        procfs_driver_nodes[i].flags = FS_FILE;
        procfs_driver_nodes[i].mask = 0444;
        procfs_driver_nodes[i].uid = 0;
        procfs_driver_nodes[i].gid = 0;
        procfs_driver_nodes[i].read = &procfs_generic_read;
        procfs_refresh_timestamps(&procfs_driver_nodes[i]);
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
    procfs_refresh_timestamps(&procfs_self_node);

    memset(&procfs_root_node, 0, sizeof(fs_node_t));
    strncpy(procfs_root_node.name, "proc", sizeof(procfs_root_node.name) - 1);
    procfs_root_node.name[sizeof(procfs_root_node.name) - 1] = '\0';
    procfs_root_node.flags = FS_DIRECTORY;
    procfs_root_node.mask = 0555;
    procfs_root_node.uid = 0;
    procfs_root_node.gid = 0;
    procfs_root_node.readdir = &procfs_readdir;
    procfs_root_node.finddir = &procfs_finddir;
    procfs_refresh_timestamps(&procfs_root_node);

    vfs_register_filesystem(&procfs_fs);
}

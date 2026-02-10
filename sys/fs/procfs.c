/*
 * procfs.c - Process Filesystem Implementation
 *
 * Table-driven design for extensibility. Each static entry is defined
 * in procfs_entries[]. Per-process directories are handled separately.
 */

#include <vfs/vfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

/* External declarations */
extern uint32_t get_time(void);
extern void cmdline_get(char *buf, size_t buf_len);
extern uint32_t pmm_get_total_memory(void);    /* from PMM */
extern uint32_t pmm_get_free_memory(void);     /* from PMM */
extern filesystem_t *vfs_get_filesystems(void); /* from VFS */

/* Forward declarations */
static size_t procfs_generic_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *procfs_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *procfs_finddir(fs_node_t *node, char *name);

/*
 * ProcFS Entry Structure
 * Each static /proc entry is defined by a generator function that fills
 * a buffer with the file contents.
 */
typedef uint32_t (*procfs_gen_t)(char *buf, size_t size);

struct procfs_entry {
    const char *name;
    procfs_gen_t generator;
};

/* Generator functions for each /proc entry */

static uint32_t gen_cpuinfo(char *buf, size_t size __attribute__((unused))) {
    return sprintf(buf,
        "processor\t: 0\n"
        "vendor_id\t: GenuineIntel\n"
        "model name\t: Substrate Virtual CPU\n"
        "cpu MHz\t\t: 1000.000\n"
        "cache size\t: 256 KB\n"
        "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic\n");
}

static uint32_t gen_meminfo(char *buf, size_t size __attribute__((unused))) {
    /* Get real values from PMM when available */
    uint32_t total_kb = pmm_get_total_memory() / 1024;
    uint32_t free_kb = pmm_get_free_memory() / 1024;
    uint32_t used_kb = total_kb - free_kb;
    
    return sprintf(buf,
        "MemTotal:    %8u kB\n"
        "MemFree:     %8u kB\n"
        "MemUsed:     %8u kB\n"
        "Buffers:            0 kB\n"
        "Cached:             0 kB\n"
        "SwapTotal:          0 kB\n"
        "SwapFree:           0 kB\n",
        total_kb, free_kb, used_kb);
}

static uint32_t gen_uptime(char *buf, size_t size __attribute__((unused))) {
    uint32_t t = get_time();
    return sprintf(buf, "%u.00 0.00\n", t);
}

static uint32_t gen_cmdline(char *buf, size_t size) {
    cmdline_get(buf, size);
    size_t len = strlen(buf);
    if (len < size - 1) {
        buf[len] = '\n';
        buf[len + 1] = '\0';
        len++;
    }
    return len;
}

static uint32_t gen_version(char *buf, size_t size __attribute__((unused))) {
    return sprintf(buf, 
        "Substrate version 0.1.0 (gcc) #1 SMP PREEMPT %s\n",
        __DATE__);
}


static uint32_t gen_loadavg(char *buf, size_t size __attribute__((unused))) {
    unsigned long loads[3];
    sched_get_loadavg(loads);

    uint32_t runnable = sched_count_runnable();
    uint32_t total = sched_count_threads();
    int last_pid = proc_get_last_pid();

    return sprintf(buf,
        "%lu.%02lu %lu.%02lu %lu.%02lu %u/%u %d\n",
        LOAD_INT(loads[0]), LOAD_FRAC(loads[0]),
        LOAD_INT(loads[1]), LOAD_FRAC(loads[1]),
        LOAD_INT(loads[2]), LOAD_FRAC(loads[2]),
        runnable, total, last_pid);
}

static uint32_t proc_pmap_stats_read(char *buf, size_t size) {
    (void)size;
    struct pmap_stats stats;
    // sys_pmap_stats is declared in pmap.h
    if (sys_pmap_stats(&stats) != 0) {
        return sprintf(buf, "error: could not get stats\n"); 
    }
    
    return sprintf(buf, 
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
        "Total PMAPs: %u\n",
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
        stats.total_pmaps
    );
}

static uint32_t gen_filesystems(char *buf, size_t size __attribute__((unused))) {
    /* Dynamically generate from VFS registry */
    int off = 0;
    filesystem_t *fs = vfs_get_filesystems();
    
    while (fs && off < (int)size - 32) {
        /* Check if this is a pseudo-filesystem (no device required) */
        int nodev = (strcmp(fs->name, "procfs") == 0 ||
                    strcmp(fs->name, "devfs") == 0 ||
                    strcmp(fs->name, "sysfs") == 0 ||
                    strcmp(fs->name, "tmpfs") == 0);
        
        if (nodev) {
            off += sprintf(buf + off, "nodev\t%s\n", fs->name);
        } else {
            off += sprintf(buf + off, "\t%s\n", fs->name);
        }
        fs = fs->next;
    }
    return off;
}

/*
 * Entry table - Static /proc entries
 * Add new entries here for automatic registration.
 */
static struct procfs_entry procfs_entries[] = {
    { "cpuinfo",     gen_cpuinfo },
    { "meminfo",     gen_meminfo },
    { "uptime",      gen_uptime },
    { "cmdline",     gen_cmdline },
    { "version",     gen_version },
    { "loadavg",     gen_loadavg },
    { "pmap_stats",  proc_pmap_stats_read },
    { "filesystems", gen_filesystems },
    { NULL, NULL }  /* Sentinel */
};

#define PROCFS_STATIC_COUNT (sizeof(procfs_entries) / sizeof(procfs_entries[0]) - 1)

/*
 * Generic read function for table-driven entries
 * The entry pointer is stored in node->impl
 */
static size_t procfs_generic_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    struct procfs_entry *entry = (struct procfs_entry *)(uintptr_t)node->impl;
    if (!entry || !entry->generator) return 0;
    
    char tmp[1024];
    uint32_t len = entry->generator(tmp, sizeof(tmp));
    
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    
    memcpy(buffer, tmp + offset, size);
    return size;
}

/* Per-process directory support */
static struct dirent proc_dirent;

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

    char buf[512];
    int len;
    
    struct personality *pers = perso_lookup(current_process->perso_id);
    if (pers && pers->name && strcmp(pers->name, "Linux") == 0) {
        len = sprintf(buf,
            "Name:\t%s\n"
            "State:\tR (running)\n"
            "Tgid:\t%d\n"
            "Pid:\t%d\n"
            "Uid:\t%d\t%d\t%d\t%d\n"
            "Gid:\t%d\t%d\t%d\t%d\n",
            p->comm, p->pid, p->pid,
            p->uid, p->uid, p->uid, p->uid,
            p->gid, p->gid, p->gid, p->gid);
    } else {
        len = sprintf(buf,
            "Name:\t%s\n"
            "Pid:\t%d\n"
            "Uid:\t%d\n"
            "Gid:\t%d\n"
            "State:\tRunning\n",
            p->comm, p->pid, p->uid, p->gid);
    }
    
    if (offset >= (uint32_t)len) return 0;
    if (offset + size > (uint32_t)len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

static size_t proc_pid_cmdline_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    int pid = node->inode;
    process_t *p = proc_find(pid);
    if (!p) return 0;
    
    /* Return process command name (comm) as cmdline */
    uint32_t len = strlen(p->comm);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, p->comm + offset, size);
    return size;
}

static struct dirent *proc_pid_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    const char *entries[] = { ".", "..", "status", "cmdline", NULL };
    if (index >= 4) return NULL;
    strcpy(proc_dirent.name, entries[index]);
    return &proc_dirent;
}

static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name) {
    static fs_node_t pid_file;
    
    memset(&pid_file, 0, sizeof(fs_node_t));
    pid_file.inode = node->inode;
    pid_file.flags = FS_FILE;
    
    if (strcmp(name, "status") == 0) {
        strcpy(pid_file.name, "status");
        pid_file.read = &proc_pid_status_read;
        return &pid_file;
    }
    if (strcmp(name, "cmdline") == 0) {
        strcpy(pid_file.name, "cmdline");
        pid_file.read = &proc_pid_cmdline_read;
        return &pid_file;
    }
    return NULL;
}

/* Root /proc directory operations */

static struct dirent *procfs_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    
    /* . and .. */
    if (index == 0) { strcpy(proc_dirent.name, "."); return &proc_dirent; }
    if (index == 1) { strcpy(proc_dirent.name, ".."); return &proc_dirent; }
    
    /* Static entries from table */
    uint64_t static_idx = index - 2;
    if (static_idx < PROCFS_STATIC_COUNT) {
        strcpy(proc_dirent.name, procfs_entries[static_idx].name);
        return &proc_dirent;
    }
    
    /* Process directories */
    uint64_t proc_idx = static_idx - PROCFS_STATIC_COUNT;
    uint32_t count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid != -1) {
            if (count == proc_idx) {
                sprintf(proc_dirent.name, "%d", processes[i].pid);
                return &proc_dirent;
            }
            count++;
        }
    }
    return NULL;
}

static fs_node_t *procfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    static fs_node_t entry_node;
    
    /* Search static entries table */
    for (int i = 0; procfs_entries[i].name != NULL; i++) {
        if (strcmp(name, procfs_entries[i].name) == 0) {
            memset(&entry_node, 0, sizeof(fs_node_t));
            strcpy(entry_node.name, procfs_entries[i].name);
            entry_node.flags = FS_FILE;
            entry_node.impl = (uintptr_t)&procfs_entries[i];
            entry_node.read = &procfs_generic_read;
            return &entry_node;
        }
    }

    /* Parse numeric PID */
    int pid = 0;
    char *p = name;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }

    if (pid > 0 && *p == '\0') {
        for (int i = 0; i < MAX_PROCS; i++) {
            if (processes[i].pid == pid) {
                static fs_node_t pid_dir;
                memset(&pid_dir, 0, sizeof(fs_node_t));
                sprintf(pid_dir.name, "%d", pid);
                pid_dir.flags = FS_DIRECTORY;
                pid_dir.inode = pid;
                pid_dir.readdir = &proc_pid_readdir;
                pid_dir.finddir = &proc_pid_finddir;
                return &pid_dir;
            }
        }
    }
    return NULL;
}

/* Mount and initialization */

static fs_node_t procfs_root_node;

static fs_node_t *procfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    return &procfs_root_node;
}

static filesystem_t procfs_fs = {
    .name = "procfs",
    .mount = &procfs_mount,
};

void procfs_init(void) {
    memset(&procfs_root_node, 0, sizeof(fs_node_t));
    strcpy(procfs_root_node.name, "proc");
    procfs_root_node.flags = FS_DIRECTORY;
    procfs_root_node.readdir = &procfs_readdir;
    procfs_root_node.finddir = &procfs_finddir;

    vfs_register_filesystem(&procfs_fs);
}

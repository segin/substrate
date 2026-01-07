#include "../vfs/vfs.h"
#include "../../include/sys/proc.h"
#include "../../pm/pm.h"
#include "../exec/perso/personality.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

// processes[] is declared in pm.h

static struct dirent proc_dirent;

// Node for /proc/<pid>/status
static uint32_t proc_status_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
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
    if (current_process->pers && strcmp(current_process->pers->name, "Linux") == 0) {
        sprintf(buf, "Name:\t%s\nState:\tR (running)\nTgid:\t%d\nPid:\t%d\nUid:\t%d\t%d\t%d\t%d\nGid:\t%d\t%d\t%d\t%d\n", 
                p->comm, p->pid, p->pid, p->uid, p->uid, p->uid, p->uid, p->gid, p->gid, p->gid, p->gid);
    } else if (current_process->pers && strcmp(current_process->pers->name, "FreeBSD") == 0) {
        // FreeBSD uses a different format, but for now just label it
        sprintf(buf, "Name: %s\nPid: %d\nABI: FreeBSD\n", p->comm, p->pid);
    } else {
        sprintf(buf, "Name:\t%s\nPid:\t%d\nUid:\t%d\nGid:\t%d\nState:\tRunning\n", 
                p->comm, p->pid, p->uid, p->gid);
    }
    
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/cpuinfo
static uint32_t proc_cpuinfo_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    char buf[256];
    sprintf(buf, "Processor:\t0\nVendor:\t\tGenericx86\nModel Name:\tTestUnix Virtual CPU\n");
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/meminfo
static uint32_t proc_meminfo_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    // Mock values
    char buf[256];
    sprintf(buf, "MemTotal:\t65536 kB\nMemFree:\t32768 kB\n");
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/uptime
extern uint32_t get_time(void);
static uint32_t proc_uptime_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    char buf[64];
    sprintf(buf, "%d.00\n", get_time());
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/cow_stats
#include "../../arch/i386/pmap.h"
extern int sys_get_cow_stats(struct pmap_stats *out);

static uint32_t proc_cow_stats_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    struct pmap_stats stats;
    if (sys_get_cow_stats(&stats) != 0) return 0;
    
    char buf[512];
    sprintf(buf, "Faults:\t%u\nCOW Faults:\t%u\nZero Fills:\t%u\nProt Upgrades:\t%u\nProt Downgrades:\t%u\nCOW Pages Mapped:\t%u\n",
            stats.faults, stats.cow_faults, stats.zero_fills, 
            stats.protection_upgrades, stats.protection_downgrades,
            stats.cow_pages_mapped);
            
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/<pid>/
static struct dirent *proc_pid_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index == 0) { strcpy(proc_dirent.name, "."); return &proc_dirent; }
    if (index == 1) { strcpy(proc_dirent.name, ".."); return &proc_dirent; }
    if (index == 2) { strcpy(proc_dirent.name, "status"); return &proc_dirent; }
    if (index == 3) { strcpy(proc_dirent.name, "cmdline"); return &proc_dirent; }
    return NULL;
}

static fs_node_t *proc_pid_finddir(fs_node_t *node, char *name) {
    if (strcmp(name, "status") == 0) {
        static fs_node_t status_node;
        memset(&status_node, 0, sizeof(fs_node_t));
        strcpy(status_node.name, "status");
        status_node.flags = FS_FILE;
        status_node.inode = node->inode; // Pass PID
        status_node.read = &proc_status_read;
        return &status_node;
    }
    return NULL;
}

// Root /proc readdir
static struct dirent *procfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index == 0) { strcpy(proc_dirent.name, "."); return &proc_dirent; }
    if (index == 1) { strcpy(proc_dirent.name, ".."); return &proc_dirent; }
    if (index == 2) { strcpy(proc_dirent.name, "cpuinfo"); return &proc_dirent; }
    if (index == 3) { strcpy(proc_dirent.name, "meminfo"); return &proc_dirent; }
    if (index == 4) { strcpy(proc_dirent.name, "uptime"); return &proc_dirent; }
    if (index == 5) { strcpy(proc_dirent.name, "cow_stats"); return &proc_dirent; }

    int count = 6;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid != -1) {
            if (count == (int)index) {
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

    if (strcmp(name, "cpuinfo") == 0) {
        static fs_node_t cpu_node;
        memset(&cpu_node, 0, sizeof(fs_node_t));
        strcpy(cpu_node.name, "cpuinfo");
        cpu_node.flags = FS_FILE;
        cpu_node.read = &proc_cpuinfo_read;
        return &cpu_node;
    }
    if (strcmp(name, "meminfo") == 0) {
        static fs_node_t mem_node;
        memset(&mem_node, 0, sizeof(fs_node_t));
        strcpy(mem_node.name, "meminfo");
        mem_node.flags = FS_FILE;
        mem_node.read = &proc_meminfo_read;
        return &mem_node;
    }
    if (strcmp(name, "uptime") == 0) {
        static fs_node_t up_node;
        memset(&up_node, 0, sizeof(fs_node_t));
        strcpy(up_node.name, "uptime");
        up_node.flags = FS_FILE;
        up_node.read = &proc_uptime_read;
        return &up_node;
    }
    if (strcmp(name, "cow_stats") == 0) {
        static fs_node_t cow_node;
        memset(&cow_node, 0, sizeof(fs_node_t));
        strcpy(cow_node.name, "cow_stats");
        cow_node.flags = FS_FILE;
        cow_node.read = &proc_cow_stats_read;
        return &cow_node;
    }

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
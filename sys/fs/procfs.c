#include "../vfs/vfs.h"
#include "../sys/proc.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

extern process_t processes[];

static struct dirent proc_dirent;

// Node for /proc/<pid>/status
static uint32_t proc_status_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    int pid = node->inode;
    process_t *p = NULL;
    for (int i = 0; i < 16; i++) {
        if (processes[i].pid == pid) {
            p = &processes[i];
            break;
        }
    }
    if (!p) return 0;

    char buf[256];
    sprintf(buf, "Name:\t%s\nPid:\t%d\nUid:\t%d\nGid:\t%d\nState:\tRunning\n", 
            p->comm, p->pid, p->uid, p->gid);
    
    uint32_t len = strlen(buf);
    if (offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buffer, buf + offset, size);
    return size;
}

// Node for /proc/<pid>/
static struct dirent *proc_pid_readdir(fs_node_t *node, uint32_t index) {
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

    int count = 2;
    for (int i = 0; i < 16; i++) {
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
    int pid = 0;
    char *p = name;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }

    if (pid > 0 && *p == '\0') {
        for (int i = 0; i < 16; i++) {
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
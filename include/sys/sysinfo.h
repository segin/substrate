#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H

#include <stdint.h>
#include <sys/types.h>

// Process Bitness
typedef enum {
    BITNESS_16 = 16,
    BITNESS_32 = 32,
    BITNESS_64 = 64
} proc_bitness_t;

// Process Info Structure
// Used by sys_proc_info syscall
typedef struct sys_procinfo {
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t sid;
    uid_t uid;
    gid_t gid;
    uint8_t state;
    uint8_t bitness; // proc_bitness_t
    char name[32];   // Process name (comm)
    
    // Time accounting
    uint32_t start_time;
    uint32_t user_time;
    uint32_t sys_time;
    
    // Memory usage
    uint32_t vsize;  // Virtual memory size
    uint32_t rss;    // Resident Set Size (pages)
    
} sys_procinfo_t;

typedef struct sys_fd {
    int fd;
    char path[256];
    uint32_t flags;
} sys_fd_t;

typedef struct sys_map {
    uintptr_t start;
    uintptr_t end;
    uint32_t flags;
    char name[256];
} sys_map_t;

struct sysinfo {
    long uptime;             /* Seconds since boot */
    unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram;  /* Total usable main memory size */
    unsigned long freeram;   /* Available memory size */
    unsigned long sharedram; /* Amount of shared memory */
    unsigned long bufferram; /* Memory used by buffers */
    unsigned long totalswap; /* Total swap space size */
    unsigned long freeswap;  /* swap space still available */
    unsigned short procs;    /* Number of current processes */
    unsigned short pad;      /* Explicit padding for 32-bit alignment */
    unsigned long totalhigh; /* Total high memory size */
    unsigned long freehigh;  /* Available high memory size */
    unsigned int mem_unit;   /* Memory unit size in bytes */
    char _f[20-2*sizeof(long)-sizeof(int)]; /* Padding to 64 bytes */
};

#ifndef _KERNEL
int sysinfo(struct sysinfo *info);
int sys_proc_count(void);
int sys_proc_list(pid_t *pids, size_t count);
int sys_proc_info(pid_t pid, sys_procinfo_t *info);
int sys_proc_threads(pid_t pid, tid_t *tids, size_t *count);
int sys_proc_fds(pid_t pid, sys_fd_t *fds, size_t *count);
int sys_proc_maps(pid_t pid, sys_map_t *maps, size_t *count);
int sys_proc_cwd(pid_t pid, char *buf, size_t len);
int sys_proc_exe(pid_t pid, char *buf, size_t len);
int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc);
int sys_proc_environ(pid_t pid, char **envp, size_t *envc);
#endif

#endif

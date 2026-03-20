#ifndef _SUBSTRATE_SYS_SYSINFO_H
#define _SUBSTRATE_SYS_SYSINFO_H

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
    pid_t pgid;          // Process group ID
    pid_t sid;           // Session ID
    uid_t uid;
    gid_t gid;
    uid_t euid;          // Effective user ID
    gid_t egid;          // Effective group ID
    uint8_t state;       // 1=IDL, 2=RUN, 3=SLP, 4=STP, 5=ZOM, 6=DIE
    uint8_t bitness;     // proc_bitness_t
    int16_t perso_id;    // Personality ID (PERS_NATIVE, PERS_LINUX, etc.)
    int16_t tty;         // Controlling terminal device (-1 if none)
    uint16_t nice;       // Nice value
    char name[32];       // Process name (comm)

    // Time accounting
    uint32_t start_time; // Process start time (seconds since boot)
    uint32_t user_time;  // User mode CPU time (jiffies)
    uint32_t sys_time;   // System mode CPU time (jiffies)

    // Memory usage
    uint32_t vsize;      // Virtual memory size (bytes)
    uint32_t rss;        // Resident Set Size (pages)

} sys_procinfo_t;

typedef struct sys_fd {
    int fd;
    char path[256];
    uint32_t flags;
} sys_fd_t;

typedef struct sys_map {
    uint32_t start;
    uint32_t end;
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

/* VM Statistics Structure */
typedef struct sys_vmstat {
    uint64_t total;        /* Total physical memory */
    uint64_t free;         /* Free memory */
    uint64_t available;    /* Available memory (includes reclaimable) */
    uint64_t buffers;      /* Buffer cache */
    uint64_t cached;       /* Page cache */
    uint64_t swap_total;   /* Total swap */
    uint64_t swap_free;    /* Free swap */
    uint64_t swap_cached;  /* Cached swap */
} sys_vmstat_t;

#ifndef _KERNEL
int sysinfo(struct sysinfo *info);

/* Process Information API */
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
int sys_proc_pers_name(int perso_id, char *buf, size_t len);

/* VM Statistics API */
int sys_vm_stats(sys_vmstat_t *stats);
int sys_cpu_count(void);
#endif

#endif

#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

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
    uid_t euid;          // Effective user ID
    gid_t egid;          // Effective group ID
    uint8_t state;       // R=Running, S=Sleeping, Z=Zombie, T=Stopped
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

/* VM Zone Info */
typedef struct sys_vminfo {
    uint64_t dma_total;
    uint64_t dma_free;
    uint64_t normal_total;
    uint64_t normal_free;
    uint64_t highmem_total;
    uint64_t highmem_free;
} sys_vminfo_t;

/* Swap Info */
typedef struct sys_swapinfo {
    char path[256];
    uint64_t total;
    uint64_t used;
    int priority;
} sys_swapinfo_t;

/* Buffer Cache Info */
typedef struct sys_bufinfo {
    uint64_t nr_buffers;
    uint64_t buffer_size;
    uint64_t dirty;
    uint64_t writeback;
} sys_bufinfo_t;

/* Slab Info */
typedef struct sys_slabinfo {
    char name[32];
    uint32_t active;
    uint32_t total;
    uint32_t objsize;
    uint32_t slabs;
    uint32_t pages_per_slab;
} sys_slabinfo_t;

/* CPU Info Structure */
typedef struct sys_cpuinfo {
    char vendor[16];       /* e.g., "GenuineIntel", "AuthenticAMD" */
    char model[64];        /* e.g., "Intel Core i7-9700K" */
    uint32_t family;
    uint32_t model_id;
    uint32_t stepping;
    uint32_t mhz;          /* Clock speed in MHz */
    uint32_t cache_l1d;    /* L1 data cache (KB) */
    uint32_t cache_l1i;    /* L1 instruction cache (KB) */
    uint32_t cache_l2;     /* L2 cache (KB) */
    uint32_t cache_l3;     /* L3 cache (KB) */
    uint32_t flags;        /* CPU feature flags */
} sys_cpuinfo_t;

/* CPU Times Structure */
typedef struct sys_cputimes {
    uint64_t user;         /* User mode jiffies */
    uint64_t nice;         /* Nice mode jiffies */
    uint64_t system;       /* Kernel mode jiffies */
    uint64_t idle;         /* Idle jiffies */
    uint64_t iowait;       /* I/O wait jiffies */
    uint64_t irq;          /* Hardware interrupt jiffies */
    uint64_t softirq;      /* Software interrupt jiffies */
} sys_cputimes_t;

/* Kernel Version Structure */
typedef struct sys_version {
    int major;
    int minor;
    int patch;
    char release[64];      /* e.g., "0.1.0-dev" */
    char version[128];     /* Full version string */
} sys_version_t;

/* Network Interface Structure */
typedef struct sys_netif {
    char name[16];         /* e.g., "em0", "lo0" */
    int index;             /* Interface index */
    uint32_t flags;        /* IFF_UP, IFF_RUNNING, etc. */
    uint32_t mtu;          /* Maximum transmission unit */
    uint8_t hwaddr[6];     /* Hardware (MAC) address */
    uint16_t type;         /* Interface type (Ethernet, loopback, etc.) */
} sys_netif_t;

/* Network Address Structure */
typedef struct sys_netaddr {
    int family;            /* AF_INET, AF_INET6 */
    uint8_t addr[16];      /* Address bytes (4 for IPv4, 16 for IPv6) */
    uint8_t mask[16];      /* Netmask */
    uint8_t bcast[16];     /* Broadcast (IPv4 only) */
} sys_netaddr_t;

/* Network Statistics Structure */
typedef struct sys_netstats {
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t tx_bytes;
    uint64_t tx_packets;
    uint64_t tx_errors;
    uint64_t tx_dropped;
} sys_netstats_t;

/* Routing Entry Structure */
typedef struct sys_route {
    uint8_t dest[16];      /* Destination address */
    uint8_t gateway[16];   /* Gateway address */
    uint8_t mask[16];      /* Netmask */
    int family;            /* AF_INET, AF_INET6 */
    uint32_t metric;
    uint32_t flags;
    char ifname[16];
} sys_route_t;

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

/* Memory Statistics API */
int sys_vm_stats(sys_vmstat_t *stats);
int sys_vm_info(sys_vminfo_t *info);
int sys_vm_swap(sys_swapinfo_t *swap, size_t *count);
int sys_vm_buffers(sys_bufinfo_t *buf);
int sys_vm_slabs(sys_slabinfo_t *slabs, size_t *count);

/* CPU Information API */
int sys_cpu_count(void);
int sys_cpu_info(int cpu, sys_cpuinfo_t *info);
int sys_cpu_times(int cpu, sys_cputimes_t *times);
int sys_cpu_loadavg(double *avg1, double *avg5, double *avg15);

/* System Information API */
int sys_uptime(struct timespec *ts);
int sys_boottime(struct timespec *ts);
int sys_hostname(char *buf, size_t len);
int sys_domainname(char *buf, size_t len);
int sys_kernel_version(sys_version_t *ver);

/* Network Information API */
int sys_net_interfaces(sys_netif_t *ifs, size_t *count);
int sys_net_addrs(const char *ifname, sys_netaddr_t *addrs, size_t *count);
int sys_net_stats(const char *ifname, sys_netstats_t *stats);
int sys_net_routes(sys_route_t *routes, size_t *count);
#endif

#endif

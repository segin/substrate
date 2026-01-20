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

#endif

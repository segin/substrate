/*
 * kern/sysinfo.c
 *
 * System Information Syscall
 */

#include <sys/sysinfo.h>
#include <stdint.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <pm/pm.h>
#include <kern/time.h>
#include <vm/phys_mem.h>
#include <arch/i386/pmm.h> /* For PMM constants */
#include <string.h>

#define PAGE_SIZE 4096

/*
 * validate_user_buffer is static in syscall.c. 
 * We iterate processes directly from pm.h
 */

// Internal implementation (no pointer validation)
int do_sysinfo(struct sysinfo *info) {
    if (!info) return -14; // EFAULT

    struct sysinfo kinfo;
    memset(&kinfo, 0, sizeof(kinfo));

    kinfo.uptime = get_uptime();

    // Memory Stats
    // vm_phys_get_free/used returns pages.
    unsigned long free_pages = vm_phys_get_free();
    unsigned long used_pages = vm_phys_get_used();

    kinfo.totalram = (free_pages + used_pages) * PAGE_SIZE;
    kinfo.freeram = free_pages * PAGE_SIZE;
    kinfo.mem_unit = 1;
    
    // Process Count
    // Iterate global process table
    uint16_t procs = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (processes[i].pid != -1) {
            procs++;
        }
    }
    kinfo.procs = procs;

    // Loads (scaled by 65536, SI_LOAD_SCALE)
    // Currently using instantaneous load as proxy
    extern uint32_t sched_get_system_load(void);
    unsigned long load = sched_get_system_load() * 65536;
    kinfo.loads[0] = load;
    kinfo.loads[1] = load;
    kinfo.loads[2] = load;

    // Copyout
    // Using memcpy as placeholder for copyout/copy_to_user
    // Assumes flat address space or same-context copy.
    memcpy(info, &kinfo, sizeof(kinfo));

    return 0;
}

int sys_sysinfo(struct sysinfo *info) {
    // Check user pointer validity (basic)
    if ((uintptr_t)info >= 0xC0000000) return -14;

    return do_sysinfo(info);
}

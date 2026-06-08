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
#include <vm/vm_page.h>
#include <vm/vm_swap.h>
#include <string.h>
#include <stdio.h>
#include <sys/copy.h>
#include "memtrack.h"

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
    FOREACH_PROC(proc) {
        (void)proc;
        procs++;
    }
    kinfo.procs = procs;

    // Loads: the real 1/5/15-minute exponentially-decayed averages the
    // kernel maintains in avenrun (sched_get_loadavg), not an instantaneous
    // proxy.  avenrun is FSHIFT=11 fixed point; sysinfo(2) reports loads
    // scaled by SI_LOAD_SCALE = 65536 = 2^16, so shift left (16 - 11) = 5.
    extern void sched_get_loadavg(unsigned long *loads);
    unsigned long av[3];
    sched_get_loadavg(av);
    kinfo.loads[0] = av[0] << 5;
    kinfo.loads[1] = av[1] << 5;
    kinfo.loads[2] = av[2] << 5;

    // Using copyout to send result to userspace
    if (copyout(&kinfo, info, sizeof(struct sysinfo)) != 0) return -14; // EFAULT

    return 0;
}

int sys_sysinfo(struct sysinfo *info) {
    return do_sysinfo(info);
}

int sys_vm_stats(sys_vmstat_t *stats) {
    if (!stats) return -14; // EFAULT

    sys_vmstat_t kstats;
    memset(&kstats, 0, sizeof(kstats));

    size_t free_pages = vm_phys_get_free();
    size_t used_pages = vm_phys_get_used();

    kstats.total = (uint64_t)(free_pages + used_pages) * PAGE_SIZE;
    kstats.free = (uint64_t)free_pages * PAGE_SIZE;

    // Page stats (cache, buffers)
    vm_page_stats_t pstats;
    vm_page_get_stats(&pstats);

    kstats.cached = (uint64_t)(pstats.active_count + pstats.inactive_count) * PAGE_SIZE;
    kstats.buffers = 0; // Not tracked separately
    kstats.available = kstats.free + kstats.cached; // Cache is reclaimable

    // Swap stats
    uint64_t swap_total = 0;
    uint64_t swap_free = 0;
    vm_swap_get_stats(&swap_total, &swap_free);

    kstats.swap_total = swap_total * PAGE_SIZE;
    kstats.swap_free = swap_free * PAGE_SIZE;
    kstats.swap_cached = 0; // Not tracked separately

    if (copyout(&kstats, stats, sizeof(sys_vmstat_t)) != 0) return -14; // EFAULT
    return 0;
}

/* sys_vm_info: per-zone breakdown.  Substrate today maintains a
 * single physical zone (the buddy allocator covers everything below
 * PMM_DIRECTMAP_PHYS_LIMIT), so we report the entire pool under
 * "normal" with DMA and HighMem zeroed.  When a zone split lands the
 * accessor becomes vm_phys_zone_stats(zone) per slot. */
int sys_vm_info(sys_vminfo_t *info) {
    if (!info) return -14;
    sys_vminfo_t k;
    memset(&k, 0, sizeof(k));
    uint64_t total = (uint64_t)(vm_phys_get_free() + vm_phys_get_used()) * PAGE_SIZE;
    uint64_t free  = (uint64_t)vm_phys_get_free() * PAGE_SIZE;
    k.normal_total = total;
    k.normal_free  = free;
    if (copyout(&k, info, sizeof(k)) != 0) return -14;
    return 0;
}

/* sys_vm_swap: enumerate swap backing stores.  *count is in/out
 * (capacity → actual).  Substrate currently exposes a single
 * aggregate via vm_swap_get_stats; until per-device enumeration
 * lands we synthesize one entry "swap0" iff swap is configured. */
int sys_vm_swap(sys_swapinfo_t *swap, size_t *count) {
    if (!count) return -14;
    size_t cap = 0;
    if (copyin(count, &cap, sizeof(cap)) != 0) return -14;

    uint64_t total = 0, free = 0;
    vm_swap_get_stats(&total, &free);
    size_t n = (total > 0) ? 1 : 0;

    if (swap && cap > 0 && n > 0) {
        sys_swapinfo_t k;
        memset(&k, 0, sizeof(k));
        const char *path = "swap0";
        size_t plen = strlen(path);
        memcpy(k.path, path, plen);
        k.total = total * PAGE_SIZE;
        k.used  = (total - free) * PAGE_SIZE;
        k.priority = 0;
        if (copyout(&k, &swap[0], sizeof(k)) != 0) return -14;
    }

    if (copyout(&n, count, sizeof(n)) != 0) return -14;
    return 0;
}

/* sys_vm_buffers: report bio cache buffer counts.  Substrate's bio
 * subsystem doesn't yet export a stats accessor, so report all-zero
 * with the conventional 4 KiB buffer size.  Field semantics are
 * preserved so the wrapper doesn't have to gate on feature support. */
int sys_vm_buffers(sys_bufinfo_t *buf) {
    if (!buf) return -14;
    sys_bufinfo_t k;
    memset(&k, 0, sizeof(k));
    k.buffer_size = PAGE_SIZE;
    if (copyout(&k, buf, sizeof(k)) != 0) return -14;
    return 0;
}

/* sys_vm_slabs: report the per-call-site physical-page accounting
 * table (kern/memtrack.c).  Each record maps to one sys_slabinfo_t:
 *   name           "pc=0x........" — the allocating call site
 *   objsize        page size (these are page-granular allocations)
 *   total          pages allocated from this site over its lifetime
 *   active         pages still outstanding (alloc - free) — the leak
 *   slabs          pages freed back over the lifetime
 * `*count` is the caller's array capacity on entry and the number of
 * records written on return. */
int sys_vm_slabs(sys_slabinfo_t *slabs, size_t *count) {
    if (!count) return -EINVAL;
    size_t cap = 0;
    if (copyin(count, &cap, sizeof(cap)) != 0) return -EFAULT;

    memtrack_rec_t recs[MEMTRACK_SITES];
    size_t n = memtrack_snapshot(recs, MEMTRACK_SITES);

    size_t out_n = 0;
    for (size_t i = 0; i < n && slabs && out_n < cap; i++) {
        sys_slabinfo_t si;
        memset(&si, 0, sizeof(si));
        snprintf(si.name, sizeof(si.name), "pc=0x%08x", (unsigned)recs[i].pc);
        si.objsize        = PAGE_SIZE;
        si.total          = (uint32_t)recs[i].pages_alloc;
        si.active         = (uint32_t)(recs[i].pages_alloc - recs[i].pages_free);
        si.slabs          = (uint32_t)recs[i].pages_free;
        si.pages_per_slab = 1;
        if (copyout(&si, &slabs[out_n], sizeof(si)) != 0) return -EFAULT;
        out_n++;
    }
    if (copyout(&out_n, count, sizeof(out_n)) != 0) return -EFAULT;
    return 0;
}

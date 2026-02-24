#ifndef _PMAP_H
#define _PMAP_H
struct pmap_stats {
    uint32_t faults;
    uint32_t cow_faults;
    uint32_t zero_fills;
    uint32_t protection_upgrades;
    uint32_t protection_downgrades;
    uint32_t cow_pages_mapped;
    uint32_t cow_duplications;
    uint32_t pages_saved_by_cow;
    uint32_t tlb_invlpg_count;
    uint32_t tlb_full_flush_count;
    uint32_t total_pmaps;
};
int sys_pmap_stats(struct pmap_stats *stats);
#endif

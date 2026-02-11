#ifndef _X86_64_PMAP_H
#define _X86_64_PMAP_H

#include <stdint.h>
#include <stddef.h>
#include <sys/queue.h>
#include <sys/lock.h>

// x86_64 Page Table Flags
#define PTE_P           0x001UL
#define PTE_W           0x002UL
#define PTE_U           0x004UL
#define PTE_PWT         0x008UL
#define PTE_PCD         0x010UL
#define PTE_A           0x020UL
#define PTE_D           0x040UL
#define PTE_PS          0x080UL
#define PTE_G           0x100UL
#define PTE_NX          (1UL << 63)

// To be added in the fix, but adding here now to test
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXEC    0x04
#define VM_PROT_USER    0x08
#define VM_PROT_ALL     (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC|VM_PROT_USER)

#define NPTE_LEVEL      512
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000UL

typedef uint64_t pml4e_t;
typedef uint64_t pdpte_t;
typedef uint64_t pde_t;
typedef uint64_t pte_t;

#define RECURSIVE_SLOT  510UL

// Mocked Accessor Functions
pml4e_t* mock_get_pml4_entry(int i);
pdpte_t* mock_get_pdpt_entry(int i, int j);
pde_t*   mock_get_pd_entry(int i, int j, int k);
pte_t*   mock_get_pt_entry(int i, int j, int k, int l);

#define V_PML4_INDEX(i)        mock_get_pml4_entry(i)
#define V_PDPT_INDEX(i, j)     mock_get_pdpt_entry(i, j)
#define V_PD_INDEX(i,j,k)      mock_get_pd_entry(i, j, k)
#define V_PT_INDEX(i,j,k,l)    mock_get_pt_entry(i, j, k, l)

#define V_PML4       V_PML4_INDEX(0)
#define V_PDPT(pml4i)       V_PDPT_INDEX(pml4i, 0)

#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

struct pmap_stats {
    uint64_t faults;
    uint64_t cow_faults;
    uint64_t zero_fills;
    uint64_t cow_pages_mapped;
    uint64_t protection_upgrades;
    uint64_t protection_downgrades;
};

struct pmap {
    pml4e_t *pml4;
    uint64_t pml4_phys;
    int ref_count;
    int resident_count;
    int wired_count;
    struct pmap_stats stats;
    int lock;
    int asid;
    TAILQ_ENTRY(pmap) list_entry;
};
typedef struct pmap *pmap_t;

TAILQ_HEAD(pmap_list, pmap);
extern struct pmap_list global_pmap_list;
extern spinlock_t pmap_list_lock;

void pmap_init(void);
pmap_t pmap_kernel(void);
int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uint64_t va);
uint64_t pmap_extract(pmap_t pmap, uint64_t va);
int pmap_protect(pmap_t pmap, uint64_t sva, uint64_t eva, uint64_t prot);
pmap_t pmap_create(void);
void pmap_destroy(pmap_t pmap);
void pmap_activate(pmap_t pmap);
void pmap_reference(pmap_t pmap);
int pmap_is_referenced(pmap_t pmap, uint64_t va);
int pmap_is_modified(pmap_t pmap, uint64_t va);
void pmap_clear_reference(pmap_t pmap, uint64_t va);
void pmap_clear_modify(pmap_t pmap, uint64_t va);
int pmap_is_referenced_range(pmap_t pmap, uint64_t sva, uint64_t eva);
int pmap_is_modified_range(pmap_t pmap, uint64_t sva, uint64_t eva);
int pmap_test_and_clear_reference(pmap_t pmap, uint64_t va);
int pmap_test_and_clear_modify(pmap_t pmap, uint64_t va);
void pmap_invalidate_page(uint64_t va);
pmap_t pmap_fork(pmap_t src_pmap);
int pmap_page_is_cow(pmap_t pmap, uint64_t va);
void pmap_release(pmap_t pmap);
void pmap_invalidate_all(void);
void pmap_shootdown_handler(void);
void pmap_shootdown_page(uint64_t va);
void pmap_shootdown_range(uint64_t va, uint64_t len);
void pmap_shootdown_all(void);
void pmap_shootdown_defer(uint64_t va);
void pmap_shootdown_commit(void);
void pmap_shootdown_wait(int expected_cpus);
int cpuid_check_1gb_pages(void);
int pmap_enter_2mb(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags);
int pmap_enter_1gb(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags);
void pmap_remove_2mb(pmap_t pmap, uint64_t va);
void pmap_remove_1gb(pmap_t pmap, uint64_t va);
int cpuid_check_pge(void);
void pmap_pge_enable(void);
void pmap_pge_disable(void);
int pmap_set_global(pmap_t pmap, uint64_t va);
int pmap_clear_global(pmap_t pmap, uint64_t va);
void pmap_invalidate_global(void);
void pmap_mark_kernel_global(pmap_t pmap, uint64_t sva, uint64_t eva);
int cpuid_check_pcid(void);
int cpuid_check_invpcid(void);
void pmap_pcid_enable(void);
int pmap_pcid_alloc(pmap_t pmap);
void pmap_pcid_free(pmap_t pmap);
void pmap_activate_pcid(pmap_t pmap, int noflush);
void pmap_invpcid(int type, int pcid, uint64_t va);
void pmap_invpcid_single(uint64_t va);
void pmap_invpcid_context(int pcid);
void pmap_invpcid_all(void);
void pmap_invpcid_all_global(void);

#endif

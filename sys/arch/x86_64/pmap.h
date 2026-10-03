/*
 * pmap.h - x86_64 physical map: 4-level page tables
 *
 * The interface is arch/i386/pmap.h's, so the machine-independent VM code
 * drives either kernel unchanged.  Underneath:
 *
 * - Each address space is a PML4.  The upper half (slots 256-511) is the
 *   kernel's and is shared: pmap_create() copies the kernel's PML4 entries,
 *   and every kernel PML4 slot that will ever be used (the direct map, the
 *   kernel image, the ioremap window) is populated at boot, so a new kernel
 *   mapping shows up in every address space without pmap_growkernel().
 * - The lower half is the process's.  Today's processes are 32-bit, so
 *   only its first 4 GiB is ever used.
 * - Page tables are reached through the direct map (P2V), never through a
 *   recursive slot, so any pmap can be read or changed without loading it.
 *
 * The signal trampoline page sits at SIG_TRAMPOLINE_ADDR (0xFE000000) as on
 * i386.  There that is kernel space; here it is in the 32-bit process's own
 * 4 GiB, so pmap_create() maps it into every address space and the
 * user-range walkers leave it alone.
 */
#ifndef _ARCH_X86_64_PMAP_H
#define _ARCH_X86_64_PMAP_H

#include <stdint.h>
#include <stddef.h>
#include <machine/vmparam.h>

/* Page-table entry flags.  The low twelve bits have the i386 meanings. */
#define PTE_P           0x001ULL    // Present
#define PTE_W           0x002ULL    // Writeable
#define PTE_U           0x004ULL    // User-accessible
#define PTE_PWT         0x008ULL    // Write-Through
#define PTE_PCD         0x010ULL    // Cache-Disable
#define PTE_A           0x020ULL    // Accessed
#define PTE_D           0x040ULL    // Dirty
#define PTE_PAT         0x080ULL    // PAT index bit for 4KB PTEs
#define PTE_PS          0x080ULL    // Page Size (2MB PDEs, 1GB PDPTEs)
#define PTE_G           0x100ULL    // Global
#define PTE_NX          (1ULL << 63) // No Execute

/* Physical frame of an entry: bits 12-51. */
#define PTE_FRAME       0x000FFFFFFFFFF000ULL

typedef uint64_t pt_entry_t;

typedef struct pmap *pmap_t;
extern pmap_t curpmap;

// Per-pmap statistics
struct pmap_stats {
    uint32_t faults;               // Total page faults
    uint32_t cow_faults;           // COW page faults
    uint32_t zero_fills;           // Zero-fill page faults
    uint32_t protection_upgrades;  // Protection upgrades (read→write)
    uint32_t protection_downgrades; // Protection downgrades (write→read)
    uint32_t cow_pages_mapped;     // Total pages initially shared as COW
    uint32_t cow_duplications;     // Pages physically duplicated during COW
    uint32_t pages_saved_by_cow;   // Pages never duplicated (process exited clean)
    uint32_t tlb_invlpg_count;     // Single-page TLB invalidations (invlpg)
    uint32_t tlb_full_flush_count; // Full TLB flushes (CR3 reload)
    uint32_t total_pmaps;          // Current number of allocated pmaps
    uint32_t active_pmaps;         // Unique pmaps referenced by live threads
};

int sys_pmap_stats(struct pmap_stats *out);

struct pmap_list_entry {
    struct pmap *next;
    struct pmap *prev;
};

struct pmap {
    pt_entry_t *pml4;           // Direct-map pointer to the PML4
    uint64_t pml4_phys;         // Its physical address: the CR3 value
    int ref_count;              // References (for COW sharing)
    uint32_t resident_count;    // Count of resident pages in this pmap
    uint32_t wired_count;       // Count of wired (unpageable) pages
    uint32_t mapped_count;      // Count of mapped pages/slots in this pmap
    struct pmap_stats stats;    // Per-pmap statistics
    volatile int lock;          // Spinlock for SMP safety
    uint16_t asid;              // Address Space ID (future PCID)
    struct pmap_list_entry list_entry;  // Global pmap list
};

// Initialization
void pmap_bootstrap(void);

// Address Space Management
pmap_t pmap_create(void);
void pmap_destroy(pmap_t pmap);
void pmap_activate(pmap_t pmap);
pmap_t pmap_kernel(void);
uint32_t pmap_resident_count(pmap_t pmap);
void pmap_reference(pmap_t pmap);
void pmap_release(pmap_t pmap);
pmap_t pmap_fork(pmap_t src_pmap);
void pmap_share_range(pmap_t pmap, uintptr_t start, uintptr_t end);
void pmap_growkernel(uintptr_t va);

// Mapping Operations.  Return 0 on success, < 0 on error.
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
int pmap_enter_batch(pmap_t pmap, uintptr_t va_start, int count, uintptr_t *pa_list, uint32_t prot, uint32_t flags);
int pmap_enter_large(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uintptr_t va);
void pmap_remove_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);
void pmap_fork_clear_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);
uintptr_t pmap_extract(pmap_t pmap, uintptr_t va);
size_t pmap_copyin_other(pmap_t pmap, uintptr_t uva, void *dst, size_t len);
size_t pmap_copyout_other(pmap_t pmap, uintptr_t uva, const void *src, size_t len);

// Protection flags for pmap_enter
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXEC    0x04
#define VM_PROT_USER    0x08
#define VM_PROT_ALL     (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC|VM_PROT_USER)

// Kernel-only fast paths
void pmap_kenter(uintptr_t va, uintptr_t pa);
void pmap_kremove(uintptr_t va);

// Protection and copying
int pmap_protect(pmap_t pmap, uintptr_t sva, uintptr_t eva, uint32_t prot);
int pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, uintptr_t sva, uintptr_t eva, int cow);
int pmap_page_is_cow(pmap_t pmap, uintptr_t va);

void pmap_copy_page(uintptr_t src_pa, uintptr_t dst_pa);
void pmap_zero_page(uintptr_t pa);

void pmap_invalidate_page(uintptr_t va);
void pmap_invalidate_all(void);
void pmap_flush_global_pages(void);

void pmap_shootdown_page(uintptr_t va);
void pmap_shootdown_range(uintptr_t va, uint32_t len);
void pmap_shootdown_all(void);
void pmap_shootdown_handler(void);
void pmap_shootdown_defer(uintptr_t va);
void pmap_shootdown_commit(void);
void pmap_shootdown_wait(uint32_t gen);

int pmap_is_referenced_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);
int pmap_is_referenced(pmap_t pmap, uintptr_t va);
int pmap_is_modified(pmap_t pmap, uintptr_t va);
void pmap_clear_reference(pmap_t pmap, uintptr_t va);
void pmap_clear_modify(pmap_t pmap, uintptr_t va);
int pmap_is_modified_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);

struct vm_page;
int pmap_page_is_referenced(struct vm_page *m);
void pmap_page_clear_reference(struct vm_page *m);
int pmap_test_and_clear_ref(struct vm_page *m);
int pmap_test_and_clear_modify(struct vm_page *m);
void pmap_track_access(struct vm_page *m);
void pmap_track_modify(struct vm_page *m, uint32_t current_time);

void pmap_dump(pmap_t pmap);
int pmap_check(pmap_t pmap);

void pmap_null_protect(void);
void pmap_null_allow(int enable);

void pmap_map_trampoline(void);

int pmap_fault(uint32_t err_code, uintptr_t cr2);

#define TLB_BATCH_THRESHOLD 32

extern uint64_t pmap_destroy_anon_freed;
extern uint64_t pmap_destroy_anon_skipped;
extern uint64_t pmap_destroy_skip_obj;
extern uint64_t pmap_destroy_skip_wired;
extern uint64_t pmap_destroy_skip_refcnt;
extern uint64_t pmap_create_calls;
extern uint64_t pmap_destroy_calls;
extern uint64_t pmap_destroy_anon_rc0;
extern uint64_t pmap_destroy_anon_rc2;
extern uint64_t pmap_destroy_anon_rc_big;

#endif /* _ARCH_X86_64_PMAP_H */

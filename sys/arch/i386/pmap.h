#ifndef _PMAP_H
#define _PMAP_H

/*
 * pmap.h - x86 Physical Map (PMAP) Layer
 *
 * Per-Process Address Space Architecture:
 * - Each process has its own pmap_t representing its virtual address space.
 * - User Space: 0x00000000 - 0xBFFFFFFF (3GB, PDEs 0-767)
 * - Kernel Space: 0xC0000000 - 0xFFFFFFFF (1GB, PDEs 768-1023, shared)
 * - Kernel PDEs are shared by reference, not copied.
 * - Recursive mapping at PDE 1023 for efficient PT access.
 * - Dynamic PT Allocation: Page tables allocated on-demand (~4KB per 4MB).
 * - Minimum overhead: 1 PD (4KB) + PTs as needed.
 */

#include <stdint.h>
#include <stddef.h>

// x86 Page Table Flags
#define PTE_P           0x01    // Present
#define PTE_W           0x02    // Writeable
#define PTE_U           0x04    // User-accessible
#define PTE_PWT         0x08    // Write-Through
#define PTE_PCD         0x10    // Cache-Disable
#define PTE_A           0x20    // Accessed
#define PTE_D           0x40    // Dirty
#define PTE_PS          0x80    // Page Size (4MB)
#define PTE_G           0x100   // Global

// Abstract PMAP handle (opaque pointer to Page Directory)
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
};

// Syscall to get stats
int sys_pmap_stats(struct pmap_stats *out);

// Linked list entry for global pmap list
struct pmap_list_entry {
    struct pmap *next;
    struct pmap *prev;
};

struct pmap {
    uint32_t *pdir;             // pd_virt: Virtual pointer to PD
    uint32_t pdir_phys;         // pd_phys: Physical address of PD
    int ref_count;              // refcount: Number of references (for COW sharing)
    uint32_t resident_count;    // Count of resident pages in this pmap
    uint32_t wired_count;       // Count of wired (unpageable) pages
    struct pmap_stats stats;    // Per-pmap statistics
    volatile int lock;          // Spinlock for SMP safety
    uint16_t asid;              // Address Space ID (for TLB tagging, future PCID)
    struct pmap_list_entry list_entry;  // For global pmap list (TLB shootdown)
};

// Hardcoded Kernel Page Directory Virtual Address (Recursive Mapping)
// We'll use the last entry (1023) to point to itself.
// PD Address: 0xFFFFF000
// PTs Window: 0xFFC00000 - 0xFFFFFFFF
#define PT_BASE_ADDR    0xFFC00000

// Initialization
void pmap_bootstrap(void);

// Address Space Management
pmap_t pmap_create(void);
void pmap_destroy(pmap_t pmap);
void pmap_activate(pmap_t pmap); // Switch CR3 to this pmap
pmap_t pmap_kernel(void);        // Get kernel pmap
void pmap_reference(pmap_t pmap); // Increment ref_count
void pmap_release(pmap_t pmap);   // Decrement ref_count, destroy if 0
pmap_t pmap_fork(pmap_t src_pmap); // Fork with COW

// Mapping Operations
// Returns 0 on success, < 0 on error
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
int pmap_enter_batch(pmap_t pmap, uintptr_t va_start, int count, uintptr_t *pa_list, uint32_t prot, uint32_t flags);
int pmap_enter_large(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uintptr_t va);
void pmap_remove_range(pmap_t pmap, uint32_t sva, uint32_t eva);
uintptr_t pmap_extract(pmap_t pmap, uintptr_t va); // Get PA from VA

// Protection flags for pmap_enter
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXEC    0x04
#define VM_PROT_USER    0x08
#define VM_PROT_ALL     (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC|VM_PROT_USER)

// Kernel-only fast paths (no locking, assumes kernel pmap active)
void pmap_kenter(uintptr_t va, uintptr_t pa);
void pmap_kremove(uintptr_t va);

// Protection and copying
int pmap_protect(pmap_t pmap, uintptr_t sva, uintptr_t eva, uint32_t prot);
int pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, uintptr_t sva, uintptr_t eva, int cow);
int pmap_page_is_cow(pmap_t pmap, uintptr_t va);

// Optimized page operations
void pmap_copy_page(uintptr_t src_pa, uintptr_t dst_pa);
void pmap_zero_page(uintptr_t pa);

// Helper to flush TLB
void pmap_invalidate_page(uintptr_t va);
void pmap_invalidate_all(void);  // CR3 reload, flushes entire TLB
void pmap_flush_global_pages(void);  // CR4 toggle, flushes TLB including global pages

// SMP TLB Shootdown (invalidate on all CPUs)
void pmap_shootdown_page(uintptr_t va);
void pmap_shootdown_range(uintptr_t va, uint32_t len);
void pmap_shootdown_all(void);
void pmap_shootdown_handler(void);  // Called by IPI handler
void pmap_shootdown_defer(uintptr_t va);  // Batch accumulator
void pmap_shootdown_commit(void);        // Flush accumulated pages
void pmap_shootdown_wait(int expected_cpus);  // Completion barrier

// Page reference/modification tracking
int pmap_is_referenced_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);
int pmap_test_and_clear_ref(pmap_t pmap, uintptr_t va);
int pmap_is_modified_range(pmap_t pmap, uintptr_t sva, uintptr_t eva);
int pmap_test_and_clear_modify(pmap_t pmap, uintptr_t va);

// Per-page reference/modification tracking (walks pv_list)
struct vm_page;
int pmap_is_referenced(struct vm_page *m);
void pmap_clear_reference(struct vm_page *m);
void pmap_track_access(struct vm_page *m);

// Debug and Diagnostic Functions
void pmap_dump(pmap_t pmap);
int pmap_check(pmap_t pmap);

// NULL Protection (TASKS.md L565)
void pmap_null_protect(void);   // Unmap page 0 for NULL pointer protection
void pmap_null_allow(int enable); // Re-map page 0 for VM86/legacy mode

// Signal Trampoline
void pmap_map_trampoline(void);

int pmap_fault(uint32_t err_code, uint32_t cr2);

// Recursive Paging Helpers
#define PD_INDEX(va)    (((uint32_t)(va)) >> 22)
#define PT_INDEX(va)    ((((uint32_t)(va)) >> 12) & 0x3FF)
#define PTE_FRAME       0xFFFFF000 // Frame address mask

// Kernel Address Space Translations (Higher Half)
#define V2P(x) ((uint32_t)(x) - 0xC0000000)
#define P2V(x) ((void*)((uint32_t)(x) + 0xC0000000))

// Threshold for switching to full TLB flush vs individual INVLPG
#define TLB_BATCH_THRESHOLD 32

// Access to PD and PTs via recursive mapping
#define V_PD  ((uint32_t *)0xFFFFF000)
#define V_PT(i) ((uint32_t *)(0xFFC00000 + ((i) << 12)))

#endif
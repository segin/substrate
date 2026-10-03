/*
 * pmap.c - x86_64 physical map: 4-level page tables
 *
 * Implements arch/i386/pmap.h's interface over PML4/PDPT/PD/PT tables
 * (pmap.h describes the layout).  The bookkeeping -- pv entries, page
 * holds, the anonymous-page free rule in pmap_destroy(), copy-on-write in
 * pmap_fork()/pmap_fault() -- follows the i386 pmap exactly; what differs
 * is the walk.  Every table is reached through the direct map, so unlike
 * the i386 recursive map, any address space can be read or changed
 * without loading it, and the "must be the active pmap" restrictions of
 * the i386 functions do not apply.
 */
#include <stdio.h>
#include <string.h>

#include <arch/x86-common/cpu.h>
#include <arch/x86-common/lapic.h>
#include <arch/x86_64/boot.h>
#include <machine/percpu.h>
#include <machine/pmap.h>
#include <machine/pmm.h>
#include <machine/signal_arch.h>
#include <kern/console.h>
#include <kern/panic.h>
#include <kern/sched.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <vm/vm_kmem.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#define PML4_SHIFT      39
#define PDPT_SHIFT      30
#define PD_SHIFT        21
#define PT_SHIFT        12
#define PT_IDX(va, shift) (((uintptr_t)(va) >> (shift)) & 511)

#define USER_PML4_SLOTS 256         /* lower half: the process's */
#define PAGE_4K         0x1000UL
#define PAGE_2M         0x200000UL
#define PAGE_1G         0x40000000UL

/* The direct map the boot page tables build covers 4 GiB (layout.h);
 * everything the PMM hands out lies below it. */
#define PMAP_DMAP_LIMIT DMAP_BOOT_SIZE

static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;
pmap_t curpmap = NULL;

static struct pmap *pmap_list_head = NULL;
static volatile int pmap_list_lock = 0;

static struct pmap_stats global_pmap_stats = {0};

/* The signal trampoline page (pmap_map_trampoline), installed read-only in
 * every address space at SIG_TRAMPOLINE_ADDR. */
static uintptr_t trampoline_pa = 0;

uint64_t pmap_destroy_anon_freed     = 0;
uint64_t pmap_destroy_anon_skipped   = 0;
uint64_t pmap_destroy_skip_obj       = 0;
uint64_t pmap_destroy_skip_wired     = 0;
uint64_t pmap_destroy_skip_refcnt    = 0;
uint64_t pmap_destroy_calls          = 0;
uint64_t pmap_destroy_anon_rc0       = 0;
uint64_t pmap_destroy_anon_rc2       = 0;
uint64_t pmap_destroy_anon_rc_big    = 0;
uint64_t pmap_create_calls           = 0;

/* ==================== Hardware ==================== */

static inline uint64_t pmap_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void pmap_write_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static inline uint64_t pmap_read_cr4(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

static inline void pmap_write_cr4(uint64_t cr4) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
}

#define CR4_PGE 0x80

/* ==================== Helpers ==================== */

static void pmap_stat_inc(pmap_t pmap, size_t field_offset) {
    if (pmap) {
        uint32_t *field = (uint32_t *)((char *)&pmap->stats + field_offset);
        (*field)++;
    }
    uint32_t *global_field = (uint32_t *)((char *)&global_pmap_stats + field_offset);
    __sync_fetch_and_add(global_field, 1);
}

static void pmap_list_add(pmap_t pmap) {
    while (__sync_lock_test_and_set(&pmap_list_lock, 1)) {
        __asm__ volatile("pause");
    }
    pmap->list_entry.next = pmap_list_head;
    pmap->list_entry.prev = NULL;
    if (pmap_list_head) {
        pmap_list_head->list_entry.prev = pmap;
    }
    pmap_list_head = pmap;
    global_pmap_stats.total_pmaps++;
    __sync_lock_release(&pmap_list_lock);
}

static void pmap_list_remove(pmap_t pmap) {
    while (__sync_lock_test_and_set(&pmap_list_lock, 1)) {
        __asm__ volatile("pause");
    }
    if (pmap->list_entry.prev) {
        pmap->list_entry.prev->list_entry.next = pmap->list_entry.next;
    } else {
        pmap_list_head = pmap->list_entry.next;
    }
    if (pmap->list_entry.next) {
        pmap->list_entry.next->list_entry.prev = pmap->list_entry.prev;
    }
    pmap->list_entry.next = NULL;
    pmap->list_entry.prev = NULL;
    global_pmap_stats.total_pmaps--;
    __sync_lock_release(&pmap_list_lock);
}

static void pmap_count_map_add(pmap_t pmap, uint32_t pages) {
    if (!pmap || pages == 0) return;
    pmap->resident_count += pages;
    pmap->mapped_count += pages;
}

static void pmap_count_map_remove(pmap_t pmap, uint32_t pages) {
    if (!pmap || pages == 0) return;
    pmap->resident_count = pmap->resident_count >= pages ? pmap->resident_count - pages : 0;
    pmap->mapped_count = pmap->mapped_count >= pages ? pmap->mapped_count - pages : 0;
}

static inline int pmap_is_current(pmap_t pmap) {
    return pmap && (pmap == kernel_pmap_ptr ||
                    (pmap_read_cr3() & PTE_FRAME) == pmap->pml4_phys);
}

static inline int pmap_va_is_user(uintptr_t va) {
    return va < KERNEL_VA_START;
}

static inline int pmap_is_trampoline(uintptr_t va) {
    return trampoline_pa != 0 &&
           (va & ~(PAGE_4K - 1)) == (uintptr_t)SIG_TRAMPOLINE_ADDR;
}

/* The table an entry points at, through the direct map. */
static inline pt_entry_t *pmap_table(pt_entry_t e) {
    return (pt_entry_t *)P2V(e & PTE_FRAME);
}

static pt_entry_t *pmap_alloc_table(pt_entry_t *slot, pt_entry_t flags) {
    void *t = pmm_alloc_block();
    if (!t) {
        return NULL;
    }
    memset(t, 0, PAGE_4K);
    *slot = (pt_entry_t)V2P(t) | flags;
    return (pt_entry_t *)t;
}

/*
 * The 4 KiB PTE slot that maps `va` in `pmap`.  With `alloc`, missing
 * intermediate tables are created (user ones user-accessible, so the PTE
 * alone decides).  NULL when absent, when allocation fails, or when a
 * large page covers `va` (only the kernel's boot mappings use them).
 */
static pt_entry_t *pmap_pte(pmap_t pmap, uintptr_t va, int alloc) {
    pt_entry_t flags = PTE_P | PTE_W | (pmap_va_is_user(va) ? PTE_U : 0);
    pt_entry_t *table = pmap->pml4;
    static const int shifts[3] = { PML4_SHIFT, PDPT_SHIFT, PD_SHIFT };

    for (int level = 0; level < 3; level++) {
        pt_entry_t *slot = &table[PT_IDX(va, shifts[level])];

        if (!(*slot & PTE_P)) {
            if (!alloc) {
                return NULL;
            }
            table = pmap_alloc_table(slot, flags);
            if (!table) {
                return NULL;
            }
            continue;
        }
        if (*slot & PTE_PS) {
            return NULL;
        }
        if (pmap_va_is_user(va) && !(*slot & PTE_U)) {
            *slot |= PTE_U;
        }
        table = pmap_table(*slot);
    }
    return &table[PT_IDX(va, PT_SHIFT)];
}

/* Physical address `va` translates to in `pmap`, large pages included;
 * 0 if unmapped. */
static uintptr_t pmap_translate(pmap_t pmap, uintptr_t va) {
    pt_entry_t *table = pmap->pml4;
    pt_entry_t e;

    e = table[PT_IDX(va, PML4_SHIFT)];
    if (!(e & PTE_P)) return 0;
    table = pmap_table(e);

    e = table[PT_IDX(va, PDPT_SHIFT)];
    if (!(e & PTE_P)) return 0;
    if (e & PTE_PS) return (e & PTE_FRAME & ~(PAGE_1G - 1)) + (va & (PAGE_1G - 1));
    table = pmap_table(e);

    e = table[PT_IDX(va, PD_SHIFT)];
    if (!(e & PTE_P)) return 0;
    if (e & PTE_PS) return (e & PTE_FRAME & ~(PAGE_2M - 1)) + (va & (PAGE_2M - 1));
    table = pmap_table(e);

    e = table[PT_IDX(va, PT_SHIFT)];
    if (!(e & PTE_P)) return 0;
    return (e & PTE_FRAME) + (va & (PAGE_4K - 1));
}

static pt_entry_t pmap_pte_flags(uintptr_t va, uint32_t prot, uint32_t flags) {
    pt_entry_t pte = PTE_P;

    if (prot & VM_PROT_WRITE) {
        pte |= PTE_W;
    }
    if (pmap_va_is_user(va) || (prot & VM_PROT_USER)) {
        pte |= PTE_U;
    } else if (pmap_read_cr4() & CR4_PGE) {
        pte |= PTE_G;
    }
    pte |= (pt_entry_t)flags & (PTE_PWT | PTE_PCD | PTE_PAT);
    return pte;
}

/* Drop one user PTE's page: pv entry and hold, and free an anonymous page
 * that has no other owner.  The rule and its reasons are the i386
 * pmap_destroy()'s. */
static void pmap_release_page(pmap_t pmap, uintptr_t va, uintptr_t pa) {
    vm_page_t *page = pmm_get_page(pa);

    if (!page) {
        return;
    }
    pv_remove(page, pmap, va);
    vm_page_unhold(page);
    if (page->pv_list == NULL && page->ref_count == 1 &&
        page->wire_count == 0 && page->object == NULL) {
        vm_page_free(page);
        pmap_destroy_anon_freed++;
    } else {
        pmap_destroy_anon_skipped++;
        pmap_destroy_skip_obj   += (page->object != NULL);
        pmap_destroy_skip_wired += (page->wire_count != 0);
        pmap_destroy_skip_refcnt += (page->pv_list != NULL || page->ref_count != 1);
        if (page->object == NULL && page->wire_count == 0) {
            if (page->ref_count == 0) pmap_destroy_anon_rc0++;
            else if (page->ref_count == 2) pmap_destroy_anon_rc2++;
            else if (page->ref_count >= 3) pmap_destroy_anon_rc_big++;
        }
    }
}

/* Install the trampoline page read-only in `pmap` (no pv entry or hold:
 * it belongs to no process and is never freed). */
static int pmap_install_trampoline(pmap_t pmap) {
    pt_entry_t *pte;

    if (!trampoline_pa) {
        return 0;
    }
    pte = pmap_pte(pmap, (uintptr_t)SIG_TRAMPOLINE_ADDR, 1);
    if (!pte) {
        return -1;
    }
    *pte = trampoline_pa | PTE_P | PTE_U;
    return 0;
}

/* ==================== Bootstrap ==================== */

/* CPUID.80000001H:EDX bit 26: 1 GiB pages. */
static int pmap_cpu_has_1g_pages(void) {
    uint32_t eax = 0x80000000U, ebx, ecx = 0, edx;

    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    if (eax < 0x80000001U) {
        return 0;
    }
    eax = 0x80000001U;
    ecx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (edx >> 26) & 1;
}

/*
 * Extend the direct map from the 4 GiB boot.S built to `phys_end`, so
 * that the RAM above 4 GiB has P2V() addresses like the rest.
 *
 * boot.S gave DMAP_BASE one PDPT and filled its first four entries; the
 * rest of that table is ours to fill, one entry per GiB: a 1 GiB page
 * where the processor has them, otherwise a page directory of 2 MiB
 * pages taken from low memory.  The PDPT hangs off a kernel PML4 slot
 * that every address space shares, so nothing has to be propagated.
 *
 * Returns 0, or -1 if a page directory could not be allocated -- the
 * caller then leaves high memory unused.
 */
static int pmap_extend_dmap(uint64_t phys_end) {
    pt_entry_t *pdpt;
    int use_1g;

    if (phys_end <= PMAP_DMAP_LIMIT) {
        return 0;
    }
    if (phys_end > PMM_HIGHMEM_CAP) {
        phys_end = PMM_HIGHMEM_CAP;
    }

    pdpt = (pt_entry_t *)P2V(boot_pml4[PT_IDX(DMAP_BASE, PML4_SHIFT)] &
                             PTE_FRAME);
    use_1g = pmap_cpu_has_1g_pages();

    for (uint64_t pa = PMAP_DMAP_LIMIT; pa < phys_end; pa += PAGE_1G) {
        pt_entry_t *slot = &pdpt[PT_IDX(DMAP_BASE + pa, PDPT_SHIFT)];

        if (*slot & PTE_P) {
            continue;
        }
        if (use_1g) {
            *slot = pa | PTE_P | PTE_W | PTE_PS;
        } else {
            pt_entry_t *pd = (pt_entry_t *)pmm_alloc_block();

            if (!pd) {
                kprint("PMAP: out of memory extending the direct map\n");
                return -1;
            }
            for (int i = 0; i < 512; i++) {
                pd[i] = (pa + (uint64_t)i * PAGE_2M) | PTE_P | PTE_W | PTE_PS;
            }
            *slot = (pt_entry_t)V2P(pd) | PTE_P | PTE_W;
        }
    }
    kprintf("PMAP: direct map extended to %lu MiB (%s pages)\n",
            (unsigned long)(phys_end >> 20), use_1g ? "1 GiB" : "2 MiB");
    return 0;
}

void pmap_bootstrap(void) {
    kprint("PMAP: Bootstrapping (4-level, direct map at DMAP_BASE)...\n");

    kernel_pmap_store.pml4 = (pt_entry_t *)boot_pml4;
    kernel_pmap_store.pml4_phys = V2P(boot_pml4);
    kernel_pmap_store.ref_count = 1;

    /* The low identity window boot.S needed to reach the higher half is
     * gone by now (kmain64), so the lower half of the kernel pmap is empty
     * and stays that way. */
    pmap_activate(kernel_pmap_ptr);

    pmm_enable_highmem();
    kprint("PMAP: Paging Enabled (kernel at KERNEL_VMA, 4 GiB direct map)\n");

    /* RAM above 4 GiB: map it, then let the allocator have it. */
    if (pmap_extend_dmap(pmm_high_memory_end()) == 0) {
        pmm_add_high_memory();
    }
}

/* Page 0 is never mapped on this kernel: the identity window that held it
 * is removed before the first process exists.  Nothing to do either way. */
void pmap_null_protect(void) {
}

void pmap_null_allow(int enable) {
    (void)enable;
}

pmap_t pmap_kernel(void) {
    return kernel_pmap_ptr;
}

uint32_t pmap_resident_count(pmap_t pmap) {
    return pmap ? pmap->resident_count : 0;
}

/* ==================== Address spaces ==================== */

pmap_t pmap_create(void) {
    pmap_create_calls++;

    pmap_t pmap = (pmap_t)pmm_alloc_block();
    if (!pmap) return NULL;
    memset(pmap, 0, sizeof(*pmap));

    pt_entry_t *pml4 = (pt_entry_t *)pmm_alloc_block();
    if (!pml4) {
        pmm_free_block(pmap);
        return NULL;
    }
    memset(pml4, 0, PAGE_4K);

    /* The kernel half is shared: every slot of it is populated at boot, so
     * copying the entries now is enough forever. */
    for (int i = USER_PML4_SLOTS; i < 512; i++) {
        pml4[i] = kernel_pmap_ptr->pml4[i];
    }

    pmap->pml4 = pml4;
    pmap->pml4_phys = V2P(pml4);
    pmap->ref_count = 1;

    if (pmap_install_trampoline(pmap) != 0) {
        pmm_free_block(pml4);
        pmm_free_block(pmap);
        return NULL;
    }

    pmap_list_add(pmap);
    return pmap;
}

void pmap_destroy(pmap_t pmap) {
    if (!pmap || pmap == kernel_pmap_ptr) return;

    pmap_destroy_calls++;

    pmap->ref_count--;
    if (pmap->ref_count > 0) return;

    if (pmap_is_current(pmap)) {
        pmap_activate(kernel_pmap_ptr);
    }

    pt_entry_t *pml4 = pmap->pml4;
    for (int i4 = 0; i4 < USER_PML4_SLOTS; i4++) {
        if (!(pml4[i4] & PTE_P)) continue;
        pt_entry_t *pdpt = pmap_table(pml4[i4]);
        for (int i3 = 0; i3 < 512; i3++) {
            if (!(pdpt[i3] & PTE_P)) continue;
            pt_entry_t *pd = pmap_table(pdpt[i3]);
            for (int i2 = 0; i2 < 512; i2++) {
                if (!(pd[i2] & PTE_P)) continue;
                pt_entry_t *pt = pmap_table(pd[i2]);
                for (int i1 = 0; i1 < 512; i1++) {
                    if (!(pt[i1] & PTE_P)) continue;
                    uintptr_t va = ((uintptr_t)i4 << PML4_SHIFT) |
                                   ((uintptr_t)i3 << PDPT_SHIFT) |
                                   ((uintptr_t)i2 << PD_SHIFT) |
                                   ((uintptr_t)i1 << PT_SHIFT);
                    if (!pmap_is_trampoline(va)) {
                        pmap_release_page(pmap, va, pt[i1] & PTE_FRAME);
                    }
                    pt[i1] = 0;
                }
                pmm_free_block(pt);
                pd[i2] = 0;
            }
            pmm_free_block(pd);
            pdpt[i3] = 0;
        }
        pmm_free_block(pdpt);
        pml4[i4] = 0;
    }

    if (pmap->stats.cow_pages_mapped > pmap->stats.cow_duplications) {
        uint32_t saved = pmap->stats.cow_pages_mapped - pmap->stats.cow_duplications;
        __sync_fetch_and_add(&global_pmap_stats.pages_saved_by_cow, saved);
    }

    pmap_list_remove(pmap);
    pmm_free_block(pml4);
    pmm_free_block(pmap);
}

void pmap_reference(pmap_t pmap) {
    if (!pmap || pmap == kernel_pmap_ptr) return;
    __sync_fetch_and_add(&pmap->ref_count, 1);
}

void pmap_release(pmap_t pmap) {
    if (!pmap || pmap == kernel_pmap_ptr) return;
    int old_count = __sync_fetch_and_sub(&pmap->ref_count, 1);
    if (old_count == 1) {
        pmap->ref_count = 1; // Reset for pmap_destroy's decrement
        pmap_destroy(pmap);
    }
}

pmap_t pmap_fork(pmap_t src_pmap) {
    if (!src_pmap) return NULL;

    pmap_t dst_pmap = pmap_create();
    if (!dst_pmap) return NULL;

    pt_entry_t *pml4 = src_pmap->pml4;
    for (int i4 = 0; i4 < USER_PML4_SLOTS; i4++) {
        if (!(pml4[i4] & PTE_P)) continue;
        pt_entry_t *pdpt = pmap_table(pml4[i4]);
        for (int i3 = 0; i3 < 512; i3++) {
            if (!(pdpt[i3] & PTE_P)) continue;
            pt_entry_t *pd = pmap_table(pdpt[i3]);
            for (int i2 = 0; i2 < 512; i2++) {
                if (!(pd[i2] & PTE_P)) continue;
                pt_entry_t *pt = pmap_table(pd[i2]);
                for (int i1 = 0; i1 < 512; i1++) {
                    pt_entry_t src_pte = pt[i1];
                    if (!(src_pte & PTE_P)) continue;
                    uintptr_t va = ((uintptr_t)i4 << PML4_SHIFT) |
                                   ((uintptr_t)i3 << PDPT_SHIFT) |
                                   ((uintptr_t)i2 << PD_SHIFT) |
                                   ((uintptr_t)i1 << PT_SHIFT);
                    if (pmap_is_trampoline(va)) continue;  /* pmap_create did it */

                    pt_entry_t *dst = pmap_pte(dst_pmap, va, 1);
                    if (!dst) {
                        pmap_destroy(dst_pmap);
                        return NULL;
                    }

                    /* Both sides read-only: the first write on either
                     * faults into copy-on-write (pmap_fault). */
                    *dst = src_pte & ~PTE_W;
                    pt[i1] = src_pte & ~PTE_W;

                    vm_page_t *page = pmm_get_page(src_pte & PTE_FRAME);
                    if (page) {
                        vm_page_hold(page);
                        pv_insert(page, dst_pmap, va);
                    }
                    pmap_count_map_add(dst_pmap, 1);
                    dst_pmap->stats.cow_pages_mapped++;
                    src_pmap->stats.cow_pages_mapped++;
                }
            }
        }
    }

    /* The parent's mappings just lost their write bit: flush the stale
     * writable TLB entries so its next write faults into COW. */
    if (pmap_is_current(src_pmap)) {
        pmap_shootdown_all();
    }
    return dst_pmap;
}

void pmap_share_range(pmap_t pmap, uintptr_t start, uintptr_t end) {
    if (!pmap) return;

    for (uintptr_t va = start & ~(PAGE_4K - 1); va < end && pmap_va_is_user(va);
         va += PAGE_4K) {
        pt_entry_t *pte = pmap_pte(pmap, va, 0);
        if (pte && (*pte & PTE_P)) {
            *pte |= PTE_W;
        }
    }
}

/* Every kernel PML4 slot is populated at boot and shared by reference. */
void pmap_growkernel(uintptr_t va) {
    (void)va;
}

void pmap_activate(pmap_t pmap) {
    if (!pmap) return;

    curpmap = pmap;
    if ((pmap_read_cr3() & PTE_FRAME) != pmap->pml4_phys) {
        pmap_write_cr3(pmap->pml4_phys);
    }
}

/* ==================== Mappings ==================== */

int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    if (!pmap) {
        return -1;
    }
    va &= ~(PAGE_4K - 1);

    pt_entry_t *pte = pmap_pte(pmap, va, 1);
    if (!pte) {
        return -1;
    }

    pt_entry_t old_pte = *pte;
    uintptr_t old_pa = old_pte & PTE_FRAME;
    uintptr_t new_pa = pa & ~(PAGE_4K - 1);

    if (old_pte & PTE_P) {
        vm_page_t *old_page = pmm_get_page(old_pa);
        if (old_page && old_pa != new_pa) {
            pv_remove(old_page, pmap, va);
            vm_page_unhold(old_page);
        }
    } else {
        pmap_count_map_add(pmap, 1);
    }

    *pte = new_pa | pmap_pte_flags(va, prot, flags);

    vm_page_t *new_page = pmm_get_page(new_pa);
    if (new_page && (old_pa != new_pa || !(old_pte & PTE_P))) {
        vm_page_hold(new_page);
        pv_insert(new_page, pmap, va);
    }
    if (pmap_is_current(pmap)) {
        pmap_invalidate_page(va);
    }
    return 0;
}

int pmap_enter_batch(pmap_t pmap, uintptr_t va_start, int count, uintptr_t *pa_list,
                     uint32_t prot, uint32_t flags) {
    if (!pmap || count < 0 || !pa_list) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (pmap_enter(pmap, va_start + (uintptr_t)i * PAGE_4K, pa_list[i],
                       prot, flags) != 0) {
            /* Leave nothing half-done: undo what this call entered. */
            for (int j = 0; j < i; j++) {
                pmap_remove(pmap, va_start + (uintptr_t)j * PAGE_4K);
            }
            return -1;
        }
    }
    return 0;
}

/*
 * The i386 interface maps a 4 MiB superpage here.  Long mode has 2 MiB
 * ones, which would not cover the same range, so the range is mapped with
 * 4 KiB pages instead -- the same translation, and every page gets the
 * pv entry and hold the removal paths expect.
 */
int pmap_enter_large(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    if ((va & 0x3FFFFF) || (pa & 0x3FFFFF)) return -1;
    for (uintptr_t off = 0; off < 0x400000; off += PAGE_4K) {
        if (pmap_enter(pmap, va + off, pa + off, prot, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

void pmap_remove(pmap_t pmap, uintptr_t va) {
    if (!pmap) return;

    pt_entry_t *pte = pmap_pte(pmap, va, 0);
    if (!pte || !(*pte & PTE_P)) return;

    vm_page_t *page = pmm_get_page(*pte & PTE_FRAME);
    if (page) {
        pv_remove(page, pmap, va & ~(PAGE_4K - 1));
        vm_page_unhold(page);
    }
    *pte = 0;
    pmap_count_map_remove(pmap, 1);
    if (pmap_is_current(pmap)) {
        pmap_invalidate_page(va);
    }
}

/* Clear device-mapping PTEs in [sva, eva) without pv or hold bookkeeping
 * (see the i386 pmap_remove_range for why). */
void pmap_remove_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) {
    uint32_t cleared = 0;
    int is_current;

    if (!pmap || eva <= sva) return;
    is_current = pmap_is_current(pmap);

    for (uintptr_t va = sva & ~(PAGE_4K - 1); va < eva; va += PAGE_4K) {
        pt_entry_t *pte = pmap_pte(pmap, va, 0);
        if (!pte || !(*pte & PTE_P)) continue;
        *pte = 0;
        cleared++;
        if (is_current) pmap_invalidate_page(va);
    }
    pmap_count_map_remove(pmap, cleared);
}

/* Undo pmap_fork()'s COW clones over [sva, eva) in a fresh child (see the
 * i386 pmap_fork_clear_range). */
void pmap_fork_clear_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) {
    uint32_t cleared = 0;
    int is_current;

    if (!pmap || eva <= sva) return;
    is_current = pmap_is_current(pmap);

    for (uintptr_t va = sva & ~(PAGE_4K - 1); va < eva; va += PAGE_4K) {
        pt_entry_t *pte = pmap_pte(pmap, va, 0);
        if (!pte || !(*pte & PTE_P) || pmap_is_trampoline(va)) continue;
        vm_page_t *page = pmm_get_page(*pte & PTE_FRAME);
        if (page) {
            pv_remove(page, pmap, va);
            vm_page_unhold(page);
        }
        *pte = 0;
        cleared++;
        if (is_current) pmap_invalidate_page(va);
    }
    pmap_count_map_remove(pmap, cleared);
}

void pmap_kenter(uintptr_t va, uintptr_t pa) {
    pt_entry_t *pte = pmap_pte(kernel_pmap_ptr, va, 1);
    if (!pte) return;  // OOM - should not happen for kernel
    *pte = (pa & ~(PAGE_4K - 1)) | PTE_P | PTE_W |
           ((pmap_read_cr4() & CR4_PGE) ? PTE_G : 0);
    pmap_invalidate_page(va);
}

void pmap_kremove(uintptr_t va) {
    pt_entry_t *pte = pmap_pte(kernel_pmap_ptr, va, 0);
    if (!pte) return;
    *pte = 0;
    pmap_invalidate_page(va);
}

uintptr_t pmap_extract(pmap_t pmap, uintptr_t va) {
    return pmap ? pmap_translate(pmap, va) : 0;
}

static size_t pmap_copy_other(pmap_t pmap, uintptr_t uva, uint8_t *buf, size_t len,
                              int to_user) {
    size_t done = 0;

    /* User space only: a caller-supplied address in the kernel half would
     * reach kernel memory through the shared kernel entries. */
    if (!pmap || uva >= USER32_VA_END || len > USER32_VA_END - uva) {
        return 0;
    }
    while (done < len) {
        uintptr_t va = uva + done;
        uintptr_t pa = pmap_translate(pmap, va);
        size_t chunk;

        if (pa == 0 || pa >= PMAP_DMAP_LIMIT) {
            break;
        }
        chunk = PAGE_4K - (va & (PAGE_4K - 1));
        if (chunk > len - done) {
            chunk = len - done;
        }
        if (to_user) {
            memcpy(P2V(pa), buf + done, chunk);
        } else {
            memcpy(buf + done, P2V(pa), chunk);
        }
        done += chunk;
    }
    return done;
}

size_t pmap_copyin_other(pmap_t pmap, uintptr_t uva, void *dst, size_t len) {
    return pmap_copy_other(pmap, uva, (uint8_t *)dst, len, 0);
}

/* The write lands on the backing physical page directly: it bypasses PTE
 * write protection and does not break copy-on-write (ptrace POKE). */
size_t pmap_copyout_other(pmap_t pmap, uintptr_t uva, const void *src, size_t len) {
    return pmap_copy_other(pmap, uva, (uint8_t *)(uintptr_t)src, len, 1);
}

static int pmap_page_mapping_count(vm_page_t *page) {
    int count = 0;
    for (struct pv_entry *pv = page ? page->pv_list : NULL; pv; pv = pv->next) {
        count++;
    }
    return count;
}

int pmap_protect(pmap_t pmap, uintptr_t sva, uintptr_t eva, uint32_t prot) {
    uint32_t pages_modified = 0;
    int is_current;

    if (!pmap) return -1;
    is_current = pmap_is_current(pmap);

    for (uintptr_t va = sva & ~(PAGE_4K - 1); va < eva; va += PAGE_4K) {
        pt_entry_t *pte = pmap_pte(pmap, va, 0);
        if (!pte || !(*pte & PTE_P)) continue;

        pt_entry_t old_pte = *pte;
        int was_writable = (old_pte & PTE_W) != 0;
        int wants_writable = (prot & VM_PROT_WRITE) != 0;

        /* Upgrading a page several address spaces map: the caller must
         * copy it first. */
        if (!was_writable && wants_writable &&
            pmap_page_mapping_count(pmm_get_page(old_pte & PTE_FRAME)) > 1) {
            return -11; // -EAGAIN: COW copy required
        }

        pt_entry_t new_pte = old_pte & (PTE_FRAME | PTE_A | PTE_D | PTE_G |
                                        PTE_PWT | PTE_PCD | PTE_PAT);
        new_pte |= PTE_P;
        if (wants_writable) new_pte |= PTE_W;
        if (pmap_va_is_user(va)) new_pte |= PTE_U;
        *pte = new_pte;

        if (!was_writable && wants_writable) {
            pmap->stats.protection_upgrades++;
        } else if (was_writable && !wants_writable) {
            pmap->stats.protection_downgrades++;
        }
        pages_modified++;
        if (is_current && pages_modified <= TLB_BATCH_THRESHOLD) {
            pmap_invalidate_page(va);
        }
    }
    if (is_current && pages_modified > TLB_BATCH_THRESHOLD) {
        pmap_invalidate_all();
    }
    return 0;
}

int pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, uintptr_t sva, uintptr_t eva, int cow) {
    if (!dst_pmap || !src_pmap) return -1;
    int src_current = pmap_is_current(src_pmap);

    for (uintptr_t va = sva & ~(PAGE_4K - 1); va < eva; va += PAGE_4K) {
        pt_entry_t *src = pmap_pte(src_pmap, va, 0);
        if (!src || !(*src & PTE_P)) continue;

        pt_entry_t src_pte = *src;
        uintptr_t page_pa = src_pte & PTE_FRAME;
        vm_page_t *page = pmm_get_page(page_pa);
        int is_private = (page && (page->flags & PG_PRIVATE));

        pt_entry_t *dst = pmap_pte(dst_pmap, va, 1);
        if (!dst) return -1;

        if (is_private) {
            /* Private mapping: give the destination its own copy. */
            void *new_page_virt = pmm_alloc_block();
            if (!new_page_virt) return -1;
            memcpy(new_page_virt, P2V(page_pa), PAGE_4K);
            uintptr_t new_page_phys = V2P(new_page_virt);
            *dst = new_page_phys | (src_pte & (PAGE_4K - 1));
            pmap_count_map_add(dst_pmap, 1);
            vm_page_t *new_pg = pmm_get_page(new_page_phys);
            if (new_pg) {
                new_pg->flags |= PG_PRIVATE;
                pv_insert(new_pg, dst_pmap, va);
            }
        } else {
            pt_entry_t dst_pte = src_pte;
            if (cow && (src_pte & PTE_W)) {
                dst_pte &= ~PTE_W;
                *src &= ~PTE_W;
                if (src_current) pmap_invalidate_page(va);
            }
            if (page) {
                vm_page_hold(page);
                pv_insert(page, dst_pmap, va);
            }
            *dst = dst_pte;
            pmap_count_map_add(dst_pmap, 1);
        }
    }
    return 0;
}

int pmap_page_is_cow(pmap_t pmap, uintptr_t va) {
    if (!pmap) return 0;
    pt_entry_t *pte = pmap_pte(pmap, va, 0);
    if (!pte || !(*pte & PTE_P) || (*pte & PTE_W)) return 0;

    vm_page_t *page = pmm_get_page(*pte & PTE_FRAME);
    return page && page->ref_count > 1;
}

/* ==================== TLB ==================== */

void pmap_invalidate_page(uintptr_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
    __sync_fetch_and_add(&global_pmap_stats.tlb_invlpg_count, 1);
}

void pmap_invalidate_all(void) {
    pmap_write_cr3(pmap_read_cr3());
    __sync_fetch_and_add(&global_pmap_stats.tlb_full_flush_count, 1);
}

void pmap_flush_global_pages(void) {
    uint64_t cr4 = pmap_read_cr4();

    if (cr4 & CR4_PGE) {
        pmap_write_cr4(cr4 & ~(uint64_t)CR4_PGE);
        pmap_write_cr3(pmap_read_cr3());
        pmap_write_cr4(cr4);
    } else {
        pmap_invalidate_all();
    }
    __sync_fetch_and_add(&global_pmap_stats.tlb_full_flush_count, 1);
}

/*
 * TLB shootdown.  The x86_64 kernel runs on the boot processor only
 * (smp.c), so a shootdown is the local invalidation; the deferred-batch
 * interface is kept so callers need not know.
 */
void pmap_shootdown_handler(void) {
    pmap_invalidate_all();
    lapic_send_eoi();
}

void pmap_shootdown_page(uintptr_t va) {
    pmap_invalidate_page(va);
}

void pmap_shootdown_range(uintptr_t va, uint32_t len) {
    for (uintptr_t addr = va; addr < va + len; addr += PAGE_4K) {
        pmap_invalidate_page(addr);
    }
}

void pmap_shootdown_all(void) {
    pmap_invalidate_all();
}

static uintptr_t deferred_pages[16];
static int deferred_count = 0;
static spinlock_t deferred_lock = SPINLOCK_INIT("pmap_deferred");

void pmap_shootdown_defer(uintptr_t va) {
    spinlock_acquire(&deferred_lock);
    if (deferred_count < 16) {
        deferred_pages[deferred_count++] = va;
        spinlock_release(&deferred_lock);
    } else {
        deferred_count = 0;
        spinlock_release(&deferred_lock);
        pmap_shootdown_all();
    }
}

void pmap_shootdown_commit(void) {
    uintptr_t pages[16];
    int count;

    spinlock_acquire(&deferred_lock);
    count = deferred_count;
    for (int i = 0; i < count; i++) pages[i] = deferred_pages[i];
    deferred_count = 0;
    spinlock_release(&deferred_lock);

    if (count > 4) {
        pmap_shootdown_all();
    } else {
        for (int i = 0; i < count; i++) pmap_shootdown_page(pages[i]);
    }
}

void pmap_shootdown_wait(uint32_t gen) {
    (void)gen;
}

/* ==================== Reference and modify tracking ==================== */

static pt_entry_t *pmap_present_pte(pmap_t pmap, uintptr_t va) {
    pt_entry_t *pte = pmap ? pmap_pte(pmap, va, 0) : NULL;
    return (pte && (*pte & PTE_P)) ? pte : NULL;
}

int pmap_is_referenced(pmap_t pmap, uintptr_t va) {
    pt_entry_t *pte = pmap_present_pte(pmap, va);
    return pte && (*pte & PTE_A);
}

int pmap_is_modified(pmap_t pmap, uintptr_t va) {
    pt_entry_t *pte = pmap_present_pte(pmap, va);
    return pte && (*pte & PTE_D);
}

static int pmap_test_and_clear_bit(pmap_t pmap, uintptr_t va, pt_entry_t bit) {
    pt_entry_t *pte = pmap_present_pte(pmap, va);
    if (!pte || !(*pte & bit)) {
        return 0;
    }
    *pte &= ~bit;
    if (pmap_is_current(pmap)) {
        pmap_invalidate_page(va);
    }
    return 1;
}

void pmap_clear_reference(pmap_t pmap, uintptr_t va) {
    (void)pmap_test_and_clear_bit(pmap, va, PTE_A);
}

void pmap_clear_modify(pmap_t pmap, uintptr_t va) {
    (void)pmap_test_and_clear_bit(pmap, va, PTE_D);
}

int pmap_page_is_referenced(vm_page_t *m) {
    for (struct pv_entry *pv = m ? m->pv_list : NULL; pv; pv = pv->next) {
        if (pmap_is_referenced(pv->pmap, pv->va)) {
            return 1;
        }
    }
    return 0;
}

void pmap_page_clear_reference(vm_page_t *m) {
    for (struct pv_entry *pv = m ? m->pv_list : NULL; pv; pv = pv->next) {
        pmap_clear_reference(pv->pmap, pv->va);
    }
}

int pmap_is_referenced_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) {
    int n = 0;
    for (uintptr_t va = sva; pmap && va < eva; va += PAGE_4K) {
        n += pmap_is_referenced(pmap, va);
    }
    return n;
}

int pmap_is_modified_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) {
    int n = 0;
    for (uintptr_t va = sva; pmap && va < eva; va += PAGE_4K) {
        n += pmap_is_modified(pmap, va);
    }
    return n;
}

void pmap_track_access(vm_page_t *m) {
    int was_accessed = 0;

    for (struct pv_entry *pv = m ? m->pv_list : NULL; pv; pv = pv->next) {
        if (pmap_test_and_clear_bit(pv->pmap, pv->va, PTE_A)) {
            was_accessed = 1;
        }
    }
    if (was_accessed && m->access_count < 0xFFFF) {
        m->access_count++;
    }
}

int pmap_test_and_clear_ref(vm_page_t *m) {
    int was = 0;
    for (struct pv_entry *pv = m ? m->pv_list : NULL; pv; pv = pv->next) {
        if (pmap_test_and_clear_bit(pv->pmap, pv->va, PTE_A)) {
            was = 1;
        }
    }
    return was;
}

int pmap_test_and_clear_modify(vm_page_t *m) {
    int was = 0;
    for (struct pv_entry *pv = m ? m->pv_list : NULL; pv; pv = pv->next) {
        if (pmap_test_and_clear_bit(pv->pmap, pv->va, PTE_D)) {
            was = 1;
        }
    }
    return was;
}

void pmap_track_modify(vm_page_t *m, uint32_t current_time) {
    if (m && pmap_test_and_clear_modify(m)) {
        m->last_modified = current_time;
    }
}

/* ==================== Faults ==================== */

/* Copy-on-write: a write to a present, read-only user page.  The policy --
 * when the sole mapper may simply regain write access, and the pv and
 * hold bookkeeping of the copy -- is the i386 pmap_fault()'s. */
int pmap_fault(uint32_t err_code, uintptr_t cr2) {
    if ((err_code & 0x03) != 0x03 || !current_process || !current_process->pmap) {
        return 0;
    }
    pmap_t pmap = current_process->pmap;
    uintptr_t va = cr2 & ~(PAGE_4K - 1);

    pt_entry_t *pte = pmap_present_pte(pmap, va);
    if (!pte || (*pte & PTE_W) || !(*pte & PTE_U)) {
        return 0;
    }

    uintptr_t phys_old = *pte & PTE_FRAME;
    vm_page_t *page_old = pmm_get_page(phys_old);
    if (!page_old) return 0;

    int single_mapper_safe = (page_old->ref_count <= 2) &&
        (page_old->object == NULL ||
         page_old->object->type == VM_OBJ_TYPE_DEFAULT);
    if (single_mapper_safe) {
        *pte |= PTE_W;
        pmap_invalidate_page(va);
        return 1;
    }

    void *virt_new = pmm_alloc_block();
    if (!virt_new) {
        kprint("pmap_fault: OOM during COW\n");
        return 0;
    }
    memcpy(virt_new, P2V(phys_old), PAGE_4K);

    uintptr_t phys_new = V2P(virt_new);
    vm_page_t *page_new = pmm_get_page(phys_new);

    pv_remove(page_old, pmap, va);
    vm_page_unhold(page_old);
    if (page_new) {
        vm_page_hold(page_new);
        pv_insert(page_new, pmap, va);
    }
    *pte = phys_new | PTE_P | PTE_W | PTE_U | PTE_A | PTE_D;
    pmap_invalidate_page(va);

    pmap_stat_inc(pmap, offsetof(struct pmap_stats, cow_faults));
    pmap_stat_inc(pmap, offsetof(struct pmap_stats, cow_duplications));
    pmap_stat_inc(pmap, offsetof(struct pmap_stats, faults));
    return 1;
}

/* ==================== Signal trampoline ==================== */

void pmap_map_trampoline(void) {
    void *page = pmm_alloc_block();
    if (!page) {
        panic("PMAP: Failed to allocate trampoline page");
    }
    memset(page, 0, PAGE_4K);
    memcpy(page, sig_trampoline_code, sig_trampoline_size);
    trampoline_pa = V2P(page);

    /* Into every address space that already exists; pmap_create() does
     * the rest. */
    while (__sync_lock_test_and_set(&pmap_list_lock, 1)) {
        __asm__ volatile("pause");
    }
    for (pmap_t p = pmap_list_head; p; p = p->list_entry.next) {
        if (pmap_install_trampoline(p) != 0) {
            panic("PMAP: Failed to map trampoline page");
        }
    }
    __sync_lock_release(&pmap_list_lock);

    kprintf("PMAP: Mapped Signal Trampoline at 0x%08x\n",
            (unsigned int)SIG_TRAMPOLINE_ADDR);
}

/* ==================== Statistics and diagnostics ==================== */

static uint32_t pmap_count_active(void) {
    uint32_t total = 0;
    FOREACH_THREAD(t) { (void)t; total++; }
    if (total == 0) {
        return 0;
    }

    pmap_t *seen = kmalloc(total * sizeof(*seen));
    if (!seen) {
        return 0;
    }

    uint32_t seen_count = 0;
    FOREACH_THREAD(thread) {
        if (thread->state == THREAD_ZOMBIE || !thread->proc || !thread->proc->pmap) {
            continue;
        }
        pmap_t pmap = thread->proc->pmap;
        int duplicate = 0;
        for (uint32_t j = 0; j < seen_count; j++) {
            if (seen[j] == pmap) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate && seen_count < total) {
            seen[seen_count++] = pmap;
        }
    }

    kfree(seen, total * sizeof(*seen));
    return seen_count;
}

int sys_pmap_stats(struct pmap_stats *out) {
    struct pmap_stats stats;

    if (!out) return -EFAULT;

    stats = global_pmap_stats;
    stats.active_pmaps = pmap_count_active();

    /* Kernel callers may pass a kernel pointer; user callers get copyout. */
    if ((uintptr_t)out >= KERNEL_VA_START) {
        *out = stats;
        return 0;
    }
    if (copyout(&stats, out, sizeof(stats)) != 0) {
        return -EFAULT;
    }
    return 0;
}

void pmap_copy_page(uintptr_t src_pa, uintptr_t dst_pa) {
    memcpy(P2V(dst_pa), P2V(src_pa), PAGE_4K);
}

void pmap_zero_page(uintptr_t pa) {
    memset(P2V(pa), 0, PAGE_4K);
}

void pmap_dump(pmap_t pmap) {
    if (!pmap) {
        kprint("pmap_dump: NULL pmap\n");
        return;
    }
    kprintf("pmap_dump: pml4 phys 0x%lx, %u resident\n",
            (unsigned long)pmap->pml4_phys, pmap->resident_count);
    for (int i4 = 0; i4 < USER_PML4_SLOTS; i4++) {
        if (!(pmap->pml4[i4] & PTE_P)) continue;
        pt_entry_t *pdpt = pmap_table(pmap->pml4[i4]);
        for (int i3 = 0; i3 < 512; i3++) {
            if (!(pdpt[i3] & PTE_P)) continue;
            pt_entry_t *pd = pmap_table(pdpt[i3]);
            for (int i2 = 0; i2 < 512; i2++) {
                if (!(pd[i2] & PTE_P)) continue;
                pt_entry_t *pt = pmap_table(pd[i2]);
                int n = 0;
                for (int i1 = 0; i1 < 512; i1++) {
                    n += (pt[i1] & PTE_P) != 0;
                }
                kprintf("  %016lx: %d pages\n",
                        ((unsigned long)i4 << PML4_SHIFT) |
                        ((unsigned long)i3 << PDPT_SHIFT) |
                        ((unsigned long)i2 << PD_SHIFT), n);
            }
        }
    }
}

int pmap_check(pmap_t pmap) {
    int errors = 0;

    if (!pmap) return -1;
    if (pmap->pml4_phys & (PAGE_4K - 1)) {
        kprint("pmap_check: PML4 not page-aligned\n");
        errors++;
    }
    if (pmap->ref_count <= 0) {
        kprint("pmap_check: Invalid ref_count\n");
        errors++;
    }
    for (int i = USER_PML4_SLOTS; i < 512; i++) {
        if (pmap->pml4[i] != kernel_pmap_ptr->pml4[i]) {
            kprint("pmap_check: Kernel PML4 entry mismatch\n");
            errors++;
            break;
        }
    }
    if (errors == 0) {
        kprint("pmap_check: OK\n");
    }
    return errors ? -errors : 0;
}

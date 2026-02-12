#define HOST_TEST

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/queue.h> // Host system usually has this or we use the kernel one
// Since we use -I sys/include, we use the kernel one.

// Mock includes
#include <arch/x86_64/pmap.h>

// Mock Data
static pml4e_t mock_pml4[512];
static pdpte_t mock_pdpt[512];
static pde_t   mock_pd[512];
static pte_t   mock_pt[512];

// Reset mock tables
void reset_mocks() {
    memset(mock_pml4, 0, sizeof(mock_pml4));
    memset(mock_pdpt, 0, sizeof(mock_pdpt));
    memset(mock_pd, 0, sizeof(mock_pd));
    memset(mock_pt, 0, sizeof(mock_pt));

    // Link them up for VA 0 (indices 0,0,0,0)
    // We assume the macros cast these pointers to uint64_t when needed or used as addresses
    // In pmap logic: `if (pd[pdi] & PTE_P)` -> `pd[pdi]` is a u64.
    // We need to store valid "physical" addresses (pointers) in the tables.

    // Note: The recursive mapping macros I defined return the ADDRESS of the entry.
    // The pmap.c code reads the entry value: `if (V_PD[pdi] & PTE_P)`
    // Wait, `V_PD_INDEX` returns `pde_t*`.
    // `pde_t *pd = (pde_t*)V_PD_INDEX(...)`
    // `if (pd[pdi] & PTE_P)`

    // We need to make sure that walking the table works.
    // pmap.c:
    // `pdpte_t *pdpt = V_PDPT(pml4i);` -> Returns `&mock_pdpt[0]` (base of array)
    // `if (!(pdpt[pdpti] & PTE_P))`

    // So `mock_pdpt[0]` must contain P bit and address of next level.
    // But `V_PD_INDEX` short circuits the address lookup in my mock.
    // So the values inside the tables (phys pointers) are only used if `pmap.c` tries to
    // convert them to virtual addresses manually (e.g. `pmap_ptokv`).
    // `pmap_protect` uses `V_PT_INDEX` etc. macros directly.
    // It DOES verify the P bit of the *entry* before descending.

    // So we need to set the P bit in the entries of the upper levels.

    mock_pml4[0] = PTE_P | PTE_W | PTE_U; // Address doesn't matter for mock macros
    mock_pdpt[0] = PTE_P | PTE_W | PTE_U;
    mock_pd[0]   = PTE_P | PTE_W | PTE_U;
}

// Mock Accessors
pml4e_t* mock_get_pml4_entry(int i) {
    if (i < 0 || i >= 512) return NULL;
    return &mock_pml4[i];
}

pdpte_t* mock_get_pdpt_entry(int i, int j) {
    if (i == 0 && j >= 0 && j < 512) return &mock_pdpt[j];
    return NULL;
}

pde_t* mock_get_pd_entry(int i, int j, int k) {
    if (i == 0 && j == 0 && k >= 0 && k < 512) return &mock_pd[k];
    return NULL;
}

pte_t* mock_get_pt_entry(int i, int j, int k, int l) {
    if (i == 0 && j == 0 && k == 0 && l >= 0 && l < 512) return &mock_pt[l];
    return NULL;
}

// Mock Kernel Functions
void *pmm_alloc_block(void) { return malloc(4096); }
void pmm_free_block(void *p) { free(p); }
void *kmalloc(size_t sz) { return malloc(sz); }
void kfree(void *p, size_t sz) { free(p); }

void spinlock_init(spinlock_t *l, const char *n) { (void)l; (void)n; }
void spinlock_acquire(spinlock_t *l) { (void)l; }
void spinlock_release(spinlock_t *l) { (void)l; }

void kprint(const char *msg) { printf("%s", msg); }

uint64_t pmap_rdmsr(uint32_t msr) { (void)msr; return 0; }
void pmap_wrmsr(uint32_t msr, uint64_t val) { (void)msr; (void)val; }
void lapic_send_eoi(void) {}
void lapic_send_ipi_all_excl_self(int vector) { (void)vector; }

uint64_t boot_pml4[512];

// Include the source file to test
// We need to define included headers that might be missing or problematic
#include <sys/arch/x86_64/pmap.c>

void test_protect_4kb() {
    printf("Test: pmap_protect 4KB page\n");
    reset_mocks();

    // Setup a 4KB page at VA 0
    uint64_t pa = 0x1000;
    mock_pt[0] = pa | PTE_P | PTE_W | PTE_NX; // Initially RW, NX

    // Change to RX (Read-Exec) -> Clear W, Set nothing (NX clear)
    // prot = VM_PROT_READ | VM_PROT_EXEC (5)
    pmap_t p = (pmap_t)malloc(sizeof(struct pmap));
    memset(p, 0, sizeof(struct pmap));
    curpmap = p;

    pmap_protect(p, 0, 0x1000, 5);

    int failed = 0;
    if (mock_pt[0] & PTE_W) { printf("  FAIL: PTE_W should be cleared\n"); failed=1; }
    if (mock_pt[0] & PTE_NX) { printf("  FAIL: PTE_NX should be cleared\n"); failed=1; }

    if (!failed) printf("  PASS\n");
    free(p);
}

void test_protect_2mb() {
    printf("Test: pmap_protect 2MB page\n");
    reset_mocks();

    // Setup a 2MB page at VA 2MB (index 1 in PD)
    // VA = 0x200000
    uint64_t pa = 0x200000;
    mock_pd[1] = pa | PTE_P | PTE_W | PTE_PS | PTE_NX; // Large page, RW, NX

    pmap_t p = (pmap_t)malloc(sizeof(struct pmap));
    memset(p, 0, sizeof(struct pmap));
    curpmap = p;

    pmap_protect(p, 0x200000, 0x400000, 5);

    int failed = 0;
    if (mock_pd[1] & PTE_W) { printf("  FAIL: PDE_W should be cleared (is %lx)\n", mock_pd[1]); failed=1; }
    if (mock_pd[1] & PTE_NX) { printf("  FAIL: PDE_NX should be cleared (is %lx)\n", mock_pd[1]); failed=1; }

    if (!failed) printf("  PASS\n");
    free(p);
}

int main() {
    test_protect_4kb();
    test_protect_2mb();
    return 0;
}

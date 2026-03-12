#include <kern/resource.h>
#include <string.h>
#include <vm/vm_kmem.h>

#ifndef HOST_TEST
#include <arch/i386/pmap.h>
#else
typedef void *pmap_t;
#define VM_PROT_READ  0x01
#define VM_PROT_WRITE 0x02
#define PTE_PCD       0x10
extern pmap_t pmap_kernel(void);
extern int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
extern void pmap_kremove(uintptr_t va);
#endif

#define IOREMAP_BASE  0xF7000000U
#define IOREMAP_LIMIT 0xF8000000U
#define PAGE_SIZE     4096U

struct ioremap_region {
    void *addr;
    uintptr_t va_base;
    resource_size_t phys_addr;
    size_t size;
    size_t map_size;
    int owns_resource;
    struct ioremap_region *next;
};

static struct ioremap_region *ioremap_regions;
static uintptr_t ioremap_next = IOREMAP_BASE;

static size_t page_round_up(size_t size) {
    return (size + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
}

static void *ioremap_internal(resource_size_t phys_addr, size_t size, struct resource *res) {
    struct ioremap_region *region;
    uintptr_t page_phys;
    uintptr_t va_base;
    size_t page_offset;
    size_t map_size;
    int owns_resource = 0;

    if (size == 0) {
        return NULL;
    }

    if (res == NULL) {
        res = request_mem_region(phys_addr, size, "ioremap");
        owns_resource = 1;
    }
    if (res == NULL) {
        return NULL;
    }

    page_phys = (uintptr_t)(phys_addr & ~(resource_size_t)(PAGE_SIZE - 1U));
    page_offset = (size_t)(phys_addr - page_phys);
    map_size = page_round_up(page_offset + size);
    va_base = ioremap_next;

    if (va_base + map_size > IOREMAP_LIMIT) {
        if (owns_resource) {
            release_mem_region(phys_addr, size);
        }
        return NULL;
    }

    region = kmalloc(sizeof(*region));
    if (region == NULL) {
        if (owns_resource) {
            release_mem_region(phys_addr, size);
        }
        return NULL;
    }
    memset(region, 0, sizeof(*region));

    for (size_t offset = 0; offset < map_size; offset += PAGE_SIZE) {
        if (pmap_enter(pmap_kernel(), va_base + offset, page_phys + offset,
                       VM_PROT_READ | VM_PROT_WRITE, PTE_PCD) != 0) {
            while (offset > 0) {
                offset -= PAGE_SIZE;
                pmap_kremove(va_base + offset);
            }
            kfree(region, sizeof(*region));
            if (owns_resource) {
                release_mem_region(phys_addr, size);
            }
            return NULL;
        }
    }

    region->addr = (void *)(va_base + page_offset);
    region->va_base = va_base;
    region->phys_addr = phys_addr;
    region->size = size;
    region->map_size = map_size;
    region->owns_resource = owns_resource;
    region->next = ioremap_regions;
    ioremap_regions = region;
    ioremap_next += map_size;
    return region->addr;
}

void *ioremap(resource_size_t phys_addr, size_t size) {
    return ioremap_internal(phys_addr, size, NULL);
}

void *ioremap_resource(struct resource *res, size_t max_len) {
    size_t size;

    if (res == NULL || res->type != RES_MEM) {
        return NULL;
    }

    size = (size_t)resource_size(res);
    if (max_len != 0 && size > max_len) {
        size = max_len;
    }
    return ioremap_internal(res->start, size, res);
}

void iounmap(void *addr) {
    struct ioremap_region *curr = ioremap_regions;
    struct ioremap_region *prev = NULL;

    while (curr != NULL) {
        if (curr->addr == addr) {
            for (size_t offset = 0; offset < curr->map_size; offset += PAGE_SIZE) {
                pmap_kremove(curr->va_base + offset);
            }
            if (curr->owns_resource) {
                release_mem_region(curr->phys_addr, curr->size);
            }

            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                ioremap_regions = curr->next;
            }
            kfree(curr, sizeof(*curr));
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

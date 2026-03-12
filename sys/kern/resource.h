/*
 * sys/kern/resource.h
 *
 * Core Data Structure: Resource
 * Represents a hardware resource (IO port, Memory range, IRQ, DMA).
 */

#ifndef _KERN_RESOURCE_H
#define _KERN_RESOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Resource Types */
#define RES_IO   0x00000001
#define RES_MEM  0x00000002
#define RES_IRQ  0x00000003
#define RES_DMA  0x00000004

/* Forward declarations */
struct device;

typedef uint64_t resource_size_t;

/*
 * struct resource
 *
 * Fields:
 * - type: Resource type (RES_*)
 * - start: Start address/number (inclusive)
 * - end: End address/number (inclusive)
 * - flags: Resource attribute flags
 * - parent: Parent resource (container)
 * - sibling: Next resource in the same container
 * - child: First child resource (sub-allocations)
 * - owner: Device that owns this resource
 */
struct resource {
    uint32_t type;
    resource_size_t start;
    resource_size_t end;
    uint32_t flags;
    const char *name;

    struct resource *parent;
    struct resource *sibling;
    struct resource *child;

    struct device *owner;
};

/* Helper Functions */

/* Check if r1 fully contains r2 */
static inline bool resource_contains(struct resource *r1, struct resource *r2) {
    if (r1->start <= r2->start && r1->end >= r2->end) {
        return true;
    }
    return false;
}

/* Check if r1 overlaps with r2 */
static inline bool resource_overlaps(struct resource *r1, struct resource *r2) {
    if (r1->start <= r2->end && r1->end >= r2->start) {
        return true;
    }
    return false;
}

/* Calculate size */
static inline resource_size_t resource_size(struct resource *r) {
    if (r->start > r->end) return 0;
    return r->end - r->start + 1;
}

void resource_init(void);
struct resource *resource_root(uint32_t type);
struct resource *resource_find(uint32_t type, resource_size_t start, resource_size_t n);
size_t resource_dump(uint32_t type, char *buf, size_t size);
struct resource *request_region(resource_size_t start, resource_size_t n, const char *name);
void release_region(resource_size_t start, resource_size_t n);
struct resource *request_mem_region(resource_size_t start, resource_size_t n, const char *name);
void release_mem_region(resource_size_t start, resource_size_t n);
void *ioremap(resource_size_t phys_addr, size_t size);
void *ioremap_resource(struct resource *res, size_t max_len);
void iounmap(void *addr);

#endif /* _KERN_RESOURCE_H */

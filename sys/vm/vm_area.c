#include "vm_area.h"
#include <arch/i386/pmm.h>
#include <string.h>

vm_area_t *vm_area_create(uint32_t start, uint32_t end, uint32_t prot, uint32_t flags) {
    vm_area_t *area = (vm_area_t *)pmm_alloc_block();
    if (!area) return NULL;
    
    // Initialize
    area->vm_start = start;
    area->vm_end = end;
    area->vm_prot = prot;
    area->vm_flags = flags;
    area->vm_file = NULL;
    area->vm_offset = 0;
    area->next = NULL;
    
    return area;
}

void vm_area_destroy(vm_area_t *area) {
    if (!area) return;
    pmm_free_block(area);
}

vm_area_t *vm_area_find(vm_area_t *head, uint32_t addr) {
    vm_area_t *curr = head;
    while (curr) {
        if (addr >= curr->vm_start && addr < curr->vm_end) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int vm_area_insert(vm_area_t **head, vm_area_t *new_area) {
    if (!head || !new_area) return -1;
    
    // Check for overlaps
    vm_area_t *curr = *head;
    while (curr) {
        // Check if ranges overlap
        if (!(new_area->vm_end <= curr->vm_start || new_area->vm_start >= curr->vm_end)) {
            return -1;  // Overlap detected
        }
        curr = curr->next;
    }
    
    // Insert at head (simple insertion)
    new_area->next = *head;
    *head = new_area;
    
    return 0;
}

void vm_area_remove(vm_area_t **head, vm_area_t *area) {
    if (!head || !area) return;
    
    vm_area_t **curr = head;
    while (*curr) {
        if (*curr == area) {
            *curr = area->next;
            return;
        }
        curr = &(*curr)->next;
    }
}

void vm_area_free_all(vm_area_t *head) {
    vm_area_t *curr = head;
    while (curr) {
        vm_area_t *next = curr->next;
        vm_area_destroy(curr);
        curr = next;
    }
}

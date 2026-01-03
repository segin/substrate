#ifndef _VM_AREA_H
#define _VM_AREA_H

#include <stdint.h>
#include "../vfs/vfs.h"

// VM area flags
#define VM_READ    0x1
#define VM_WRITE   0x2
#define VM_EXEC    0x4
#define VM_SHARED  0x8
#define VM_GROWSDOWN 0x10

// Virtual memory area descriptor
typedef struct vm_area {
    uint32_t vm_start;      // Start address (inclusive)
    uint32_t vm_end;        // End address (exclusive)
    uint32_t vm_prot;       // Protection flags (VM_READ, VM_WRITE, VM_EXEC)
    uint32_t vm_flags;      // Flags (VM_SHARED, etc.)
    
    // File backing (NULL for anonymous)
    fs_node_t *vm_file;
    uint32_t vm_offset;     // Offset in file
    
    struct vm_area *next;   // Linked list
} vm_area_t;

// VM area management
vm_area_t *vm_area_create(uint32_t start, uint32_t end, uint32_t prot, uint32_t flags);
void vm_area_destroy(vm_area_t *area);
vm_area_t *vm_area_find(vm_area_t *head, uint32_t addr);
int vm_area_insert(vm_area_t **head, vm_area_t *new_area);
void vm_area_remove(vm_area_t **head, vm_area_t *area);
void vm_area_free_all(vm_area_t *head);

#endif

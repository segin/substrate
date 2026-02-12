#include <exec/formats/coff.h>

#include <kern/console.h>
#include <exec/perso/personality.h>
#include <kern/sched.h>
#include <sys/sysinfo.h>
#include <pm/pm.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <sys/proc.h>
#include <arch/i386/pmap.h>

#define P2V(x) ((uintptr_t)(x) + 0xC0000000)

int coff_load_file(void *file, uint32_t size) {
    coff_filehdr_t *filehdr = (coff_filehdr_t *)file;

    if (filehdr->f_magic != COFF_MAGIC_I386) {
        return -1;
    }

    kprint("Loading COFF file...\n");

    // If it has an optional header, parse it for entry point
    if (filehdr->f_opthdr > 0) {
        coff_aouthdr_t *aouthdr = (coff_aouthdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t));
        // Entry point is aouthdr->entry
        char buf[64];
        sprintf(buf, "COFF: Entry point at 0x%08x\n", aouthdr->entry);
        kprint(buf);
    }

    // Identify and map sections
    coff_scnhdr_t *scnhdr = (coff_scnhdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t) + filehdr->f_opthdr);
    for (int i = 0; i < filehdr->f_nscns; i++) {
        char name[9];
        strncpy(name, scnhdr[i].s_name, 8);
        name[8] = '\0';
        char buf[64];
        sprintf(buf, "COFF: Mapping section %s\n", name);
        kprint(buf);
        
        // Use vm_map_insert to map section raw data
        uint32_t va_start = scnhdr[i].s_vaddr;
        uint32_t va_end = va_start + scnhdr[i].s_size;

        // Skip empty sections
        if (scnhdr[i].s_size == 0) continue;

        // Align to page boundaries
        uint32_t map_start = va_start & ~0xFFF;
        uint32_t map_end = (va_end + 0xFFF) & ~0xFFF;
        uint32_t map_size = map_end - map_start;

        if (map_size == 0) continue;

        // Determine permissions
        uint8_t prot = VM_PROT_USER; // Always allow user access for loaded sections
        if (scnhdr[i].s_flags & STYP_TEXT) prot |= VM_PROT_READ | VM_PROT_EXEC;
        if (scnhdr[i].s_flags & STYP_DATA) prot |= VM_PROT_READ | VM_PROT_WRITE;
        if (scnhdr[i].s_flags & STYP_BSS)  prot |= VM_PROT_READ | VM_PROT_WRITE;

        // Fallback default
        if (prot == VM_PROT_USER) prot |= VM_PROT_READ | VM_PROT_WRITE;

        // Allocate VM Object (Anonymous)
        vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, map_size);
        if (!obj) {
             kprint("COFF: Failed to allocate VM object\n");
             return -1;
        }

        // Insert into VM Map
        if (current_process && current_process->vm_map) {
             if (vm_map_insert(current_process->vm_map, obj, 0, map_start, map_end, prot, VM_PROT_ALL, VM_INHERIT_COPY) != 0) {
                 kprint("COFF: Failed to insert into vm_map (overlap?)\n");
                 vm_object_deallocate(obj);
                 // We might fail if sections overlap on pages. For now, fail hard.
                 return -1;
             }
        } else {
             kprint("COFF: No current process vm_map!\n");
             vm_object_deallocate(obj);
             return -1;
        }

        // Copy Data to Pages if not BSS and data pointer is valid
        if (scnhdr[i].s_scnptr != 0 && !(scnhdr[i].s_flags & STYP_BSS)) {
             if (scnhdr[i].s_scnptr < 0 || scnhdr[i].s_size < 0 ||
                 (uint32_t)scnhdr[i].s_scnptr + (uint32_t)scnhdr[i].s_size > size) {
                 kprint("COFF: Section data out of bounds\n");
                 // Cleanup allocated map/object ideally, but process load failure cleans up whole address space usually.
                 return -1;
             }

             uint8_t *file_data = (uint8_t *)file + scnhdr[i].s_scnptr;
             uint32_t data_size = scnhdr[i].s_size;

             // Iterate pages
             for (uint32_t offset = 0; offset < map_size; offset += 0x1000) {
                  uint32_t page_va = map_start + offset;

                  // Allocate page
                  vm_page_t *page = vm_page_alloc(obj, offset / 0x1000, 0);
                  if (!page) {
                       kprint("COFF: Page allocation failed\n");
                       return -1;
                  }

                  // Get kernel mapping for page physical address
                  void *page_kva = (void *)P2V(page->phys_addr);

                  // Zero the page first
                  memset(page_kva, 0, 0x1000);

                  // Determine overlap with file data
                  uint32_t page_start_va = page_va;
                  uint32_t page_end_va = page_va + 0x1000;

                  // Data range in VA
                  uint32_t data_start_va = va_start;
                  uint32_t data_end_va = va_start + data_size;

                  // Intersection
                  uint32_t copy_start = (page_start_va > data_start_va) ? page_start_va : data_start_va;
                  uint32_t copy_end = (page_end_va < data_end_va) ? page_end_va : data_end_va;

                  if (copy_start < copy_end) {
                       uint32_t copy_len = copy_end - copy_start;
                       uint32_t src_offset = copy_start - data_start_va;
                       uint32_t dst_offset = copy_start - page_start_va;

                       memcpy((uint8_t*)page_kva + dst_offset, file_data + src_offset, copy_len);
                  }

                  // Add page to object
                  vm_object_add_page(obj, page);

                  // Map into current pmap immediately
                  if (current_process && current_process->pmap) {
                      pmap_enter(current_process->pmap, page_va, page->phys_addr, prot, 0);
                  }
             }
        }
    }

    // Default to SVR3 for now if we detect a 386 COFF binary
    if (current_process) {
        current_process->perso_id = PERS_SVR3;
        proc_set_bitness(current_process, BITNESS_32);
    }

    kprint("COFF Loader invoked (header parsed).\n");

    return 0;
}


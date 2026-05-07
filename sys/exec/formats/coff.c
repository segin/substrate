#include <exec/formats/coff.h>

#ifndef HOST_TEST
#include <kern/console.h>
#include <exec/perso/personality.h>
#include <kern/sched.h>
#include <sys/sysinfo.h>
#include <pm/pm.h>
#include <stdio.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <sys/proc.h>
#include <arch/i386/pmap.h>
#endif

#include <string.h>

/*
 * Hard cap on per-segment size.  COFF stores sizes as int32_t; we further
 * reject anything that would overflow user-space layout calculations.  The
 * cap is intentionally well below 2 GiB so that text+data+bss arithmetic
 * cannot wrap a 32-bit address.
 */
#define COFF_MAX_SEGMENT_BYTES (1U << 30)  /* 1 GiB */

int coff_validate_filehdr(const coff_filehdr_t *fh, uint32_t file_size) {
    if (!fh) return -1;
    if (fh->f_magic != COFF_MAGIC_I386) return -1;
    if (fh->f_nscns == 0 || fh->f_nscns > 256) return -1;

    /* The optional header (if present) sits immediately after the file
     * header; the section table follows.  Bound both against file_size. */
    if (fh->f_opthdr > 4096) return -1;  /* sane upper bound */
    uint32_t scn_offset = (uint32_t)sizeof(coff_filehdr_t) + (uint32_t)fh->f_opthdr;
    if (scn_offset > file_size) return -1;

    uint32_t scn_table_bytes = (uint32_t)fh->f_nscns * (uint32_t)sizeof(coff_scnhdr_t);
    if (scn_offset + scn_table_bytes < scn_offset) return -1;  /* overflow */
    if (scn_offset + scn_table_bytes > file_size) return -1;

    return 0;
}

int coff_validate_aouthdr(const coff_aouthdr_t *opt) {
    if (!opt) return -1;

    /* REQ-05-0407, REQ-05-0408: only ZMAGIC (demand-paged) is accepted. */
    if (opt->magic != AOUT_ZMAGIC) return -1;

    /* REQ-05-0413: sizes must be non-negative and bounded.  Negative values
     * are nonsensical for an executable image; very large values are likely
     * corrupted headers and would overflow address arithmetic below. */
    if (opt->tsize < 0 || (uint32_t)opt->tsize > COFF_MAX_SEGMENT_BYTES) return -1;
    if (opt->dsize < 0 || (uint32_t)opt->dsize > COFF_MAX_SEGMENT_BYTES) return -1;
    if (opt->bsize < 0 || (uint32_t)opt->bsize > COFF_MAX_SEGMENT_BYTES) return -1;

    /* ZMAGIC requires page-aligned tsize so .text and .data live in disjoint
     * page frames — without this, demand paging would have to copy partial
     * pages on first fault.  dsize/bsize need only sub-page accuracy. */
    if ((uint32_t)opt->tsize & (COFF_PAGE_SIZE - 1U)) return -1;

    /* REQ-05-0420: text_start must be strictly below data_start when both
     * are present, and text+tsize must fit before data_start.  When the
     * binary is BSS-only (tsize == 0) we relax the ordering requirement. */
    if (opt->tsize > 0) {
        uint32_t text_end = (uint32_t)opt->text_start + (uint32_t)opt->tsize;
        if (text_end < (uint32_t)opt->text_start) return -1;  /* overflow */

        if (opt->dsize > 0 || opt->bsize > 0) {
            if ((uint32_t)opt->data_start < text_end) return -1;
        }

        /* REQ-05-0416: entry must fall within the text segment. */
        if ((uint32_t)opt->entry < (uint32_t)opt->text_start ||
            (uint32_t)opt->entry >= text_end) {
            return -1;
        }
    } else if (opt->dsize > 0 || opt->bsize > 0) {
        /* BSS-only / data-only image: there's no text to anchor entry in. */
        if (opt->entry != 0) return -1;
    }

    return 0;
}

#ifndef HOST_TEST
int coff_load_file(void *file, uint32_t size) {
    coff_filehdr_t *filehdr = (coff_filehdr_t *)file;

    if (coff_validate_filehdr(filehdr, size) != 0) {
        kprint("COFF: Invalid file header\n");
        return -1;
    }

    kprint("Loading COFF file...\n");

    // If it has an optional header, parse it for entry point
    if (filehdr->f_opthdr >= sizeof(coff_aouthdr_t)) {
        coff_aouthdr_t *aouthdr = (coff_aouthdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t));
        if (coff_validate_aouthdr(aouthdr) != 0) {
            kprint("COFF: Invalid optional header (magic/size/entry)\n");
            return -1;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "COFF: Entry point at 0x%08x\n", aouthdr->entry);
        kprint(buf);
    }

    // Identify and map sections
    coff_scnhdr_t *scnhdr = (coff_scnhdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t) + filehdr->f_opthdr);
    for (int i = 0; i < filehdr->f_nscns; i++) {
        char name[9];
        strncpy(name, scnhdr[i].s_name, 8);
        name[8] = '\0';
        char buf[64];
        snprintf(buf, sizeof(buf), "COFF: Mapping section %s\n", name);
        kprint(buf);
        
        // Use vm_map_insert to map section raw data
        uint32_t va_start = scnhdr[i].s_vaddr;
        uint32_t va_end = va_start + scnhdr[i].s_size;

        // Skip empty sections
        if (scnhdr[i].s_size == 0) continue;

        // Check for overflow and reject sections mapping into kernel space
        if (va_end < va_start || va_start >= 0xC0000000 || va_end > 0xC0000000) {
            kprint("COFF: Section maps into kernel space\n");
            return -1;
        }

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
             if ((uint32_t)scnhdr[i].s_scnptr + (uint32_t)scnhdr[i].s_size > size) {
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
#endif /* !HOST_TEST */


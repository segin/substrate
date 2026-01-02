#include "coff.h"
#include "../../drivers/video/vga.h"
#include "../../exec/perso/personality.h"
#include "../../kern/sched.h"

int coff_load_file(void *file, uint32_t size) {
    coff_filehdr_t *filehdr = (coff_filehdr_t *)file;

    if (filehdr->f_magic != COFF_MAGIC_I386) {
        return -1;
    }

    vga_write("Loading COFF file...\n", 21);

    // If it has an optional header, parse it for entry point
    if (filehdr->f_opthdr > 0) {
        coff_aouthdr_t *aouthdr = (coff_aouthdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t));
        // Entry point is aouthdr->entry
        vga_write("COFF: Entry point at 0x", 23);
        // TODO: hex dump entry
        (void)aouthdr;
    }

    // Identify and map sections
    coff_scnhdr_t *scnhdr = (coff_scnhdr_t *)((uintptr_t)file + sizeof(coff_filehdr_t) + filehdr->f_opthdr);
    for (int i = 0; i < filehdr->f_nscns; i++) {
        vga_write("COFF: Mapping section ", 22);
        vga_write(scnhdr[i].s_name, 8);
        vga_write("\n", 1);
        
        // TODO: Use vm_map_insert to map section raw data
    }

    // Default to SVR3 for now if we detect a 386 COFF binary
    if (current_process) {
        current_process->pers = &personality_svr3;
    }

    vga_write("COFF Loader invoked (header parsed).\n", 37);

    return 0;
}


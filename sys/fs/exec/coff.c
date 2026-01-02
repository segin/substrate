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
        (void)aouthdr;
    }

    // Default to Xenix for now if we detect a 386 COFF binary, 
    // though this should be more robust.
    if (current_process) {
        current_process->pers = &personality_linux; // Placeholder
    }

    vga_write("COFF Loader invoked (header parsed).\n", 37);

    return 0;
}


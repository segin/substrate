#include "coff.h"
#include "../../kern/console.h"
#include <stdio.h>
#include <string.h>
#include "../../exec/perso/personality.h"
#include "../../kern/sched.h"
#include <sys/sysinfo.h>
#include "../../pm/pm.h"

int coff_load_file(void *file, uint32_t size) {
    (void)size;
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
        
        // TODO: Use vm_map_insert to map section raw data
    }

    // Default to SVR3 for now if we detect a 386 COFF binary
    if (current_process) {
        current_process->pers = &personality_svr3;
        proc_set_bitness(current_process, BITNESS_32);
    }

    kprint("COFF Loader invoked (header parsed).\n");

    return 0;
}


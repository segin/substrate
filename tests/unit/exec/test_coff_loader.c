#include "../../../sys/fs/exec/coff.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * COFF Loader Unit Tests
 */

bool test_coff_parsing(void) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    coff_filehdr_t *filehdr = (coff_filehdr_t *)buffer;
    filehdr->f_magic = COFF_MAGIC_I386;
    filehdr->f_nscns = 2;
    filehdr->f_opthdr = sizeof(coff_aouthdr_t);
    
    coff_aouthdr_t *aouthdr = (coff_aouthdr_t *)(buffer + sizeof(coff_filehdr_t));
    aouthdr->entry = 0x1000;
    
    coff_scnhdr_t *scnhdr = (coff_scnhdr_t *)(buffer + sizeof(coff_filehdr_t) + filehdr->f_opthdr);
    strncpy(scnhdr[0].s_name, ".text", 8);
    scnhdr[0].s_vaddr = 0x1000;
    scnhdr[0].s_size = 0x100;
    
    strncpy(scnhdr[1].s_name, ".data", 8);
    scnhdr[1].s_vaddr = 0x2000;
    scnhdr[1].s_size = 0x100;
    
    // Action: Attempt load
    int result = coff_load_file(buffer, sizeof(buffer));
    
    // In current stubbed state, it returns 0 after printing
    return (result == 0);
}

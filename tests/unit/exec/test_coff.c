#include <fs/exec/coff.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * COFF Loader Unit Tests
 */

bool test_coff_header_validation(void) {
    coff_filehdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    // 1. Invalid Magic
    hdr.f_magic = 0xFFFF;
    if (coff_load_file(&hdr, sizeof(hdr)) != -1) return false;
    
    // 2. Valid Magic (i386)
    hdr.f_magic = COFF_MAGIC_I386;
    // (In current stubbed state, it returns 0 after printing)
    if (coff_load_file(&hdr, sizeof(hdr)) != 0) return false;
    
    return true;
}

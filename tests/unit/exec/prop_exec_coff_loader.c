#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fs/exec/coff.h"

/*
 * Property-based test: COFF Loader Invariant
 * Prop: Loader must reject invalid magic numbers and malformed headers.
 */

bool prop_coff_magic_validation(uint16_t magic) {
    coff_filehdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.f_magic = magic;
    
    int result = coff_load_file(&hdr, sizeof(hdr));
    
    if (magic == COFF_MAGIC_I386) {
        return (result == 0);
    } else {
        return (result == -1);
    }
}

void run_coff_loader_properties(void) {
    prop_coff_magic_validation(0x14c); // Valid
    prop_coff_magic_validation(0x0);   // Invalid
    prop_coff_magic_validation(0xFFFF); // Invalid
}

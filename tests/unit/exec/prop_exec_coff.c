#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fs/exec/coff.h>

/*
 * Property-based test: COFF Parsing Invariant
 * Prop: File size < sizeof(hdr) -> rejected.
 */

bool prop_coff_size_invariant(size_t size) {
    if (size >= sizeof(coff_filehdr_t)) return true;

    uint8_t buffer[sizeof(coff_filehdr_t)];
    memset(buffer, 0, sizeof(buffer));
    
    // Action: Attempt load with small size
    int result = coff_load_file(buffer, size);
    
    // Invariant: Must fail if size is truly insufficient for header
    // (Note: Implementation needs to check 'size' param)
    return (result == -1);
}

void run_coff_properties(void) {
    prop_coff_size_invariant(0);
    prop_coff_size_invariant(sizeof(coff_filehdr_t) - 1);
}

#ifndef _PE_H
#define _PE_H

#include <stdint.h>

// DOS Header
typedef struct {
    uint16_t e_magic; // "MZ"
    // ...
    uint32_t e_lfanew; // Offset to PE header
} IMAGE_DOS_HEADER;

// PE Header Signature "PE\0\0"
#define PE_SIGNATURE 0x00004550

int pe_load_file(void *file, uint32_t size);

#endif

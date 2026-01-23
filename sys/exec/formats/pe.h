#ifndef _PE_H
#define _PE_H

#include <stdint.h>
#include <exec/formats/mz.h>

// PE Header Signature "PE\0\0"
#define PE_SIGNATURE 0x00004550

int pe_load_file(void *file, uint32_t size);

#endif

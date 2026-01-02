#include "pe.h"
#include "../../drivers/video/vga.h"

int pe_load_file(void *file, uint32_t size) {
    // Stub
    (void)file; (void)size;
    vga_write("PE Loader invoked (stub).\n", 26);
    return 0;
}


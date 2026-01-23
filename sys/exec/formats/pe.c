#include <fs/exec/pe.h>
#include <kern/console.h>

int pe_load_file(void *file, uint32_t size) {
    // Stub
    (void)file; (void)size;
    kprint("PE Loader invoked (stub).\n");
    return 0;
}


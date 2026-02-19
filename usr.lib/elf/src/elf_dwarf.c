#include "elf_private.h"

int elfobj_section_is_dwarf(const elf_section_t *section) {
    const char *name = elf_section_name(section);
    if (name == NULL) {
        return 0;
    }
    return strncmp(name, ".debug_", 7) == 0 || strcmp(name, ".eh_frame") == 0;
}

#include "elf_private.h"

int elfobj_section_is_gnu_extension(const elf_section_t *section) {
    const char *name = elf_section_name(section);
    if (name == NULL) {
        return 0;
    }
    return strncmp(name, ".gnu", 4) == 0 || strcmp(name, ".note.gnu.build-id") == 0;
}

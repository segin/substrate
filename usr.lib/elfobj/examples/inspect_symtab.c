#include <elfobj.h>
#include <stdio.h>

int main(int argc, char **argv) {
    elfobj_t *obj;
    if (argc != 2) {
        fprintf(stderr, "usage: %s <elf-file>\n", argv[0]);
        return 1;
    }
    if (elf_open(argv[1], &obj) != ELF_OK) {
        fprintf(stderr, "failed to open %s\n", argv[1]);
        return 1;
    }
    printf("symbols: %zu\n", elf_symbol_count(obj));
    elf_close(obj);
    return 0;
}

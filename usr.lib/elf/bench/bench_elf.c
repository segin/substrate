#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    elfobj_t *obj;
    clock_t start;
    clock_t end;
    int i;

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        return 1;
    }

    start = clock();
    for (i = 0; i < 10000; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "sym_%d", i);
        if (!elf_add_symbol(obj, name, (uint64_t)i, 4, STB_GLOBAL, STT_OBJECT)) {
            return 1;
        }
    }
    end = clock();

    printf("added 10k symbols in %.2f ms\n", ((double)(end - start) * 1000.0) / CLOCKS_PER_SEC);
    elf_close(obj);
    return 0;
}

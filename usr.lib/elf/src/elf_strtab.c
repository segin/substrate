#include "elf_private.h"

elf_err_t elf__strtab_init(elf_strtab_t *tab) {
    if (tab == NULL) {
        return ELF_ERR_STATE;
    }
    memset(tab, 0, sizeof(*tab));
    tab->data = (char *)malloc(1);
    if (tab->data == NULL) {
        return ELF_ERR_OOM;
    }
    tab->data[0] = '\0';
    tab->size = 1;
    tab->cap = 1;
    return ELF_OK;
}

void elf__strtab_free(elf_strtab_t *tab) {
    if (tab == NULL) {
        return;
    }
    free(tab->data);
    tab->data = NULL;
    tab->size = 0;
    tab->cap = 0;
}

uint32_t elf__strtab_add(elf_strtab_t *tab, const char *s) {
    size_t len;
    size_t off;
    char *next;

    if (tab == NULL || s == NULL) {
        return 0;
    }

    len = strlen(s) + 1;
    off = tab->size;
    if (tab->cap < tab->size + len) {
        size_t new_cap = tab->cap == 0 ? 16 : tab->cap;
        while (new_cap < tab->size + len) {
            if (new_cap > ((size_t)-1) / 2) {
                return 0;
            }
            new_cap *= 2;
        }
        next = (char *)realloc(tab->data, new_cap);
        if (next == NULL) {
            return 0;
        }
        tab->data = next;
        tab->cap = new_cap;
    }

    memcpy(tab->data + tab->size, s, len);
    tab->size += len;
    return (uint32_t)off;
}

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
        return UINT32_MAX;
    }

    len = strlen(s) + 1;
    off = tab->size;
    /* Guard tab->size + len against size_t wrap before using it as a target
     * capacity (a wrapped value would under-grow the buffer, then the memcpy
     * below would overflow it). */
    if (len > (size_t)-1 - tab->size) {
        return UINT32_MAX;
    }
    if (tab->cap < tab->size + len) {
        size_t new_cap = tab->cap == 0 ? 16 : tab->cap;
        while (new_cap < tab->size + len) {
            if (new_cap > ((size_t)-1) / 2) {
                return UINT32_MAX;
            }
            new_cap *= 2;
        }
        next = (char *)realloc(tab->data, new_cap);
        if (next == NULL) {
            return UINT32_MAX;
        }
        tab->data = next;
        tab->cap = new_cap;
    }

    if (off > UINT32_MAX) {
        return UINT32_MAX;
    }
    memcpy(tab->data + tab->size, s, len);
    tab->size += len;
    return (uint32_t)off;
}

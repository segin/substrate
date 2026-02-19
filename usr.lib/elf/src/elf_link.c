#include "elf_private.h"

static int is_internal_section_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
           strcmp(name, ".shstrtab") == 0 || strncmp(name, ".rel", 4) == 0;
}

static elf_err_t append_section_data(struct elf_section *dst, const uint8_t *src, size_t src_sz) {
    uint8_t *buf;

    if (src_sz == 0) {
        return ELF_OK;
    }
    if (dst->type == SHT_NOBITS) {
        dst->size += src_sz;
        return ELF_OK;
    }

    buf = (uint8_t *)realloc(dst->data, dst->data_size + src_sz);
    if (buf == NULL) {
        return ELF_ERR_OOM;
    }
    memcpy(buf + dst->data_size, src, src_sz);
    dst->data = buf;
    dst->owns_data = 1;
    dst->data_size += src_sz;
    dst->size = dst->data_size;
    return ELF_OK;
}

static elf_err_t merge_symbols(elfobj_t *out, elfobj_t *in) {
    size_t i;

    for (i = 0; i < in->symbol_count; ++i) {
        struct elf_symbol *sym = in->symbols[i];
        struct elf_symbol *existing;

        if (sym == NULL || sym->name == NULL || sym->name[0] == '\0') {
            continue;
        }

        existing = elf_find_symbol(out, sym->name);
        if (existing == NULL) {
            struct elf_symbol *n = elf_add_symbol(out, sym->name, sym->value,
                                                  sym->size, sym->bind, sym->type);
            if (n == NULL) {
                return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
            }
            n->other = sym->other;
            n->shndx = sym->shndx;
            continue;
        }

        if (existing->bind == STB_WEAK && sym->bind == STB_GLOBAL) {
            existing->bind = sym->bind;
            existing->type = sym->type;
            existing->value = sym->value;
            existing->size = sym->size;
            existing->shndx = sym->shndx;
        }
    }

    return ELF_OK;
}

elf_err_t elf_link(elfobj_t **inputs, size_t count, elfobj_t **output) {
    elfobj_t *out;
    size_t i;
    size_t j;

    if (inputs == NULL || count == 0 || output == NULL) {
        return ELF_ERR_STATE;
    }

    out = elf_create(ET_REL, inputs[0]->machine, inputs[0]->cls, inputs[0]->endian);
    if (out == NULL) {
        return ELF_ERR_OOM;
    }

    for (i = 0; i < count; ++i) {
        elfobj_t *in = inputs[i];
        if (in == NULL) {
            elf_close(out);
            return ELF_ERR_STATE;
        }
        if (in->machine != out->machine || in->cls != out->cls || in->endian != out->endian) {
            elf_close(out);
            return ELF_ERR_UNSUPPORTED;
        }

        for (j = 0; j < in->section_count; ++j) {
            struct elf_section *src = in->sections[j];
            struct elf_section *dst;
            elf_err_t err;

            if (src == NULL || src->name == NULL || src->name[0] == '\0') {
                continue;
            }
            if (is_internal_section_name(src->name)) {
                continue;
            }

            dst = elf_find_section(out, src->name);
            if (dst == NULL) {
                dst = elf_add_section(out, src->name, src->type, src->flags);
                if (dst == NULL) {
                    elf_close(out);
                    return out->last_err == ELF_OK ? ELF_ERR_OOM : out->last_err;
                }
                dst->addralign = src->addralign;
            }

            err = append_section_data(dst, src->data, src->data_size);
            if (err != ELF_OK) {
                elf_close(out);
                return err;
            }
        }

        if (merge_symbols(out, in) != ELF_OK) {
            elf_close(out);
            return ELF_ERR_FORMAT;
        }

        for (j = 0; j < in->reloc_count; ++j) {
            struct elf_reloc *r = in->relocs[j];
            struct elf_section *dst_sec;
            struct elf_symbol *dst_sym;
            if (r == NULL || r->section == NULL || r->section->name == NULL) {
                continue;
            }
            dst_sec = elf_find_section(out, r->section->name);
            if (dst_sec == NULL) {
                continue;
            }
            dst_sym = r->symbol == NULL ? NULL : elf_find_symbol(out, r->symbol->name);
            if (dst_sym == NULL) {
                continue;
            }
            if (elf_add_relocation(dst_sec, r->offset, dst_sym, r->type, r->addend) != ELF_OK) {
                elf_close(out);
                return ELF_ERR_RELOC;
            }
        }
    }

    *output = out;
    return ELF_OK;
}

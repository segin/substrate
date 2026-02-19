#include "elf_private.h"
#include <errno.h>

static int mul_overflow(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > ((size_t)-1) / b) {
        return 1;
    }
    *out = a * b;
    return 0;
}

elfobj_t *elf__alloc_obj(void) {
    elfobj_t *obj = (elfobj_t *)elf__calloc(1, sizeof(*obj));
    if (obj == NULL) {
        return NULL;
    }
    obj->cls = ELFOBJ_CLASS_32;
    obj->endian = ELFOBJ_ENDIAN_LE;
    obj->type = ET_REL;
    obj->machine = EM_386;
    return obj;
}

void *elf__calloc(size_t n, size_t sz) {
    size_t total = 0;
    if (mul_overflow(n, sz, &total)) {
        return NULL;
    }
    return calloc(1, total);
}

void *elf__reallocarray(void *ptr, size_t n, size_t sz) {
    size_t total = 0;
    if (mul_overflow(n, sz, &total)) {
        return NULL;
    }
    return realloc(ptr, total);
}

char *elf__strdup(const char *s) {
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    return out;
}

int elf__bounds_ok(size_t off, size_t len, size_t total) {
    if (off > total) {
        return 0;
    }
    if (len > total - off) {
        return 0;
    }
    return 1;
}

uint16_t elf__rd16(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
    }
    return (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

uint32_t elf__rd32(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

uint64_t elf__rd64(const uint8_t *p, elfobj_endian_t e) {
    uint64_t lo;
    uint64_t hi;

    if (e == ELFOBJ_ENDIAN_BE) {
        hi = elf__rd32(p, e);
        lo = elf__rd32(p + 4, e);
        return (hi << 32) | lo;
    }
    lo = elf__rd32(p, e);
    hi = elf__rd32(p + 4, e);
    return (hi << 32) | lo;
}

void elf__wr16(uint8_t *p, elfobj_endian_t e, uint16_t v) {
    if (e == ELFOBJ_ENDIAN_BE) {
        p[0] = (uint8_t)((v >> 8) & 0xff);
        p[1] = (uint8_t)(v & 0xff);
        return;
    }
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}

void elf__wr32(uint8_t *p, elfobj_endian_t e, uint32_t v) {
    if (e == ELFOBJ_ENDIAN_BE) {
        p[0] = (uint8_t)((v >> 24) & 0xff);
        p[1] = (uint8_t)((v >> 16) & 0xff);
        p[2] = (uint8_t)((v >> 8) & 0xff);
        p[3] = (uint8_t)(v & 0xff);
        return;
    }
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

void elf__wr64(uint8_t *p, elfobj_endian_t e, uint64_t v) {
    if (e == ELFOBJ_ENDIAN_BE) {
        elf__wr32(p, e, (uint32_t)(v >> 32));
        elf__wr32(p + 4, e, (uint32_t)(v & 0xffffffffu));
        return;
    }
    elf__wr32(p, e, (uint32_t)(v & 0xffffffffu));
    elf__wr32(p + 4, e, (uint32_t)(v >> 32));
}

elf_err_t elf__append_diag(elfobj_t *obj, const char *msg) {
    size_t need;
    char *next;

    if (obj == NULL || msg == NULL) {
        return ELF_ERR_STATE;
    }

    need = strlen(msg) + 1;
    if (obj->diag.cap < obj->diag.len + need) {
        size_t new_cap = obj->diag.cap == 0 ? 128 : obj->diag.cap;
        while (new_cap < obj->diag.len + need) {
            if (new_cap > ((size_t)-1) / 2) {
                return ELF_ERR_OOM;
            }
            new_cap *= 2;
        }
        next = (char *)realloc(obj->diag.buf, new_cap);
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->diag.buf = next;
        obj->diag.cap = new_cap;
    }

    memcpy(obj->diag.buf + obj->diag.len, msg, need - 1);
    obj->diag.len += need - 1;
    obj->diag.buf[obj->diag.len++] = '\n';
    obj->diag.buf[obj->diag.len] = '\0';
    return ELF_OK;
}

elf_err_t elf__append_diag_fmt(elfobj_t *obj, const char *prefix, uint64_t value) {
    char tmp[128];
    size_t pfx_len;

    if (obj == NULL || prefix == NULL) {
        return ELF_ERR_STATE;
    }

    pfx_len = strlen(prefix);
    if (pfx_len > sizeof(tmp) - 24) {
        return ELF_ERR_STATE;
    }
    memcpy(tmp, prefix, pfx_len);
    tmp[pfx_len] = '\0';
    (void)snprintf(tmp + pfx_len, sizeof(tmp) - pfx_len, "%llu",
                   (unsigned long long)value);
    return elf__append_diag(obj, tmp);
}

void elf__set_err(elfobj_t *obj, elf_err_t err, const char *msg) {
    if (obj != NULL) {
        obj->last_err = err;
        if (msg != NULL) {
            (void)elf__append_diag(obj, msg);
        }
    }
}

static void elf_free_sections(elfobj_t *obj) {
    size_t i;
    if (obj == NULL) {
        return;
    }
    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *s = obj->sections[i];
        if (s == NULL) {
            continue;
        }
        free(s->name);
        if (s->owns_data) {
            free(s->data);
        }
        free(s->relocs);
        free(s);
    }
    free(obj->sections);
}

static void elf_free_symbols(elfobj_t *obj) {
    size_t i;
    if (obj == NULL) {
        return;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL) {
            continue;
        }
        free(sym->name);
        free(sym);
    }
    free(obj->symbols);
}

static void elf_free_relocs(elfobj_t *obj) {
    size_t i;
    if (obj == NULL) {
        return;
    }
    for (i = 0; i < obj->reloc_count; ++i) {
        free(obj->relocs[i]);
    }
    free(obj->relocs);
}

const char *elf_errstr(elf_err_t err) {
    switch (err) {
        case ELF_OK: return "ok";
        case ELF_ERR_IO: return "io error";
        case ELF_ERR_FORMAT: return "invalid ELF format";
        case ELF_ERR_RELOC: return "relocation error";
        case ELF_ERR_OOM: return "out of memory";
        case ELF_ERR_BOUNDS: return "bounds error";
        case ELF_ERR_UNSUPPORTED: return "unsupported feature";
        case ELF_ERR_STATE: return "invalid object state";
        case ELF_ERR_NOTFOUND: return "not found";
        default: return "unknown error";
    }
}

void elf_close(elfobj_t *obj) {
    if (obj == NULL) {
        return;
    }
    if (obj->owns_image) {
        free(obj->image);
    }
    elf_free_sections(obj);
    elf_free_symbols(obj);
    elf_free_relocs(obj);
    free(obj->diag.buf);
    free(obj);
}

elfobj_t *elf_create(uint16_t type, uint16_t machine, elfobj_class_t cls, elfobj_endian_t endian) {
    elfobj_t *obj = elf__alloc_obj();
    if (obj == NULL) {
        return NULL;
    }
    obj->type = type;
    obj->machine = machine;
    obj->cls = cls;
    obj->endian = endian;
    return obj;
}

elf_err_t elf_finalize(elfobj_t *obj) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    obj->finalized = 1;
    return ELF_OK;
}

size_t elf_section_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->section_count;
}

size_t elf_symbol_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->symbol_count;
}

size_t elf_reloc_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->reloc_count;
}

const char *elf_section_name(const elf_section_t *section) {
    return section == NULL ? NULL : section->name;
}

uint32_t elf_section_type(const elf_section_t *section) {
    return section == NULL ? 0 : section->type;
}

uint64_t elf_section_flags(const elf_section_t *section) {
    return section == NULL ? 0 : section->flags;
}

uint64_t elf_section_size(const elf_section_t *section) {
    return section == NULL ? 0 : section->size;
}

const void *elf_section_data(const elf_section_t *section, size_t *size_out) {
    if (size_out != NULL) {
        *size_out = section == NULL ? 0 : section->data_size;
    }
    return section == NULL ? NULL : section->data;
}

const char *elf_symbol_name(const elf_symbol_t *symbol) {
    return symbol == NULL ? NULL : symbol->name;
}

uint64_t elf_symbol_value(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->value;
}

uint64_t elf_symbol_size(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->size;
}

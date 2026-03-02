#include "elf_private.h"
#include <errno.h>
#include <sys/mman.h>

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
    obj->validate_mode = ELF_VALIDATE_STRICT;
    return obj;
}

void elf__diag_clear(elfobj_t *obj) {
    size_t i;
    if (obj == NULL) {
        return;
    }
    free(obj->diag.buf);
    obj->diag.buf = NULL;
    obj->diag.len = 0;
    obj->diag.cap = 0;
    for (i = 0; i < obj->diag_item_count; ++i) {
        free(obj->diag_items[i].message);
    }
    free(obj->diag_items);
    obj->diag_items = NULL;
    obj->diag_item_count = 0;
    obj->diag_item_cap = 0;
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

int elf__u64_add(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
    if (a > UINT64_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

int elf__u64_mul(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
    if (a == 0 || b == 0) {
        *out = 0;
        return 1;
    }
    if (a > UINT64_MAX / b) {
        return 0;
    }
    *out = a * b;
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

elf_err_t elf__diag_append(elfobj_t *obj, elf_diag_level_t level, elf_err_t code,
                           uint64_t index, const char *msg) {
    size_t need;
    char *next;
    char *copy;
    void *items_next;

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

    if (obj->diag_item_count == obj->diag_item_cap) {
        size_t new_cap = obj->diag_item_cap == 0 ? 16 : obj->diag_item_cap * 2;
        items_next = elf__reallocarray(obj->diag_items, new_cap, sizeof(obj->diag_items[0]));
        if (items_next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->diag_items = (elf_diag_item_t *)items_next;
        obj->diag_item_cap = new_cap;
    }
    copy = elf__strdup(msg);
    if (copy == NULL) {
        return ELF_ERR_OOM;
    }
    obj->diag_items[obj->diag_item_count].level = level;
    obj->diag_items[obj->diag_item_count].code = code;
    obj->diag_items[obj->diag_item_count].index = index;
    obj->diag_items[obj->diag_item_count].message = copy;
    obj->diag_item_count++;
    return ELF_OK;
}

elf_err_t elf__append_diag(elfobj_t *obj, const char *msg) {
    return elf__diag_append(obj, ELF_DIAG_ERROR, ELF_ERR_FORMAT, UINT64_MAX, msg);
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
    return elf__diag_append(obj, ELF_DIAG_ERROR, ELF_ERR_FORMAT, value, tmp);
}

void elf__set_err(elfobj_t *obj, elf_err_t err, const char *msg) {
    if (obj != NULL) {
        obj->last_err = err;
        if (msg != NULL) {
            (void)elf__diag_append(obj, ELF_DIAG_ERROR, err, UINT64_MAX, msg);
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
        free(s->note_name);
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

static void elf_free_phdrs(elfobj_t *obj) {
    if (obj == NULL) {
        return;
    }
    free(obj->phdrs);
}

static void elf_free_segments(elfobj_t *obj) {
    size_t i;
    if (obj == NULL) {
        return;
    }
    for (i = 0; i < obj->segment_count; ++i) {
        struct elf_segment *seg = obj->segments[i];
        if (seg == NULL) {
            continue;
        }
        free(seg->section_indices);
        free(seg->interp_path);
        free(seg);
    }
    free(obj->segments);
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
    if (obj->mmapped_image && obj->image != NULL) {
        (void)munmap(obj->image, obj->image_size);
    } else if (obj->owns_image) {
        free(obj->image);
    }
    elf_free_sections(obj);
    elf_free_symbols(obj);
    elf_free_relocs(obj);
    elf_free_phdrs(obj);
    elf_free_segments(obj);
    elf__diag_clear(obj);
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

static void init_default_sections(elfobj_t *obj, uint64_t align_text, uint64_t align_data) {
    elf_section_t *sec;
    sec = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (sec != NULL) {
        (void)elf_section_set_align(sec, align_text);
    }
    sec = elf_add_section(obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (sec != NULL) {
        (void)elf_section_set_align(sec, align_data);
    }
    sec = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    if (sec != NULL) {
        (void)elf_section_set_align(sec, align_data);
    }
}

elfobj_t *elf_init_i386(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_x86_64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    init_default_sections(obj, 16, 8);
    return obj;
}

elfobj_t *elf_init_arm(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_ARM, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_ARM_ABI_VER5;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_aarch64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_AARCH64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = 0;
    init_default_sections(obj, 4, 8);
    return obj;
}

elfobj_t *elf_init_mips32(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_MIPS, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_MIPS_ABI_O32 | EF_MIPS_ARCH_32R2;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_mips64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_MIPS, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_MIPS_ABI_O64 | EF_MIPS_ARCH_64R2;
    init_default_sections(obj, 8, 8);
    return obj;
}

elfobj_t *elf_init_riscv32(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_RISCV, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_RISCV_FLOAT_ABI_SOFT;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_riscv64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_RISCV, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_RISCV_FLOAT_ABI_SOFT;
    init_default_sections(obj, 8, 8);
    return obj;
}

elfobj_t *elf_init_loongarch32(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_LOONGARCH, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_LARCH_ABI_DOUBLE_FLOAT | EF_LARCH_OBJABI_V1;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_loongarch64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_LOONGARCH, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_LARCH_ABI_DOUBLE_FLOAT | EF_LARCH_OBJABI_V1;
    init_default_sections(obj, 8, 8);
    return obj;
}

elfobj_t *elf_init_m68k(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_68K, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = 0;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_vax(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_VAX, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = 0;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_ppc32(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_PPC, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = 0;
    init_default_sections(obj, 4, 4);
    return obj;
}

elfobj_t *elf_init_ppc64(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_PPC64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = EF_PPC64_ABI_V2;
    init_default_sections(obj, 8, 8);
    return obj;
}

elfobj_t *elf_init_alpha(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_ALPHA, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    obj->flags = 0;
    init_default_sections(obj, 8, 8);
    return obj;
}

elf_section_t *elf_add_arm_exidx(elfobj_t *obj) {
    elf_section_t *exidx;
    elf_section_t *extab;

    if (obj == NULL) {
        return NULL;
    }
    exidx = elf_find_section(obj, ".ARM.exidx");
    if (exidx == NULL) {
        exidx = elf_add_section(obj, ".ARM.exidx", SHT_ARM_EXIDX, SHF_ALLOC | SHF_LINK_ORDER);
        if (exidx == NULL) {
            return NULL;
        }
        (void)elf_section_set_align(exidx, 4);
    }
    extab = elf_find_section(obj, ".ARM.extab");
    if (extab == NULL) {
        extab = elf_add_section(obj, ".ARM.extab", SHT_PROGBITS, SHF_ALLOC);
        if (extab == NULL) {
            return NULL;
        }
        (void)elf_section_set_align(extab, 4);
    }
    exidx->link = (uint32_t)extab->index;
    obj->dirty = 1;
    return exidx;
}

elf_err_t elf_add_arm_attributes(elfobj_t *obj, const void *attrs_data, size_t attrs_size) {
    elf_section_t *attrs;
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    attrs = elf_find_section(obj, ".ARM.attributes");
    if (attrs == NULL) {
        attrs = elf_add_section(obj, ".ARM.attributes", SHT_ARM_ATTRIBUTES, 0);
        if (attrs == NULL) {
            return obj->last_err == ELF_OK ? ELF_ERR_OOM : obj->last_err;
        }
        (void)elf_section_set_align(attrs, 1);
    }
    if (attrs_data == NULL || attrs_size == 0) {
        static const uint8_t default_attrs[] = { 'A', 0 };
        return elf_section_set_data(attrs, default_attrs, sizeof(default_attrs));
    }
    return elf_section_set_data(attrs, attrs_data, attrs_size);
}

elf_err_t elf_add_gnu_property_aarch64(elfobj_t *obj, uint32_t feature_1) {
    elf_section_t *sec;
    uint8_t data[32];
    size_t off = 0;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    sec = elf_find_section(obj, ".note.gnu.property");
    if (sec == NULL) {
        sec = elf_add_section(obj, ".note.gnu.property", SHT_NOTE, SHF_ALLOC);
        if (sec == NULL) {
            return obj->last_err == ELF_OK ? ELF_ERR_OOM : obj->last_err;
        }
        (void)elf_section_set_align(sec, obj->cls == ELFOBJ_CLASS_64 ? 8 : 4);
    }

    memset(data, 0, sizeof(data));
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian, 16);
    off += 4;
    elf__wr32(data + off, obj->endian, 5);
    off += 4;
    memcpy(data + off, "GNU\0", 4);
    off += 4;
    elf__wr32(data + off, obj->endian, GNU_PROPERTY_AARCH64_FEATURE_1_AND);
    off += 4;
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian,
              feature_1 & (GNU_PROPERTY_AARCH64_FEATURE_1_BTI |
                           GNU_PROPERTY_AARCH64_FEATURE_1_PAC));
    off += 4;
    elf__wr32(data + off, obj->endian, 0);
    off += 4;
    return elf_section_set_data(sec, data, off);
}

elf_err_t elf_finalize(elfobj_t *obj) {
    elf_err_t err;
    size_t first_global;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->finalized) {
        return ELF_OK;
    }
    if (obj->readonly && obj->image != NULL && obj->dirty == 0) {
        obj->finalized = 1;
        return ELF_OK;
    }
    err = elf__layout(obj);
    if (err != ELF_OK) {
        elf__set_err(obj, err, "layout failed during finalize");
        return err;
    }
    err = elf_symbols_sort_deterministic(obj, &first_global);
    if (err != ELF_OK) {
        elf__set_err(obj, err, "symbol ordering failed during finalize");
        return err;
    }
    (void)first_global;
    obj->finalized = 1;
    obj->readonly = 1;
    return ELF_OK;
}

elf_err_t elf_set_type(elfobj_t *obj, uint16_t type) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->type = type;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_set_machine(elfobj_t *obj, uint16_t machine) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->machine = machine;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_set_osabi(elfobj_t *obj, uint8_t osabi) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->osabi = osabi;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_set_abiversion(elfobj_t *obj, uint8_t abiversion) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->abiversion = abiversion;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_set_flags(elfobj_t *obj, uint32_t flags) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->flags = flags;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_set_entry(elfobj_t *obj, uint64_t entry) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    obj->entry = entry;
    obj->dirty = 1;
    return ELF_OK;
}

uint16_t elf_type(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->type;
}

uint16_t elf_machine(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->machine;
}

uint8_t elf_osabi(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->osabi;
}

uint8_t elf_abiversion(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->abiversion;
}

uint32_t elf_flags(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->flags;
}

uint64_t elf_entry(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->entry;
}

elfobj_class_t elf_class(const elfobj_t *obj) {
    return obj == NULL ? ELFOBJ_CLASS_NONE : obj->cls;
}

elfobj_endian_t elf_endian(const elfobj_t *obj) {
    return obj == NULL ? ELFOBJ_ENDIAN_NONE : obj->endian;
}

size_t elf_section_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->section_count;
}

size_t elf_symbol_count(const elfobj_t *obj) {
    if (obj == NULL) {
        return 0;
    }
    (void)elf__ensure_symbols_relocs((elfobj_t *)obj);
    return obj->symbol_count;
}

size_t elf_reloc_count(const elfobj_t *obj) {
    if (obj == NULL) {
        return 0;
    }
    (void)elf__ensure_symbols_relocs((elfobj_t *)obj);
    return obj->reloc_count;
}

uint16_t elf_program_header_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : (uint16_t)obj->phdr_count;
}

uint32_t elf_program_header_type(const elfobj_t *obj, size_t index) {
    if (obj == NULL || index >= obj->phdr_count) {
        return 0;
    }
    return obj->phdrs[index].type;
}

uint32_t elf_program_header_flags(const elfobj_t *obj, size_t index) {
    if (obj == NULL || index >= obj->phdr_count) {
        return 0;
    }
    return obj->phdrs[index].flags;
}

uint64_t elf_program_header_align(const elfobj_t *obj, size_t index) {
    if (obj == NULL || index >= obj->phdr_count) {
        return 0;
    }
    return obj->phdrs[index].align;
}

elf_err_t elf_program_header_set_type(elfobj_t *obj, size_t index, uint32_t type) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    if (index >= obj->phdr_count) {
        return ELF_ERR_BOUNDS;
    }
    obj->phdrs[index].type = type;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_program_header_set_flags(elfobj_t *obj, size_t index, uint32_t flags) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    if (index >= obj->phdr_count) {
        return ELF_ERR_BOUNDS;
    }
    obj->phdrs[index].flags = flags;
    obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_program_header_set_align(elfobj_t *obj, size_t index, uint64_t align) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->readonly || obj->finalized) {
        return ELF_ERR_STATE;
    }
    if (index >= obj->phdr_count) {
        return ELF_ERR_BOUNDS;
    }
    obj->phdrs[index].align = align == 0 ? 1 : align;
    obj->dirty = 1;
    return ELF_OK;
}

elf_section_t *elf_section_get(const elfobj_t *obj, size_t index) {
    if (obj == NULL || index >= obj->section_count) {
        return NULL;
    }
    return obj->sections[index];
}

size_t elf_segment_count(const elfobj_t *obj) {
    return obj == NULL ? 0 : obj->segment_count;
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

uint64_t elf_section_addr(const elf_section_t *section) {
    return section == NULL ? 0 : section->addr;
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

uint8_t elf_symbol_bind(const elf_symbol_t *symbol) {
    return symbol == NULL ? STB_LOCAL : symbol->bind;
}

uint8_t elf_symbol_type(const elf_symbol_t *symbol) {
    return symbol == NULL ? STT_NOTYPE : symbol->type;
}

uint16_t elf_symbol_shndx(const elf_symbol_t *symbol) {
    return symbol == NULL ? SHN_UNDEF : symbol->shndx;
}

uint64_t elf_symbol_value(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->value;
}

uint64_t elf_symbol_size(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->size;
}

int elf_uses_mmap(const elfobj_t *obj) {
    return obj == NULL ? 0 : (obj->mmapped_image != 0);
}

int elf_is_lazy_parse_enabled(const elfobj_t *obj) {
    return obj == NULL ? 0 : (obj->lazy_parse != 0);
}

elf_err_t elf_last_error(const elfobj_t *obj) {
    return obj == NULL ? ELF_ERR_STATE : obj->last_err;
}

const char *elf_last_diagnostics(const elfobj_t *obj) {
    if (obj == NULL || obj->diag.buf == NULL) {
        return "";
    }
    return obj->diag.buf;
}

elf_err_t elf_set_validation_mode(elfobj_t *obj, elf_validate_mode_t mode) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (mode != ELF_VALIDATE_PERMISSIVE && mode != ELF_VALIDATE_STRICT) {
        return ELF_ERR_STATE;
    }
    obj->validate_mode = mode;
    return ELF_OK;
}

elf_validate_mode_t elf_get_validation_mode(const elfobj_t *obj) {
    if (obj == NULL) {
        return ELF_VALIDATE_STRICT;
    }
    return obj->validate_mode;
}

size_t elf_diag_count(const elfobj_t *obj) {
    if (obj == NULL) {
        return 0;
    }
    return obj->diag_item_count;
}

int elf_diag_entry(const elfobj_t *obj, size_t index, elf_diag_entry_t *out) {
    const elf_diag_item_t *in;
    if (obj == NULL || out == NULL || index >= obj->diag_item_count) {
        return 0;
    }
    in = &obj->diag_items[index];
    out->level = in->level;
    out->code = in->code;
    out->index = in->index;
    out->message = in->message;
    return 1;
}

elf_err_t elf__push_phdr(elfobj_t *obj, const struct elf_phdr *phdr) {
    void *next;

    if (obj == NULL || phdr == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->phdr_count == obj->phdr_cap) {
        size_t new_cap = obj->phdr_cap == 0 ? 8 : obj->phdr_cap * 2;
        next = elf__reallocarray(obj->phdrs, new_cap, sizeof(obj->phdrs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->phdrs = (struct elf_phdr *)next;
        obj->phdr_cap = new_cap;
    }
    obj->phdrs[obj->phdr_count++] = *phdr;
    obj->phnum = (uint16_t)obj->phdr_count;
    return ELF_OK;
}

elf_err_t elf__push_segment(elfobj_t *obj, struct elf_segment *seg) {
    void *next;

    if (obj == NULL || seg == NULL) {
        return ELF_ERR_STATE;
    }
    if (obj->segment_count == obj->segment_cap) {
        size_t new_cap = obj->segment_cap == 0 ? 4 : obj->segment_cap * 2;
        next = elf__reallocarray(obj->segments, new_cap, sizeof(obj->segments[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->segments = (struct elf_segment **)next;
        obj->segment_cap = new_cap;
    }
    obj->segments[obj->segment_count++] = seg;
    return ELF_OK;
}

#include "elf_private.h"

#define ELFOBJ_MAX_RELOC_BACKENDS 16

static struct elf_reloc_backend g_backends[ELFOBJ_MAX_RELOC_BACKENDS];
static size_t g_backend_count;
static volatile int g_backend_lock;
static int g_builtin_backends_ready;

#if defined(__SIZEOF_INT128__)
typedef __int128 elf_swide_t;
#else
typedef long long elf_swide_t;
#endif

static void backend_lock(void) {
    while (!__sync_bool_compare_and_swap(&g_backend_lock, 0, 1)) {
    }
}

static void backend_unlock(void) {
    __sync_lock_release(&g_backend_lock);
}

static int swide_in_signed_bits(elf_swide_t v, int bits) {
    elf_swide_t minv;
    elf_swide_t maxv;

    if (bits <= 0 || bits > 64) {
        return 0;
    }
    if (bits == 64) {
        return 1;
    }
    minv = -((elf_swide_t)1 << (bits - 1));
    maxv = (((elf_swide_t)1 << (bits - 1)) - 1);
    return v >= minv && v <= maxv;
}

static int swide_in_unsigned_bits(elf_swide_t v, int bits) {
    elf_swide_t maxv;

    if (bits <= 0 || bits > 64) {
        return 0;
    }
    if (v < 0) {
        return 0;
    }
    if (bits == 64) {
        return 1;
    }
    maxv = (((elf_swide_t)1 << bits) - 1);
    return v <= maxv;
}

static uint64_t swide_to_width(elf_swide_t v, int bits) {
    uint64_t uv = (uint64_t)v;

    if (bits <= 0) {
        return 0;
    }
    if (bits >= 64) {
        return uv;
    }
    return uv & ((((uint64_t)1) << bits) - 1);
}

static int i386_reloc_size(uint32_t type) {
    switch (type) {
        case R_386_NONE:
            return 0;
        case R_386_32:
        case R_386_PC32:
        case R_386_GOT32:
        case R_386_PLT32:
        case R_386_RELATIVE:
        case R_386_GOTOFF:
        case R_386_GOTPC:
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
            return 4;
        default:
            return -1;
    }
}

static int i386_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_386_PC32:
        case R_386_PLT32:
        case R_386_GOTPC:
            return 1;
        default:
            return 0;
    }
}

static int i386_apply(const elfobj_reloc_ctx_t *ctx,
                      uint32_t type,
                      uint64_t place,
                      uint64_t sym_value,
                      int64_t addend,
                      uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_386_NONE:
            *out_value = 0;
            return 0;
        case R_386_32:
        case R_386_RELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_PC32:
        case R_386_PLT32:
        case R_386_GOTPC:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_GOT32:
        case R_386_GOTOFF:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        default:
            return -1;
    }
}

static int x64_reloc_size(uint32_t type) {
    switch (type) {
        case R_X86_64_NONE:
            return 0;
        case R_X86_64_64:
            return 8;
        case R_X86_64_PC32:
        case R_X86_64_GOT32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
            return 4;
        default:
            return -1;
    }
}

static int x64_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
            return 1;
        default:
            return 0;
    }
}

static int x64_apply(const elfobj_reloc_ctx_t *ctx,
                     uint32_t type,
                     uint64_t place,
                     uint64_t sym_value,
                     int64_t addend,
                     uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_X86_64_NONE:
            *out_value = 0;
            return 0;
        case R_X86_64_64:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 64)) {
                return -2;
            }
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_X86_64_32:
        case R_X86_64_GOT32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        default:
            return -1;
    }
}

static void register_builtin_backends_locked(void) {
    struct elf_reloc_backend b;

    if (g_builtin_backends_ready) {
        return;
    }

    memset(&b, 0, sizeof(b));
    b.machine = EM_386;
    b.apply_reloc = i386_apply;
    b.reloc_size = i386_reloc_size;
    b.is_pc_relative = i386_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_X86_64;
    b.apply_reloc = x64_apply;
    b.reloc_size = x64_reloc_size;
    b.is_pc_relative = x64_is_pc_relative;
    g_backends[g_backend_count++] = b;

    g_builtin_backends_ready = 1;
}

static const struct elf_reloc_backend *find_backend(uint32_t machine) {
    size_t i;
    const struct elf_reloc_backend *ret = NULL;

    backend_lock();
    register_builtin_backends_locked();
    for (i = 0; i < g_backend_count; ++i) {
        if (g_backends[i].machine == machine) {
            ret = &g_backends[i];
            break;
        }
    }
    backend_unlock();
    return ret;
}

elf_err_t elf__push_reloc(elfobj_t *obj, struct elf_reloc *rel) {
    void *next;

    if (obj->reloc_count == obj->reloc_cap) {
        size_t new_cap = obj->reloc_cap == 0 ? 16 : obj->reloc_cap * 2;
        next = elf__reallocarray(obj->relocs, new_cap, sizeof(obj->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->relocs = (struct elf_reloc **)next;
        obj->reloc_cap = new_cap;
    }
    obj->relocs[obj->reloc_count++] = rel;
    return ELF_OK;
}

elf_err_t elf__section_push_reloc(struct elf_section *section, struct elf_reloc *rel) {
    void *next;

    if (section->reloc_count == section->reloc_cap) {
        size_t new_cap = section->reloc_cap == 0 ? 8 : section->reloc_cap * 2;
        next = elf__reallocarray(section->relocs, new_cap, sizeof(section->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        section->relocs = (struct elf_reloc **)next;
        section->reloc_cap = new_cap;
    }
    section->relocs[section->reloc_count++] = rel;
    return ELF_OK;
}

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend) {
    struct elf_reloc *rel;
    elf_err_t err;

    if (section == NULL || section->obj == NULL || symbol == NULL) {
        return ELF_ERR_STATE;
    }
    if (section->obj != symbol->obj) {
        return ELF_ERR_STATE;
    }
    if (section->obj->readonly || section->obj->finalized) {
        elf__set_err(section->obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return ELF_ERR_STATE;
    }

    rel = (struct elf_reloc *)elf__calloc(1, sizeof(*rel));
    if (rel == NULL) {
        elf__set_err(section->obj, ELF_ERR_OOM, "alloc relocation failed");
        return ELF_ERR_OOM;
    }

    rel->section = section;
    rel->offset = offset;
    rel->symbol = symbol;
    rel->type = type;
    rel->addend = addend;
    rel->has_addend = section->obj->cls == ELFOBJ_CLASS_64 ? 1 : 0;

    err = elf__section_push_reloc(section, rel);
    if (err != ELF_OK) {
        free(rel);
        return err;
    }
    err = elf__push_reloc(section->obj, rel);
    if (err != ELF_OK) {
        section->reloc_count--;
        free(rel);
        return err;
    }
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_register_reloc_backend(const struct elf_reloc_backend *backend) {
    size_t i;

    if (backend == NULL || backend->apply_reloc == NULL) {
        return ELF_ERR_STATE;
    }

    backend_lock();
    register_builtin_backends_locked();
    for (i = 0; i < g_backend_count; ++i) {
        if (g_backends[i].machine == backend->machine) {
            g_backends[i] = *backend;
            backend_unlock();
            return ELF_OK;
        }
    }

    if (g_backend_count >= ELFOBJ_MAX_RELOC_BACKENDS) {
        backend_unlock();
        return ELF_ERR_OOM;
    }
    g_backends[g_backend_count++] = *backend;
    backend_unlock();
    return ELF_OK;
}

size_t elf_section_reloc_count(const elf_section_t *section) {
    return section == NULL ? 0 : section->reloc_count;
}

elf_reloc_t *elf_section_reloc_at(elf_section_t *section, size_t index) {
    if (section == NULL || index >= section->reloc_count) {
        return NULL;
    }
    return section->relocs[index];
}

elf_reloc_t *elf_reloc_at(elfobj_t *obj, size_t index) {
    if (obj == NULL || index >= obj->reloc_count) {
        return NULL;
    }
    return obj->relocs[index];
}

uint64_t elf_reloc_offset(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->offset;
}

uint32_t elf_reloc_type(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->type;
}

int64_t elf_reloc_addend(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->addend;
}

int elf_reloc_has_addend(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->has_addend != 0;
}

elf_symbol_t *elf_reloc_symbol(const elf_reloc_t *reloc) {
    return reloc == NULL ? NULL : reloc->symbol;
}

elf_section_t *elf_reloc_section(const elf_reloc_t *reloc) {
    return reloc == NULL ? NULL : reloc->section;
}

elf_err_t elf_set_reloc_hooks(elfobj_t *obj, const elf_reloc_hooks_t *hooks, void *user) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (hooks == NULL) {
        memset(&obj->reloc_hooks, 0, sizeof(obj->reloc_hooks));
        obj->reloc_hook_user = NULL;
    } else {
        obj->reloc_hooks = *hooks;
        obj->reloc_hook_user = user;
    }
    return ELF_OK;
}

int elf_reloc_size_for_machine(uint16_t machine, uint32_t type) {
    const struct elf_reloc_backend *backend = find_backend(machine);
    if (backend == NULL || backend->reloc_size == NULL) {
        return -1;
    }
    return backend->reloc_size(type);
}

int elf_reloc_is_pc_relative_for_machine(uint16_t machine, uint32_t type) {
    const struct elf_reloc_backend *backend = find_backend(machine);
    if (backend == NULL || backend->is_pc_relative == NULL) {
        return 0;
    }
    return backend->is_pc_relative(type);
}

int elf_reloc_is_tls_for_machine(uint16_t machine, uint32_t type) {
    if (machine == EM_386) {
        return type == R_386_TLS_TPOFF || type == R_386_TLS_IE || type == R_386_TLS_GOTIE ||
               type == R_386_TLS_LE || type == R_386_TLS_GD || type == R_386_TLS_LDM ||
               type == R_386_TLS_LDO_32;
    }
    if (machine == EM_X86_64) {
        return type == R_X86_64_TLSGD || type == R_X86_64_GOTTPOFF || type == R_X86_64_TPOFF32;
    }
    return 0;
}

elf_err_t elf_apply_relocation_value(const elfobj_t *obj, uint32_t type, uint64_t place,
                                     uint64_t sym_value, int64_t addend, uint64_t *out_value) {
    elfobj_reloc_ctx_t ctx;
    const struct elf_reloc_backend *backend;
    int rc;

    if (obj == NULL || out_value == NULL) {
        return ELF_ERR_STATE;
    }

    backend = find_backend(obj->machine);
    if (backend == NULL || backend->apply_reloc == NULL) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_UNSUPPORTED, "no relocation backend for machine");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        return ELF_ERR_UNSUPPORTED;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.machine = obj->machine;
    ctx.use_rela = obj->cls == ELFOBJ_CLASS_64 ? 1 : 0;

    rc = backend->apply_reloc(&ctx, type, place, sym_value, addend, out_value);
    if (rc == 0) {
        return ELF_OK;
    }
    if (rc == -1) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_UNSUPPORTED, "unsupported relocation type");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        (void)elf__append_diag_fmt((elfobj_t *)obj, "reloc-type=", type);
        return ELF_ERR_UNSUPPORTED;
    }
    if (rc == -2) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_RELOC, "relocation overflow");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        (void)elf__append_diag_fmt((elfobj_t *)obj, "reloc-type=", type);
        return ELF_ERR_RELOC;
    }
    elf__set_err((elfobj_t *)obj, ELF_ERR_RELOC, "relocation backend apply failure");
    return ELF_ERR_RELOC;
}

elf_err_t elf_apply_relocation(const elf_reloc_t *reloc, uint64_t place, uint64_t sym_value,
                               uint64_t *out_value) {
    elfobj_t *obj;
    elf_err_t err;

    if (reloc == NULL || reloc->section == NULL || reloc->section->obj == NULL || out_value == NULL) {
        return ELF_ERR_STATE;
    }
    obj = reloc->section->obj;

    if (obj->reloc_hooks.before_apply != NULL) {
        if (!obj->reloc_hooks.before_apply(reloc, obj->reloc_hook_user)) {
            elf__set_err(obj, ELF_ERR_RELOC, "relocation apply blocked by relax hook");
            return ELF_ERR_RELOC;
        }
    }

    err = elf_apply_relocation_value(obj, reloc->type, place, sym_value, reloc->addend, out_value);
    if (err != ELF_OK) {
        return err;
    }

    if (obj->reloc_hooks.after_apply != NULL) {
        obj->reloc_hooks.after_apply(reloc, *out_value, obj->reloc_hook_user);
    }
    if (obj->reloc_hooks.incremental_note != NULL) {
        obj->reloc_hooks.incremental_note("reloc_type", reloc->type, obj->reloc_hook_user);
    }
    return ELF_OK;
}

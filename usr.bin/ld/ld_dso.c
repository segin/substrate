/*
 * ld_dso.c -- shared objects as inputs: what they define, and symbol versions.
 */

#include "ld.h"

static const char *safe_strtab_name(const uint8_t *strtab, size_t strtab_sz, uint32_t off) {
    size_t i;

    if (strtab == NULL || off >= strtab_sz) {
        return NULL;
    }
    for (i = off; i < strtab_sz; ++i) {
        if (strtab[i] == '\0') {
            return (const char *)(strtab + off);
        }
    }
    return NULL;
}

static void verdef_table_free(verdef_table_t *tab) {
    size_t i;

    if (tab == NULL) {
        return;
    }
    for (i = 0; i < tab->count; ++i) {
        free(tab->items[i].name);
    }
    free(tab->items);
    tab->items = NULL;
    tab->count = 0;
    tab->cap = 0;
}

static int verdef_table_add(verdef_table_t *tab, uint16_t index, const char *name) {
    verdef_name_t *next;
    size_t i;

    if (tab == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < tab->count; ++i) {
        if (tab->items[i].index == index) {
            return 0;
        }
    }
    if (tab->count == tab->cap) {
        size_t ncap = tab->cap == 0 ? 8 : tab->cap * 2;
        next = (verdef_name_t *)realloc(tab->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        tab->items = next;
        tab->cap = ncap;
    }
    tab->items[tab->count].index = index;
    tab->items[tab->count].name = xstrdup(name);
    if (tab->items[tab->count].name == NULL) {
        return -1;
    }
    tab->count++;
    return 0;
}

static const char *verdef_lookup(const verdef_table_t *tab, uint16_t index) {
    size_t i;

    if (tab == NULL || index <= VER_NDX_GLOBAL) {
        return NULL;
    }
    for (i = 0; i < tab->count; ++i) {
        if (tab->items[i].index == index) {
            return tab->items[i].name;
        }
    }
    return NULL;
}

static void dyn_ver_plan_init(dyn_ver_plan_t *plan) {
    if (plan == NULL) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
    plan->next_index = 2;
}

static void dyn_ver_plan_free(dyn_ver_plan_t *plan) {
    size_t i;

    if (plan == NULL) {
        return;
    }
    for (i = 0; i < plan->def_count; ++i) {
        free(plan->defs[i].name);
    }
    free(plan->defs);
    for (i = 0; i < plan->need_count; ++i) {
        free(plan->needs[i].file);
        free(plan->needs[i].name);
    }
    free(plan->needs);
    memset(plan, 0, sizeof(*plan));
}

static int dyn_ver_plan_alloc_index(dyn_ver_plan_t *plan, uint16_t *out) {
    if (plan == NULL || out == NULL) {
        return -1;
    }
    if (plan->next_index >= VER_NDX_HIDDEN) {
        return -1;
    }
    *out = plan->next_index++;
    return 0;
}

static int dyn_ver_plan_get_or_add_def(dyn_ver_plan_t *plan, const char *name, uint16_t *out_index) {
    dyn_verdef_t *next;
    uint16_t idx;
    size_t i;

    if (plan == NULL || name == NULL || name[0] == '\0' || out_index == NULL) {
        return -1;
    }
    for (i = 0; i < plan->def_count; ++i) {
        if (strcmp(plan->defs[i].name, name) == 0) {
            *out_index = plan->defs[i].index;
            return 0;
        }
    }
    if (dyn_ver_plan_alloc_index(plan, &idx) != 0) {
        return -1;
    }
    if (plan->def_count == plan->def_cap) {
        size_t ncap = plan->def_cap == 0 ? 8 : plan->def_cap * 2;
        next = (dyn_verdef_t *)realloc(plan->defs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        plan->defs = next;
        plan->def_cap = ncap;
    }
    plan->defs[plan->def_count].name = xstrdup(name);
    if (plan->defs[plan->def_count].name == NULL) {
        return -1;
    }
    plan->defs[plan->def_count].index = idx;
    plan->defs[plan->def_count].name_off = UINT32_MAX;
    plan->def_count++;
    *out_index = idx;
    return 0;
}

static int dyn_ver_plan_get_or_add_need(dyn_ver_plan_t *plan, const char *file, const char *name, uint16_t *out_index) {
    dyn_verneed_t *next;
    uint16_t idx;
    size_t i;

    if (plan == NULL || file == NULL || name == NULL || file[0] == '\0' || name[0] == '\0' || out_index == NULL) {
        return -1;
    }
    for (i = 0; i < plan->need_count; ++i) {
        if (strcmp(plan->needs[i].file, file) == 0 && strcmp(plan->needs[i].name, name) == 0) {
            *out_index = plan->needs[i].index;
            return 0;
        }
    }
    if (dyn_ver_plan_alloc_index(plan, &idx) != 0) {
        return -1;
    }
    if (plan->need_count == plan->need_cap) {
        size_t ncap = plan->need_cap == 0 ? 8 : plan->need_cap * 2;
        next = (dyn_verneed_t *)realloc(plan->needs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        plan->needs = next;
        plan->need_cap = ncap;
    }
    plan->needs[plan->need_count].file = xstrdup(file);
    plan->needs[plan->need_count].name = xstrdup(name);
    if (plan->needs[plan->need_count].file == NULL || plan->needs[plan->need_count].name == NULL) {
        free(plan->needs[plan->need_count].file);
        free(plan->needs[plan->need_count].name);
        return -1;
    }
    plan->needs[plan->need_count].index = idx;
    plan->needs[plan->need_count].file_off = UINT32_MAX;
    plan->needs[plan->need_count].name_off = UINT32_MAX;
    plan->need_count++;
    *out_index = idx;
    return 0;
}

static int dyn_ver_plan_ensure_def_index(dyn_ver_plan_t *plan, const char *name, uint16_t index) {
    dyn_verdef_t *next;
    size_t i;

    if (plan == NULL || name == NULL || name[0] == '\0' || index <= VER_NDX_GLOBAL || index >= VER_NDX_HIDDEN) {
        return -1;
    }
    for (i = 0; i < plan->def_count; ++i) {
        if (strcmp(plan->defs[i].name, name) == 0) {
            return plan->defs[i].index == index ? 0 : -1;
        }
    }
    if (plan->def_count == plan->def_cap) {
        size_t ncap = plan->def_cap == 0 ? 8 : plan->def_cap * 2;
        next = (dyn_verdef_t *)realloc(plan->defs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        plan->defs = next;
        plan->def_cap = ncap;
    }
    plan->defs[plan->def_count].name = xstrdup(name);
    if (plan->defs[plan->def_count].name == NULL) {
        return -1;
    }
    plan->defs[plan->def_count].index = index;
    plan->defs[plan->def_count].name_off = UINT32_MAX;
    plan->def_count++;
    if (plan->next_index <= index) {
        plan->next_index = (uint16_t)(index + 1);
    }
    return 0;
}

static int dyn_ver_plan_ensure_need_index(dyn_ver_plan_t *plan, const char *file, const char *name, uint16_t index) {
    dyn_verneed_t *next;
    size_t i;

    if (plan == NULL || file == NULL || name == NULL || file[0] == '\0' || name[0] == '\0' ||
        index <= VER_NDX_GLOBAL || index >= VER_NDX_HIDDEN) {
        return -1;
    }
    for (i = 0; i < plan->need_count; ++i) {
        if (strcmp(plan->needs[i].file, file) == 0 && strcmp(plan->needs[i].name, name) == 0) {
            return plan->needs[i].index == index ? 0 : -1;
        }
    }
    if (plan->need_count == plan->need_cap) {
        size_t ncap = plan->need_cap == 0 ? 8 : plan->need_cap * 2;
        next = (dyn_verneed_t *)realloc(plan->needs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        plan->needs = next;
        plan->need_cap = ncap;
    }
    plan->needs[plan->need_count].file = xstrdup(file);
    plan->needs[plan->need_count].name = xstrdup(name);
    if (plan->needs[plan->need_count].file == NULL || plan->needs[plan->need_count].name == NULL) {
        free(plan->needs[plan->need_count].file);
        free(plan->needs[plan->need_count].name);
        return -1;
    }
    plan->needs[plan->need_count].index = index;
    plan->needs[plan->need_count].file_off = UINT32_MAX;
    plan->needs[plan->need_count].name_off = UINT32_MAX;
    plan->need_count++;
    if (plan->next_index <= index) {
        plan->next_index = (uint16_t)(index + 1);
    }
    return 0;
}

static int load_dso_verdef_table(const elfobj_t *obj, verdef_table_t *out) {
    const elf_section_t *verdef_sec;
    const elf_section_t *dynstr_sec;
    const uint8_t *verdef_data;
    const uint8_t *dynstr_data;
    size_t verdef_sz;
    size_t dynstr_sz;
    size_t off;
    elfobj_endian_t endian;

    if (obj == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    verdef_sec = elf_find_section((elfobj_t *)obj, ".gnu.version_d");
    if (verdef_sec == NULL) {
        return 0;
    }
    dynstr_sec = elf_find_section((elfobj_t *)obj, ".dynstr");
    if (dynstr_sec == NULL) {
        return -1;
    }
    verdef_data = (const uint8_t *)elf_section_data(verdef_sec, &verdef_sz);
    dynstr_data = (const uint8_t *)elf_section_data(dynstr_sec, &dynstr_sz);
    if (verdef_data == NULL || dynstr_data == NULL || verdef_sz == 0 || dynstr_sz == 0) {
        return -1;
    }
    endian = elf_endian(obj);
    off = 0;
    while (off + 20 <= verdef_sz) {
        uint16_t vd_ndx = read_u16_endian(verdef_data + off + 4, endian);
        uint32_t vd_aux = read_u32_endian(verdef_data + off + 12, endian);
        uint32_t vd_next = read_u32_endian(verdef_data + off + 16, endian);
        size_t aux_off;
        uint32_t name_off;
        const char *name;

        if (vd_aux == 0 || vd_aux > verdef_sz - off || off + vd_aux + 8 > verdef_sz) {
            verdef_table_free(out);
            return -1;
        }
        aux_off = off + vd_aux;
        name_off = read_u32_endian(verdef_data + aux_off + 0, endian);
        name = safe_strtab_name(dynstr_data, dynstr_sz, name_off);
        if (verdef_table_add(out, (uint16_t)(vd_ndx & (uint16_t)~VER_NDX_HIDDEN), name) != 0) {
            verdef_table_free(out);
            return -1;
        }
        if (vd_next == 0) {
            break;
        }
        if (vd_next < 20 || vd_next > verdef_sz - off) {
            verdef_table_free(out);
            return -1;
        }
        off += vd_next;
    }
    return 0;
}

void split_symbol_version(const char *name, const char **base, size_t *base_len, const char **ver_name,
                                 int *is_default) {
    const char *at2;
    const char *at1;

    if (base != NULL) {
        *base = name;
    }
    if (base_len != NULL) {
        *base_len = name != NULL ? strlen(name) : 0;
    }
    if (ver_name != NULL) {
        *ver_name = NULL;
    }
    if (is_default != NULL) {
        *is_default = 0;
    }
    if (name == NULL || name[0] == '\0') {
        return;
    }
    at2 = strstr(name, "@@");
    if (at2 != NULL) {
        if (base_len != NULL) {
            *base_len = (size_t)(at2 - name);
        }
        if (ver_name != NULL && at2[2] != '\0') {
            *ver_name = at2 + 2;
        }
        if (is_default != NULL) {
            *is_default = 1;
        }
        return;
    }
    at1 = strchr(name, '@');
    if (at1 != NULL) {
        if (base_len != NULL) {
            *base_len = (size_t)(at1 - name);
        }
        if (ver_name != NULL && at1[1] != '\0') {
            *ver_name = at1 + 1;
        }
    }
}

static char *make_versioned_symbol(const char *base, size_t base_len, const char *sep, const char *ver_name) {
    size_t sep_len;
    size_t ver_len;
    char *out;

    if (base == NULL || sep == NULL || ver_name == NULL) {
        return NULL;
    }
    sep_len = strlen(sep);
    ver_len = strlen(ver_name);
    if (base_len > SIZE_MAX - sep_len - ver_len - 1) {
        return NULL;
    }
    out = (char *)malloc(base_len + sep_len + ver_len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, base, base_len);
    memcpy(out + base_len, sep, sep_len);
    memcpy(out + base_len + sep_len, ver_name, ver_len);
    out[base_len + sep_len + ver_len] = '\0';
    return out;
}

static int symstate_define_name(symstate_t *state, const char *name) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (symset_add(&state->defined, name) != 0) {
        return -1;
    }
    symset_remove(&state->unresolved, name);
    return 0;
}

static int dso_symbol_match_unresolved(const symstate_t *state, const char *sym_name, uint16_t sym_ver,
                                       const verdef_table_t *defs) {
    const char *base;
    size_t base_len;
    const char *ver_name;
    int is_default_name;
    int hidden;

    split_symbol_version(sym_name, &base, &base_len, &ver_name, &is_default_name);
    /* Not the default version: by the bit beside a dynamic symbol, or by
     * the single '@' in the name of one read from a full symbol table. */
    hidden = (sym_ver & VER_NDX_HIDDEN) != 0 || (ver_name != NULL && !is_default_name);
    sym_ver = (uint16_t)(sym_ver & (uint16_t)~VER_NDX_HIDDEN);
    if (ver_name == NULL && sym_ver > VER_NDX_GLOBAL) {
        ver_name = verdef_lookup(defs, sym_ver);
    }
    if (ver_name != NULL) {
        char *at_name = make_versioned_symbol(base, base_len, "@", ver_name);
        if (at_name != NULL) {
            if (symset_contains(&state->unresolved, at_name)) {
                free(at_name);
                return 1;
            }
            free(at_name);
        }
        if (!hidden || is_default_name) {
            char *at_at_name = make_versioned_symbol(base, base_len, "@@", ver_name);
            char *plain = NULL;
            int matched = 0;
            if (at_at_name != NULL && symset_contains(&state->unresolved, at_at_name)) {
                matched = 1;
            }
            plain = (char *)malloc(base_len + 1);
            if (plain != NULL) {
                memcpy(plain, base, base_len);
                plain[base_len] = '\0';
                if (symset_contains(&state->unresolved, plain)) {
                    matched = 1;
                }
            }
            free(at_at_name);
            free(plain);
            if (matched) {
                return 1;
            }
        } else {
            /* A version that is not the default is for those who ask for
             * it by name, "sym@VER": a plain reference to sym is not
             * one, and where there is no default version sym is not
             * defined for it at all. */
            return 0;
        }
    }
    return symset_contains(&state->unresolved, sym_name);
}

static int symstate_note_dso_symbol(symstate_t *state, const char *sym_name, uint16_t sym_ver,
                                    const verdef_table_t *defs) {
    const char *base;
    size_t base_len;
    const char *ver_name;
    int is_default_name;
    int hidden;

    split_symbol_version(sym_name, &base, &base_len, &ver_name, &is_default_name);
    /* Not the default version: by the bit beside a dynamic symbol, or by
     * the single '@' in the name of one read from a full symbol table. */
    hidden = (sym_ver & VER_NDX_HIDDEN) != 0 || (ver_name != NULL && !is_default_name);
    sym_ver = (uint16_t)(sym_ver & (uint16_t)~VER_NDX_HIDDEN);
    if (ver_name == NULL && sym_ver > VER_NDX_GLOBAL) {
        ver_name = verdef_lookup(defs, sym_ver);
    }
    /* The name as it stands is defined -- unless this is a version that
     * is not the default, which defines "sym@VER" below and not "sym". */
    if (!(hidden && ver_name != NULL && !is_default_name) && symstate_define_name(state, sym_name) != 0) {
        return -1;
    }
    if (base == NULL || base_len == 0) {
        return 0;
    }
    if (ver_name != NULL) {
        char *at_name = make_versioned_symbol(base, base_len, "@", ver_name);
        if (at_name == NULL || symstate_define_name(state, at_name) != 0) {
            free(at_name);
            return -1;
        }
        free(at_name);
        if (!hidden || is_default_name) {
            char *at_at_name = make_versioned_symbol(base, base_len, "@@", ver_name);
            char *plain = (char *)malloc(base_len + 1);
            if (at_at_name == NULL || plain == NULL) {
                free(at_at_name);
                free(plain);
                return -1;
            }
            memcpy(plain, base, base_len);
            plain[base_len] = '\0';
            if (symstate_define_name(state, at_at_name) != 0 || symstate_define_name(state, plain) != 0) {
                free(at_at_name);
                free(plain);
                return -1;
            }
            free(at_at_name);
            free(plain);
        }
        return 0;
    }
    if (base_len != strlen(sym_name)) {
        char *plain = (char *)malloc(base_len + 1);
        if (plain == NULL) {
            return -1;
        }
        memcpy(plain, base, base_len);
        plain[base_len] = '\0';
        if (symstate_define_name(state, plain) != 0) {
            free(plain);
            return -1;
        }
        free(plain);
    }
    return 0;
}

int shared_object_matches_unresolved(const char *path, ld_ctx_t *ctx, const symstate_t *state,
                                            int *out_match) {
    elfobj_t *obj = NULL;
    verdef_table_t defs;
    size_t i;

    *out_match = 0;
    if (state == NULL || state->unresolved.count == 0) {
        return 0;
    }
    if (elf_open(path, &obj) != ELF_OK) {
        return -1;
    }
    maybe_autoswitch_mode(ctx, obj, 0, path);
    if (!obj_matches_mode(obj, ctx->mode) || elf_type(obj) != ET_DYN) {
        elf_close(obj);
        return 0;
    }
    if (load_dso_verdef_table(obj, &defs) != 0) {
        elf_close(obj);
        return -1;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint16_t shndx;
        uint16_t ver;
        uint8_t bind;
        uint8_t vis;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        vis = elf_symbol_visibility(sym);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        if (vis != STV_DEFAULT && vis != STV_PROTECTED) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        ver = elf_symbol_version(sym);
        if (shndx != SHN_UNDEF && dso_symbol_match_unresolved(state, name, ver, &defs)) {
            *out_match = 1;
            break;
        }
    }
    verdef_table_free(&defs);
    elf_close(obj);
    return 0;
}

int register_dso_provider(ld_ctx_t *ctx, const char *path, symstate_t *state) {
    elfobj_t *obj = NULL;
    verdef_table_t defs;
    size_t i;

    if (elf_open(path, &obj) != ELF_OK) {
        return -1;
    }
    maybe_autoswitch_mode(ctx, obj, 0, path);
    if (!obj_matches_mode(obj, ctx->mode) || elf_type(obj) != ET_DYN) {
        elf_close(obj);
        return -1;
    }
    if (load_dso_verdef_table(obj, &defs) != 0) {
        elf_close(obj);
        return -1;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint8_t bind;
        uint8_t vis;
        uint16_t shndx;
        uint16_t ver;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        vis = elf_symbol_visibility(sym);
        shndx = elf_symbol_shndx(sym);
        if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
            (vis == STV_DEFAULT || vis == STV_PROTECTED) &&
            shndx != SHN_UNDEF) {
            ver = elf_symbol_version(sym);
            if (symstate_note_dso_symbol(state, name, ver, &defs) != 0) {
                verdef_table_free(&defs);
                elf_close(obj);
                return -1;
            }
        }
    }
    verdef_table_free(&defs);
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        if (strcmp(ctx->dso_inputs.items[i], path) == 0) {
            elf_close(obj);
            return 0;
        }
    }
    if (strvec_push(&ctx->dso_inputs, path) != 0) {
        elf_close(obj);
        return -1;
    }
    if (ctx->trace_inputs) {
        fprintf(stderr, "ld: trace: dso %s\n", path);
    }
    elf_close(obj);
    /*
     * --copy-dt-needed-entries: what this library needs may supply what
     * the program refers to, as if it had been named too.  A C++ program
     * is linked with libstdc++ and gets _Unwind_Resume from the libgcc_s
     * that libstdc++ needs.  Each is looked for as -l:NAME would be, and
     * taken, with what it needs in turn, if it defines something still
     * wanted.
     */
    if (ctx->copy_dt_needed) {
        strvec_t needed;
        int rc = 0;

        memset(&needed, 0, sizeof(needed));
        if (dso_dynamic_strings(path, DT_NEEDED, &needed) == 0) {
            for (i = 0; rc == 0 && i < needed.count; ++i) {
                char *dep = resolve_library_path_exact(ctx, needed.items[i]);
                int wanted = 0;
                size_t k;

                for (k = 0; dep != NULL && k < ctx->dso_inputs.count; ++k) {
                    if (strcmp(ctx->dso_inputs.items[k], dep) == 0) {
                        free(dep);
                        dep = NULL;
                    }
                }
                if (dep != NULL && shared_object_matches_unresolved(dep, ctx, state, &wanted) == 0 && wanted) {
                    rc = register_dso_provider(ctx, dep, state);
                }
                free(dep);
            }
        }
        strvec_free(&needed);
        return rc;
    }
    return 0;
}

int unresolved_symbol_has_dso_provider(ld_ctx_t *ctx, const char *name, int *out_has_provider) {
    symstate_t probe;
    size_t i;

    if (out_has_provider == NULL) {
        return -1;
    }
    *out_has_provider = 0;
    if (ctx == NULL || name == NULL || name[0] == '\0' || ctx->dso_inputs.count == 0) {
        return 0;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        elfobj_t *obj = NULL;
        const elf_symbol_t *sym;
        uint8_t bind;
        uint8_t vis;
        uint16_t shndx;

        if (elf_open(ctx->dso_inputs.items[i], &obj) != ELF_OK) {
            continue;
        }
        maybe_autoswitch_mode(ctx, obj, 0, ctx->dso_inputs.items[i]);
        if (!obj_matches_mode(obj, ctx->mode) || elf_type(obj) != ET_DYN) {
            elf_close(obj);
            continue;
        }
        sym = elf_find_symbol(obj, name);
        if (sym != NULL) {
            bind = elf_symbol_bind(sym);
            vis = elf_symbol_visibility(sym);
            shndx = elf_symbol_shndx(sym);
            /* One found by its bare name and marked hidden is a version
             * that is not the default; whether there is a default one as
             * well is for the search below, which knows about versions. */
            if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
                (vis == STV_DEFAULT || vis == STV_PROTECTED) &&
                shndx != SHN_UNDEF && (elf_symbol_version(sym) & VER_NDX_HIDDEN) == 0) {
                *out_has_provider = 1;
                elf_close(obj);
                return 0;
            }
        }
        elf_close(obj);
    }

    memset(&probe, 0, sizeof(probe));
    if (symset_add(&probe.unresolved, name) != 0) {
        symstate_free(&probe);
        return -1;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        int matched = 0;

        if (shared_object_matches_unresolved(ctx->dso_inputs.items[i], ctx, &probe, &matched) != 0) {
            symstate_free(&probe);
            return -1;
        }
        if (matched) {
            *out_has_provider = 1;
            break;
        }
    }
    symstate_free(&probe);
    return 0;
}

static uint32_t dynsym_name_off_raw(const uint8_t *dynsym, size_t dynsym_len, size_t entsz,
                                    elfobj_endian_t endian, size_t index) {
    size_t off = index * entsz;

    if (dynsym == NULL || entsz == 0 || off > dynsym_len || dynsym_len - off < entsz) {
        return 0;
    }
    return read_u32_endian(dynsym + off, endian);
}

static int dynsym_shndx_raw(const uint8_t *dynsym, size_t dynsym_len, size_t entsz, elfobj_class_t cls,
                            elfobj_endian_t endian, size_t index, uint16_t *out_shndx) {
    size_t off = index * entsz;

    if (dynsym == NULL || entsz == 0 || out_shndx == NULL || off > dynsym_len || dynsym_len - off < entsz) {
        return -1;
    }
    if (cls == ELFOBJ_CLASS_64) {
        *out_shndx = read_u16_endian(dynsym + off + 6, endian);
    } else {
        *out_shndx = read_u16_endian(dynsym + off + 14, endian);
    }
    return 0;
}

static int versym_read_raw(const uint8_t *versym, size_t versym_len, elfobj_endian_t endian, size_t index,
                           uint16_t *out_ver) {
    size_t off = index * 2;

    if (versym == NULL || out_ver == NULL || off > versym_len || versym_len - off < 2) {
        return -1;
    }
    *out_ver = read_u16_endian(versym + off, endian);
    return 0;
}

static int versym_write_raw(uint8_t *versym, size_t versym_len, elfobj_endian_t endian, size_t index, uint16_t ver) {
    size_t off = index * 2;

    if (versym == NULL || off > versym_len || versym_len - off < 2) {
        return -1;
    }
    write_u16_endian(versym + off, endian, ver);
    return 0;
}

static int dso_has_versioned_export(const ld_ctx_t *ctx, const char *path, const char *base, size_t base_len,
                                    const char *ver_name) {
    char *at_name = NULL;
    char *atat_name = NULL;
    symstate_t state;
    int matched = 0;

    if (ctx == NULL || path == NULL || base == NULL || base_len == 0 || ver_name == NULL || ver_name[0] == '\0') {
        return 0;
    }
    memset(&state, 0, sizeof(state));
    at_name = make_versioned_symbol(base, base_len, "@", ver_name);
    atat_name = make_versioned_symbol(base, base_len, "@@", ver_name);
    if (at_name == NULL || atat_name == NULL ||
        symset_add(&state.unresolved, at_name) != 0 || symset_add(&state.unresolved, atat_name) != 0 ||
        shared_object_matches_unresolved(path, (ld_ctx_t *)ctx, &state, &matched) != 0) {
        free(at_name);
        free(atat_name);
        symstate_free(&state);
        return 0;
    }
    free(at_name);
    free(atat_name);
    symstate_free(&state);
    return matched != 0;
}

/* The name the i'th shared object of the link is asked for by at run time. */
const char *dso_needed_name(const ld_ctx_t *ctx, size_t i) {
    const char *path = ctx->dso_inputs.items[i];
    const char *leaf = strrchr(path, '/');

    if (i < ctx->dso_names.count) {
        return ctx->dso_names.items[i];
    }
    return leaf != NULL ? leaf + 1 : path;
}

/*
 * Learn those names, once the shared objects of the link are known, and
 * what each refers to without defining.  The second is for the dynamic
 * symbol table of an executable: a library that calls back into the
 * program, or that needs a function the program has from an archive, can
 * only find it there.
 */
int note_dso_names(ld_ctx_t *ctx) {
    while (ctx->dso_names.count < ctx->dso_inputs.count) {
        size_t i = ctx->dso_names.count;
        char *soname = dso_soname(ctx->dso_inputs.items[i]);
        int rc = strvec_push(&ctx->dso_names, soname != NULL ? soname : dso_needed_name(ctx, i));
        elfobj_t *obj = NULL;
        size_t k;

        free(soname);
        if (rc != 0) {
            return -1;
        }
        if (elf_open(ctx->dso_inputs.items[i], &obj) != ELF_OK) {
            continue;
        }
        for (k = 0; k < elf_symbol_count(obj); ++k) {
            const elf_symbol_t *sym = elf_symbol_at(obj, k);
            const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;

            /* ...and what it defines for all to see: the program's
             * definition of the same name is the one the library is to
             * use too, and it can only be found if it is exported. */
            if (name != NULL && name[0] != '\0' &&
                (elf_symbol_shndx(sym) == SHN_UNDEF || elf_symbol_visibility(sym) == STV_DEFAULT) &&
                (elf_symbol_bind(sym) == STB_GLOBAL || elf_symbol_bind(sym) == STB_WEAK) &&
                !symset_contains(&ctx->dso_wants, name) && symset_add(&ctx->dso_wants, name) != 0) {
                elf_close(obj);
                return -1;
            }
        }
        elf_close(obj);
    }
    return 0;
}

static const char *resolve_version_need_provider(const ld_ctx_t *ctx, const char *base, size_t base_len,
                                                 const char *ver_name) {
    size_t i;

    if (ctx == NULL || base == NULL || base_len == 0 || ver_name == NULL || ver_name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        const char *path = ctx->dso_inputs.items[i];
        if (path == NULL || path[0] == '\0') {
            continue;
        }
        if (!dso_has_versioned_export(ctx, path, base, base_len, ver_name)) {
            continue;
        }
        return dso_needed_name(ctx, i);
    }
    return NULL;
}

/*
 * What a reference to plain `base` binds to in this shared object: 1 and
 * the name of the version, if it has a default version of it; 2 if it
 * defines it with no version; 0 if it does not define it, which is also
 * the answer where every definition it has is of a version that is not
 * the default -- those are for references that name the version.
 */
static int dso_find_default_version_export(const ld_ctx_t *ctx, const char *path, const char *base, size_t base_len,
                                           char **out_ver_name) {
    elfobj_t *obj = NULL;
    verdef_table_t defs;
    size_t i;
    int found = 0;
    int plain = 0;

    if (out_ver_name == NULL) {
        return -1;
    }
    *out_ver_name = NULL;
    if (ctx == NULL || path == NULL || base == NULL || base_len == 0) {
        return 0;
    }
    if (elf_open(path, &obj) != ELF_OK) {
        return 0;
    }
    maybe_autoswitch_mode((ld_ctx_t *)ctx, obj, 0, path);
    if (!obj_matches_mode(obj, ctx->mode) || elf_type(obj) != ET_DYN) {
        elf_close(obj);
        return 0;
    }
    if (load_dso_verdef_table(obj, &defs) != 0) {
        elf_close(obj);
        return -1;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        const char *sym_base = NULL;
        const char *ver_name;
        size_t sym_base_len = 0;
        uint8_t bind;
        uint8_t vis;
        uint16_t shndx;
        uint16_t sym_ver;
        int hidden;
        char *dup;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        vis = elf_symbol_visibility(sym);
        shndx = elf_symbol_shndx(sym);
        if ((bind != STB_GLOBAL && bind != STB_WEAK) || (vis != STV_DEFAULT && vis != STV_PROTECTED) ||
            shndx == SHN_UNDEF) {
            continue;
        }
        split_symbol_version(name, &sym_base, &sym_base_len, NULL, NULL);
        if (sym_base == NULL || sym_base_len != base_len ||
            memcmp(sym_base, base, base_len) != 0) {
            continue;
        }
        sym_ver = elf_symbol_version(sym);
        hidden = (sym_ver & VER_NDX_HIDDEN) != 0;
        sym_ver = (uint16_t)(sym_ver & (uint16_t)~VER_NDX_HIDDEN);
        if (sym_ver <= VER_NDX_GLOBAL) {
            /* "sym@VER" in the full symbol table is the versioned symbol
             * over again, spelt out; the dynamic one has the number. */
            if (!hidden && sym_base_len == strlen(name)) {
                plain = 1;
            }
            continue;
        }
        ver_name = verdef_lookup(&defs, sym_ver);
        if (hidden || ver_name == NULL || ver_name[0] == '\0') {
            continue;
        }
        dup = xstrdup(ver_name);
        if (dup == NULL) {
            verdef_table_free(&defs);
            elf_close(obj);
            return -1;
        }
        *out_ver_name = dup;
        found = 1;
        break;
    }
    verdef_table_free(&defs);
    elf_close(obj);
    return found ? 1 : plain ? 2 : 0;
}

static int resolve_default_version_need(const ld_ctx_t *ctx, const char *base, size_t base_len,
                                        const char **out_provider, char **out_ver_name) {
    size_t i;

    if (out_provider == NULL || out_ver_name == NULL) {
        return -1;
    }
    *out_provider = NULL;
    *out_ver_name = NULL;
    if (ctx == NULL || base == NULL || base_len == 0) {
        return 0;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        const char *path = ctx->dso_inputs.items[i];
        char *ver_name = NULL;
        int rc;

        if (path == NULL || path[0] == '\0') {
            continue;
        }
        rc = dso_find_default_version_export(ctx, path, base, base_len, &ver_name);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            continue;
        }
        if (rc == 2) {
            /* The first shared object to define it is the one bound to,
             * and this one defines it with no version: a later one's
             * version of the same name is not what was found. */
            return 0;
        }
        *out_provider = dso_needed_name(ctx, i);
        *out_ver_name = ver_name;
        return 1;
    }
    return 0;
}

static int dyn_ver_plan_assign_dynstr_offsets(dyn_ver_plan_t *plan, uint8_t **dynstr_buf, size_t *dynstr_len,
                                              size_t *dynstr_cap) {
    size_t i;

    if (plan == NULL || dynstr_buf == NULL || dynstr_len == NULL || dynstr_cap == NULL) {
        return -1;
    }
    for (i = 0; i < plan->def_count; ++i) {
        if (plan->defs[i].name_off != UINT32_MAX) {
            continue;
        }
        if (dynstr_append_cstr(dynstr_buf, dynstr_len, dynstr_cap, plan->defs[i].name, &plan->defs[i].name_off) != 0) {
            return -1;
        }
    }
    for (i = 0; i < plan->need_count; ++i) {
        if (plan->needs[i].file_off == UINT32_MAX &&
            dynstr_append_cstr(dynstr_buf, dynstr_len, dynstr_cap, plan->needs[i].file, &plan->needs[i].file_off) != 0) {
            return -1;
        }
        if (plan->needs[i].name_off == UINT32_MAX &&
            dynstr_append_cstr(dynstr_buf, dynstr_len, dynstr_cap, plan->needs[i].name, &plan->needs[i].name_off) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * .gnu.version_d: the versions this object defines.  The first is the
 * base, index 1, flagged VER_FLG_BASE and named for the object itself: it
 * is what the unversioned symbols belong to, and a reader of the section
 * takes the first entry for it whether it is or not.
 */
static int build_gnu_verdef_data(const dyn_ver_plan_t *plan, elfobj_endian_t endian, const char *base_name,
                                 uint32_t base_name_off, uint8_t **out_buf, size_t *out_sz) {
    uint8_t *buf;
    size_t i;
    size_t off;

    if (out_buf == NULL || out_sz == NULL || plan == NULL || plan->def_count == 0) {
        return -1;
    }
    if (plan->def_count > SIZE_MAX / 28 - 1) {
        return -1;
    }
    *out_sz = (plan->def_count + 1) * 28;
    buf = (uint8_t *)calloc(1, *out_sz);
    if (buf == NULL) {
        return -1;
    }
    write_u16_endian(buf + 0, endian, 1);               /* vd_version */
    write_u16_endian(buf + 2, endian, 1);               /* VER_FLG_BASE */
    write_u16_endian(buf + 4, endian, 1);               /* vd_ndx */
    write_u16_endian(buf + 6, endian, 1);               /* vd_cnt */
    write_u32_endian(buf + 8, endian, elf_hash_sysv(base_name));
    write_u32_endian(buf + 12, endian, 20);             /* vd_aux */
    write_u32_endian(buf + 16, endian, 28);             /* vd_next */
    write_u32_endian(buf + 20, endian, base_name_off);
    write_u32_endian(buf + 24, endian, 0);
    off = 28;
    for (i = 0; i < plan->def_count; ++i) {
        size_t next = i + 1 < plan->def_count ? 28 : 0;
        write_u16_endian(buf + off + 0, endian, 1);
        write_u16_endian(buf + off + 2, endian, 0);
        write_u16_endian(buf + off + 4, endian, plan->defs[i].index);
        write_u16_endian(buf + off + 6, endian, 1);
        write_u32_endian(buf + off + 8, endian, elf_hash_sysv(plan->defs[i].name));
        write_u32_endian(buf + off + 12, endian, 20);
        write_u32_endian(buf + off + 16, endian, (uint32_t)next);
        write_u32_endian(buf + off + 20, endian, plan->defs[i].name_off);
        write_u32_endian(buf + off + 24, endian, 0);
        off += 28;
    }
    *out_buf = buf;
    return 0;
}

static int build_gnu_verneed_data(const dyn_ver_plan_t *plan, elfobj_endian_t endian, uint8_t **out_buf, size_t *out_sz,
                                  size_t *out_need_file_count) {
    typedef struct {
        const char *file;
        uint32_t file_off;
        size_t count;
    } need_file_t;

    need_file_t *files = NULL;
    uint8_t *buf = NULL;
    size_t file_count = 0;
    size_t file_cap = 0;
    size_t total = 0;
    size_t i;
    size_t off;

    if (out_buf == NULL || out_sz == NULL || out_need_file_count == NULL || plan == NULL || plan->need_count == 0) {
        return -1;
    }
    for (i = 0; i < plan->need_count; ++i) {
        size_t j;
        int found = 0;
        for (j = 0; j < file_count; ++j) {
            if (strcmp(files[j].file, plan->needs[i].file) == 0) {
                files[j].count++;
                found = 1;
                break;
            }
        }
        if (found) {
            continue;
        }
        if (file_count == file_cap) {
            size_t ncap = file_cap == 0 ? 4 : file_cap * 2;
            need_file_t *next = (need_file_t *)realloc(files, ncap * sizeof(*next));
            if (next == NULL) {
                free(files);
                return -1;
            }
            files = next;
            file_cap = ncap;
        }
        files[file_count].file = plan->needs[i].file;
        files[file_count].file_off = plan->needs[i].file_off;
        files[file_count].count = 1;
        file_count++;
    }
    for (i = 0; i < file_count; ++i) {
        if (files[i].count > ((size_t)UINT16_MAX)) {
            free(files);
            return -1;
        }
        if (files[i].count > (SIZE_MAX - total - 16) / 16) {
            free(files);
            return -1;
        }
        total += 16 + (files[i].count * 16);
    }

    buf = (uint8_t *)calloc(1, total);
    if (buf == NULL) {
        free(files);
        return -1;
    }
    off = 0;
    for (i = 0; i < file_count; ++i) {
        size_t this_sz = 16 + (files[i].count * 16);
        size_t aux_written = 0;
        size_t j;
        size_t aux_off = off + 16;

        write_u16_endian(buf + off + 0, endian, 1);
        write_u16_endian(buf + off + 2, endian, (uint16_t)files[i].count);
        write_u32_endian(buf + off + 4, endian, files[i].file_off);
        write_u32_endian(buf + off + 8, endian, 16);
        write_u32_endian(buf + off + 12, endian, (uint32_t)(i + 1 < file_count ? this_sz : 0));

        for (j = 0; j < plan->need_count; ++j) {
            if (strcmp(plan->needs[j].file, files[i].file) != 0) {
                continue;
            }
            write_u32_endian(buf + aux_off + 0, endian, elf_hash_sysv(plan->needs[j].name));
            write_u16_endian(buf + aux_off + 4, endian, 0);
            write_u16_endian(buf + aux_off + 6, endian, plan->needs[j].index);
            write_u32_endian(buf + aux_off + 8, endian, plan->needs[j].name_off);
            write_u32_endian(buf + aux_off + 12, endian, aux_written + 1 < files[i].count ? 16 : 0);
            aux_off += 16;
            aux_written++;
        }
        off += this_sz;
    }
    free(files);
    *out_buf = buf;
    *out_sz = total;
    *out_need_file_count = file_count;
    return 0;
}

int plan_symbol_version_sections(ld_ctx_t *ctx, elfobj_t *out, uint8_t **dynstr_buf, size_t *dynstr_len,
                                        size_t *dynstr_cap, const uint8_t *dynsym_buf, size_t dynsym_len, size_t entsz,
                                        uint8_t *versym_buf, size_t versym_len, size_t *out_verdef_count,
                                        size_t *out_verneed_count) {
    dyn_ver_plan_t plan;
    elfobj_endian_t endian;
    size_t nsyms;
    size_t i;
    uint8_t *verdef_data = NULL;
    uint8_t *verneed_data = NULL;
    size_t verdef_sz = 0;
    size_t verneed_sz = 0;
    size_t need_file_count = 0;

    if (ctx == NULL || out == NULL || dynstr_buf == NULL || dynstr_len == NULL || dynstr_cap == NULL ||
        dynsym_buf == NULL || entsz == 0 || versym_buf == NULL || out_verdef_count == NULL || out_verneed_count == NULL) {
        return -1;
    }
    if ((dynsym_len % entsz) != 0) {
        return -1;
    }
    nsyms = dynsym_len / entsz;
    if (versym_len < nsyms * 2) {
        return -1;
    }
    *out_verdef_count = 0;
    *out_verneed_count = 0;

    dyn_ver_plan_init(&plan);
    endian = elf_endian(out);
    for (i = 1; i < nsyms; ++i) {
        uint32_t noff = dynsym_name_off_raw(dynsym_buf, dynsym_len, entsz, endian, i);
        const char *name = safe_strtab_name(*dynstr_buf, *dynstr_len, noff);
        const char *base;
        size_t base_len;
        const char *ver_name;
        int is_default_name;
        uint16_t shndx;
        uint16_t curr_ver;
        uint16_t curr_base;
        int curr_hidden;
        uint16_t assigned = VER_NDX_GLOBAL;

        if (name == NULL || name[0] == '\0') {
            continue;
        }
        split_symbol_version(name, &base, &base_len, &ver_name, &is_default_name);
        if (dynsym_shndx_raw(dynsym_buf, dynsym_len, entsz, elf_class(out), endian, i, &shndx) != 0 ||
            versym_read_raw(versym_buf, versym_len, endian, i, &curr_ver) != 0) {
            dyn_ver_plan_free(&plan);
            return -1;
        }
        curr_base = (uint16_t)(curr_ver & (uint16_t)~VER_NDX_HIDDEN);
        curr_hidden = (curr_ver & VER_NDX_HIDDEN) != 0;
        if (ver_name == NULL || ver_name[0] == '\0') {
            if (shndx == SHN_UNDEF && base != NULL && base_len != 0 && curr_base <= VER_NDX_GLOBAL) {
                const char *provider = NULL;
                char *auto_ver_name = NULL;
                int found = resolve_default_version_need(ctx, base, base_len, &provider, &auto_ver_name);

                if (found < 0) {
                    dyn_ver_plan_free(&plan);
                    return -1;
                }
                if (found > 0 && provider != NULL && auto_ver_name != NULL) {
                    if (dyn_ver_plan_get_or_add_need(&plan, provider, auto_ver_name, &assigned) != 0 ||
                        versym_write_raw(versym_buf, versym_len, endian, i, assigned) != 0) {
                        free(auto_ver_name);
                        dyn_ver_plan_free(&plan);
                        return -1;
                    }
                    free(auto_ver_name);
                }
            }
            continue;
        }
        if (shndx != SHN_UNDEF) {
            if (curr_base > VER_NDX_GLOBAL) {
                if (dyn_ver_plan_ensure_def_index(&plan, ver_name, curr_base) != 0) {
                    dyn_ver_plan_free(&plan);
                    return -1;
                }
                assigned = curr_base;
            } else if (dyn_ver_plan_get_or_add_def(&plan, ver_name, &assigned) != 0) {
                dyn_ver_plan_free(&plan);
                return -1;
            }
            if (curr_hidden || !is_default_name) {
                assigned = (uint16_t)(assigned | VER_NDX_HIDDEN);
            }
            if (versym_write_raw(versym_buf, versym_len, endian, i, assigned) != 0) {
                dyn_ver_plan_free(&plan);
                return -1;
            }
            continue;
        }
        {
            const char *provider = resolve_version_need_provider(ctx, base, base_len, ver_name);
            /* No shared object of the link has that version of it: the
             * reference goes out with no version, rather than with one
             * said to come from whichever shared object was named first. */
            if (provider == NULL) {
                continue;
            }
            if (curr_base > VER_NDX_GLOBAL) {
                if (dyn_ver_plan_ensure_need_index(&plan, provider, ver_name, curr_base) != 0) {
                    dyn_ver_plan_free(&plan);
                    return -1;
                }
                assigned = curr_base;
            } else if (dyn_ver_plan_get_or_add_need(&plan, provider, ver_name, &assigned) != 0) {
                dyn_ver_plan_free(&plan);
                return -1;
            }
            if (curr_hidden) {
                assigned = (uint16_t)(assigned | VER_NDX_HIDDEN);
            }
            if (versym_write_raw(versym_buf, versym_len, endian, i, assigned) != 0) {
                dyn_ver_plan_free(&plan);
                return -1;
            }
        }
    }
    if (plan.def_count == 0 && plan.need_count == 0) {
        dyn_ver_plan_free(&plan);
        return 0;
    }
    if (dyn_ver_plan_assign_dynstr_offsets(&plan, dynstr_buf, dynstr_len, dynstr_cap) != 0) {
        dyn_ver_plan_free(&plan);
        return -1;
    }
    if (plan.def_count != 0) {
        elf_section_t *sec = elf_find_section(out, ".gnu.version_d");
        const char *slash = ctx->out_path != NULL ? strrchr(ctx->out_path, '/') : NULL;
        const char *base_name = ctx->soname != NULL ? ctx->soname
                              : slash != NULL ? slash + 1 : ctx->out_path != NULL ? ctx->out_path : "";
        uint32_t base_off = 0;

        if (dynstr_append_cstr(dynstr_buf, dynstr_len, dynstr_cap, base_name, &base_off) != 0 ||
            build_gnu_verdef_data(&plan, endian, base_name, base_off, &verdef_data, &verdef_sz) != 0) {
            dyn_ver_plan_free(&plan);
            return -1;
        }
        if (sec == NULL) {
            sec = elf_add_section(out, ".gnu.version_d", SHT_GNU_verdef, SHF_ALLOC);
            if (sec == NULL) {
                free(verdef_data);
                dyn_ver_plan_free(&plan);
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || elf_section_set_data(sec, verdef_data, verdef_sz) != ELF_OK) {
            free(verdef_data);
            dyn_ver_plan_free(&plan);
            return -1;
        }
        *out_verdef_count = plan.def_count + 1;
    }
    if (plan.need_count != 0) {
        elf_section_t *sec = elf_find_section(out, ".gnu.version_r");
        if (build_gnu_verneed_data(&plan, endian, &verneed_data, &verneed_sz, &need_file_count) != 0) {
            free(verdef_data);
            dyn_ver_plan_free(&plan);
            return -1;
        }
        if (sec == NULL) {
            sec = elf_add_section(out, ".gnu.version_r", SHT_GNU_verneed, SHF_ALLOC);
            if (sec == NULL) {
                free(verdef_data);
                free(verneed_data);
                dyn_ver_plan_free(&plan);
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || elf_section_set_data(sec, verneed_data, verneed_sz) != ELF_OK) {
            free(verdef_data);
            free(verneed_data);
            dyn_ver_plan_free(&plan);
            return -1;
        }
        *out_verneed_count = need_file_count;
    }
    free(verdef_data);
    free(verneed_data);
    dyn_ver_plan_free(&plan);
    return 0;
}

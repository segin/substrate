#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define LD_MAX_SCRIPT_INCLUDE_DEPTH 16
/* How deeply a script expression may nest: parentheses, unary operators
 * and the arguments of builtins each count one. */
#define LD_MAX_SCRIPT_EXPR_DEPTH 256
#define LD_MAX_TRACKED_SYMBOLS 262144U
#define LD_MAX_INPUT_OBJECTS 131072U
#define LD_MAX_ARCHIVE_SCAN_PASSES 1024

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} strvec_t;

typedef enum {
    LD_INPUT_FILE = 0,
    LD_INPUT_LIB = 1,
    LD_INPUT_GROUP_START = 2,
    LD_INPUT_GROUP_END = 3
} ld_input_kind_t;

typedef enum {
    LD_LIBMODE_DYNAMIC = 0,
    LD_LIBMODE_STATIC = 1
} ld_lib_mode_t;

typedef enum {
    LD_COMPAT_GNU = 0,
    LD_COMPAT_LLD = 1
} ld_compat_mode_t;

typedef enum {
    LD_HASH_BOTH = 0,
    LD_HASH_SYSV = 1,
    LD_HASH_GNU = 2
} ld_hash_style_t;

typedef struct {
    ld_input_kind_t kind;
    ld_lib_mode_t lib_mode;
    int whole_archive;
    int as_needed;
    char *text;
} ld_input_t;

typedef struct {
    ld_input_t *items;
    size_t count;
    size_t cap;
} inputvec_t;

typedef struct {
    char *name;
    int need_plt;
    int need_got;
    int need_tls_gd;
    int need_tls_ie;
    size_t plt_slot;
    size_t got_slot;
    size_t tls_gd_slot;
    size_t tls_ie_slot;
    int canonical;              /* its PLT entry is its address: see
                                 * plan_copy_relocs() */
} dyn_import_t;

typedef struct {
    dyn_import_t *items;
    size_t count;
    size_t cap;
    /* The data of shared objects that the executable has a copy of
     * (R_*_COPY): each symbol the copy is of.  Its aliases, defined at
     * the same place, are in copy_aliases. */
    elf_symbol_t **copies;
    size_t copy_count;
    size_t copy_cap;
    elf_symbol_t **copy_aliases;
    size_t alias_count;
    size_t alias_cap;
} dyn_import_vec_t;

typedef struct {
    elfobj_t **objs;
    char **names;
    size_t count;
    size_t cap;
} objvec_t;

typedef struct {
    char *name;
    uint64_t value;
} defsym_t;

typedef struct {
    defsym_t *items;
    size_t count;
    size_t cap;
} defsymvec_t;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} symset_t;

typedef struct {
    symset_t defined;
    symset_t unresolved;
} symstate_t;

typedef struct {
    int mode; /* 32 or 64 */
    int explicit_mode;
    int mode_settled;           /* an input has said which machine */
    int explicit_unresolved_policy;
    uint16_t expect_type;
    int allow_undefined;
    int warn_common;
    int fatal_warnings;
    int warning_count;
    int query_version;
    int trace_inputs;
    int export_dynamic;
    int gc_sections;
    int gc_print_sections;
    int icf_mode; /* 0=off, 1=safe, 2=all */
    int z_text_mode; /* 0=default, 1=text, 2=notext */
    int z_execstack; /* -1=auto, 0=noexecstack, 1=execstack */
    int z_relro; /* 0=norelro, 1=relro */
    int z_now;   /* -z now: the dynamic linker binds everything at once */
    ld_hash_style_t hash_style;
    const char *out_path;
    const char *self_path;
    const char *script_path;
    struct lds_script *script;  /* script_path, parsed */
    const char *plugin_path;
    const char *plugin_opts[32];
    size_t plugin_opt_count;
    int plugin_checked;
    int plugin_unusable;        /* one was named that cannot be run */
    int eh_frame_hdr;           /* --eh-frame-hdr */
    int copy_dt_needed;         /* --copy-dt-needed-entries */
    const char *sysroot;        /* --sysroot */
    const char *entry_symbol;
    const char *interp_path;
    const char *soname;         /* -soname, -h: the output's DT_SONAME */
    uint64_t image_base;        /* -Ttext-segment, when have_image_base */
    int have_image_base;
    int pie;                    /* -pie: ET_DYN, and a program */
    int strip_debug;            /* -S, -s: no debugging information */
    int emit_relocs;            /* -q: the output keeps its relocations */
    strvec_t rpaths;            /* -rpath: the output's DT_RUNPATH */
    const char *map_path;
    const char *reproduce_path;
    ld_compat_mode_t compat_mode;
    ld_lib_mode_t current_lib_mode;
    int explicit_lib_mode;
    int current_whole_archive;
    int current_as_needed;
    strvec_t lib_paths;
    strvec_t trace_symbols;
    strvec_t force_undefined;
    defsymvec_t defsyms;
    strvec_t dso_inputs;
    strvec_t dso_names;         /* what each is needed as: its DT_SONAME, or
                                 * failing that the name it was found by */
    symset_t dso_wants;         /* the symbols they refer to and do not define */
    dyn_import_vec_t dyn_imports;
    /* i386: the symbols defined in the output that have a slot in a .got
     * this link made for them (plan_local_got_i386), in slot order. */
    const elf_symbol_t **local_got;
    size_t local_got_count;
    size_t local_got_cap;
    int local_got_owned;        /* .got is ours: the slots exist */
    inputvec_t inputs;
} ld_ctx_t;

static int dynstr_append_cstr(uint8_t **buf, size_t *len, size_t *cap, const char *name, uint32_t *out_off);
static char *dso_soname(const char *path);
static int dso_dynamic_strings(const char *path, uint64_t want, strvec_t *out);
static int dynsym_should_export(const ld_ctx_t *ctx, const elfobj_t *out, const elf_symbol_t *sym);
static int symbol_is_copied(const dyn_import_vec_t *imports, const elf_symbol_t *sym);
static const dyn_import_t *find_planned_import(const ld_ctx_t *ctx, const char *name);
static int reloc_is_direct_ref(uint16_t machine, uint32_t type, int data);
static int set_section_zero_data(elf_section_t *sec, size_t sz);
static int resolve_symbol_addr(elfobj_t *obj, const elf_symbol_t *sym, int allow_undef,
                               uint64_t *out_addr, const char **undef_name);
static int register_dso_provider(ld_ctx_t *ctx, const char *path, symstate_t *state);
static int is_relro_candidate_name(const char *name);

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64|-m <emulation>] [-r|-shared|-pie|-static] "
            "[-o output] [-L dir] [-l name] [-Bstatic|-Bdynamic] "
            "[--start-group ... --end-group] [--whole-archive|--no-whole-archive] "
            "[-plugin path] [-plugin-opt opt] "
            "[-e symbol] [-T script] [--allow-undefined] [-z text|notext|execstack|noexecstack|relro|norelro] "
            "[--reproduce dir] "
            "[--hash-style=sysv|gnu|both] "
            "[--compat=gnu|lld] input...\n",
            prog);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static int strvec_push(strvec_t *v, const char *s) {
    char **next;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (char **)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }

    v->items[v->count] = xstrdup(s);
    if (v->items[v->count] == NULL) {
        return -1;
    }
    v->count++;
    return 0;
}

static void strvec_free(strvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static void strvec_pop(strvec_t *v) {
    if (v == NULL || v->count == 0) {
        return;
    }
    v->count--;
    free(v->items[v->count]);
    v->items[v->count] = NULL;
}

static int defsymvec_push(defsymvec_t *v, const char *name, uint64_t value) {
    defsym_t *next;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (defsym_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].value = value;
    v->count++;
    return 0;
}

static int defsymvec_find(const defsymvec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < v->count; ++i) {
        if (v->items[i].name != NULL && strcmp(v->items[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int defsymvec_get(const defsymvec_t *v, const char *name, uint64_t *out_value) {
    int idx;

    if (out_value == NULL) {
        return -1;
    }
    idx = defsymvec_find(v, name);
    if (idx < 0) {
        return -1;
    }
    *out_value = v->items[(size_t)idx].value;
    return 0;
}

static int defsymvec_set(defsymvec_t *v, const char *name, uint64_t value) {
    int idx;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    idx = defsymvec_find(v, name);
    if (idx >= 0) {
        v->items[(size_t)idx].value = value;
        return 0;
    }
    return defsymvec_push(v, name, value);
}

static void defsymvec_free(defsymvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int inputvec_push(inputvec_t *v, ld_input_kind_t kind, ld_lib_mode_t lib_mode, int whole_archive,
                         int as_needed, const char *text) {
    ld_input_t *next;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (ld_input_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }

    v->items[v->count].kind = kind;
    v->items[v->count].lib_mode = lib_mode;
    v->items[v->count].whole_archive = whole_archive ? 1 : 0;
    v->items[v->count].as_needed = as_needed ? 1 : 0;
    if (text != NULL) {
        v->items[v->count].text = xstrdup(text);
        if (v->items[v->count].text == NULL) {
            return -1;
        }
    } else {
        v->items[v->count].text = NULL;
    }
    v->count++;
    return 0;
}

static void inputvec_free(inputvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].text);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int dyn_import_find(const dyn_import_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int dyn_import_push(dyn_import_vec_t *v, const char *name, size_t *out_idx) {
    dyn_import_t *next;
    char *dup;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (dyn_import_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    dup = xstrdup(name);
    if (dup == NULL) {
        return -1;
    }
    v->items[v->count].name = dup;
    v->items[v->count].need_plt = 0;
    v->items[v->count].need_got = 0;
    v->items[v->count].need_tls_gd = 0;
    v->items[v->count].need_tls_ie = 0;
    v->items[v->count].plt_slot = 0;
    v->items[v->count].got_slot = 0;
    v->items[v->count].tls_gd_slot = 0;
    v->items[v->count].tls_ie_slot = 0;
    if (out_idx != NULL) {
        *out_idx = v->count;
    }
    v->count++;
    return 0;
}

static dyn_import_t *dyn_import_get_or_add(dyn_import_vec_t *v, const char *name) {
    int idx;
    size_t new_idx = 0;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    idx = dyn_import_find(v, name);
    if (idx >= 0) {
        return &v->items[idx];
    }
    if (dyn_import_push(v, name, &new_idx) != 0) {
        return NULL;
    }
    return &v->items[new_idx];
}

static void dyn_import_vec_free(dyn_import_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    free(v->copies);
    free(v->copy_aliases);
    memset(v, 0, sizeof(*v));
}

static int objvec_push(objvec_t *v, elfobj_t *obj, const char *name) {
    elfobj_t **new_objs;
    char **new_names;
    char *dup;

    if (v->count >= LD_MAX_INPUT_OBJECTS) {
        fprintf(stderr, "ld: input object limit exceeded (%u)\n", (unsigned)LD_MAX_INPUT_OBJECTS);
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        new_objs = (elfobj_t **)realloc(v->objs, ncap * sizeof(*new_objs));
        if (new_objs == NULL) {
            return -1;
        }
        new_names = (char **)realloc(v->names, ncap * sizeof(*new_names));
        if (new_names == NULL) {
            v->objs = new_objs;
            return -1;
        }
        v->objs = new_objs;
        v->names = new_names;
        v->cap = ncap;
    }

    dup = xstrdup(name != NULL ? name : "<input>");
    if (dup == NULL) {
        return -1;
    }
    v->objs[v->count] = obj;
    v->names[v->count] = dup;
    v->count++;
    return 0;
}

static void objvec_free(objvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        if (v->objs[i] != NULL) {
            elf_close(v->objs[i]);
        }
        free(v->names[i]);
    }
    free(v->objs);
    free(v->names);
    v->objs = NULL;
    v->names = NULL;
    v->count = 0;
    v->cap = 0;
}

static int symset_index_of(const symset_t *set, const char *sym) {
    size_t i;

    for (i = 0; i < set->count; ++i) {
        if (strcmp(set->items[i], sym) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int symset_contains(const symset_t *set, const char *sym) {
    return symset_index_of(set, sym) >= 0;
}

static int symset_add(symset_t *set, const char *sym) {
    char **next;

    if (sym == NULL || sym[0] == '\0' || symset_contains(set, sym)) {
        return 0;
    }
    if (set->count >= LD_MAX_TRACKED_SYMBOLS) {
        fprintf(stderr, "ld: symbol tracking limit exceeded (%u)\n", (unsigned)LD_MAX_TRACKED_SYMBOLS);
        return -1;
    }
    if (set->count == set->cap) {
        size_t ncap = set->cap == 0 ? 32 : set->cap * 2;
        next = (char **)realloc(set->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        set->items = next;
        set->cap = ncap;
    }
    set->items[set->count] = xstrdup(sym);
    if (set->items[set->count] == NULL) {
        return -1;
    }
    set->count++;
    return 0;
}

static void symset_remove(symset_t *set, const char *sym) {
    int idx = symset_index_of(set, sym);

    if (idx < 0) {
        return;
    }
    free(set->items[idx]);
    set->count--;
    if ((size_t)idx != set->count) {
        set->items[idx] = set->items[set->count];
    }
}

static void symset_free(symset_t *set) {
    size_t i;

    for (i = 0; i < set->count; ++i) {
        free(set->items[i]);
    }
    free(set->items);
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

static void symstate_free(symstate_t *state) {
    symset_free(&state->defined);
    symset_free(&state->unresolved);
}

static char *path_join(const char *dir, const char *leaf) {
    size_t dlen;
    size_t llen;
    size_t need_slash;
    char *out;

    if (dir == NULL || leaf == NULL) {
        return NULL;
    }
    dlen = strlen(dir);
    llen = strlen(leaf);
    need_slash = dlen > 0 && dir[dlen - 1] != '/';
    out = (char *)malloc(dlen + need_slash + llen + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, dir, dlen);
    if (need_slash) {
        out[dlen++] = '/';
    }
    memcpy(out + dlen, leaf, llen);
    out[dlen + llen] = '\0';
    return out;
}

static int default_mode(void) {
    return (int)(sizeof(void *) == 8 ? 64 : 32);
}

static int parse_mode_token(const char *tok) {
    if (tok == NULL) {
        return 0;
    }
    /* The _substrate names are what the substrate GCC asks for. */
    if (strcmp(tok, "elf_x86_64") == 0 || strcmp(tok, "elf64-x86-64") == 0 ||
        strcmp(tok, "elf_x86_64_substrate") == 0 ||
        strcmp(tok, "x86_64") == 0 || strcmp(tok, "amd64") == 0 || strcmp(tok, "64") == 0) {
        return 64;
    }
    if (strcmp(tok, "elf_i386") == 0 || strcmp(tok, "elf32-i386") == 0 ||
        strcmp(tok, "elf_i386_substrate") == 0 ||
        strcmp(tok, "i386") == 0 || strcmp(tok, "x86") == 0 || strcmp(tok, "32") == 0) {
        return 32;
    }
    return 0;
}

static int parse_compat_mode(const char *tok, ld_compat_mode_t *out_mode) {
    if (tok == NULL || out_mode == NULL) {
        return -1;
    }
    if (strcmp(tok, "gnu") == 0 || strcmp(tok, "bfd") == 0 || strcmp(tok, "gold") == 0) {
        *out_mode = LD_COMPAT_GNU;
        return 0;
    }
    if (strcmp(tok, "lld") == 0) {
        *out_mode = LD_COMPAT_LLD;
        return 0;
    }
    return -1;
}

static const char *canonical_mode_name(int mode) {
    if (mode == 64) {
        return "x86-64";
    }
    if (mode == 32) {
        return "i386";
    }
    return "unknown";
}

static int parse_z_option(ld_ctx_t *ctx, const char *val) {
    if (ctx == NULL || val == NULL || val[0] == '\0') {
        return -1;
    }
    if (strcmp(val, "text") == 0) {
        ctx->z_text_mode = 1;
        return 0;
    }
    if (strcmp(val, "notext") == 0) {
        ctx->z_text_mode = 2;
        return 0;
    }
    if (strcmp(val, "execstack") == 0) {
        ctx->z_execstack = 1;
        return 0;
    }
    if (strcmp(val, "noexecstack") == 0) {
        ctx->z_execstack = 0;
        return 0;
    }
    if (strcmp(val, "relro") == 0) {
        ctx->z_relro = 1;
        return 0;
    }
    if (strcmp(val, "norelro") == 0) {
        ctx->z_relro = 0;
        return 0;
    }
    if (strcmp(val, "now") == 0 || strcmp(val, "lazy") == 0) {
        ctx->z_now = val[0] == 'n';
        return 0;
    }
    return -1;
}

static int parse_hash_style_option(const char *val, ld_hash_style_t *out_style) {
    if (val == NULL || out_style == NULL || val[0] == '\0') {
        return -1;
    }
    if (strcmp(val, "sysv") == 0) {
        *out_style = LD_HASH_SYSV;
        return 0;
    }
    if (strcmp(val, "gnu") == 0) {
        *out_style = LD_HASH_GNU;
        return 0;
    }
    if (strcmp(val, "both") == 0) {
        *out_style = LD_HASH_BOTH;
        return 0;
    }
    return -1;
}

static void ld_diag_note(const char *category, const char *source, const char *hint) {
    if ((category == NULL || category[0] == '\0') &&
        (source == NULL || source[0] == '\0') &&
        (hint == NULL || hint[0] == '\0')) {
        return;
    }
    fprintf(stderr, "ld: note:");
    if (category != NULL && category[0] != '\0') {
        fprintf(stderr, " category=%s", category);
    }
    if (source != NULL && source[0] != '\0') {
        fprintf(stderr, " source=%s", source);
    }
    if (hint != NULL && hint[0] != '\0') {
        fprintf(stderr, " hint=%s", hint);
    }
    fputc('\n', stderr);
}

static int ld_warn(ld_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "ld: warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    ctx->warning_count++;
    if (ctx->fatal_warnings) {
        fprintf(stderr, "ld: error: warnings treated as errors (--fatal-warnings)\n");
        return -1;
    }
    return 0;
}

static int set_explicit_mode(ld_ctx_t *ctx, int mode, const char *opt_text) {
    if (mode != 32 && mode != 64) {
        return -1;
    }
    if (ctx->explicit_mode && ctx->mode != mode) {
        fprintf(stderr,
                "ld: conflicting target mode options: already %s, new %s from %s\n",
                canonical_mode_name(ctx->mode), canonical_mode_name(mode),
                opt_text != NULL ? opt_text : "<option>");
        return -1;
    }
    ctx->mode = mode;
    ctx->explicit_mode = 1;
    return 0;
}

static int align_up_u64_checked(uint64_t v, uint64_t a, uint64_t *out) {
    uint64_t add;

    if (out == NULL) {
        return 0;
    }
    if (a <= 1) {
        *out = v;
        return 1;
    }
    if ((a & (a - 1)) == 0) {
        if (v > UINT64_MAX - (a - 1)) {
            return 0;
        }
        *out = (v + (a - 1)) & ~(a - 1);
        return 1;
    }
    add = a - (v % a);
    if (add == a) {
        add = 0;
    }
    if (v > UINT64_MAX - add) {
        return 0;
    }
    *out = v + add;
    return 1;
}

static int add_u64_checked(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || a > UINT64_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int mul_u64_checked(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
    if (a != 0 && b > UINT64_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t n;
    size_t m;

    if (s == NULL || suffix == NULL) {
        return 0;
    }
    n = strlen(s);
    m = strlen(suffix);
    if (n < m) {
        return 0;
    }
    return strcmp(s + (n - m), suffix) == 0;
}

static int read_file(const char *path, unsigned char **out, size_t *out_sz) {
    unsigned char *buf;
    int fd;
    struct stat st;
    size_t cap;
    size_t used;
    ssize_t nr;

    *out = NULL;
    *out_sz = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0) {
        size_t total = (size_t)st.st_size;
        size_t off = 0;
        buf = (unsigned char *)malloc(total);
        if (buf == NULL && total != 0) {
            close(fd);
            return -1;
        }
        while (off < total) {
            nr = read(fd, buf + off, total - off);
            if (nr <= 0) {
                free(buf);
                close(fd);
                return -1;
            }
            off += (size_t)nr;
        }
        close(fd);
        *out = buf;
        *out_sz = total;
        return 0;
    }
    cap = 4096;
    used = 0;
    buf = (unsigned char *)malloc(cap);
    if (buf == NULL) {
        close(fd);
        return -1;
    }
    while ((nr = read(fd, buf + used, cap - used)) > 0) {
        used += (size_t)nr;
        if (used == cap) {
            size_t new_cap;
            unsigned char *next;
            if (cap > ((size_t)-1) / 2) {
                free(buf);
                close(fd);
                return -1;
            }
            new_cap = cap * 2;
            next = (unsigned char *)realloc(buf, new_cap);
            if (next == NULL) {
                free(buf);
                close(fd);
                return -1;
            }
            buf = next;
            cap = new_cap;
        }
    }
    close(fd);
    if (nr < 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_sz = used;
    return 0;
}

typedef enum {
    LDS_TOK_EOF = 0,
    LDS_TOK_IDENT,
    LDS_TOK_NUMBER,
    LDS_TOK_STRING,
    LDS_TOK_LBRACE,
    LDS_TOK_RBRACE,
    LDS_TOK_LPAREN,
    LDS_TOK_RPAREN,
    LDS_TOK_SEMI,
    LDS_TOK_COLON,
    LDS_TOK_COMMA,
    LDS_TOK_EQUAL,
    LDS_TOK_OTHER
} lds_tok_kind_t;

typedef struct {
    lds_tok_kind_t kind;
    char *text;
    const char *path;
    size_t line;
    size_t col;
} lds_tok_t;

typedef struct {
    const unsigned char *buf;
    size_t len;
    size_t pos;
    const char *path;
    size_t line;
    size_t col;
    /*
     * What a word is depends on where it stands.  In an expression a name
     * is letters, digits, '_', '.' and '$', so that a-b is a subtraction;
     * where a file or section name is expected ("names") it also takes
     * the characters of a path and of a wildcard, so that
     * .note.gnu.build-id, /DISCARD/ and *crt?.o are each one word.
     */
    int names;
} lds_lexer_t;

typedef struct {
    lds_tok_t *items;
    size_t count;
    size_t cap;
} lds_tokvec_t;

typedef struct {
    ld_ctx_t *ctx;
    const elfobj_t *obj;
    const lds_tok_t *err_tok;
    const char *err_msg;
    unsigned depth;             /* nesting, bounded by LD_MAX_SCRIPT_EXPR_DEPTH */
    int have_dot;               /* inside SECTIONS: "." has a value */
    uint64_t dot;
    char err_buf[160];          /* for an err_msg that names something */
    unsigned skip;              /* in the arm of a ?: that is not taken:
                                 * read, and nothing in it is an error */
} lds_eval_ctx_t;

typedef struct {
    char *name;
    uint32_t type;
    uint32_t flags;
    uint64_t align;
    int has_flags;              /* FLAGS(...) given, else from the sections */
} lds_phdr_entry_t;

typedef struct {
    lds_phdr_entry_t *items;
    size_t count;
    size_t cap;
} lds_phdr_vec_t;

/*
 * A linker script, parsed.  The file is read once, into this, and the
 * link then consults it at the points where it has something to say:
 * while the inputs are merged (which output section an input section goes
 * to), when the sections are ordered, when they are given addresses (the
 * location counter), and when the program headers are made.
 */
typedef enum {
    LDS_ST_ASSIGN = 0,          /* sym = expr, sym += expr, PROVIDE(...) */
    LDS_ST_ASSERT,              /* ASSERT(expr, "message") */
    LDS_ST_OUTSEC,              /* name [addr] : { body } [>region] [:phdr] */
    LDS_ST_INPUT,               /* file(sections...), KEEP(...) */
    LDS_ST_DATA                 /* BYTE(expr) and its kind: not implemented */
} lds_stmt_kind_t;

typedef enum {
    LDS_IN_TOP = 0,             /* outside SECTIONS: no location counter */
    LDS_IN_SECTIONS,            /* between output sections */
    LDS_IN_BODY                 /* inside an output section's braces */
} lds_where_t;

typedef struct lds_stmt lds_stmt_t;

typedef struct {
    lds_stmt_t *items;
    size_t count;
    size_t cap;
} lds_stmtvec_t;

struct lds_stmt {
    lds_stmt_kind_t kind;
    lds_where_t where;
    lds_tok_t at;               /* where it is; text: the symbol, the output
                                 * section, the file pattern, the keyword */
    char op;                    /* ASSIGN: '=' or the operator of op= */
    int provide;                /* ASSIGN: only if referenced and undefined */
    int hidden;
    int active;                 /* ASSIGN: it defines its symbol in this link */
    lds_tokvec_t expr;          /* ASSIGN value, ASSERT condition, OUTSEC address */
    char *message;              /* ASSERT */
    int discard;                /* OUTSEC: /DISCARD/ */
    lds_stmtvec_t body;         /* OUTSEC */
    strvec_t phdrs;             /* OUTSEC: :phdr ... */
    lds_tokvec_t align;         /* OUTSEC: ALIGN(expr) after the colon */
    char *region;               /* OUTSEC: >region */
    uint64_t pad;               /* OUTSEC: what its body adds to its end */
    int keep;                   /* INPUT: KEEP() */
    strvec_t patterns;          /* INPUT: section name patterns */
    strvec_t excludes;          /* INPUT: EXCLUDE_FILE() patterns */
};

typedef struct {
    char *name;
    lds_tokvec_t origin;
    lds_tokvec_t length;
    int evaluated;
    uint64_t org;
    uint64_t len;
    uint64_t cursor;            /* where its next section goes */
} lds_region_t;

typedef struct lds_script {
    lds_stmtvec_t stmts;        /* in order; SECTIONS' own statements inline */
    lds_phdr_vec_t phdrs;
    lds_region_t *regions;
    size_t region_count;
    size_t region_cap;
    char *entry;                /* ENTRY(sym) */
    int has_sections;
    defsymvec_t locals;         /* what PROVIDE names and nothing refers to:
                                 * no symbol, but the script may use it */
    strvec_t files;             /* the script and what it INCLUDEs: tokens
                                 * point at these names */
} lds_script_t;

static void lds_tok_free(lds_tok_t *tok) {
    if (tok == NULL) {
        return;
    }
    free(tok->text);
    tok->text = NULL;
}

static int lds_tok_dup(lds_tok_t *dst, const lds_tok_t *src) {
    if (dst == NULL || src == NULL) {
        return -1;
    }
    memset(dst, 0, sizeof(*dst));
    dst->kind = src->kind;
    dst->path = src->path;
    dst->line = src->line;
    dst->col = src->col;
    if (src->text != NULL) {
        dst->text = xstrdup(src->text);
        if (dst->text == NULL) {
            return -1;
        }
    }
    return 0;
}

static void lds_tokvec_free(lds_tokvec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        lds_tok_free(&v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int lds_tokvec_push(lds_tokvec_t *v, const lds_tok_t *tok) {
    lds_tok_t *next;

    if (v == NULL || tok == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (lds_tok_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    if (lds_tok_dup(&v->items[v->count], tok) != 0) {
        return -1;
    }
    v->count++;
    return 0;
}

static void lds_phdr_vec_free(lds_phdr_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int lds_phdr_vec_push(lds_phdr_vec_t *v, const char *name, uint32_t type, uint32_t flags, uint64_t align) {
    lds_phdr_entry_t *next;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (lds_phdr_entry_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].type = type;
    v->items[v->count].flags = flags;
    v->items[v->count].align = align;
    v->count++;
    return 0;
}

static int lds_phdr_vec_find(const lds_phdr_vec_t *v, const char *name) {
    size_t i;

    if (v == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < v->count; ++i) {
        if (v->items[i].name != NULL && strcmp(v->items[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int align_u64(uint64_t value, uint64_t align, uint64_t *out) {
    uint64_t rem;
    uint64_t add;

    if (out == NULL || align == 0) {
        return -1;
    }
    rem = value % align;
    if (rem == 0) {
        *out = value;
        return 0;
    }
    add = align - rem;
    if (value > UINT64_MAX - add) {
        return -1;
    }
    *out = value + add;
    return 0;
}

static int lds_tok_is(const lds_tok_t *tok, lds_tok_kind_t kind, const char *text) {
    if (tok == NULL || tok->kind != kind) {
        return 0;
    }
    if (text == NULL) {
        return 1;
    }
    return tok->text != NULL && strcmp(tok->text, text) == 0;
}

static int script_lookup_symbol_value(const lds_eval_ctx_t *ec, const char *name, uint64_t *out) {
    if (ec == NULL || ec->ctx == NULL || name == NULL || out == NULL) {
        return -1;
    }
    if (defsymvec_get(&ec->ctx->defsyms, name, out) == 0 ||
        (ec->ctx->script != NULL && defsymvec_get(&ec->ctx->script->locals, name, out) == 0)) {
        return 0;
    }
    if (ec->obj != NULL) {
        /* A symbol of the program: its address, the layout being done by
         * the time a script's expressions are evaluated. */
        const elf_symbol_t *sym = elf_find_symbol((elfobj_t *)ec->obj, name);
        const char *undef = NULL;

        if (sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF &&
            resolve_symbol_addr((elfobj_t *)ec->obj, sym, 0, out, &undef) == 0) {
            return 0;
        }
    }
    return -1;
}

/* Whether a symbol has a definition: the script's, or an input's. */
static int script_symbol_defined(const lds_eval_ctx_t *ec, const char *name) {
    const elf_symbol_t *sym;

    if (ec == NULL || ec->ctx == NULL || name == NULL) {
        return 0;
    }
    if (defsymvec_find(&ec->ctx->defsyms, name) >= 0) {
        return 1;
    }
    sym = ec->obj != NULL ? elf_find_symbol((elfobj_t *)ec->obj, name) : NULL;
    return sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
}

static int script_lookup_section_metric(const lds_eval_ctx_t *ec, const char *name, int metric, uint64_t *out) {
    elf_section_t *sec;

    if (ec == NULL || name == NULL || out == NULL || ec->obj == NULL) {
        if (out != NULL) {
            *out = 0;
        }
        return 0;
    }
    sec = elf_find_section((elfobj_t *)ec->obj, name);
    if (sec == NULL) {
        *out = 0;
        return 0;
    }
    if (metric == 0) {
        *out = elf_section_addr(sec);
    } else if (metric == 1) {
        *out = elf_section_size(sec);
    } else {
        *out = elf_section_addr(sec);
    }
    return 0;
}

static int lds_eval_expr_slice(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t begin, size_t end, uint64_t *out);

static int lds_find_matching_rparen(const lds_tok_t *items, size_t begin, size_t end, size_t *close_idx) {
    size_t i;
    int depth = 0;

    if (items == NULL || close_idx == NULL || begin >= end || items[begin].kind != LDS_TOK_LPAREN) {
        return -1;
    }
    for (i = begin; i < end; ++i) {
        if (items[i].kind == LDS_TOK_LPAREN) {
            depth++;
        } else if (items[i].kind == LDS_TOK_RPAREN) {
            depth--;
            if (depth == 0) {
                *close_idx = i;
                return 0;
            }
            if (depth < 0) {
                return -1;
            }
        }
    }
    return -1;
}

static int lds_eval_builtin_call(lds_eval_ctx_t *ec, const lds_tok_t *name_tok, const lds_tok_t *items, size_t begin,
                                 size_t end, uint64_t *out) {
    const char *name;
    size_t open_idx;
    size_t close_idx;
    size_t arg_starts[8];
    size_t arg_ends[8];
    size_t arg_count = 0;
    size_t i;
    int depth = 0;
    int takes_name;
    const char *word;           /* the argument, of a function that takes a name */
    uint64_t vals[8];

    if (ec == NULL || name_tok == NULL || name_tok->text == NULL || items == NULL || out == NULL || begin >= end) {
        return -1;
    }
    name = name_tok->text;
    open_idx = begin;
    if (items[open_idx].kind != LDS_TOK_LPAREN || lds_find_matching_rparen(items, open_idx, end, &close_idx) != 0) {
        ec->err_tok = name_tok;
        ec->err_msg = "malformed function call";
        return -1;
    }

    if (open_idx + 1 <= close_idx) {
        size_t start = open_idx + 1;
        for (i = open_idx + 1; i < close_idx; ++i) {
            if (items[i].kind == LDS_TOK_LPAREN) {
                depth++;
            } else if (items[i].kind == LDS_TOK_RPAREN) {
                depth--;
            } else if (items[i].kind == LDS_TOK_COMMA && depth == 0) {
                if (arg_count >= sizeof(arg_starts) / sizeof(arg_starts[0])) {
                    ec->err_tok = &items[i];
                    ec->err_msg = "too many arguments";
                    return -1;
                }
                arg_starts[arg_count] = start;
                arg_ends[arg_count] = i;
                arg_count++;
                start = i + 1;
            }
        }
        if (start < close_idx || (close_idx == open_idx + 1 && items[open_idx + 1].kind != LDS_TOK_RPAREN)) {
            if (arg_count >= sizeof(arg_starts) / sizeof(arg_starts[0])) {
                ec->err_tok = &items[open_idx];
                ec->err_msg = "too many arguments";
                return -1;
            }
            arg_starts[arg_count] = start;
            arg_ends[arg_count] = close_idx;
            arg_count++;
        }
    }

    /* The argument of these is a name, of a section or a symbol, and not
     * something that has a value. */
    takes_name = strcmp(name, "ADDR") == 0 || strcmp(name, "LOADADDR") == 0 || strcmp(name, "SIZEOF") == 0 ||
                 strcmp(name, "ALIGNOF") == 0 || strcmp(name, "CONSTANT") == 0 || strcmp(name, "ORIGIN") == 0 ||
                 strcmp(name, "LENGTH") == 0 || strcmp(name, "DEFINED") == 0 || strcmp(name, "defined") == 0;
    word = takes_name && arg_count == 1 && arg_starts[0] + 1 == arg_ends[0] &&
           (items[arg_starts[0]].kind == LDS_TOK_IDENT || items[arg_starts[0]].kind == LDS_TOK_STRING)
               ? items[arg_starts[0]].text : NULL;
    for (i = 0; !takes_name && i < arg_count; ++i) {
        if (lds_eval_expr_slice(ec, items, arg_starts[i], arg_ends[i], &vals[i]) != 0) {
            return -1;
        }
    }

    if (strcmp(name, "ALIGN") == 0) {
        if (arg_count == 1) {
            /* ALIGN(n) is the location counter, aligned. */
            if (!ec->have_dot) {
                ec->err_tok = name_tok;
                ec->err_msg = "ALIGN(n) aligns the location counter, which has no value outside SECTIONS";
                return -1;
            }
            if (align_u64(ec->dot, vals[0], out) != 0) {
                ec->err_tok = name_tok;
                ec->err_msg = "ALIGN argument must be non-zero";
                return -1;
            }
        } else if (arg_count == 2) {
            if (align_u64(vals[0], vals[1], out) != 0) {
                ec->err_tok = name_tok;
                ec->err_msg = "invalid ALIGN arguments";
                return -1;
            }
        } else {
            ec->err_tok = name_tok;
            ec->err_msg = "ALIGN expects one or two arguments";
            return -1;
        }
    } else if (strcmp(name, "ADDR") == 0 || strcmp(name, "LOADADDR") == 0 || strcmp(name, "SIZEOF") == 0) {
        const char *section_name = NULL;
        int metric = strcmp(name, "SIZEOF") == 0 ? 1 : 0;
        if (arg_count != 1) {
            ec->err_tok = name_tok;
            ec->err_msg = "section builtin expects one argument";
            return -1;
        }
        if (arg_starts[0] < arg_ends[0]) {
            const lds_tok_t *at = &items[arg_starts[0]];
            if (at->kind == LDS_TOK_IDENT || at->kind == LDS_TOK_STRING) {
                section_name = at->text;
            }
        }
        if (section_name == NULL) {
            *out = 0;
        } else if (script_lookup_section_metric(ec, section_name, metric, out) != 0) {
            ec->err_tok = name_tok;
            ec->err_msg = "failed to resolve section builtin";
            return -1;
        }
    } else if (strcmp(name, "DEFINED") == 0 || strcmp(name, "defined") == 0) {
        const char *sym = NULL;
        if (arg_count != 1 || arg_starts[0] >= arg_ends[0]) {
            ec->err_tok = name_tok;
            ec->err_msg = "DEFINED expects one symbol argument";
            return -1;
        }
        if (items[arg_starts[0]].kind == LDS_TOK_IDENT || items[arg_starts[0]].kind == LDS_TOK_STRING) {
            sym = items[arg_starts[0]].text;
        }
        *out = sym != NULL && script_symbol_defined(ec, sym) ? 1 : 0;
    } else if ((strcmp(name, "MAX") == 0 || strcmp(name, "MIN") == 0) && arg_count == 2) {
        *out = (name[1] == 'A') == (vals[0] > vals[1]) ? vals[0] : vals[1];
    } else if ((strcmp(name, "ABSOLUTE") == 0 || strcmp(name, "DATA_SEGMENT_END") == 0) && arg_count == 1) {
        *out = vals[0];
    } else if ((strcmp(name, "SEGMENT_START") == 0 || strcmp(name, "DATA_SEGMENT_RELRO_END") == 0) &&
               arg_count == 2) {
        /* No -T<segment> option overrides the default; no gap is left
         * after the read-only part of the data. */
        *out = vals[1];
    } else if (strcmp(name, "DATA_SEGMENT_ALIGN") == 0 && arg_count == 2 && vals[0] != 0 && ec->have_dot) {
        /* The next page, at the same place within it: the data begins
         * where the text ended in the file, a page further on in memory. */
        if (align_u64(ec->dot, vals[0], out) != 0) {
            ec->err_tok = name_tok;
            ec->err_msg = "invalid DATA_SEGMENT_ALIGN arguments";
            return -1;
        }
        *out += ec->dot & (vals[0] - 1);
    } else if (strcmp(name, "CONSTANT") == 0 && word != NULL &&
               (strcmp(word, "MAXPAGESIZE") == 0 || strcmp(word, "COMMONPAGESIZE") == 0)) {
        *out = 0x1000u;
    } else if (strcmp(name, "ALIGNOF") == 0 && word != NULL) {
        elf_section_t *sec = ec->obj != NULL ? elf_find_section((elfobj_t *)ec->obj, word) : NULL;

        *out = sec != NULL ? elf_section_align(sec) : 0;
    } else if ((strcmp(name, "ORIGIN") == 0 || strcmp(name, "LENGTH") == 0) && word != NULL) {
        const lds_script_t *sc = ec->ctx != NULL ? ec->ctx->script : NULL;
        size_t r;

        for (r = 0; sc != NULL && r < sc->region_count && strcmp(sc->regions[r].name, word) != 0; ++r) {
        }
        if (sc == NULL || r == sc->region_count) {
            snprintf(ec->err_buf, sizeof(ec->err_buf), "%s of '%s', which MEMORY does not define", name, word);
            ec->err_tok = name_tok;
            ec->err_msg = ec->err_buf;
            return -1;
        }
        *out = name[0] == 'O' ? sc->regions[r].org : sc->regions[r].len;
    } else {
        snprintf(ec->err_buf, sizeof(ec->err_buf), "%s(): not a function of this linker, or not with these arguments",
                 name);
        ec->err_tok = name_tok;
        ec->err_msg = ec->err_buf;
        return -1;
    }
    return (int)(close_idx + 1);
}

static int lds_eval_primary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_unary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_mul(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_add(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_shift(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_rel(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_eq(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_band(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_bxor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_bor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_land(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_lor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);
static int lds_eval_cond(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);

static int lds_eval_primary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    const lds_tok_t *tok;
    char *num_end;
    unsigned long long parsed;
    uint64_t inner = 0;

    if (idx == NULL || out == NULL || *idx >= end) {
        if (ec != NULL && idx != NULL && *idx < end) {
            ec->err_tok = &items[*idx];
        }
        if (ec != NULL) {
            ec->err_msg = "unexpected end of expression";
        }
        return -1;
    }
    tok = &items[*idx];
    if (tok->kind == LDS_TOK_NUMBER && tok->text != NULL) {
        errno = 0;
        parsed = strtoull(tok->text, &num_end, 0);
        /* 64K, 2M: kilobytes and megabytes. */
        if (errno == 0 && num_end != tok->text && num_end[0] != '\0' && num_end[1] == '\0') {
            unsigned shift = (num_end[0] == 'K' || num_end[0] == 'k') ? 10
                           : (num_end[0] == 'M' || num_end[0] == 'm') ? 20 : 0;

            if (shift != 0 && parsed <= (~0ULL >> shift)) {
                parsed <<= shift;
                num_end++;
            }
        }
        if (errno != 0 || num_end == tok->text || *num_end != '\0') {
            ec->err_tok = tok;
            ec->err_msg = "invalid integer literal";
            return -1;
        }
        *out = (uint64_t)parsed;
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_LPAREN) {
        (*idx)++;
        if (lds_eval_cond(ec, items, idx, end, &inner) != 0) {
            return -1;
        }
        if (*idx >= end || items[*idx].kind != LDS_TOK_RPAREN) {
            ec->err_tok = tok;
            ec->err_msg = "expected ')'";
            return -1;
        }
        (*idx)++;
        *out = inner;
        return 0;
    }
    if (tok->kind == LDS_TOK_IDENT && tok->text != NULL && strcmp(tok->text, ".") == 0) {
        if (!ec->have_dot) {
            ec->err_tok = tok;
            ec->err_msg = "the location counter has no value outside SECTIONS";
            return -1;
        }
        *out = ec->dot;
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_IDENT && tok->text != NULL) {
        if (*idx + 1 < end && items[*idx + 1].kind == LDS_TOK_LPAREN) {
            int consumed = lds_eval_builtin_call(ec, tok, items, *idx + 1, end, out);
            if (consumed < 0) {
                return -1;
            }
            *idx = (size_t)consumed;
            return 0;
        }
        if (strcmp(tok->text, "SIZEOF_HEADERS") == 0 || strcmp(tok->text, "sizeof_headers") == 0) {
            /* The ELF header and the program headers, which begin the file. */
            int wide = ec->obj != NULL && elf_class(ec->obj) == ELFOBJ_CLASS_64;

            *out = (wide ? 64u : 52u) +
                   (uint64_t)(ec->obj != NULL ? elf_segment_count(ec->obj) : 0) * (wide ? 56u : 32u);
            (*idx)++;
            return 0;
        }
        if (script_lookup_symbol_value(ec, tok->text, out) != 0 && ec->skip != 0) {
            /* In the arm of a conditional that is not taken. */
            *out = 0;
            (*idx)++;
            return 0;
        }
        if (script_lookup_symbol_value(ec, tok->text, out) != 0) {
            snprintf(ec->err_buf, sizeof(ec->err_buf), "undefined symbol '%s' in expression", tok->text);
            ec->err_tok = tok;
            ec->err_msg = ec->err_buf;
            return -1;
        }
        (*idx)++;
        return 0;
    }
    if (tok->kind == LDS_TOK_STRING) {
        *out = 0;
        (*idx)++;
        return 0;
    }
    ec->err_tok = tok;
    ec->err_msg = "unexpected token in expression";
    return -1;
}

static int lds_eval_unary_nested(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out);

/*
 * Every level of nesting an expression can have -- a parenthesis, a unary
 * operator, an argument of a builtin -- comes through here, so this is
 * where the depth is counted.  The evaluator recurses on the C stack and a
 * script is input: a megabyte of '(' must be a syntax error.
 */
static int lds_eval_unary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    int rc;

    if (ec->depth >= LD_MAX_SCRIPT_EXPR_DEPTH) {
        ec->err_tok = *idx < end ? &items[*idx] : NULL;
        ec->err_msg = "expression nested too deeply";
        return -1;
    }
    ec->depth++;
    rc = lds_eval_unary_nested(ec, items, idx, end, out);
    ec->depth--;
    return rc;
}

static int lds_eval_unary_nested(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "+")) {
        (*idx)++;
        return lds_eval_unary(ec, items, idx, end, out);
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "-")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = (uint64_t)(0ULL - v);
        return 0;
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "~")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = ~v;
        return 0;
    }
    if (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "!")) {
        uint64_t v = 0;
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &v) != 0) {
            return -1;
        }
        *out = v == 0 ? 1 : 0;
        return 0;
    }
    return lds_eval_primary(ec, items, idx, end, out);
}

static int lds_eval_mul(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_unary(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "*") && !lds_tok_is(op, LDS_TOK_OTHER, "/") &&
            !lds_tok_is(op, LDS_TOK_OTHER, "%")) {
            break;
        }
        (*idx)++;
        if (lds_eval_unary(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "*")) {
            *out = (*out) * rhs;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, "/")) {
            if (rhs == 0 && ec->skip == 0) {
                ec->err_tok = op;
                ec->err_msg = "division by zero";
                return -1;
            }
            *out = rhs != 0 ? (*out) / rhs : 0;
        } else {
            if (rhs == 0 && ec->skip == 0) {
                ec->err_tok = op;
                ec->err_msg = "modulo by zero";
                return -1;
            }
            *out = rhs != 0 ? (*out) % rhs : 0;
        }
    }
    return 0;
}

static int lds_eval_add(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_mul(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "+") && !lds_tok_is(op, LDS_TOK_OTHER, "-")) {
            break;
        }
        (*idx)++;
        if (lds_eval_mul(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "+")) {
            *out = (*out) + rhs;
        } else {
            *out = (*out) - rhs;
        }
    }
    return 0;
}

static int lds_eval_shift(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_add(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "<<") && !lds_tok_is(op, LDS_TOK_OTHER, ">>")) {
            break;
        }
        (*idx)++;
        if (lds_eval_add(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        rhs &= 63;
        if (lds_tok_is(op, LDS_TOK_OTHER, "<<")) {
            *out <<= rhs;
        } else {
            *out >>= rhs;
        }
    }
    return 0;
}

static int lds_eval_rel(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_shift(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "<") && !lds_tok_is(op, LDS_TOK_OTHER, ">") &&
            !lds_tok_is(op, LDS_TOK_OTHER, "<=") && !lds_tok_is(op, LDS_TOK_OTHER, ">=")) {
            break;
        }
        (*idx)++;
        if (lds_eval_shift(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "<")) {
            *out = (*out < rhs) ? 1 : 0;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, ">")) {
            *out = (*out > rhs) ? 1 : 0;
        } else if (lds_tok_is(op, LDS_TOK_OTHER, "<=")) {
            *out = (*out <= rhs) ? 1 : 0;
        } else {
            *out = (*out >= rhs) ? 1 : 0;
        }
    }
    return 0;
}

static int lds_eval_eq(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_rel(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end) {
        const lds_tok_t *op = &items[*idx];
        uint64_t rhs = 0;
        if (!lds_tok_is(op, LDS_TOK_OTHER, "==") && !lds_tok_is(op, LDS_TOK_OTHER, "!=")) {
            break;
        }
        (*idx)++;
        if (lds_eval_rel(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        if (lds_tok_is(op, LDS_TOK_OTHER, "==")) {
            *out = (*out == rhs) ? 1 : 0;
        } else {
            *out = (*out != rhs) ? 1 : 0;
        }
    }
    return 0;
}

static int lds_eval_band(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_eq(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "&")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_eq(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out &= rhs;
    }
    return 0;
}

static int lds_eval_bxor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_band(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "^")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_band(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out ^= rhs;
    }
    return 0;
}

static int lds_eval_bor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_bxor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "|")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_bxor(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out |= rhs;
    }
    return 0;
}

static int lds_eval_land(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_bor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "&&")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_bor(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out = ((*out != 0) && (rhs != 0)) ? 1 : 0;
    }
    return 0;
}

static int lds_eval_lor(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    if (lds_eval_land(ec, items, idx, end, out) != 0) {
        return -1;
    }
    while (*idx < end && lds_tok_is(&items[*idx], LDS_TOK_OTHER, "||")) {
        uint64_t rhs = 0;
        (*idx)++;
        if (lds_eval_land(ec, items, idx, end, &rhs) != 0) {
            return -1;
        }
        *out = ((*out != 0) || (rhs != 0)) ? 1 : 0;
    }
    return 0;
}

/*
 * cond ? a : b.  The arm not taken is read, to find where it ends, with
 * its errors off: "DEFINED(x) ? x : 0" is what the operator is for.
 */
static int lds_eval_cond(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
    uint64_t a = 0, b = 0;
    int rc;

    if (lds_eval_lor(ec, items, idx, end, out) != 0) {
        return -1;
    }
    if (*idx >= end || !lds_tok_is(&items[*idx], LDS_TOK_OTHER, "?")) {
        return 0;
    }
    if (ec->depth >= LD_MAX_SCRIPT_EXPR_DEPTH) {
        ec->err_tok = &items[*idx];
        ec->err_msg = "expression nested too deeply";
        return -1;
    }
    (*idx)++;
    ec->depth++;
    ec->skip += *out == 0;
    rc = lds_eval_cond(ec, items, idx, end, &a);
    ec->skip -= *out == 0;
    if (rc == 0 && (*idx >= end || items[*idx].kind != LDS_TOK_COLON)) {
        ec->err_tok = *idx < end ? &items[*idx] : &items[end - 1];
        ec->err_msg = "expected ':' of the conditional";
        rc = -1;
    }
    if (rc == 0) {
        (*idx)++;
        ec->skip += *out != 0;
        rc = lds_eval_cond(ec, items, idx, end, &b);
        ec->skip -= *out != 0;
    }
    ec->depth--;
    if (rc == 0) {
        *out = *out != 0 ? a : b;
    }
    return rc;
}

static int lds_eval_expr_slice(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t begin, size_t end, uint64_t *out) {
    size_t idx = begin;

    if (ec == NULL || items == NULL || out == NULL || begin > end) {
        return -1;
    }
    if (begin == end) {
        ec->err_tok = begin < end ? &items[begin] : NULL;
        ec->err_msg = "empty expression";
        return -1;
    }
    if (lds_eval_cond(ec, items, &idx, end, out) != 0) {
        return -1;
    }
    if (idx != end) {
        ec->err_tok = &items[idx];
        ec->err_msg = "unexpected trailing tokens in expression";
        return -1;
    }
    return 0;
}

#define LD_MAX_SCRIPT_TOKEN_LEN (1u << 20)  /* 1 MiB per token */

static int lds_lex_push_text(lds_tok_t *tok, const unsigned char *start, size_t n) {
    if (n > LD_MAX_SCRIPT_TOKEN_LEN) {
        return -1;
    }
    tok->text = (char *)malloc(n + 1);
    if (tok->text == NULL) {
        return -1;
    }
    memcpy(tok->text, start, n);
    tok->text[n] = '\0';
    return 0;
}

static int lds_is_ident_start(int c) {
    return isalpha(c) || c == '_' || c == '.' || c == '$';
}

static int lds_is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.' || c == '$';
}

/* A character of a file or section name, wildcards included. */
static int lds_is_name_char(int c) {
    return c > 0 && (isalnum(c) || strchr("_.$/-+*?[]!~\\^@%", c) != NULL);
}

static void lds_advance(lds_lexer_t *lx) {
    if (lx->pos >= lx->len) {
        return;
    }
    if (lx->buf[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static int lds_peek(const lds_lexer_t *lx, size_t off) {
    if (lx->pos + off >= lx->len) {
        return -1;
    }
    return lx->buf[lx->pos + off];
}

static int lds_skip_ws_comments(lds_lexer_t *lx) {
    for (;;) {
        int c = lds_peek(lx, 0);
        if (c < 0) {
            return 0;
        }
        if (isspace(c)) {
            lds_advance(lx);
            continue;
        }
        if (c == '/' && lds_peek(lx, 1) == '/') {
            while ((c = lds_peek(lx, 0)) >= 0 && c != '\n') {
                lds_advance(lx);
            }
            continue;
        }
        if (c == '/' && lds_peek(lx, 1) == '*') {
            lds_advance(lx);
            lds_advance(lx);
            while ((c = lds_peek(lx, 0)) >= 0) {
                if (c == '*' && lds_peek(lx, 1) == '/') {
                    lds_advance(lx);
                    lds_advance(lx);
                    break;
                }
                lds_advance(lx);
            }
            if (c < 0) {
                return -1;
            }
            continue;
        }
        return 0;
    }
}

static int lds_lex_next(lds_lexer_t *lx, lds_tok_t *out) {
    size_t start;
    int c;

    memset(out, 0, sizeof(*out));
    out->path = lx->path;
    out->line = lx->line;
    out->col = lx->col;
    if (lds_skip_ws_comments(lx) != 0) {
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup("<unterminated-comment>");
        out->line = lx->line;
        out->col = lx->col;
        return out->text == NULL ? -1 : 0;
    }
    out->path = lx->path;
    out->line = lx->line;
    out->col = lx->col;
    c = lds_peek(lx, 0);
    if (c < 0) {
        out->kind = LDS_TOK_EOF;
        return 0;
    }
    if (lx->names && lds_is_name_char(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && lds_is_name_char(c)) {
            /* A comment directly after a name ends the name. */
            if (c == '/' && (lds_peek(lx, 1) == '*' || lds_peek(lx, 1) == '/') && lx->pos > start) {
                break;
            }
            lds_advance(lx);
        }
        out->kind = LDS_TOK_IDENT;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (lds_is_ident_start(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && lds_is_ident_char(c)) {
            lds_advance(lx);
        }
        out->kind = LDS_TOK_IDENT;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (isdigit(c)) {
        start = lx->pos;
        while ((c = lds_peek(lx, 0)) >= 0 && (isalnum(c) || c == 'x' || c == 'X')) {
            lds_advance(lx);
        }
        out->kind = LDS_TOK_NUMBER;
        return lds_lex_push_text(out, lx->buf + start, lx->pos - start);
    }
    if (c == '"') {
        start = ++lx->pos;
        lx->col++;
        while ((c = lds_peek(lx, 0)) >= 0) {
            if (c == '"') {
                size_t end = lx->pos;
                lds_advance(lx);
                out->kind = LDS_TOK_STRING;
                return lds_lex_push_text(out, lx->buf + start, end - start);
            }
            if (c == '\\' && lds_peek(lx, 1) >= 0) {
                lds_advance(lx);
            }
            lds_advance(lx);
        }
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup("<unterminated-string>");
        return out->text == NULL ? -1 : 0;
    }

    if ((c == '=' && lds_peek(lx, 1) == '=') || (c == '!' && lds_peek(lx, 1) == '=') ||
        (c == '<' && (lds_peek(lx, 1) == '=' || lds_peek(lx, 1) == '<')) ||
        (c == '>' && (lds_peek(lx, 1) == '=' || lds_peek(lx, 1) == '>')) ||
        (c == '&' && lds_peek(lx, 1) == '&') || (c == '|' && lds_peek(lx, 1) == '|')) {
        char op[3];
        op[0] = (char)c;
        op[1] = (char)lds_peek(lx, 1);
        op[2] = '\0';
        lds_advance(lx);
        lds_advance(lx);
        out->kind = LDS_TOK_OTHER;
        out->text = xstrdup(op);
        return out->text == NULL ? -1 : 0;
    }

    lds_advance(lx);
    switch (c) {
    case '{': out->kind = LDS_TOK_LBRACE; break;
    case '}': out->kind = LDS_TOK_RBRACE; break;
    case '(': out->kind = LDS_TOK_LPAREN; break;
    case ')': out->kind = LDS_TOK_RPAREN; break;
    case ';': out->kind = LDS_TOK_SEMI; break;
    case ':': out->kind = LDS_TOK_COLON; break;
    case ',': out->kind = LDS_TOK_COMMA; break;
    case '=': out->kind = LDS_TOK_EQUAL; break;
    default:
        out->kind = LDS_TOK_OTHER;
        out->text = (char *)malloc(2);
        if (out->text == NULL) {
            return -1;
        }
        out->text[0] = (char)c;
        out->text[1] = '\0';
        break;
    }
    return 0;
}

static void lds_report_error(const strvec_t *include_stack, const lds_tok_t *tok, const char *msg) {
    size_t i;
    const char *path = tok != NULL && tok->path != NULL ? tok->path : "<script>";
    size_t line = tok != NULL ? tok->line : 1;
    size_t col = tok != NULL ? tok->col : 1;

    fprintf(stderr, "ld: %s:%zu:%zu: linker script parse error: %s\n", path, line, col, msg != NULL ? msg : "error");
    ld_diag_note("script-parse", path, "check linker script syntax and block delimiters");
    if (include_stack != NULL && include_stack->count > 1) {
        fprintf(stderr, "ld: include stack:\n");
        for (i = 0; i < include_stack->count; ++i) {
            fprintf(stderr, "ld:   %s\n", include_stack->items[i]);
        }
    }
}

static char *dirname_copy(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL || path[0] == '\0') {
        return xstrdup(".");
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    n = (size_t)(slash - path);
    if (n == 0) {
        return xstrdup("/");
    }
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *resolve_script_include_path(const char *parent_path, const char *name) {
    char *dir;
    char *joined;

    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    if (name[0] == '/') {
        return xstrdup(name);
    }
    dir = dirname_copy(parent_path);
    if (dir == NULL) {
        return NULL;
    }
    joined = path_join(dir, name);
    free(dir);
    return joined;
}

/*
 * A script's wildcard against a name: '*' is any run of characters, '?'
 * any one, [abc] [a-z] [!a-z] one of a set, '\' takes the next character
 * as itself.  A '*' matches '/' too, as it does for GNU ld.
 */
static int lds_glob(const char *pat, const char *s) {
    const char *star = NULL;
    const char *resume = NULL;

    if (pat == NULL || s == NULL) {
        return 0;
    }
    for (;;) {
        if (*pat == '*') {
            star = ++pat;
            resume = s;
            continue;
        }
        if (*s == '\0') {
            return *pat == '\0';
        }
        if (*pat == '[' && strchr(pat + 2, ']') != NULL) {
            const char *q = pat + 1;
            int negate = *q == '!' || *q == '^';
            int hit = 0;

            if (negate) {
                q++;
            }
            do {
                if (q[1] == '-' && q[2] != ']' && q[2] != '\0') {
                    hit |= (unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2];
                    q += 3;
                } else {
                    hit |= *s == *q;
                    q++;
                }
            } while (*q != ']' && *q != '\0');
            if (*q == ']' && hit != negate) {
                pat = q + 1;
                s++;
                continue;
            }
        } else if (*pat == '?' || (*pat == '\\' && pat[1] != '\0' && pat[1] == *s) ||
                   (*pat != '\\' && *pat != '\0' && *pat == *s)) {
            pat += *pat == '\\' ? 2 : 1;
            s++;
            continue;
        }
        if (star == NULL) {
            return 0;
        }
        pat = star;
        s = ++resume;
    }
}

static int script_section_pattern_match(const char *pattern, const char *name) {
    return lds_glob(pattern, name);
}

static int apply_script_keep_pattern(elfobj_t *obj, const char *pattern) {
    size_t i;
    size_t count;

    if (obj == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    count = elf_section_count(obj);
    for (i = 0; i < count; ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        uint64_t flags;
        if (name == NULL || !script_section_pattern_match(pattern, name)) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_GNU_RETAIN) == 0 && elf_section_set_flags(sec, flags | SHF_GNU_RETAIN) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int is_protected_output_section_name(const char *name) {
    if (name == NULL) {
        return 1;
    }
    return strcmp(name, ".shstrtab") == 0 || strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0;
}

static int apply_script_discard_pattern(elfobj_t *obj, const char *pattern) {
    size_t i;

    if (obj == NULL || pattern == NULL || pattern[0] == '\0') {
        return 0;
    }
    for (i = elf_section_count(obj); i > 0; --i) {
        size_t idx = i - 1;
        elf_section_t *sec = elf_section_get(obj, idx);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        if (sec == NULL || name == NULL || is_protected_output_section_name(name)) {
            continue;
        }
        if (!script_section_pattern_match(pattern, name)) {
            continue;
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int phdr_type_from_token(const char *tok, uint32_t *out_type) {
    if (tok == NULL || out_type == NULL) {
        return -1;
    }
    if (strcmp(tok, "PT_LOAD") == 0) {
        *out_type = PT_LOAD;
    } else if (strcmp(tok, "PT_DYNAMIC") == 0) {
        *out_type = PT_DYNAMIC;
    } else if (strcmp(tok, "PT_NOTE") == 0) {
        *out_type = PT_NOTE;
    } else if (strcmp(tok, "PT_TLS") == 0) {
        *out_type = PT_TLS;
    } else if (strcmp(tok, "PT_GNU_EH_FRAME") == 0) {
        *out_type = PT_GNU_EH_FRAME;
    } else if (strcmp(tok, "PT_GNU_RELRO") == 0) {
        *out_type = PT_GNU_RELRO;
    } else if (strcmp(tok, "PT_GNU_STACK") == 0) {
        *out_type = PT_GNU_STACK;
    } else if (strcmp(tok, "PT_GNU_PROPERTY") == 0) {
        *out_type = PT_GNU_PROPERTY;
    } else if (strcmp(tok, "PT_INTERP") == 0) {
        *out_type = PT_INTERP;
    } else if (strcmp(tok, "PT_PHDR") == 0) {
        *out_type = PT_PHDR;
    } else {
        return -1;
    }
    return 0;
}

/* Segment permissions (p_flags). */
enum {
    LD_PF_X = 0x1u,
    LD_PF_W = 0x2u,
    LD_PF_R = 0x4u
};

static uint32_t phdr_default_flags(uint32_t type) {
    if (type == PT_LOAD) {
        return LD_PF_R;
    }
    if (type == PT_DYNAMIC || type == PT_TLS || type == PT_GNU_STACK) {
        return LD_PF_R | LD_PF_W;
    }
    return LD_PF_R;
}

static lds_stmt_t *script_find_outsec(lds_script_t *sc, const char *name, size_t *index);

/*
 * The program headers a script's PHDRS declares, with the sections that
 * its SECTIONS puts in each.  A section goes in the headers named after
 * its output section (":text :note"), and one that names none goes where
 * the one before it went.  A PT_LOAD the script gives no FLAGS gets them
 * from what is in it.  1: done; 0: the script declares none.
 */
static int add_script_segments(elfobj_t *obj, const ld_ctx_t *ctx) {
    lds_script_t *sc = ctx->script;
    const lds_phdr_vec_t *phdrs;
    const strvec_t *current = NULL;
    elf_segment_t **segs = NULL;
    uint32_t *flags = NULL;
    size_t i, k;
    int pass;
    int rc = -1;

    if (sc == NULL || sc->phdrs.count == 0) {
        return 0;
    }
    phdrs = &sc->phdrs;
    segs = (elf_segment_t **)calloc(phdrs->count, sizeof(*segs));
    flags = (uint32_t *)calloc(phdrs->count, sizeof(*flags));
    if (segs == NULL || flags == NULL) {
        goto done;
    }
    /* Once to learn each header's flags, once more to fill them. */
    for (pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            int have_phdr = 0, have_interp = 0;

            for (k = 0; k < phdrs->count; ++k) {
                have_phdr |= phdrs->items[k].type == PT_PHDR;
                have_interp |= phdrs->items[k].type == PT_INTERP;
            }
            if (!have_phdr && elf_add_segment(obj, PT_PHDR, LD_PF_R, 8) == NULL) {
                goto done;
            }
            if (!have_interp && ctx->interp_path != NULL && ctx->interp_path[0] != '\0' &&
                elf_add_interp_segment(obj, ctx->interp_path) == NULL) {
                goto done;
            }
            for (k = 0; k < phdrs->count; ++k) {
                const lds_phdr_entry_t *ph = &phdrs->items[k];

                if (ph->type == PT_INTERP && ctx->interp_path != NULL && ctx->interp_path[0] != '\0') {
                    segs[k] = elf_add_interp_segment(obj, ctx->interp_path);
                } else {
                    segs[k] = elf_add_segment(obj, ph->type,
                                              ph->has_flags || ph->type != PT_LOAD ? ph->flags : (flags[k] | LD_PF_R),
                                              ph->align);
                }
                if (segs[k] == NULL) {
                    goto done;
                }
            }
        }
        current = NULL;
        for (i = 0; i < elf_section_count(obj); ++i) {
            elf_section_t *sec = elf_section_get(obj, i);
            const char *name = sec != NULL ? elf_section_name(sec) : NULL;
            const lds_stmt_t *os;
            uint64_t sflags;

            if (name == NULL || ((sflags = elf_section_flags(sec)) & SHF_ALLOC) == 0) {
                continue;
            }
            os = script_find_outsec(sc, name, NULL);
            if (os != NULL && os->phdrs.count != 0) {
                current = &os->phdrs;
            }
            for (k = 0; current != NULL && k < current->count; ++k) {
                int pidx = lds_phdr_vec_find(phdrs, current->items[k]);

                if (pidx < 0) {
                    fprintf(stderr, "ld: section %s is assigned to program header '%s', which PHDRS does not declare\n",
                            name, current->items[k]);
                    goto done;
                }
                if (pass == 0) {
                    flags[pidx] |= ((sflags & SHF_WRITE) != 0 ? LD_PF_W : 0) |
                                   ((sflags & SHF_EXECINSTR) != 0 ? LD_PF_X : 0);
                } else if (phdrs->items[pidx].type != PT_INTERP &&
                           elf_segment_add_section(segs[pidx], sec) != ELF_OK) {
                    goto done;
                }
            }
        }
    }
    rc = 1;
done:
    free(segs);
    free(flags);
    return rc;
}

/*
 * The parser of linker scripts: one token of lookahead, read either as a
 * name or as part of an expression according to what the grammar expects
 * there.  When the two readings would differ the token is read again.
 * Expressions are kept as their tokens and evaluated when their values
 * exist.
 */
#define LD_MAX_SCRIPT_EXPR_TOKENS 4096

typedef struct {
    lds_lexer_t lx;
    lds_lexer_t before;         /* lx as it was before the token in hand */
    lds_tok_t tok;
    int have;
    lds_script_t *sc;
    const ld_ctx_t *ctx;
    strvec_t *include_stack;
    int depth;
} lds_parser_t;

static int lds_parse_file(lds_script_t *sc, const ld_ctx_t *ctx, strvec_t *include_stack, const char *path,
                          lds_where_t where, lds_stmtvec_t *v, int depth);

static void lds_stmtvec_free(lds_stmtvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        lds_stmt_t *st = &v->items[i];

        lds_tok_free(&st->at);
        lds_tokvec_free(&st->expr);
        lds_tokvec_free(&st->align);
        free(st->message);
        free(st->region);
        lds_stmtvec_free(&st->body);
        strvec_free(&st->phdrs);
        strvec_free(&st->patterns);
        strvec_free(&st->excludes);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static void lds_script_free(lds_script_t *sc) {
    size_t i;

    if (sc == NULL) {
        return;
    }
    lds_stmtvec_free(&sc->stmts);
    lds_phdr_vec_free(&sc->phdrs);
    for (i = 0; i < sc->region_count; ++i) {
        free(sc->regions[i].name);
        lds_tokvec_free(&sc->regions[i].origin);
        lds_tokvec_free(&sc->regions[i].length);
    }
    free(sc->regions);
    defsymvec_free(&sc->locals);
    free(sc->entry);
    strvec_free(&sc->files);
    free(sc);
}

static lds_stmt_t *lds_stmt_new(lds_stmtvec_t *v, lds_stmt_kind_t kind, lds_where_t where, const lds_tok_t *at) {
    lds_stmt_t *st;

    if (v->count == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 16;
        lds_stmt_t *n = (lds_stmt_t *)realloc(v->items, ncap * sizeof(*n));

        if (n == NULL) {
            return NULL;
        }
        v->items = n;
        v->cap = ncap;
    }
    st = &v->items[v->count];
    memset(st, 0, sizeof(*st));
    st->kind = kind;
    st->where = where;
    st->op = '=';
    if (lds_tok_dup(&st->at, at) != 0) {
        return NULL;
    }
    v->count++;
    return st;
}

static void lp_error(lds_parser_t *p, const lds_tok_t *tok, const char *msg) {
    lds_report_error(p->include_stack, tok, msg);
}

/* The next token, unread.  NULL when there is none to be had, reported. */
static const lds_tok_t *lp_peek(lds_parser_t *p, int names) {
    if (p->have && p->lx.names != names) {
        lds_tok_free(&p->tok);
        p->lx = p->before;
        p->have = 0;
    }
    if (!p->have) {
        p->before = p->lx;
        p->lx.names = names;
        if (lds_lex_next(&p->lx, &p->tok) != 0) {
            lds_tok_t here;

            memset(&here, 0, sizeof(here));
            here.path = p->lx.path;
            here.line = p->lx.line;
            here.col = p->lx.col;
            lp_error(p, &here, "out of memory reading the script");
            return NULL;
        }
        if (p->tok.kind == LDS_TOK_OTHER && p->tok.text != NULL && p->tok.text[0] == '<' &&
            strncmp(p->tok.text, "<unterminated-", 14) == 0) {
            lp_error(p, &p->tok, strcmp(p->tok.text, "<unterminated-comment>") == 0
                                     ? "comment is not closed" : "string is not closed");
            lds_tok_free(&p->tok);
            return NULL;
        }
        p->have = 1;
    }
    return &p->tok;
}

static void lp_take(lds_parser_t *p) {
    if (p->have) {
        lds_tok_free(&p->tok);
        p->have = 0;
    }
}

static int lp_expect(lds_parser_t *p, lds_tok_kind_t kind, const char *what) {
    const lds_tok_t *t = lp_peek(p, 0);

    if (t == NULL) {
        return -1;
    }
    if (t->kind != kind) {
        lp_error(p, t, what);
        return -1;
    }
    lp_take(p);
    return 0;
}

/* An optional ';'. */
static int lp_semi(lds_parser_t *p) {
    const lds_tok_t *t = lp_peek(p, 0);

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
    }
    return 0;
}

static int lds_word_in(const char *w, const char *const *list) {
    size_t i;

    for (i = 0; list[i] != NULL; ++i) {
        if (strcmp(w, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The operator of a compound assignment, if `t` begins one. */
static char lds_assign_op(const lds_tok_t *t) {
    static const char *const ops[] = { "+", "-", "*", "/", "&", "|", "<<", ">>", NULL };

    if (t->kind == LDS_TOK_OTHER && t->text != NULL && lds_word_in(t->text, ops)) {
        return t->text[0];
    }
    return 0;
}

/* "( ... )" or "{ ... }" that this linker has no use for. */
static int lp_skip_group(lds_parser_t *p, lds_tok_kind_t open, lds_tok_kind_t close, const char *what) {
    int depth = 0;

    if (lp_expect(p, open, what) != 0) {
        return -1;
    }
    depth = 1;
    while (depth > 0) {
        const lds_tok_t *t = lp_peek(p, 1);

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_EOF) {
            lp_error(p, t, what);
            return -1;
        }
        if (t->kind == open) {
            depth++;
        } else if (t->kind == close) {
            depth--;
        }
        lp_take(p);
    }
    return 0;
}

/*
 * An expression's tokens, up to what ends it, which is left unread: a ';'
 * or ',', the ')' of whatever it is inside, or with `colon` a ':' that is
 * not a conditional's.  With `juxta` it also ends where a second operand
 * follows a first with nothing between, which is all that separates the
 * lines of MEMORY.
 */
static int lp_expr(lds_parser_t *p, lds_tokvec_t *out, int colon, int juxta) {
    int depth = 0;
    int cond = 0;
    int operand = 0;            /* the last token was the end of an operand */

    for (;;) {
        const lds_tok_t *t = lp_peek(p, 0);
        int starts;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_EOF || t->kind == LDS_TOK_LBRACE ||
            (t->kind == LDS_TOK_RBRACE && !juxta)) {
            lp_error(p, t, "expression is not finished");
            return -1;
        }
        starts = t->kind == LDS_TOK_IDENT || t->kind == LDS_TOK_NUMBER || t->kind == LDS_TOK_STRING;
        if (depth == 0) {
            if (t->kind == LDS_TOK_SEMI || t->kind == LDS_TOK_COMMA || t->kind == LDS_TOK_RPAREN ||
                t->kind == LDS_TOK_RBRACE || t->kind == LDS_TOK_EQUAL) {
                break;
            }
            if (t->kind == LDS_TOK_COLON) {
                if (cond == 0) {
                    if (colon) {
                        break;
                    }
                    lp_error(p, t, "unexpected ':' in expression");
                    return -1;
                }
                cond--;
            }
            if (lds_tok_is(t, LDS_TOK_OTHER, "?")) {
                cond++;
            }
            if (juxta && operand && starts) {
                break;
            }
        }
        if (t->kind == LDS_TOK_LPAREN) {
            depth++;
        } else if (t->kind == LDS_TOK_RPAREN) {
            depth--;
        }
        operand = starts || t->kind == LDS_TOK_RPAREN;
        if (out->count >= LD_MAX_SCRIPT_EXPR_TOKENS) {
            lp_error(p, t, "expression is too long");
            return -1;
        }
        if (lds_tokvec_push(out, t) != 0) {
            return -1;
        }
        lp_take(p);
    }
    if (out->count == 0) {
        const lds_tok_t *t = lp_peek(p, 0);

        if (t != NULL) {
            lp_error(p, t, "expected an expression");
        }
        return -1;
    }
    return 0;
}

/*
 * name = expr, name op= expr; the name has been read.  `preop` is the
 * operator when the name swallowed it ("a+" before "=").  `closer` is what
 * follows: a ';', or the ')' of PROVIDE(...).
 */
static int lp_assignment(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *name, char preop,
                         int provide, int hidden, lds_tok_kind_t closer) {
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_ASSIGN, where, name);
    const lds_tok_t *t;

    if (st == NULL) {
        return -1;
    }
    st->provide = provide;
    st->hidden = hidden;
    t = lp_peek(p, 0);
    if (t == NULL) {
        return -1;
    }
    if (preop != 0) {
        st->op = preop;
    } else if (lds_assign_op(t) != 0) {
        st->op = lds_assign_op(t);
        lp_take(p);
    }
    if (lp_expect(p, LDS_TOK_EQUAL, "expected '=' after the name") != 0 ||
        lp_expr(p, &st->expr, 0, 0) != 0) {
        return -1;
    }
    if (closer == LDS_TOK_RPAREN) {
        return lp_expect(p, LDS_TOK_RPAREN, "expected ')'") != 0 ? -1 : lp_semi(p);
    }
    return lp_expect(p, LDS_TOK_SEMI, "expected ';' after the assignment");
}

/* PROVIDE(name = expr), PROVIDE_HIDDEN(...), HIDDEN(...); the keyword has been read. */
static int lp_provide(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *kw) {
    int provide = strcmp(kw->text, "HIDDEN") != 0;
    int hidden = strcmp(kw->text, "PROVIDE") != 0;
    const lds_tok_t *t;
    lds_tok_t name;
    int rc;

    if (lp_expect(p, LDS_TOK_LPAREN, "expected '('") != 0 || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a symbol name");
        return -1;
    }
    if (lds_tok_dup(&name, t) != 0) {
        return -1;
    }
    lp_take(p);
    rc = lp_assignment(p, v, where, &name, 0, provide, hidden, LDS_TOK_RPAREN);
    lds_tok_free(&name);
    return rc;
}

/* ASSERT(expr, "message"); the keyword has been read.  The ';' is optional. */
static int lp_assert(lds_parser_t *p, lds_stmtvec_t *v, lds_where_t where, const lds_tok_t *kw) {
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_ASSERT, where, kw);
    const lds_tok_t *t;

    if (st == NULL || lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ASSERT") != 0 ||
        lp_expr(p, &st->expr, 0, 0) != 0 || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_COMMA) {
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind != LDS_TOK_STRING && t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, "expected the message of the ASSERT");
            return -1;
        }
        st->message = xstrdup(t->text);
        if (st->message == NULL) {
            return -1;
        }
        lp_take(p);
    }
    return lp_expect(p, LDS_TOK_RPAREN, "expected ')' after ASSERT") != 0 ? -1 : lp_semi(p);
}

/* ENTRY(symbol); the keyword has been read. */
static int lp_entry(lds_parser_t *p) {
    const lds_tok_t *t;

    if (lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ENTRY") != 0 || (t = lp_peek(p, 1)) == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a symbol name");
        return -1;
    }
    free(p->sc->entry);
    p->sc->entry = xstrdup(t->text);
    if (p->sc->entry == NULL) {
        return -1;
    }
    lp_take(p);
    return lp_expect(p, LDS_TOK_RPAREN, "expected ')' after ENTRY") != 0 ? -1 : lp_semi(p);
}

/* INCLUDE file, where a statement may stand; the keyword has been read. */
static int lp_include(lds_parser_t *p, lds_where_t where, lds_stmtvec_t *v) {
    const lds_tok_t *t = lp_peek(p, 1);
    char *path = NULL;
    size_t i;
    int rc;

    if (t == NULL) {
        return -1;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a file name after INCLUDE");
        return -1;
    }
    /* Beside the script that names it, then as given, then along -L. */
    path = resolve_script_include_path(p->lx.path, t->text);
    if (path != NULL && access(path, R_OK) != 0) {
        free(path);
        path = access(t->text, R_OK) == 0 ? xstrdup(t->text) : NULL;
        for (i = 0; path == NULL && p->ctx != NULL && i < p->ctx->lib_paths.count; ++i) {
            size_t n = strlen(p->ctx->lib_paths.items[i]) + strlen(t->text) + 2;
            char *cand = (char *)malloc(n);

            if (cand == NULL) {
                return -1;
            }
            snprintf(cand, n, "%s/%s", p->ctx->lib_paths.items[i], t->text);
            if (access(cand, R_OK) == 0) {
                path = cand;
            } else {
                free(cand);
            }
        }
        if (path == NULL) {
            lp_error(p, t, "INCLUDE: file not found");
            return -1;
        }
    }
    if (path == NULL) {
        return -1;
    }
    lp_take(p);
    rc = lds_parse_file(p->sc, p->ctx, p->include_stack, path, where, v, p->depth + 1);
    free(path);
    return rc;
}

/*
 * file ( section ... ): which input sections go here.  The file pattern
 * has been read.  SORT and its relatives are accepted and change nothing,
 * the order within an output section being the order of the inputs.
 */
static int lp_input(lds_parser_t *p, lds_stmtvec_t *v, const lds_tok_t *file, int keep) {
    static const char *const sorts[] = { "SORT", "SORT_BY_NAME", "SORT_BY_ALIGNMENT",
                                         "SORT_BY_INIT_PRIORITY", "SORT_NONE", NULL };
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_INPUT, LDS_IN_BODY, file);
    int nested = 0;

    if (st == NULL || lp_expect(p, LDS_TOK_LPAREN, "expected '(' after the file name") != 0) {
        return -1;
    }
    st->keep = keep;
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        char *word;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RPAREN) {
            lp_take(p);
            if (nested == 0) {
                break;
            }
            nested--;
            continue;
        }
        if (t->kind == LDS_TOK_COMMA) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
            lp_error(p, t, "expected a section name or ')'");
            return -1;
        }
        word = xstrdup(t->text);
        if (word == NULL) {
            return -1;
        }
        lp_take(p);
        t = lp_peek(p, 1);
        if (t == NULL) {
            free(word);
            return -1;
        }
        if (t->kind == LDS_TOK_LPAREN && lds_word_in(word, sorts)) {
            lp_take(p);
            nested++;
        } else if (t->kind == LDS_TOK_LPAREN && strcmp(word, "EXCLUDE_FILE") == 0) {
            lp_take(p);
            for (;;) {
                t = lp_peek(p, 1);
                if (t == NULL) {
                    free(word);
                    return -1;
                }
                if (t->kind == LDS_TOK_RPAREN) {
                    lp_take(p);
                    break;
                }
                if ((t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) ||
                    strvec_push(&st->excludes, t->text) != 0) {
                    lp_error(p, t, "expected a file name in EXCLUDE_FILE");
                    free(word);
                    return -1;
                }
                lp_take(p);
            }
        } else if (strvec_push(&st->patterns, word) != 0) {
            free(word);
            return -1;
        }
        free(word);
    }
    return 0;
}

/* Whether `text` is a name as an expression has them; *op is set to a
 * trailing operator that reading it as a file name took in ("a+"). */
static int lds_expr_name(char *text, char *op) {
    size_t n = strlen(text);
    size_t i;

    *op = 0;
    if (n > 1 && strchr("+-*/", text[n - 1]) != NULL) {
        *op = text[n - 1];
        text[--n] = '\0';
    }
    if (n == 0 || !lds_is_ident_start((unsigned char)text[0])) {
        return 0;
    }
    for (i = 1; i < n; ++i) {
        if (!lds_is_ident_char((unsigned char)text[i])) {
            return 0;
        }
    }
    return 1;
}

/*
 * One statement of an output section's body.  1: the '}' that ends the
 * body (read); 2: end of file; 0: a statement; -1: an error, reported.
 */
static int lp_body_statement(lds_parser_t *p, lds_stmtvec_t *v) {
    static const char *const data[] = { "BYTE", "SHORT", "LONG", "QUAD", "SQUAD", NULL };
    static const char *const nothing[] = { "CONSTRUCTORS", "CREATE_OBJECT_SYMBOLS", NULL };
    const lds_tok_t *t = lp_peek(p, 1);
    lds_tok_t head;
    char op = 0;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected a statement or '}'");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    t = lp_peek(p, 0);
    if (t == NULL) {
        lds_tok_free(&head);
        return -1;
    }
    if (strcmp(head.text, "KEEP") == 0 && t->kind == LDS_TOK_LPAREN) {
        lds_tok_t file;

        lp_take(p);
        t = lp_peek(p, 1);
        if (t != NULL && (t->kind == LDS_TOK_IDENT || t->kind == LDS_TOK_STRING) && lds_tok_dup(&file, t) == 0) {
            lp_take(p);
            rc = lp_input(p, v, &file, 1);
            lds_tok_free(&file);
            if (rc == 0) {
                rc = lp_expect(p, LDS_TOK_RPAREN, "expected ')' after KEEP");
            }
        } else if (t != NULL) {
            lp_error(p, t, "expected a file name in KEEP");
        }
    } else if (strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
               strcmp(head.text, "HIDDEN") == 0) {
        rc = lp_provide(p, v, LDS_IN_BODY, &head);
    } else if (strcmp(head.text, "ASSERT") == 0) {
        rc = lp_assert(p, v, LDS_IN_BODY, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_BODY, v);
    } else if (lds_word_in(head.text, data) && t->kind == LDS_TOK_LPAREN) {
        lds_stmt_t *st = lds_stmt_new(v, LDS_ST_DATA, LDS_IN_BODY, &head);

        lp_take(p);
        if (st != NULL && lp_expr(p, &st->expr, 0, 0) == 0) {
            rc = lp_expect(p, LDS_TOK_RPAREN, "expected ')'");
        }
    } else if (strcmp(head.text, "FILL") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "FILL( is not closed");
    } else if (lds_word_in(head.text, nothing)) {
        rc = 0;
    } else if (t->kind == LDS_TOK_LPAREN) {
        rc = lp_input(p, v, &head, 0);
    } else if ((t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) && lds_expr_name(head.text, &op)) {
        rc = lp_assignment(p, v, LDS_IN_BODY, &head, op, 0, 0, LDS_TOK_SEMI);
    } else if (t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) {
        lp_error(p, &head, "not a name that can be assigned to");
    } else {
        /* A file by itself: all of its sections. */
        lds_stmt_t *st = lds_stmt_new(v, LDS_ST_INPUT, LDS_IN_BODY, &head);

        rc = st != NULL && strvec_push(&st->patterns, "*") == 0 ? 0 : -1;
    }
    lds_tok_free(&head);
    return rc;
}

/*
 * name [address] [(type)] : [AT(lma)] [ALIGN(n)] { body } [>region]
 * [AT>region] [:phdr ...] [=fill] [,] -- the name has been read.
 */
static int lp_outsec(lds_parser_t *p, lds_stmtvec_t *v, const lds_tok_t *name) {
    static const char *const types[] = { "NOLOAD", "DSECT", "COPY", "INFO", "OVERLAY", NULL };
    static const char *const skipped[] = { "AT", "SUBALIGN", "BLOCK", NULL };
    static const char *const flags[] = { "ONLY_IF_RO", "ONLY_IF_RW", "SPECIAL", NULL };
    lds_stmt_t *st = lds_stmt_new(v, LDS_ST_OUTSEC, LDS_IN_SECTIONS, name);
    const lds_tok_t *t;
    int rc;

    if (st == NULL || (t = lp_peek(p, 0)) == NULL) {
        return -1;
    }
    st->discard = strcmp(name->text, "/DISCARD/") == 0;
    if (t->kind != LDS_TOK_COLON) {
        size_t n;

        if (lp_expr(p, &st->expr, 1, 0) != 0) {
            return -1;
        }
        n = st->expr.count;
        if (n >= 3 && st->expr.items[n - 1].kind == LDS_TOK_RPAREN && st->expr.items[n - 3].kind == LDS_TOK_LPAREN &&
            st->expr.items[n - 2].kind == LDS_TOK_IDENT && lds_word_in(st->expr.items[n - 2].text, types)) {
            lds_tok_free(&st->expr.items[n - 1]);
            lds_tok_free(&st->expr.items[n - 2]);
            lds_tok_free(&st->expr.items[n - 3]);
            st->expr.count = n - 3;
        }
    }
    if (lp_expect(p, LDS_TOK_COLON, "expected ':' after the output section's name") != 0) {
        return -1;
    }
    for (;;) {
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_LBRACE) {
            lp_take(p);
            break;
        }
        if (t->kind == LDS_TOK_IDENT && strcmp(t->text, "ALIGN") == 0) {
            lp_take(p);
            if (lp_expect(p, LDS_TOK_LPAREN, "expected '(' after ALIGN") != 0 ||
                lp_expr(p, &st->align, 0, 0) != 0 || lp_expect(p, LDS_TOK_RPAREN, "expected ')'") != 0) {
                return -1;
            }
        } else if (t->kind == LDS_TOK_IDENT && lds_word_in(t->text, skipped)) {
            lp_take(p);
            if (lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0) {
                return -1;
            }
        } else if (t->kind == LDS_TOK_IDENT && lds_word_in(t->text, flags)) {
            lp_take(p);
        } else {
            lp_error(p, t, "expected '{' to begin the output section");
            return -1;
        }
    }
    while ((rc = lp_body_statement(p, &st->body)) == 0) {
    }
    if (rc != 1) {
        if (rc == 2) {
            lp_error(p, &st->at, "output section is not closed: missing '}'");
        }
        return -1;
    }
    for (;;) {
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (lds_tok_is(t, LDS_TOK_OTHER, ">") || lds_tok_is(t, LDS_TOK_IDENT, "AT")) {
            int lma = t->kind == LDS_TOK_IDENT;

            lp_take(p);
            if (lma) {
                t = lp_peek(p, 0);
                if (t == NULL || !lds_tok_is(t, LDS_TOK_OTHER, ">")) {
                    if (t != NULL) {
                        lp_error(p, t, "expected '>' after AT");
                    }
                    return -1;
                }
                lp_take(p);
            }
            t = lp_peek(p, 1);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_IDENT) {
                lp_error(p, t, "expected the name of a memory region");
                return -1;
            }
            if (!lma) {
                free(st->region);
                st->region = xstrdup(t->text);
                if (st->region == NULL) {
                    return -1;
                }
            }
            lp_take(p);
        } else if (t->kind == LDS_TOK_COLON) {
            lp_take(p);
            t = lp_peek(p, 1);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_IDENT || strvec_push(&st->phdrs, t->text) != 0) {
                lp_error(p, t, "expected the name of a program header");
                return -1;
            }
            lp_take(p);
        } else if (t->kind == LDS_TOK_EQUAL) {
            lp_take(p);
            t = lp_peek(p, 0);
            if (t == NULL) {
                return -1;
            }
            if (t->kind != LDS_TOK_NUMBER && t->kind != LDS_TOK_IDENT) {
                lp_error(p, t, "expected the fill value");
                return -1;
            }
            lp_take(p);
        } else {
            if (t->kind == LDS_TOK_COMMA) {
                lp_take(p);
            }
            break;
        }
    }
    return 0;
}

/* One statement of SECTIONS; results as lp_body_statement's. */
static int lp_sections_statement(lds_parser_t *p, lds_stmtvec_t *v) {
    const lds_tok_t *t = lp_peek(p, 1);
    lds_tok_t head;
    char op = 0;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_STRING) {
        lp_error(p, t, "expected an output section, an assignment or '}'");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    t = lp_peek(p, 0);
    if (t == NULL) {
        lds_tok_free(&head);
        return -1;
    }
    if (strcmp(head.text, "ENTRY") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_entry(p);
    } else if (strcmp(head.text, "ASSERT") == 0 && t->kind == LDS_TOK_LPAREN) {
        rc = lp_assert(p, v, LDS_IN_SECTIONS, &head);
    } else if ((strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
                strcmp(head.text, "HIDDEN") == 0) && t->kind == LDS_TOK_LPAREN) {
        rc = lp_provide(p, v, LDS_IN_SECTIONS, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_SECTIONS, v);
    } else if (t->kind == LDS_TOK_EQUAL || lds_assign_op(t) != 0) {
        if (lds_expr_name(head.text, &op)) {
            rc = lp_assignment(p, v, LDS_IN_SECTIONS, &head, op, 0, 0, LDS_TOK_SEMI);
        } else {
            lp_error(p, &head, "not a name that can be assigned to");
        }
    } else {
        rc = lp_outsec(p, v, &head);
    }
    lds_tok_free(&head);
    return rc;
}

/* PHDRS { name type [FILEHDR] [PHDRS] [AT(addr)] [FLAGS(bits)] ; ... } */
static int lp_phdrs(lds_parser_t *p) {
    if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after PHDRS") != 0) {
        return -1;
    }
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        char *name;
        uint32_t type = 0;
        uint32_t flags = 0;
        int has_flags = 0;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RBRACE) {
            lp_take(p);
            return 0;
        }
        if (t->kind == LDS_TOK_SEMI) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, t->kind == LDS_TOK_EOF ? "PHDRS is not closed: missing '}'"
                                                    : "expected the name of a program header");
            return -1;
        }
        if (lds_phdr_vec_find(&p->sc->phdrs, t->text) >= 0) {
            lp_error(p, t, "program header is declared twice");
            return -1;
        }
        name = xstrdup(t->text);
        if (name == NULL) {
            return -1;
        }
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL || (t->kind != LDS_TOK_IDENT && t->kind != LDS_TOK_NUMBER) ||
            (t->kind == LDS_TOK_IDENT && phdr_type_from_token(t->text, &type) != 0)) {
            if (t != NULL) {
                lp_error(p, t, "expected the type of the program header");
            }
            free(name);
            return -1;
        }
        if (t->kind == LDS_TOK_NUMBER) {
            type = (uint32_t)strtoul(t->text, NULL, 0);
        }
        lp_take(p);
        for (;;) {
            t = lp_peek(p, 0);
            if (t == NULL) {
                free(name);
                return -1;
            }
            if (t->kind == LDS_TOK_SEMI) {
                lp_take(p);
                break;
            }
            if (lds_tok_is(t, LDS_TOK_IDENT, "FILEHDR") || lds_tok_is(t, LDS_TOK_IDENT, "PHDRS")) {
                lp_take(p);
            } else if (lds_tok_is(t, LDS_TOK_IDENT, "AT")) {
                lp_take(p);
                if (lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "AT( is not closed") != 0) {
                    free(name);
                    return -1;
                }
            } else if (lds_tok_is(t, LDS_TOK_IDENT, "FLAGS")) {
                lds_tokvec_t e;
                lds_eval_ctx_t ec;
                uint64_t val = 0;
                int ok;

                memset(&e, 0, sizeof(e));
                memset(&ec, 0, sizeof(ec));
                lp_take(p);
                ok = lp_expect(p, LDS_TOK_LPAREN, "expected '(' after FLAGS") == 0 && lp_expr(p, &e, 0, 0) == 0;
                if (ok && lds_eval_expr_slice(&ec, e.items, 0, e.count, &val) != 0) {
                    lp_error(p, ec.err_tok != NULL ? ec.err_tok : &e.items[0],
                             ec.err_msg != NULL ? ec.err_msg : "FLAGS must be a constant");
                    ok = 0;
                }
                lds_tokvec_free(&e);
                if (!ok || lp_expect(p, LDS_TOK_RPAREN, "expected ')' after FLAGS") != 0) {
                    free(name);
                    return -1;
                }
                flags = (uint32_t)(val & 0x7u);
                has_flags = 1;
            } else {
                lp_error(p, t, "expected ';' after the program header");
                free(name);
                return -1;
            }
        }
        if (lds_phdr_vec_push(&p->sc->phdrs, name, type, has_flags ? flags : phdr_default_flags(type),
                              type == PT_LOAD ? 0x1000u : 8u) != 0) {
            free(name);
            return -1;
        }
        p->sc->phdrs.items[p->sc->phdrs.count - 1].has_flags = has_flags;
        free(name);
    }
}

/* MEMORY { name [(attr)] : ORIGIN = expr, LENGTH = expr ... } */
static int lp_memory(lds_parser_t *p) {
    static const char *const org[] = { "ORIGIN", "org", "o", NULL };
    static const char *const len[] = { "LENGTH", "len", "l", NULL };

    if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after MEMORY") != 0) {
        return -1;
    }
    for (;;) {
        const lds_tok_t *t = lp_peek(p, 1);
        lds_region_t *r;
        int k;

        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_RBRACE) {
            lp_take(p);
            return 0;
        }
        if (t->kind == LDS_TOK_SEMI || t->kind == LDS_TOK_COMMA) {
            lp_take(p);
            continue;
        }
        if (t->kind != LDS_TOK_IDENT) {
            lp_error(p, t, t->kind == LDS_TOK_EOF ? "MEMORY is not closed: missing '}'"
                                                    : "expected the name of a memory region");
            return -1;
        }
        if (p->sc->region_count == p->sc->region_cap) {
            size_t ncap = p->sc->region_cap ? p->sc->region_cap * 2 : 4;
            lds_region_t *n = (lds_region_t *)realloc(p->sc->regions, ncap * sizeof(*n));

            if (n == NULL) {
                return -1;
            }
            p->sc->regions = n;
            p->sc->region_cap = ncap;
        }
        r = &p->sc->regions[p->sc->region_count];
        memset(r, 0, sizeof(*r));
        r->name = xstrdup(t->text);
        if (r->name == NULL) {
            return -1;
        }
        p->sc->region_count++;
        lp_take(p);
        t = lp_peek(p, 0);
        if (t == NULL) {
            return -1;
        }
        if (t->kind == LDS_TOK_LPAREN && lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0) {
            return -1;
        }
        if (lp_expect(p, LDS_TOK_COLON, "expected ':' after the region's name") != 0) {
            return -1;
        }
        for (k = 0; k < 2; ++k) {
            t = lp_peek(p, 0);
            if (t == NULL) {
                return -1;
            }
            if (t->kind == LDS_TOK_COMMA) {
                lp_take(p);
                t = lp_peek(p, 0);
                if (t == NULL) {
                    return -1;
                }
            }
            if (t->kind != LDS_TOK_IDENT || !lds_word_in(t->text, k == 0 ? org : len)) {
                lp_error(p, t, k == 0 ? "expected ORIGIN" : "expected LENGTH");
                return -1;
            }
            lp_take(p);
            if (lp_expect(p, LDS_TOK_EQUAL, "expected '='") != 0 ||
                lp_expr(p, k == 0 ? &r->origin : &r->length, 0, 1) != 0) {
                return -1;
            }
        }
    }
}

/* One command at the top of a script; results as lp_body_statement's. */
static int lp_top_statement(lds_parser_t *p) {
    static const char *const call[] = { "OUTPUT_FORMAT", "OUTPUT_ARCH", "OUTPUT", "TARGET", "SEARCH_DIR", "INPUT",
                                        "GROUP", "STARTUP", "EXTERN", "NOCROSSREFS", "NOCROSSREFS_TO",
                                        "REGION_ALIAS", NULL };
    static const char *const bare[] = { "FORCE_COMMON_ALLOCATION", "INHIBIT_COMMON_ALLOCATION",
                                        "FORCE_GROUP_ALLOCATION", NULL };
    lds_stmtvec_t *v = &p->sc->stmts;
    const lds_tok_t *t = lp_peek(p, 0);
    lds_tok_t head;
    int rc = -1;

    if (t == NULL) {
        return -1;
    }
    if (t->kind == LDS_TOK_EOF) {
        return 2;
    }
    if (t->kind == LDS_TOK_RBRACE) {
        lp_take(p);
        return 1;
    }
    if (t->kind == LDS_TOK_SEMI) {
        lp_take(p);
        return 0;
    }
    if (t->kind != LDS_TOK_IDENT) {
        lp_error(p, t, "expected a command");
        return -1;
    }
    if (lds_tok_dup(&head, t) != 0) {
        return -1;
    }
    lp_take(p);
    if (strcmp(head.text, "SECTIONS") == 0) {
        if (lp_expect(p, LDS_TOK_LBRACE, "expected '{' after SECTIONS") == 0) {
            p->sc->has_sections = 1;
            while ((rc = lp_sections_statement(p, v)) == 0) {
            }
            if (rc == 2) {
                lp_error(p, &head, "SECTIONS is not closed: missing '}'");
            }
            rc = rc == 1 ? 0 : -1;
        }
    } else if (strcmp(head.text, "PHDRS") == 0) {
        rc = lp_phdrs(p);
    } else if (strcmp(head.text, "MEMORY") == 0) {
        rc = lp_memory(p);
    } else if (strcmp(head.text, "ENTRY") == 0) {
        rc = lp_entry(p);
    } else if (strcmp(head.text, "ASSERT") == 0) {
        rc = lp_assert(p, v, LDS_IN_TOP, &head);
    } else if (strcmp(head.text, "PROVIDE") == 0 || strcmp(head.text, "PROVIDE_HIDDEN") == 0 ||
               strcmp(head.text, "HIDDEN") == 0) {
        rc = lp_provide(p, v, LDS_IN_TOP, &head);
    } else if (strcmp(head.text, "INCLUDE") == 0) {
        rc = lp_include(p, LDS_IN_TOP, v);
    } else if (strcmp(head.text, "VERSION") == 0) {
        rc = lp_skip_group(p, LDS_TOK_LBRACE, LDS_TOK_RBRACE, "VERSION { is not closed");
    } else if (strcmp(head.text, "INSERT") == 0) {
        /* Read and not acted on: there is no default script to insert into. */
        t = lp_peek(p, 0);
        if (t != NULL && (lds_tok_is(t, LDS_TOK_IDENT, "AFTER") || lds_tok_is(t, LDS_TOK_IDENT, "BEFORE"))) {
            lp_take(p);
            t = lp_peek(p, 1);
            if (t != NULL && t->kind == LDS_TOK_IDENT) {
                lp_take(p);
                rc = lp_semi(p);
            } else if (t != NULL) {
                lp_error(p, t, "expected a section name after INSERT BEFORE/AFTER");
            }
        } else if (t != NULL) {
            lp_error(p, t, "expected BEFORE or AFTER after INSERT");
        }
    } else if (lds_word_in(head.text, call)) {
        rc = lp_skip_group(p, LDS_TOK_LPAREN, LDS_TOK_RPAREN, "'(' is not closed") != 0 ? -1 : lp_semi(p);
    } else if (lds_word_in(head.text, bare)) {
        rc = lp_semi(p);
    } else {
        rc = lp_assignment(p, v, LDS_IN_TOP, &head, 0, 0, 0, LDS_TOK_SEMI);
    }
    lds_tok_free(&head);
    return rc;
}

/*
 * Parse one file into `sc`: the script itself, or one it INCLUDEs, whose
 * statements are of the kind that the INCLUDE stood among and go where it
 * stood.
 */
static int lds_parse_file(lds_script_t *sc, const ld_ctx_t *ctx, strvec_t *include_stack, const char *path,
                          lds_where_t where, lds_stmtvec_t *v, int depth) {
    lds_parser_t p;
    lds_tok_t here;
    unsigned char *buf = NULL;
    size_t sz = 0;
    int rc;

    memset(&p, 0, sizeof(p));
    memset(&here, 0, sizeof(here));
    here.path = path;
    here.line = 1;
    here.col = 1;
    if (depth >= LD_MAX_SCRIPT_INCLUDE_DEPTH) {
        lds_report_error(include_stack, &here, "INCLUDE depth exceeds limit");
        return -1;
    }
    if (strvec_push(include_stack, path) != 0 || strvec_push(&sc->files, path) != 0) {
        return -1;
    }
    if (read_file(path, &buf, &sz) != 0) {
        lds_report_error(include_stack, &here, "unable to read linker script");
        strvec_pop(include_stack);
        return -1;
    }
    p.lx.buf = buf;
    p.lx.len = sz;
    p.lx.path = sc->files.items[sc->files.count - 1];
    p.lx.line = 1;
    p.lx.col = 1;
    p.sc = sc;
    p.ctx = ctx;
    p.include_stack = include_stack;
    p.depth = depth;
    do {
        rc = where == LDS_IN_TOP ? lp_top_statement(&p)
           : where == LDS_IN_SECTIONS ? lp_sections_statement(&p, v) : lp_body_statement(&p, v);
    } while (rc == 0);
    if (rc == 1) {
        here.path = p.lx.path;
        here.line = p.lx.line;
        here.col = p.lx.col;
        lds_report_error(include_stack, &here, "unexpected '}'");
    }
    lp_take(&p);
    free(buf);
    strvec_pop(include_stack);
    return rc == 2 ? 0 : -1;
}

/* Read the script at `path`.  NULL: it could not be, and why was said. */
static lds_script_t *lds_script_parse(const char *path, const ld_ctx_t *ctx) {
    lds_script_t *sc = (lds_script_t *)calloc(1, sizeof(*sc));
    strvec_t include_stack;
    int rc;

    if (sc == NULL) {
        return NULL;
    }
    memset(&include_stack, 0, sizeof(include_stack));
    rc = lds_parse_file(sc, ctx, &include_stack, path, LDS_IN_TOP, &sc->stmts, 0);
    strvec_free(&include_stack);
    if (rc != 0) {
        lds_script_free(sc);
        return NULL;
    }
    return sc;
}

static int parse_u64_dec(const char *s, size_t n, uint64_t *out) {
    size_t i = 0;
    uint64_t v = 0;
    int saw = 0;

    while (i < n && isspace((unsigned char)s[i])) {
        i++;
    }
    for (; i < n; ++i) {
        char c = s[i];
        if (isspace((unsigned char)c)) {
            break;
        }
        if (c < '0' || c > '9') {
            return -1;
        }
        saw = 1;
        if (v > (UINT64_MAX - (uint64_t)(c - '0')) / 10) {
            return -1;
        }
        v = v * 10 + (uint64_t)(c - '0');
    }
    if (!saw) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_u64_auto(const char *s, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;

    if (s == NULL || s[0] == '\0') {
        return -1;
    }
    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static int run_cmd_first_line(char *const argv[], char *out, size_t out_sz) {
    int pipefd[2];
    pid_t pid;
    int status;
    int rc = -1;
    ssize_t nread;
    char *nl;

    if (argv == NULL || argv[0] == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';

    if (pipe(pipefd) == -1) {
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            if (devnull != STDERR_FILENO) {
                close(devnull);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    do {
        nread = read(pipefd[0], out, out_sz - 1);
    } while (nread == -1 && errno == EINTR);

    if (nread > 0) {
        out[nread] = '\0';
        nl = strchr(out, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        rc = out[0] != '\0' ? 0 : 1;
    } else {
        out[0] = '\0';
        rc = 1;
    }

    close(pipefd[0]);
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            out[0] = '\0';
            return -1;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out[0] = '\0';
        return -1;
    }

    return rc;
}

static int discover_default_plugin(ld_ctx_t *ctx) {
    static char discovered[PATH_MAX];
    const char *envp;
    int rc;

    if (ctx == NULL || (ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0')) {
        return 0;
    }
    envp = getenv("SUBSTRATE_LD_PLUGIN");
    if (envp != NULL && envp[0] != '\0' && access(envp, R_OK | X_OK) == 0) {
        ctx->plugin_path = envp;
        return 0;
    }
    envp = getenv("LD_PLUGIN");
    if (envp != NULL && envp[0] != '\0' && access(envp, R_OK | X_OK) == 0) {
        ctx->plugin_path = envp;
        return 0;
    }

    {
        char *gcc_args[] = {"gcc", "-print-file-name=liblto_plugin.so", NULL};
        rc = run_cmd_first_line(gcc_args, discovered, sizeof(discovered));
        if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
            ctx->plugin_path = discovered;
            return 0;
        }
    }

    {
        char *clang_args[] = {"clang", "-print-file-name=LLVMgold.so", NULL};
        rc = run_cmd_first_line(clang_args, discovered, sizeof(discovered));
        if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
            ctx->plugin_path = discovered;
            return 0;
        }
    }

    return 0;
}

static int plugin_discover_and_handshake(ld_ctx_t *ctx) {
    int rc;
    pid_t pid;
    int status;

    if (ctx == NULL || ctx->plugin_checked) {
        return 0;
    }
    /*
     * A compiler driver names its LTO plugin on every link, and the
     * plugin GCC has is a shared object for a linker to load, which this
     * one does not: its plugins are programs it runs.  Such a plugin is
     * set aside.  It is only wanted if an input turns out to hold
     * bytecode in place of code, and that input is refused when met.
     */
    if (ctx->plugin_path != NULL && strstr(ctx->plugin_path, ".so") != NULL) {
        ctx->plugin_path = NULL;
        ctx->plugin_unusable = 1;
        ctx->plugin_checked = 1;
        return 0;
    }
    if (ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0') {
        if (ctx->plugin_opt_count != 0) {
            if (discover_default_plugin(ctx) != 0) {
                return -1;
            }
            if (ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0') {
                fprintf(stderr, "ld: -plugin-opt requires -plugin or a discoverable plugin\n");
                return -1;
            }
        } else {
            return 0;
        }
    }
    if (access(ctx->plugin_path, R_OK | X_OK) != 0) {
        fprintf(stderr, "ld: plugin not executable: %s\n", ctx->plugin_path);
        return -1;
    }

    pid = fork();
    if (pid == -1) {
        return -1;
    }

    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char *args[] = {(char *)ctx->plugin_path, "--version", NULL};
        execv(ctx->plugin_path, args);
        _exit(127);
    }

    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "ld: waitpid failed for plugin handshake\n");
            return -1;
        }
    }
    rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (rc != 0) {
        fprintf(stderr, "ld: plugin handshake failed for %s\n", ctx->plugin_path);
        return -1;
    }

    ctx->plugin_checked = 1;
    return 0;
}

static int plugin_materialize_object(const ld_ctx_t *ctx, const char *in_path, char *out_path, size_t out_path_sz) {
    char *argv[3 + 32 + 1];
    char *plugin_opt_args[32];
    size_t i, argc;
    int pipefd[2];
    pid_t pid;
    int status;
    ssize_t nread;
    char *nl;

    if (out_path == NULL || out_path_sz == 0) {
        return -1;
    }
    out_path[0] = '\0';
    if (ctx == NULL || ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0' || in_path == NULL) {
        return 0;
    }

    memset(plugin_opt_args, 0, sizeof(plugin_opt_args));
    argc = 0;
    argv[argc++] = (char *)ctx->plugin_path;
    argv[argc++] = "--materialize";
    argv[argc++] = (char *)in_path;

    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        if (argc + 1 >= sizeof(argv) / sizeof(argv[0])) {
            goto fail;
        }
        size_t len = strlen("--plugin-opt=") + strlen(ctx->plugin_opts[i]) + 1;
        plugin_opt_args[i] = (char *)malloc(len);
        if (plugin_opt_args[i] == NULL) {
            goto fail;
        }
        snprintf(plugin_opt_args[i], len, "--plugin-opt=%s", ctx->plugin_opts[i]);
        argv[argc++] = plugin_opt_args[i];
    }
    argv[argc] = NULL;

    if (pipe(pipefd) == -1) {
        goto fail;
    }

    pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        goto fail;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            if (devnull != STDERR_FILENO) {
                close(devnull);
            }
        }

        execv(ctx->plugin_path, argv);
        _exit(127);
    }

    close(pipefd[1]);

    do {
        nread = read(pipefd[0], out_path, out_path_sz - 1);
    } while (nread == -1 && errno == EINTR);

    if (nread > 0) {
        out_path[nread] = '\0';
        nl = strchr(out_path, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
    } else {
        out_path[0] = '\0';
    }

    close(pipefd[0]);
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            out_path[0] = '\0';
            goto fail;
        }
    }

    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        free(plugin_opt_args[i]);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        out_path[0] = '\0';
        return -1;
    }

    return out_path[0] != '\0' ? 1 : 0;

fail:
    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        free(plugin_opt_args[i]);
    }
    out_path[0] = '\0';
    return -1;
}

static void trim_trailing(char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            s[n - 1] = '\0';
            n--;
            continue;
        }
        break;
    }
}

static char *decode_ar_name(const char *raw_name16, const unsigned char *member_data, uint64_t member_size,
                            const char *strtab, size_t strtab_sz, size_t *name_extra) {
    char raw[17];
    char *out;
    uint64_t ext_len = 0;

    memcpy(raw, raw_name16, 16);
    raw[16] = '\0';
    trim_trailing(raw);
    if (strcmp(raw, "/") == 0 || strcmp(raw, "__.SYMDEF") == 0 || strcmp(raw, "__.SYMDEF SORTED") == 0) {
        *name_extra = 0;
        return xstrdup(raw);
    }
    if (strcmp(raw, "//") == 0) {
        *name_extra = 0;
        return xstrdup(raw);
    }
    if (strncmp(raw, "#1/", 3) == 0) {
        if (parse_u64_dec(raw + 3, strlen(raw + 3), &ext_len) != 0 || ext_len > member_size) {
            return NULL;
        }
        out = (char *)malloc((size_t)ext_len + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, member_data, (size_t)ext_len);
        out[ext_len] = '\0';
        *name_extra = (size_t)ext_len;
        return out;
    }
    if (raw[0] == '/' && isdigit((unsigned char)raw[1]) && strtab != NULL) {
        uint64_t off = 0;
        size_t i;
        size_t n = 0;
        if (parse_u64_dec(raw + 1, strlen(raw + 1), &off) != 0 || off >= strtab_sz) {
            return NULL;
        }
        for (i = (size_t)off; i < strtab_sz; ++i) {
            char c = strtab[i];
            if (c == '\0' || c == '\n') {
                break;
            }
            if (c == '/' && (i + 1 >= strtab_sz || strtab[i + 1] == '\n')) {
                break;
            }
            n++;
        }
        out = (char *)malloc(n + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, strtab + off, n);
        out[n] = '\0';
        *name_extra = 0;
        return out;
    }
    if (raw[0] != '\0') {
        size_t n = strlen(raw);
        if (n > 0 && raw[n - 1] == '/') {
            raw[n - 1] = '\0';
        }
    }
    *name_extra = 0;
    return xstrdup(raw);
}

static int obj_matches_mode(const elfobj_t *obj, int mode) {
    if (obj == NULL || elf_endian(obj) != ELFOBJ_ENDIAN_LE) {
        return 0;
    }
    if (mode == 64) {
        return elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64;
    }
    if (mode == 32) {
        return elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386;
    }
    return (elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64) ||
           (elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386);
}

static int detect_object_mode(const elfobj_t *obj) {
    if (obj == NULL || elf_endian(obj) != ELFOBJ_ENDIAN_LE) {
        return 0;
    }
    if (elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64) {
        return 64;
    }
    if (elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386) {
        return 32;
    }
    return 0;
}

static void maybe_autoswitch_mode(ld_ctx_t *ctx, const elfobj_t *obj, size_t loaded_count, const char *path) {
    int detected;

    /* The first input that is of a machine says which machine the link is
     * for, and that is the end of it: the shared objects looked into later
     * are asked whether they suit the link, not what it should be. */
    if (ctx == NULL || ctx->explicit_mode || ctx->mode_settled || loaded_count != 0) {
        return;
    }
    detected = detect_object_mode(obj);
    if (detected == 0) {
        return;
    }
    ctx->mode_settled = 1;
    if (detected == ctx->mode) {
        return;
    }
    ctx->mode = detected;
    if (ctx->trace_inputs) {
        fprintf(stderr, "ld: trace: auto-selected mode %s from %s\n",
                canonical_mode_name(ctx->mode), path != NULL ? path : "<input>");
    }
}

static int validate_relocatable_input(const elfobj_t *obj, const char *display_name) {
    size_t i;

    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags = sec != NULL ? elf_section_flags(sec) : 0;
        size_t rc;
        size_t sec_sz = 0;
        const void *sec_data;
        size_t ri;

        if (sec == NULL) {
            continue;
        }
        sec_data = elf_section_data(sec, &sec_sz);
        if (elf_section_type(sec) != SHT_NOBITS && elf_section_size(sec) > 0 && sec_data == NULL) {
            fprintf(stderr, "ld: input %s section %s has invalid data payload\n",
                    display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        if (elf_section_align(sec) == 0) {
            fprintf(stderr, "ld: input %s section %s has invalid zero alignment\n",
                    display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at((elf_section_t *)sec, ri);
            const elf_symbol_t *sym;
            int width;

            if (rel == NULL) {
                fprintf(stderr, "ld: input %s section %s has null relocation entry\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            if (elf_reloc_offset(rel) > elf_section_size(sec)) {
                fprintf(stderr, "ld: input %s section %s has out-of-range relocation offset\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            width = elf_reloc_size_for_machine(elf_machine(obj), elf_reloc_type(rel));
            if (width <= 0 || width > 8) {
                fprintf(stderr, "ld: input %s section %s has unsupported relocation type %u\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>",
                        (unsigned)elf_reloc_type(rel));
                return -1;
            }
            if (elf_reloc_offset(rel) + (uint64_t)width > elf_section_size(sec)) {
                fprintf(stderr, "ld: input %s section %s has relocation exceeding section bounds\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            sym = elf_reloc_symbol(rel);
            if (sym == NULL) {
                fprintf(stderr, "ld: input %s section %s has relocation with missing symbol\n",
                        display_name, elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
        }
    }

    return 0;
}

static int symstate_note_object(symstate_t *state, elfobj_t *obj) {
    size_t i;

    if (state == NULL || obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint8_t bind;
        uint16_t shndx;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx == SHN_UNDEF) {
            if (bind != STB_WEAK && !symset_contains(&state->defined, name)) {
                if (symset_add(&state->unresolved, name) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (symset_add(&state->defined, name) != 0) {
            return -1;
        }
        symset_remove(&state->unresolved, name);
    }
    return 0;
}

static int obj_defines_unresolved(const symstate_t *state, elfobj_t *obj) {
    size_t i;

    if (state == NULL || obj == NULL) {
        return 1;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint8_t bind;
        uint16_t shndx;

        if (sym == NULL) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        bind = elf_symbol_bind(sym);
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx != SHN_UNDEF && symset_contains(&state->unresolved, name)) {
            return 1;
        }
    }
    return 0;
}

static int parse_archive_header(const char *path, unsigned char **out_buf, size_t *out_sz, int *out_thin) {
    if (read_file(path, out_buf, out_sz) != 0) {
        fprintf(stderr, "ld: cannot read archive %s\n", path);
        return -1;
    }
    if (*out_sz < 8) {
        free(*out_buf);
        *out_buf = NULL;
        *out_sz = 0;
        fprintf(stderr, "ld: unsupported archive format: %s\n", path);
        return -1;
    }
    if (memcmp(*out_buf, "!<arch>\n", 8) == 0) {
        *out_thin = 0;
        return 0;
    }
    if (memcmp(*out_buf, "!<thin>\n", 8) == 0) {
        *out_thin = 1;
        return 0;
    }
    free(*out_buf);
    *out_buf = NULL;
    *out_sz = 0;
    fprintf(stderr, "ld: unsupported archive format: %s\n", path);
    return -1;
}

static char *path_dirname_dup(const char *path) {
    char *dup = xstrdup(path);
    char *slash;

    if (dup == NULL) {
        return NULL;
    }
    slash = strrchr(dup, '/');
    if (slash == NULL) {
        dup[0] = '.';
        dup[1] = '\0';
    } else if (slash == dup) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return dup;
}

static int path_is_within_dir(const char *dir, const char *path) {
    size_t dlen = strlen(dir);

    if (strncmp(dir, path, dlen) != 0) {
        return 0;
    }
    if (path[dlen] == '\0' || path[dlen] == '/') {
        return 1;
    }
    return 0;
}

static char *resolve_thin_member_path(const char *archive_path, const char *member_name) {
    char *archive_dir = NULL;
    char *archive_real = NULL;
    char *candidate = NULL;
    char *member_real = NULL;

    if (member_name == NULL || member_name[0] == '\0') {
        return NULL;
    }
    archive_dir = path_dirname_dup(archive_path);
    if (archive_dir == NULL) {
        return NULL;
    }
    archive_real = realpath(archive_dir, NULL);
    if (archive_real == NULL) {
        free(archive_dir);
        return NULL;
    }
    if (member_name[0] == '/') {
        candidate = xstrdup(member_name);
    } else {
        candidate = path_join(archive_real, member_name);
    }
    if (candidate == NULL) {
        free(archive_real);
        free(archive_dir);
        return NULL;
    }
    member_real = realpath(candidate, NULL);
    if (member_real == NULL || !path_is_within_dir(archive_real, member_real)) {
        free(member_real);
        free(candidate);
        free(archive_real);
        free(archive_dir);
        return NULL;
    }
    free(candidate);
    free(archive_real);
    free(archive_dir);
    return member_real;
}

static int load_object_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state, int quiet);

static int load_archive_members(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                                int whole_archive) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    size_t off = 8;
    const char *strtab = NULL;
    size_t strtab_sz = 0;
    int thin = 0;
    symset_t seen_members;
    int pass_progress;
    int pass_count = 0;
    const char *bad = NULL;

    memset(&seen_members, 0, sizeof(seen_members));
    if (parse_archive_header(path, &buf, &sz, &thin) != 0) {
        return -1;
    }

    do {
        pass_count++;
        if (pass_count > LD_MAX_ARCHIVE_SCAN_PASSES) {
            fprintf(stderr, "ld: archive resolution pass limit exceeded (%d) for %s\n",
                    LD_MAX_ARCHIVE_SCAN_PASSES, path);
            symset_free(&seen_members);
            free(buf);
            return -1;
        }
        pass_progress = 0;
        off = 8;
        while (off + 60 <= sz) {
            const unsigned char *hdr = buf + off;
            uint64_t msize = 0;
            const unsigned char *mdata = NULL;
            const unsigned char *body = NULL;
            char *mname;
            size_t name_extra = 0;
            size_t body_sz = 0;
            char member_key[32];
            int is_special = 0;
            int has_member_payload = 0;

            /*
             * An archive is input like any other, and one that is not as
             * an archive should be is said to be so: to stop reading at
             * the first thing that does not fit, as this did, is to link
             * without the members after it and blame their symbols.
             */
            if (hdr[58] != '`' || hdr[59] != '\n') {
                bad = "a member header does not end as one does";
                goto malformed;
            }
            if (parse_u64_dec((const char *)hdr + 48, 10, &msize) != 0) {
                bad = "a member's size is not a number";
                goto malformed;
            }
            off += 60;
            mdata = buf + off;
            /* A name kept in the member's data is read out of it: no
             * further than the file goes, whatever size the header claims. */
            mname = decode_ar_name((const char *)hdr, mdata, msize < (uint64_t)(sz - off) ? msize : (uint64_t)(sz - off),
                                   strtab, strtab_sz, &name_extra);
            if (mname == NULL) {
                bad = "a member's name cannot be read";
                goto malformed;
            }
            is_special = strcmp(mname, "/") == 0 ||
                         strcmp(mname, "//") == 0 ||
                         strcmp(mname, "__.SYMDEF") == 0 ||
                         strcmp(mname, "__.SYMDEF SORTED") == 0;
            has_member_payload = !thin || is_special;
            /* In the member's own width: a size_t may be narrower. */
            if (has_member_payload && msize > (uint64_t)(sz - off)) {
                free(mname);
                bad = "a member is longer than what is left of the file";
                goto malformed;
            }
            if (has_member_payload && (uint64_t)name_extra > msize) {
                free(mname);
                bad = "a member's name is longer than the member";
                goto malformed;
            }
            if (has_member_payload) {
                body_sz = (size_t)msize - name_extra;
                body = mdata + name_extra;
            }

            if (strcmp(mname, "//") == 0) {
                strtab = (const char *)body;
                strtab_sz = body_sz;
            } else if (!is_special && has_member_payload &&
                       body_sz >= 4 && body[0] == 0x7f && body[1] == 'E' && body[2] == 'L' && body[3] == 'F') {
                elfobj_t *obj = NULL;
                /* Where it is in this archive is what it is: the set is this
                 * archive's alone. */
                snprintf(member_key, sizeof(member_key), "%zu", off);
                if (!symset_contains(&seen_members, member_key) && elf_open_memory(body, body_sz, &obj) == ELF_OK) {
                    maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, path);
                    if (elf_type(obj) == ET_REL && obj_matches_mode(obj, ctx->mode) &&
                        (whole_archive || obj_defines_unresolved(state, obj))) {
                        char member_name[512];
                        if (symset_add(&seen_members, member_key) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        snprintf(member_name, sizeof(member_name), "%s(%s)", path, mname);
                        if (validate_relocatable_input(obj, member_name) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        if (objvec_push(objs, obj, member_name) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        /* The list has it now, and will close it. */
                        if (symstate_note_object(state, obj) != 0) {
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        pass_progress = 1;
                    } else {
                        elf_close(obj);
                    }
                }
            } else if (!is_special && thin) {
                char *thin_member_path = resolve_thin_member_path(path, mname);
                elfobj_t *obj = NULL;
                if (thin_member_path == NULL) {
                    fprintf(stderr, "ld: invalid thin archive member path '%s' in %s\n", mname, path);
                    free(mname);
                    symset_free(&seen_members);
                    free(buf);
                    return -1;
                }
                if (!symset_contains(&seen_members, thin_member_path)) {
                    int should_load = 0;
                    if (elf_open(thin_member_path, &obj) == ELF_OK) {
                        maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, thin_member_path);
                        should_load = elf_type(obj) == ET_REL && obj_matches_mode(obj, ctx->mode) &&
                                      (whole_archive || obj_defines_unresolved(state, obj));
                        elf_close(obj);
                    }
                    if (should_load) {
                        if (symset_add(&seen_members, thin_member_path) != 0) {
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        if (load_object_input(thin_member_path, ctx, objs, state, 1) != 0) {
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        pass_progress = 1;
                    }
                }
                free(thin_member_path);
            }
            free(mname);
            if (has_member_payload) {
                off += (size_t)msize;
                if ((off & 1u) != 0) {
                    off++;
                }
            }
        }
        if (off < sz) {
            bad = "it ends in the middle of a member header";
            goto malformed;
        }
    } while (!whole_archive && pass_progress);

    symset_free(&seen_members);
    free(buf);
    return 0;

malformed:
    fprintf(stderr, "ld: %s: malformed archive: %s (at offset %zu)\n", path, bad, off);
    symset_free(&seen_members);
    free(buf);
    return -1;
}

static int object_has_lto_sections(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        if (name != NULL &&
            (strncmp(name, ".gnu.lto_", 9) == 0 ||
             strcmp(name, ".llvmbc") == 0 ||
             strcmp(name, ".llvmcmd") == 0 ||
             strncmp(name, ".llvm.lto", 9) == 0)) {
            return 1;
        }
    }
    return 0;
}

static int load_object_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state, int quiet) {
    elfobj_t *obj = NULL;
    char mat_path[1024];
    int mat_rc;

    if (elf_open(path, &obj) != ELF_OK) {
        if (!quiet) {
            fprintf(stderr, "ld: failed to open input %s\n", path);
        }
        return -1;
    }
    maybe_autoswitch_mode(ctx, obj, objs != NULL ? objs->count : 0, path);
    if (object_has_lto_sections(obj) && ctx != NULL && ctx->plugin_unusable) {
        fprintf(stderr, "ld: %s holds LTO bytecode, and the plugin to compile it is a shared object, "
                        "which this linker cannot load: compile without -flto\n", path);
        elf_close(obj);
        return -1;
    }
    if (object_has_lto_sections(obj) && ctx != NULL && ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0') {
        mat_rc = plugin_materialize_object(ctx, path, mat_path, sizeof(mat_path));
        if (mat_rc < 0) {
            if (!quiet) {
                fprintf(stderr, "ld: plugin materialization failed for %s\n", path);
            }
            elf_close(obj);
            return -1;
        }
        if (mat_rc > 0 && strcmp(mat_path, path) != 0) {
            elf_close(obj);
            return load_object_input(mat_path, ctx, objs, state, quiet);
        }
    }
    if (!obj_matches_mode(obj, ctx->mode)) {
        if (!quiet) {
            fprintf(stderr, "ld: input %s has mismatched class/machine/endianness\n", path);
        }
        elf_close(obj);
        return -1;
    }
    if (elf_type(obj) != ET_REL) {
        if (elf_type(obj) == ET_DYN && ctx != NULL &&
            (ctx->expect_type == ET_EXEC || ctx->expect_type == ET_DYN)) {
            elf_close(obj);
            return register_dso_provider(ctx, path, state);
        }
        if (!quiet) {
            fprintf(stderr, "ld: input %s is not relocatable (only ET_REL supported)\n", path);
        }
        elf_close(obj);
        return -1;
    }
    if (validate_relocatable_input(obj, path) != 0) {
        elf_close(obj);
        return -1;
    }
    if (objvec_push(objs, obj, path) != 0) {
        elf_close(obj);
        return -1;
    }
    /* The list has it now, and will close it. */
    return symstate_note_object(state, obj) != 0 ? -1 : 0;
}

/*
 * Whether the file at `path` can be the library a -l asks for: it can be
 * read, and if it is an ELF file it is for the machine the link is for,
 * once that is known.  A library for another machine in an earlier
 * directory is passed over, not an error: a search path that names both
 * /lib64 and /lib is an ordinary one.  What is not an ELF file is an
 * archive or a script, and is looked into later.
 */
static int lib_candidate_suits(const ld_ctx_t *ctx, const char *path) {
    unsigned char h[20];
    FILE *f;
    size_t n;
    int mode;

    if (access(path, R_OK) != 0) {
        return 0;
    }
    if (!ctx->explicit_mode && !ctx->mode_settled) {
        return 1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(h, 1, sizeof(h), f);
    fclose(f);
    if (n < sizeof(h) || memcmp(h, "\177ELF", 4) != 0) {
        return 1;
    }
    /* e_ident[EI_CLASS], and e_machine at 18, little-endian. */
    mode = h[4] == 2 && h[18] == (EM_X86_64 & 0xff) && h[19] == 0 ? 64
         : h[4] == 1 && h[18] == EM_386 && h[19] == 0 ? 32 : 0;
    return mode == ctx->mode;
}

static char *resolve_library_path_suffix_ex(const ld_ctx_t *ctx, const char *name, const char *suffix,
                                            int include_default_dirs) {
    static const char *default_dirs[] = {
        "/usr/lib",
        "/usr/local/lib"
    };
    char leaf[512];
#ifdef LD_SUBSTRATE_BUILD
    const char *sysroot;
#endif
    size_t i;

    snprintf(leaf, sizeof(leaf), "lib%s%s", name, suffix != NULL ? suffix : "");
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        char *cand = path_join(ctx->lib_paths.items[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
        /* The directories are searched in order, and each for the shared
         * library and then the archive: an archive in an earlier
         * directory is found before a shared library in a later one. */
        if (suffix != NULL && strcmp(suffix, ".so") == 0) {
            char aleaf[512];
            int earlier;

            snprintf(aleaf, sizeof(aleaf), "lib%s.a", name);
            cand = path_join(ctx->lib_paths.items[i], aleaf);
            earlier = cand != NULL && lib_candidate_suits(ctx, cand);
            free(cand);
            if (earlier) {
                return NULL;
            }
        }
    }
    if (!include_default_dirs) {
        return NULL;
    }
#ifdef LD_SUBSTRATE_BUILD
    sysroot = getenv("SUBSTRATE_SYSROOT");
    if (sysroot != NULL && sysroot[0] != '\0') {
        for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
            char prefixed[PATH_MAX];
            char *cand;
            if (snprintf(prefixed, sizeof(prefixed), "%s%s", sysroot, default_dirs[i]) >= (int)sizeof(prefixed)) {
                continue;
            }
            cand = path_join(prefixed, leaf);
            if (cand != NULL && lib_candidate_suits(ctx, cand)) {
                return cand;
            }
            free(cand);
        }
        return NULL;
    }
#endif
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
    return NULL;
}

static char *resolve_library_path_exact(const ld_ctx_t *ctx, const char *leaf) {
    static const char *default_dirs[] = {
        "/usr/lib",
        "/usr/local/lib"
    };
#ifdef LD_SUBSTRATE_BUILD
    const char *sysroot;
#endif
    size_t i;

    if (leaf == NULL || leaf[0] == '\0') {
        return NULL;
    }
    if (strchr(leaf, '/') != NULL) {
        return access(leaf, R_OK) == 0 ? xstrdup(leaf) : NULL;
    }
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        char *cand = path_join(ctx->lib_paths.items[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
#ifdef LD_SUBSTRATE_BUILD
    sysroot = getenv("SUBSTRATE_SYSROOT");
    if (sysroot != NULL && sysroot[0] != '\0') {
        for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
            char prefixed[PATH_MAX];
            char *cand;
            if (snprintf(prefixed, sizeof(prefixed), "%s%s", sysroot, default_dirs[i]) >= (int)sizeof(prefixed)) {
                continue;
            }
            cand = path_join(prefixed, leaf);
            if (cand != NULL && lib_candidate_suits(ctx, cand)) {
                return cand;
            }
            free(cand);
        }
        return NULL;
    }
#endif
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && lib_candidate_suits(ctx, cand)) {
            return cand;
        }
        free(cand);
    }
    return NULL;
}

static char *resolve_library_path_suffix(const ld_ctx_t *ctx, const char *name, const char *suffix) {
    return resolve_library_path_suffix_ex(ctx, name, suffix, 1);
}

static char *resolve_library_path_suffix_explicit(const ld_ctx_t *ctx, const char *name, const char *suffix) {
    return resolve_library_path_suffix_ex(ctx, name, suffix, 0);
}

/* Whether the file begins as an archive does, whatever it is called. */
static int file_is_archive(const char *path) {
    char magic[8];
    FILE *f = fopen(path, "rb");
    int is = 0;

    if (f != NULL) {
        is = fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
             (memcmp(magic, "!<arch>\n", 8) == 0 || memcmp(magic, "!<thin>\n", 8) == 0);
        fclose(f);
    }
    return is;
}

static int load_path_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                           int whole_archive, int quiet) {
    if (file_is_archive(path)) {
        return load_archive_members(path, ctx, objs, state, whole_archive);
    }
    return load_object_input(path, ctx, objs, state, quiet);
}

typedef struct {
    uint16_t index;
    char *name;
} verdef_name_t;

typedef struct {
    verdef_name_t *items;
    size_t count;
    size_t cap;
} verdef_table_t;

typedef struct {
    char *name;
    uint16_t index;
    uint32_t name_off;
} dyn_verdef_t;

typedef struct {
    char *file;
    char *name;
    uint16_t index;
    uint32_t file_off;
    uint32_t name_off;
} dyn_verneed_t;

typedef struct {
    dyn_verdef_t *defs;
    size_t def_count;
    size_t def_cap;
    dyn_verneed_t *needs;
    size_t need_count;
    size_t need_cap;
    uint16_t next_index;
} dyn_ver_plan_t;

static uint16_t read_u16_endian(const uint8_t *p, elfobj_endian_t endian) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    }
    return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static uint32_t read_u32_endian(const uint8_t *p, elfobj_endian_t endian) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static uint64_t read_u64_endian(const uint8_t *p, elfobj_endian_t endian) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
               ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | (uint64_t)p[7];
    }
    return ((uint64_t)p[7] << 56) | ((uint64_t)p[6] << 48) | ((uint64_t)p[5] << 40) | ((uint64_t)p[4] << 32) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[2] << 16) | ((uint64_t)p[1] << 8) | (uint64_t)p[0];
}

static void write_u16_endian(uint8_t *p, elfobj_endian_t endian, uint16_t v) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        p[0] = (uint8_t)((v >> 8) & 0xffu);
        p[1] = (uint8_t)(v & 0xffu);
        return;
    }
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void write_u32_endian(uint8_t *p, elfobj_endian_t endian, uint32_t v) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        p[0] = (uint8_t)((v >> 24) & 0xffu);
        p[1] = (uint8_t)((v >> 16) & 0xffu);
        p[2] = (uint8_t)((v >> 8) & 0xffu);
        p[3] = (uint8_t)(v & 0xffu);
        return;
    }
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void write_u64_endian(uint8_t *p, elfobj_endian_t endian, uint64_t v) {
    if (endian == ELFOBJ_ENDIAN_BE) {
        p[0] = (uint8_t)((v >> 56) & 0xffu);
        p[1] = (uint8_t)((v >> 48) & 0xffu);
        p[2] = (uint8_t)((v >> 40) & 0xffu);
        p[3] = (uint8_t)((v >> 32) & 0xffu);
        p[4] = (uint8_t)((v >> 24) & 0xffu);
        p[5] = (uint8_t)((v >> 16) & 0xffu);
        p[6] = (uint8_t)((v >> 8) & 0xffu);
        p[7] = (uint8_t)(v & 0xffu);
        return;
    }
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
    p[4] = (uint8_t)((v >> 32) & 0xffu);
    p[5] = (uint8_t)((v >> 40) & 0xffu);
    p[6] = (uint8_t)((v >> 48) & 0xffu);
    p[7] = (uint8_t)((v >> 56) & 0xffu);
}

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

static void split_symbol_version(const char *name, const char **base, size_t *base_len, const char **ver_name,
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
    hidden = (sym_ver & VER_NDX_HIDDEN) != 0;
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

    if (symstate_define_name(state, sym_name) != 0) {
        return -1;
    }
    split_symbol_version(sym_name, &base, &base_len, &ver_name, &is_default_name);
    if (base == NULL || base_len == 0) {
        return 0;
    }
    hidden = (sym_ver & VER_NDX_HIDDEN) != 0;
    sym_ver = (uint16_t)(sym_ver & (uint16_t)~VER_NDX_HIDDEN);
    if (ver_name == NULL && sym_ver > VER_NDX_GLOBAL) {
        ver_name = verdef_lookup(defs, sym_ver);
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

static int shared_object_matches_unresolved(const char *path, ld_ctx_t *ctx, const symstate_t *state,
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

static int register_dso_provider(ld_ctx_t *ctx, const char *path, symstate_t *state) {
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

static int unresolved_symbol_has_dso_provider(ld_ctx_t *ctx, const char *name, int *out_has_provider) {
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
            if ((bind == STB_GLOBAL || bind == STB_WEAK) &&
                (vis == STV_DEFAULT || vis == STV_PROTECTED) &&
                shndx != SHN_UNDEF) {
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
static const char *dso_needed_name(const ld_ctx_t *ctx, size_t i) {
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
static int note_dso_names(ld_ctx_t *ctx) {
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

            if (name != NULL && name[0] != '\0' && elf_symbol_shndx(sym) == SHN_UNDEF &&
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

static int dso_find_default_version_export(const ld_ctx_t *ctx, const char *path, const char *base, size_t base_len,
                                           char **out_ver_name) {
    elfobj_t *obj = NULL;
    verdef_table_t defs;
    char *fallback = NULL;
    size_t i;
    int found = 0;

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
            continue;
        }
        ver_name = verdef_lookup(&defs, sym_ver);
        if (ver_name == NULL || ver_name[0] == '\0') {
            continue;
        }
        dup = xstrdup(ver_name);
        if (dup == NULL) {
            free(fallback);
            verdef_table_free(&defs);
            elf_close(obj);
            return -1;
        }
        if (!hidden) {
            free(fallback);
            fallback = NULL;
            *out_ver_name = dup;
            found = 1;
            break;
        }
        if (fallback == NULL) {
            fallback = dup;
        } else {
            free(dup);
        }
    }
    if (!found && fallback != NULL) {
        *out_ver_name = fallback;
        fallback = NULL;
        found = 1;
    }
    free(fallback);
    verdef_table_free(&defs);
    elf_close(obj);
    return found;
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

static int build_gnu_verdef_data(const dyn_ver_plan_t *plan, elfobj_endian_t endian, uint8_t **out_buf, size_t *out_sz) {
    uint8_t *buf;
    size_t i;
    size_t off;

    if (out_buf == NULL || out_sz == NULL || plan == NULL || plan->def_count == 0) {
        return -1;
    }
    if (plan->def_count > SIZE_MAX / 28) {
        return -1;
    }
    *out_sz = plan->def_count * 28;
    buf = (uint8_t *)calloc(1, *out_sz);
    if (buf == NULL) {
        return -1;
    }
    off = 0;
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

static int plan_symbol_version_sections(ld_ctx_t *ctx, elfobj_t *out, uint8_t **dynstr_buf, size_t *dynstr_len,
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
            if (provider == NULL && ctx->dso_inputs.count != 0) {
                const char *path = ctx->dso_inputs.items[0];
                const char *leaf = path != NULL ? strrchr(path, '/') : NULL;
                provider = leaf != NULL ? leaf + 1 : path;
            }
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
        if (build_gnu_verdef_data(&plan, endian, &verdef_data, &verdef_sz) != 0) {
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
        *out_verdef_count = plan.def_count;
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

static int dynbuf_append(uint8_t **buf, size_t *len, size_t *cap, const void *src, size_t n) {
    uint8_t *next;
    size_t ncap;

    if (buf == NULL || len == NULL || cap == NULL) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    if (*len > SIZE_MAX - n) {
        return -1;
    }
    if (*len + n > *cap) {
        ncap = *cap == 0 ? 64 : *cap;
        while (ncap < *len + n) {
            if (ncap > SIZE_MAX / 2) {
                ncap = *len + n;
                break;
            }
            ncap *= 2;
        }
        next = (uint8_t *)realloc(*buf, ncap);
        if (next == NULL) {
            return -1;
        }
        *buf = next;
        *cap = ncap;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return 0;
}

static int dynstr_append_cstr(uint8_t **buf, size_t *len, size_t *cap, const char *name, uint32_t *out_off) {
    size_t n;
    size_t off;

    if (buf == NULL || len == NULL || cap == NULL || out_off == NULL || name == NULL) {
        return -1;
    }
    off = *len;
    if (off > UINT32_MAX) {
        return -1;
    }
    n = strlen(name) + 1;
    if (dynbuf_append(buf, len, cap, name, n) != 0) {
        return -1;
    }
    *out_off = (uint32_t)off;
    return 0;
}

static int dynsym_should_export(const ld_ctx_t *ctx, const elfobj_t *out, const elf_symbol_t *sym) {
    uint8_t bind;
    uint8_t vis;
    uint16_t shndx;

    if (ctx == NULL || out == NULL || sym == NULL) {
        return 0;
    }
    if (elf_symbol_name(sym) == NULL || elf_symbol_name(sym)[0] == '\0') {
        return 0;
    }
    bind = elf_symbol_bind(sym);
    if (bind != STB_GLOBAL && bind != STB_WEAK) {
        return 0;
    }
    vis = elf_symbol_visibility(sym);
    if (vis != STV_DEFAULT && vis != STV_PROTECTED) {
        return 0;
    }
    shndx = elf_symbol_shndx(sym);
    if (elf_type(out) == ET_DYN) {
        return 1;
    }
    if (shndx == SHN_UNDEF) {
        return 1;
    }
    /* The executable's copy of a shared object's variable is the variable,
     * and the shared object has to be able to find it. */
    if (symbol_is_copied(&ctx->dyn_imports, sym)) {
        return 1;
    }
    /* What a shared object of the link refers to and the program has. */
    if (symset_contains(&ctx->dso_wants, elf_symbol_name(sym))) {
        return 1;
    }
    return ctx->export_dynamic ? 1 : 0;
}

static int dynamic_append_entry(uint8_t **buf, size_t *len, size_t *cap, elfobj_class_t cls,
                                elfobj_endian_t endian, int64_t tag, uint64_t value) {
    uint8_t entry[16];
    size_t entsz;

    if (cls == ELFOBJ_CLASS_64) {
        entsz = 16;
        write_u64_endian(entry + 0, endian, (uint64_t)tag);
        write_u64_endian(entry + 8, endian, value);
    } else {
        entsz = 8;
        write_u32_endian(entry + 0, endian, (uint32_t)tag);
        write_u32_endian(entry + 4, endian, (uint32_t)value);
    }
    return dynbuf_append(buf, len, cap, entry, entsz);
}

static uint32_t dynsym_name_off_at(const uint8_t *dynsym, size_t dynsym_len, size_t entsz,
                                   elfobj_endian_t endian, size_t index) {
    size_t off = index * entsz;
    if (dynsym == NULL || entsz == 0 || off > dynsym_len || dynsym_len - off < entsz) {
        return 0;
    }
    return read_u32_endian(dynsym + off, endian);
}

static uint8_t *build_sysv_hash_section(const uint8_t *dynsym, size_t dynsym_len,
                                        const uint8_t *dynstr, size_t dynstr_len,
                                        size_t entsz, elfobj_endian_t endian, size_t *out_sz) {
    size_t nsyms;
    size_t nbucket;
    size_t nchain;
    uint32_t *buckets = NULL;
    uint32_t *chains = NULL;
    uint8_t *buf = NULL;
    size_t i;
    size_t j;

    if (out_sz == NULL || entsz == 0 || (dynsym_len % entsz) != 0) {
        return NULL;
    }
    nsyms = dynsym_len / entsz;
    if (nsyms > SIZE_MAX / 8 - 1) {
        return NULL;
    }
    nbucket = nsyms > 1 ? nsyms - 1 : 1;
    nchain = nsyms;
    buckets = (uint32_t *)calloc(nbucket, sizeof(*buckets));
    chains = (uint32_t *)calloc(nchain, sizeof(*chains));
    if (buckets == NULL || chains == NULL) {
        free(buckets);
        free(chains);
        return NULL;
    }

    for (i = 1; i < nsyms; ++i) {
        uint32_t noff = dynsym_name_off_at(dynsym, dynsym_len, entsz, endian, i);
        const char *name = (noff < dynstr_len) ? (const char *)(dynstr + noff) : "";
        uint32_t h = elf_hash_sysv(name);
        size_t b = (size_t)(h % (uint32_t)nbucket);

        if (buckets[b] == 0) {
            buckets[b] = (uint32_t)i;
        } else {
            j = buckets[b];
            while (j < nchain && chains[j] != 0) {
                j = chains[j];
            }
            if (j < nchain) {
                chains[j] = (uint32_t)i;
            }
        }
    }

    *out_sz = (2 + nbucket + nchain) * 4;
    buf = (uint8_t *)malloc(*out_sz);
    if (buf == NULL) {
        free(buckets);
        free(chains);
        return NULL;
    }
    write_u32_endian(buf + 0, endian, (uint32_t)nbucket);
    write_u32_endian(buf + 4, endian, (uint32_t)nchain);
    for (i = 0; i < nbucket; ++i) {
        write_u32_endian(buf + 8 + (i * 4), endian, buckets[i]);
    }
    for (i = 0; i < nchain; ++i) {
        write_u32_endian(buf + 8 + (nbucket * 4) + (i * 4), endian, chains[i]);
    }
    free(buckets);
    free(chains);
    return buf;
}

static uint8_t *build_gnu_hash_section(const uint8_t *dynsym, size_t dynsym_len,
                                       const uint8_t *dynstr, size_t dynstr_len,
                                       size_t entsz, elfobj_class_t cls,
                                       elfobj_endian_t endian, size_t *out_sz) {
    size_t nsyms;
    uint32_t nbuckets;
    uint32_t symoffset = 1;
    uint32_t bloom_size = 1;
    uint32_t bloom_shift = 5;
    size_t chain_count;
    size_t word_sz;
    size_t i;
    uint8_t *buf = NULL;
    size_t off;

    if (out_sz == NULL || entsz == 0 || (dynsym_len % entsz) != 0) {
        return NULL;
    }
    nsyms = dynsym_len / entsz;
    nbuckets = (nsyms > 1) ? 1u : 1u;
    chain_count = (nsyms > symoffset) ? (nsyms - symoffset) : 0;
    word_sz = cls == ELFOBJ_CLASS_64 ? 8 : 4;
    *out_sz = 16 + (bloom_size * word_sz) + ((size_t)nbuckets * 4) + (chain_count * 4);
    buf = (uint8_t *)calloc(1, *out_sz);
    if (buf == NULL) {
        return NULL;
    }

    write_u32_endian(buf + 0, endian, nbuckets);
    write_u32_endian(buf + 4, endian, symoffset);
    write_u32_endian(buf + 8, endian, bloom_size);
    write_u32_endian(buf + 12, endian, bloom_shift);

    off = 16;
    if (chain_count > 0) {
        uint64_t bloom = 0;
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off, endian, 0);
        } else {
            write_u32_endian(buf + off, endian, 0);
        }
        off += word_sz;
        write_u32_endian(buf + off, endian, symoffset);
        off += 4;
        for (i = 0; i < chain_count; ++i) {
            size_t sym_index = symoffset + i;
            uint32_t noff = dynsym_name_off_at(dynsym, dynsym_len, entsz, endian, sym_index);
            const char *name = (noff < dynstr_len) ? (const char *)(dynstr + noff) : "";
            uint32_t h = elf_hash_gnu(name);
            uint32_t chain = h & ~1u;
            if (i + 1 == chain_count) {
                chain |= 1u;
            }
            if (cls == ELFOBJ_CLASS_64) {
                bloom |= (1ull << (h % 64));
                bloom |= (1ull << ((h >> bloom_shift) % 64));
            } else {
                bloom |= (1u << (h % 32));
                bloom |= (1u << ((h >> bloom_shift) % 32));
            }
            write_u32_endian(buf + off + (i * 4), endian, chain);
        }
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + 16, endian, bloom);
        } else {
            write_u32_endian(buf + 16, endian, (uint32_t)bloom);
        }
    } else {
        if (cls == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off, endian, 0);
        } else {
            write_u32_endian(buf + off, endian, 0);
        }
        off += word_sz;
        write_u32_endian(buf + off, endian, 0);
    }
    return buf;
}

static int is_runtime_import_symbol(const elf_symbol_t *sym) {
    uint8_t bind;
    uint8_t vis;

    if (sym == NULL || elf_symbol_name(sym) == NULL || elf_symbol_name(sym)[0] == '\0') {
        return 0;
    }
    if (elf_symbol_shndx(sym) != SHN_UNDEF) {
        return 0;
    }
    bind = elf_symbol_bind(sym);
    if (bind != STB_GLOBAL && bind != STB_WEAK) {
        return 0;
    }
    vis = elf_symbol_visibility(sym);
    if (vis != STV_DEFAULT && vis != STV_PROTECTED) {
        return 0;
    }
    return 1;
}

static int reloc_is_x64_plt_ref(uint32_t type) {
    return type == R_X86_64_PLT32;
}

static int reloc_is_x64_got_ref(uint32_t type) {
    switch (type) {
    case R_X86_64_GOT32:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        return 1;
    default:
        return 0;
    }
}

static int reloc_is_x64_tls_gd_ref(uint32_t type) {
    return type == R_X86_64_TLSGD;
}

static int reloc_is_x64_tls_ie_ref(uint32_t type) {
    return type == R_X86_64_GOTTPOFF;
}

static int reloc_is_x64_runtime_data_ref(uint32_t type) {
    return type == R_X86_64_64;
}

/*
 * A reference that is a call, and so can be sent through the PLT when
 * what it names turns out to be in a shared object.  R_386_PLT32 says so
 * outright.  R_386_PC32 is what a compiler not told to make
 * position-independent code writes for every call; on i386 nothing but a
 * branch is pc-relative, so against an import it is one too.
 */
static int reloc_is_i386_plt_ref(uint32_t type) {
    return type == R_386_PLT32 || type == R_386_PC32;
}

static int reloc_is_i386_got_ref(uint32_t type) {
    switch (type) {
    case R_386_GOT32:
    case R_386_GOT32X:
        return 1;
    default:
        return 0;
    }
}

static int reloc_is_i386_tls_gd_ref(uint32_t type) {
    return type == R_386_TLS_GD;
}

static int reloc_is_i386_tls_ie_ref(uint32_t type) {
    return type == R_386_TLS_IE || type == R_386_TLS_GOTIE;
}

static int reloc_is_i386_runtime_data_ref(uint32_t type) {
    return type == R_386_32;
}

static int symbol_needs_runtime_relative_reloc(const elf_symbol_t *sym) {
    uint16_t shndx;

    if (sym == NULL) {
        return 0;
    }
    shndx = elf_symbol_shndx(sym);
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        return 0;
    }
    return 1;
}

static int resolve_runtime_relative_addend(elfobj_t *obj, const elf_symbol_t *sym, int64_t addend,
                                           uint64_t *out_addend) {
    uint64_t sym_addr;

    if (out_addend == NULL) {
        return -1;
    }
    if (resolve_symbol_addr(obj, sym, 1, &sym_addr, NULL) != 0) {
        return -1;
    }
    if (addend >= 0) {
        uint64_t uadd = (uint64_t)addend;
        if (sym_addr > UINT64_MAX - uadd) {
            return -1;
        }
        *out_addend = sym_addr + uadd;
    } else {
        uint64_t neg = (uint64_t)(-(addend + 1)) + 1u;
        if (sym_addr < neg) {
            return -1;
        }
        *out_addend = sym_addr - neg;
    }
    return 0;
}

/* Whether an import is one whose address is its PLT entry. */
static int import_is_canonical(const dyn_import_vec_t *imports, const elf_symbol_t *sym) {
    int idx = sym != NULL ? dyn_import_find(imports, elf_symbol_name(sym)) : -1;

    return idx >= 0 && imports->items[idx].canonical;
}

static size_t count_runtime_data_import_relocs_x64(elfobj_t *out, const dyn_import_vec_t *imports) {
    size_t i;
    size_t n = 0;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (reloc_is_x64_runtime_data_ref(elf_reloc_type(rel)) &&
                ((is_runtime_import_symbol(sym) && !import_is_canonical(imports, sym)) ||
                 (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)))) {
                n++;
            }
        }
    }
    return n;
}

static size_t count_runtime_data_import_relocs_i386(elfobj_t *out, const dyn_import_vec_t *imports) {
    size_t i;
    size_t n = 0;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (reloc_is_i386_runtime_data_ref(elf_reloc_type(rel)) &&
                ((is_runtime_import_symbol(sym) && !import_is_canonical(imports, sym)) ||
                 (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)))) {
                n++;
            }
        }
    }
    return n;
}

/* How a shared object of the link defines a symbol. */
typedef struct {
    int type;                   /* STT_* */
    uint64_t size;
    uint64_t value;
    uint16_t shndx;
    size_t dso;                 /* which of ctx->dso_inputs */
} dso_def_t;

/* The definition of `name` among the shared objects of the link, the first
 * there is.  0, or -1 if none defines it. */
static int dso_find_definition(const ld_ctx_t *ctx, const char *name, dso_def_t *def) {
    size_t d, i;

    for (d = 0; ctx != NULL && name != NULL && d < ctx->dso_inputs.count; ++d) {
        elfobj_t *obj = NULL;
        int found = 0;

        if (elf_open(ctx->dso_inputs.items[d], &obj) != ELF_OK) {
            continue;
        }
        for (i = 0; !found && i < elf_symbol_count(obj); ++i) {
            const elf_symbol_t *sym = elf_symbol_at(obj, i);
            const char *sname = sym != NULL ? elf_symbol_name(sym) : NULL;

            if (sname != NULL && elf_symbol_shndx(sym) != SHN_UNDEF && strcmp(sname, name) == 0 &&
                (elf_symbol_bind(sym) == STB_GLOBAL || elf_symbol_bind(sym) == STB_WEAK)) {
                def->type = elf_symbol_type(sym);
                def->size = elf_symbol_size(sym);
                def->value = elf_symbol_value(sym);
                def->shndx = elf_symbol_shndx(sym);
                def->dso = d;
                found = 1;
            }
        }
        elf_close(obj);
        if (found) {
            return 0;
        }
    }
    return -1;
}

/*
 * What the shared objects of the link define `name` as (STT_FUNC,
 * STT_OBJECT, ...); -1 if none of them defines it.
 */
static int dso_definition_type(const ld_ctx_t *ctx, const char *name) {
    dso_def_t def;

    return dso_find_definition(ctx, name, &def) == 0 ? def.type : -1;
}

/*
 * What a program that is not position-independent does about the things
 * of shared objects it refers to directly.
 *
 * Such code has the address of what it names built into its instructions,
 * and its instructions cannot be changed when it is loaded.  So what it
 * names has to be somewhere the linker knows now:
 *
 *   - a variable gets a copy in the executable (.dynbss), which becomes
 *     the variable for everyone: the executable defines the symbol, and
 *     an R_*_COPY relocation has the dynamic linker fill the copy from
 *     the shared object's initial value before anything runs.  The other
 *     names the shared object has for the same variable (environ and
 *     __environ) are defined at the copy too, or the library would go on
 *     using its own.  This is done here, before imports are collected:
 *     once defined, the symbol is not an import.
 *
 *   - a function whose address is taken has its PLT entry for an
 *     address.  The symbol stays undefined, with the PLT entry as its
 *     value, which tells the dynamic linker to give that same address to
 *     every other module that asks.  The import is marked `canonical`
 *     when imports are collected.
 *
 * A shared object, and position-independent code anywhere, needs neither:
 * it reaches everything through the GOT.
 */
static int reloc_is_direct_ref(uint16_t machine, uint32_t type, int data) {
    if (machine == EM_386) {
        return type == R_386_32;
    }
    if (machine == EM_X86_64) {
        return type == R_X86_64_64 || type == R_X86_64_32 || type == R_X86_64_32S ||
               (data && type == R_X86_64_PC32);
    }
    return 0;
}

static int symvec_push(elf_symbol_t ***items, size_t *count, size_t *cap, elf_symbol_t *sym) {
    if (*count == *cap) {
        size_t ncap = *cap ? *cap * 2 : 8;
        elf_symbol_t **n = (elf_symbol_t **)realloc(*items, ncap * sizeof(*n));

        if (n == NULL) {
            return -1;
        }
        *items = n;
        *cap = ncap;
    }
    (*items)[(*count)++] = sym;
    return 0;
}

static int plan_copy_relocs(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    uint16_t machine = elf_machine(out);
    size_t si, ri, k;

    if (elf_type(out) != ET_EXEC || ctx->dso_inputs.count == 0) {
        return 0;
    }
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            elf_symbol_t *sym = rel != NULL ? (elf_symbol_t *)elf_reloc_symbol(rel) : NULL;
            elf_section_t *dynbss;
            elfobj_t *dso = NULL;
            uint64_t align, off;
            dso_def_t def;

            if (sym == NULL || !is_runtime_import_symbol(sym) ||
                !reloc_is_direct_ref(machine, elf_reloc_type(rel), 1) ||
                dso_find_definition(ctx, elf_symbol_name(sym), &def) != 0) {
                continue;
            }
            if (def.type == STT_TLS) {
                fprintf(stderr, "ld: %s is thread-local in a shared object and is referred to directly: "
                                "not supported; compile with -fPIC\n", elf_symbol_name(sym));
                return -1;
            }
            if (def.type != STT_OBJECT) {
                continue;
            }
            dynbss = elf_find_section(out, ".dynbss");
            if (dynbss == NULL) {
                dynbss = elf_add_section(out, ".dynbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
                if (dynbss == NULL) {
                    return -1;
                }
            }
            /* As aligned as anything of its size can need to be. */
            for (align = 1; align < 16 && align * 2 <= def.size; align *= 2) {
            }
            off = (elf_section_size(dynbss) + align - 1) & ~(align - 1);
            if (elf_section_align(dynbss) < align && elf_section_set_align(dynbss, align) != ELF_OK) {
                return -1;
            }
            if (set_section_zero_data(dynbss, (size_t)(off + (def.size != 0 ? def.size : 1))) != 0 ||
                elf_symbol_define(sym, dynbss, off) != ELF_OK || elf_symbol_set_type(sym, STT_OBJECT) != ELF_OK ||
                elf_symbol_set_size(sym, def.size) != ELF_OK ||
                symvec_push(&imports->copies, &imports->copy_count, &imports->copy_cap, sym) != 0) {
                return -1;
            }
            /* Its other names. */
            if (elf_open(ctx->dso_inputs.items[def.dso], &dso) != ELF_OK) {
                continue;
            }
            for (k = 0; k < elf_symbol_count(dso); ++k) {
                const elf_symbol_t *other = elf_symbol_at(dso, k);
                const char *oname = other != NULL ? elf_symbol_name(other) : NULL;
                elf_symbol_t *alias;

                if (oname == NULL || oname[0] == '\0' || strcmp(oname, elf_symbol_name(sym)) == 0 ||
                    elf_symbol_shndx(other) != def.shndx || elf_symbol_value(other) != def.value ||
                    elf_symbol_type(other) != STT_OBJECT ||
                    (elf_symbol_bind(other) != STB_GLOBAL && elf_symbol_bind(other) != STB_WEAK)) {
                    continue;
                }
                alias = elf_find_symbol(out, oname);
                if (alias != NULL && elf_symbol_shndx(alias) != SHN_UNDEF) {
                    continue;   /* the program has one of its own by that name */
                }
                if (alias == NULL) {
                    alias = elf_add_symbol(out, oname, 0, 0, elf_symbol_bind(other), STT_OBJECT);
                }
                if (alias == NULL || elf_symbol_define(alias, dynbss, off) != ELF_OK ||
                    elf_symbol_set_type(alias, STT_OBJECT) != ELF_OK || elf_symbol_set_size(alias, def.size) != ELF_OK ||
                    symvec_push(&imports->copy_aliases, &imports->alias_count, &imports->alias_cap, alias) != 0) {
                    elf_close(dso);
                    return -1;
                }
            }
            elf_close(dso);
        }
    }
    return 0;
}

/* Whether this reference, from an executable, takes the address of a
 * function that is in a shared object: the address is its PLT entry. */
static int import_address_is_plt(const ld_ctx_t *ctx, const elfobj_t *out, uint32_t type, const elf_symbol_t *sym) {
    int what;

    if (elf_type(out) != ET_EXEC || !reloc_is_direct_ref(elf_machine(out), type, 0) ||
        !is_runtime_import_symbol(sym)) {
        return 0;
    }
    what = dso_definition_type(ctx, elf_symbol_name(sym));
    return what == STT_FUNC || what == STT_GNU_IFUNC;
}

/* Whether the executable has a copy of this symbol's data. */
static int symbol_is_copied(const dyn_import_vec_t *imports, const elf_symbol_t *sym) {
    size_t i;

    for (i = 0; i < imports->copy_count; ++i) {
        if (imports->copies[i] == sym) {
            return 1;
        }
    }
    for (i = 0; i < imports->alias_count; ++i) {
        if (imports->copy_aliases[i] == sym) {
            return 1;
        }
    }
    return 0;
}

/* The same, where a symbol nothing defines is one of no type. */
static int dso_import_type(const ld_ctx_t *ctx, const char *name) {
    int type = dso_definition_type(ctx, name);

    return type >= 0 ? type : STT_NOTYPE;
}

/*
 * A weak reference that nothing in the link defines, neither an input nor
 * a shared object, is zero, and in an executable it is zero now: the
 * program asks "is this here?" and the answer does not wait for run time.
 * Left as an import it became a relocation for the dynamic linker to
 * apply to the instruction that asks, in memory that cannot be written.
 */
static int settle_undefined_weak(const ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;

    if (elf_type(out) != ET_EXEC || ctx->dso_inputs.count == 0) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(out); ++i) {
        elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;

        if (name == NULL || name[0] == '\0' || elf_symbol_shndx(sym) != SHN_UNDEF ||
            elf_symbol_bind(sym) != STB_WEAK || dso_definition_type(ctx, name) >= 0) {
            continue;
        }
        if (elf_symbol_set_value(sym, 0) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int collect_dynamic_imports_x64(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    size_t i;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;
            dyn_import_t *imp;
            uint32_t type;
            int plt_ref;
            int canonical;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            type = elf_reloc_type(rel);
            plt_ref = reloc_is_x64_plt_ref(type);
            if (!plt_ref && type == R_X86_64_PC32 &&
                (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE)) {
                /*
                 * A pc-relative reference to something in a shared
                 * object.  A reference does not say what it refers to
                 * (the symbol is undefined, of no type), and on x86-64
                 * data is addressed this way as much as functions are
                 * called this way; the shared object says which.  A call
                 * goes through the PLT.  Data would need a copy of it in
                 * the executable, which is not made here, and to send
                 * it through the PLT is to read the PLT's instructions
                 * as the variable.
                 */
                int what = elf_symbol_type(sym) == STT_FUNC ? STT_FUNC : dso_import_type(ctx, elf_symbol_name(sym));

                if (what == STT_OBJECT || what == STT_TLS) {
                    fprintf(stderr,
                            "ld: %s is data in a shared object and is referred to pc-relatively from %s: "
                            "that needs a copy relocation, which is not supported; compile with -fPIC\n",
                            elf_symbol_name(sym), elf_section_name(sec) != NULL ? elf_section_name(sec) : "?");
                    return -1;
                }
                plt_ref = 1;
            }
            canonical = !plt_ref && import_address_is_plt(ctx, out, type, sym);
            plt_ref |= canonical;
            if (!plt_ref && !reloc_is_x64_got_ref(type) &&
                !reloc_is_x64_tls_gd_ref(type) && !reloc_is_x64_tls_ie_ref(type) &&
                !reloc_is_x64_runtime_data_ref(type)) {
                continue;
            }
            imp = dyn_import_get_or_add(imports, elf_symbol_name(sym));
            if (imp == NULL) {
                return -1;
            }
            if (plt_ref) {
                imp->need_plt = 1;
            }
            if (canonical) {
                imp->canonical = 1;
            }
            if (reloc_is_x64_got_ref(type)) {
                imp->need_got = 1;
            }
            if (reloc_is_x64_tls_gd_ref(type)) {
                imp->need_tls_gd = 1;
            }
            if (reloc_is_x64_tls_ie_ref(type)) {
                imp->need_tls_ie = 1;
            }
        }
    }
    return 0;
}

static int collect_dynamic_imports_i386(const ld_ctx_t *ctx, elfobj_t *out, dyn_import_vec_t *imports) {
    size_t i;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);
        size_t ri;
        size_t rc;

        if (sec == NULL) {
            continue;
        }
        rc = elf_section_reloc_count(sec);
        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym;
            dyn_import_t *imp;
            uint32_t type;

            if (rel == NULL) {
                continue;
            }
            sym = elf_reloc_symbol(rel);
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            type = elf_reloc_type(rel);
            if (!reloc_is_i386_plt_ref(type) && !reloc_is_i386_got_ref(type) &&
                !reloc_is_i386_tls_gd_ref(type) && !reloc_is_i386_tls_ie_ref(type) &&
                !reloc_is_i386_runtime_data_ref(type)) {
                continue;
            }
            imp = dyn_import_get_or_add(imports, elf_symbol_name(sym));
            if (imp == NULL) {
                return -1;
            }
            if (reloc_is_i386_plt_ref(type)) {
                imp->need_plt = 1;
            }
            if (import_address_is_plt(ctx, out, type, sym)) {
                imp->need_plt = 1;
                imp->canonical = 1;
            }
            if (reloc_is_i386_got_ref(type)) {
                imp->need_got = 1;
            }
            if (reloc_is_i386_tls_gd_ref(type)) {
                imp->need_tls_gd = 1;
            }
            if (reloc_is_i386_tls_ie_ref(type)) {
                imp->need_tls_ie = 1;
            }
        }
    }
    return 0;
}

static int set_section_zero_data(elf_section_t *sec, size_t sz) {
    uint8_t *buf = NULL;
    int rc = -1;

    if (sec == NULL) {
        return -1;
    }
    if (sz != 0) {
        buf = (uint8_t *)calloc(1, sz);
        if (buf == NULL) {
            return -1;
        }
    }
    if (elf_section_set_data(sec, buf, sz) == ELF_OK) {
        rc = 0;
    }
    free(buf);
    return rc;
}

static int ensure_dynamic_import_sections_x64(elfobj_t *out, const dyn_import_vec_t *imports, size_t extra_dyn_count) {
    size_t i;
    size_t plt_count = 0;
    size_t got_count = 0;
    size_t dyn_count = 0;
    elf_section_t *sec;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            plt_count++;
        }
        if (imports->items[i].need_got) {
            got_count++;
        }
        if (imports->items[i].need_tls_ie) {
            got_count++;
        }
        if (imports->items[i].need_tls_gd) {
            got_count += 2;
        }
    }
    dyn_count = got_count + extra_dyn_count;

    if (plt_count != 0) {
        sec = elf_find_section(out, ".plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 16) != ELF_OK || set_section_zero_data(sec, 16 * (1 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".got.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got.plt", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 8 * (3 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".rela.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rela.plt", SHT_RELA, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 24 * plt_count) != 0) {
            return -1;
        }
    }

    if (got_count != 0) {
        sec = elf_find_section(out, ".got");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 8 * got_count) != 0) {
            return -1;
        }

    }

    if (dyn_count != 0) {
        sec = elf_find_section(out, ".rela.dyn");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rela.dyn", SHT_RELA, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 8) != ELF_OK || set_section_zero_data(sec, 24 * dyn_count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int ensure_dynamic_import_sections_i386(elfobj_t *out, const dyn_import_vec_t *imports, size_t extra_dyn_count) {
    size_t i;
    size_t plt_count = 0;
    size_t got_count = 0;
    size_t dyn_count = 0;
    elf_section_t *sec;

    if (out == NULL || imports == NULL) {
        return -1;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            plt_count++;
        }
        if (imports->items[i].need_got) {
            got_count++;
        }
        if (imports->items[i].need_tls_ie) {
            got_count++;
        }
        if (imports->items[i].need_tls_gd) {
            got_count += 2;
        }
    }
    dyn_count = got_count + extra_dyn_count;

    if (plt_count != 0) {
        sec = elf_find_section(out, ".plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".plt", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 16) != ELF_OK || set_section_zero_data(sec, 16 * (1 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".got.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got.plt", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 4 * (3 + plt_count)) != 0) {
            return -1;
        }

        sec = elf_find_section(out, ".rel.plt");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rel.plt", SHT_REL, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 8 * plt_count) != 0) {
            return -1;
        }
    }

    if (got_count != 0) {
        sec = elf_find_section(out, ".got");
        if (sec == NULL) {
            sec = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 4 * got_count) != 0) {
            return -1;
        }

    }

    if (dyn_count != 0) {
        sec = elf_find_section(out, ".rel.dyn");
        if (sec == NULL) {
            sec = elf_add_section(out, ".rel.dyn", SHT_REL, SHF_ALLOC);
            if (sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(sec, 4) != ELF_OK || set_section_zero_data(sec, 8 * dyn_count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int plan_dynamic_imports(ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;
    size_t plt_slot = 0;
    size_t got_slot = 0;
    size_t extra_dyn_relocs = 0;
    static int trace_imports_env = -1;

    if (trace_imports_env < 0) {
        const char *v = getenv("LD_DEBUG_IMPORTS");
        trace_imports_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    if ((ctx->mode != 64 && ctx->mode != 32) || (elf_type(out) != ET_DYN && ctx->dso_inputs.count == 0)) {
        return 0;
    }
    dyn_import_vec_free(&ctx->dyn_imports);
    if (plan_copy_relocs(ctx, out, &ctx->dyn_imports) != 0) {
        return -1;
    }
    if (ctx->mode == 64) {
        if (collect_dynamic_imports_x64(ctx, out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_x64(out, &ctx->dyn_imports);
    } else {
        if (collect_dynamic_imports_i386(ctx, out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_i386(out, &ctx->dyn_imports);
    }
    extra_dyn_relocs += ctx->dyn_imports.copy_count;
    for (i = 0; i < ctx->dyn_imports.count; ++i) {
        if (ctx->dyn_imports.items[i].need_plt) {
            ctx->dyn_imports.items[i].plt_slot = plt_slot++;
        }
        if (ctx->dyn_imports.items[i].need_got) {
            ctx->dyn_imports.items[i].got_slot = got_slot++;
        }
        if (ctx->dyn_imports.items[i].need_tls_ie) {
            ctx->dyn_imports.items[i].tls_ie_slot = got_slot++;
        }
        if (ctx->dyn_imports.items[i].need_tls_gd) {
            ctx->dyn_imports.items[i].tls_gd_slot = got_slot;
            got_slot += 2;
        }
    }
    if (trace_imports_env) {
        fprintf(stderr, "ld: import-plan: mode=%d imports=%zu extra-dyn=%zu plt=%zu got=%zu\n",
                ctx->mode, ctx->dyn_imports.count, extra_dyn_relocs, plt_slot, got_slot);
        for (i = 0; i < ctx->dyn_imports.count; ++i) {
            const dyn_import_t *imp = &ctx->dyn_imports.items[i];
            fprintf(stderr,
                    "ld: import-plan: %s plt=%d got=%d tls_gd=%d tls_ie=%d slots(plt=%zu got=%zu gd=%zu ie=%zu)\n",
                    imp->name != NULL ? imp->name : "<null>",
                    imp->need_plt, imp->need_got, imp->need_tls_gd, imp->need_tls_ie,
                    imp->plt_slot, imp->got_slot, imp->tls_gd_slot, imp->tls_ie_slot);
        }
    }
    if (ctx->mode == 64) {
        if (ensure_dynamic_import_sections_x64(out, &ctx->dyn_imports, extra_dyn_relocs) != 0) {
            return -1;
        }
    } else {
        if (ensure_dynamic_import_sections_i386(out, &ctx->dyn_imports, extra_dyn_relocs) != 0) {
            return -1;
        }
    }
    return 0;
}

static int dynsym_index_by_name(const elfobj_t *out, const char *name, uint32_t *out_index) {
    const elf_section_t *dynsym;
    const elf_section_t *dynstr;
    const uint8_t *sym_data;
    const uint8_t *str_data;
    size_t sym_sz = 0;
    size_t str_sz = 0;
    size_t entsz;
    size_t i;
    uint32_t nsyms;

    if (out == NULL || name == NULL || out_index == NULL) {
        return -1;
    }
    dynsym = elf_find_section((elfobj_t *)out, ".dynsym");
    dynstr = elf_find_section((elfobj_t *)out, ".dynstr");
    if (dynsym == NULL || dynstr == NULL) {
        return -1;
    }
    sym_data = (const uint8_t *)elf_section_data(dynsym, &sym_sz);
    str_data = (const uint8_t *)elf_section_data(dynstr, &str_sz);
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    if (sym_data == NULL || str_data == NULL || sym_sz < entsz || (sym_sz % entsz) != 0) {
        return -1;
    }
    nsyms = (uint32_t)(sym_sz / entsz);
    for (i = 1; i < nsyms; ++i) {
        uint32_t noff = read_u32_endian(sym_data + (i * entsz), elf_endian(out));
        const char *nm;
        if (noff >= str_sz) {
            continue;
        }
        nm = (const char *)(str_data + noff);
        if (strcmp(nm, name) == 0) {
            *out_index = (uint32_t)i;
            return 0;
        }
    }
    return -1;
}

static int finalize_dynamic_imports_x64(elfobj_t *out, const dyn_import_vec_t *imports) {
    elf_section_t *plt;
    elf_section_t *gotplt;
    elf_section_t *got;
    elf_section_t *rela_plt;
    elf_section_t *rela_dyn;
    const elf_section_t *dynamic;
    uint8_t *plt_buf = NULL;
    uint8_t *gotplt_buf = NULL;
    uint8_t *got_buf = NULL;
    uint8_t *rela_plt_buf = NULL;
    uint8_t *rela_dyn_buf = NULL;
    size_t plt_sz = 0;
    size_t gotplt_sz = 0;
    size_t got_sz = 0;
    size_t rela_plt_sz = 0;
    size_t rela_dyn_sz = 0;
    size_t rela_dyn_base_count = 0;
    size_t runtime_extra_count = 0;
    size_t required_rela_dyn_sz = 0;
    size_t need_plt_count = 0;
    size_t i;
    uint64_t plt_addr;
    uint64_t gotplt_addr;
    uint64_t got_addr = 0;
    uint64_t dynamic_addr = 0;
    elfobj_endian_t e;

    if (out == NULL || imports == NULL) {
        return 0;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            need_plt_count++;
        }
        if (imports->items[i].need_got) {
            rela_dyn_base_count++;
        }
        if (imports->items[i].need_tls_ie) {
            rela_dyn_base_count++;
        }
        if (imports->items[i].need_tls_gd) {
            rela_dyn_base_count += 2;
        }
    }
    runtime_extra_count = count_runtime_data_import_relocs_x64(out, imports) + imports->copy_count;
    required_rela_dyn_sz = (rela_dyn_base_count + runtime_extra_count) * 24;
    plt = elf_find_section(out, ".plt");
    gotplt = elf_find_section(out, ".got.plt");
    got = elf_find_section(out, ".got");
    rela_plt = elf_find_section(out, ".rela.plt");
    rela_dyn = elf_find_section(out, ".rela.dyn");
    dynamic = elf_find_section(out, ".dynamic");
    if (need_plt_count != 0 && (plt == NULL || gotplt == NULL)) {
        return -1;
    }

    plt_addr = plt != NULL ? elf_section_addr(plt) : 0;
    gotplt_addr = gotplt != NULL ? elf_section_addr(gotplt) : 0;
    if (got != NULL) {
        got_addr = elf_section_addr(got);
    }
    if (dynamic != NULL) {
        dynamic_addr = elf_section_addr(dynamic);
    }
    e = elf_endian(out);

    plt_sz = plt != NULL ? elf_section_size(plt) : 0;
    gotplt_sz = gotplt != NULL ? elf_section_size(gotplt) : 0;
    got_sz = got != NULL ? elf_section_size(got) : 0;
    rela_plt_sz = rela_plt != NULL ? elf_section_size(rela_plt) : 0;
    rela_dyn_sz = rela_dyn != NULL ? elf_section_size(rela_dyn) : 0;
    if (rela_dyn_sz < required_rela_dyn_sz) {
        rela_dyn_sz = required_rela_dyn_sz;
    }

    if (plt_sz != 0) {
        plt_buf = (uint8_t *)calloc(1, plt_sz);
        if (plt_buf == NULL) {
            return -1;
        }
    }
    if (gotplt_sz != 0) {
        gotplt_buf = (uint8_t *)calloc(1, gotplt_sz);
        if (gotplt_buf == NULL) {
            free(plt_buf);
            return -1;
        }
    }
    if (got_sz != 0) {
        got_buf = (uint8_t *)calloc(1, got_sz);
        if (got_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            return -1;
        }
    }
    if (rela_plt_sz != 0) {
        rela_plt_buf = (uint8_t *)calloc(1, rela_plt_sz);
        if (rela_plt_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            return -1;
        }
    }
    if (rela_dyn_sz != 0) {
        rela_dyn_buf = (uint8_t *)calloc(1, rela_dyn_sz);
        if (rela_dyn_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            free(rela_plt_buf);
            return -1;
        }
    }

    if (plt_buf != NULL && plt_sz >= 16) {
        int32_t disp;
        /* PLT0: pushq GOT+8(%rip); jmp *GOT+16(%rip); nopl 0(%rax) */
        plt_buf[0] = 0xff;
        plt_buf[1] = 0x35;
        disp = (int32_t)((int64_t)(gotplt_addr + 8) - (int64_t)(plt_addr + 6));
        write_u32_endian(plt_buf + 2, e, (uint32_t)disp);
        plt_buf[6] = 0xff;
        plt_buf[7] = 0x25;
        disp = (int32_t)((int64_t)(gotplt_addr + 16) - (int64_t)(plt_addr + 12));
        write_u32_endian(plt_buf + 8, e, (uint32_t)disp);
        plt_buf[12] = 0x0f;
        plt_buf[13] = 0x1f;
        plt_buf[14] = 0x40;
        plt_buf[15] = 0x00;
    }
    if (gotplt_buf != NULL && gotplt_sz >= 24) {
        write_u64_endian(gotplt_buf + 0, e, dynamic_addr);
        write_u64_endian(gotplt_buf + 8, e, 0);
        write_u64_endian(gotplt_buf + 16, e, 0);
    }

    for (i = 0; i < imports->count; ++i) {
        const dyn_import_t *imp = &imports->items[i];
        uint32_t dynidx = 0;
        if (dynsym_index_by_name(out, imp->name, &dynidx) != 0) {
            /* Its PLT entry and GOT slot would be left zero, to be
             * jumped through at run time. */
            fprintf(stderr, "ld: %s is imported and has no entry in the dynamic symbol table\n",
                    imp->name != NULL ? imp->name : "?");
            goto fail_import;
        }
        if (imp->need_plt) {
            size_t ent = imp->plt_slot;
            uint64_t ent_addr = plt_addr + 16 + (ent * 16);
            uint64_t slot_addr = gotplt_addr + 24 + (ent * 8);
            size_t poff = 16 + (ent * 16);
            size_t roff = ent * 24;
            int32_t disp;

            if (poff + 16 <= plt_sz) {
                plt_buf[poff + 0] = 0xff;
                plt_buf[poff + 1] = 0x25;
                disp = (int32_t)((int64_t)slot_addr - (int64_t)(ent_addr + 6));
                write_u32_endian(plt_buf + poff + 2, e, (uint32_t)disp);
                plt_buf[poff + 6] = 0x68;
                write_u32_endian(plt_buf + poff + 7, e, (uint32_t)ent);
                plt_buf[poff + 11] = 0xe9;
                disp = (int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16));
                write_u32_endian(plt_buf + poff + 12, e, (uint32_t)disp);
            }
            if ((24 + ((ent + 1) * 8)) <= gotplt_sz) {
                write_u64_endian(gotplt_buf + 24 + (ent * 8), e, ent_addr + 6);
            }
            if (rela_plt_buf != NULL && roff + 24 <= rela_plt_sz) {
                write_u64_endian(rela_plt_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_plt_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_JUMP_SLOT);
                write_u64_endian(rela_plt_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_got && got != NULL) {
            size_t ent = imp->got_slot;
            size_t roff = ent * 24;
            uint64_t slot_addr = got_addr + (ent * 8);

            if (rela_dyn_buf != NULL && roff + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_GLOB_DAT);
                write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_tls_ie && got != NULL) {
            size_t ent = imp->tls_ie_slot;
            size_t roff = ent * 24;
            uint64_t slot_addr = got_addr + (ent * 8);

            if (rela_dyn_buf != NULL && roff + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_TPOFF64);
                write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
            }
        }
        if (imp->need_tls_gd && got != NULL) {
            size_t ent = imp->tls_gd_slot;
            size_t roff0 = ent * 24;
            size_t roff1 = (ent + 1) * 24;
            uint64_t slot0 = got_addr + (ent * 8);
            uint64_t slot1 = got_addr + ((ent + 1) * 8);

            if (rela_dyn_buf != NULL && roff0 + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff0 + 0, e, slot0);
                write_u64_endian(rela_dyn_buf + roff0 + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_DTPMOD64);
                write_u64_endian(rela_dyn_buf + roff0 + 16, e, 0);
            }
            if (rela_dyn_buf != NULL && roff1 + 24 <= rela_dyn_sz) {
                write_u64_endian(rela_dyn_buf + roff1 + 0, e, slot1);
                write_u64_endian(rela_dyn_buf + roff1 + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_DTPOFF64);
                write_u64_endian(rela_dyn_buf + roff1 + 16, e, 0);
            }
        }
    }
    {
        size_t si;
        size_t extra_idx = 0;
        for (si = 0; si < elf_section_count(out); ++si) {
            elf_section_t *sec = elf_section_get(out, si);
            size_t rc;
            size_t ri;
            if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                uint32_t type;
                uint32_t dynidx = 0;
                uint64_t slot_addr;
                int64_t addend = 0;
                size_t roff;
                uint64_t off;
                int emit = 0;
                int relative = 0;
                uint64_t relative_addend = 0;

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                type = elf_reloc_type(rel);
                if (!reloc_is_x64_runtime_data_ref(type)) {
                    continue;
                }
                off = elf_reloc_offset(rel);
                slot_addr = elf_section_addr(sec) + off;
                if (elf_reloc_has_addend(rel)) {
                    addend = elf_reloc_addend(rel);
                } else {
                    const uint8_t *sbuf;
                    size_t ssz = 0;
                    sbuf = (const uint8_t *)elf_section_data(sec, &ssz);
                    if (sbuf == NULL || off + 8 > ssz) {
                        continue;
                    }
                    addend = (int64_t)read_u64_endian(sbuf + off, e);
                }
                if (is_runtime_import_symbol(sym) && import_is_canonical(imports, sym)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym)) {
                    if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                        continue;
                    }
                    emit = 1;
                } else if (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)) {
                    if (resolve_runtime_relative_addend(out, sym, addend, &relative_addend) != 0) {
                        continue;
                    }
                    relative = 1;
                    emit = 1;
                }
                if (!emit) {
                    continue;
                }
                roff = (rela_dyn_base_count + extra_idx) * 24;
                extra_idx++;
                if (rela_dyn_buf == NULL || roff + 24 > rela_dyn_sz) {
                    return -1;
                }
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                if (relative) {
                    write_u64_endian(rela_dyn_buf + roff + 8, e, R_X86_64_RELATIVE);
                } else {
                    write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_64);
                }
                write_u64_endian(rela_dyn_buf + roff + 16, e, relative ? relative_addend : (uint64_t)addend);
            }
        }
        /* And the copies: "fill this from the shared object's own". */
        for (si = 0; si < imports->copy_count; ++si) {
            const elf_symbol_t *sym = imports->copies[si];
            size_t roff = (rela_dyn_base_count + extra_idx++) * 24;
            uint64_t addr = 0;
            uint32_t dynidx = 0;

            if (rela_dyn_buf == NULL || roff + 24 > rela_dyn_sz || resolve_symbol_addr(out, sym, 0, &addr, NULL) != 0 ||
                dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                fprintf(stderr, "ld: cannot make the copy relocation for %s\n", elf_symbol_name(sym));
                goto fail_import;
            }
            write_u64_endian(rela_dyn_buf + roff + 0, e, addr);
            write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_COPY);
            write_u64_endian(rela_dyn_buf + roff + 16, e, 0);
        }
    }

    if ((plt != NULL && elf_section_set_data(plt, plt_buf, plt_sz) != ELF_OK) ||
        (gotplt != NULL && elf_section_set_data(gotplt, gotplt_buf, gotplt_sz) != ELF_OK) ||
        (got != NULL && elf_section_set_data(got, got_buf, got_sz) != ELF_OK) ||
        (rela_plt != NULL && elf_section_set_data(rela_plt, rela_plt_buf, rela_plt_sz) != ELF_OK) ||
        (rela_dyn != NULL && elf_section_set_data(rela_dyn, rela_dyn_buf, rela_dyn_sz) != ELF_OK)) {
        free(plt_buf);
        free(gotplt_buf);
        free(got_buf);
        free(rela_plt_buf);
        free(rela_dyn_buf);
        return -1;
    }

    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rela_plt_buf);
    free(rela_dyn_buf);
    return 0;

fail_import:
    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rela_plt_buf);
    free(rela_dyn_buf);
    return -1;
}

static int finalize_dynamic_imports_i386(elfobj_t *out, const dyn_import_vec_t *imports) {
    elf_section_t *plt;
    elf_section_t *gotplt;
    elf_section_t *got;
    elf_section_t *rel_plt;
    elf_section_t *rel_dyn;
    const elf_section_t *dynamic;
    uint8_t *plt_buf = NULL;
    uint8_t *gotplt_buf = NULL;
    uint8_t *got_buf = NULL;
    uint8_t *rel_plt_buf = NULL;
    uint8_t *rel_dyn_buf = NULL;
    size_t plt_sz = 0;
    size_t gotplt_sz = 0;
    size_t got_sz = 0;
    size_t rel_plt_sz = 0;
    size_t rel_dyn_sz = 0;
    size_t rel_dyn_base_count = 0;
    size_t runtime_extra_count = 0;
    size_t required_rel_dyn_sz = 0;
    size_t need_plt_count = 0;
    size_t i;
    uint64_t plt_addr;
    uint64_t gotplt_addr;
    uint64_t got_addr = 0;
    uint64_t dynamic_addr = 0;
    elfobj_endian_t e;
    int plt_pic_mode;

    if (out == NULL || imports == NULL) {
        return 0;
    }
    for (i = 0; i < imports->count; ++i) {
        if (imports->items[i].need_plt) {
            need_plt_count++;
        }
        if (imports->items[i].need_got) {
            rel_dyn_base_count++;
        }
        if (imports->items[i].need_tls_ie) {
            rel_dyn_base_count++;
        }
        if (imports->items[i].need_tls_gd) {
            rel_dyn_base_count += 2;
        }
    }
    runtime_extra_count = count_runtime_data_import_relocs_i386(out, imports) + imports->copy_count;
    required_rel_dyn_sz = (rel_dyn_base_count + runtime_extra_count) * 8;
    plt = elf_find_section(out, ".plt");
    gotplt = elf_find_section(out, ".got.plt");
    got = elf_find_section(out, ".got");
    rel_plt = elf_find_section(out, ".rel.plt");
    rel_dyn = elf_find_section(out, ".rel.dyn");
    dynamic = elf_find_section(out, ".dynamic");
    if (need_plt_count != 0 && (plt == NULL || gotplt == NULL)) {
        return -1;
    }

    plt_addr = plt != NULL ? elf_section_addr(plt) : 0;
    gotplt_addr = gotplt != NULL ? elf_section_addr(gotplt) : 0;
    if (got != NULL) {
        got_addr = elf_section_addr(got);
    }
    if (dynamic != NULL) {
        dynamic_addr = elf_section_addr(dynamic);
    }
    e = elf_endian(out);
    plt_pic_mode = elf_type(out) == ET_DYN ? 1 : 0;

    plt_sz = plt != NULL ? elf_section_size(plt) : 0;
    gotplt_sz = gotplt != NULL ? elf_section_size(gotplt) : 0;
    got_sz = got != NULL ? elf_section_size(got) : 0;
    rel_plt_sz = rel_plt != NULL ? elf_section_size(rel_plt) : 0;
    rel_dyn_sz = rel_dyn != NULL ? elf_section_size(rel_dyn) : 0;
    if (rel_dyn_sz < required_rel_dyn_sz) {
        rel_dyn_sz = required_rel_dyn_sz;
    }

    if (plt_sz != 0) {
        plt_buf = (uint8_t *)calloc(1, plt_sz);
        if (plt_buf == NULL) {
            return -1;
        }
    }
    if (gotplt_sz != 0) {
        gotplt_buf = (uint8_t *)calloc(1, gotplt_sz);
        if (gotplt_buf == NULL) {
            free(plt_buf);
            return -1;
        }
    }
    if (got_sz != 0) {
        got_buf = (uint8_t *)calloc(1, got_sz);
        if (got_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            return -1;
        }
    }
    if (rel_plt_sz != 0) {
        rel_plt_buf = (uint8_t *)calloc(1, rel_plt_sz);
        if (rel_plt_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            return -1;
        }
    }
    if (rel_dyn_sz != 0) {
        rel_dyn_buf = (uint8_t *)calloc(1, rel_dyn_sz);
        if (rel_dyn_buf == NULL) {
            free(plt_buf);
            free(gotplt_buf);
            free(got_buf);
            free(rel_plt_buf);
            return -1;
        }
    }

    if (plt_buf != NULL && plt_sz >= 16) {
        if (plt_pic_mode) {
            /* PIC PLT0: pushl 4(%ebx); jmp *8(%ebx); nop*4 */
            plt_buf[0] = 0xff;
            plt_buf[1] = 0xb3;
            write_u32_endian(plt_buf + 2, e, 4);
            plt_buf[6] = 0xff;
            plt_buf[7] = 0xa3;
            write_u32_endian(plt_buf + 8, e, 8);
            plt_buf[12] = 0x90;
            plt_buf[13] = 0x90;
            plt_buf[14] = 0x90;
            plt_buf[15] = 0x90;
        } else {
            /* Non-PIC PLT0: pushl *GOT+4; jmp *GOT+8; nop*4 */
            plt_buf[0] = 0xff;
            plt_buf[1] = 0x35;
            write_u32_endian(plt_buf + 2, e, (uint32_t)(gotplt_addr + 4));
            plt_buf[6] = 0xff;
            plt_buf[7] = 0x25;
            write_u32_endian(plt_buf + 8, e, (uint32_t)(gotplt_addr + 8));
            plt_buf[12] = 0x90;
            plt_buf[13] = 0x90;
            plt_buf[14] = 0x90;
            plt_buf[15] = 0x90;
        }
    }
    if (gotplt_buf != NULL && gotplt_sz >= 12) {
        write_u32_endian(gotplt_buf + 0, e, (uint32_t)dynamic_addr);
        write_u32_endian(gotplt_buf + 4, e, 0);
        write_u32_endian(gotplt_buf + 8, e, 0);
    }

    for (i = 0; i < imports->count; ++i) {
        const dyn_import_t *imp = &imports->items[i];
        uint32_t dynidx = 0;
        if (dynsym_index_by_name(out, imp->name, &dynidx) != 0) {
            /* Its PLT entry and GOT slot would be left zero, to be
             * jumped through at run time. */
            fprintf(stderr, "ld: %s is imported and has no entry in the dynamic symbol table\n",
                    imp->name != NULL ? imp->name : "?");
            goto fail_import;
        }
        if (imp->need_plt) {
            size_t ent = imp->plt_slot;
            size_t poff = 16 + (ent * 16);
            size_t roff = ent * 8;
            uint64_t ent_addr = plt_addr + 16 + (ent * 16);
            uint64_t slot_addr = gotplt_addr + 12 + (ent * 4);
            int32_t rel;

            if (poff + 16 <= plt_sz) {
                if (plt_pic_mode) {
                    plt_buf[poff + 0] = 0xff;
                    plt_buf[poff + 1] = 0xa3;
                    write_u32_endian(plt_buf + poff + 2, e, (uint32_t)(12 + (ent * 4)));
                } else {
                    plt_buf[poff + 0] = 0xff;
                    plt_buf[poff + 1] = 0x25;
                    write_u32_endian(plt_buf + poff + 2, e, (uint32_t)slot_addr);
                }
                /* What is pushed for the resolver is where this entry's
                 * relocation is in .rel.plt, in bytes (the i386 psABI),
                 * not which entry it is. */
                plt_buf[poff + 6] = 0x68;
                write_u32_endian(plt_buf + poff + 7, e, (uint32_t)roff);
                plt_buf[poff + 11] = 0xe9;
                rel = (int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16));
                write_u32_endian(plt_buf + poff + 12, e, (uint32_t)rel);
            }
            if ((12 + ((ent + 1) * 4)) <= gotplt_sz) {
                write_u32_endian(gotplt_buf + 12 + (ent * 4), e, (uint32_t)(ent_addr + 6));
            }
            if (rel_plt_buf != NULL && roff + 8 <= rel_plt_sz) {
                write_u32_endian(rel_plt_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_plt_buf + roff + 4, e, (dynidx << 8) | R_386_JMP_SLOT);
            }
        }
        if (imp->need_got && got != NULL) {
            size_t ent = imp->got_slot;
            size_t roff = ent * 8;
            uint64_t slot_addr = got_addr + (ent * 4);

            if (rel_dyn_buf != NULL && roff + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_GLOB_DAT);
            }
        }
        if (imp->need_tls_ie && got != NULL) {
            size_t ent = imp->tls_ie_slot;
            size_t roff = ent * 8;
            uint64_t slot_addr = got_addr + (ent * 4);

            if (rel_dyn_buf != NULL && roff + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_TLS_TPOFF32);
            }
        }
        if (imp->need_tls_gd && got != NULL) {
            size_t ent = imp->tls_gd_slot;
            size_t roff0 = ent * 8;
            size_t roff1 = (ent + 1) * 8;
            uint64_t slot0 = got_addr + (ent * 4);
            uint64_t slot1 = got_addr + ((ent + 1) * 4);

            if (rel_dyn_buf != NULL && roff0 + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff0 + 0, e, (uint32_t)slot0);
                write_u32_endian(rel_dyn_buf + roff0 + 4, e, (dynidx << 8) | R_386_TLS_DTPMOD32);
            }
            if (rel_dyn_buf != NULL && roff1 + 8 <= rel_dyn_sz) {
                write_u32_endian(rel_dyn_buf + roff1 + 0, e, (uint32_t)slot1);
                write_u32_endian(rel_dyn_buf + roff1 + 4, e, (dynidx << 8) | R_386_TLS_DTPOFF32);
            }
        }
    }
    {
        size_t si;
        size_t extra_idx = 0;
        for (si = 0; si < elf_section_count(out); ++si) {
            elf_section_t *sec = elf_section_get(out, si);
            size_t rc;
            size_t ri;
            if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                uint32_t type;
                uint32_t dynidx = 0;
                uint64_t slot_addr;
                size_t roff;
                int emit = 0;
                int relative = 0;

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                type = elf_reloc_type(rel);
                if (!reloc_is_i386_runtime_data_ref(type)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym) && import_is_canonical(imports, sym)) {
                    continue;
                }
                if (is_runtime_import_symbol(sym)) {
                    if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                        continue;
                    }
                    emit = 1;
                } else if (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym)) {
                    relative = 1;
                    emit = 1;
                }
                if (!emit) {
                    continue;
                }
                slot_addr = elf_section_addr(sec) + elf_reloc_offset(rel);
                roff = (rel_dyn_base_count + extra_idx) * 8;
                extra_idx++;
                if (rel_dyn_buf == NULL || roff + 8 > rel_dyn_sz) {
                    return -1;
                }
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                if (relative) {
                    write_u32_endian(rel_dyn_buf + roff + 4, e, R_386_RELATIVE);
                } else {
                    write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_32);
                }
            }
        }
        /* And the copies: "fill this from the shared object's own". */
        for (si = 0; si < imports->copy_count; ++si) {
            const elf_symbol_t *sym = imports->copies[si];
            size_t roff = (rel_dyn_base_count + extra_idx++) * 8;
            uint64_t addr = 0;
            uint32_t dynidx = 0;

            if (rel_dyn_buf == NULL || roff + 8 > rel_dyn_sz || resolve_symbol_addr(out, sym, 0, &addr, NULL) != 0 ||
                dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                fprintf(stderr, "ld: cannot make the copy relocation for %s\n", elf_symbol_name(sym));
                goto fail_import;
            }
            write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)addr);
            write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_COPY);
        }
    }

    if ((plt != NULL && elf_section_set_data(plt, plt_buf, plt_sz) != ELF_OK) ||
        (gotplt != NULL && elf_section_set_data(gotplt, gotplt_buf, gotplt_sz) != ELF_OK) ||
        (got != NULL && elf_section_set_data(got, got_buf, got_sz) != ELF_OK) ||
        (rel_plt != NULL && elf_section_set_data(rel_plt, rel_plt_buf, rel_plt_sz) != ELF_OK) ||
        (rel_dyn != NULL && elf_section_set_data(rel_dyn, rel_dyn_buf, rel_dyn_sz) != ELF_OK)) {
        free(plt_buf);
        free(gotplt_buf);
        free(got_buf);
        free(rel_plt_buf);
        free(rel_dyn_buf);
        return -1;
    }

    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rel_plt_buf);
    free(rel_dyn_buf);
    return 0;

fail_import:
    free(plt_buf);
    free(gotplt_buf);
    free(got_buf);
    free(rel_plt_buf);
    free(rel_dyn_buf);
    return -1;
}

/*
 * A text relocation is one the dynamic linker is left to apply to memory
 * that is mapped read-only.  Every object file has relocations in its
 * text, and nearly all are settled here; the ones that are not are the
 * direct references to what is only known at run time: something in a
 * shared object that has neither a copy nor a PLT entry for an address,
 * and, in a shared object or PIE, the address of anything at all.  The
 * name of the first read-only section that has one, or NULL.  It asks
 * what the import plan decided, so it is for use after that is made.
 */
static const char *text_relocation_section(const ld_ctx_t *ctx, elfobj_t *out) {
    uint16_t machine = elf_machine(out);
    size_t si, ri;

    if (elf_type(out) != ET_DYN && ctx->dso_inputs.count == 0) {
        return NULL;
    }
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & (SHF_ALLOC | SHF_WRITE)) != SHF_ALLOC) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym = rel != NULL ? elf_reloc_symbol(rel) : NULL;
            uint32_t type = rel != NULL ? elf_reloc_type(rel) : 0;

            if (rel == NULL ||
                !(machine == EM_X86_64 ? reloc_is_x64_runtime_data_ref(type) : reloc_is_i386_runtime_data_ref(type))) {
                continue;
            }
            if ((is_runtime_import_symbol(sym) && !import_is_canonical(&ctx->dyn_imports, sym)) ||
                (elf_type(out) == ET_DYN && symbol_needs_runtime_relative_reloc(sym))) {
                return elf_section_name(sec) != NULL ? elf_section_name(sec) : "?";
            }
        }
    }
    return NULL;
}

/* The DT_SONAME of the shared object at `path`, to be freed; NULL if it
 * has none or cannot be read. */
static char *dso_soname(const char *path) {
    strvec_t names;
    char *name;

    memset(&names, 0, sizeof(names));
    name = dso_dynamic_strings(path, DT_SONAME, &names) == 0 && names.count != 0 ? xstrdup(names.items[0]) : NULL;
    strvec_free(&names);
    return name;
}

/* The strings the dynamic section of the shared object at `path` gives
 * under `tag` (DT_SONAME, DT_NEEDED), added to `out`.  -1 if the object
 * cannot be read. */
static int dso_dynamic_strings(const char *path, uint64_t want, strvec_t *out) {
    elfobj_t *obj = NULL;
    const elf_section_t *dynamic, *dynstr;
    const uint8_t *d, *s;
    size_t dsz = 0, ssz = 0, entsz, i;
    int rc = 0;

    if (elf_open(path, &obj) != ELF_OK) {
        return -1;
    }
    dynamic = elf_find_section(obj, ".dynamic");
    dynstr = elf_find_section(obj, ".dynstr");
    d = dynamic != NULL ? (const uint8_t *)elf_section_data(dynamic, &dsz) : NULL;
    s = dynstr != NULL ? (const uint8_t *)elf_section_data(dynstr, &ssz) : NULL;
    entsz = elf_class(obj) == ELFOBJ_CLASS_64 ? 16 : 8;
    for (i = 0; d != NULL && s != NULL && i + entsz <= dsz; i += entsz) {
        uint64_t tag = entsz == 16 ? read_u64_endian(d + i, elf_endian(obj)) : read_u32_endian(d + i, elf_endian(obj));
        uint64_t val = entsz == 16 ? read_u64_endian(d + i + 8, elf_endian(obj))
                                   : read_u32_endian(d + i + 4, elf_endian(obj));

        if (tag == DT_NULL) {
            break;
        }
        if (tag == want && val < ssz && memchr(s + val, '\0', ssz - (size_t)val) != NULL && s[val] != '\0' &&
            strvec_push(out, (const char *)s + val) != 0) {
            rc = -1;
            break;
        }
    }
    elf_close(obj);
    return rc;
}

static int plan_dynamic_needed(ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *dynstr;
    elf_section_t *dynsym;
    elf_section_t *versym_sec = NULL;
    elf_section_t *dynamic;
    elf_section_t *hash_sec = NULL;
    elf_section_t *gnu_hash_sec = NULL;
    elf_section_t *init_sec = NULL;
    elf_section_t *fini_sec = NULL;
    elf_section_t *init_array_sec = NULL;
    elf_section_t *fini_array_sec = NULL;
    uint8_t *dynstr_buf = NULL;
    uint8_t *dynsym_buf = NULL;
    uint8_t *versym_buf = NULL;
    uint8_t *dynamic_buf = NULL;
    uint8_t *hash_buf = NULL;
    uint8_t *gnu_hash_buf = NULL;
    size_t dynstr_len = 0;
    size_t dynstr_cap = 0;
    size_t dynsym_len = 0;
    size_t dynsym_cap = 0;
    size_t versym_len = 0;
    size_t versym_cap = 0;
    size_t dynamic_len = 0;
    size_t dynamic_cap = 0;
    size_t hash_sz = 0;
    size_t gnu_hash_sz = 0;
    size_t verdef_count = 0;
    size_t verneed_count = 0;
    int emit_versym = 0;
    size_t i;
    size_t entsz;
    int need_dyn;
    int textrel;
    elf_section_t *gotplt_sec = NULL;
    elf_section_t *rela_plt_sec = NULL;
    elf_section_t *rel_plt_sec = NULL;
    elf_section_t *rela_dyn_sec = NULL;
    elf_section_t *rel_dyn_sec = NULL;

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    need_dyn = (elf_type(out) == ET_DYN) || (ctx->dso_inputs.count != 0) || ctx->export_dynamic;
    if (!need_dyn) {
        return 0;
    }

    dynstr = elf_find_section(out, ".dynstr");
    if (dynstr == NULL) {
        dynstr = elf_add_section(out, ".dynstr", SHT_STRTAB, SHF_ALLOC);
        if (dynstr == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynstr, 1) != ELF_OK) {
        return -1;
    }
    dynsym = elf_find_section(out, ".dynsym");
    if (dynsym == NULL) {
        dynsym = elf_add_section(out, ".dynsym", SHT_DYNSYM, SHF_ALLOC);
        if (dynsym == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynsym, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
        return -1;
    }
    if (ctx->hash_style == LD_HASH_SYSV || ctx->hash_style == LD_HASH_BOTH) {
        hash_sec = elf_find_section(out, ".hash");
        if (hash_sec == NULL) {
            hash_sec = elf_add_section(out, ".hash", SHT_HASH, SHF_ALLOC);
            if (hash_sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(hash_sec, 4) != ELF_OK) {
            return -1;
        }
    }
    if (ctx->hash_style == LD_HASH_GNU || ctx->hash_style == LD_HASH_BOTH) {
        gnu_hash_sec = elf_find_section(out, ".gnu.hash");
        if (gnu_hash_sec == NULL) {
            gnu_hash_sec = elf_add_section(out, ".gnu.hash", SHT_GNU_HASH, SHF_ALLOC);
            if (gnu_hash_sec == NULL) {
                return -1;
            }
        }
        if (elf_section_set_align(gnu_hash_sec, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
            return -1;
        }
    }
    dynamic = elf_find_section(out, ".dynamic");
    if (dynamic == NULL) {
        dynamic = elf_add_section(out, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
        if (dynamic == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(dynamic, elf_class(out) == ELFOBJ_CLASS_64 ? 8 : 4) != ELF_OK) {
        return -1;
    }
    init_sec = elf_find_section(out, ".init");
    fini_sec = elf_find_section(out, ".fini");
    init_array_sec = elf_find_section(out, ".init_array");
    fini_array_sec = elf_find_section(out, ".fini_array");
    gotplt_sec = elf_find_section(out, ".got.plt");
    rela_plt_sec = elf_find_section(out, ".rela.plt");
    rel_plt_sec = elf_find_section(out, ".rel.plt");
    rela_dyn_sec = elf_find_section(out, ".rela.dyn");
    rel_dyn_sec = elf_find_section(out, ".rel.dyn");

    if (dynbuf_append(&dynstr_buf, &dynstr_len, &dynstr_cap, "\0", 1) != 0) {
        free(dynstr_buf);
        return -1;
    }

    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        /* A library is needed by the name it gives itself: what it was
         * found as at link time (libc.so) is a link to it, and need not
         * exist where the program runs. */
        uint32_t off = 0;

        if (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, dso_needed_name(ctx, i), &off) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_NEEDED, off) != 0) {
            free(dynstr_buf);
            free(dynamic_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (ctx->soname != NULL || ctx->rpaths.count != 0) {
        char *runpath = NULL;
        size_t len = 0;
        uint32_t off = 0;
        int bad = 0;

        for (i = 0; i < ctx->rpaths.count; ++i) {
            len += strlen(ctx->rpaths.items[i]) + 1;
        }
        if (len != 0) {
            runpath = (char *)calloc(1, len);
            bad = runpath == NULL;
            for (i = 0; !bad && i < ctx->rpaths.count; ++i) {
                snprintf(runpath + strlen(runpath), len - strlen(runpath), "%s%s", i != 0 ? ":" : "",
                         ctx->rpaths.items[i]);
            }
        }
        bad = bad ||
              (ctx->soname != NULL &&
               (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, ctx->soname, &off) != 0 ||
                dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                     elf_class(out), elf_endian(out), DT_SONAME, off) != 0)) ||
              (runpath != NULL &&
               (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, runpath, &off) != 0 ||
                dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                     elf_class(out), elf_endian(out), DT_RUNPATH, off) != 0));
        free(runpath);
        if (bad) {
            free(dynstr_buf);
            free(dynamic_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (init_sec != NULL && elf_section_size(init_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (fini_sec != NULL && elf_section_size(fini_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (init_array_sec != NULL && elf_section_size(init_array_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT_ARRAY, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_INIT_ARRAYSZ,
                                 elf_section_size(init_array_sec)) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (fini_array_sec != NULL && elf_section_size(fini_array_sec) != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI_ARRAY, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_FINI_ARRAYSZ,
                                 elf_section_size(fini_array_sec)) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (hash_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_HASH, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (gnu_hash_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_GNU_HASH, 0) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    dynsym_buf = (uint8_t *)calloc(1, entsz);
    if (dynsym_buf == NULL) {
        free(dynstr_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    dynsym_len = entsz;
    dynsym_cap = entsz;
    {
        uint8_t v0[2];
        write_u16_endian(v0, elf_endian(out), VER_NDX_LOCAL);
        if (dynbuf_append(&versym_buf, &versym_len, &versym_cap, v0, sizeof(v0)) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *emit_name = NULL;
        char *emit_name_tmp = NULL;
        uint8_t entry[24];
        uint8_t info;
        uint8_t other;
        uint32_t name_off = 0;
        uint16_t shndx;
        uint16_t version;
        uint64_t value;
        uint64_t size;

        if (!dynsym_should_export(ctx, out, sym)) {
            continue;
        }
        emit_name = elf_symbol_name(sym);
        if (emit_name != NULL && elf_symbol_version(sym) > VER_NDX_GLOBAL) {
            const char *base_name;
            size_t base_len;
            const char *ver_name;
            int is_default_name;
            split_symbol_version(emit_name, &base_name, &base_len, &ver_name, &is_default_name);
            if (ver_name != NULL && base_name != NULL && base_len != strlen(emit_name)) {
                emit_name_tmp = (char *)malloc(base_len + 1);
                if (emit_name_tmp == NULL) {
                    free(dynstr_buf);
                    free(dynsym_buf);
                    free(dynamic_buf);
                    free(versym_buf);
                    free(hash_buf);
                    free(gnu_hash_buf);
                    return -1;
                }
                memcpy(emit_name_tmp, base_name, base_len);
                emit_name_tmp[base_len] = '\0';
                emit_name = emit_name_tmp;
            }
        }
        if (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, emit_name, &name_off) != 0) {
            free(emit_name_tmp);
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
        free(emit_name_tmp);

        info = (uint8_t)(((elf_symbol_bind(sym) & 0x0f) << 4) | (elf_symbol_type(sym) & 0x0f));
        other = (uint8_t)(elf_symbol_visibility(sym) & 0x03);
        shndx = elf_symbol_shndx(sym);
        value = elf_symbol_value(sym);
        size = elf_symbol_size(sym);
        memset(entry, 0, sizeof(entry));
        if (elf_class(out) == ELFOBJ_CLASS_64) {
            write_u32_endian(entry + 0, elf_endian(out), name_off);
            entry[4] = info;
            entry[5] = other;
            write_u16_endian(entry + 6, elf_endian(out), shndx);
            write_u64_endian(entry + 8, elf_endian(out), value);
            write_u64_endian(entry + 16, elf_endian(out), size);
        } else {
            write_u32_endian(entry + 0, elf_endian(out), name_off);
            write_u32_endian(entry + 4, elf_endian(out), (uint32_t)value);
            write_u32_endian(entry + 8, elf_endian(out), (uint32_t)size);
            entry[12] = info;
            entry[13] = other;
            write_u16_endian(entry + 14, elf_endian(out), shndx);
        }
        if (dynbuf_append(&dynsym_buf, &dynsym_len, &dynsym_cap, entry, entsz) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
        version = elf_symbol_version(sym);
        if (version == 0) {
            version = VER_NDX_GLOBAL;
        }
        {
            uint8_t vraw[2];
            write_u16_endian(vraw, elf_endian(out), version);
            if (dynbuf_append(&versym_buf, &versym_len, &versym_cap, vraw, sizeof(vraw)) != 0) {
                free(dynstr_buf);
                free(dynsym_buf);
                free(dynamic_buf);
                free(versym_buf);
                free(hash_buf);
                free(gnu_hash_buf);
                return -1;
            }
        }
    }

    if (plan_symbol_version_sections(ctx, out, &dynstr_buf, &dynstr_len, &dynstr_cap,
                                     dynsym_buf, dynsym_len, entsz, versym_buf, versym_len,
                                     &verdef_count, &verneed_count) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    emit_versym = dynsym_len > entsz || verdef_count != 0 || verneed_count != 0;
    if (emit_versym) {
        versym_sec = elf_find_section(out, ".gnu.version");
        if (versym_sec == NULL) {
            versym_sec = elf_add_section(out, ".gnu.version", SHT_GNU_versym, SHF_ALLOC);
            if (versym_sec == NULL) {
                free(dynstr_buf);
                free(dynsym_buf);
                free(dynamic_buf);
                free(versym_buf);
                free(hash_buf);
                free(gnu_hash_buf);
                return -1;
            }
        }
        if (elf_section_set_align(versym_sec, 2) != ELF_OK) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (hash_sec != NULL) {
        hash_buf = build_sysv_hash_section(dynsym_buf, dynsym_len, dynstr_buf, dynstr_len,
                                           entsz, elf_endian(out), &hash_sz);
        if (hash_buf == NULL) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (gnu_hash_sec != NULL) {
        gnu_hash_buf = build_gnu_hash_section(dynsym_buf, dynsym_len, dynstr_buf, dynstr_len,
                                              entsz, elf_class(out), elf_endian(out), &gnu_hash_sz);
        if (gnu_hash_buf == NULL) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            return -1;
        }
    }

    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_STRTAB, 0) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_SYMTAB, 0) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_STRSZ, dynstr_len) != 0 ||
        dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_SYMENT, entsz) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    /*
     * -z now, said the three ways dynamic linkers look for it; and that
     * the dynamic linker will have to write on what is mapped read-only,
     * said the two ways, where the output has such relocations.
     */
    textrel = text_relocation_section(ctx, out) != NULL;
    if ((ctx->z_now &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_BIND_NOW, 0) != 0) ||
        /* DF_1_PIE is how a PIE is told from a shared object, both being
         * ET_DYN. */
        ((ctx->z_now || ctx->pie) &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_FLAGS_1,
                              (ctx->z_now ? DF_1_NOW : 0) | (ctx->pie ? DF_1_PIE : 0)) != 0) ||
        (textrel &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_TEXTREL, 0) != 0) ||
        ((ctx->z_now || textrel) &&
         dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                              elf_class(out), elf_endian(out), DT_FLAGS,
                              (ctx->z_now ? DF_BIND_NOW : 0) | (textrel ? DF_TEXTREL : 0)) != 0)) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_DEBUG, 0) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    if (emit_versym) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERSYM, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (verdef_count != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERDEF, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERDEFNUM, verdef_count) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (verneed_count != 0) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERNEED, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_VERNEEDNUM, verneed_count) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (gotplt_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTGOT, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (rela_plt_sec != NULL || rel_plt_sec != NULL) {
        uint64_t pltrel_type = rela_plt_sec != NULL ? DT_RELA : DT_REL;
        uint64_t pltrel_sz = rela_plt_sec != NULL ? elf_section_size(rela_plt_sec) : elf_section_size(rel_plt_sec);
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTREL, pltrel_type) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_PLTRELSZ, pltrel_sz) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_JMPREL, 0) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }
    if (rela_dyn_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELA, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELASZ, elf_section_size(rela_dyn_sec)) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELAENT,
                                 elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    } else if (rel_dyn_sec != NULL) {
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_REL, 0) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELSZ, elf_section_size(rel_dyn_sec)) != 0 ||
            dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_RELENT,
                                 elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u) != 0) {
            free(dynstr_buf);
            free(dynsym_buf);
            free(dynamic_buf);
            free(versym_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
    }

    if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                             elf_class(out), elf_endian(out), DT_NULL, 0) != 0) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }

    if (elf_section_set_data(dynstr, dynstr_buf, dynstr_len) != ELF_OK ||
        elf_section_set_data(dynsym, dynsym_buf, dynsym_len) != ELF_OK ||
        (emit_versym && elf_section_set_data(versym_sec, versym_buf, versym_len) != ELF_OK) ||
        elf_section_set_data(dynamic, dynamic_buf, dynamic_len) != ELF_OK ||
        (hash_sec != NULL && elf_section_set_data(hash_sec, hash_buf, hash_sz) != ELF_OK) ||
        (gnu_hash_sec != NULL && elf_section_set_data(gnu_hash_sec, gnu_hash_buf, gnu_hash_sz) != ELF_OK)) {
        free(dynstr_buf);
        free(dynsym_buf);
        free(dynamic_buf);
        free(versym_buf);
        free(hash_buf);
        free(gnu_hash_buf);
        return -1;
    }
    free(dynstr_buf);
    free(dynsym_buf);
    free(dynamic_buf);
    free(versym_buf);
    free(hash_buf);
    free(gnu_hash_buf);
    return 0;
}

static int patch_dynamic_tag_values(elfobj_t *out) {
    elf_section_t *dynamic;
    elf_section_t *dynstr;
    elf_section_t *dynsym;
    elf_section_t *hash;
    elf_section_t *gnu_hash;
    elf_section_t *init_sec;
    elf_section_t *fini_sec;
    elf_section_t *init_array_sec;
    elf_section_t *fini_array_sec;
    elf_section_t *versym;
    elf_section_t *verdef;
    elf_section_t *verneed;
    elf_section_t *gotplt;
    elf_section_t *rela_plt;
    elf_section_t *rel_plt;
    elf_section_t *rela_dyn;
    elf_section_t *rel_dyn;
    size_t dyn_sz = 0;
    const uint8_t *dyn_data;
    uint8_t *buf;
    size_t i;
    size_t entsz;
    uint64_t dynstr_addr = 0;
    uint64_t dynsym_addr = 0;
    uint64_t hash_addr = 0;
    uint64_t gnu_hash_addr = 0;
    uint64_t init_addr = 0;
    uint64_t fini_addr = 0;
    uint64_t init_array_addr = 0;
    uint64_t init_array_size = 0;
    uint64_t fini_array_addr = 0;
    uint64_t fini_array_size = 0;
    uint64_t dynstr_size = 0;
    uint64_t dynsym_entsz;
    uint64_t versym_addr = 0;
    uint64_t verdef_addr = 0;
    uint64_t verneed_addr = 0;
    uint64_t gotplt_addr = 0;
    uint64_t jmprel_addr = 0;
    uint64_t jmprel_size = 0;
    uint64_t rela_addr = 0;
    uint64_t rela_size = 0;
    uint64_t rel_addr = 0;
    uint64_t rel_size = 0;

    if (out == NULL) {
        return -1;
    }
    dynamic = elf_find_section(out, ".dynamic");
    if (dynamic == NULL) {
        return 0;
    }
    dynstr = elf_find_section(out, ".dynstr");
    dynsym = elf_find_section(out, ".dynsym");
    hash = elf_find_section(out, ".hash");
    gnu_hash = elf_find_section(out, ".gnu.hash");
    init_sec = elf_find_section(out, ".init");
    fini_sec = elf_find_section(out, ".fini");
    init_array_sec = elf_find_section(out, ".init_array");
    fini_array_sec = elf_find_section(out, ".fini_array");
    versym = elf_find_section(out, ".gnu.version");
    verdef = elf_find_section(out, ".gnu.version_d");
    verneed = elf_find_section(out, ".gnu.version_r");
    gotplt = elf_find_section(out, ".got.plt");
    rela_plt = elf_find_section(out, ".rela.plt");
    rel_plt = elf_find_section(out, ".rel.plt");
    rela_dyn = elf_find_section(out, ".rela.dyn");
    rel_dyn = elf_find_section(out, ".rel.dyn");
    if (dynstr == NULL || dynsym == NULL) {
        return -1;
    }
    dynstr_addr = elf_section_addr(dynstr);
    dynsym_addr = elf_section_addr(dynsym);
    if (hash != NULL) {
        hash_addr = elf_section_addr(hash);
    }
    if (gnu_hash != NULL) {
        gnu_hash_addr = elf_section_addr(gnu_hash);
    }
    if (init_sec != NULL) {
        init_addr = elf_section_addr(init_sec);
    }
    if (fini_sec != NULL) {
        fini_addr = elf_section_addr(fini_sec);
    }
    if (init_array_sec != NULL) {
        init_array_addr = elf_section_addr(init_array_sec);
        init_array_size = elf_section_size(init_array_sec);
    }
    if (fini_array_sec != NULL) {
        fini_array_addr = elf_section_addr(fini_array_sec);
        fini_array_size = elf_section_size(fini_array_sec);
    }
    dynstr_size = elf_section_size(dynstr);
    dynsym_entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 16u;
    if (versym != NULL) {
        versym_addr = elf_section_addr(versym);
    }
    if (verdef != NULL) {
        verdef_addr = elf_section_addr(verdef);
    }
    if (verneed != NULL) {
        verneed_addr = elf_section_addr(verneed);
    }
    if (gotplt != NULL) {
        gotplt_addr = elf_section_addr(gotplt);
    }
    if (rela_plt != NULL) {
        jmprel_addr = elf_section_addr(rela_plt);
        jmprel_size = elf_section_size(rela_plt);
    } else if (rel_plt != NULL) {
        jmprel_addr = elf_section_addr(rel_plt);
        jmprel_size = elf_section_size(rel_plt);
    }
    if (rela_dyn != NULL) {
        rela_addr = elf_section_addr(rela_dyn);
        rela_size = elf_section_size(rela_dyn);
    }
    if (rel_dyn != NULL) {
        rel_addr = elf_section_addr(rel_dyn);
        rel_size = elf_section_size(rel_dyn);
    }

    dyn_data = (const uint8_t *)elf_section_data(dynamic, &dyn_sz);
    if (dyn_sz == 0 || dyn_data == NULL) {
        return -1;
    }
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 16 : 8;
    if ((dyn_sz % entsz) != 0) {
        return -1;
    }
    buf = (uint8_t *)malloc(dyn_sz);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, dyn_data, dyn_sz);

    for (i = 0; i < dyn_sz; i += entsz) {
        int64_t tag;
        uint8_t *p = buf + i;

        if (elf_class(out) == ELFOBJ_CLASS_64) {
            tag = (int64_t)read_u64_endian(p + 0, elf_endian(out));
        } else {
            tag = (int64_t)(int32_t)read_u32_endian(p + 0, elf_endian(out));
        }
        if (tag == DT_STRTAB) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynstr_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynstr_addr);
            }
        } else if (tag == DT_HASH) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), hash_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)hash_addr);
            }
        } else if (tag == DT_GNU_HASH) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), gnu_hash_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)gnu_hash_addr);
            }
        } else if (tag == DT_INIT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_addr);
            }
        } else if (tag == DT_FINI) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_addr);
            }
        } else if (tag == DT_INIT_ARRAY) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_array_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_array_addr);
            }
        } else if (tag == DT_INIT_ARRAYSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), init_array_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)init_array_size);
            }
        } else if (tag == DT_FINI_ARRAY) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_array_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_array_addr);
            }
        } else if (tag == DT_FINI_ARRAYSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), fini_array_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)fini_array_size);
            }
        } else if (tag == DT_SYMTAB) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynsym_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynsym_addr);
            }
        } else if (tag == DT_STRSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynstr_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynstr_size);
            }
        } else if (tag == DT_SYMENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), dynsym_entsz);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)dynsym_entsz);
            }
        } else if (tag == DT_PLTGOT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), gotplt_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)gotplt_addr);
            }
        } else if (tag == DT_VERSYM) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), versym_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)versym_addr);
            }
        } else if (tag == DT_VERDEF) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), verdef_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)verdef_addr);
            }
        } else if (tag == DT_VERNEED) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), verneed_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)verneed_addr);
            }
        } else if (tag == DT_JMPREL) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), jmprel_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)jmprel_addr);
            }
        } else if (tag == DT_PLTRELSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), jmprel_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)jmprel_size);
            }
        } else if (tag == DT_RELA) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rela_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rela_addr);
            }
        } else if (tag == DT_RELASZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rela_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rela_size);
            }
        } else if (tag == DT_RELAENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u);
            } else {
                write_u32_endian(p + 4, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 24u : 12u);
            }
        } else if (tag == DT_REL) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rel_addr);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rel_addr);
            }
        } else if (tag == DT_RELSZ) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), rel_size);
            } else {
                write_u32_endian(p + 4, elf_endian(out), (uint32_t)rel_size);
            }
        } else if (tag == DT_RELENT) {
            if (elf_class(out) == ELFOBJ_CLASS_64) {
                write_u64_endian(p + 8, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u);
            } else {
                write_u32_endian(p + 4, elf_endian(out), elf_class(out) == ELFOBJ_CLASS_64 ? 16u : 8u);
            }
        }
    }

    if (elf_section_set_data(dynamic, buf, dyn_sz) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static int finalize_symbol_values_for_output(elfobj_t *out) {
    size_t i;

    if (out == NULL) {
        return -1;
    }
    if (elf_type(out) != ET_EXEC && elf_type(out) != ET_DYN) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(out); ++i) {
        elf_symbol_t *sym = elf_symbol_at(out, i);
        uint16_t shndx;
        uint64_t value;
        uint64_t sec_addr;

        if (sym == NULL) {
            continue;
        }
        shndx = elf_symbol_shndx(sym);
        if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
            continue;
        }
        if (shndx == 0 || (size_t)(shndx - 1) >= elf_section_count(out)) {
            return -1;
        }
        value = elf_symbol_value(sym);
        sec_addr = elf_section_addr(elf_section_get(out, (size_t)(shndx - 1)));
        if (value > UINT64_MAX - sec_addr) {
            return -1;
        }
        if (elf_symbol_set_value(sym, value + sec_addr) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int patch_dynsym_symbol_values(const ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *dynsym;
    size_t dyn_sz = 0;
    const uint8_t *src;
    uint8_t *buf;
    size_t entsz;
    size_t nslots;
    size_t slot = 1;
    size_t i;

    if (ctx == NULL || out == NULL) {
        return -1;
    }
    dynsym = elf_find_section(out, ".dynsym");
    if (dynsym == NULL) {
        return 0;
    }
    src = (const uint8_t *)elf_section_data(dynsym, &dyn_sz);
    if (src == NULL || dyn_sz == 0) {
        return -1;
    }
    entsz = elf_class(out) == ELFOBJ_CLASS_64 ? 24 : 16;
    if ((dyn_sz % entsz) != 0) {
        return -1;
    }
    nslots = dyn_sz / entsz;
    buf = (uint8_t *)malloc(dyn_sz);
    if (buf == NULL) {
        return -1;
    }
    memcpy(buf, src, dyn_sz);

    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        uint64_t value;
        size_t off;

        if (!dynsym_should_export(ctx, out, sym)) {
            continue;
        }
        if (slot >= nslots) {
            free(buf);
            return -1;
        }
        value = elf_symbol_value(sym);
        if (elf_symbol_shndx(sym) == SHN_UNDEF && import_is_canonical(&ctx->dyn_imports, sym)) {
            /* Undefined, with a value: its PLT entry here is its address
             * for every module. */
            const dyn_import_t *imp = find_planned_import(ctx, elf_symbol_name(sym));
            const elf_section_t *plt = elf_find_section(out, ".plt");

            if (imp == NULL || plt == NULL) {
                free(buf);
                return -1;
            }
            value = elf_section_addr(plt) + 16 + (imp->plt_slot * 16);
        }
        off = slot * entsz;
        if (elf_class(out) == ELFOBJ_CLASS_64) {
            write_u64_endian(buf + off + 8, elf_endian(out), value);
        } else {
            write_u32_endian(buf + off + 4, elf_endian(out), (uint32_t)value);
        }
        slot++;
    }

    if (elf_section_set_data(dynsym, buf, dyn_sz) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static int load_library_script(const char *script_path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                               int whole_archive, int as_needed, int *handled) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    char *text = NULL;
    char *dir = NULL;
    size_t i = 0;
    int loaded_any = 0;
    int paren_depth = 0;
    int as_needed_depth = 0;
    int pending_as_needed = 0;

    if (handled != NULL) {
        *handled = 0;
    }
    if (script_path == NULL) {
        return 0;
    }
    if (read_file(script_path, &buf, &sz) != 0) {
        return -1;
    }
    text = (char *)malloc(sz + 1);
    if (text == NULL) {
        free(buf);
        return -1;
    }
    memcpy(text, buf, sz);
    text[sz] = '\0';
    if (strstr(text, "INPUT") == NULL && strstr(text, "GROUP") == NULL) {
        free(text);
        free(buf);
        return 0;
    }
    if (handled != NULL) {
        *handled = 1;
    }
    dir = path_dirname_dup(script_path);
    if (dir == NULL) {
        free(text);
        free(buf);
        return -1;
    }

    while (i < sz) {
        char tok[1024];
        size_t tn = 0;
        char *resolved = NULL;
        int effective_as_needed;
        int c;

        if (i + 1 < sz && text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < sz && !(text[i] == '*' && text[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < sz) {
                i += 2;
            }
            continue;
        }
        c = (unsigned char)text[i];
        if (isspace(c) || c == ',') {
            i++;
            continue;
        }
        if (c == '(') {
            paren_depth++;
            if (pending_as_needed) {
                as_needed_depth = paren_depth;
                pending_as_needed = 0;
            }
            i++;
            continue;
        }
        if (c == ')') {
            if (as_needed_depth == paren_depth) {
                as_needed_depth = 0;
            }
            if (paren_depth > 0) {
                paren_depth--;
            }
            i++;
            continue;
        }
        while (i < sz && !isspace((unsigned char)text[i]) && text[i] != '(' && text[i] != ')' && text[i] != ',' &&
               tn + 1 < sizeof(tok)) {
            tok[tn++] = text[i++];
        }
        while (i < sz && !isspace((unsigned char)text[i]) && text[i] != '(' && text[i] != ')' && text[i] != ',') {
            i++;
        }
        tok[tn] = '\0';
        if (tok[0] == '\0') {
            i++;
            continue;
        }
        if (strcmp(tok, "INPUT") == 0 || strcmp(tok, "GROUP") == 0 ||
            strcmp(tok, "OUTPUT_FORMAT") == 0 ||
            strcmp(tok, "SEARCH_DIR") == 0) {
            continue;
        }
        if (strcmp(tok, "AS_NEEDED") == 0) {
            pending_as_needed = 1;
            continue;
        }
        if (tok[0] != '/' && strstr(tok, ".so") == NULL && strstr(tok, ".a") == NULL && strstr(tok, ".o") == NULL) {
            continue;
        }
        effective_as_needed = as_needed || as_needed_depth != 0;
        if (tok[0] == '/') {
            resolved = xstrdup(tok);
        } else {
            resolved = path_join(dir, tok);
        }
        if (resolved == NULL) {
            free(dir);
            free(text);
            free(buf);
            return -1;
        }
        if (strstr(tok, ".so") != NULL) {
            int shared_matches = 0;
            int have_shared_match = 0;

            if (effective_as_needed) {
                if (shared_object_matches_unresolved(resolved, ctx, state, &shared_matches) == 0) {
                    have_shared_match = 1;
                    if (!shared_matches) {
                        free(resolved);
                        continue;
                    }
                }
            }
            if (register_dso_provider(ctx, resolved, state) != 0) {
                if (!effective_as_needed || !have_shared_match || shared_matches) {
                    free(resolved);
                    free(dir);
                    free(text);
                    free(buf);
                    return -1;
                }
            } else {
                loaded_any = 1;
            }
        } else {
            if (load_path_input(resolved, ctx, objs, state, whole_archive, 0) != 0) {
                free(resolved);
                free(dir);
                free(text);
                free(buf);
                return -1;
            }
            loaded_any = 1;
        }
        free(resolved);
    }

    free(dir);
    free(text);
    free(buf);
    if (!loaded_any) {
        return -1;
    }
    return 0;
}

static int load_library_input(ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    char *path_so = NULL;
    char *path_a = NULL;
    char *path_a_explicit = NULL;
    int shared_matches = 0;
    int have_shared_match = 0;
    int handled = 0;

    if (in->text != NULL && in->text[0] == ':') {
        char *path_exact = resolve_library_path_exact(ctx, in->text + 1);
        if (path_exact == NULL) {
            fprintf(stderr, "ld: cannot find -l%s\n", in->text);
            return -1;
        }
        if (has_suffix(path_exact, ".so") &&
            load_library_script(path_exact, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
            handled) {
            free(path_exact);
            return 0;
        }
        if (has_suffix(path_exact, ".so") && (ctx->expect_type == ET_DYN || ctx->expect_type == ET_EXEC)) {
            if (in->as_needed &&
                shared_object_matches_unresolved(path_exact, ctx, state, &shared_matches) == 0 &&
                !shared_matches) {
                free(path_exact);
                return 0;
            }
            if (register_dso_provider(ctx, path_exact, state) == 0) {
                free(path_exact);
                return 0;
            }
        }
        if (load_path_input(path_exact, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_exact);
            return -1;
        }
        free(path_exact);
        return 0;
    }

    if (in->lib_mode == LD_LIBMODE_STATIC) {
        path_a = resolve_library_path_suffix(ctx, in->text, ".a");
        if (path_a == NULL) {
            fprintf(stderr, "ld: cannot find -l%s\n", in->text);
            return -1;
        }
        if (load_path_input(path_a, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_a);
            return -1;
        }
        free(path_a);
        return 0;
    }

    path_so = resolve_library_path_suffix_explicit(ctx, in->text, ".so");
    if (path_so == NULL) {
        path_a_explicit = resolve_library_path_suffix_explicit(ctx, in->text, ".a");
        if (path_a_explicit != NULL) {
            if (load_path_input(path_a_explicit, ctx, objs, state, in->whole_archive, 0) != 0) {
                free(path_a_explicit);
                return -1;
            }
            free(path_a_explicit);
            return 0;
        }
        path_so = resolve_library_path_suffix(ctx, in->text, ".so");
    }
    if (path_so != NULL &&
        load_library_script(path_so, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
        handled) {
        free(path_so);
        return 0;
    }
    if (path_so != NULL && (ctx->expect_type == ET_DYN || ctx->expect_type == ET_EXEC)) {
        if (in->as_needed) {
            if (shared_object_matches_unresolved(path_so, ctx, state, &shared_matches) == 0) {
                have_shared_match = 1;
                if (!shared_matches) {
                    free(path_so);
                    return 0;
                }
            }
        }
        if (register_dso_provider(ctx, path_so, state) == 0) {
            free(path_so);
            return 0;
        }
    }
    if (path_so != NULL && load_path_input(path_so, ctx, objs, state, in->whole_archive, 1) == 0) {
        free(path_so);
        return 0;
    }
    if (path_so != NULL && in->as_needed) {
        if (shared_object_matches_unresolved(path_so, ctx, state, &shared_matches) == 0) {
            have_shared_match = 1;
            if (!shared_matches) {
                free(path_so);
                return 0;
            }
        }
    }

    path_a = resolve_library_path_suffix(ctx, in->text, ".a");
    if (path_a != NULL) {
        if (load_library_script(path_a, ctx, objs, state, in->whole_archive, in->as_needed, &handled) == 0 &&
            handled) {
            free(path_so);
            free(path_a);
            return 0;
        }
        if (load_path_input(path_a, ctx, objs, state, in->whole_archive, 0) != 0) {
            free(path_so);
            free(path_a);
            return -1;
        }
        free(path_so);
        free(path_a);
        return 0;
    }

    if (path_so != NULL) {
        if (in->as_needed && have_shared_match && !shared_matches) {
            free(path_so);
            return 0;
        }
        fprintf(stderr,
                "ld: cannot use shared library %s for this link (shared-object linking not implemented)\n",
                path_so);
        free(path_so);
        return -1;
    }

    fprintf(stderr, "ld: cannot find -l%s\n", in->text);
    return -1;
}

static int process_input_once(ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    if (in->kind == LD_INPUT_FILE) {
        return load_path_input(in->text, ctx, objs, state, in->whole_archive, 0);
    }
    if (in->kind == LD_INPUT_LIB) {
        return load_library_input(ctx, in, objs, state);
    }
    return 0;
}

static int load_group_inputs(ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                             size_t begin, size_t end) {
    int progress;

    do {
        size_t before = objs->count;
        size_t i;

        for (i = begin; i < end; ++i) {
            const ld_input_t *in = &ctx->inputs.items[i];
            if (in->kind == LD_INPUT_GROUP_START || in->kind == LD_INPUT_GROUP_END) {
                continue;
            }
            if (process_input_once(ctx, in, objs, state) != 0) {
                return -1;
            }
        }
        progress = objs->count > before;
    } while (progress);

    return 0;
}

static int load_all_inputs(ld_ctx_t *ctx, objvec_t *objs) {
    symstate_t state;
    size_t i;

    memset(&state, 0, sizeof(state));
    for (i = 0; i < ctx->force_undefined.count; ++i) {
        if (symset_add(&state.unresolved, ctx->force_undefined.items[i]) != 0) {
            symstate_free(&state);
            return -1;
        }
    }
    for (i = 0; i < ctx->inputs.count; ++i) {
        const ld_input_t *in = &ctx->inputs.items[i];
        if (in->kind == LD_INPUT_GROUP_START) {
            size_t j;
            int depth = 1;

            for (j = i + 1; j < ctx->inputs.count; ++j) {
                if (ctx->inputs.items[j].kind == LD_INPUT_GROUP_START) {
                    depth++;
                } else if (ctx->inputs.items[j].kind == LD_INPUT_GROUP_END) {
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (depth != 0) {
                fprintf(stderr, "ld: --start-group without matching --end-group\n");
                symstate_free(&state);
                return -1;
            }
            if (load_group_inputs(ctx, objs, &state, i + 1, j) != 0) {
                symstate_free(&state);
                return -1;
            }
            i = j;
            continue;
        }
        if (in->kind == LD_INPUT_GROUP_END) {
            fprintf(stderr, "ld: --end-group without matching --start-group\n");
            symstate_free(&state);
            return -1;
        }
        if (process_input_once(ctx, in, objs, &state) != 0) {
            symstate_free(&state);
            return -1;
        }
    }

    symstate_free(&state);
    return 0;
}

static int trace_symbol_requested(const ld_ctx_t *ctx, const char *name) {
    size_t i;

    if (ctx->trace_symbols.count == 0 || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < ctx->trace_symbols.count; ++i) {
        if (strcmp(ctx->trace_symbols.items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void emit_trace_inputs(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->trace_inputs) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(stderr, "ld: trace: input %s\n", inputs->names[i]);
    }
}

static void emit_trace_symbols(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (ctx->trace_symbols.count == 0) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;

            if (sym == NULL) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (!trace_symbol_requested(ctx, name)) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_UNDEF) {
                fprintf(stderr, "ld: trace-symbol: %s defined in %s\n", name, inputs->names[i]);
            } else {
                fprintf(stderr, "ld: trace-symbol: %s referenced by %s\n", name, inputs->names[i]);
            }
        }
    }
}

static int emit_common_symbol_warnings(ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->warn_common) {
        return 0;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_COMMON) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            if (ld_warn(ctx, "common symbol `%s` in %s", name, inputs->names[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

typedef struct {
    char *name;
    const char *strong_src;
    const char *weak_src;
    const char *common_src;
    uint64_t common_size;
} symrule_entry_t;

typedef struct {
    symrule_entry_t *items;
    size_t count;
    size_t cap;
} symrule_vec_t;

typedef struct {
    char *name;
    const char *source;
} symref_entry_t;

typedef struct {
    symref_entry_t *items;
    size_t count;
    size_t cap;
} symref_map_t;

static void symrule_free(symrule_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static symrule_entry_t *symrule_get(symrule_vec_t *v, const char *name) {
    size_t i;
    symrule_entry_t *next;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i].name, name) == 0) {
            return &v->items[i];
        }
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 128 : v->cap * 2;
        next = (symrule_entry_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        v->items = next;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return NULL;
    }
    v->count++;
    return &v->items[v->count - 1];
}

static int is_i386_hidden_pc_thunk_symbol(const elf_symbol_t *sym, const char *name) {
    if (sym == NULL || name == NULL) {
        return 0;
    }
    if (strncmp(name, "__x86.get_pc_thunk.", 19) != 0) {
        return 0;
    }
    return elf_symbol_visibility(sym) == STV_HIDDEN;
}

static int check_symbol_precedence(ld_ctx_t *ctx, const objvec_t *inputs) {
    symrule_vec_t table;
    size_t i;

    memset(&table, 0, sizeof(table));
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;
            uint16_t shndx;
            symrule_entry_t *entry;

            if (sym == NULL) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            shndx = elf_symbol_shndx(sym);
            if (shndx == SHN_UNDEF) {
                continue;
            }
            entry = symrule_get(&table, name);
            if (entry == NULL) {
                symrule_free(&table);
                return -1;
            }
            if (shndx == SHN_COMMON) {
                uint64_t sz = elf_symbol_size(sym);
                if (entry->strong_src != NULL) {
                    continue;
                }
                if (entry->common_src == NULL || sz > entry->common_size) {
                    entry->common_src = inputs->names[i];
                    entry->common_size = sz;
                }
                continue;
            }
            if (bind == STB_WEAK) {
                if (entry->strong_src == NULL && entry->weak_src == NULL) {
                    entry->weak_src = inputs->names[i];
                }
                continue;
            }
            if (entry->strong_src != NULL && strcmp(entry->strong_src, inputs->names[i]) != 0) {
                if (is_i386_hidden_pc_thunk_symbol(sym, name)) {
                    continue;
                }
                fprintf(stderr,
                        "ld: duplicate strong definition of `%s`: %s and %s\n",
                        name, entry->strong_src, inputs->names[i]);
                symrule_free(&table);
                return -1;
            }
            if (entry->common_src != NULL && ctx->warn_common) {
                if (ld_warn(ctx, "common symbol `%s` overridden by strong definition in %s (common from %s)",
                            name, inputs->names[i], entry->common_src) != 0) {
                    symrule_free(&table);
                    return -1;
                }
            }
            entry->strong_src = inputs->names[i];
        }
    }
    symrule_free(&table);
    return 0;
}

static void symref_map_free(symref_map_t *m) {
    size_t i;
    size_t n;

    if (m == NULL) {
        return;
    }
    if (m->items == NULL || m->cap == 0) {
        m->items = NULL;
        m->count = 0;
        m->cap = 0;
        return;
    }
    n = m->count;
    if (n > m->cap) {
        n = m->cap;
    }
    for (i = 0; i < n; ++i) {
        free(m->items[i].name);
    }
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->cap = 0;
}

static const char *symref_map_get(const symref_map_t *m, const char *name) {
    size_t i;
    size_t n;

    if (m == NULL || name == NULL || m->items == NULL || m->count == 0) {
        return NULL;
    }
    n = m->count;
    if (n > m->cap) {
        n = m->cap;
    }
    for (i = 0; i < n; ++i) {
        if (strcmp(m->items[i].name, name) == 0) {
            return m->items[i].source;
        }
    }
    return NULL;
}

static int symref_map_add(symref_map_t *m, const char *name, const char *source) {
    symref_entry_t *next;

    if (name == NULL || name[0] == '\0' || symref_map_get(m, name) != NULL) {
        return 0;
    }
    if (m->count == m->cap) {
        size_t ncap = m->cap == 0 ? 64 : m->cap * 2;
        next = (symref_entry_t *)realloc(m->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        m->items = next;
        m->cap = ncap;
    }
    m->items[m->count].name = xstrdup(name);
    if (m->items[m->count].name == NULL) {
        return -1;
    }
    m->items[m->count].source = source;
    m->count++;
    return 0;
}

static int collect_undefined_refs(const objvec_t *inputs, symref_map_t *out) {
    size_t i;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL || elf_symbol_shndx(sym) != SHN_UNDEF) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (symref_map_add(out, name, inputs->names[i]) != 0) {
                symref_map_free(out);
                return -1;
            }
        }
    }
    return 0;
}

static const char *find_symbol_source_input(const objvec_t *inputs, const char *sym_name) {
    size_t i;

    if (inputs == NULL || sym_name == NULL || sym_name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        if (obj == NULL) {
            continue;
        }
        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL || elf_symbol_shndx(sym) == SHN_UNDEF) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name != NULL && strcmp(name, sym_name) == 0) {
                return inputs->names[i];
            }
        }
    }
    return NULL;
}

static int ensure_dir_exists(const char *path) {
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (errno != ENOENT) {
        return -1;
    }
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    return 0;
}

static int copy_file_bytes(const char *src, const char *dst) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    FILE *fp;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (read_file(src, &buf, &sz) != 0) {
        return -1;
    }
    fp = fopen(dst, "wb");
    if (fp == NULL) {
        free(buf);
        return -1;
    }
    if (sz != 0 && fwrite(buf, 1, sz, fp) != sz) {
        fclose(fp);
        free(buf);
        return -1;
    }
    fclose(fp);
    free(buf);
    return 0;
}

static int write_reproduce_bundle(const ld_ctx_t *ctx, const objvec_t *inputs) {
    char manifest_path[1024];
    char script_path[1024];
    char script_copy[1024];
    FILE *mf = NULL;
    FILE *sf = NULL;
    size_t i;

    if (ctx == NULL || inputs == NULL || ctx->reproduce_path == NULL || ctx->reproduce_path[0] == '\0') {
        return 0;
    }
    if (ensure_dir_exists(ctx->reproduce_path) != 0) {
        fprintf(stderr, "ld: failed to create --reproduce directory %s: %s\n", ctx->reproduce_path, strerror(errno));
        return -1;
    }
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt", ctx->reproduce_path);
    snprintf(script_path, sizeof(script_path), "%s/repro.sh", ctx->reproduce_path);
    mf = fopen(manifest_path, "w");
    if (mf == NULL) {
        fprintf(stderr, "ld: failed to write --reproduce manifest %s: %s\n", manifest_path, strerror(errno));
        return -1;
    }
    fprintf(mf, "mode=%s\n", ctx->mode == 64 ? "x86_64" : "i386");
    fprintf(mf, "type=%u\n", (unsigned)ctx->expect_type);
    if (ctx->entry_symbol != NULL) {
        fprintf(mf, "entry=%s\n", ctx->entry_symbol);
    }
    if (ctx->script_path != NULL) {
        fprintf(mf, "script=%s\n", ctx->script_path);
    }
    if (ctx->plugin_path != NULL) {
        fprintf(mf, "plugin=%s\n", ctx->plugin_path);
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(mf, "input[%zu]=%s\n", i, inputs->names[i] != NULL ? inputs->names[i] : "<unknown>");
    }
    fclose(mf);

    for (i = 0; i < inputs->count; ++i) {
        char obj_path[1024];
        snprintf(obj_path, sizeof(obj_path), "%s/input_%03zu.o", ctx->reproduce_path, i);
        if (elf_write_file(inputs->objs[i], obj_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write --reproduce object %s\n", obj_path);
            return -1;
        }
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        snprintf(script_copy, sizeof(script_copy), "%s/linker_script.ld", ctx->reproduce_path);
        if (copy_file_bytes(ctx->script_path, script_copy) != 0) {
            fprintf(stderr, "ld: failed to copy linker script into --reproduce bundle\n");
            return -1;
        }
    }

    sf = fopen(script_path, "w");
    if (sf == NULL) {
        fprintf(stderr, "ld: failed to write --reproduce script %s: %s\n", script_path, strerror(errno));
        return -1;
    }
    fprintf(sf, "#!/bin/sh\nset -eu\n");
    fprintf(sf, "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n");
    fprintf(sf, "LD_TOOL=${LD_TOOL:-ld}\n");
    fprintf(sf, "exec \"$LD_TOOL\" -m%s ", ctx->mode == 64 ? "64" : "32");
    if (ctx->expect_type == ET_REL) {
        fprintf(sf, "-r ");
    } else if (ctx->expect_type == ET_DYN) {
        fprintf(sf, "-shared ");
    }
    if (ctx->entry_symbol != NULL && ctx->entry_symbol[0] != '\0') {
        fprintf(sf, "-e '%s' ", ctx->entry_symbol);
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        fprintf(sf, "-T \"$DIR/linker_script.ld\" ");
    }
    if (ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0') {
        size_t pi;
        fprintf(sf, "-plugin '%s' ", ctx->plugin_path);
        for (pi = 0; pi < ctx->plugin_opt_count; ++pi) {
            fprintf(sf, "-plugin-opt '%s' ", ctx->plugin_opts[pi]);
        }
    }
    fprintf(sf, "-o \"$DIR/repro.out\" ");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(sf, "\"$DIR/input_%03zu.o\" ", i);
    }
    fprintf(sf, "\"$@\"\n");
    fclose(sf);
    if (chmod(script_path, 0755) != 0) {
        fprintf(stderr, "ld: failed to mark --reproduce script executable: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int write_map_file(const ld_ctx_t *ctx, const objvec_t *inputs, elfobj_t *out) {
    FILE *fp;
    size_t i;

    if (ctx->map_path == NULL || ctx->map_path[0] == '\0') {
        return 0;
    }
    fp = fopen(ctx->map_path, "w");
    if (fp == NULL) {
        fprintf(stderr, "ld: failed to open map file %s: %s\n", ctx->map_path, strerror(errno));
        return -1;
    }

    fprintf(fp, "Output: %s\n", ctx->out_path);
    fprintf(fp, "Type: %u\n", (unsigned)elf_type(out));
    fprintf(fp, "Class: %s\n", elf_class(out) == ELFOBJ_CLASS_64 ? "ELF64" : "ELF32");
    fprintf(fp, "\nInputs:\n");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(fp, "  %s\n", inputs->names[i]);
    }
    if (ctx->dso_inputs.count > 0) {
        fprintf(fp, "\nDSO Inputs:\n");
        for (i = 0; i < ctx->dso_inputs.count; ++i) {
            fprintf(fp, "  %s\n", ctx->dso_inputs.items[i]);
        }
    }

    fprintf(fp, "\nSections:\n");
    for (i = 0; i < elf_section_count(out); ++i) {
        const elf_section_t *sec = elf_section_get(out, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;

        if (sec == NULL) {
            continue;
        }
        fprintf(fp, "  %-20s addr=0x%llx size=0x%llx flags=0x%llx\n",
                name != NULL ? name : "<unnamed>",
                (unsigned long long)elf_section_addr(sec),
                (unsigned long long)elf_section_size(sec),
                (unsigned long long)elf_section_flags(sec));
    }

    fprintf(fp, "\nProgram Headers:\n");
    for (i = 0; i < (size_t)elf_program_header_count(out); ++i) {
        fprintf(fp, "  [%02zu] type=0x%x flags=0x%x align=0x%llx\n",
                i,
                (unsigned)elf_program_header_type(out, i),
                (unsigned)elf_program_header_flags(out, i),
                (unsigned long long)elf_program_header_align(out, i));
    }

    fprintf(fp, "\nSymbols:\n");
    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name;
        const char *src;

        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_LOCAL || elf_symbol_shndx(sym) == SHN_UNDEF) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        src = find_symbol_source_input(inputs, name);
        fprintf(fp, "  %-28s value=0x%llx size=%llu bind=%u type=%u shndx=%u source=%s\n",
                name,
                (unsigned long long)elf_symbol_value(sym),
                (unsigned long long)elf_symbol_size(sym),
                (unsigned)elf_symbol_bind(sym),
                (unsigned)elf_symbol_type(sym),
                (unsigned)elf_symbol_shndx(sym),
                src != NULL ? src : "<synthetic>");
    }

    fclose(fp);
    return 0;
}

static int apply_defsyms(ld_ctx_t *ctx, elfobj_t *out) {
    size_t i;

    for (i = 0; i < ctx->defsyms.count; ++i) {
        const char *name = ctx->defsyms.items[i].name;
        uint64_t value = ctx->defsyms.items[i].value;
        elf_symbol_t *sym = elf_find_symbol(out, name);

        if (sym == NULL) {
            sym = elf_add_symbol(out, name, value, 0, STB_GLOBAL, STT_NOTYPE);
            if (sym == NULL) {
                fprintf(stderr, "ld: failed to create --defsym symbol `%s`\n", name);
                return -1;
            }
        }
        if (elf_symbol_set_value(sym, value) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
            fprintf(stderr, "ld: failed to apply --defsym `%s`\n", name);
            return -1;
        }
    }
    return 0;
}

static int64_t sign_extend_u64(uint64_t v, int bits) {
    uint64_t m;
    if (bits <= 0 || bits >= 64) {
        return (int64_t)v;
    }
    m = 1ULL << (bits - 1);
    return (int64_t)((v ^ m) - m);
}

static int reloc_addend_is_signed(uint16_t machine, uint32_t type) {
    if (elf_reloc_is_pc_relative_for_machine(machine, type)) {
        return 1;
    }
    switch (machine) {
    case EM_386:
        switch (type) {
        case R_386_GOT32:
        case R_386_GOTOFF:
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
            return 1;
        default:
            return 0;
        }
    case EM_X86_64:
        switch (type) {
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
            return 1;
        default:
            return 0;
        }
    default:
        return 1;
    }
}

static uint64_t read_uint_bytes(const uint8_t *p, int sz, elfobj_endian_t e) {
    uint64_t v = 0;
    int i;
    if (e == ELFOBJ_ENDIAN_BE) {
        for (i = 0; i < sz; ++i) {
            v = (v << 8) | (uint64_t)p[i];
        }
    } else {
        for (i = sz - 1; i >= 0; --i) {
            v = (v << 8) | (uint64_t)p[i];
        }
    }
    return v;
}

static void write_uint_bytes(uint8_t *p, int sz, elfobj_endian_t e, uint64_t v) {
    int i;
    if (e == ELFOBJ_ENDIAN_BE) {
        for (i = sz - 1; i >= 0; --i) {
            p[i] = (uint8_t)(v & 0xffu);
            v >>= 8;
        }
    } else {
        for (i = 0; i < sz; ++i) {
            p[i] = (uint8_t)(v & 0xffu);
            v >>= 8;
        }
    }
}

static int resolve_symbol_addr(elfobj_t *obj, const elf_symbol_t *sym, int allow_undef,
                               uint64_t *out_addr, const char **undef_name) {
    uint16_t shndx;
    uint8_t bind;
    uint64_t value;

    if (sym == NULL) {
        *out_addr = 0;
        return 0;
    }

    shndx = elf_symbol_shndx(sym);
    bind = elf_symbol_bind(sym);
    value = elf_symbol_value(sym);
    if (shndx == SHN_UNDEF) {
        if (allow_undef || bind == STB_WEAK) {
            *out_addr = 0;
            return 0;
        }
        if (undef_name != NULL) {
            *undef_name = elf_symbol_name(sym);
        }
        return -1;
    }
    if (shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        *out_addr = value;
        return 0;
    }
    if (shndx == 0 || (size_t)(shndx - 1) >= elf_section_count(obj)) {
        return -1;
    }
    *out_addr = elf_section_addr(elf_section_get(obj, (size_t)(shndx - 1))) + value;
    return 0;
}

static const dyn_import_t *find_planned_import(const ld_ctx_t *ctx, const char *name) {
    int idx;

    if (ctx == NULL || name == NULL || name[0] == '\0') {
        return NULL;
    }
    idx = dyn_import_find(&ctx->dyn_imports, name);
    if (idx < 0) {
        return NULL;
    }
    return &ctx->dyn_imports.items[idx];
}

static int resolve_symbol_addr_for_reloc(elfobj_t *obj, const ld_ctx_t *ctx, const elf_symbol_t *sym,
                                         int allow_undef, uint32_t type, uint64_t *out_addr,
                                         const char **undef_name) {
    const dyn_import_t *imp;
    const char *name;
    elf_section_t *sec;

    if (sym == NULL || out_addr == NULL) {
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    if (elf_symbol_shndx(sym) != SHN_UNDEF) {
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    name = elf_symbol_name(sym);
    imp = find_planned_import(ctx, name);
    if (imp == NULL) {
        /* The table's own name is nobody's import and no input defines
         * it: it is where the table is, when there is one. */
        if (name != NULL && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0 &&
            (elf_find_section(obj, ".got.plt") != NULL ||
             elf_find_section(obj, ".got") != NULL)) {
            sec = elf_find_section(obj, ".got.plt");
            if (sec == NULL) {
                sec = elf_find_section(obj, ".got");
            }
            *out_addr = elf_section_addr(sec);
            return 0;
        }
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    if (ctx != NULL && ctx->mode == 64) {
        int plt_ref = reloc_is_x64_plt_ref(type);
        if (!plt_ref && type == R_X86_64_PC32 &&
            (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE)) {
            plt_ref = 1;
        }
        plt_ref |= imp->canonical && reloc_is_direct_ref(EM_X86_64, type, 0);
        if (plt_ref && imp->need_plt) {
            sec = elf_find_section(obj, ".plt");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + 16 + (imp->plt_slot * 16);
            return 0;
        }
        if (reloc_is_x64_got_ref(type)) {
            if (imp->need_got) {
                sec = elf_find_section(obj, ".got");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + (imp->got_slot * 8);
                return 0;
            }
            if (imp->need_plt) {
                sec = elf_find_section(obj, ".got.plt");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + 24 + (imp->plt_slot * 8);
                return 0;
            }
        }
        if (reloc_is_x64_tls_gd_ref(type) && imp->need_tls_gd) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_gd_slot * 8);
            return 0;
        }
        if (reloc_is_x64_tls_ie_ref(type) && imp->need_tls_ie) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_ie_slot * 8);
            return 0;
        }
    } else if (ctx != NULL && ctx->mode == 32) {
        if ((reloc_is_i386_plt_ref(type) || (imp->canonical && reloc_is_direct_ref(EM_386, type, 0))) &&
            imp->need_plt) {
            sec = elf_find_section(obj, ".plt");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + 16 + (imp->plt_slot * 16);
            return 0;
        }
        if (reloc_is_i386_got_ref(type)) {
            if (imp->need_got) {
                sec = elf_find_section(obj, ".got");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + (imp->got_slot * 4);
                return 0;
            }
            if (imp->need_plt) {
                sec = elf_find_section(obj, ".got.plt");
                if (sec == NULL) {
                    return -1;
                }
                *out_addr = elf_section_addr(sec) + 12 + (imp->plt_slot * 4);
                return 0;
            }
        }
        if (reloc_is_i386_tls_gd_ref(type) && imp->need_tls_gd) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_gd_slot * 4);
            return 0;
        }
        if (reloc_is_i386_tls_ie_ref(type) && imp->need_tls_ie) {
            sec = elf_find_section(obj, ".got");
            if (sec == NULL) {
                return -1;
            }
            *out_addr = elf_section_addr(sec) + (imp->tls_ie_slot * 4);
            return 0;
        }
    }
    if (name != NULL && strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0) {
        sec = elf_find_section(obj, ".got.plt");
        if (sec == NULL) {
            sec = elf_find_section(obj, ".got");
        }
        if (sec != NULL) {
            *out_addr = elf_section_addr(sec);
            return 0;
        }
    }
    return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
}

static int can_defer_runtime_reloc(const ld_ctx_t *ctx, uint16_t machine, uint32_t type, const elf_symbol_t *sym) {
    const dyn_import_t *imp;

    if (ctx == NULL || sym == NULL || !is_runtime_import_symbol(sym)) {
        return 0;
    }
    if (machine == EM_X86_64 && !reloc_is_x64_runtime_data_ref(type)) {
        return 0;
    }
    if (machine == EM_386 && !reloc_is_i386_runtime_data_ref(type)) {
        return 0;
    }
    if (machine != EM_X86_64 && machine != EM_386) {
        return 0;
    }
    imp = find_planned_import(ctx, elf_symbol_name(sym));
    return imp != NULL && !imp->canonical;
}

static int alloc_section_class(uint64_t flags) {
    if ((flags & SHF_ALLOC) == 0) {
        return -1;
    }
    if ((flags & SHF_EXECINSTR) != 0) {
        return 0; /* RX */
    }
    if ((flags & SHF_WRITE) != 0) {
        return 2; /* RW */
    }
    return 1; /* RO */
}

/*
 * The global offset table of an i386 link, for what is defined in it.
 *
 * Position-independent i386 code finds its data through %ebx, which it
 * loads with the address of the GOT (R_386_GOTPC), and then either adds an
 * offset from there (R_386_GOTOFF) or loads an address out of a slot of
 * the table (R_386_GOT32, R_386_GOT32X).  A dynamic link has had a .got
 * since there were imports to put in it.  A static link had none, and a
 * symbol defined in the output never had a slot in either: the C startup
 * file is position-independent, so that was every link.
 *
 * plan_local_got_i386() runs before addresses are assigned.  If anything
 * is GOT-relative and there is no table, it makes a .got, with a slot for
 * each defined symbol a GOT32-class relocation names; fill_local_got_i386()
 * stores the addresses once they are known.  Where the table is the
 * imports' own, sized by other code, a reference to a defined symbol is
 * instead turned into a direct one when it is applied (the `mov` of a
 * GOT32X becomes a `lea`), which is what the X in GOT32X permits.
 */
static int reloc_is_i386_got_slot(uint32_t type) {
    return type == R_386_GOT32 || type == R_386_GOT32X;
}

static int reloc_is_i386_got_relative(uint32_t type) {
    return type == R_386_GOTPC || type == R_386_GOTOFF ||
           reloc_is_i386_got_slot(type);
}

static uint64_t i386_got_base(elfobj_t *obj) {
    elf_section_t *sec = elf_find_section(obj, ".got.plt");

    if (sec == NULL) {
        sec = elf_find_section(obj, ".got");
    }
    return sec != NULL ? elf_section_addr(sec) : 0;
}

static long local_got_slot_lookup(const ld_ctx_t *ctx, const elf_symbol_t *sym) {
    size_t i;

    for (i = 0; i < ctx->local_got_count; ++i) {
        if (ctx->local_got[i] == sym) {
            return (long)i;
        }
    }
    return -1;
}

/* The slot of `sym` in the table this link made, or -1. */
static long local_got_slot(const ld_ctx_t *ctx, const elf_symbol_t *sym) {
    if (ctx == NULL || !ctx->local_got_owned) {
        return -1;
    }
    return local_got_slot_lookup(ctx, sym);
}

static int plan_local_got_i386(ld_ctx_t *ctx, elfobj_t *out) {
    size_t si, ri;
    int need_base = 0;
    elf_section_t *got;

    if (ctx == NULL || out == NULL || elf_machine(out) != EM_386) {
        return 0;
    }
    ctx->local_got_count = 0;
    ctx->local_got_owned = 0;
    for (si = 0; si < elf_section_count(out); ++si) {
        elf_section_t *sec = elf_section_get(out, si);

        if (sec == NULL || (elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        for (ri = 0; ri < elf_section_reloc_count(sec); ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const elf_symbol_t *sym = elf_reloc_symbol(rel);
            uint32_t type = elf_reloc_type(rel);

            if (!reloc_is_i386_got_relative(type)) {
                continue;
            }
            need_base = 1;
            /*
             * A slot holds an address, and in a shared object or a PIE an
             * address is not known until it is loaded: a slot there would
             * need a relocation of its own for the dynamic linker, which
             * is not made.  Such an output gets the table for a base and
             * no slots, and its references to what it defines are turned
             * into direct ones as they are applied.
             */
            if (elf_type(out) == ET_DYN || !reloc_is_i386_got_slot(type) || sym == NULL ||
                elf_symbol_shndx(sym) == SHN_UNDEF ||
                local_got_slot_lookup(ctx, sym) >= 0) {
                continue;
            }
            if (ctx->local_got_count == ctx->local_got_cap) {
                size_t ncap = ctx->local_got_cap ? ctx->local_got_cap * 2 : 16;
                const elf_symbol_t **n = (const elf_symbol_t **)
                    realloc((void *)ctx->local_got, ncap * sizeof(n[0]));

                if (n == NULL) {
                    return -1;
                }
                ctx->local_got = n;
                ctx->local_got_cap = ncap;
            }
            ctx->local_got[ctx->local_got_count++] = sym;
        }
    }
    if (!need_base || elf_find_section(out, ".got") != NULL ||
        elf_find_section(out, ".got.plt") != NULL) {
        return 0;               /* nothing to do, or the imports' table */
    }
    got = elf_add_section(out, ".got", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (got == NULL || elf_section_set_align(got, 4) != ELF_OK ||
        set_section_zero_data(got, 4 * (ctx->local_got_count ? ctx->local_got_count : 1)) != 0) {
        return -1;
    }
    ctx->local_got_owned = 1;
    return 0;
}

static int fill_local_got_i386(const ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *got;
    uint8_t *buf;
    size_t i, sz;
    int rc;

    if (ctx == NULL || !ctx->local_got_owned || ctx->local_got_count == 0) {
        return 0;
    }
    got = elf_find_section(out, ".got");
    sz = 4 * ctx->local_got_count;
    buf = (uint8_t *)calloc(1, sz);
    if (got == NULL || buf == NULL) {
        free(buf);
        return -1;
    }
    for (i = 0; i < ctx->local_got_count; ++i) {
        uint64_t addr = 0;
        const char *undef = NULL;

        if (resolve_symbol_addr(out, ctx->local_got[i], 0, &addr, &undef) != 0) {
            free(buf);
            return -1;
        }
        write_uint_bytes(buf + 4 * i, 4, elf_endian(out), addr);
    }
    rc = elf_section_set_data(got, buf, sz) == ELF_OK ? 0 : -1;
    free(buf);
    return rc;
}

static int assign_section_addresses(elfobj_t *obj, uint64_t base_vaddr) {
    uint64_t off;
    uint64_t mem_end;
    uint64_t ehsize;
    uint64_t phentsz;
    uint64_t phnum;
    const uint64_t page_align = 0x1000u;
    int last_alloc_class = -1;
    int last_rw_relro = -1;
    size_t i;

    ehsize = elf_class(obj) == ELFOBJ_CLASS_64 ? 64u : 52u;
    phentsz = elf_class(obj) == ELFOBJ_CLASS_64 ? 56u : 32u;
    phnum = (uint64_t)elf_segment_count(obj);
    if (phnum == 0) {
        phnum = (uint64_t)elf_program_header_count(obj);
    }
    if (phnum == 0 && (elf_type(obj) == ET_EXEC || elf_type(obj) == ET_DYN)) {
        int has_dynamic = 0;
        int has_tls = 0;
        int has_interp = 0;
        phnum = 1;
        for (i = 0; i < elf_section_count(obj); ++i) {
            const elf_section_t *sec = elf_section_get(obj, i);
            const char *nm;
            if (sec == NULL) {
                continue;
            }
            nm = elf_section_name(sec);
            if (elf_section_type(sec) == SHT_DYNAMIC) {
                has_dynamic = 1;
            }
            if ((elf_section_flags(sec) & SHF_TLS) != 0) {
                has_tls = 1;
            }
            if (nm != NULL && strcmp(nm, ".interp") == 0) {
                has_interp = 1;
            }
        }
        if (has_dynamic) {
            phnum++;
        }
        if (has_interp) {
            phnum++;
        }
        if (has_tls) {
            phnum++;
        }
    }

    {
        uint64_t ph_total;
        if (!mul_u64_checked(phnum, phentsz, &ph_total) ||
            !add_u64_checked(ehsize, ph_total, &off) ||
            !add_u64_checked(base_vaddr, off, &mem_end)) {
            return -1;
        }
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint64_t align;
        uint64_t size;
        uint64_t flags;
        const char *name;
        uint64_t file_off = 0;
        uint64_t addr = 0;

        if (sec == NULL) {
            continue;
        }
        align = elf_section_align(sec);
        if (align == 0) {
            align = 1;
        }
        size = elf_section_size(sec);
        flags = elf_section_flags(sec);
        name = elf_section_name(sec);

        if ((flags & SHF_ALLOC) != 0) {
            int curr_alloc_class = alloc_section_class(flags);
            if (last_alloc_class != -1 && curr_alloc_class != last_alloc_class) {
                if (!align_up_u64_checked(off, page_align, &off) ||
                    !align_up_u64_checked(mem_end, page_align, &mem_end)) {
                    return -1;
                }
                last_rw_relro = -1;
            } else if (curr_alloc_class == 2) {
                int curr_relro = is_relro_candidate_name(name) ? 1 : 0;
                if (last_rw_relro != -1 && curr_relro != last_rw_relro) {
                    if (!align_up_u64_checked(off, page_align, &off) ||
                        !align_up_u64_checked(mem_end, page_align, &mem_end)) {
                        return -1;
                    }
                }
                last_rw_relro = curr_relro;
            } else {
                last_rw_relro = -1;
            }
            last_alloc_class = curr_alloc_class;
        }

        if (elf_section_type(sec) != SHT_NOBITS) {
            if (!align_up_u64_checked(off, align, &off)) {
                return -1;
            }
            file_off = off;
            if (!add_u64_checked(off, size, &off)) {
                return -1;
            }
        }

        if ((flags & SHF_ALLOC) != 0) {
            if (elf_section_type(sec) == SHT_NOBITS) {
                if (!align_up_u64_checked(mem_end, align, &addr)) {
                    return -1;
                }
            } else {
                if (!add_u64_checked(base_vaddr, file_off, &addr)) {
                    return -1;
                }
                if ((addr & 0xfffu) != (file_off & 0xfffu)) {
                    return -1;
                }
            }
            {
                uint64_t addr_end;
                if (!add_u64_checked(addr, size, &addr_end)) {
                    return -1;
                }
                if (addr_end > mem_end) {
                    mem_end = addr_end;
                }
            }
            if (elf_section_set_addr(sec, addr) != ELF_OK) {
                return -1;
            }
        } else if (elf_section_set_addr(sec, 0) != ELF_OK) {
            return -1;
        }
    }

    return 0;
}

/*
 * What a parsed linker script does to the link.
 *
 * script_output_name()     as the inputs are merged: the output section
 *                          an input section belongs to
 * script_declare_symbols() before symbols are resolved: the names the
 *                          script will define exist
 * script_apply_sections()  once the output's sections exist: discard,
 *                          keep, make, and put in the script's order
 * script_assign_addresses() after the default layout: the location
 *                          counter, the script's symbols, its assertions
 */
static lds_stmt_t *script_find_outsec(lds_script_t *sc, const char *name, size_t *index) {
    size_t i;

    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *st = &sc->stmts.items[i];

        if (st->kind == LDS_ST_OUTSEC && !st->discard && strcmp(st->at.text, name) == 0) {
            if (index != NULL) {
                *index = i;
            }
            return st;
        }
    }
    return NULL;
}

static int script_input_matches(const lds_stmt_t *in, const char *section, const char *file) {
    const char *base = file != NULL && strrchr(file, '/') != NULL ? strrchr(file, '/') + 1 : file;
    size_t i;

    if (file != NULL && !lds_glob(in->at.text, file) && !lds_glob(in->at.text, base)) {
        return 0;
    }
    for (i = 0; file != NULL && i < in->excludes.count; ++i) {
        if (lds_glob(in->excludes.items[i], file) || lds_glob(in->excludes.items[i], base)) {
            return 0;
        }
    }
    for (i = 0; i < in->patterns.count; ++i) {
        if (lds_glob(in->patterns.items[i], section)) {
            return 1;
        }
    }
    return 0;
}

/* The elfobj hook: the first output section of the script that asks for
 * this input section has it; one nothing asks for keeps its own name. */
static const char *script_output_name(const char *section, const char *file, void *user) {
    const lds_script_t *sc = (const lds_script_t *)user;
    size_t i, k;

    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *os = &sc->stmts.items[i];

        if (os->kind != LDS_ST_OUTSEC) {
            continue;
        }
        for (k = 0; k < os->body.count; ++k) {
            if (os->body.items[k].kind == LDS_ST_INPUT &&
                script_input_matches(&os->body.items[k], section, file)) {
                return os->discard ? NULL : os->at.text;
            }
        }
    }
    return section;
}

static int script_declare_in(ld_ctx_t *ctx, elfobj_t *out, lds_stmtvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        lds_stmt_t *st = &v->items[i];

        if (st->kind == LDS_ST_OUTSEC) {
            if (script_declare_in(ctx, out, &st->body) != 0) {
                return -1;
            }
            continue;
        }
        if (st->kind != LDS_ST_ASSIGN || strcmp(st->at.text, ".") == 0) {
            continue;
        }
        st->active = 1;
        if (st->provide) {
            /* PROVIDE defines what is asked for and nobody has. */
            const elf_symbol_t *sym = elf_find_symbol(out, st->at.text);

            st->active = sym != NULL && elf_symbol_shndx(sym) == SHN_UNDEF &&
                         defsymvec_find(&ctx->defsyms, st->at.text) < 0;
        }
        if (st->active && defsymvec_find(&ctx->defsyms, st->at.text) < 0 &&
            defsymvec_set(&ctx->defsyms, st->at.text, 0) != 0) {
            return -1;
        }
        if (!st->active && elf_find_symbol(out, st->at.text) == NULL &&
            defsymvec_find(&ctx->defsyms, st->at.text) < 0 &&
            defsymvec_set(&ctx->script->locals, st->at.text, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static int script_declare_symbols(ld_ctx_t *ctx, elfobj_t *out) {
    return ctx->script != NULL ? script_declare_in(ctx, out, &ctx->script->stmts) : 0;
}

static int script_body_moves_dot(const lds_stmt_t *os) {
    size_t k;

    for (k = 0; k < os->body.count; ++k) {
        if (os->body.items[k].kind == LDS_ST_ASSIGN && strcmp(os->body.items[k].at.text, ".") == 0) {
            return 1;
        }
    }
    return 0;
}

static int script_apply_sections(ld_ctx_t *ctx, elfobj_t *out) {
    lds_script_t *sc = ctx->script;
    elf_section_t **want = NULL;
    size_t *slot = NULL;
    size_t n = 0, i, k, j;
    int rc = -1;

    if (sc == NULL || !sc->has_sections) {
        return 0;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *os = &sc->stmts.items[i];
        int keep = 0;

        if (os->kind != LDS_ST_OUTSEC) {
            continue;
        }
        for (k = 0; k < os->body.count; ++k) {
            const lds_stmt_t *in = &os->body.items[k];

            if (in->kind != LDS_ST_INPUT) {
                continue;
            }
            keep |= in->keep;
            /* What the linker made itself was not there to be left out
             * when the inputs were merged. */
            for (j = 0; os->discard && j < in->patterns.count; ++j) {
                if (apply_script_discard_pattern(out, in->patterns.items[j]) != 0) {
                    fprintf(stderr, "ld: failed to discard sections matching '%s'\n", in->patterns.items[j]);
                    return -1;
                }
            }
        }
        if (os->discard) {
            continue;
        }
        if (elf_find_section(out, os->at.text) == NULL && script_body_moves_dot(os)) {
            /* Room the script reserves and no input fills. */
            if (elf_add_section(out, os->at.text, SHT_NOBITS, SHF_ALLOC | SHF_WRITE) == NULL) {
                return -1;
            }
        }
        if (keep && apply_script_keep_pattern(out, os->at.text) != 0) {
            return -1;
        }
    }

    /*
     * The script's order.  The sections it names change places among
     * themselves: the places they hold stay the ones the default policy
     * gave to them as a set, so that what the script does not mention
     * stays where it was.
     */
    want = (elf_section_t **)calloc(sc->stmts.count + 1, sizeof(*want));
    slot = (size_t *)calloc(sc->stmts.count + 1, sizeof(*slot));
    if (want == NULL || slot == NULL) {
        goto done;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *os = &sc->stmts.items[i];
        elf_section_t *sec;

        if (os->kind != LDS_ST_OUTSEC || os->discard || (sec = elf_find_section(out, os->at.text)) == NULL) {
            continue;
        }
        for (k = 0; k < n && want[k] != sec; ++k) {
        }
        if (k == n) {
            want[n++] = sec;
        }
    }
    for (i = 0, k = 0; i < elf_section_count(out) && k < n; ++i) {
        elf_section_t *sec = elf_section_get(out, i);

        for (j = 0; j < n; ++j) {
            if (want[j] == sec) {
                slot[k++] = i;
                break;
            }
        }
    }
    for (k = 0; k < n; ++k) {
        if (elf_section_get(out, slot[k]) != want[k] && elf_reorder_section(out, want[k], slot[k]) != ELF_OK) {
            fprintf(stderr, "ld: failed to put the sections in the script's order\n");
            goto done;
        }
    }
    rc = 0;
done:
    free(want);
    free(slot);
    return rc;
}

typedef struct {
    ld_ctx_t *ctx;
    elfobj_t *out;
    lds_script_t *sc;
    uint64_t dot;
    uint64_t high;              /* the end of what has an address so far */
    int moved;                  /* a statement set the counter since the last section */
    int final;                  /* values are settled: define, assert */
} lds_walk_t;

static int lw_eval(lds_walk_t *w, const lds_tokvec_t *e, int have_dot, uint64_t dot, const lds_tok_t *at,
                   uint64_t *out) {
    lds_eval_ctx_t ec;

    memset(&ec, 0, sizeof(ec));
    ec.ctx = w->ctx;
    ec.obj = w->out;
    ec.have_dot = have_dot;
    ec.dot = dot;
    if (lds_eval_expr_slice(&ec, e->items, 0, e->count, out) != 0) {
        lds_report_error(NULL, ec.err_tok != NULL ? ec.err_tok : at,
                         ec.err_msg != NULL ? ec.err_msg : "expression evaluation failed");
        return -1;
    }
    return 0;
}

/* Give a symbol the script defines its value in the output.  One defined
 * among the sections belongs to the section its address is in, so that it
 * moves with the program if the program is moved. */
static int lw_define(lds_walk_t *w, const lds_stmt_t *st, uint64_t value, elf_section_t *hint, uint64_t hint_end) {
    elf_symbol_t *sym = elf_find_symbol(w->out, st->at.text);
    elf_section_t *home = NULL;
    size_t i;

    if (sym == NULL) {
        sym = elf_add_symbol(w->out, st->at.text, value, 0, STB_GLOBAL, STT_NOTYPE);
        if (sym == NULL) {
            return -1;
        }
    }
    if (st->where != LDS_IN_TOP) {
        if (hint != NULL && value >= elf_section_addr(hint) && value <= hint_end) {
            home = hint;
        }
        for (i = 0; home == NULL && i < elf_section_count(w->out); ++i) {
            elf_section_t *sec = elf_section_get(w->out, i);

            if (sec != NULL && (elf_section_flags(sec) & SHF_ALLOC) != 0 && value >= elf_section_addr(sec) &&
                value <= elf_section_addr(sec) + elf_section_size(sec)) {
                home = sec;
            }
        }
    }
    if (home != NULL) {
        if (elf_symbol_define(sym, home, value - elf_section_addr(home)) != ELF_OK) {
            return -1;
        }
    } else if (elf_symbol_set_value(sym, value) != ELF_OK || elf_symbol_set_shndx(sym, SHN_ABS) != ELF_OK) {
        return -1;
    }
    if (st->hidden && elf_symbol_set_visibility(sym, STV_HIDDEN) != ELF_OK) {
        return -1;
    }
    return 0;
}

/* Carry out one assignment.  *dot is the counter where the statement
 * stands, and is what an assignment to "." changes. */
static int lw_assign(lds_walk_t *w, const lds_stmt_t *st, int have_dot, uint64_t *dot, elf_section_t *hint,
                     uint64_t hint_end) {
    int is_dot = strcmp(st->at.text, ".") == 0;
    uint64_t v = 0;
    uint64_t cur = 0;

    if (!is_dot && !st->active) {
        /* A PROVIDE that provides nothing still has a value the rest of
         * the script may use, if nothing else defines the name. */
        if (st->op != '=' || defsymvec_find(&w->sc->locals, st->at.text) < 0) {
            return 0;
        }
        return lw_eval(w, &st->expr, have_dot, *dot, &st->at, &v) != 0 ? -1
             : defsymvec_set(&w->sc->locals, st->at.text, v);
    }
    if (lw_eval(w, &st->expr, have_dot, *dot, &st->at, &v) != 0) {
        return -1;
    }
    if (st->op != '=') {
        if (is_dot) {
            cur = *dot;
        } else if (defsymvec_get(&w->ctx->defsyms, st->at.text, &cur) != 0) {
            lds_report_error(NULL, &st->at, "the symbol has no value to change");
            return -1;
        }
        switch (st->op) {
        case '+': v = cur + v; break;
        case '-': v = cur - v; break;
        case '*': v = cur * v; break;
        case '&': v = cur & v; break;
        case '|': v = cur | v; break;
        case '<': v = v < 64 ? cur << v : 0; break;
        case '>': v = v < 64 ? cur >> v : 0; break;
        case '/':
            if (v == 0) {
                lds_report_error(NULL, &st->at, "division by zero");
                return -1;
            }
            v = cur / v;
            break;
        default:
            return -1;
        }
    }
    if (is_dot) {
        if (!have_dot) {
            lds_report_error(NULL, &st->at, "the location counter has no value outside SECTIONS");
            return -1;
        }
        *dot = v;
        return 0;
    }
    if (defsymvec_set(&w->ctx->defsyms, st->at.text, v) != 0) {
        return -1;
    }
    return w->final ? lw_define(w, st, v, hint, hint_end) : 0;
}

static int lw_assert(lds_walk_t *w, const lds_stmt_t *st, int have_dot, uint64_t dot) {
    uint64_t v = 0;

    if (!w->final) {
        return 0;
    }
    if (lw_eval(w, &st->expr, have_dot, dot, &st->at, &v) != 0) {
        return -1;
    }
    if (v == 0) {
        fprintf(stderr, "ld: %s:%zu: %s\n", st->at.path != NULL ? st->at.path : "<script>", st->at.line,
                st->message != NULL ? st->message : "ASSERT failed");
        return -1;
    }
    return 0;
}

/*
 * The body of an output section that is at `start` and `size` long.  What
 * stands before its first input description is at its start, what stands
 * after its last is at its end, and there the counter may move on, which
 * makes the section that much longer.  Where the pieces of the section
 * meet is not kept once they are merged, so a statement that depends on
 * it, between two input descriptions, is refused.
 */
static int lw_body(lds_walk_t *w, lds_stmt_t *os, elf_section_t *sec, uint64_t start, uint64_t size,
                   uint64_t *end_out) {
    size_t first = os->body.count, last = 0, k;
    uint64_t end = start + size;

    for (k = 0; k < os->body.count; ++k) {
        if (os->body.items[k].kind == LDS_ST_INPUT) {
            if (first == os->body.count) {
                first = k;
            }
            last = k;
        }
    }
    for (k = 0; k < os->body.count; ++k) {
        const lds_stmt_t *st = &os->body.items[k];
        int at_start = size != 0 && k < first;
        uint64_t dot = at_start ? start : end;

        if (st->kind == LDS_ST_INPUT) {
            continue;
        }
        if (st->kind == LDS_ST_DATA) {
            lds_report_error(NULL, &st->at, "data statements (BYTE, SHORT, LONG, QUAD) are not supported");
            return -1;
        }
        if (size != 0 && first != os->body.count && k > first && k < last) {
            lds_report_error(NULL, &st->at,
                             "a statement between two input section descriptions is not supported");
            return -1;
        }
        if (st->kind == LDS_ST_ASSERT) {
            if (lw_assert(w, st, 1, dot) != 0) {
                return -1;
            }
            continue;
        }
        if (lw_assign(w, st, 1, &dot, sec, end) != 0) {
            return -1;
        }
        if (at_start && dot != start) {
            lds_report_error(NULL, &st->at,
                             "moving the location counter before a section's contents is not supported");
            return -1;
        }
        if (!at_start) {
            if (dot < end) {
                lds_report_error(NULL, &st->at, "the location counter may not move backwards");
                return -1;
            }
            end = dot;
        }
    }
    os->pad = end - (start + size);
    *end_out = end;
    return 0;
}

/* The statements stmts[lo..hi) that stand between output sections. */
static int lw_run(lds_walk_t *w, size_t lo, size_t hi) {
    size_t i;

    for (i = lo; i < hi; ++i) {
        lds_stmt_t *st = &w->sc->stmts.items[i];
        int have_dot = st->where != LDS_IN_TOP;
        uint64_t dot = w->dot;

        if (st->kind == LDS_ST_ASSERT) {
            if (lw_assert(w, st, have_dot, dot) != 0) {
                return -1;
            }
        } else if (st->kind == LDS_ST_ASSIGN) {
            if (lw_assign(w, st, have_dot, &dot, NULL, 0) != 0) {
                return -1;
            }
        } else if (st->kind == LDS_ST_OUTSEC && !st->discard) {
            /* An output section nothing went into: its symbols are where
             * it would have been. */
            uint64_t end = dot;

            if (st->expr.count != 0 && lw_eval(w, &st->expr, 1, dot, &st->at, &dot) != 0) {
                return -1;
            }
            if (lw_body(w, st, NULL, dot, 0, &end) != 0) {
                return -1;
            }
            st->pad = 0;
            dot = end;
        }
        if (dot != w->dot) {
            if (dot < w->high) {
                lds_report_error(NULL, &st->at, "the location counter may not move back over what is placed");
                return -1;
            }
            w->dot = dot;
            w->moved = 1;
        }
    }
    return 0;
}

static lds_region_t *script_find_region(lds_script_t *sc, const char *name) {
    size_t i;

    for (i = 0; i < sc->region_count; ++i) {
        if (strcmp(sc->regions[i].name, name) == 0) {
            return &sc->regions[i];
        }
    }
    return NULL;
}

/* One pass over the output's sections with the script's location counter. */
static int lw_pass(lds_walk_t *w, uint64_t first_addr) {
    const uint64_t page = 0x1000u;
    lds_script_t *sc = w->sc;
    size_t next = 0, i;
    int last_class = -1;
    int last_relro = -1;

    w->dot = first_addr;
    w->high = 0;
    w->moved = 0;
    for (i = 0; i < sc->region_count; ++i) {
        lds_region_t *r = &sc->regions[i];
        lds_tok_t none;

        memset(&none, 0, sizeof(none));
        if (lw_eval(w, &r->origin, 0, 0, &none, &r->org) != 0 || lw_eval(w, &r->length, 0, 0, &none, &r->len) != 0) {
            return -1;
        }
        r->cursor = r->org;
    }
    if (!sc->has_sections) {
        return lw_run(w, 0, sc->stmts.count);
    }
    /* What stands before the first output section comes before anything
     * is placed: ". = 0x100000;" is where the image begins. */
    while (next < sc->stmts.count && sc->stmts.items[next].kind != LDS_ST_OUTSEC) {
        next++;
    }
    if (lw_run(w, 0, next) != 0) {
        return -1;
    }
    for (i = 0; i < elf_section_count(w->out); ++i) {
        elf_section_t *sec = elf_section_get(w->out, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;
        lds_stmt_t *os = NULL;
        lds_region_t *region = NULL;
        uint64_t flags, align, size, end;
        size_t si = 0;
        int explicit;
        int cls;

        if (sec == NULL || name == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        os = script_find_outsec(sc, name, &si);
        if (os != NULL && si < next) {
            os = NULL;          /* out of the script's order: placed as found */
        }
        if (os != NULL) {
            if (lw_run(w, next, si) != 0) {
                return -1;
            }
            next = si + 1;
        }
        explicit = w->moved;
        w->moved = 0;
        if (os != NULL && os->expr.count != 0) {
            if (lw_eval(w, &os->expr, 1, w->dot, &os->at, &w->dot) != 0) {
                return -1;
            }
            explicit = 1;
        }
        if (os != NULL && os->region != NULL) {
            region = script_find_region(sc, os->region);
            if (region == NULL) {
                lds_report_error(NULL, &os->at, "the section's memory region is not defined in MEMORY");
                return -1;
            }
            if (os->expr.count == 0) {
                w->dot = region->cursor;
                explicit = 1;
            }
        }
        cls = alloc_section_class(flags);
        if (!explicit) {
            /* As the default layout has it: what is protected differently
             * does not share a page. */
            int relro = cls == 2 && is_relro_candidate_name(name);

            if ((last_class != -1 && cls != last_class) ||
                (cls == 2 && last_class == 2 && last_relro != -1 && relro != last_relro)) {
                if (!align_up_u64_checked(w->dot, page, &w->dot)) {
                    return -1;
                }
            }
        }
        last_class = cls;
        last_relro = cls == 2 ? (is_relro_candidate_name(name) ? 1 : 0) : -1;
        if (os != NULL && os->align.count != 0) {
            uint64_t a = 0;

            if (lw_eval(w, &os->align, 1, w->dot, &os->at, &a) != 0) {
                return -1;
            }
            if (a == 0 || !align_up_u64_checked(w->dot, a, &w->dot)) {
                lds_report_error(NULL, &os->at, "invalid ALIGN for the output section");
                return -1;
            }
        }
        align = elf_section_align(sec);
        if (!align_up_u64_checked(w->dot, align != 0 ? align : 1, &w->dot)) {
            return -1;
        }
        if (w->dot < w->high) {
            fprintf(stderr, "ld: section %s at 0x%llx would begin before the end (0x%llx) of the one before it\n",
                    name, (unsigned long long)w->dot, (unsigned long long)w->high);
            return -1;
        }
        if (elf_section_set_addr(sec, w->dot) != ELF_OK) {
            return -1;
        }
        size = elf_section_size(sec);
        end = w->dot + size;
        if (os != NULL && lw_body(w, os, sec, w->dot, size, &end) != 0) {
            return -1;
        }
        w->dot = end;
        w->high = end;
        /* The symbols a script defines straight after a section are that
         * section's end ("etext = .;"), not the end of whatever the
         * script does not mention and is placed after it.  What moves the
         * counter waits its turn. */
        while (os != NULL && next < sc->stmts.count &&
               (sc->stmts.items[next].kind == LDS_ST_ASSERT ||
                (sc->stmts.items[next].kind == LDS_ST_ASSIGN && strcmp(sc->stmts.items[next].at.text, ".") != 0))) {
            if (lw_run(w, next, next + 1) != 0) {
                return -1;
            }
            next++;
        }
        if (region != NULL) {
            region->cursor = end;
            if (end > region->org + region->len) {
                fprintf(stderr, "ld: section %s does not fit in memory region %s (ends at 0x%llx, the region at 0x%llx)\n",
                        name, region->name, (unsigned long long)end,
                        (unsigned long long)(region->org + region->len));
                return -1;
            }
        }
    }
    return lw_run(w, next, sc->stmts.count);
}

static int script_assign_addresses(ld_ctx_t *ctx, elfobj_t *out) {
    lds_script_t *sc = ctx->script;
    lds_walk_t w;
    uint64_t first_addr = 0;
    int have_first = 0;
    size_t i;
    int pass;

    if (sc == NULL) {
        return 0;
    }
    /* Where the default layout began is where the script's begins, until
     * the script says otherwise. */
    for (i = 0; i < elf_section_count(out); ++i) {
        elf_section_t *sec = elf_section_get(out, i);

        if (sec != NULL && (elf_section_flags(sec) & SHF_ALLOC) != 0 &&
            (!have_first || elf_section_addr(sec) < first_addr)) {
            first_addr = elf_section_addr(sec);
            have_first = 1;
        }
    }
    memset(&w, 0, sizeof(w));
    w.ctx = ctx;
    w.out = out;
    w.sc = sc;
    /* A statement may use what a later one defines: twice over to learn
     * the values, a third time to act on them. */
    for (pass = 0; pass < 3; ++pass) {
        w.final = pass == 2;
        if (lw_pass(&w, first_addr) != 0) {
            return -1;
        }
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        lds_stmt_t *os = &sc->stmts.items[i];
        elf_section_t *sec;
        size_t size;

        if (os->kind != LDS_ST_OUTSEC || os->pad == 0 || (sec = elf_find_section(out, os->at.text)) == NULL) {
            continue;
        }
        size = (size_t)elf_section_size(sec);
        if (elf_section_type(sec) == SHT_NOBITS) {
            if (set_section_zero_data(sec, size + (size_t)os->pad) != 0) {
                return -1;
            }
        } else {
            const void *old = elf_section_data(sec, &size);
            uint8_t *buf = (uint8_t *)calloc(1, size + (size_t)os->pad);
            int ok;

            if (buf == NULL) {
                return -1;
            }
            if (old != NULL && size != 0) {
                memcpy(buf, old, size);
            }
            ok = elf_section_set_data(sec, buf, size + (size_t)os->pad) == ELF_OK;
            free(buf);
            if (!ok) {
                return -1;
            }
        }
        os->pad = 0;
    }
    return 0;
}

static int section_order_rank(const elf_section_t *sec) {
    const char *name;
    uint64_t flags;
    uint32_t type;

    if (sec == NULL) {
        return 999;
    }
    name = elf_section_name(sec);
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);

    if (name != NULL &&
        (strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 || strcmp(name, ".shstrtab") == 0)) {
        return 200;
    }
    /*
     * Among what is writable, what is written only by the dynamic linker
     * comes first and together, so that it can be made read-only once it
     * has been (PT_GNU_RELRO) without anything the program writes lying
     * in the same range: a .data between .data.rel.ro and .got was in
     * the range, and read-only when the program came to write it.
     */
    if (name != NULL && (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE) &&
        type != SHT_NOBITS && is_relro_candidate_name(name)) {
        if (strcmp(name, ".dynamic") == 0) {
            return 28;
        }
        if (strncmp(name, ".got", 4) == 0) {
            return 29;
        }
        return strncmp(name, ".data.rel.ro", 12) == 0 ? 27 : 26;
    }
    if (name != NULL) {
        if (strcmp(name, ".got.plt") == 0) {
            return 30;
        }
        if (strcmp(name, ".tm_clone_table") == 0) {
            return 32;
        }
    }
    if (type == SHT_REL || type == SHT_RELA) {
        if ((flags & SHF_ALLOC) != 0) {
            return 25;
        }
        return 190;
    }
    if ((flags & SHF_ALLOC) != 0) {
        if ((flags & SHF_EXECINSTR) != 0) {
            return 10;
        }
        if ((flags & SHF_WRITE) == 0) {
            return 20;
        }
        if (type == SHT_NOBITS) {
            return 40;
        }
        return 31;
    }
    return 100;
}

static int reorder_sections_default_policy(elfobj_t *obj) {
    size_t count;
    size_t target;

    if (obj == NULL) {
        return -1;
    }
    count = elf_section_count(obj);
    for (target = 0; target < count; ++target) {
        size_t i;
        size_t best = target;
        int best_rank = section_order_rank(elf_section_get(obj, target));

        for (i = target + 1; i < count; ++i) {
            int rank = section_order_rank(elf_section_get(obj, i));
            if (rank < best_rank) {
                best = i;
                best_rank = rank;
            }
        }
        if (best != target) {
            elf_section_t *best_sec = elf_section_get(obj, best);
            if (best_sec == NULL || elf_reorder_section(obj, best_sec, target) != ELF_OK) {
                return -1;
            }
        }
    }
    return 0;
}

static int is_gc_candidate_section(const elf_section_t *sec) {
    const char *name;
    uint64_t flags;
    uint32_t type;

    if (sec == NULL) {
        return 0;
    }
    name = elf_section_name(sec);
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    if ((flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if (type == SHT_NULL || type == SHT_SYMTAB || type == SHT_STRTAB || type == SHT_REL || type == SHT_RELA) {
        return 0;
    }
    if (name != NULL &&
        (strcmp(name, ".shstrtab") == 0 || strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 ||
         strcmp(name, ".group") == 0)) {
        return 0;
    }
    return 1;
}

static int mark_live_section(uint8_t *live, size_t count, uint16_t shndx, int *changed) {
    size_t idx;

    if (live == NULL || changed == NULL) {
        return -1;
    }
    if (shndx == SHN_UNDEF || shndx == SHN_ABS || shndx == SHN_COMMON || shndx >= 0xff00) {
        return 0;
    }
    if (shndx == 0 || (size_t)(shndx - 1) >= count) {
        return -1;
    }
    idx = (size_t)(shndx - 1);
    if (!live[idx]) {
        live[idx] = 1;
        *changed = 1;
    }
    return 0;
}

static void mark_group_peers_live(elfobj_t *obj, uint8_t *live, size_t count, int *changed) {
    size_t i;

    for (i = 0; i < count; ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        if (sec == NULL || !live[i]) {
            continue;
        }
        if ((elf_section_flags(sec) & SHF_GROUP) == 0) {
            continue;
        }
        {
            size_t j;
            for (j = 0; j < count; ++j) {
                const elf_section_t *peer = elf_section_get(obj, j);
                if (peer == NULL || live[j] || !is_gc_candidate_section(peer)) {
                    continue;
                }
                if ((elf_section_flags(peer) & SHF_GROUP) != 0) {
                    live[j] = 1;
                    *changed = 1;
                }
            }
        }
    }
}

static int gc_sections_by_reachability(elfobj_t *obj, const ld_ctx_t *ctx) {
    uint8_t *live;
    size_t count;
    int changed;
    size_t i;

    if (obj == NULL || ctx == NULL) {
        return -1;
    }
    count = elf_section_count(obj);
    live = (uint8_t *)calloc(count, sizeof(*live));
    if (live == NULL && count != 0) {
        return -1;
    }

    changed = 0;
    if (ctx->entry_symbol != NULL && ctx->entry_symbol[0] != '\0') {
        const elf_symbol_t *entry = elf_find_symbol(obj, ctx->entry_symbol);
        if (entry != NULL && mark_live_section(live, count, elf_symbol_shndx(entry), &changed) != 0) {
            free(live);
            return -1;
        }
    } else {
        const elf_symbol_t *entry = elf_find_symbol(obj, "_start");
        if (entry != NULL && mark_live_section(live, count, elf_symbol_shndx(entry), &changed) != 0) {
            free(live);
            return -1;
        }
    }
    for (i = 0; i < ctx->force_undefined.count; ++i) {
        const elf_symbol_t *root = elf_find_symbol(obj, ctx->force_undefined.items[i]);
        if (root != NULL && mark_live_section(live, count, elf_symbol_shndx(root), &changed) != 0) {
            free(live);
            return -1;
        }
    }
    for (i = 0; i < count; ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint32_t type = sec != NULL ? elf_section_type(sec) : SHT_NULL;
        if (type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_PREINIT_ARRAY) {
            live[i] = 1;
            changed = 1;
            continue;
        }
        if (sec != NULL && (elf_section_flags(sec) & SHF_GNU_RETAIN) != 0) {
            live[i] = 1;
            changed = 1;
        }
    }

    do {
        changed = 0;
        for (i = 0; i < count; ++i) {
            elf_section_t *sec;
            size_t rc;
            size_t ri;
            if (!live[i]) {
                continue;
            }
            sec = elf_section_get(obj, i);
            if (sec == NULL) {
                continue;
            }
            rc = elf_section_reloc_count(sec);
            for (ri = 0; ri < rc; ++ri) {
                const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
                const elf_symbol_t *sym;
                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                if (sym == NULL) {
                    continue;
                }
                if (mark_live_section(live, count, elf_symbol_shndx(sym), &changed) != 0) {
                    free(live);
                    return -1;
                }
            }
        }
        mark_group_peers_live(obj, live, count, &changed);
    } while (changed);

    for (i = count; i > 0; --i) {
        size_t idx = i - 1;
        elf_section_t *sec = elf_section_get(obj, idx);
        if (sec == NULL || !is_gc_candidate_section(sec) || live[idx]) {
            continue;
        }
        if (ctx->gc_print_sections) {
            const char *name = elf_section_name(sec);
            fprintf(stderr, "ld: gc-sections: removing %s\n", name != NULL ? name : "<unnamed>");
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            free(live);
            return -1;
        }
    }
    free(live);
    return 0;
}

static int is_icf_special_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, ".init_array") == 0 || strcmp(name, ".fini_array") == 0 ||
           strcmp(name, ".preinit_array") == 0 || strcmp(name, ".dynamic") == 0 ||
           strcmp(name, ".dynsym") == 0 || strcmp(name, ".dynstr") == 0;
}

static int is_icf_candidate_section(const elf_section_t *sec, int icf_mode) {
    uint64_t flags;
    uint32_t type;
    const char *name;

    if (sec == NULL || icf_mode == 0) {
        return 0;
    }
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);
    name = elf_section_name(sec);
    if (type != SHT_PROGBITS || (flags & SHF_ALLOC) == 0) {
        return 0;
    }
    if ((flags & SHF_GROUP) != 0) {
        return 0;
    }
    if (is_icf_special_name(name)) {
        return 0;
    }
    if (icf_mode == 1 && (flags & SHF_WRITE) != 0) {
        return 0;
    }
    return 1;
}

static int sections_reloc_signature_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t ra_n;
    size_t rb_n;
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    ra_n = elf_section_reloc_count(a);
    rb_n = elf_section_reloc_count(b);
    if (ra_n != rb_n) {
        return 0;
    }
    for (i = 0; i < ra_n; ++i) {
        const elf_reloc_t *ra = elf_section_reloc_at((elf_section_t *)a, i);
        const elf_reloc_t *rb = elf_section_reloc_at((elf_section_t *)b, i);
        const elf_symbol_t *sa;
        const elf_symbol_t *sb;
        const char *na = NULL;
        const char *nb = NULL;

        if (ra == NULL || rb == NULL) {
            return 0;
        }
        if (elf_reloc_offset(ra) != elf_reloc_offset(rb) || elf_reloc_type(ra) != elf_reloc_type(rb) ||
            elf_reloc_addend(ra) != elf_reloc_addend(rb) || elf_reloc_has_addend(ra) != elf_reloc_has_addend(rb)) {
            return 0;
        }
        sa = elf_reloc_symbol(ra);
        sb = elf_reloc_symbol(rb);
        na = sa != NULL ? elf_symbol_name(sa) : NULL;
        nb = sb != NULL ? elf_symbol_name(sb) : NULL;
        if ((na == NULL) != (nb == NULL)) {
            return 0;
        }
        if (na != NULL && strcmp(na, nb) != 0) {
            return 0;
        }
    }
    return 1;
}

static int sections_icf_equal(const elf_section_t *a, const elf_section_t *b) {
    size_t asz = 0;
    size_t bsz = 0;
    const void *ad;
    const void *bd;

    if (a == NULL || b == NULL) {
        return 0;
    }
    if (elf_section_type(a) != elf_section_type(b) || elf_section_flags(a) != elf_section_flags(b) ||
        elf_section_align(a) != elf_section_align(b) || elf_section_size(a) != elf_section_size(b)) {
        return 0;
    }
    ad = elf_section_data(a, &asz);
    bd = elf_section_data(b, &bsz);
    if (asz != bsz) {
        return 0;
    }
    if (asz != 0 && (ad == NULL || bd == NULL || memcmp(ad, bd, asz) != 0)) {
        return 0;
    }
    if (!sections_reloc_signature_equal(a, b)) {
        return 0;
    }
    return 1;
}

static int icf_fold_section(elfobj_t *obj, size_t leader_idx, size_t dupe_idx) {
    size_t i;
    elf_section_t *dupe;
    uint16_t dupe_shndx;
    uint16_t leader_shndx;

    if (obj == NULL || leader_idx >= elf_section_count(obj) || dupe_idx >= elf_section_count(obj)) {
        return -1;
    }
    dupe = elf_section_get(obj, dupe_idx);
    if (dupe == NULL) {
        return -1;
    }
    dupe_shndx = (uint16_t)(dupe_idx + 1);
    leader_shndx = (uint16_t)(leader_idx + 1);
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        elf_symbol_t *sym = elf_symbol_at(obj, i);
        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_shndx(sym) == dupe_shndx) {
            if (elf_symbol_set_shndx(sym, leader_shndx) != ELF_OK) {
                return -1;
            }
        }
    }
    if (elf_remove_section(obj, dupe) != ELF_OK) {
        return -1;
    }
    return 0;
}

static int apply_identical_code_folding(elfobj_t *obj, const ld_ctx_t *ctx) {
    size_t i;

    if (obj == NULL || ctx == NULL || ctx->icf_mode == 0) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *leader = elf_section_get(obj, i);
        size_t j;

        if (!is_icf_candidate_section(leader, ctx->icf_mode)) {
            continue;
        }
        for (j = i + 1; j < elf_section_count(obj);) {
            elf_section_t *dupe = elf_section_get(obj, j);
            if (!is_icf_candidate_section(dupe, ctx->icf_mode) || !sections_icf_equal(leader, dupe)) {
                j++;
                continue;
            }
            if (icf_fold_section(obj, i, j) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int is_relro_candidate_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strncmp(name, ".got.plt", 8) == 0) {
        return 0;
    }
    if (strncmp(name, ".got", 4) == 0 ||
        strncmp(name, ".data.rel.ro", 12) == 0 ||
        strcmp(name, ".dynamic") == 0 ||
        strncmp(name, ".init_array", 11) == 0 ||
        strncmp(name, ".fini_array", 11) == 0 ||
        strncmp(name, ".preinit_array", 14) == 0 ||
        strncmp(name, ".ctors", 6) == 0 ||
        strncmp(name, ".dtors", 6) == 0) {
        return 1;
    }
    return 0;
}

static int has_execstack_note(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        if (sec == NULL) {
            continue;
        }
        name = elf_section_name(sec);
        if (name == NULL || strcmp(name, ".note.GNU-stack") != 0) {
            continue;
        }
        if ((elf_section_flags(sec) & SHF_EXECINSTR) != 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Whether the script says where the output section `name` goes, rather
 * than leaving it to follow the one before: an address of its own, a
 * memory region the one before was not in, or an assignment to the
 * location counter since.  Such a section may be anywhere, and a segment
 * is one run of the file mapped at one run of addresses, so it begins a
 * segment of its own.
 */
static int script_places_section(const ld_ctx_t *ctx, const char *name) {
    const lds_script_t *sc = ctx->script;
    const lds_stmt_t *prev = NULL;
    int moved = 0;
    size_t i;

    if (sc == NULL || !sc->has_sections || name == NULL) {
        return 0;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *st = &sc->stmts.items[i];

        if (st->kind == LDS_ST_ASSIGN && strcmp(st->at.text, ".") == 0) {
            moved = 1;
        }
        if (st->kind != LDS_ST_OUTSEC || st->discard) {
            continue;
        }
        if (strcmp(st->at.text, name) == 0) {
            return moved || st->expr.count != 0 ||
                   (st->region != NULL &&
                    (prev == NULL || prev->region == NULL || strcmp(prev->region, st->region) != 0));
        }
        prev = st;
        moved = 0;
    }
    return 0;
}

static int add_default_segments(elfobj_t *obj, const ld_ctx_t *ctx) {
    elf_segment_t *load_rx = NULL;
    elf_segment_t *load_ro = NULL;
    elf_segment_t *load_rw = NULL;
    elf_segment_t *tls_seg = NULL;
    elf_segment_t *relro_seg = NULL;
    elf_segment_t *eh_frame_seg = NULL;
    elf_segment_t *gnu_property_seg = NULL;
    elf_section_t *dyn;
    size_t i;
    int execstack_mode;
    int applied;

    if (obj == NULL || ctx == NULL) {
        return -1;
    }
    applied = add_script_segments(obj, ctx);
    if (applied != 0) {
        return applied < 0 ? -1 : 0;
    }

    if (elf_add_segment(obj, PT_PHDR, LD_PF_R, 8) == NULL) {
        return -1;
    }
    if (ctx->interp_path != NULL && ctx->interp_path[0] != '\0') {
        if (elf_add_interp_segment(obj, ctx->interp_path) == NULL) {
            return -1;
        }
    }

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        uint64_t flags;
        uint32_t type;
        int is_alloc;

        if (sec == NULL) {
            continue;
        }
        name = elf_section_name(sec);
        flags = elf_section_flags(sec);
        type = elf_section_type(sec);
        is_alloc = (flags & SHF_ALLOC) != 0;

        if (is_alloc && script_places_section(ctx, name)) {
            if ((flags & SHF_EXECINSTR) != 0) {
                load_rx = NULL;
            } else if ((flags & SHF_WRITE) != 0) {
                load_rw = NULL;
            } else {
                load_ro = NULL;
            }
        }
        if (is_alloc) {
            if ((flags & SHF_EXECINSTR) != 0) {
                if (load_rx == NULL) {
                    load_rx = elf_add_load_segment(obj, LD_PF_R | LD_PF_X, 0x1000);
                    if (load_rx == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_rx, sec) != ELF_OK) {
                    return -1;
                }
            } else if ((flags & SHF_WRITE) != 0) {
                if (load_rw == NULL) {
                    load_rw = elf_add_load_segment(obj, LD_PF_R | LD_PF_W, 0x1000);
                    if (load_rw == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_rw, sec) != ELF_OK) {
                    return -1;
                }
            } else {
                if (load_ro == NULL) {
                    load_ro = elf_add_load_segment(obj, LD_PF_R, 0x1000);
                    if (load_ro == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_ro, sec) != ELF_OK) {
                    return -1;
                }
            }
        }

        if (is_alloc && type == SHT_NOTE) {
            elf_segment_t *note_seg = elf_add_segment(obj, PT_NOTE, LD_PF_R, 4);
            if (note_seg == NULL) {
                return -1;
            }
            if (elf_segment_add_section(note_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && name != NULL && strcmp(name, ".eh_frame_hdr") == 0 && elf_section_size(sec) != 0) {
            if (eh_frame_seg == NULL) {
                eh_frame_seg = elf_add_segment(obj, PT_GNU_EH_FRAME, LD_PF_R, 4);
                if (eh_frame_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(eh_frame_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && name != NULL && strcmp(name, ".note.gnu.property") == 0) {
            if (gnu_property_seg == NULL) {
                gnu_property_seg = elf_add_segment(obj, PT_GNU_PROPERTY, LD_PF_R, 8);
                if (gnu_property_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(gnu_property_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (ctx->z_relro && is_alloc && (flags & SHF_WRITE) != 0 &&
            is_relro_candidate_name(name)) {
            if (relro_seg == NULL) {
                relro_seg = elf_add_segment(obj, PT_GNU_RELRO, LD_PF_R, 1);
                if (relro_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(relro_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && (flags & SHF_TLS) != 0) {
            if (tls_seg == NULL) {
                tls_seg = elf_add_tls_segment(obj, 8);
                if (tls_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(tls_seg, sec) != ELF_OK) {
                return -1;
            }
        }
    }

    dyn = elf_find_section(obj, ".dynamic");
    if (dyn != NULL) {
        elf_segment_t *dyn_seg = elf_add_dynamic_segment(obj, 8);
        if (dyn_seg == NULL || elf_segment_add_section(dyn_seg, dyn) != ELF_OK) {
            return -1;
        }
    }

    execstack_mode = ctx->z_execstack;
    if (execstack_mode < 0) {
        execstack_mode = has_execstack_note(obj) ? 1 : 0;
    }
    if (elf_add_segment(obj, PT_GNU_STACK,
                        LD_PF_R | LD_PF_W | (execstack_mode ? LD_PF_X : 0), 16) == NULL) {
        return -1;
    }
    return 0;
}

static int strip_group_sections_for_final(elfobj_t *obj) {
    size_t i = 0;

    while (i < elf_section_count(obj)) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        if (sec == NULL) {
            i++;
            continue;
        }
        name = elf_section_name(sec);
        if (name == NULL || strcmp(name, ".group") != 0) {
            i++;
            continue;
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int enforce_wx_policy(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags;
        const char *name;
        if (sec == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        if ((flags & SHF_WRITE) != 0 && (flags & SHF_EXECINSTR) != 0) {
            name = elf_section_name(sec);
            fprintf(stderr,
                    "ld: W^X policy violation: section %s is both writable and executable\n",
                    name != NULL ? name : "<unnamed>");
            return -1;
        }
    }
    return 0;
}

static int symbol_is_defined(const elf_symbol_t *sym) {
    return sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
}

/*
 * The mode of the output: what creat() would have given it, readable and
 * writable by all and executable if it is a program, less what the umask
 * takes away.  The file was made private, by mkstemp(), and was then set
 * to 0755 or 0644 whatever the umask said.  A device or a pipe that was
 * written into keeps the mode it has.
 */
static int set_output_mode(const char *path, int executable) {
    struct stat st;
    mode_t mask;

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    mask = umask(0);
    (void)umask(mask);
    return chmod(path, (executable ? 0777 : 0666) & ~mask);
}

/* Take away an output that turned out wrong, if it is a file of ours. */
static void remove_output(const char *path) {
    struct stat st;

    if (path != NULL && lstat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        (void)unlink(path);
    }
}

static int set_entry_symbol(ld_ctx_t *ctx, elfobj_t *obj, const char *entry_symbol, int require_entry,
                            int entry_explicit) {
    const elf_symbol_t *sym;
    uint64_t addr = 0;

    if (entry_symbol == NULL || entry_symbol[0] == '\0') {
        return 0;
    }
    sym = elf_find_symbol(obj, entry_symbol);
    if (!symbol_is_defined(sym)) {
        if (require_entry) {
            if (entry_explicit) {
                fprintf(stderr, "ld: entry symbol '%s' not found\n", entry_symbol);
                return -1;
            }
            /*
             * No _start.  The program then begins where its text does,
             * and is told so: to begin instead at whichever global
             * function came first in the symbol table, as this did, is
             * to run something nobody chose.
             */
            {
                elf_section_t *text = elf_find_section(obj, ".text");

                if (text == NULL || (elf_section_flags(text) & SHF_ALLOC) == 0) {
                    fprintf(stderr, "ld: entry symbol '%s' not found, and there is no .text to begin at\n",
                            entry_symbol);
                    return -1;
                }
                addr = elf_section_addr(text);
                if (ld_warn(ctx, "cannot find entry symbol %s; the program begins at the start of .text, 0x%llx",
                            entry_symbol, (unsigned long long)addr) != 0) {
                    return -1;
                }
                if (elf_set_entry(obj, addr) != ELF_OK) {
                    fprintf(stderr, "ld: failed to set fallback .text entry address\n");
                    return -1;
                }
                return 0;
            }
        }
        return 0;
    }
    if (resolve_symbol_addr(obj, sym, 0, &addr, NULL) != 0) {
        fprintf(stderr, "ld: failed to resolve entry symbol '%s'\n", entry_symbol);
        return -1;
    }
    if (elf_set_entry(obj, addr) != ELF_OK) {
        fprintf(stderr, "ld: failed to set entry address\n");
        return -1;
    }
    return 0;
}

static int check_undefined_symbols(elfobj_t *obj, const ld_ctx_t *ctx, int allow_undefined, const symref_map_t *refs) {
    size_t i;
    if (allow_undefined) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_shndx(sym) != SHN_UNDEF) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_WEAK) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_GLOBAL) {
            const char *name = elf_symbol_name(sym);
            if (name != NULL && name[0] != '\0') {
                if (strcmp(name, "_GLOBAL_OFFSET_TABLE_") == 0) {
                    continue;
                }
                {
                    int has_provider = 0;
                    if (unresolved_symbol_has_dso_provider((ld_ctx_t *)ctx, name, &has_provider) != 0) {
                        fprintf(stderr, "ld: failed while validating unresolved symbol providers\n");
                        return -1;
                    }
                    if (has_provider) {
                        continue;
                    }
                }
                const char *src = refs != NULL ? symref_map_get(refs, name) : NULL;
                if (src != NULL) {
                    fprintf(stderr, "ld: undefined reference to `%s` (referenced by %s)\n", name, src);
                    ld_diag_note("unresolved-symbol", src, "add defining object/library before this reference");
                } else {
                    fprintf(stderr, "ld: undefined reference to `%s`\n", name);
                    ld_diag_note("unresolved-symbol", NULL, "add defining object/library to link inputs");
                }
                return -1;
            }
        }
    }
    return 0;
}

static int apply_all_relocations(elfobj_t *obj, const ld_ctx_t *ctx, int allow_undefined) {
    size_t i;
    static int trace_reloc_env = -1;

    if (trace_reloc_env < 0) {
        const char *v = getenv("LD_DEBUG_RELOC_TRACE");
        trace_reloc_env = (v != NULL && v[0] != '\0') ? 1 : 0;
    }
    elfobj_endian_t endian = elf_endian(obj);
    uint16_t machine = elf_machine(obj);

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags = sec != NULL ? elf_section_flags(sec) : 0;
        uint8_t *buf;
        const void *src;
        size_t sec_sz;
        size_t rc;
        size_t ri;

        if (sec == NULL) {
            continue;
        }
        /* Sections that are not loaded are relocated like the rest: the
         * debugging information is all addresses of the program, and it
         * was left pointing at nothing, with its relocations copied into
         * the executable beside it. */
        rc = elf_section_reloc_count(sec);
        if (rc == 0) {
            continue;
        }
        if (elf_section_type(sec) == SHT_NOBITS) {
            fprintf(stderr, "ld: relocations against NOBITS section '%s' unsupported\n",
                    elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        src = elf_section_data(sec, &sec_sz);
        if (src == NULL || sec_sz == 0) {
            fprintf(stderr, "ld: relocation target section '%s' has no data\n",
                    elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
            return -1;
        }
        buf = (uint8_t *)malloc(sec_sz);
        if (buf == NULL) {
            return -1;
        }
        memcpy(buf, src, sec_sz);

        for (ri = 0; ri < rc; ++ri) {
            const elf_reloc_t *rel = elf_section_reloc_at(sec, ri);
            const char *sec_name = elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>";
            uint64_t off;
            uint32_t type;
            int64_t addend;
            int width;
            const elf_symbol_t *sym;
            const char *sym_name = "<none>";
            uint64_t S = 0;
            uint64_t P;
            uint64_t outv = 0;
            const char *undef_name = NULL;
            elf_err_t err;

            if (rel == NULL) {
                continue;
            }
            off = elf_reloc_offset(rel);
            type = elf_reloc_type(rel);
            width = elf_reloc_size_for_machine(elf_machine(obj), type);
            if (width <= 0 || width > 8) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: unsupported relocation width\n",
                        sec_name, (unsigned long long)off, type, sym_name);
                return -1;
            }
            if (off + (uint64_t)width > sec_sz) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: relocation out of range\n",
                        sec_name, (unsigned long long)off, type, sym_name);
                return -1;
            }
            if (elf_reloc_has_addend(rel)) {
                addend = elf_reloc_addend(rel);
            } else {
                uint64_t raw = read_uint_bytes(buf + off, width, endian);
                if (reloc_addend_is_signed(machine, type)) {
                    addend = sign_extend_u64(raw, width * 8);
                } else {
                    addend = (int64_t)raw;
                }
            }

            sym = elf_reloc_symbol(rel);
            if (sym != NULL && elf_symbol_name(sym) != NULL && elf_symbol_name(sym)[0] != '\0') {
                sym_name = elf_symbol_name(sym);
            }
            if (resolve_symbol_addr_for_reloc(obj, ctx, sym, allow_undefined, type, &S, &undef_name) != 0) {
                if ((flags & SHF_ALLOC) == 0) {
                    /* What a description of the program refers to may
                     * have been left out of it; the description then
                     * says 0, and nothing that runs depends on it. */
                    continue;
                }
                if (can_defer_runtime_reloc(ctx, machine, type, sym)) {
                    if (trace_reloc_env) {
                        fprintf(stderr,
                                "ld: reloc-trace: deferred runtime relocation section=%s off=0x%llx type=%u sym=%s\n",
                                sec_name, (unsigned long long)off, (unsigned)type,
                                undef_name != NULL ? undef_name : sym_name);
                    }
                    continue;
                }
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: unresolved relocation symbol\n",
                        sec_name, (unsigned long long)off, type, undef_name != NULL ? undef_name : sym_name);
                return -1;
            }
            P = elf_section_addr(sec) + off;
            if (machine == EM_386 && reloc_is_i386_got_relative(type)) {
                /*
                 * The i386 relocations that are relative to the table
                 * (psABI: GOTPC = GOT + A - P, GOTOFF = S + A - GOT,
                 * GOT32 and GOT32X = G + A - GOT, G the address of the
                 * symbol's slot).  The library's arithmetic has S, A and
                 * P to work with and made each of them S + A.
                 */
                uint64_t got = i386_got_base(obj);
                int defined = sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF;
                long slot = defined ? local_got_slot(ctx, sym) : -1;
                const char *why = NULL;

                if (type == R_386_GOTPC) {
                    outv = got + (uint64_t)addend - P;
                } else if (type == R_386_GOTOFF) {
                    outv = S + (uint64_t)addend - got;
                } else if (!defined) {
                    /* An import: S is its slot already. */
                    outv = S + (uint64_t)addend - got;
                } else if (slot >= 0) {
                    elf_section_t *gs = elf_find_section(obj, ".got");

                    outv = elf_section_addr(gs) + 4U * (uint64_t)slot +
                           (uint64_t)addend - got;
                } else if (type == R_386_GOT32X && off >= 2 &&
                           buf[off - 2] == 0x8b && (buf[off - 1] & 0xc0) == 0x80) {
                    /* mov sym@GOT(%reg), %reg -> lea sym@GOTOFF(%reg), %reg */
                    buf[off - 2] = 0x8d;
                    outv = S + (uint64_t)addend - got;
                } else {
                    why = "a reference through the GOT to a symbol with no slot in it";
                }
                if (got == 0 && why == NULL) {
                    why = "GOT-relative relocation and no GOT";
                }
                if (why != NULL) {
                    free(buf);
                    fprintf(stderr,
                            "ld: relocation error: section=%s offset=0x%llx type=%s symbol=%s: %s\n",
                            sec_name, (unsigned long long)off,
                            elf_reloc_name_for_machine(machine, type), sym_name, why);
                    return -1;
                }
                write_uint_bytes(buf + off, width, endian, outv & 0xffffffffULL);
                continue;
            }
            err = elf_apply_relocation_value(obj, type, P, S, addend, &outv);
            if (err != ELF_OK) {
                free(buf);
                fprintf(stderr,
                        "ld: relocation error: section=%s offset=0x%llx type=%u symbol=%s: %s\n",
                        sec_name, (unsigned long long)off, type, sym_name, elf_errstr(err));
                return -1;
            }
            if (machine == EM_X86_64 &&
                (type == R_X86_64_GOTPCREL ||
                 type == R_X86_64_GOTPCRELX ||
                 type == R_X86_64_REX_GOTPCRELX) &&
                sym != NULL && elf_symbol_shndx(sym) != SHN_UNDEF &&
                off >= 2 && buf[off - 2] == 0x8b) {
                /*
                 * We currently materialize GOTPCREL-family relocations with
                 * S+A-P math in elf_reloc.c. For resolved/non-preemptible
                 * symbols that is only valid when we relax MOV mem->reg into
                 * LEA so the instruction yields the symbol address.
                 */
                buf[off - 2] = 0x8d;
            }
            if (trace_reloc_env) {
                fprintf(stderr,
                        "ld: reloc-trace: section=%s off=0x%llx type=%u sym=%s P=0x%llx S=0x%llx A=%lld out=0x%llx\n",
                        sec_name != NULL ? sec_name : "<unnamed>",
                        (unsigned long long)off,
                        (unsigned)type,
                        sym_name != NULL ? sym_name : "<null>",
                        (unsigned long long)P,
                        (unsigned long long)S,
                        (long long)addend,
                        (unsigned long long)outv);
            }
            write_uint_bytes(buf + off, width, endian, outv);
        }

        if (elf_section_set_data(sec, buf, sec_sz) != ELF_OK) {
            free(buf);
            return -1;
        }
        free(buf);
        /* Applied, and what the dynamic linker has to do has its own
         * records by now; an executable or shared object does not carry
         * the linker's. */
        if (elf_type(obj) != ET_REL && !ctx->emit_relocs &&
            elf_section_clear_relocations(sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

static int validate_output(const ld_ctx_t *ctx) {
#ifdef LD_SUBSTRATE_BUILD
    (void)ctx;
    return 0;
#else
    elfobj_t *obj = NULL;
    struct stat st;

    /* Only what can be read back: an output that went to a pipe or a
     * device is gone, and opening a pipe to look would wait for ever. */
    if (stat(ctx->out_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (elf_open(ctx->out_path, &obj) != ELF_OK) {
        fprintf(stderr, "ld: failed to open output %s\n", ctx->out_path);
        return -1;
    }
    if (ctx->expect_type != 0 && elf_type(obj) != ctx->expect_type) {
        fprintf(stderr, "ld: wrong output ELF type\n");
        elf_close(obj);
        return -1;
    }
    if (ctx->mode == 64) {
        if (elf_class(obj) != ELFOBJ_CLASS_64 || elf_machine(obj) != EM_X86_64) {
            fprintf(stderr, "ld: expected x86_64 ELF64 output\n");
            elf_close(obj);
            return -1;
        }
    } else {
        if (elf_class(obj) != ELFOBJ_CLASS_32 || elf_machine(obj) != EM_386) {
            fprintf(stderr, "ld: expected i386 ELF32 output\n");
            elf_close(obj);
            return -1;
        }
    }
    elf_close(obj);
    return 0;
#endif
}

static int ensure_substrate_ld_note(elfobj_t *out) {
    static const char note_name[] = "Substrate";
    static const char note_desc[] = "Substrate Linker v0.1";
    static const uint32_t note_type = 0x5355424cU; /* "SUBL" */
    elf_section_t *sec;
    elfobj_endian_t endian;
    uint8_t *buf;
    size_t namesz;
    size_t descsz;
    size_t name_pad;
    size_t desc_pad;
    size_t total;

    if (out == NULL) {
        return -1;
    }
    sec = elf_find_section(out, ".note.substrate_ld");
    if (sec == NULL) {
        sec = elf_add_section(out, ".note.substrate_ld", SHT_NOTE, SHF_ALLOC);
        if (sec == NULL) {
            return -1;
        }
    }

    namesz = sizeof(note_name);
    descsz = sizeof(note_desc);
    name_pad = (namesz + 3u) & ~(size_t)3u;
    desc_pad = (descsz + 3u) & ~(size_t)3u;
    total = 12u + name_pad + desc_pad;

    buf = (uint8_t *)calloc(1, total);
    if (buf == NULL) {
        return -1;
    }

    endian = elf_endian(out);
    write_u32_endian(buf + 0, endian, (uint32_t)namesz);
    write_u32_endian(buf + 4, endian, (uint32_t)descsz);
    write_u32_endian(buf + 8, endian, note_type);
    memcpy(buf + 12, note_name, namesz);
    memcpy(buf + 12 + name_pad, note_desc, descsz);

    if (elf_section_set_align(sec, 4) != ELF_OK || elf_section_set_data(sec, buf, total) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static int run_internal_link(ld_ctx_t *ctx) {
    objvec_t inputs;
    symref_map_t undef_refs;
    elfobj_t *out = NULL;
    elf_err_t err;
    uint16_t out_type;
    int allow_undef_runtime;
    int is_program;             /* an executable or a PIE: not a library */
    uint64_t base_vaddr;

    memset(&inputs, 0, sizeof(inputs));
    memset(&undef_refs, 0, sizeof(undef_refs));
    if (plugin_discover_and_handshake(ctx) != 0) {
        return -1;
    }
    if (load_all_inputs(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (write_reproduce_bundle(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    emit_trace_inputs(ctx, &inputs);
    emit_trace_symbols(ctx, &inputs);
    if (emit_common_symbol_warnings(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (check_symbol_precedence(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (collect_undefined_refs(&inputs, &undef_refs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (inputs.count == 0) {
        fprintf(stderr, "ld: no compatible relocatable input objects found\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        return -1;
    }

    /* The inputs go in under their own names, so that a failure in one of
     * them can be reported as that one's. */
    {
        elf_link_plan_t *plan = elf_link_plan_create();
        size_t pi;

        err = plan != NULL ? ELF_OK : ELF_ERR_OOM;
        for (pi = 0; err == ELF_OK && pi < inputs.count; ++pi) {
            err = elf_link_plan_add_input(plan, inputs.objs[pi],
                                          inputs.names[pi] != NULL ? inputs.names[pi] : "?");
        }
        if (err == ELF_OK && ctx->script != NULL && ctx->script->has_sections) {
            err = elf_link_plan_set_section_name_hook(plan, script_output_name, ctx->script);
        }
        if (err == ELF_OK) {
            err = elf_link_plan_link(plan, &out);
        }
        if (plan != NULL) {
            elf_link_plan_destroy(plan);
        }
    }
    if (err != ELF_OK || out == NULL) {
        const char *why = out != NULL ? elf_last_diagnostics(out) : "";

        fprintf(stderr, "ld: link merge failed: %s%s%s\n", elf_errstr(err),
                why[0] != '\0' ? ": " : "", why);
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        if (out != NULL) {
            elf_close(out);
        }
        return -1;
    }

    out_type = ctx->expect_type == 0 ? ET_EXEC : ctx->expect_type;
    is_program = out_type == ET_EXEC || (out_type == ET_DYN && ctx->pie);
    allow_undef_runtime = ctx->allow_undefined;
    if (!is_program && out_type == ET_DYN && !ctx->explicit_unresolved_policy) {
        allow_undef_runtime = 1;
    }
    if (elf_set_type(out, out_type) != ELF_OK) {
        fprintf(stderr, "ld: failed to set output type\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    /*
     * What this linker makes is for substrate, and says so, whichever
     * system the linker itself was built to run on: the brand is what
     * the kernel picks the personality by, and the system's own shared
     * objects carry it as its programs do.
     */
    if ((out_type == ET_EXEC || out_type == ET_DYN) && elf_set_osabi(out, ELFOSABI_SUBSTRATE) != ELF_OK) {
        fprintf(stderr, "ld: failed to set Substrate ELF OSABI\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->strip_debug) {
        size_t si;

        for (si = elf_section_count(out); si > 0; --si) {
            elf_section_t *sec = elf_section_get(out, si - 1);
            const char *name = sec != NULL ? elf_section_name(sec) : NULL;

            if (name != NULL && (elf_section_flags(sec) & SHF_ALLOC) == 0 &&
                (strncmp(name, ".debug", 6) == 0 || strncmp(name, ".zdebug", 7) == 0 ||
                 strncmp(name, ".stab", 5) == 0 || strncmp(name, ".gnu.debuglto_", 14) == 0) &&
                elf_remove_section(out, sec) != ELF_OK) {
                fprintf(stderr, "ld: failed to leave out section %s\n", name);
                symref_map_free(&undef_refs);
                objvec_free(&inputs);
                elf_close(out);
                return -1;
            }
        }
    }
    if (script_declare_symbols(ctx, out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (apply_defsyms(ctx, out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to apply default section placement policy\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (script_apply_sections(ctx, out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->gc_sections && gc_sections_by_reachability(out, ctx) != 0) {
        fprintf(stderr, "ld: --gc-sections failed during reachability sweep\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->icf_mode != 0 && apply_identical_code_folding(out, ctx) != 0) {
        fprintf(stderr, "ld: --icf fold pass failed\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (out_type == ET_REL) {
        if (write_map_file(ctx, &inputs, out) != 0) {
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (elf_write_file(out, ctx->out_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (set_output_mode(ctx->out_path, 0) != 0) {
            if (ld_warn(ctx, "failed to set output mode on %s: %s",
                        ctx->out_path, strerror(errno)) != 0) {
                symref_map_free(&undef_refs);
                objvec_free(&inputs);
                elf_close(out);
                return -1;
            }
        }
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return 0;
    }

    if (is_program && ensure_substrate_ld_note(out) != 0) {
        fprintf(stderr, "ld: failed to emit .note.substrate_ld metadata\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (strip_group_sections_for_final(out) != 0) {
        fprintf(stderr, "ld: failed to strip SHT_GROUP sections for final output\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (note_dso_names(ctx) != 0 || settle_undefined_weak(ctx, out) != 0 || plan_dynamic_imports(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to plan GOT/PLT dynamic imports\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (plan_local_got_i386(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to make the global offset table\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (plan_dynamic_needed(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to plan dynamic DT_NEEDED entries\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to reorder sections after dynamic planning\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (add_default_segments(out, ctx) != 0) {
        fprintf(stderr, "ld: failed to add output program segments\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (reorder_sections_default_policy(out) != 0) {
        fprintf(stderr, "ld: failed to reorder sections after segment planning\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (ctx->mode == 64) {
        base_vaddr = (out_type == ET_DYN) ? 0x0ULL : 0x400000ULL;
    } else {
        base_vaddr = (out_type == ET_DYN) ? 0x0ULL : 0x08048000ULL;
    }
    if (ctx->have_image_base) {
        base_vaddr = ctx->image_base;
    }

    if (assign_section_addresses(out, base_vaddr) != 0) {
        fprintf(stderr, "ld: failed to assign section virtual addresses\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (script_assign_addresses(ctx, out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->mode == 64) {
        if (finalize_dynamic_imports_x64(out, &ctx->dyn_imports) != 0) {
            fprintf(stderr, "ld: failed to finalize x86_64 GOT/PLT dynamic data\n");
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    } else if (ctx->mode == 32) {
        if (finalize_dynamic_imports_i386(out, &ctx->dyn_imports) != 0) {
            fprintf(stderr, "ld: failed to finalize i386 GOT/PLT dynamic data\n");
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }
    if (patch_dynamic_tag_values(out) != 0) {
        fprintf(stderr, "ld: failed to finalize .dynamic tag values\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (enforce_wx_policy(out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    {
        const char *textrel_sec = text_relocation_section(ctx, out);

        if (textrel_sec != NULL && ctx->z_text_mode == 1) {
            fprintf(stderr,
                    "ld: -z text: section %s is read-only and has relocations the dynamic linker would apply\n",
                    textrel_sec);
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (textrel_sec != NULL && ctx->z_text_mode != 2 &&
            ld_warn(ctx, "section %s is read-only and has relocations for the dynamic linker (DT_TEXTREL); "
                         "was it compiled without -fPIC?", textrel_sec) != 0) {
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }

    if (check_undefined_symbols(out, ctx, allow_undef_runtime, &undef_refs) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (fill_local_got_i386(ctx, out) != 0 ||
        apply_all_relocations(out, ctx, allow_undef_runtime) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (set_entry_symbol(ctx, out, ctx->entry_symbol != NULL ? ctx->entry_symbol : "_start",
                         is_program, ctx->entry_symbol != NULL) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (finalize_symbol_values_for_output(out) != 0) {
        fprintf(stderr, "ld: failed to finalize output symbol value addresses\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (patch_dynsym_symbol_values(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to patch .dynsym symbol value addresses\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (write_map_file(ctx, &inputs, out) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (elf_write_file(out, ctx->out_path) != ELF_OK) {
        fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (set_output_mode(ctx->out_path, is_program) != 0) {
        if (ld_warn(ctx, "failed to set output mode on %s: %s",
                    ctx->out_path, strerror(errno)) != 0) {
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
    }

    symref_map_free(&undef_refs);
    objvec_free(&inputs);
    elf_close(out);
    return 0;
}

static int parse_arg_value(const char *arg, const char *opt, const char **out_val) {
    size_t n = strlen(opt);
    if (strncmp(arg, opt, n) != 0) {
        return 0;
    }
    if (arg[n] == '\0') {
        return 1;
    }
    *out_val = arg + n;
    return 2;
}

/*
 * An option that is a word and takes a value: -word VALUE, --word VALUE,
 * -word=VALUE, --word=VALUE.  1, with *val the value and *i on the last
 * argument used; 0 if argv[*i] is not this option; -1 if it is and has no
 * value.
 */
static int long_opt_value(int argc, char **argv, int *i, const char *word, const char **val) {
    const char *a = argv[*i];
    size_t n = strlen(word);

    if (a[0] != '-') {
        return 0;
    }
    a += a[1] == '-' ? 2 : 1;
    if (strncmp(a, word, n) != 0 || (a[n] != '\0' && a[n] != '=')) {
        return 0;
    }
    if (a[n] == '=') {
        *val = a + n + 1;
        return 1;
    }
    if (*i + 1 >= argc) {
        return -1;
    }
    *val = argv[++*i];
    return 1;
}

int main(int argc, char **argv) {
    ld_ctx_t ctx;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out";
    ctx.self_path = argv[0];
    ctx.compat_mode = LD_COMPAT_GNU;
#ifdef LD_SUBSTRATE_BUILD
    ctx.current_lib_mode = LD_LIBMODE_STATIC;
#else
    ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
#endif
    ctx.current_whole_archive = 0;
    ctx.current_as_needed = 0;
    ctx.z_text_mode = 0;
    ctx.z_execstack = -1;
    ctx.z_relro = 1;
    ctx.hash_style = LD_HASH_BOTH;

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *val = NULL;
        int p;

        if (strcmp(a, "--help") == 0) {
            usage(argv[0]);
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            strvec_free(&ctx.force_undefined);
            defsymvec_free(&ctx.defsyms);
            strvec_free(&ctx.dso_inputs);
            return 0;
        }
        if ((p = parse_arg_value(a, "--compat=", &val)) != 0) {
            ld_compat_mode_t compat;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (parse_compat_mode(val, &compat) != 0) {
                fprintf(stderr, "ld: unsupported compatibility mode '%s' (expected gnu or lld)\n", val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.compat_mode = compat;
            continue;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-v") == 0) {
            ctx.query_version = 1;
            continue;
        }
        if (strcmp(a, "-m32") == 0) {
            if (set_explicit_mode(&ctx, 32, "-m32") != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "-m64") == 0) {
            if (set_explicit_mode(&ctx, 64, "-m64") != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-m", &val)) != 0) {
            int m;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            m = parse_mode_token(val);
            if (m == 0) {
                fprintf(stderr,
                        "ld: unsupported emulation '%s' for -m "
                        "(supported: elf_x86_64, elf64-x86-64, x86_64, amd64, "
                        "elf_i386, elf32-i386, i386)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (set_explicit_mode(&ctx, m, a) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }
        if ((p = parse_arg_value(a, "-plugin-opt", &val)) != 0 ||
            (p = parse_arg_value(a, "--plugin-opt", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -plugin-opt requires an argument\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (ctx.plugin_opt_count >= sizeof(ctx.plugin_opts) / sizeof(ctx.plugin_opts[0])) {
                fprintf(stderr, "ld: too many -plugin-opt arguments (max %zu)\n",
                        sizeof(ctx.plugin_opts) / sizeof(ctx.plugin_opts[0]));
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.plugin_opts[ctx.plugin_opt_count++] = val;
            continue;
        }
        if ((p = parse_arg_value(a, "-plugin", &val)) != 0 || (p = parse_arg_value(a, "--plugin", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -plugin requires a path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.plugin_path = val;
            continue;
        }
        if (strcmp(a, "-r") == 0) {
            ctx.expect_type = ET_REL;
            continue;
        }
        if (strcmp(a, "-shared") == 0 || strcmp(a, "-pie") == 0 || strcmp(a, "--pie") == 0 ||
            strcmp(a, "-Bshareable") == 0) {
            /* Both are ET_DYN.  A PIE is a program all the same: it has
             * an entry, what it leaves undefined is an error, and it is
             * made executable. */
            ctx.expect_type = ET_DYN;
            ctx.pie = a[1] == 'p' || a[2] == 'p';
            if (!ctx.explicit_lib_mode) {
                ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
            }
            continue;
        }
        if (strcmp(a, "-no-pie") == 0 || strcmp(a, "--no-pie") == 0) {
            if (ctx.pie) {
                ctx.pie = 0;
                ctx.expect_type = ET_EXEC;
            }
            continue;
        }
        if (strcmp(a, "-static") == 0) {
            /* Which libraries to use, not what to make: -static -pie is a
             * PIE, and -shared -static a shared object of archives. */
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "-Bstatic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "-Bdynamic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
            ctx.explicit_lib_mode = 1;
            continue;
        }
        if (strcmp(a, "--whole-archive") == 0) {
            ctx.current_whole_archive = 1;
            continue;
        }
        if (strcmp(a, "--no-whole-archive") == 0) {
            ctx.current_whole_archive = 0;
            continue;
        }
        if (strcmp(a, "--as-needed") == 0) {
            ctx.current_as_needed = 1;
            continue;
        }
        if (strcmp(a, "--no-as-needed") == 0) {
            ctx.current_as_needed = 0;
            continue;
        }
        if (strcmp(a, "--gc-sections") == 0) {
            ctx.gc_sections = 1;
            continue;
        }
        if (strcmp(a, "--no-gc-sections") == 0) {
            ctx.gc_sections = 0;
            continue;
        }
        if (strcmp(a, "--print-gc-sections") == 0) {
            ctx.gc_print_sections = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--icf", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (strcmp(val, "safe") == 0) {
                ctx.icf_mode = 1;
            } else if (strcmp(val, "all") == 0) {
                ctx.icf_mode = 2;
            } else if (strcmp(val, "none") == 0) {
                ctx.icf_mode = 0;
            } else {
                fprintf(stderr,
                        "ld: unsupported --icf mode '%s' (supported: safe, all, none)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--start-group") == 0) {
            if (inputvec_push(&ctx.inputs, LD_INPUT_GROUP_START, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, NULL) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if (strcmp(a, "--end-group") == 0) {
            if (inputvec_push(&ctx.inputs, LD_INPUT_GROUP_END, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, NULL) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if (strcmp(a, "--allow-undefined") == 0) {
            ctx.allow_undefined = 1;
            ctx.explicit_unresolved_policy = 1;
            continue;
        }
        if (strcmp(a, "--no-undefined") == 0) {
            ctx.allow_undefined = 0;
            ctx.explicit_unresolved_policy = 1;
            continue;
        }
        if (strcmp(a, "--warn-common") == 0) {
            ctx.warn_common = 1;
            continue;
        }
        if (strcmp(a, "--no-warn-common") == 0) {
            ctx.warn_common = 0;
            continue;
        }
        if (strcmp(a, "--fatal-warnings") == 0) {
            ctx.fatal_warnings = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--unresolved-symbols", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (strcmp(val, "ignore-all") == 0) {
                ctx.allow_undefined = 1;
                ctx.explicit_unresolved_policy = 1;
            } else if (strcmp(val, "report-all") == 0) {
                ctx.allow_undefined = 0;
                ctx.explicit_unresolved_policy = 1;
            } else {
                fprintf(stderr,
                        "ld: unsupported --unresolved-symbols policy '%s' "
                        "(supported: ignore-all, report-all)\n",
                        val);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-u", &val)) != 0 || (p = parse_arg_value(a, "--undefined", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    strvec_free(&ctx.dso_inputs);
                    defsymvec_free(&ctx.defsyms);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0' || strvec_push(&ctx.force_undefined, val) != 0) {
                fprintf(stderr, "ld: --undefined requires a symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "--defsym", &val)) != 0) {
            char *eq;
            char *name_dup;
            uint64_t value;

            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    strvec_free(&ctx.dso_inputs);
                    defsymvec_free(&ctx.defsyms);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            name_dup = xstrdup(val != NULL ? val : "");
            if (name_dup == NULL) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 1;
            }
            eq = strchr(name_dup, '=');
            if (eq == NULL) {
                free(name_dup);
                fprintf(stderr, "ld: --defsym requires NAME=VALUE\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            *eq++ = '\0';
            if (name_dup[0] == '\0' || parse_u64_auto(eq, &value) != 0 ||
                defsymvec_push(&ctx.defsyms, name_dup, value) != 0) {
                free(name_dup);
                fprintf(stderr, "ld: invalid --defsym `%s`\n", val != NULL ? val : "");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                strvec_free(&ctx.dso_inputs);
                defsymvec_free(&ctx.defsyms);
                return 2;
            }
            free(name_dup);
            continue;
        }
        if (strcmp(a, "--export-dynamic") == 0) {
            ctx.export_dynamic = 1;
            continue;
        }
        if (strcmp(a, "-rdynamic") == 0 || strcmp(a, "-E") == 0) {
            ctx.export_dynamic = 1;
            continue;
        }
        if (strcmp(a, "--no-export-dynamic") == 0) {
            ctx.export_dynamic = 0;
            continue;
        }
        if (strcmp(a, "-e") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.entry_symbol = argv[++i];
            continue;
        }
        if ((p = parse_arg_value(a, "--entry", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: --entry requires a non-empty symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.entry_symbol = val;
            continue;
        }
        {
            /* -name VALUE, --name VALUE, -name=VALUE, --name=VALUE */
            const char *lval = NULL;
            int got;

            if ((got = long_opt_value(argc, argv, &i, "dynamic-linker", &lval)) > 0) {
                ctx.interp_path = lval;
            } else if (got == 0 && ((got = long_opt_value(argc, argv, &i, "soname", &lval)) > 0 ||
                                    (got == 0 && strcmp(a, "-h") == 0 && i + 1 < argc && (lval = argv[++i]) != NULL &&
                                     (got = 1) != 0))) {
                ctx.soname = lval;
            } else if (got == 0 && (got = long_opt_value(argc, argv, &i, "rpath", &lval)) > 0) {
                if (strvec_push(&ctx.rpaths, lval) != 0) {
                    got = -1;
                }
            } else if (got == 0) {
                /* Where to look for the libraries of libraries at link
                 * time, which this linker does not go looking for. */
                got = long_opt_value(argc, argv, &i, "rpath-link", &lval);
            }
            if (got < 0) {
                fprintf(stderr, "ld: %s needs a value\n", a);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            if (got > 0) {
                continue;
            }
        }
        if ((p = parse_arg_value(a, "-L", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (strvec_push(&ctx.lib_paths, val) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-l", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    return 2;
                }
                val = argv[++i];
            }
            if (inputvec_push(&ctx.inputs, LD_INPUT_LIB, ctx.current_lib_mode,
                              ctx.current_whole_archive, ctx.current_as_needed, val) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 1;
            }
            continue;
        }
        if ((p = parse_arg_value(a, "-Map", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -Map requires a non-empty path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.map_path = val;
            continue;
        }
        if ((p = parse_arg_value(a, "--reproduce", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: --reproduce requires a non-empty directory path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.reproduce_path = val;
            continue;
        }
        if ((p = parse_arg_value(a, "--hash-style", &val)) != 0) {
            ld_hash_style_t style;
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (parse_hash_style_option(val, &style) != 0) {
                fprintf(stderr, "ld: unsupported --hash-style value '%s' (expected sysv|gnu|both)\n",
                        val != NULL ? val : "");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.hash_style = style;
            continue;
        }
        if (strcmp(a, "--trace") == 0) {
            ctx.trace_inputs = 1;
            continue;
        }
        if ((p = parse_arg_value(a, "--trace-symbol", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0' || strvec_push(&ctx.trace_symbols, val) != 0) {
                fprintf(stderr, "ld: --trace-symbol requires a symbol name\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }

        {
            /* -Ttext and its kin are words of their own, not -T and a
             * script called "text". */
            static const char *const unsupported[] = { "Ttext", "Tdata", "Tbss", "Trodata-segment",
                                                       "Tldata-segment", NULL };
            const char *lval = NULL;
            size_t u;
            int got;

            if ((got = long_opt_value(argc, argv, &i, "Ttext-segment", &lval)) > 0) {
                if (parse_u64_auto(lval, &ctx.image_base) != 0 || (ctx.image_base & 0xfffu) != 0) {
                    fprintf(stderr, "ld: -Ttext-segment needs an address that is a multiple of the page size, not '%s'\n",
                            lval);
                    got = -2;
                } else {
                    ctx.have_image_base = 1;
                }
            }
            for (u = 0; got == 0 && unsupported[u] != NULL; ++u) {
                if (long_opt_value(argc, argv, &i, unsupported[u], &lval) != 0) {
                    fprintf(stderr,
                            "ld: -%s is not supported: say where a section goes in a linker script (-T), "
                            "as in SECTIONS { .text ADDRESS : { *(.text) } }, or where the image begins with "
                            "-Ttext-segment\n", unsupported[u]);
                    got = -2;
                }
            }
            if (got < 0) {
                if (got == -1) {
                    fprintf(stderr, "ld: %s needs a value\n", a);
                }
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            if (got > 0) {
                continue;
            }
        }
        if ((p = parse_arg_value(a, "-T", &val)) != 0 || (p = parse_arg_value(a, "--script", &val)) != 0) {
            if (p == 1) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                val = argv[++i];
            } else if (val[0] == '=') {
                val++;
            }
            if (val == NULL || val[0] == '\0') {
                fprintf(stderr, "ld: -T/--script requires a path\n");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ctx.script_path = val;
            continue;
        }
        if (strcmp(a, "-z") == 0 || strncmp(a, "-z", 2) == 0) {
            const char *zval = NULL;
            if (strcmp(a, "-z") == 0) {
                if (i + 1 >= argc) {
                    usage(argv[0]);
                    inputvec_free(&ctx.inputs);
                    strvec_free(&ctx.lib_paths);
                    strvec_free(&ctx.trace_symbols);
                    return 2;
                }
                zval = argv[++i];
            } else {
                zval = a + 2;
                if (zval[0] == '=') {
                    zval++;
                }
            }
            /*
             * A keyword this linker does nothing about is said to be so
             * and the link goes on: build systems pass -z defs, -z
             * max-page-size=..., -z separate-code and -z nodelete as a
             * matter of course, and a link is not wrong for lacking one.
             */
            if (zval != NULL && zval[0] != '\0' &&
                (parse_z_option(&ctx, zval) == 0 ||
                 ld_warn(&ctx, "-z %s is not supported and is ignored", zval) == 0)) {
                continue;
            }
            if (zval == NULL || zval[0] == '\0') {
                fprintf(stderr, "ld: -z needs a keyword\n");
            }
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            return 2;
        }
        if (strcmp(a, "--strip-all") == 0 || strcmp(a, "-s") == 0 || strcmp(a, "--strip-debug") == 0 ||
            strcmp(a, "-S") == 0) {
            /* The debugging information is left out.  -s would leave the
             * symbol table out as well, which is not done. */
            ctx.strip_debug = 1;
            continue;
        }
        if (strcmp(a, "--emit-relocs") == 0 || strcmp(a, "-q") == 0) {
            ctx.emit_relocs = 1;
            continue;
        }
        if (strcmp(a, "--build-id") == 0 || strncmp(a, "--build-id=", 11) == 0) {
            continue;           /* no build ID is made */
        }
        if (strcmp(a, "--eh-frame-hdr") == 0 || strcmp(a, "--no-eh-frame-hdr") == 0) {
            ctx.eh_frame_hdr = a[2] == 'e';
            continue;
        }
        if (strncmp(a, "--sysroot=", 10) == 0) {
            ctx.sysroot = a + 10;
            continue;
        }
        if (strcmp(a, "--copy-dt-needed-entries") == 0 || strcmp(a, "--no-copy-dt-needed-entries") == 0 ||
            strcmp(a, "--add-needed") == 0 || strcmp(a, "--no-add-needed") == 0) {
            ctx.copy_dt_needed = strncmp(a, "--no-", 5) != 0;
            continue;
        }
        if (a[0] == '-' && a[1] == 'o' && a[2] != '\0') {
            ctx.out_path = a + 2;       /* -oFILE */
            continue;
        }

        if (a[0] == '-') {
            if (ctx.compat_mode == LD_COMPAT_LLD) {
                fprintf(stderr, "ld: error: unsupported option in lld mode: %s\n", a);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            if (ld_warn(&ctx, "unsupported option ignored (gnu mode): %s", a) != 0) {
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        /* Defensive: silently skip an empty argv entry or a bare "/"
         * argv slot.  Substrate's cc occasionally synthesised one of
         * these from a misresolved runtime path (uninitialised libgcc
         * buffer + access("/", R_OK) succeeds + libgcc kept as just
         * "/").  Treating this as a hard error left users staring at
         * `ld: failed to open input /` with no idea what produced it.
         * Rather than failing the whole link, ignore the bogus input
         * and let cc be fixed at its source.  A real one-character "/"
         * input file would be wrong on every conceivable system, so
         * losing nothing legitimate. */
        if (a == NULL || a[0] == '\0' ||
            (a[0] == '/' && a[1] == '\0')) {
            ld_warn(&ctx, "ignoring empty/bare-slash input argv slot "
                          "(probable upstream cc bug)");
            continue;
        }
        if (inputvec_push(&ctx.inputs, LD_INPUT_FILE, ctx.current_lib_mode,
                          ctx.current_whole_archive, ctx.current_as_needed, a) != 0) {
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            return 1;
        }
    }

    if (ctx.query_version && ctx.inputs.count == 0) {
        printf("GNU ld (Substrate) 2.42.0\n");
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 0;
    }
    if (ctx.script_path != NULL) {
        /* The script is read here, once; the link consults what it says. */
        ctx.script = lds_script_parse(ctx.script_path, &ctx);
        if (ctx.script != NULL && ctx.script->entry != NULL && ctx.entry_symbol == NULL) {
            ctx.entry_symbol = ctx.script->entry;
        }
        if (ctx.script == NULL) {
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            strvec_free(&ctx.trace_symbols);
            strvec_free(&ctx.force_undefined);
            defsymvec_free(&ctx.defsyms);
            strvec_free(&ctx.dso_inputs);
            dyn_import_vec_free(&ctx.dyn_imports);
            return 2;
        }
    }
    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 2;
    }

    if (ctx.mode == 0) {
        ctx.mode = default_mode();
    }
    if (ctx.expect_type == 0) {
        ctx.expect_type = ET_EXEC;
    }

    if (run_internal_link(&ctx) != 0) {
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 1;
    }
    if (validate_output(&ctx) != 0) {
        remove_output(ctx.out_path);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        dyn_import_vec_free(&ctx.dyn_imports);
        return 1;
    }

    inputvec_free(&ctx.inputs);
    strvec_free(&ctx.lib_paths);
    strvec_free(&ctx.trace_symbols);
    strvec_free(&ctx.force_undefined);
    defsymvec_free(&ctx.defsyms);
    strvec_free(&ctx.dso_inputs);
    dyn_import_vec_free(&ctx.dyn_imports);
    lds_script_free(ctx.script);
    return 0;
}

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
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define LD_MAX_SCRIPT_INCLUDE_DEPTH 64
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
} dyn_import_t;

typedef struct {
    dyn_import_t *items;
    size_t count;
    size_t cap;
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
    ld_hash_style_t hash_style;
    const char *out_path;
    const char *self_path;
    const char *script_path;
    const char *plugin_path;
    const char *plugin_opts[32];
    size_t plugin_opt_count;
    int plugin_checked;
    const char *entry_symbol;
    const char *interp_path;
    const char *map_path;
    const char *reproduce_path;
    ld_compat_mode_t compat_mode;
    ld_lib_mode_t current_lib_mode;
    int current_whole_archive;
    int current_as_needed;
    strvec_t lib_paths;
    strvec_t trace_symbols;
    strvec_t force_undefined;
    defsymvec_t defsyms;
    strvec_t dso_inputs;
    dyn_import_vec_t dyn_imports;
    inputvec_t inputs;
} ld_ctx_t;

static int dynstr_append_cstr(uint8_t **buf, size_t *len, size_t *cap, const char *name, uint32_t *out_off);
static int dynsym_should_export(const ld_ctx_t *ctx, const elfobj_t *out, const elf_symbol_t *sym);
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
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
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
    if (strcmp(tok, "elf_x86_64") == 0 || strcmp(tok, "elf64-x86-64") == 0 ||
        strcmp(tok, "x86_64") == 0 || strcmp(tok, "amd64") == 0 || strcmp(tok, "64") == 0) {
        return 64;
    }
    if (strcmp(tok, "elf_i386") == 0 || strcmp(tok, "elf32-i386") == 0 ||
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
    FILE *fp;
    long end;
    size_t got;
    unsigned char *buf;

    *out = NULL;
    *out_sz = 0;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    end = ftell(fp);
    if (end < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)end);
    if (buf == NULL && end != 0) {
        fclose(fp);
        return -1;
    }
    got = fread(buf, 1, (size_t)end, fp);
    fclose(fp);
    if (got != (size_t)end) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_sz = (size_t)end;
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
} lds_lexer_t;

typedef enum {
    LDS_AST_SECTIONS = 0,
    LDS_AST_PHDRS,
    LDS_AST_MEMORY,
    LDS_AST_ASSIGN,
    LDS_AST_ASSERT
} lds_ast_kind_t;

typedef struct {
    lds_ast_kind_t kind;
    char *name;
    const char *path;
    size_t line;
    size_t col;
} lds_ast_node_t;

typedef struct {
    lds_ast_node_t *nodes;
    size_t count;
    size_t cap;
} lds_ast_t;

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
} lds_eval_ctx_t;

typedef struct {
    char *name;
    uint32_t type;
    uint32_t flags;
    uint64_t align;
} lds_phdr_entry_t;

typedef struct {
    lds_phdr_entry_t *items;
    size_t count;
    size_t cap;
} lds_phdr_vec_t;

typedef struct {
    char *section_name;
    char *phdr_name;
} lds_sec_phdr_map_t;

typedef struct {
    lds_sec_phdr_map_t *items;
    size_t count;
    size_t cap;
} lds_sec_phdr_vec_t;

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

static void lds_sec_phdr_vec_free(lds_sec_phdr_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].section_name);
        free(v->items[i].phdr_name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int lds_sec_phdr_vec_push(lds_sec_phdr_vec_t *v, const char *section_name, const char *phdr_name) {
    lds_sec_phdr_map_t *next;

    if (v == NULL || section_name == NULL || phdr_name == NULL ||
        section_name[0] == '\0' || phdr_name[0] == '\0') {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        next = (lds_sec_phdr_map_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].section_name = xstrdup(section_name);
    v->items[v->count].phdr_name = xstrdup(phdr_name);
    if (v->items[v->count].section_name == NULL || v->items[v->count].phdr_name == NULL) {
        free(v->items[v->count].section_name);
        free(v->items[v->count].phdr_name);
        v->items[v->count].section_name = NULL;
        v->items[v->count].phdr_name = NULL;
        return -1;
    }
    v->count++;
    return 0;
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
    if (defsymvec_get(&ec->ctx->defsyms, name, out) == 0) {
        return 0;
    }
    if (ec->obj != NULL) {
        const elf_symbol_t *sym = elf_find_symbol((elfobj_t *)ec->obj, name);
        if (sym != NULL) {
            *out = elf_symbol_value(sym);
            return 0;
        }
    }
    *out = 0;
    return 0;
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

    for (i = 0; i < arg_count; ++i) {
        if (lds_eval_expr_slice(ec, items, arg_starts[i], arg_ends[i], &vals[i]) != 0) {
            return -1;
        }
    }

    if (strcmp(name, "ALIGN") == 0) {
        if (arg_count == 1) {
            if (align_u64(0, vals[0], out) != 0) {
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
        uint64_t tmp = 0;
        const char *sym = NULL;
        if (arg_count != 1 || arg_starts[0] >= arg_ends[0]) {
            ec->err_tok = name_tok;
            ec->err_msg = "DEFINED expects one symbol argument";
            return -1;
        }
        if (items[arg_starts[0]].kind == LDS_TOK_IDENT || items[arg_starts[0]].kind == LDS_TOK_STRING) {
            sym = items[arg_starts[0]].text;
        }
        if (sym != NULL && script_lookup_symbol_value(ec, sym, &tmp) == 0 && defsymvec_find(&ec->ctx->defsyms, sym) >= 0) {
            *out = 1;
        } else {
            *out = 0;
        }
    } else {
        ec->err_tok = name_tok;
        ec->err_msg = "unsupported linker-script builtin";
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
        if (lds_eval_lor(ec, items, idx, end, &inner) != 0) {
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
    if (tok->kind == LDS_TOK_IDENT && tok->text != NULL) {
        if (*idx + 1 < end && items[*idx + 1].kind == LDS_TOK_LPAREN) {
            int consumed = lds_eval_builtin_call(ec, tok, items, *idx + 1, end, out);
            if (consumed < 0) {
                return -1;
            }
            *idx = (size_t)consumed;
            return 0;
        }
        if (script_lookup_symbol_value(ec, tok->text, out) != 0) {
            ec->err_tok = tok;
            ec->err_msg = "failed to resolve symbol in expression";
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

static int lds_eval_unary(lds_eval_ctx_t *ec, const lds_tok_t *items, size_t *idx, size_t end, uint64_t *out) {
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
            if (rhs == 0) {
                ec->err_tok = op;
                ec->err_msg = "division by zero";
                return -1;
            }
            *out = (*out) / rhs;
        } else {
            if (rhs == 0) {
                ec->err_tok = op;
                ec->err_msg = "modulo by zero";
                return -1;
            }
            *out = (*out) % rhs;
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
    if (lds_eval_lor(ec, items, &idx, end, out) != 0) {
        return -1;
    }
    if (idx != end) {
        ec->err_tok = &items[idx];
        ec->err_msg = "unexpected trailing tokens in expression";
        return -1;
    }
    return 0;
}

static void lds_ast_free(lds_ast_t *ast) {
    size_t i;

    if (ast == NULL) {
        return;
    }
    for (i = 0; i < ast->count; ++i) {
        free(ast->nodes[i].name);
    }
    free(ast->nodes);
    ast->nodes = NULL;
    ast->count = 0;
    ast->cap = 0;
}

static int lds_ast_push(lds_ast_t *ast, lds_ast_kind_t kind, const char *name, const lds_tok_t *tok) {
    lds_ast_node_t *next;

    if (ast == NULL || tok == NULL) {
        return -1;
    }
    if (ast->count == ast->cap) {
        size_t ncap = ast->cap == 0 ? 16 : ast->cap * 2;
        next = (lds_ast_node_t *)realloc(ast->nodes, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ast->nodes = next;
        ast->cap = ncap;
    }
    ast->nodes[ast->count].kind = kind;
    ast->nodes[ast->count].name = name != NULL ? xstrdup(name) : NULL;
    ast->nodes[ast->count].path = tok->path;
    ast->nodes[ast->count].line = tok->line;
    ast->nodes[ast->count].col = tok->col;
    if (name != NULL && ast->nodes[ast->count].name == NULL) {
        return -1;
    }
    ast->count++;
    return 0;
}

static int lds_lex_push_text(lds_tok_t *tok, const unsigned char *start, size_t n) {
    tok->text = (char *)malloc(n + 1);
    if (tok->text == NULL) {
        return -1;
    }
    memcpy(tok->text, start, n);
    tok->text[n] = '\0';
    return 0;
}

static int lds_is_ident_start(int c) {
    return isalpha(c) || c == '_' || c == '.' || c == '/' || c == '$';
}

static int lds_is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.' || c == '/' || c == '$' || c == '-' || c == '+';
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

static int script_section_pattern_match(const char *pattern, const char *name) {
    size_t pn;

    if (pattern == NULL || name == NULL) {
        return 0;
    }
    if (strcmp(pattern, "*") == 0) {
        return 1;
    }
    pn = strlen(pattern);
    if (pn > 0 && pattern[pn - 1] == '*') {
        return strncmp(name, pattern, pn - 1) == 0;
    }
    return strcmp(pattern, name) == 0;
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

static int apply_script_output_section_order(elfobj_t *obj, const char *section_name, size_t *target_index) {
    elf_section_t *sec;
    size_t target;
    size_t count;
    size_t cur_idx;

    if (obj == NULL || section_name == NULL || target_index == NULL || section_name[0] == '\0') {
        return 0;
    }
    sec = elf_find_section(obj, section_name);
    if (sec == NULL) {
        return 0;
    }
    count = elf_section_count(obj);
    if (*target_index >= count) {
        return 0;
    }
    cur_idx = count;
    for (target = 0; target < count; ++target) {
        if (elf_section_get(obj, target) == sec) {
            cur_idx = target;
            break;
        }
    }
    target = *target_index;
    if (cur_idx != target && elf_reorder_section(obj, sec, target) != ELF_OK) {
        return -1;
    }
    (*target_index)++;
    return 0;
}

static int parse_linker_script_file_rec(const char *path, strvec_t *include_stack, lds_ast_t *ast, ld_ctx_t *ctx,
                                        const elfobj_t *obj, int apply_semantics, size_t *section_reorder_target,
                                        int depth);

static int apply_script_assignment(const strvec_t *include_stack, const lds_tok_t *name_tok, const char *name,
                                   const lds_tokvec_t *expr_tokens, ld_ctx_t *ctx, const elfobj_t *obj,
                                   int apply_semantics, int provide_only) {
    lds_eval_ctx_t ec;
    uint64_t value = 0;

    if (name_tok == NULL || name == NULL || expr_tokens == NULL) {
        return -1;
    }
    if (!apply_semantics || ctx == NULL) {
        return 0;
    }
    memset(&ec, 0, sizeof(ec));
    ec.ctx = ctx;
    ec.obj = obj;
    if (lds_eval_expr_slice(&ec, expr_tokens->items, 0, expr_tokens->count, &value) != 0) {
        lds_report_error(include_stack, ec.err_tok != NULL ? ec.err_tok : name_tok,
                         ec.err_msg != NULL ? ec.err_msg : "expression evaluation failed");
        return -1;
    }
    if (provide_only && defsymvec_find(&ctx->defsyms, name) >= 0) {
        return 0;
    }
    if (defsymvec_set(&ctx->defsyms, name, value) != 0) {
        lds_report_error(include_stack, name_tok, "failed to record script symbol assignment");
        return -1;
    }
    return 0;
}

static int parse_script_assert_directive(lds_lexer_t *lx, const strvec_t *include_stack, ld_ctx_t *ctx,
                                         const elfobj_t *obj, int apply_semantics, const lds_tok_t *assert_tok) {
    lds_tok_t tok;
    lds_tokvec_t args;
    size_t i;
    int depth = 1;

    memset(&args, 0, sizeof(args));
    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_LPAREN) {
        lds_report_error(include_stack, &tok, "expected '(' after ASSERT");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);

    while (depth > 0) {
        if (lds_lex_next(lx, &tok) != 0) {
            lds_tokvec_free(&args);
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_report_error(include_stack, assert_tok, "unexpected end-of-file in ASSERT");
            lds_tok_free(&tok);
            lds_tokvec_free(&args);
            return -1;
        }
        if (tok.kind == LDS_TOK_LPAREN) {
            depth++;
            if (depth > 1 && lds_tokvec_push(&args, &tok) != 0) {
                lds_tok_free(&tok);
                lds_tokvec_free(&args);
                return -1;
            }
        } else if (tok.kind == LDS_TOK_RPAREN) {
            depth--;
            if (depth > 0 && lds_tokvec_push(&args, &tok) != 0) {
                lds_tok_free(&tok);
                lds_tokvec_free(&args);
                return -1;
            }
        } else if (lds_tokvec_push(&args, &tok) != 0) {
            lds_tok_free(&tok);
            lds_tokvec_free(&args);
            return -1;
        }
        lds_tok_free(&tok);
    }

    if (lds_lex_next(lx, &tok) != 0) {
        lds_tokvec_free(&args);
        return -1;
    }
    if (tok.kind != LDS_TOK_SEMI) {
        lds_report_error(include_stack, &tok, "expected ';' after ASSERT");
        lds_tok_free(&tok);
        lds_tokvec_free(&args);
        return -1;
    }
    lds_tok_free(&tok);

    if (apply_semantics && ctx != NULL) {
        lds_eval_ctx_t ec;
        size_t split = args.count;
        int comma_depth = 0;
        uint64_t result = 0;
        const char *msg = "ASSERT expression is false";

        for (i = 0; i < args.count; ++i) {
            if (args.items[i].kind == LDS_TOK_LPAREN) {
                comma_depth++;
            } else if (args.items[i].kind == LDS_TOK_RPAREN) {
                comma_depth--;
            } else if (args.items[i].kind == LDS_TOK_COMMA && comma_depth == 0) {
                split = i;
                break;
            }
        }
        memset(&ec, 0, sizeof(ec));
        ec.ctx = ctx;
        ec.obj = obj;
        if (lds_eval_expr_slice(&ec, args.items, 0, split, &result) != 0) {
            lds_report_error(include_stack, ec.err_tok != NULL ? ec.err_tok : assert_tok,
                             ec.err_msg != NULL ? ec.err_msg : "ASSERT expression evaluation failed");
            lds_tokvec_free(&args);
            return -1;
        }
        if (split + 1 < args.count && args.items[split + 1].kind == LDS_TOK_STRING && args.items[split + 1].text != NULL) {
            msg = args.items[split + 1].text;
        }
        if (result == 0) {
            lds_report_error(include_stack, assert_tok, msg);
            lds_tokvec_free(&args);
            return -1;
        }
    }

    lds_tokvec_free(&args);
    return 0;
}

static int parse_script_provide_directive(lds_lexer_t *lx, const strvec_t *include_stack, ld_ctx_t *ctx,
                                          const elfobj_t *obj, int apply_semantics, const lds_tok_t *provide_tok) {
    lds_tok_t tok;
    lds_tok_t name_tok;
    lds_tokvec_t expr;
    int depth = 1;
    int rc;

    memset(&name_tok, 0, sizeof(name_tok));
    memset(&expr, 0, sizeof(expr));

    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_LPAREN) {
        lds_report_error(include_stack, &tok, "expected '(' after PROVIDE");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);

    if (lds_lex_next(lx, &name_tok) != 0) {
        return -1;
    }
    if (name_tok.kind != LDS_TOK_IDENT || name_tok.text == NULL) {
        lds_report_error(include_stack, &name_tok, "expected symbol name in PROVIDE");
        lds_tok_free(&name_tok);
        return -1;
    }

    if (lds_lex_next(lx, &tok) != 0) {
        lds_tok_free(&name_tok);
        return -1;
    }
    if (tok.kind != LDS_TOK_EQUAL) {
        lds_report_error(include_stack, &tok, "expected '=' in PROVIDE");
        lds_tok_free(&tok);
        lds_tok_free(&name_tok);
        return -1;
    }
    lds_tok_free(&tok);

    while (depth > 0) {
        if (lds_lex_next(lx, &tok) != 0) {
            lds_tok_free(&name_tok);
            lds_tokvec_free(&expr);
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_report_error(include_stack, provide_tok, "unexpected end-of-file in PROVIDE");
            lds_tok_free(&tok);
            lds_tok_free(&name_tok);
            lds_tokvec_free(&expr);
            return -1;
        }
        if (tok.kind == LDS_TOK_LPAREN) {
            depth++;
            if (lds_tokvec_push(&expr, &tok) != 0) {
                lds_tok_free(&tok);
                lds_tok_free(&name_tok);
                lds_tokvec_free(&expr);
                return -1;
            }
        } else if (tok.kind == LDS_TOK_RPAREN) {
            depth--;
            if (depth > 0 && lds_tokvec_push(&expr, &tok) != 0) {
                lds_tok_free(&tok);
                lds_tok_free(&name_tok);
                lds_tokvec_free(&expr);
                return -1;
            }
        } else if (lds_tokvec_push(&expr, &tok) != 0) {
            lds_tok_free(&tok);
            lds_tok_free(&name_tok);
            lds_tokvec_free(&expr);
            return -1;
        }
        lds_tok_free(&tok);
    }

    if (lds_lex_next(lx, &tok) != 0) {
        lds_tok_free(&name_tok);
        lds_tokvec_free(&expr);
        return -1;
    }
    if (tok.kind != LDS_TOK_SEMI) {
        lds_report_error(include_stack, &tok, "expected ';' after PROVIDE");
        lds_tok_free(&tok);
        lds_tok_free(&name_tok);
        lds_tokvec_free(&expr);
        return -1;
    }
    lds_tok_free(&tok);

    rc = apply_script_assignment(include_stack, &name_tok, name_tok.text, &expr, ctx, obj, apply_semantics, 1);
    lds_tok_free(&name_tok);
    lds_tokvec_free(&expr);
    return rc;
}

static int parse_script_keep_directive(lds_lexer_t *lx, const strvec_t *include_stack, ld_ctx_t *ctx,
                                       const elfobj_t *obj, int apply_semantics, const lds_tok_t *keep_tok) {
    lds_tok_t tok;
    int depth = 1;

    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_LPAREN) {
        lds_report_error(include_stack, &tok, "expected '(' after KEEP");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);

    while (depth > 0) {
        if (lds_lex_next(lx, &tok) != 0) {
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_report_error(include_stack, keep_tok, "unexpected end-of-file in KEEP");
            lds_tok_free(&tok);
            return -1;
        }
        if (tok.kind == LDS_TOK_LPAREN) {
            depth++;
        } else if (tok.kind == LDS_TOK_RPAREN) {
            depth--;
        } else if (apply_semantics && ctx != NULL && obj != NULL &&
                   (tok.kind == LDS_TOK_IDENT || tok.kind == LDS_TOK_STRING) &&
                   tok.text != NULL && tok.text[0] == '.') {
            if (apply_script_keep_pattern((elfobj_t *)obj, tok.text) != 0) {
                lds_report_error(include_stack, &tok, "failed to apply KEEP section pattern");
                lds_tok_free(&tok);
                return -1;
            }
        }
        lds_tok_free(&tok);
    }
    return 0;
}

static int parse_script_discard_directive(lds_lexer_t *lx, const strvec_t *include_stack, ld_ctx_t *ctx,
                                          const elfobj_t *obj, int apply_semantics, const lds_tok_t *discard_tok) {
    lds_tok_t tok;
    int depth = 1;

    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_COLON) {
        lds_report_error(include_stack, &tok, "expected ':' after /DISCARD/");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);
    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_LBRACE) {
        lds_report_error(include_stack, &tok, "expected '{' after /DISCARD/:");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);

    while (depth > 0) {
        if (lds_lex_next(lx, &tok) != 0) {
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_report_error(include_stack, discard_tok, "unexpected end-of-file in /DISCARD/ block");
            lds_tok_free(&tok);
            return -1;
        }
        if (tok.kind == LDS_TOK_LBRACE) {
            depth++;
        } else if (tok.kind == LDS_TOK_RBRACE) {
            depth--;
        } else if (apply_semantics && ctx != NULL && obj != NULL &&
                   (tok.kind == LDS_TOK_IDENT || tok.kind == LDS_TOK_STRING) &&
                   tok.text != NULL && tok.text[0] == '.') {
            if (apply_script_discard_pattern((elfobj_t *)obj, tok.text) != 0) {
                lds_report_error(include_stack, &tok, "failed to apply /DISCARD/ section pattern");
                lds_tok_free(&tok);
                return -1;
            }
        }
        lds_tok_free(&tok);
    }
    return 0;
}

static int parse_script_insert_directive(lds_lexer_t *lx, const strvec_t *include_stack, const lds_tok_t *insert_tok) {
    lds_tok_t tok;

    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_IDENT || tok.text == NULL ||
        (strcmp(tok.text, "AFTER") != 0 && strcmp(tok.text, "BEFORE") != 0)) {
        lds_report_error(include_stack, &tok, "expected BEFORE or AFTER after INSERT");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);
    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if ((tok.kind != LDS_TOK_IDENT && tok.kind != LDS_TOK_STRING) || tok.text == NULL) {
        lds_report_error(include_stack, &tok, "expected section name after INSERT BEFORE/AFTER");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);
    if (lds_lex_next(lx, &tok) != 0) {
        return -1;
    }
    if (tok.kind != LDS_TOK_SEMI) {
        lds_report_error(include_stack, &tok, "expected ';' after INSERT directive");
        lds_tok_free(&tok);
        return -1;
    }
    lds_tok_free(&tok);
    (void)insert_tok;
    return 0;
}

static int handle_script_include(const char *current_path, strvec_t *include_stack, lds_ast_t *ast, ld_ctx_t *ctx,
                                 const elfobj_t *obj, int apply_semantics, size_t *section_reorder_target, int depth,
                                 const lds_tok_t *tok) {
    char *resolved;
    int rc;

    if (tok == NULL || tok->text == NULL) {
        return -1;
    }
    resolved = resolve_script_include_path(current_path, tok->text);
    if (resolved == NULL) {
        lds_report_error(include_stack, tok, "failed to resolve INCLUDE path");
        return -1;
    }
    rc = parse_linker_script_file_rec(resolved, include_stack, ast, ctx, obj, apply_semantics, section_reorder_target,
                                      depth + 1);
    free(resolved);
    return rc;
}

static int parse_linker_script_file_rec(const char *path, strvec_t *include_stack, lds_ast_t *ast, ld_ctx_t *ctx,
                                        const elfobj_t *obj, int apply_semantics, size_t *section_reorder_target,
                                        int depth) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    lds_lexer_t lx;
    lds_tok_t tok;
    lds_tok_t assign_name_tok;
    lds_tokvec_t assign_expr_tokens;
    int brace_depth = 0;
    int paren_depth = 0;
    int pending_block_expect_lbrace = 0;
    int pending_block_depth = 0;
    int pending_sections_block = 0;
    int sections_block_depth = 0;
    char *pending_assign_name = NULL;
    char *pending_output_section_name = NULL;
    int pending_assign_wait_eq = 0;
    int pending_assign_active = 0;

    if (path == NULL || include_stack == NULL || ast == NULL) {
        return -1;
    }
    memset(&assign_name_tok, 0, sizeof(assign_name_tok));
    memset(&assign_expr_tokens, 0, sizeof(assign_expr_tokens));
    if (depth > LD_MAX_SCRIPT_INCLUDE_DEPTH) {
        lds_tok_t fake;
        memset(&fake, 0, sizeof(fake));
        fake.path = path;
        fake.line = 1;
        fake.col = 1;
        lds_report_error(include_stack, &fake, "INCLUDE depth exceeds limit (64)");
        return -1;
    }
    if (strvec_push(include_stack, path) != 0) {
        return -1;
    }
    if (read_file(path, &buf, &sz) != 0) {
        lds_tok_t fake;
        memset(&fake, 0, sizeof(fake));
        fake.path = path;
        fake.line = 1;
        fake.col = 1;
        lds_report_error(include_stack, &fake, "unable to read linker script");
        strvec_pop(include_stack);
        return -1;
    }
    memset(&lx, 0, sizeof(lx));
    lx.buf = buf;
    lx.len = sz;
    lx.path = path;
    lx.line = 1;
    lx.col = 1;

    for (;;) {
        int rc = lds_lex_next(&lx, &tok);
        if (rc != 0) {
            free(buf);
            strvec_pop(include_stack);
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_tok_free(&tok);
            break;
        }
        if (tok.kind == LDS_TOK_OTHER && tok.text != NULL &&
            (strcmp(tok.text, "<unterminated-comment>") == 0 || strcmp(tok.text, "<unterminated-string>") == 0)) {
            lds_report_error(include_stack, &tok, tok.text);
            lds_tok_free(&tok);
            free(buf);
            strvec_pop(include_stack);
            return -1;
        }
        if (tok.kind == LDS_TOK_IDENT && tok.text != NULL && strcmp(tok.text, "INCLUDE") == 0) {
            lds_tok_t include_tok;
            if (lds_lex_next(&lx, &include_tok) != 0) {
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                return -1;
            }
            if (include_tok.kind != LDS_TOK_IDENT && include_tok.kind != LDS_TOK_STRING) {
                lds_report_error(include_stack, &include_tok, "expected include file name after INCLUDE");
                lds_tok_free(&include_tok);
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                return -1;
            }
            if (handle_script_include(path, include_stack, ast, ctx, obj, apply_semantics, section_reorder_target,
                                      depth, &include_tok) != 0) {
                lds_tok_free(&include_tok);
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                return -1;
            }
            lds_tok_free(&include_tok);
            lds_tok_free(&tok);
            continue;
        }
        if (tok.kind == LDS_TOK_IDENT && tok.text != NULL && strcmp(tok.text, "KEEP") == 0) {
            if (parse_script_keep_directive(&lx, include_stack, ctx, obj, apply_semantics, &tok) != 0) {
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                return -1;
            }
            lds_tok_free(&tok);
            continue;
        }
        if (tok.kind == LDS_TOK_IDENT && tok.text != NULL && strcmp(tok.text, "/DISCARD/") == 0) {
            if (parse_script_discard_directive(&lx, include_stack, ctx, obj, apply_semantics, &tok) != 0) {
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                return -1;
            }
            lds_tok_free(&tok);
            continue;
        }
        if (sections_block_depth == 1 && pending_output_section_name != NULL && tok.kind == LDS_TOK_COLON) {
            if (apply_semantics && obj != NULL && section_reorder_target != NULL &&
                apply_script_output_section_order((elfobj_t *)obj, pending_output_section_name,
                                                  section_reorder_target) != 0) {
                lds_report_error(include_stack, &tok, "failed to apply SECTIONS output order");
                lds_tok_free(&tok);
                free(buf);
                free(pending_assign_name);
                free(pending_output_section_name);
                strvec_pop(include_stack);
                return -1;
            }
            free(pending_output_section_name);
            pending_output_section_name = NULL;
        } else if (sections_block_depth == 1 && tok.kind == LDS_TOK_IDENT && tok.text != NULL && tok.text[0] == '.' &&
                   strcmp(tok.text, ".") != 0) {
            free(pending_output_section_name);
            pending_output_section_name = xstrdup(tok.text);
            if (pending_output_section_name == NULL) {
                lds_tok_free(&tok);
                free(buf);
                free(pending_assign_name);
                free(pending_output_section_name);
                strvec_pop(include_stack);
                return -1;
            }
        } else if (pending_output_section_name != NULL &&
                   (tok.kind == LDS_TOK_SEMI || tok.kind == LDS_TOK_LBRACE || tok.kind == LDS_TOK_RBRACE)) {
            free(pending_output_section_name);
            pending_output_section_name = NULL;
        }
        if (!pending_assign_active && !pending_assign_wait_eq &&
            brace_depth == 0 && paren_depth == 0 && tok.kind == LDS_TOK_IDENT && tok.text != NULL) {
            if (strcmp(tok.text, "SECTIONS") == 0) {
                if (lds_ast_push(ast, LDS_AST_SECTIONS, NULL, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    free(pending_output_section_name);
                    return -1;
                }
                pending_block_expect_lbrace = 1;
                pending_sections_block = 1;
                lds_tok_free(&tok);
                continue;
            }
            if (strcmp(tok.text, "PHDRS") == 0) {
                if (lds_ast_push(ast, LDS_AST_PHDRS, NULL, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    free(pending_output_section_name);
                    return -1;
                }
                pending_block_expect_lbrace = 1;
                lds_tok_free(&tok);
                continue;
            }
            if (strcmp(tok.text, "MEMORY") == 0) {
                if (lds_ast_push(ast, LDS_AST_MEMORY, NULL, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    free(pending_output_section_name);
                    return -1;
                }
                pending_block_expect_lbrace = 1;
                lds_tok_free(&tok);
                continue;
            }
            if (strcmp(tok.text, "ASSERT") == 0) {
                if (lds_ast_push(ast, LDS_AST_ASSERT, NULL, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    free(pending_output_section_name);
                    return -1;
                }
                if (parse_script_assert_directive(&lx, include_stack, ctx, obj, apply_semantics, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    return -1;
                }
                lds_tok_free(&tok);
                continue;
            }
            if (strcmp(tok.text, "PROVIDE") == 0) {
                if (parse_script_provide_directive(&lx, include_stack, ctx, obj, apply_semantics, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    return -1;
                }
                lds_tok_free(&tok);
                continue;
            }
            if (strcmp(tok.text, "INSERT") == 0) {
                if (parse_script_insert_directive(&lx, include_stack, &tok) != 0) {
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    return -1;
                }
                lds_tok_free(&tok);
                continue;
            }
            free(pending_assign_name);
            pending_assign_name = xstrdup(tok.text);
            pending_assign_wait_eq = pending_assign_name != NULL;
            pending_assign_active = 0;
            lds_tok_free(&assign_name_tok);
            if (lds_tok_dup(&assign_name_tok, &tok) != 0) {
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                free(pending_output_section_name);
                return -1;
            }
        }
        if (tok.kind == LDS_TOK_LBRACE) {
            int started_sections_block = 0;
            brace_depth++;
            if (pending_block_expect_lbrace) {
                pending_block_expect_lbrace = 0;
                pending_block_depth = 1;
                if (pending_sections_block) {
                    sections_block_depth = 1;
                    pending_sections_block = 0;
                    started_sections_block = 1;
                }
            } else if (pending_block_depth > 0) {
                pending_block_depth++;
            }
            if (sections_block_depth > 0 && !started_sections_block) {
                sections_block_depth++;
            }
        } else if (tok.kind == LDS_TOK_RBRACE) {
            if (brace_depth == 0) {
                lds_report_error(include_stack, &tok, "unexpected '}'");
                    lds_tok_free(&tok);
                    free(buf);
                    strvec_pop(include_stack);
                    free(pending_output_section_name);
                    return -1;
                }
            if (pending_block_depth > 0) {
                pending_block_depth--;
            }
            if (sections_block_depth > 0) {
                sections_block_depth--;
            }
            brace_depth--;
        } else if (tok.kind == LDS_TOK_LPAREN) {
            paren_depth++;
        } else if (tok.kind == LDS_TOK_RPAREN) {
            if (paren_depth == 0) {
                lds_report_error(include_stack, &tok, "unexpected ')'");
                lds_tok_free(&tok);
                free(buf);
                strvec_pop(include_stack);
                free(pending_output_section_name);
                return -1;
            }
            paren_depth--;
        }
        if (pending_block_expect_lbrace && tok.kind != LDS_TOK_SEMI && tok.kind != LDS_TOK_COMMA) {
            lds_report_error(include_stack, &tok, "expected '{' after block keyword");
            lds_tok_free(&tok);
            free(pending_assign_name);
            free(buf);
            strvec_pop(include_stack);
            return -1;
        }
        if (pending_assign_wait_eq) {
            if (tok.kind == LDS_TOK_EQUAL) {
                lds_tok_t fake = tok;
                if (lds_ast_push(ast, LDS_AST_ASSIGN, pending_assign_name, &fake) != 0) {
                    lds_tok_free(&tok);
                    free(pending_assign_name);
                    free(buf);
                    strvec_pop(include_stack);
                    return -1;
                }
                pending_assign_wait_eq = 0;
                pending_assign_active = 1;
                lds_tokvec_free(&assign_expr_tokens);
                free(pending_assign_name);
                pending_assign_name = NULL;
            } else if (tok.kind != LDS_TOK_IDENT && tok.kind != LDS_TOK_NUMBER && tok.kind != LDS_TOK_STRING) {
                pending_assign_wait_eq = 0;
                free(pending_assign_name);
                pending_assign_name = NULL;
            }
        } else if (pending_assign_active && brace_depth == 0 && paren_depth == 0 && tok.kind == LDS_TOK_SEMI) {
            if (apply_script_assignment(include_stack, &assign_name_tok, assign_name_tok.text, &assign_expr_tokens,
                                        ctx, obj, apply_semantics, 0) != 0) {
                lds_tok_free(&tok);
                free(buf);
                free(pending_assign_name);
                free(pending_output_section_name);
                lds_tok_free(&assign_name_tok);
                lds_tokvec_free(&assign_expr_tokens);
                strvec_pop(include_stack);
                return -1;
            }
            lds_tokvec_free(&assign_expr_tokens);
            pending_assign_active = 0;
        } else if (pending_assign_active && lds_tokvec_push(&assign_expr_tokens, &tok) != 0) {
            lds_tok_free(&tok);
            free(buf);
            free(pending_assign_name);
            free(pending_output_section_name);
            lds_tok_free(&assign_name_tok);
            lds_tokvec_free(&assign_expr_tokens);
            strvec_pop(include_stack);
            return -1;
        }
        lds_tok_free(&tok);
    }

    free(buf);
    free(pending_assign_name);
    free(pending_output_section_name);
    lds_tok_free(&assign_name_tok);
    lds_tokvec_free(&assign_expr_tokens);
    if (brace_depth != 0 || paren_depth != 0) {
        lds_tok_t fake;
        memset(&fake, 0, sizeof(fake));
        fake.path = path;
        fake.line = lx.line;
        fake.col = lx.col;
        lds_report_error(include_stack, &fake,
                         brace_depth != 0 ? "unexpected end-of-file: missing '}'"
                                          : "unexpected end-of-file: missing ')'");
        strvec_pop(include_stack);
        return -1;
    }
    strvec_pop(include_stack);
    return 0;
}

static int parse_linker_script_file(const char *path, ld_ctx_t *ctx, const elfobj_t *obj, int apply_semantics) {
    strvec_t include_stack;
    lds_ast_t ast;
    size_t section_reorder_target = 0;
    int rc;

    memset(&include_stack, 0, sizeof(include_stack));
    memset(&ast, 0, sizeof(ast));
    rc = parse_linker_script_file_rec(path, &include_stack, &ast, ctx, obj, apply_semantics,
                                      apply_semantics ? &section_reorder_target : NULL, 0);
    lds_ast_free(&ast);
    strvec_free(&include_stack);
    return rc;
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

static uint32_t phdr_default_flags(uint32_t type) {
    enum {
        LD_PF_X = 0x1u,
        LD_PF_W = 0x2u,
        LD_PF_R = 0x4u
    };

    if (type == PT_LOAD) {
        return LD_PF_R;
    }
    if (type == PT_DYNAMIC || type == PT_TLS || type == PT_GNU_STACK) {
        return LD_PF_R | LD_PF_W;
    }
    return LD_PF_R;
}

static int parse_script_phdr_and_map_plan(const char *path, lds_phdr_vec_t *phdrs, lds_sec_phdr_vec_t *maps) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    lds_lexer_t lx;
    lds_tok_t tok;
    int brace_depth = 0;
    int phdrs_depth = 0;
    int sections_depth = 0;
    int section_body_depth = 0;
    char *active_section = NULL;

    if (path == NULL || phdrs == NULL || maps == NULL) {
        return -1;
    }
    if (read_file(path, &buf, &sz) != 0) {
        return -1;
    }
    memset(&lx, 0, sizeof(lx));
    lx.buf = buf;
    lx.len = sz;
    lx.path = path;
    lx.line = 1;
    lx.col = 1;

    for (;;) {
        if (lds_lex_next(&lx, &tok) != 0) {
            free(buf);
            free(active_section);
            return -1;
        }
        if (tok.kind == LDS_TOK_EOF) {
            lds_tok_free(&tok);
            break;
        }
        if (tok.kind == LDS_TOK_IDENT && tok.text != NULL && strcmp(tok.text, "PHDRS") == 0) {
            lds_tok_t t2;
            if (lds_lex_next(&lx, &t2) != 0) {
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            if (t2.kind == LDS_TOK_LBRACE) {
                phdrs_depth = 1;
                brace_depth++;
            }
            lds_tok_free(&t2);
            lds_tok_free(&tok);
            continue;
        }
        if (tok.kind == LDS_TOK_IDENT && tok.text != NULL && strcmp(tok.text, "SECTIONS") == 0) {
            lds_tok_t t2;
            if (lds_lex_next(&lx, &t2) != 0) {
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            if (t2.kind == LDS_TOK_LBRACE) {
                sections_depth = 1;
                brace_depth++;
            }
            lds_tok_free(&t2);
            lds_tok_free(&tok);
            continue;
        }

        if (phdrs_depth == 1 && tok.kind == LDS_TOK_IDENT && tok.text != NULL) {
            char *phdr_name = xstrdup(tok.text);
            lds_tok_t type_tok;
            uint32_t type;
            uint32_t flags;
            uint64_t align = 0x1000;
            int done = 0;
            if (phdr_name == NULL) {
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            if (lds_lex_next(&lx, &type_tok) != 0) {
                free(phdr_name);
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            if (type_tok.kind != LDS_TOK_IDENT || type_tok.text == NULL ||
                phdr_type_from_token(type_tok.text, &type) != 0) {
                lds_tok_free(&type_tok);
                free(phdr_name);
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            flags = phdr_default_flags(type);
            lds_tok_free(&type_tok);

            while (!done) {
                lds_tok_t at;
                if (lds_lex_next(&lx, &at) != 0) {
                    free(phdr_name);
                    lds_tok_free(&tok);
                    free(buf);
                    free(active_section);
                    return -1;
                }
                if (at.kind == LDS_TOK_IDENT && at.text != NULL && strcmp(at.text, "FLAGS") == 0) {
                    lds_tok_t lp;
                    lds_tokvec_t expr;
                    lds_eval_ctx_t ec;
                    uint64_t v = 0;
                    int pdepth = 1;

                    memset(&expr, 0, sizeof(expr));
                    if (lds_lex_next(&lx, &lp) != 0 || lp.kind != LDS_TOK_LPAREN) {
                        lds_tok_free(&lp);
                        lds_tok_free(&at);
                        free(phdr_name);
                        lds_tok_free(&tok);
                        free(buf);
                        free(active_section);
                        return -1;
                    }
                    lds_tok_free(&lp);
                    while (pdepth > 0) {
                        lds_tok_t et;
                        if (lds_lex_next(&lx, &et) != 0) {
                            lds_tok_free(&at);
                            free(phdr_name);
                            lds_tok_free(&tok);
                            lds_tokvec_free(&expr);
                            free(buf);
                            free(active_section);
                            return -1;
                        }
                        if (et.kind == LDS_TOK_LPAREN) {
                            pdepth++;
                            if (lds_tokvec_push(&expr, &et) != 0) {
                                lds_tok_free(&et);
                                lds_tok_free(&at);
                                free(phdr_name);
                                lds_tok_free(&tok);
                                lds_tokvec_free(&expr);
                                free(buf);
                                free(active_section);
                                return -1;
                            }
                        } else if (et.kind == LDS_TOK_RPAREN) {
                            pdepth--;
                            if (pdepth > 0 && lds_tokvec_push(&expr, &et) != 0) {
                                lds_tok_free(&et);
                                lds_tok_free(&at);
                                free(phdr_name);
                                lds_tok_free(&tok);
                                lds_tokvec_free(&expr);
                                free(buf);
                                free(active_section);
                                return -1;
                            }
                        } else if (lds_tokvec_push(&expr, &et) != 0) {
                            lds_tok_free(&et);
                            lds_tok_free(&at);
                            free(phdr_name);
                            lds_tok_free(&tok);
                            lds_tokvec_free(&expr);
                            free(buf);
                            free(active_section);
                            return -1;
                        }
                        lds_tok_free(&et);
                    }
                    memset(&ec, 0, sizeof(ec));
                    if (expr.count > 0 && lds_eval_expr_slice(&ec, expr.items, 0, expr.count, &v) == 0) {
                        flags = (uint32_t)(v & 0x7u);
                    }
                    lds_tokvec_free(&expr);
                } else if (at.kind == LDS_TOK_SEMI) {
                    done = 1;
                }
                lds_tok_free(&at);
            }

            if (type != PT_PHDR && type != PT_GNU_STACK) {
                if (type != PT_LOAD) {
                    align = 8;
                }
            } else {
                align = 8;
            }
            if (lds_phdr_vec_push(phdrs, phdr_name, type, flags, align) != 0) {
                free(phdr_name);
                lds_tok_free(&tok);
                free(buf);
                free(active_section);
                return -1;
            }
            free(phdr_name);
            lds_tok_free(&tok);
            continue;
        }

        if (sections_depth == 1 && tok.kind == LDS_TOK_IDENT && tok.text != NULL && tok.text[0] == '.') {
            free(active_section);
            active_section = xstrdup(tok.text);
            section_body_depth = 0;
        } else if (sections_depth >= 2 && tok.kind == LDS_TOK_RBRACE && section_body_depth == sections_depth) {
            section_body_depth = sections_depth - 1;
        } else if (sections_depth >= 1 && section_body_depth == 1 && tok.kind == LDS_TOK_COLON && active_section != NULL) {
            lds_tok_t p;
            if (lds_lex_next(&lx, &p) == 0) {
                if (p.kind == LDS_TOK_IDENT && p.text != NULL &&
                    lds_sec_phdr_vec_push(maps, active_section, p.text) != 0) {
                    lds_tok_free(&p);
                    lds_tok_free(&tok);
                    free(buf);
                    free(active_section);
                    return -1;
                }
                lds_tok_free(&p);
            }
            free(active_section);
            active_section = NULL;
            section_body_depth = 0;
        }

        if (tok.kind == LDS_TOK_LBRACE) {
            brace_depth++;
            if (phdrs_depth > 0) {
                phdrs_depth++;
            }
            if (sections_depth > 0) {
                sections_depth++;
                if (active_section != NULL && section_body_depth == 0) {
                    section_body_depth = sections_depth;
                }
            }
        } else if (tok.kind == LDS_TOK_RBRACE) {
            if (brace_depth > 0) {
                brace_depth--;
            }
            if (phdrs_depth > 0) {
                phdrs_depth--;
            }
            if (sections_depth > 0) {
                sections_depth--;
            }
            if (sections_depth == 0) {
                free(active_section);
                active_section = NULL;
                section_body_depth = 0;
            }
        } else if (tok.kind == LDS_TOK_SEMI && sections_depth == 1) {
            free(active_section);
            active_section = NULL;
            section_body_depth = 0;
        }
        lds_tok_free(&tok);
    }

    free(buf);
    free(active_section);
    return 0;
}

static int add_script_segments_from_plan(elfobj_t *obj, const ld_ctx_t *ctx, const lds_phdr_vec_t *phdrs,
                                         const lds_sec_phdr_vec_t *maps) {
    elf_segment_t **segs;
    size_t i;

    if (obj == NULL || ctx == NULL || phdrs == NULL || phdrs->count == 0) {
        return 0;
    }
    if (elf_add_segment(obj, PT_PHDR, 0x4u, 8) == NULL) {
        return -1;
    }
    if (ctx->interp_path != NULL && ctx->interp_path[0] != '\0') {
        if (elf_add_interp_segment(obj, ctx->interp_path) == NULL) {
            return -1;
        }
    }
    segs = (elf_segment_t **)calloc(phdrs->count, sizeof(*segs));
    if (segs == NULL) {
        return -1;
    }
    for (i = 0; i < phdrs->count; ++i) {
        segs[i] = elf_add_segment(obj, phdrs->items[i].type, phdrs->items[i].flags, phdrs->items[i].align);
        if (segs[i] == NULL) {
            free(segs);
            return -1;
        }
    }
    for (i = 0; i < maps->count; ++i) {
        int pidx = lds_phdr_vec_find(phdrs, maps->items[i].phdr_name);
        elf_section_t *sec;
        if (pidx < 0) {
            continue;
        }
        sec = elf_find_section(obj, maps->items[i].section_name);
        if (sec == NULL) {
            continue;
        }
        if (elf_segment_add_section(segs[(size_t)pidx], sec) != ELF_OK) {
            free(segs);
            return -1;
        }
    }
    free(segs);
    return 1;
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

static int run_cmd_first_line(const char *cmd, char *out, size_t out_sz) {
    FILE *fp;
    char *nl;
    int rc;

    if (cmd == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';
    fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(out, (int)out_sz, fp) == NULL) {
        rc = pclose(fp);
        (void)rc;
        out[0] = '\0';
        return 1;
    }
    rc = pclose(fp);
    if (rc != 0) {
        out[0] = '\0';
        return -1;
    }
    nl = strchr(out, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return out[0] != '\0' ? 0 : 1;
}

static int discover_default_plugin(ld_ctx_t *ctx) {
    static char discovered[PATH_MAX];
    const char *envp;
    char cmd[256];
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

    snprintf(cmd, sizeof(cmd), "gcc -print-file-name=liblto_plugin.so 2>/dev/null");
    rc = run_cmd_first_line(cmd, discovered, sizeof(discovered));
    if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
        ctx->plugin_path = discovered;
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "clang -print-file-name=LLVMgold.so 2>/dev/null");
    rc = run_cmd_first_line(cmd, discovered, sizeof(discovered));
    if (rc == 0 && discovered[0] == '/' && access(discovered, R_OK | X_OK) == 0) {
        ctx->plugin_path = discovered;
        return 0;
    }

    return 0;
}

static int plugin_discover_and_handshake(ld_ctx_t *ctx) {
    int status;
    pid_t pid;

    if (ctx == NULL || ctx->plugin_checked) {
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
    if (pid < 0) {
        fprintf(stderr, "ld: fork failed\n");
        return -1;
    }

    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) {
                close(fd);
            }
        }

        char *args[] = {(char *)ctx->plugin_path, "--version", NULL};
        execv(ctx->plugin_path, args);
        exit(127);
    }

    if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "ld: plugin handshake failed for %s\n", ctx->plugin_path);
        return -1;
    }

    ctx->plugin_checked = 1;
    return 0;
}

static int plugin_materialize_object(const ld_ctx_t *ctx, const char *in_path, char *out_path, size_t out_path_sz) {
    FILE *fp;
    char *nl;
    size_t i;
    int pfd[2];
    pid_t pid;
    int status;
    char **args;
    size_t args_cap;
    size_t args_count;
    char *plugin_opt_prefix = "--plugin-opt=";
    char **plugin_opt_args;

    if (out_path == NULL || out_path_sz == 0) {
        return -1;
    }
    out_path[0] = '\0';
    if (ctx == NULL || ctx->plugin_path == NULL || ctx->plugin_path[0] == '\0' || in_path == NULL) {
        return 0;
    }

    args_cap = 5 + ctx->plugin_opt_count;
    args = (char **)malloc(args_cap * sizeof(char *));
    if (args == NULL) {
        return -1;
    }

    plugin_opt_args = (char **)malloc(ctx->plugin_opt_count * sizeof(char *));
    if (plugin_opt_args == NULL && ctx->plugin_opt_count > 0) {
        free(args);
        return -1;
    }

    args_count = 0;
    args[args_count++] = (char *)ctx->plugin_path;
    args[args_count++] = "--materialize";
    args[args_count++] = (char *)in_path;

    for (i = 0; i < ctx->plugin_opt_count; ++i) {
        size_t len = strlen(plugin_opt_prefix) + strlen(ctx->plugin_opts[i]) + 1;
        plugin_opt_args[i] = (char *)malloc(len);
        if (plugin_opt_args[i] == NULL) {
            size_t j;
            for (j = 0; j < i; ++j) {
                free(plugin_opt_args[j]);
            }
            free(plugin_opt_args);
            free(args);
            return -1;
        }
        snprintf(plugin_opt_args[i], len, "%s%s", plugin_opt_prefix, ctx->plugin_opts[i]);
        args[args_count++] = plugin_opt_args[i];
    }
    args[args_count] = NULL;

    if (pipe(pfd) < 0) {
        for (i = 0; i < ctx->plugin_opt_count; ++i) free(plugin_opt_args[i]);
        free(plugin_opt_args);
        free(args);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        for (i = 0; i < ctx->plugin_opt_count; ++i) free(plugin_opt_args[i]);
        free(plugin_opt_args);
        free(args);
        return -1;
    }

    if (pid == 0) {
        close(pfd[0]);
        if (pfd[1] != STDOUT_FILENO) {
            dup2(pfd[1], STDOUT_FILENO);
            close(pfd[1]);
        }
        execv(ctx->plugin_path, args);
        exit(127);
    }

    close(pfd[1]);

    fp = fdopen(pfd[0], "r");
    if (fp == NULL) {
        close(pfd[0]);
        waitpid(pid, &status, 0);
        for (i = 0; i < ctx->plugin_opt_count; ++i) free(plugin_opt_args[i]);
        free(plugin_opt_args);
        free(args);
        return -1;
    }

    if (fgets(out_path, (int)out_path_sz, fp) == NULL) {
        fclose(fp);
        waitpid(pid, &status, 0);
        out_path[0] = '\0';
        for (i = 0; i < ctx->plugin_opt_count; ++i) free(plugin_opt_args[i]);
        free(plugin_opt_args);
        free(args);
        return 0;
    }

    fclose(fp);
    waitpid(pid, &status, 0);

    for (i = 0; i < ctx->plugin_opt_count; ++i) free(plugin_opt_args[i]);
    free(plugin_opt_args);
    free(args);

    nl = strchr(out_path, '\n');
    if (nl != NULL) {
        *nl = '\0';
    }
    return out_path[0] != '\0' ? 1 : 0;
}

static void trim_trailing(char *s) {
    size_t n;
    while ((n = strlen(s)) > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            s[n - 1] = '\0';
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

    if (ctx == NULL || ctx->explicit_mode || loaded_count != 0) {
        return;
    }
    detected = detect_object_mode(obj);
    if (detected == 0 || detected == ctx->mode) {
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
    int changed_any = 0;
    int thin = 0;
    symset_t seen_members;
    int pass_progress;
    int pass_count = 0;

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
            char member_key[96];
            int is_special = 0;
            int has_member_payload = 0;

            if (hdr[58] != '`' || hdr[59] != '\n') {
                break;
            }
            if (parse_u64_dec((const char *)hdr + 48, 10, &msize) != 0) {
                break;
            }
            off += 60;
            if (off > sz) {
                break;
            }
            mdata = buf + off;
            mname = decode_ar_name((const char *)hdr, mdata, msize, strtab, strtab_sz, &name_extra);
            if (mname == NULL) {
                break;
            }
            is_special = strcmp(mname, "/") == 0 ||
                         strcmp(mname, "//") == 0 ||
                         strcmp(mname, "__.SYMDEF") == 0 ||
                         strcmp(mname, "__.SYMDEF SORTED") == 0;
            has_member_payload = !thin || is_special;
            if (has_member_payload && off + (size_t)msize > sz) {
                free(mname);
                break;
            }
            if (has_member_payload && name_extra > (size_t)msize) {
                free(mname);
                break;
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
                snprintf(member_key, sizeof(member_key), "%s@%zu", path, off);
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
                        if (objvec_push(objs, obj, member_name) != 0 || symstate_note_object(state, obj) != 0) {
                            elf_close(obj);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        pass_progress = 1;
                        changed_any = 1;
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
                        changed_any = 1;
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
    } while (!whole_archive && pass_progress);

    symset_free(&seen_members);
    free(buf);
    if (!whole_archive && !changed_any && state != NULL) {
        return 0;
    }
    return 0;
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
    if (objvec_push(objs, obj, path) != 0 || symstate_note_object(state, obj) != 0) {
        elf_close(obj);
        return -1;
    }
    return 0;
}

static char *resolve_library_path_suffix_ex(const ld_ctx_t *ctx, const char *name, const char *suffix,
                                            int include_default_dirs) {
    static const char *default_dirs[] = {
        "/usr/lib",
        "/usr/local/lib"
    };
    char leaf[512];
    size_t i;

    snprintf(leaf, sizeof(leaf), "lib%s%s", name, suffix != NULL ? suffix : "");
    for (i = 0; i < ctx->lib_paths.count; ++i) {
        char *cand = path_join(ctx->lib_paths.items[i], leaf);
        if (cand != NULL && access(cand, R_OK) == 0) {
            return cand;
        }
        free(cand);
    }
    if (!include_default_dirs) {
        return NULL;
    }
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && access(cand, R_OK) == 0) {
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

static int load_path_input(const char *path, ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                           int whole_archive, int quiet) {
    if (has_suffix(path, ".a")) {
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

static const char *resolve_version_need_provider(const ld_ctx_t *ctx, const char *base, size_t base_len,
                                                 const char *ver_name) {
    size_t i;

    if (ctx == NULL || base == NULL || base_len == 0 || ver_name == NULL || ver_name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        const char *path = ctx->dso_inputs.items[i];
        const char *leaf;
        if (path == NULL || path[0] == '\0') {
            continue;
        }
        if (!dso_has_versioned_export(ctx, path, base, base_len, ver_name)) {
            continue;
        }
        leaf = strrchr(path, '/');
        return leaf != NULL ? leaf + 1 : path;
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
        const char *leaf;
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
        leaf = strrchr(path, '/');
        *out_provider = leaf != NULL ? leaf + 1 : path;
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

static int reloc_is_i386_plt_ref(uint32_t type) {
    return type == R_386_PLT32;
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

static size_t count_runtime_data_import_relocs_x64(elfobj_t *out) {
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
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            if (reloc_is_x64_runtime_data_ref(elf_reloc_type(rel))) {
                n++;
            }
        }
    }
    return n;
}

static size_t count_runtime_data_import_relocs_i386(elfobj_t *out) {
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
            if (!is_runtime_import_symbol(sym)) {
                continue;
            }
            if (reloc_is_i386_runtime_data_ref(elf_reloc_type(rel))) {
                n++;
            }
        }
    }
    return n;
}

static int collect_dynamic_imports_x64(elfobj_t *out, dyn_import_vec_t *imports) {
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
                plt_ref = 1;
            }
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

static int collect_dynamic_imports_i386(elfobj_t *out, dyn_import_vec_t *imports) {
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
    if (ctx->mode == 64) {
        if (collect_dynamic_imports_x64(out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_x64(out);
    } else {
        if (collect_dynamic_imports_i386(out, &ctx->dyn_imports) != 0) {
            return -1;
        }
        extra_dyn_relocs = count_runtime_data_import_relocs_i386(out);
    }
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

    if (out == NULL || imports == NULL || imports->count == 0) {
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
    runtime_extra_count = count_runtime_data_import_relocs_x64(out);
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
            continue;
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
                const uint8_t *sbuf;
                size_t ssz = 0;
                uint64_t off;

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                if (!is_runtime_import_symbol(sym)) {
                    continue;
                }
                type = elf_reloc_type(rel);
                if (!reloc_is_x64_runtime_data_ref(type)) {
                    continue;
                }
                if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                    continue;
                }
                off = elf_reloc_offset(rel);
                slot_addr = elf_section_addr(sec) + off;
                if (elf_reloc_has_addend(rel)) {
                    addend = elf_reloc_addend(rel);
                } else {
                    sbuf = (const uint8_t *)elf_section_data(sec, &ssz);
                    if (sbuf == NULL || off + 8 > ssz) {
                        continue;
                    }
                    addend = (int64_t)read_u64_endian(sbuf + off, e);
                }
                roff = (rela_dyn_base_count + extra_idx) * 24;
                extra_idx++;
                if (rela_dyn_buf == NULL || roff + 24 > rela_dyn_sz) {
                    return -1;
                }
                write_u64_endian(rela_dyn_buf + roff + 0, e, slot_addr);
                write_u64_endian(rela_dyn_buf + roff + 8, e, (((uint64_t)dynidx) << 32) | R_X86_64_64);
                write_u64_endian(rela_dyn_buf + roff + 16, e, (uint64_t)addend);
            }
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

    if (out == NULL || imports == NULL || imports->count == 0) {
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
    runtime_extra_count = count_runtime_data_import_relocs_i386(out);
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
            continue;
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
                plt_buf[poff + 6] = 0x68;
                write_u32_endian(plt_buf + poff + 7, e, (uint32_t)ent);
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

                if (rel == NULL) {
                    continue;
                }
                sym = elf_reloc_symbol(rel);
                if (!is_runtime_import_symbol(sym)) {
                    continue;
                }
                type = elf_reloc_type(rel);
                if (!reloc_is_i386_runtime_data_ref(type)) {
                    continue;
                }
                if (dynsym_index_by_name(out, elf_symbol_name(sym), &dynidx) != 0) {
                    continue;
                }
                slot_addr = elf_section_addr(sec) + elf_reloc_offset(rel);
                roff = (rel_dyn_base_count + extra_idx) * 8;
                extra_idx++;
                if (rel_dyn_buf == NULL || roff + 8 > rel_dyn_sz) {
                    return -1;
                }
                write_u32_endian(rel_dyn_buf + roff + 0, e, (uint32_t)slot_addr);
                write_u32_endian(rel_dyn_buf + roff + 4, e, (dynidx << 8) | R_386_32);
            }
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
        const char *p = strrchr(ctx->dso_inputs.items[i], '/');
        const char *name = p != NULL ? p + 1 : ctx->dso_inputs.items[i];
        uint32_t off = 0;
        if (dynstr_append_cstr(&dynstr_buf, &dynstr_len, &dynstr_cap, name, &off) != 0) {
            free(dynstr_buf);
            free(hash_buf);
            free(gnu_hash_buf);
            return -1;
        }
        if (dynamic_append_entry(&dynamic_buf, &dynamic_len, &dynamic_cap,
                                 elf_class(out), elf_endian(out), DT_NEEDED, off) != 0) {
            free(dynstr_buf);
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
    emit_versym = verdef_count != 0 || verneed_count != 0;
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
    if (path_so != NULL && load_path_input(path_so, ctx, objs, state, in->whole_archive, 1) == 0) {
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
        return resolve_symbol_addr(obj, sym, allow_undef, out_addr, undef_name);
    }
    if (ctx != NULL && ctx->mode == 64) {
        int plt_ref = reloc_is_x64_plt_ref(type);
        if (!plt_ref && type == R_X86_64_PC32 &&
            (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE)) {
            plt_ref = 1;
        }
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
        if (reloc_is_i386_plt_ref(type) && imp->need_plt) {
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
    return imp != NULL;
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
            const char *nm = elf_section_name(sec);
            if (sec == NULL) {
                continue;
            }
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

    if (!add_u64_checked(ehsize, phnum * phentsz, &off) || !add_u64_checked(base_vaddr, off, &mem_end)) {
        return -1;
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
    if (name != NULL) {
        if (strcmp(name, ".tm_clone_table") == 0) {
            return 31;
        }
        if (strcmp(name, ".fini_array") == 0 || strcmp(name, ".init_array") == 0 ||
            strcmp(name, ".preinit_array") == 0) {
            return 32;
        }
        if (strcmp(name, ".got") == 0) {
            return 33;
        }
        if (strcmp(name, ".dynamic") == 0) {
            return 34;
        }
        if (strcmp(name, ".got.plt") == 0) {
            return 35;
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
        return 30;
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

static int add_default_segments(elfobj_t *obj, const ld_ctx_t *ctx) {
    enum {
        LD_PF_X = 0x1u,
        LD_PF_W = 0x2u,
        LD_PF_R = 0x4u
    };
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
    lds_phdr_vec_t script_phdrs;
    lds_sec_phdr_vec_t script_maps;
    int applied = 0;

    if (obj == NULL || ctx == NULL) {
        return -1;
    }
    memset(&script_phdrs, 0, sizeof(script_phdrs));
    memset(&script_maps, 0, sizeof(script_maps));
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        if (parse_script_phdr_and_map_plan(ctx->script_path, &script_phdrs, &script_maps) == 0 &&
            script_phdrs.count > 0) {
            applied = add_script_segments_from_plan(obj, ctx, &script_phdrs, &script_maps);
            lds_phdr_vec_free(&script_phdrs);
            lds_sec_phdr_vec_free(&script_maps);
            if (applied < 0) {
                return -1;
            }
            if (applied > 0) {
                return 0;
            }
        } else {
            lds_phdr_vec_free(&script_phdrs);
            lds_sec_phdr_vec_free(&script_maps);
        }
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

static int has_text_relocation(const elfobj_t *obj, const char **out_sec_name) {
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
        if ((elf_section_flags(sec) & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR)) {
            continue;
        }
        if (elf_section_reloc_count(sec) == 0) {
            continue;
        }
        name = elf_section_name(sec);
        if (out_sec_name != NULL) {
            *out_sec_name = name;
        }
        return 1;
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

static const elf_symbol_t *find_fallback_entry_symbol(elfobj_t *obj) {
    const elf_symbol_t *sym;
    size_t i;

    sym = elf_find_symbol(obj, "start");
    if (symbol_is_defined(sym)) {
        return sym;
    }
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        sym = elf_symbol_at(obj, i);
        if (!symbol_is_defined(sym)) {
            continue;
        }
        if (elf_symbol_type(sym) != STT_FUNC && elf_symbol_type(sym) != STT_NOTYPE) {
            continue;
        }
        if (elf_symbol_bind(sym) != STB_GLOBAL && elf_symbol_bind(sym) != STB_WEAK) {
            continue;
        }
        if (elf_symbol_name(sym) == NULL || elf_symbol_name(sym)[0] == '\0') {
            continue;
        }
        return sym;
    }
    return NULL;
}

static int set_entry_symbol(elfobj_t *obj, const char *entry_symbol, int require_entry, int entry_explicit) {
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
            sym = find_fallback_entry_symbol(obj);
            if (!symbol_is_defined(sym)) {
                elf_section_t *text = elf_find_section(obj, ".text");
                if (text != NULL && (elf_section_flags(text) & SHF_ALLOC) != 0) {
                    addr = elf_section_addr(text);
                    if (elf_set_entry(obj, addr) != ELF_OK) {
                        fprintf(stderr, "ld: failed to set fallback .text entry address\n");
                        return -1;
                    }
                    return 0;
                }
                fprintf(stderr, "ld: entry symbol '%s' not found (no fallback entry)\n", entry_symbol);
                return -1;
            }
            if (resolve_symbol_addr(obj, sym, 0, &addr, NULL) != 0) {
                fprintf(stderr, "ld: failed to resolve fallback entry symbol\n");
                return -1;
            }
            if (elf_set_entry(obj, addr) != ELF_OK) {
                fprintf(stderr, "ld: failed to set fallback entry address\n");
                return -1;
            }
            return 0;
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
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
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
    }
    return 0;
}

static int validate_output(const ld_ctx_t *ctx) {
    elfobj_t *obj = NULL;

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

    err = elf_link(inputs.objs, inputs.count, &out);
    if (err != ELF_OK || out == NULL) {
        fprintf(stderr, "ld: link merge failed: %s\n", out != NULL ? elf_last_diagnostics(out) : elf_errstr(err));
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        if (out != NULL) {
            elf_close(out);
        }
        return -1;
    }

    out_type = ctx->expect_type == 0 ? ET_EXEC : ctx->expect_type;
    allow_undef_runtime = ctx->allow_undefined;
    if (out_type == ET_DYN && !ctx->explicit_unresolved_policy) {
        allow_undef_runtime = 1;
    }
    if (elf_set_type(out, out_type) != ELF_OK) {
        fprintf(stderr, "ld: failed to set output type\n");
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (ctx->script_path != NULL) {
        if (parse_linker_script_file(ctx->script_path, ctx, out, 1) != 0) {
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
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
    if (ctx->script_path != NULL) {
        if (parse_linker_script_file(ctx->script_path, ctx, out, 1) != 0) {
            symref_map_free(&undef_refs);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
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
        if (chmod(ctx->out_path, 0644) != 0) {
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

    if (out_type == ET_EXEC && ensure_substrate_ld_note(out) != 0) {
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

    if (plan_dynamic_imports(ctx, out) != 0) {
        fprintf(stderr, "ld: failed to plan GOT/PLT dynamic imports\n");
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

    if (assign_section_addresses(out, base_vaddr) != 0) {
        fprintf(stderr, "ld: failed to assign section virtual addresses\n");
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
        const char *textrel_sec = NULL;
        if (has_text_relocation(out, &textrel_sec)) {
            if (ctx->z_text_mode == 1) {
                fprintf(stderr,
                        "ld: -z text rejects text relocations (section %s has pending relocations)\n",
                        textrel_sec != NULL ? textrel_sec : "<unknown>");
                symref_map_free(&undef_refs);
                objvec_free(&inputs);
                elf_close(out);
                return -1;
            }
            if (ctx->z_text_mode == 2) {
                fprintf(stderr,
                        "ld: -z notext: allowing text relocations in section %s\n",
                        textrel_sec != NULL ? textrel_sec : "<unknown>");
            }
        }
    }

    if (check_undefined_symbols(out, ctx, allow_undef_runtime, &undef_refs) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (apply_all_relocations(out, ctx, allow_undef_runtime) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (set_entry_symbol(out, ctx->entry_symbol != NULL ? ctx->entry_symbol : "_start",
                         out_type == ET_EXEC, ctx->entry_symbol != NULL) != 0) {
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
    if (chmod(ctx->out_path, out_type == ET_EXEC ? 0755 : 0644) != 0) {
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

int main(int argc, char **argv) {
    ld_ctx_t ctx;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.out_path = "a.out";
    ctx.self_path = argv[0];
    ctx.compat_mode = LD_COMPAT_GNU;
    ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
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

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
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
        if (strcmp(a, "-shared") == 0 || strcmp(a, "-pie") == 0) {
            ctx.expect_type = ET_DYN;
            continue;
        }
        if (strcmp(a, "-static") == 0) {
            ctx.expect_type = ET_EXEC;
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            continue;
        }
        if (strcmp(a, "-Bstatic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_STATIC;
            continue;
        }
        if (strcmp(a, "-Bdynamic") == 0) {
            ctx.current_lib_mode = LD_LIBMODE_DYNAMIC;
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
        if (strcmp(a, "-dynamic-linker") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ctx.interp_path = argv[++i];
            continue;
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
            if (parse_z_option(&ctx, zval) != 0) {
                fprintf(stderr,
                        "ld: unsupported -z option '%s' (supported: text, notext, execstack, noexecstack, relro, norelro)\n",
                        zval != NULL ? zval : "");
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            continue;
        }
        if (strcmp(a, "--strip-all") == 0 || strcmp(a, "--build-id") == 0 || strcmp(a, "-s") == 0) {
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
        if (inputvec_push(&ctx.inputs, LD_INPUT_FILE, ctx.current_lib_mode,
                          ctx.current_whole_archive, ctx.current_as_needed, a) != 0) {
            inputvec_free(&ctx.inputs);
            strvec_free(&ctx.lib_paths);
            return 1;
        }
    }

    if (ctx.query_version && ctx.inputs.count == 0) {
        printf("Substrate ld (internal) 0.1\n");
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
        if (parse_linker_script_file(ctx.script_path, NULL, NULL, 0) != 0) {
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
    return 0;
}

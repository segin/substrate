#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef STV_DEFAULT
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3
#endif

#ifndef SHT_INIT_ARRAY
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_PREINIT_ARRAY 16
#endif

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
    const char *out_path;
    const char *self_path;
    const char *entry_symbol;
    const char *interp_path;
    const char *map_path;
    ld_compat_mode_t compat_mode;
    ld_lib_mode_t current_lib_mode;
    int current_whole_archive;
    int current_as_needed;
    strvec_t lib_paths;
    strvec_t trace_symbols;
    strvec_t force_undefined;
    defsymvec_t defsyms;
    strvec_t dso_inputs;
    inputvec_t inputs;
} ld_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-m32|-m64|-m <emulation>] [-r|-shared|-pie|-static] "
            "[-o output] [-L dir] [-l name] [-Bstatic|-Bdynamic] "
            "[--start-group ... --end-group] [--whole-archive|--no-whole-archive] "
            "[-e symbol] [--allow-undefined] "
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

static int objvec_push(objvec_t *v, elfobj_t *obj, const char *name) {
    elfobj_t **new_objs;
    char **new_names;
    char *dup;

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
    return 64;
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
    if (mode == 64) {
        return elf_class(obj) == ELFOBJ_CLASS_64 &&
               elf_machine(obj) == EM_X86_64 &&
               elf_endian(obj) == ELFOBJ_ENDIAN_LE;
    }
    return elf_class(obj) == ELFOBJ_CLASS_32 &&
           elf_machine(obj) == EM_386 &&
           elf_endian(obj) == ELFOBJ_ENDIAN_LE;
}

static int validate_relocatable_input(const elfobj_t *obj, const char *display_name) {
    size_t i;

    if (elf_symbol_count(obj) == 0) {
        fprintf(stderr, "ld: input %s has no symbol table entries\n", display_name);
        return -1;
    }

    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
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

static int load_archive_members(const char *path, const ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
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

    memset(&seen_members, 0, sizeof(seen_members));
    if (parse_archive_header(path, &buf, &sz, &thin) != 0) {
        return -1;
    }

    do {
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
                if (!symset_contains(&seen_members, thin_member_path) &&
                    elf_open(thin_member_path, &obj) == ELF_OK) {
                    if (elf_type(obj) == ET_REL && obj_matches_mode(obj, ctx->mode) &&
                        (whole_archive || obj_defines_unresolved(state, obj))) {
                        char member_name[512];
                        if (symset_add(&seen_members, thin_member_path) != 0) {
                            elf_close(obj);
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        snprintf(member_name, sizeof(member_name), "%s(%s)", path, mname);
                        if (validate_relocatable_input(obj, member_name) != 0) {
                            elf_close(obj);
                            free(thin_member_path);
                            free(mname);
                            symset_free(&seen_members);
                            free(buf);
                            return -1;
                        }
                        if (objvec_push(objs, obj, member_name) != 0 || symstate_note_object(state, obj) != 0) {
                            elf_close(obj);
                            free(thin_member_path);
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

static int load_object_input(const char *path, const ld_ctx_t *ctx, objvec_t *objs, symstate_t *state, int quiet) {
    elfobj_t *obj = NULL;

    if (elf_open(path, &obj) != ELF_OK) {
        if (!quiet) {
            fprintf(stderr, "ld: failed to open input %s\n", path);
        }
        return -1;
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

static char *resolve_library_path_suffix(const ld_ctx_t *ctx, const char *name, const char *suffix) {
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
    for (i = 0; i < sizeof(default_dirs) / sizeof(default_dirs[0]); ++i) {
        char *cand = path_join(default_dirs[i], leaf);
        if (cand != NULL && access(cand, R_OK) == 0) {
            return cand;
        }
        free(cand);
    }
    return NULL;
}

static int load_path_input(const char *path, const ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
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

static int shared_object_matches_unresolved(const char *path, const ld_ctx_t *ctx, const symstate_t *state,
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

static int plan_dynamic_needed(ld_ctx_t *ctx, elfobj_t *out) {
    elf_section_t *dynstr;
    size_t i;
    size_t total = 1;
    uint8_t *buf;
    size_t off = 1;

    if (ctx->dso_inputs.count == 0) {
        return 0;
    }
    dynstr = elf_find_section(out, ".dynstr");
    if (dynstr == NULL) {
        dynstr = elf_add_section(out, ".dynstr", SHT_STRTAB, SHF_ALLOC);
        if (dynstr == NULL) {
            return -1;
        }
        if (elf_section_set_align(dynstr, 1) != ELF_OK) {
            return -1;
        }
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        const char *p = strrchr(ctx->dso_inputs.items[i], '/');
        const char *name = p != NULL ? p + 1 : ctx->dso_inputs.items[i];
        total += strlen(name) + 1;
    }
    buf = (uint8_t *)calloc(1, total);
    if (buf == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        const char *p = strrchr(ctx->dso_inputs.items[i], '/');
        const char *name = p != NULL ? p + 1 : ctx->dso_inputs.items[i];
        size_t n = strlen(name) + 1;
        memcpy(buf + off, name, n);
        if (elf_link_add_dynamic_entry(out, DT_NEEDED, off) != ELF_OK) {
            free(buf);
            return -1;
        }
        off += n;
    }
    if (elf_link_add_dynamic_entry(out, DT_NULL, 0) != ELF_OK) {
        free(buf);
        return -1;
    }
    if (elf_section_set_data(dynstr, buf, total) != ELF_OK) {
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

static int load_library_input(ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    char *path_so = NULL;
    char *path_a = NULL;
    int shared_matches = 0;
    int have_shared_match = 0;

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

    path_so = resolve_library_path_suffix(ctx, in->text, ".so");
    if (path_so != NULL && load_path_input(path_so, ctx, objs, state, in->whole_archive, 1) == 0) {
        free(path_so);
        return 0;
    }
    if (path_so != NULL && ctx->expect_type == ET_DYN) {
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

    fprintf(fp, "\nSymbols:\n");
    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name;

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
        fprintf(fp, "  %-28s value=0x%llx size=%llu bind=%u type=%u shndx=%u\n",
                name,
                (unsigned long long)elf_symbol_value(sym),
                (unsigned long long)elf_symbol_size(sym),
                (unsigned)elf_symbol_bind(sym),
                (unsigned)elf_symbol_type(sym),
                (unsigned)elf_symbol_shndx(sym));
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

static int assign_section_addresses(elfobj_t *obj, uint64_t base_vaddr) {
    uint64_t off;
    uint64_t mem_end;
    uint64_t ehsize;
    uint64_t phentsz;
    uint64_t phnum;
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
    if (type == SHT_REL || type == SHT_RELA) {
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

static int add_default_segments(elfobj_t *obj, const char *interp_path) {
    elf_segment_t *load_seg;
    uint32_t load_flags = 0x4;
    size_t i;

    if (interp_path != NULL && interp_path[0] != '\0') {
        if (elf_add_interp_segment(obj, interp_path) == NULL) {
            return -1;
        }
    }

    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags;
        if (sec == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        if ((flags & SHF_EXECINSTR) != 0) {
            load_flags |= 0x1;
        }
        if ((flags & SHF_WRITE) != 0) {
            load_flags |= 0x2;
        }
    }

    load_seg = elf_add_load_segment(obj, load_flags, 0x1000);
    if (load_seg == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        if (sec == NULL) {
            continue;
        }
        if ((elf_section_flags(sec) & SHF_ALLOC) == 0) {
            continue;
        }
        if (elf_segment_add_section(load_seg, sec) != ELF_OK) {
            return -1;
        }
    }

    {
        elf_section_t *dyn = elf_find_section(obj, ".dynamic");
        if (dyn != NULL) {
            elf_segment_t *dyn_seg = elf_add_dynamic_segment(obj, 8);
            if (dyn_seg == NULL || elf_segment_add_section(dyn_seg, dyn) != ELF_OK) {
                return -1;
            }
        }
    }
    {
        elf_segment_t *tls_seg = NULL;
        for (i = 0; i < elf_section_count(obj); ++i) {
            elf_section_t *sec = elf_section_get(obj, i);
            if (sec == NULL || (elf_section_flags(sec) & SHF_TLS) == 0) {
                continue;
            }
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

static int check_undefined_symbols(elfobj_t *obj, int allow_undefined, const symref_map_t *refs) {
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
                const char *src = refs != NULL ? symref_map_get(refs, name) : NULL;
                if (src != NULL) {
                    fprintf(stderr, "ld: undefined reference to `%s` (referenced by %s)\n", name, src);
                } else {
                    fprintf(stderr, "ld: undefined reference to `%s`\n", name);
                }
                return -1;
            }
        }
    }
    return 0;
}

static int apply_all_relocations(elfobj_t *obj, int allow_undefined) {
    size_t i;
    elfobj_endian_t endian = elf_endian(obj);
    uint16_t machine = elf_machine(obj);

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint8_t *buf;
        const void *src;
        size_t sec_sz;
        size_t rc;
        size_t ri;

        if (sec == NULL) {
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
            if (resolve_symbol_addr(obj, sym, allow_undefined, &S, &undef_name) != 0) {
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
                (type == R_X86_64_GOTPCRELX || type == R_X86_64_REX_GOTPCRELX) &&
                off >= 2 && buf[off - 2] == 0x8b) {
                /* Relax MOV r64, [rip+disp32] GOT load into LEA r64, [rip+disp32]. */
                buf[off - 2] = 0x8d;
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
    if (load_all_inputs(ctx, &inputs) != 0) {
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

    if (strip_group_sections_for_final(out) != 0) {
        fprintf(stderr, "ld: failed to strip SHT_GROUP sections for final output\n");
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

    if (add_default_segments(out, ctx->interp_path) != 0) {
        fprintf(stderr, "ld: failed to add output program segments\n");
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

    if (check_undefined_symbols(out, allow_undef_runtime, &undef_refs) != 0) {
        symref_map_free(&undef_refs);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (apply_all_relocations(out, allow_undef_runtime) != 0) {
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

        if (strcmp(a, "-z") == 0 || strcmp(a, "-T") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                strvec_free(&ctx.trace_symbols);
                return 2;
            }
            ++i;
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
        return 0;
    }
    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
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
        return 1;
    }
    if (validate_output(&ctx) != 0) {
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        strvec_free(&ctx.trace_symbols);
        strvec_free(&ctx.force_undefined);
        defsymvec_free(&ctx.defsyms);
        strvec_free(&ctx.dso_inputs);
        return 1;
    }

    inputvec_free(&ctx.inputs);
    strvec_free(&ctx.lib_paths);
    strvec_free(&ctx.trace_symbols);
    strvec_free(&ctx.force_undefined);
    defsymvec_free(&ctx.defsyms);
    strvec_free(&ctx.dso_inputs);
    return 0;
}

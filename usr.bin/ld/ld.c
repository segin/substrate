#include "elfobj.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    uint16_t expect_type;
    int allow_undefined;
    int query_version;
    const char *out_path;
    const char *self_path;
    const char *entry_symbol;
    const char *interp_path;
    ld_compat_mode_t compat_mode;
    ld_lib_mode_t current_lib_mode;
    int current_whole_archive;
    int current_as_needed;
    strvec_t lib_paths;
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

static uint64_t align_up_u64(uint64_t v, uint64_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + (a - 1)) & ~(a - 1);
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
            if (c == '\0' || c == '\n' || c == '/') {
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
        return elf_class(obj) == ELFOBJ_CLASS_64 && elf_machine(obj) == EM_X86_64;
    }
    return elf_class(obj) == ELFOBJ_CLASS_32 && elf_machine(obj) == EM_386;
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

static int parse_archive_header(const char *path, unsigned char **out_buf, size_t *out_sz) {
    if (read_file(path, out_buf, out_sz) != 0) {
        fprintf(stderr, "ld: cannot read archive %s\n", path);
        return -1;
    }
    if (*out_sz < 8 || memcmp(*out_buf, "!<arch>\n", 8) != 0) {
        free(*out_buf);
        *out_buf = NULL;
        *out_sz = 0;
        fprintf(stderr, "ld: unsupported archive format: %s\n", path);
        return -1;
    }
    return 0;
}

static int load_archive_members(const char *path, const ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
                                int whole_archive) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    size_t off = 8;
    const char *strtab = NULL;
    size_t strtab_sz = 0;
    int changed_any = 0;
    symset_t seen_members;
    int pass_progress;

    memset(&seen_members, 0, sizeof(seen_members));
    if (parse_archive_header(path, &buf, &sz) != 0) {
        return -1;
    }

    do {
        pass_progress = 0;
        off = 8;
        while (off + 60 <= sz) {
            const unsigned char *hdr = buf + off;
            uint64_t msize = 0;
            const unsigned char *mdata;
            char *mname;
            size_t name_extra = 0;
            size_t body_sz;
            char member_key[96];

            if (hdr[58] != '`' || hdr[59] != '\n') {
                break;
            }
            if (parse_u64_dec((const char *)hdr + 48, 10, &msize) != 0) {
                break;
            }
            off += 60;
            if (off + (size_t)msize > sz) {
                break;
            }
            mdata = buf + off;
            mname = decode_ar_name((const char *)hdr, mdata, msize, strtab, strtab_sz, &name_extra);
            if (mname == NULL) {
                break;
            }
            if (name_extra > (size_t)msize) {
                free(mname);
                break;
            }
            body_sz = (size_t)msize - name_extra;
            mdata += name_extra;

            if (strcmp(mname, "//") == 0) {
                strtab = (const char *)mdata;
                strtab_sz = body_sz;
            } else if (strcmp(mname, "/") != 0 && body_sz >= 4 &&
                       mdata[0] == 0x7f && mdata[1] == 'E' && mdata[2] == 'L' && mdata[3] == 'F') {
                elfobj_t *obj = NULL;
                snprintf(member_key, sizeof(member_key), "%s@%zu", path, off);
                if (!symset_contains(&seen_members, member_key) && elf_open_memory(mdata, body_sz, &obj) == ELF_OK) {
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
            }
            free(mname);
            off += (size_t)msize;
            if ((off & 1u) != 0) {
                off++;
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
            fprintf(stderr, "ld: input %s has mismatched class/machine\n", path);
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

static int shared_object_matches_unresolved(const char *path, const ld_ctx_t *ctx, const symstate_t *state,
                                            int *out_match) {
    elfobj_t *obj = NULL;
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
    for (i = 0; i < elf_symbol_count(obj); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *name;
        uint16_t shndx;
        uint8_t bind;

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
            *out_match = 1;
            break;
        }
    }
    elf_close(obj);
    return 0;
}

static int load_library_input(const ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
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

static int process_input_once(const ld_ctx_t *ctx, const ld_input_t *in, objvec_t *objs, symstate_t *state) {
    if (in->kind == LD_INPUT_FILE) {
        return load_path_input(in->text, ctx, objs, state, in->whole_archive, 0);
    }
    if (in->kind == LD_INPUT_LIB) {
        return load_library_input(ctx, in, objs, state);
    }
    return 0;
}

static int load_group_inputs(const ld_ctx_t *ctx, objvec_t *objs, symstate_t *state,
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

static int load_all_inputs(const ld_ctx_t *ctx, objvec_t *objs) {
    symstate_t state;
    size_t i;

    memset(&state, 0, sizeof(state));
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

static int64_t sign_extend_u64(uint64_t v, int bits) {
    uint64_t m;
    if (bits <= 0 || bits >= 64) {
        return (int64_t)v;
    }
    m = 1ULL << (bits - 1);
    return (int64_t)((v ^ m) - m);
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

    off = ehsize + phnum * phentsz;
    mem_end = base_vaddr + off;
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
            off = align_up_u64(off, align);
            file_off = off;
            off += size;
        }

        if ((flags & SHF_ALLOC) != 0) {
            if (elf_section_type(sec) == SHT_NOBITS) {
                addr = align_up_u64(mem_end, align);
            } else {
                addr = base_vaddr + file_off;
            }
            if (addr + size > mem_end) {
                mem_end = addr + size;
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

static int set_entry_symbol(elfobj_t *obj, const char *entry_symbol, int require_entry) {
    const elf_symbol_t *sym;
    uint64_t addr = 0;

    if (entry_symbol == NULL || entry_symbol[0] == '\0') {
        return 0;
    }
    sym = elf_find_symbol(obj, entry_symbol);
    if (sym == NULL || elf_symbol_shndx(sym) == SHN_UNDEF) {
        if (require_entry) {
            fprintf(stderr, "ld: entry symbol '%s' not found\n", entry_symbol);
            return -1;
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

static int check_undefined_symbols(elfobj_t *obj, int allow_undefined) {
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
                fprintf(stderr, "ld: undefined reference to `%s`\n", name);
                return -1;
            }
        }
    }
    return 0;
}

static int apply_all_relocations(elfobj_t *obj, int allow_undefined) {
    size_t i;
    elfobj_endian_t endian = elf_endian(obj);

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
            uint64_t off;
            uint32_t type;
            int64_t addend;
            int width;
            const elf_symbol_t *sym;
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
                fprintf(stderr, "ld: unsupported relocation width for type %u\n", type);
                return -1;
            }
            if (off + (uint64_t)width > sec_sz) {
                free(buf);
                fprintf(stderr, "ld: relocation out of range in section '%s'\n",
                        elf_section_name(sec) != NULL ? elf_section_name(sec) : "<unnamed>");
                return -1;
            }
            if (elf_reloc_has_addend(rel)) {
                addend = elf_reloc_addend(rel);
            } else {
                uint64_t raw = read_uint_bytes(buf + off, width, endian);
                addend = sign_extend_u64(raw, width * 8);
            }

            sym = elf_reloc_symbol(rel);
            if (resolve_symbol_addr(obj, sym, allow_undefined, &S, &undef_name) != 0) {
                free(buf);
                fprintf(stderr, "ld: unresolved relocation symbol `%s`\n",
                        undef_name != NULL ? undef_name : "<unknown>");
                return -1;
            }
            P = elf_section_addr(sec) + off;
            err = elf_apply_relocation_value(obj, type, P, S, addend, &outv);
            if (err != ELF_OK) {
                free(buf);
                fprintf(stderr, "ld: relocation apply failed (type=%u): %s\n", type, elf_errstr(err));
                return -1;
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

static int run_internal_link(const ld_ctx_t *ctx) {
    objvec_t inputs;
    elfobj_t *out = NULL;
    elf_err_t err;
    uint16_t out_type;
    uint64_t base_vaddr;

    memset(&inputs, 0, sizeof(inputs));
    if (load_all_inputs(ctx, &inputs) != 0) {
        objvec_free(&inputs);
        return -1;
    }
    if (inputs.count == 0) {
        fprintf(stderr, "ld: no compatible relocatable input objects found\n");
        objvec_free(&inputs);
        return -1;
    }

    err = elf_link(inputs.objs, inputs.count, &out);
    if (err != ELF_OK || out == NULL) {
        fprintf(stderr, "ld: link merge failed: %s\n", out != NULL ? elf_last_diagnostics(out) : elf_errstr(err));
        objvec_free(&inputs);
        if (out != NULL) {
            elf_close(out);
        }
        return -1;
    }

    out_type = ctx->expect_type == 0 ? ET_EXEC : ctx->expect_type;
    if (elf_set_type(out, out_type) != ELF_OK) {
        fprintf(stderr, "ld: failed to set output type\n");
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (out_type == ET_REL) {
        if (elf_write_file(out, ctx->out_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
            objvec_free(&inputs);
            elf_close(out);
            return -1;
        }
        if (chmod(ctx->out_path, 0644) != 0) {
            fprintf(stderr, "ld: warning: failed to set output mode on %s: %s\n",
                    ctx->out_path, strerror(errno));
        }
        objvec_free(&inputs);
        elf_close(out);
        return 0;
    }

    if (add_default_segments(out, ctx->interp_path) != 0) {
        fprintf(stderr, "ld: failed to add output program segments\n");
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
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (check_undefined_symbols(out, ctx->allow_undefined || out_type == ET_DYN) != 0) {
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (apply_all_relocations(out, ctx->allow_undefined || out_type == ET_DYN) != 0) {
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (set_entry_symbol(out, ctx->entry_symbol != NULL ? ctx->entry_symbol : "_start",
                         out_type == ET_EXEC) != 0) {
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }

    if (elf_write_file(out, ctx->out_path) != ELF_OK) {
        fprintf(stderr, "ld: failed to write output %s\n", ctx->out_path);
        objvec_free(&inputs);
        elf_close(out);
        return -1;
    }
    if (chmod(ctx->out_path, out_type == ET_EXEC ? 0755 : 0644) != 0) {
        fprintf(stderr, "ld: warning: failed to set output mode on %s: %s\n",
                ctx->out_path, strerror(errno));
    }

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

        if (strcmp(a, "-z") == 0 || strcmp(a, "-T") == 0 || strcmp(a, "-Map") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            ++i;
            continue;
        }
        if (strcmp(a, "--gc-sections") == 0 || strcmp(a, "--strip-all") == 0 ||
            strcmp(a, "--build-id") == 0 || strcmp(a, "-s") == 0) {
            continue;
        }

        if (a[0] == '-') {
            if (ctx.compat_mode == LD_COMPAT_LLD) {
                fprintf(stderr, "ld: error: unsupported option in lld mode: %s\n", a);
                inputvec_free(&ctx.inputs);
                strvec_free(&ctx.lib_paths);
                return 2;
            }
            fprintf(stderr, "ld: warning: unsupported option ignored (gnu mode): %s\n", a);
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
        return 0;
    }
    if (ctx.inputs.count == 0) {
        usage(argv[0]);
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
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
        return 1;
    }
    if (validate_output(&ctx) != 0) {
        inputvec_free(&ctx.inputs);
        strvec_free(&ctx.lib_paths);
        return 1;
    }

    inputvec_free(&ctx.inputs);
    strvec_free(&ctx.lib_paths);
    return 0;
}

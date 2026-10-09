/*
 * ld_util.c -- small things everything uses: vectors, sets, numbers, bytes, diagnostics.
 */

#include "ld.h"

/*
 * Room for one more in a growable array: `itemsp` is the address of the
 * pointer to its elements, each `size` bytes, of which `count` are in
 * use and *cap allocated.  0, or -1 if there is no memory or no such
 * size, with the array as it was.
 */
int ld_vec_room(void *itemsp, size_t *cap, size_t count, size_t size) {
    void *items;
    size_t ncap;

    if (count < *cap) {
        return 0;
    }
    ncap = *cap == 0 ? 16 : *cap * 2;
    if (ncap <= count || size == 0 || ncap > ((size_t)-1) / size) {
        return -1;
    }
    memcpy(&items, itemsp, sizeof(items));
    items = realloc(items, ncap * size);
    if (items == NULL) {
        return -1;
    }
    memcpy(itemsp, &items, sizeof(items));
    *cap = ncap;
    return 0;
}

char *xstrdup(const char *s) {
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

int strvec_push(strvec_t *v, const char *s) {
    if (ld_vec_room(&v->items, &v->cap, v->count, sizeof(v->items[0])) != 0) {
        return -1;
    }

    v->items[v->count] = xstrdup(s);
    if (v->items[v->count] == NULL) {
        return -1;
    }
    v->count++;
    return 0;
}

void strvec_free(strvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

void strvec_pop(strvec_t *v) {
    if (v == NULL || v->count == 0) {
        return;
    }
    v->count--;
    free(v->items[v->count]);
    v->items[v->count] = NULL;
}

int defsymvec_push(defsymvec_t *v, const char *name, uint64_t value) {
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    if (ld_vec_room(&v->items, &v->cap, v->count, sizeof(v->items[0])) != 0) {
        return -1;
    }
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return -1;
    }
    v->items[v->count].value = value;
    v->count++;
    return 0;
}

int defsymvec_find(const defsymvec_t *v, const char *name) {
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

int defsymvec_get(const defsymvec_t *v, const char *name, uint64_t *out_value) {
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

int defsymvec_set(defsymvec_t *v, const char *name, uint64_t value) {
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

void defsymvec_free(defsymvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

int inputvec_push(inputvec_t *v, ld_input_kind_t kind, ld_lib_mode_t lib_mode, int whole_archive,
                         int as_needed, const char *text) {
    if (ld_vec_room(&v->items, &v->cap, v->count, sizeof(v->items[0])) != 0) {
        return -1;
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

void inputvec_free(inputvec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].text);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

int dyn_import_find(const dyn_import_vec_t *v, const char *name) {
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
    char *dup;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    if (ld_vec_room(&v->items, &v->cap, v->count, sizeof(v->items[0])) != 0) {
        return -1;
    }
    dup = xstrdup(name);
    if (dup == NULL) {
        return -1;
    }
    /* All of it: realloc's memory is whatever was there before. */
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = dup;
    if (out_idx != NULL) {
        *out_idx = v->count;
    }
    v->count++;
    return 0;
}

dyn_import_t *dyn_import_get_or_add(dyn_import_vec_t *v, const char *name) {
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

void dyn_import_vec_free(dyn_import_vec_t *v) {
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

int objvec_push(objvec_t *v, elfobj_t *obj, const char *name) {
    size_t names_cap;
    char *dup;

    if (v->count >= LD_MAX_INPUT_OBJECTS) {
        fprintf(stderr, "ld: input object limit exceeded (%u)\n", (unsigned)LD_MAX_INPUT_OBJECTS);
        return -1;
    }
    /* Two arrays and one capacity, which is the second's: if the first
     * grows and the second cannot, the first is merely larger than it
     * says. */
    names_cap = v->cap;
    if (ld_vec_room(&v->names, &names_cap, v->count, sizeof(v->names[0])) != 0 ||
        ld_vec_room(&v->objs, &v->cap, v->count, sizeof(v->objs[0])) != 0) {
        return -1;
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

void objvec_free(objvec_t *v) {
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

char *path_join(const char *dir, const char *leaf) {
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

int default_mode(void) {
    return (int)(sizeof(void *) == 8 ? 64 : 32);
}

int parse_mode_token(const char *tok) {
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

int parse_compat_mode(const char *tok, ld_compat_mode_t *out_mode) {
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

const char *canonical_mode_name(int mode) {
    if (mode == 64) {
        return "x86-64";
    }
    if (mode == 32) {
        return "i386";
    }
    return "unknown";
}

int parse_z_option(ld_ctx_t *ctx, const char *val) {
    static const struct {
        const char *word;
        size_t field;           /* of ld_options_t */
        int n;
    } words[] = {
        { "text", offsetof(ld_options_t, z_text_mode), 1 },
        { "notext", offsetof(ld_options_t, z_text_mode), 2 },
        { "execstack", offsetof(ld_options_t, z_execstack), 1 },
        { "noexecstack", offsetof(ld_options_t, z_execstack), 0 },
        { "relro", offsetof(ld_options_t, z_relro), 1 },
        { "norelro", offsetof(ld_options_t, z_relro), 0 },
        { "now", offsetof(ld_options_t, z_now), 1 },
        { "lazy", offsetof(ld_options_t, z_now), 0 },
    };
    size_t i;

    if (ctx == NULL || val == NULL || val[0] == '\0') {
        return -1;
    }
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (strcmp(val, words[i].word) == 0) {
            *(int *)((char *)&ctx->opt + words[i].field) = words[i].n;
            return 0;
        }
    }
    return -1;
}

int parse_hash_style_option(const char *val, ld_hash_style_t *out_style) {
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

void ld_diag_note(const char *category, const char *source, const char *hint) {
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

int ld_warn(ld_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "ld: warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (ctx->opt.fatal_warnings) {
        fprintf(stderr, "ld: error: warnings treated as errors (--fatal-warnings)\n");
        return -1;
    }
    return 0;
}

int set_explicit_mode(ld_ctx_t *ctx, int mode, const char *opt_text) {
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

int align_up_u64_checked(uint64_t v, uint64_t a, uint64_t *out) {
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

int add_u64_checked(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL || a > UINT64_MAX - b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

int mul_u64_checked(uint64_t a, uint64_t b, uint64_t *out) {
    if (out == NULL) {
        return 0;
    }
    if (a != 0 && b > UINT64_MAX / a) {
        return 0;
    }
    *out = a * b;
    return 1;
}

int has_suffix(const char *s, const char *suffix) {
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

int read_file(const char *path, unsigned char **out, size_t *out_sz) {
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

int parse_u64_dec(const char *s, size_t n, uint64_t *out) {
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

int parse_u64_auto(const char *s, uint64_t *out) {
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

/* A number of `sz` bytes at p, in the file's byte order; and the widths
 * that have names. */
uint64_t read_uint_bytes(const uint8_t *p, int sz, elfobj_endian_t e) {
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

void write_uint_bytes(uint8_t *p, int sz, elfobj_endian_t e, uint64_t v) {
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

uint16_t read_u16_endian(const uint8_t *p, elfobj_endian_t endian) {
    return (uint16_t)read_uint_bytes(p, 2, endian);
}

uint32_t read_u32_endian(const uint8_t *p, elfobj_endian_t endian) {
    return (uint32_t)read_uint_bytes(p, 4, endian);
}

uint64_t read_u64_endian(const uint8_t *p, elfobj_endian_t endian) {
    return read_uint_bytes(p, 8, endian);
}

void write_u16_endian(uint8_t *p, elfobj_endian_t endian, uint16_t v) {
    write_uint_bytes(p, 2, endian, v);
}

void write_u32_endian(uint8_t *p, elfobj_endian_t endian, uint32_t v) {
    write_uint_bytes(p, 4, endian, v);
}

void write_u64_endian(uint8_t *p, elfobj_endian_t endian, uint64_t v) {
    write_uint_bytes(p, 8, endian, v);
}

#include "cc_frontend.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PP_MAX_INCLUDE_DEPTH 128
#define PP_MAX_EXPAND_DEPTH 32
#define PP_MAX_EXPAND_PASSES 2
#define PP_MAX_EXPANDED_TEXT (64 * 1024 * 1024)
#define PP_MAX_OUTPUT_SIZE (64 * 1024 * 1024)
#define PP_MAX_EXPANDED_TOKENS (1024 * 1024)
#define PP_EMPTY_ARG_MARKER '\x1f'
#define PP_HIDE_MACRO_BEGIN '\x1d'
#define PP_HIDE_MACRO_END '\x1c'

typedef struct {
    char *name;
    int is_function;
    int is_variadic;
    char **params;
    size_t param_count;
    char *body;
} pp_macro_t;

typedef struct {
    pp_macro_t *items;
    size_t count;
    size_t cap;
    int sorted;
} pp_macro_table_t;

typedef struct {
    char *name;
    int has_macro;
    pp_macro_t macro;
} pp_macro_snapshot_t;

typedef struct {
    pp_macro_snapshot_t *items;
    size_t count;
    size_t cap;
} pp_macro_stack_t;

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} pp_strvec_t;

typedef struct {
    int parent_active;
    int this_active;
    int any_taken;
    int saw_else;
} pp_cond_frame_t;

typedef struct {
    pp_macro_table_t macros;
    pp_strvec_t quote_paths;
    pp_strvec_t user_include_paths;
    pp_strvec_t system_include_paths;
    pp_strvec_t include_once;
    pp_strvec_t force_includes;
    pp_strvec_t force_imacros;
    pp_strvec_t dep_paths;
    pp_macro_stack_t pragma_macro_stack;
    int no_default_includes;
    int show_include_paths;
    int dump_macros;
    int emit_line_markers;
    int suppress_output;
    int dep_emit;
    int dep_user_only;
    int dep_stdout_only;
    char *dep_target;
    char *dep_file;
    int target_quote;
    int target_bits;
    int target_has_sse;
    int target_has_sse2;
    int target_has_mmx;
    int target_has_avx;
    int target_has_avx2;
    int target_supports_sse;
    int target_supports_sse2;
    int target_supports_mmx;
    int target_supports_avx;
    int target_supports_avx2;
    int enable_trigraphs;
    int std_version;
    int std_is_c11;
    int std_is_c17;
    int std_is_c23;
    int std_is_gnu;
    const char *base_file;
    int include_level;
    unsigned long counter_value;
    size_t output_bytes;
    size_t max_include_depth;
    size_t max_expand_depth;
    size_t max_expand_passes;
    size_t max_expanded_text;
    size_t max_output_size;
    size_t max_expanded_tokens;
} pp_state_t;

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sb_t;

typedef struct {
    const char *s;
    size_t pos;
    int relaxed_eval;
} expr_parser_t;

static const char *g_pp_diag_file = NULL;

static void set_diag(cc_diag_t *diag, size_t line, size_t col, const char *msg) {
    if (diag == NULL || diag->message[0] != '\0') {
        return;
    }
    if (g_pp_diag_file != NULL && g_pp_diag_file[0] != '\0') {
        snprintf(diag->path, sizeof(diag->path), "%s", g_pp_diag_file);
    } else {
        diag->path[0] = '\0';
    }
    diag->line = line;
    diag->col = col;
    snprintf(diag->message, sizeof(diag->message), "%s", msg);
}

static char *xstrdup_n(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    return xstrdup_n(s, strlen(s));
}

static int strvec_push(pp_strvec_t *v, const char *s) {
    char **next;
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
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

static int strvec_contains(const pp_strvec_t *v, const char *s) {
    size_t i;
    if (v == NULL || s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

static int strvec_push_unique(pp_strvec_t *v, const char *s) {
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (strvec_contains(v, s)) {
        return 0;
    }
    return strvec_push(v, s);
}

static int sb_append_c(sb_t *sb, char c);
static int sb_append(sb_t *sb, const char *s);
static int dir_exists(const char *path);
static int path_exists(const char *path);
static const char *path_basename(const char *path);
static int macro_set(pp_macro_table_t *t, const char *name, int is_function, int is_variadic, char **params,
                     size_t param_count, const char *body);
static void macro_unset(pp_macro_table_t *t, const char *name);
static char *mask_hidden_macro_name_tokens(const char *src, const char *name);
static char *unmask_hidden_macro_tokens(const char *src);

static int macro_item_name_cmp(const void *ap, const void *bp) {
    const pp_macro_t *a = (const pp_macro_t *)ap;
    const pp_macro_t *b = (const pp_macro_t *)bp;
    return strcmp(a->name, b->name);
}

static void macro_table_ensure_sorted(pp_macro_table_t *t) {
    if (t == NULL || t->count < 2 || t->sorted) {
        return;
    }
    qsort(t->items, t->count, sizeof(t->items[0]), macro_item_name_cmp);
    t->sorted = 1;
}

static void strvec_pop_free(pp_strvec_t *v) {
    if (v == NULL || v->count == 0) {
        return;
    }
    free(v->items[v->count - 1]);
    v->count--;
}

static void strvec_free(pp_strvec_t *v) {
    size_t i;
    for (i = 0; i < v->count; ++i) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int sb_append_escaped_make_target(sb_t *sb, const char *target, int quote_mode) {
    size_t i;
    if (target == NULL) {
        return -1;
    }
    for (i = 0; target[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)target[i];
        if (quote_mode && c == '$') {
            if (sb_append(sb, "$$") != 0) {
                return -1;
            }
            continue;
        }
        if (c == ' ' || c == '\t' || c == ':' || c == '#') {
            if (sb_append_c(sb, '\\') != 0) {
                return -1;
            }
        }
        if (sb_append_c(sb, (char)c) != 0) {
            return -1;
        }
    }
    return 0;
}

static int sb_reserve(sb_t *sb, size_t extra) {
    char *next;
    size_t need = sb->len + extra + 1;
    size_t ncap = sb->cap == 0 ? 128 : sb->cap;
    while (ncap < need) {
        ncap *= 2;
    }
    if (ncap == sb->cap) {
        return 0;
    }
    next = (char *)realloc(sb->buf, ncap);
    if (next == NULL) {
        return -1;
    }
    sb->buf = next;
    sb->cap = ncap;
    return 0;
}

static int sb_append_n(sb_t *sb, const char *s, size_t n) {
    if (sb_reserve(sb, n) != 0) {
        return -1;
    }
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_append_c(sb_t *sb, char c) {
    if (sb_reserve(sb, 1) != 0) {
        return -1;
    }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
    return 0;
}

static int sb_append(sb_t *sb, const char *s) {
    return sb_append_n(sb, s, strlen(s));
}

static void sb_free(sb_t *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int is_ident_start(int c) {
    return c == '_' || isalpha((unsigned char)c);
}

static int is_ident_char(int c) {
    return c == '_' || isalnum((unsigned char)c);
}

static int trigraph_replacement(int c) {
    switch (c) {
    case '=':
        return '#';
    case '/':
        return '\\';
    case '\'':
        return '^';
    case '(':
        return '[';
    case ')':
        return ']';
    case '!':
        return '|';
    case '<':
        return '{';
    case '>':
        return '}';
    case '-':
        return '~';
    default:
        return -1;
    }
}

static size_t normalize_trigraphs(char *buf, size_t len) {
    size_t r = 0;
    size_t w = 0;
    while (r < len) {
        if (r + 2 < len && buf[r] == '?' && buf[r + 1] == '?') {
            int repl = trigraph_replacement((unsigned char)buf[r + 2]);
            if (repl >= 0) {
                buf[w++] = (char)repl;
                r += 3;
                continue;
            }
        }
        buf[w++] = buf[r++];
    }
    return w;
}

static int std_mode_enable_trigraphs(const char *std_mode) {
    if (std_mode == NULL || std_mode[0] == '\0') {
        return 1;
    }
    if (strcmp(std_mode, "c23") == 0 || strcmp(std_mode, "gnu23") == 0 || strcmp(std_mode, "c2x") == 0 ||
        strcmp(std_mode, "gnu2x") == 0) {
        return 0;
    }
    return 1;
}

static const char *path_basename(const char *path) {
    const char *slash;
    if (path == NULL) {
        return "";
    }
    slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static int std_mode_version(const char *std_mode, int *out_is_c11, int *out_is_c17, int *out_is_c23,
                            int *out_is_gnu) {
    int is_c11 = 0;
    int is_c17 = 0;
    int is_c23 = 0;
    int is_gnu = 0;

    if (std_mode == NULL || std_mode[0] == '\0') {
        if (out_is_c11 != NULL) {
            *out_is_c11 = is_c11;
        }
        if (out_is_c17 != NULL) {
            *out_is_c17 = is_c17;
        }
        if (out_is_c23 != NULL) {
            *out_is_c23 = is_c23;
        }
        if (out_is_gnu != NULL) {
            *out_is_gnu = is_gnu;
        }
        return 199901;
    }
    if (strncmp(std_mode, "gnu", 3) == 0) {
        is_gnu = 1;
    }
    if (strcmp(std_mode, "c23") == 0 || strcmp(std_mode, "gnu23") == 0 || strcmp(std_mode, "c2x") == 0 ||
        strcmp(std_mode, "gnu2x") == 0) {
        is_c11 = 1;
        is_c17 = 1;
        is_c23 = 1;
        if (out_is_c11 != NULL) {
            *out_is_c11 = is_c11;
        }
        if (out_is_c17 != NULL) {
            *out_is_c17 = is_c17;
        }
        if (out_is_c23 != NULL) {
            *out_is_c23 = is_c23;
        }
        if (out_is_gnu != NULL) {
            *out_is_gnu = is_gnu;
        }
        return 202311;
    }
    if (strcmp(std_mode, "c17") == 0 || strcmp(std_mode, "gnu17") == 0 || strcmp(std_mode, "c18") == 0 ||
        strcmp(std_mode, "gnu18") == 0) {
        is_c11 = 1;
        is_c17 = 1;
        if (out_is_c11 != NULL) {
            *out_is_c11 = is_c11;
        }
        if (out_is_c17 != NULL) {
            *out_is_c17 = is_c17;
        }
        if (out_is_c23 != NULL) {
            *out_is_c23 = is_c23;
        }
        if (out_is_gnu != NULL) {
            *out_is_gnu = is_gnu;
        }
        return 201710;
    }
    if (strcmp(std_mode, "c11") == 0 || strcmp(std_mode, "gnu11") == 0) {
        is_c11 = 1;
        if (out_is_c11 != NULL) {
            *out_is_c11 = is_c11;
        }
        if (out_is_c17 != NULL) {
            *out_is_c17 = is_c17;
        }
        if (out_is_c23 != NULL) {
            *out_is_c23 = is_c23;
        }
        if (out_is_gnu != NULL) {
            *out_is_gnu = is_gnu;
        }
        return 201112;
    }
    if (out_is_c11 != NULL) {
        *out_is_c11 = is_c11;
    }
    if (out_is_c17 != NULL) {
        *out_is_c17 = is_c17;
    }
    if (out_is_c23 != NULL) {
        *out_is_c23 = is_c23;
    }
    if (out_is_gnu != NULL) {
        *out_is_gnu = is_gnu;
    }
    return 199901;
}

static pp_macro_t *macro_find(pp_macro_table_t *t, const char *name) {
    size_t lo;
    size_t hi;
    if (t == NULL || name == NULL || t->count == 0) {
        return NULL;
    }
    macro_table_ensure_sorted(t);
    lo = 0;
    hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(t->items[mid].name, name);
        if (cmp == 0) {
            return &t->items[mid];
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static void macro_free_item(pp_macro_t *m) {
    size_t i;
    free(m->name);
    for (i = 0; i < m->param_count; ++i) {
        free(m->params[i]);
    }
    free(m->params);
    free(m->body);
    memset(m, 0, sizeof(*m));
}

static int macro_clone_item(const pp_macro_t *src, pp_macro_t *dst) {
    size_t i;
    if (src == NULL || dst == NULL) {
        return -1;
    }
    memset(dst, 0, sizeof(*dst));
    dst->name = xstrdup(src->name != NULL ? src->name : "");
    dst->body = xstrdup(src->body != NULL ? src->body : "");
    dst->is_function = src->is_function;
    dst->is_variadic = src->is_variadic;
    dst->param_count = src->param_count;
    if (dst->name == NULL || dst->body == NULL) {
        macro_free_item(dst);
        return -1;
    }
    if (src->param_count > 0) {
        dst->params = (char **)calloc(src->param_count, sizeof(*dst->params));
        if (dst->params == NULL) {
            macro_free_item(dst);
            return -1;
        }
        for (i = 0; i < src->param_count; ++i) {
            dst->params[i] = xstrdup(src->params[i] != NULL ? src->params[i] : "");
            if (dst->params[i] == NULL) {
                macro_free_item(dst);
                return -1;
            }
        }
    }
    return 0;
}

static void macro_snapshot_free(pp_macro_snapshot_t *snap) {
    if (snap == NULL) {
        return;
    }
    free(snap->name);
    snap->name = NULL;
    if (snap->has_macro) {
        macro_free_item(&snap->macro);
    } else {
        memset(&snap->macro, 0, sizeof(snap->macro));
    }
    snap->has_macro = 0;
}

static void macro_stack_free(pp_macro_stack_t *st) {
    size_t i;
    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->count; ++i) {
        macro_snapshot_free(&st->items[i]);
    }
    free(st->items);
    st->items = NULL;
    st->count = 0;
    st->cap = 0;
}

static int pragma_macro_push(pp_state_t *st, const char *name) {
    pp_macro_snapshot_t snap;
    pp_macro_snapshot_t *next;
    pp_macro_t *cur;
    if (st == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    memset(&snap, 0, sizeof(snap));
    snap.name = xstrdup(name);
    if (snap.name == NULL) {
        return -1;
    }
    cur = macro_find(&st->macros, name);
    if (cur != NULL) {
        snap.has_macro = 1;
        if (macro_clone_item(cur, &snap.macro) != 0) {
            macro_snapshot_free(&snap);
            return -1;
        }
    }
    if (st->pragma_macro_stack.count == st->pragma_macro_stack.cap) {
        size_t ncap = st->pragma_macro_stack.cap == 0 ? 16 : st->pragma_macro_stack.cap * 2;
        next = (pp_macro_snapshot_t *)realloc(st->pragma_macro_stack.items, ncap * sizeof(*next));
        if (next == NULL) {
            macro_snapshot_free(&snap);
            return -1;
        }
        st->pragma_macro_stack.items = next;
        st->pragma_macro_stack.cap = ncap;
    }
    st->pragma_macro_stack.items[st->pragma_macro_stack.count++] = snap;
    return 0;
}

static int pragma_macro_pop(pp_state_t *st, const char *name) {
    size_t i;
    if (st == NULL || name == NULL || name[0] == '\0') {
        return -1;
    }
    for (i = st->pragma_macro_stack.count; i > 0; --i) {
        pp_macro_snapshot_t *snap = &st->pragma_macro_stack.items[i - 1];
        if (strcmp(snap->name, name) != 0) {
            continue;
        }
        if (!snap->has_macro) {
            macro_unset(&st->macros, name);
        } else {
            size_t pi;
            char **params = NULL;
            if (snap->macro.param_count > 0) {
                params = (char **)calloc(snap->macro.param_count, sizeof(*params));
                if (params == NULL) {
                    return -1;
                }
                for (pi = 0; pi < snap->macro.param_count; ++pi) {
                    params[pi] = xstrdup(snap->macro.params[pi] != NULL ? snap->macro.params[pi] : "");
                    if (params[pi] == NULL) {
                        size_t pj;
                        for (pj = 0; pj < pi; ++pj) {
                            free(params[pj]);
                        }
                        free(params);
                        return -1;
                    }
                }
            }
            if (macro_set(&st->macros, snap->macro.name, snap->macro.is_function, snap->macro.is_variadic, params,
                          snap->macro.param_count, snap->macro.body) != 0) {
                if (params != NULL) {
                    size_t pj;
                    for (pj = 0; pj < snap->macro.param_count; ++pj) {
                        free(params[pj]);
                    }
                }
                free(params);
                return -1;
            }
        }
        macro_snapshot_free(snap);
        if (i < st->pragma_macro_stack.count) {
            memmove(&st->pragma_macro_stack.items[i - 1], &st->pragma_macro_stack.items[i],
                    (st->pragma_macro_stack.count - i) * sizeof(st->pragma_macro_stack.items[0]));
        }
        st->pragma_macro_stack.count--;
        return 0;
    }
    return 0;
}

static int macro_set(pp_macro_table_t *t, const char *name, int is_function, int is_variadic, char **params,
                     size_t param_count, const char *body) {
    pp_macro_t *m = macro_find(t, name);
    if (m == NULL) {
        pp_macro_t *next;
        if (t->count == t->cap) {
            size_t ncap = t->cap == 0 ? 64 : t->cap * 2;
            next = (pp_macro_t *)realloc(t->items, ncap * sizeof(*next));
            if (next == NULL) {
                return -1;
            }
            t->items = next;
            t->cap = ncap;
        }
        m = &t->items[t->count++];
        memset(m, 0, sizeof(*m));
        t->sorted = 0;
    } else {
        macro_free_item(m);
    }
    m->name = xstrdup(name);
    m->is_function = is_function;
    m->is_variadic = is_variadic;
    m->params = params;
    m->param_count = param_count;
    m->body = xstrdup(body != NULL ? body : "");
    if (m->name == NULL || m->body == NULL) {
        macro_free_item(m);
        return -1;
    }
    return 0;
}

static void macro_unset(pp_macro_table_t *t, const char *name) {
    size_t i;
    for (i = 0; i < t->count; ++i) {
        if (strcmp(t->items[i].name, name) == 0) {
            macro_free_item(&t->items[i]);
            if (i + 1 < t->count) {
                memmove(&t->items[i], &t->items[i + 1], (t->count - i - 1) * sizeof(t->items[0]));
            }
            t->count--;
            return;
        }
    }
}

static void macro_table_free(pp_macro_table_t *t) {
    size_t i;
    for (i = 0; i < t->count; ++i) {
        macro_free_item(&t->items[i]);
    }
    free(t->items);
    t->items = NULL;
    t->count = 0;
    t->cap = 0;
    t->sorted = 0;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
        s++;
    }
    return s;
}

static int parse_ident_token(const char **sp, char *out, size_t out_sz) {
    const char *p = skip_ws(*sp);
    size_t n = 0;

    if (!is_ident_start((unsigned char)*p)) {
        return -1;
    }
    while (is_ident_char((unsigned char)p[n])) {
        n++;
    }
    if (n == 0 || n + 1 > out_sz) {
        return -1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    *sp = p + n;
    return 0;
}

static int parse_limit_value(const char *s, size_t *out) {
    unsigned long long v;
    char *end = NULL;

    if (s == NULL || out == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return -1;
    }
    if (v == 0 || (size_t)v != v) {
        return -1;
    }
    *out = (size_t)v;
    return 0;
}

static size_t count_pp_tokens(const char *src) {
    size_t i = 0;
    size_t count = 0;

    if (src == NULL) {
        return 0;
    }

    while (src[i] != '\0') {
        if (isspace((unsigned char)src[i])) {
            i++;
            continue;
        }
        if (is_ident_start((unsigned char)src[i])) {
            i++;
            while (is_ident_char((unsigned char)src[i])) {
                i++;
            }
            count++;
            continue;
        }
        if (isdigit((unsigned char)src[i]) || (src[i] == '.' && isdigit((unsigned char)src[i + 1]))) {
            i++;
            while (isalnum((unsigned char)src[i]) || src[i] == '_' || src[i] == '.' || src[i] == '+' || src[i] == '-') {
                i++;
            }
            count++;
            continue;
        }
        if (src[i] == '"' || src[i] == '\'') {
            int q = (unsigned char)src[i++];
            while (src[i] != '\0') {
                if (src[i] == '\\' && src[i + 1] != '\0') {
                    i += 2;
                    continue;
                }
                if ((unsigned char)src[i] == (unsigned char)q) {
                    i++;
                    break;
                }
                i++;
            }
            count++;
            continue;
        }
        if ((src[i] == '<' && src[i + 1] == '<' && src[i + 2] == '=') ||
            (src[i] == '>' && src[i + 1] == '>' && src[i + 2] == '=')) {
            i += 3;
            count++;
            continue;
        }
        if ((src[i] == '.' && src[i + 1] == '.' && src[i + 2] == '.') ||
            (src[i] == '<' && src[i + 1] == ':' && src[i + 2] == '>')) {
            i += 3;
            count++;
            continue;
        }
        if ((src[i] == '+' && src[i + 1] == '+') || (src[i] == '-' && src[i + 1] == '-') ||
            (src[i] == '&' && src[i + 1] == '&') || (src[i] == '|' && src[i + 1] == '|') ||
            (src[i] == '<' && src[i + 1] == '<') || (src[i] == '>' && src[i + 1] == '>') ||
            (src[i] == '=' && src[i + 1] == '=') || (src[i] == '!' && src[i + 1] == '=') ||
            (src[i] == '<' && src[i + 1] == '=') || (src[i] == '>' && src[i + 1] == '=') ||
            (src[i] == '+' && src[i + 1] == '=') || (src[i] == '-' && src[i + 1] == '=') ||
            (src[i] == '*' && src[i + 1] == '=') || (src[i] == '/' && src[i + 1] == '=') ||
            (src[i] == '%' && src[i + 1] == '=') || (src[i] == '&' && src[i + 1] == '=') ||
            (src[i] == '|' && src[i + 1] == '=') || (src[i] == '^' && src[i + 1] == '=') ||
            (src[i] == '#' && src[i + 1] == '#') || (src[i] == '-' && src[i + 1] == '>')) {
            i += 2;
            count++;
            continue;
        }
        i++;
        count++;
    }
    return count;
}

static int has_c_attribute_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "deprecated") == 0 || strcmp(name, "fallthrough") == 0 || strcmp(name, "maybe_unused") == 0 ||
        strcmp(name, "nodiscard") == 0 || strcmp(name, "noreturn") == 0 ||
        strcmp(name, "reproducible") == 0 || strcmp(name, "unsequenced") == 0) {
        return 1;
    }
    return 0;
}

static int has_gnu_attribute_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "aligned") == 0 || strcmp(name, "packed") == 0 || strcmp(name, "section") == 0 ||
        strcmp(name, "deprecated") == 0 || strcmp(name, "noreturn") == 0 || strcmp(name, "unused") == 0 ||
        strcmp(name, "used") == 0 ||
        strcmp(name, "always_inline") == 0 || strcmp(name, "noinline") == 0 || strcmp(name, "hot") == 0 ||
        strcmp(name, "cold") == 0 || strcmp(name, "format") == 0 || strcmp(name, "nonnull") == 0 ||
        strcmp(name, "malloc") == 0 || strcmp(name, "alias") == 0 || strcmp(name, "weak") == 0 ||
        strcmp(name, "flatten") == 0 || strcmp(name, "target") == 0 || strcmp(name, "tls_model") == 0 ||
        strcmp(name, "cleanup") == 0 || strcmp(name, "visibility") == 0 ||
        strcmp(name, "transparent_union") == 0 || strcmp(name, "vector_size") == 0 ||
        strcmp(name, "ext_vector_type") == 0 ||
        strcmp(name, "may_alias") == 0) {
        return 1;
    }
    return 0;
}

static int has_declspec_attribute_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "deprecated") == 0 || strcmp(name, "noreturn") == 0 || strcmp(name, "noinline") == 0 ||
        strcmp(name, "dllexport") == 0 || strcmp(name, "dllimport") == 0 || strcmp(name, "align") == 0) {
        return 1;
    }
    return 0;
}

static int has_builtin_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "__builtin_expect") == 0 || strcmp(name, "__builtin_constant_p") == 0 ||
        strcmp(name, "__builtin_ctz") == 0 || strcmp(name, "__builtin_ctzl") == 0 ||
        strcmp(name, "__builtin_ctzll") == 0 || strcmp(name, "__builtin_clz") == 0 ||
        strcmp(name, "__builtin_clzl") == 0 || strcmp(name, "__builtin_clzll") == 0 ||
        strcmp(name, "__builtin_ffs") == 0 || strcmp(name, "__builtin_ffsl") == 0 ||
        strcmp(name, "__builtin_ffsll") == 0 ||
        strcmp(name, "__builtin_bswap16") == 0 ||
        strcmp(name, "__builtin_bswap32") == 0 || strcmp(name, "__builtin_bswap64") == 0 ||
        strcmp(name, "__builtin_add_overflow") == 0 || strcmp(name, "__builtin_sub_overflow") == 0 ||
        strcmp(name, "__builtin_mul_overflow") == 0 || strcmp(name, "__builtin_object_size") == 0 ||
        strcmp(name, "__builtin___memcpy_chk") == 0 || strcmp(name, "__builtin___memmove_chk") == 0 ||
        strcmp(name, "__builtin___memset_chk") == 0 || strcmp(name, "__builtin_va_start") == 0 ||
        strcmp(name, "__builtin_c23_va_start") == 0 || strcmp(name, "__builtin_va_end") == 0 ||
        strcmp(name, "__builtin_c23_va_end") == 0 || strcmp(name, "__builtin_va_copy") == 0 ||
        strcmp(name, "__builtin_c23_va_copy") == 0 || strcmp(name, "__builtin_va_arg") == 0 ||
        strcmp(name, "__builtin_c23_va_arg") == 0 || strcmp(name, "__builtin_trap") == 0 ||
        strcmp(name, "__builtin_unreachable") == 0 || strcmp(name, "__builtin_assume") == 0 ||
        strcmp(name, "__builtin_assume_aligned") == 0 || strcmp(name, "__builtin_unpredictable") == 0 ||
        strcmp(name, "__builtin_choose_expr") == 0 ||
        strcmp(name, "__builtin_types_compatible_p") == 0 || strcmp(name, "__builtin_offsetof") == 0) {
        return 1;
    }
    return 0;
}

static int has_feature_name(const char *name, const pp_state_t *st) {
    if (name == NULL || st == NULL) {
        return 0;
    }
    if (strcmp(name, "c_thread_local") == 0 || strcmp(name, "c_alignas") == 0 || strcmp(name, "c_alignof") == 0 ||
        strcmp(name, "c_static_assert") == 0 || strcmp(name, "c_generic_selections") == 0 ||
        strcmp(name, "c_noreturn") == 0) {
        return st->std_is_c11 || st->std_is_c17 || st->std_is_c23;
    }
    if (strcmp(name, "c_variadic_macros") == 0 || strcmp(name, "c_restrict") == 0 || strcmp(name, "c99") == 0) {
        return st->std_version >= 199901;
    }
    if (strcmp(name, "c23") == 0 || strcmp(name, "c2x") == 0) {
        return st->std_is_c23;
    }
    if (strcmp(name, "gnu_statement_expression") == 0 || strcmp(name, "gnu_labels_as_values") == 0 ||
        strcmp(name, "gnu_case_range") == 0) {
        return st->std_is_gnu;
    }
    if (strcmp(name, "blocks") == 0 || strcmp(name, "block_literals") == 0 ||
        strcmp(name, "attribute_ext_vector_type") == 0) {
        return st->std_is_gnu;
    }
    return 0;
}

static int is_reserved_identifier_name(const char *name, const pp_state_t *st) {
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strcmp(name, "auto") == 0 || strcmp(name, "break") == 0 || strcmp(name, "case") == 0 ||
        strcmp(name, "char") == 0 || strcmp(name, "const") == 0 || strcmp(name, "continue") == 0 ||
        strcmp(name, "default") == 0 || strcmp(name, "do") == 0 || strcmp(name, "double") == 0 ||
        strcmp(name, "else") == 0 || strcmp(name, "enum") == 0 || strcmp(name, "extern") == 0 ||
        strcmp(name, "float") == 0 || strcmp(name, "for") == 0 || strcmp(name, "goto") == 0 ||
        strcmp(name, "if") == 0 || strcmp(name, "inline") == 0 || strcmp(name, "int") == 0 ||
        strcmp(name, "long") == 0 || strcmp(name, "register") == 0 || strcmp(name, "restrict") == 0 ||
        strcmp(name, "return") == 0 || strcmp(name, "short") == 0 || strcmp(name, "signed") == 0 ||
        strcmp(name, "sizeof") == 0 || strcmp(name, "static") == 0 || strcmp(name, "struct") == 0 ||
        strcmp(name, "switch") == 0 || strcmp(name, "typedef") == 0 || strcmp(name, "union") == 0 ||
        strcmp(name, "unsigned") == 0 || strcmp(name, "void") == 0 || strcmp(name, "volatile") == 0 ||
        strcmp(name, "while") == 0 || strcmp(name, "_Bool") == 0 || strcmp(name, "_Complex") == 0 ||
        strcmp(name, "_Imaginary") == 0 || strcmp(name, "__typeof__") == 0 || strcmp(name, "typeof") == 0 ||
        strcmp(name, "__auto_type") == 0 || strcmp(name, "__extension__") == 0) {
        return 1;
    }
    if (st != NULL && (st->std_is_c11 || st->std_is_c17 || st->std_is_c23)) {
        if (strcmp(name, "_Alignas") == 0 || strcmp(name, "_Alignof") == 0 || strcmp(name, "_Atomic") == 0 ||
            strcmp(name, "_Generic") == 0 || strcmp(name, "_Noreturn") == 0 || strcmp(name, "_Static_assert") == 0 ||
            strcmp(name, "_Thread_local") == 0) {
            return 1;
        }
    }
    if (st != NULL && st->std_is_c23) {
        if (strcmp(name, "bool") == 0 || strcmp(name, "true") == 0 || strcmp(name, "false") == 0 ||
            strcmp(name, "nullptr") == 0 || strcmp(name, "thread_local") == 0 ||
            strcmp(name, "alignas") == 0 || strcmp(name, "alignof") == 0 ||
            strcmp(name, "static_assert") == 0) {
            return 1;
        }
    }
    return 0;
}

static int validate_stdc_pragma(const char *rest, cc_diag_t *diag, size_t line_no) {
    const char *p = rest;
    char scope[32];
    char name[64];
    char value[32];

    if (parse_ident_token(&p, scope, sizeof(scope)) != 0 || strcmp(scope, "STDC") != 0) {
        return 0;
    }
    if (parse_ident_token(&p, name, sizeof(name)) != 0 || parse_ident_token(&p, value, sizeof(value)) != 0) {
        set_diag(diag, line_no, 1, "malformed #pragma STDC");
        return -1;
    }
    p = skip_ws(p);
    if (*p != '\0') {
        set_diag(diag, line_no, 1, "malformed #pragma STDC");
        return -1;
    }

    if (strcmp(name, "FP_CONTRACT") != 0 && strcmp(name, "FENV_ACCESS") != 0 &&
        strcmp(name, "CX_LIMITED_RANGE") != 0) {
        set_diag(diag, line_no, 1, "unsupported #pragma STDC directive");
        return -1;
    }
    if (strcmp(value, "ON") != 0 && strcmp(value, "OFF") != 0 && strcmp(value, "DEFAULT") != 0) {
        set_diag(diag, line_no, 1, "invalid #pragma STDC state");
        return -1;
    }
    return 1;
}

static int validate_clang_pragma(const char *rest, cc_diag_t *diag, size_t line_no) {
    const char *p = rest;
    char scope[32];
    char name[64];

    if (parse_ident_token(&p, scope, sizeof(scope)) != 0 || strcmp(scope, "clang") != 0) {
        return 0;
    }
    if (parse_ident_token(&p, name, sizeof(name)) != 0) {
        set_diag(diag, line_no, 1, "malformed #pragma clang");
        return -1;
    }
    p = skip_ws(p);

    if (strcmp(name, "diagnostic") == 0) {
        char action[32];
        if (parse_ident_token(&p, action, sizeof(action)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang diagnostic");
            return -1;
        }
        if (strcmp(action, "push") != 0 && strcmp(action, "pop") != 0 &&
            strcmp(action, "ignored") != 0 && strcmp(action, "warning") != 0 &&
            strcmp(action, "error") != 0) {
            set_diag(diag, line_no, 1, "unsupported #pragma clang diagnostic action");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "attribute") == 0) {
        char action[16];
        p = skip_ws(p);
        if (parse_ident_token(&p, action, sizeof(action)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang attribute");
            return -1;
        }
        if (strcmp(action, "push") == 0) {
            int depth = 0;
            int nest = 0;
            const char *arg_begin = NULL;
            const char *arg_end = NULL;
            const char *comma = NULL;
            p = skip_ws(p);
            if (*p != '(') {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            arg_begin = p + 1;
            while (*p != '\0') {
                if (*p == '(') {
                    depth++;
                } else if (*p == ')') {
                    depth--;
                    if (depth == 0) {
                        p++;
                        break;
                    }
                }
                p++;
            }
            if (depth != 0) {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            arg_end = p - 1;
            p = skip_ws(p);
            if (*p != '\0') {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            p = arg_begin;
            while (p < arg_end) {
                if (*p == '(') {
                    nest++;
                } else if (*p == ')') {
                    if (nest > 0) {
                        nest--;
                    }
                } else if (*p == ',' && nest == 0) {
                    comma = p;
                    break;
                }
                p++;
            }
            if (comma == NULL) {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            p = skip_ws(comma + 1);
            if (strncmp(p, "apply_to", 8) != 0) {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            p += 8;
            p = skip_ws(p);
            if (*p != '=') {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            p++;
            p = skip_ws(p);
            if (parse_ident_token(&p, action, sizeof(action)) != 0) {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            p = skip_ws(p);
            if (p != arg_end) {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute push");
                return -1;
            }
            return 1;
        }
        if (strcmp(action, "pop") == 0) {
            p = skip_ws(p);
            if (*p != '\0') {
                set_diag(diag, line_no, 1, "malformed #pragma clang attribute pop");
                return -1;
            }
            return 1;
        }
        set_diag(diag, line_no, 1, "unsupported #pragma clang attribute action");
        return -1;
    }
    if (strcmp(name, "loop") == 0) {
        char loopopt[32];
        char loopval[32];
        p = skip_ws(p);
        if (parse_ident_token(&p, loopopt, sizeof(loopopt)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang loop");
            return -1;
        }
        p = skip_ws(p);
        if (*p != '(') {
            set_diag(diag, line_no, 1, "malformed #pragma clang loop");
            return -1;
        }
        p++;
        if (parse_ident_token(&p, loopval, sizeof(loopval)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang loop option");
            return -1;
        }
        p = skip_ws(p);
        if (*p != ')') {
            set_diag(diag, line_no, 1, "malformed #pragma clang loop option");
            return -1;
        }
        p++;
        p = skip_ws(p);
        if (*p != '\0') {
            set_diag(diag, line_no, 1, "malformed #pragma clang loop");
            return -1;
        }
        if (strcmp(loopopt, "unroll") != 0 && strcmp(loopopt, "vectorize") != 0 && strcmp(loopopt, "interleave") != 0) {
            set_diag(diag, line_no, 1, "unsupported #pragma clang loop option");
            return -1;
        }
        if (strcmp(loopval, "enable") != 0 && strcmp(loopval, "disable") != 0 && strcmp(loopval, "full") != 0) {
            set_diag(diag, line_no, 1, "unsupported #pragma clang loop state");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "section") == 0) {
        int saw = 0;
        p = skip_ws(p);
        while (*p != '\0') {
            char key[32];
            const char *q;
            size_t n = 0;
            if (parse_ident_token(&p, key, sizeof(key)) != 0) {
                set_diag(diag, line_no, 1, "malformed #pragma clang section");
                return -1;
            }
            if (strcmp(key, "text") != 0 && strcmp(key, "data") != 0 && strcmp(key, "bss") != 0 &&
                strcmp(key, "rodata") != 0) {
                set_diag(diag, line_no, 1, "unsupported #pragma clang section key");
                return -1;
            }
            p = skip_ws(p);
            if (*p != '=') {
                set_diag(diag, line_no, 1, "malformed #pragma clang section assignment");
                return -1;
            }
            p++;
            p = skip_ws(p);
            if (*p != '"') {
                set_diag(diag, line_no, 1, "malformed #pragma clang section string");
                return -1;
            }
            p++;
            q = p;
            while (*q != '\0' && *q != '"') {
                if (*q == '\\' && q[1] != '\0') {
                    q += 2;
                    continue;
                }
                q++;
            }
            if (*q != '"') {
                set_diag(diag, line_no, 1, "unterminated #pragma clang section string");
                return -1;
            }
            while (p < q) {
                if ((unsigned char)*p >= 0x20 && *p != '\\') {
                    n++;
                }
                p++;
            }
            if (n == 0) {
                set_diag(diag, line_no, 1, "empty #pragma clang section name");
                return -1;
            }
            p = q + 1;
            p = skip_ws(p);
            saw = 1;
        }
        if (!saw) {
            set_diag(diag, line_no, 1, "malformed #pragma clang section");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "fp") == 0) {
        char key[32];
        char value[32];
        p = skip_ws(p);
        if (parse_ident_token(&p, key, sizeof(key)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang fp");
            return -1;
        }
        p = skip_ws(p);
        if (*p != '(') {
            set_diag(diag, line_no, 1, "malformed #pragma clang fp");
            return -1;
        }
        p++;
        if (parse_ident_token(&p, value, sizeof(value)) != 0) {
            set_diag(diag, line_no, 1, "malformed #pragma clang fp option");
            return -1;
        }
        p = skip_ws(p);
        if (*p != ')') {
            set_diag(diag, line_no, 1, "malformed #pragma clang fp option");
            return -1;
        }
        p++;
        p = skip_ws(p);
        if (*p != '\0') {
            set_diag(diag, line_no, 1, "malformed #pragma clang fp");
            return -1;
        }
        if (strcmp(key, "contract") == 0) {
            if (strcmp(value, "on") == 0 || strcmp(value, "off") == 0 || strcmp(value, "fast") == 0) {
                return 1;
            }
        } else if (strcmp(key, "reassociate") == 0 || strcmp(key, "reciprocal") == 0) {
            if (strcmp(value, "on") == 0 || strcmp(value, "off") == 0) {
                return 1;
            }
        } else if (strcmp(key, "exceptions") == 0) {
            if (strcmp(value, "ignore") == 0 || strcmp(value, "maytrap") == 0 || strcmp(value, "strict") == 0) {
                return 1;
            }
        } else if (strcmp(key, "eval_method") == 0) {
            if (strcmp(value, "source") == 0 || strcmp(value, "double") == 0 || strcmp(value, "extended") == 0) {
                return 1;
            }
        }
        set_diag(diag, line_no, 1, "unsupported #pragma clang fp option");
        return -1;
    }

    set_diag(diag, line_no, 1, "unsupported #pragma clang directive");
    return -1;
}

static int parse_pragma_macro_name(const char *rest, const char *kw, char *out, size_t out_sz) {
    const char *p;
    size_t n = 0;
    size_t kw_len;
    if (rest == NULL || kw == NULL || out == NULL || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';
    p = skip_ws(rest);
    kw_len = strlen(kw);
    if (strncmp(p, kw, kw_len) != 0 || (!isspace((unsigned char)p[kw_len]) && p[kw_len] != '(')) {
        return 0;
    }
    p += kw_len;
    p = skip_ws(p);
    if (*p != '(') {
        return -1;
    }
    p++;
    p = skip_ws(p);
    if (*p != '"') {
        return -1;
    }
    p++;
    while (*p != '\0' && *p != '"') {
        if (n + 1 >= out_sz) {
            return -1;
        }
        out[n++] = *p++;
    }
    if (*p != '"') {
        return -1;
    }
    out[n] = '\0';
    p++;
    p = skip_ws(p);
    if (*p != ')') {
        return -1;
    }
    p++;
    p = skip_ws(p);
    if (*p != '\0') {
        return -1;
    }
    return 1;
}

static char *trim_dup(const char *s) {
    const char *a = skip_ws(s);
    const char *b = a + strlen(a);
    while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r' || b[-1] == '\n')) {
        b--;
    }
    return xstrdup_n(a, (size_t)(b - a));
}

static int add_builtin_macros(pp_state_t *st) {
    const char *size_type = st->target_bits == 32 ? "unsigned int" : "unsigned long";
    const char *ptrdiff_type = st->target_bits == 32 ? "int" : "long int";
    const char *wchar_type = "int";
    const char *ptr_size = st->target_bits == 32 ? "4" : "8";
    const char *long_max = st->target_bits == 32 ? "2147483647L" : "9223372036854775807L";
    const char *size_max = st->target_bits == 32 ? "4294967295U" : "18446744073709551615UL";
    const char *ptrdiff_max = st->target_bits == 32 ? "2147483647" : "9223372036854775807L";
    const char *uintptr_max = st->target_bits == 32 ? "4294967295U" : "18446744073709551615UL";
    char stdc_ver[32];
    if (macro_set(&st->macros, "__STDC__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    snprintf(stdc_ver, sizeof(stdc_ver), "%dL", st->std_version);
    if (macro_set(&st->macros, "__STDC_VERSION__", 0, 0, NULL, 0, stdc_ver) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__GNUC__", 0, 0, NULL, 0, "13") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__GNUC_MINOR__", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__GNUC_PATCHLEVEL__", 0, 0, NULL, 0, "0") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__GNUC_STDC_INLINE__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SIZE_TYPE__", 0, 0, NULL, 0, size_type) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__PTRDIFF_TYPE__", 0, 0, NULL, 0, ptrdiff_type) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__WCHAR_TYPE__", 0, 0, NULL, 0, wchar_type) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SIZEOF_POINTER__", 0, 0, NULL, 0, ptr_size) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SIZEOF_INT128__", 0, 0, NULL, 0, "16") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__CHAR_BIT__", 0, 0, NULL, 0, "8") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SCHAR_MAX__", 0, 0, NULL, 0, "127") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SHRT_MAX__", 0, 0, NULL, 0, "32767") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__INT_MAX__", 0, 0, NULL, 0, "2147483647") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LONG_MAX__", 0, 0, NULL, 0, long_max) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LONG_LONG_MAX__", 0, 0, NULL, 0, "9223372036854775807LL") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__WCHAR_MAX__", 0, 0, NULL, 0, "2147483647") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SIZE_MAX__", 0, 0, NULL, 0, size_max) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__PTRDIFF_MAX__", 0, 0, NULL, 0, ptrdiff_max) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__INTMAX_MAX__", 0, 0, NULL, 0, "9223372036854775807LL") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__UINTMAX_MAX__", 0, 0, NULL, 0, "18446744073709551615ULL") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__UINTPTR_MAX__", 0, 0, NULL, 0, uintptr_max) != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SCHAR_WIDTH__", 0, 0, NULL, 0, "8") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SHRT_WIDTH__", 0, 0, NULL, 0, "16") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__INT_WIDTH__", 0, 0, NULL, 0, "32") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LONG_WIDTH__", 0, 0, NULL, 0, st->target_bits == 32 ? "32" : "64") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LONG_LONG_WIDTH__", 0, 0, NULL, 0, "64") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__PTRDIFF_WIDTH__", 0, 0, NULL, 0, st->target_bits == 32 ? "32" : "64") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__SIZE_WIDTH__", 0, 0, NULL, 0, st->target_bits == 32 ? "32" : "64") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__WCHAR_WIDTH__", 0, 0, NULL, 0, "32") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_RADIX__", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MANT_DIG__", 0, 0, NULL, 0, "24") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_DIG__", 0, 0, NULL, 0, "6") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_DECIMAL_DIG__", 0, 0, NULL, 0, "9") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MIN_EXP__", 0, 0, NULL, 0, "(-125)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MAX_EXP__", 0, 0, NULL, 0, "128") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MIN_10_EXP__", 0, 0, NULL, 0, "(-37)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MAX_10_EXP__", 0, 0, NULL, 0, "38") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_EPSILON__", 0, 0, NULL, 0, "1.19209290e-7F") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MIN__", 0, 0, NULL, 0, "1.17549435e-38F") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_DENORM_MIN__", 0, 0, NULL, 0, "1.40129846e-45F") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_MAX__", 0, 0, NULL, 0, "3.40282347e+38F") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MANT_DIG__", 0, 0, NULL, 0, "53") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_DIG__", 0, 0, NULL, 0, "15") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_DECIMAL_DIG__", 0, 0, NULL, 0, "17") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MIN_EXP__", 0, 0, NULL, 0, "(-1021)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MAX_EXP__", 0, 0, NULL, 0, "1024") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MIN_10_EXP__", 0, 0, NULL, 0, "(-307)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MAX_10_EXP__", 0, 0, NULL, 0, "308") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_EPSILON__", 0, 0, NULL, 0, "2.2204460492503131e-16") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MIN__", 0, 0, NULL, 0, "2.2250738585072014e-308") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_DENORM_MIN__", 0, 0, NULL, 0, "4.9406564584124654e-324") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_MAX__", 0, 0, NULL, 0, "1.7976931348623157e+308") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MANT_DIG__", 0, 0, NULL, 0, "64") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_DIG__", 0, 0, NULL, 0, "18") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_DECIMAL_DIG__", 0, 0, NULL, 0, "21") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MIN_EXP__", 0, 0, NULL, 0, "(-16381)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MAX_EXP__", 0, 0, NULL, 0, "16384") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MIN_10_EXP__", 0, 0, NULL, 0, "(-4931)") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MAX_10_EXP__", 0, 0, NULL, 0, "4932") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_HAS_DENORM__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_HAS_DENORM__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_HAS_DENORM__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_HAS_INFINITY__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_HAS_INFINITY__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_HAS_INFINITY__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_HAS_QUIET_NAN__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_HAS_QUIET_NAN__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_HAS_QUIET_NAN__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__FLT_IS_IEC_60559__", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DBL_IS_IEC_60559__", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_IS_IEC_60559__", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_EPSILON__", 0, 0, NULL, 0, "1.08420217248550443401e-19L") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MIN__", 0, 0, NULL, 0, "3.36210314311209350626e-4932L") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_DENORM_MIN__", 0, 0, NULL, 0, "3.64519953188247460253e-4951L") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__LDBL_MAX__", 0, 0, NULL, 0, "1.18973149535723176502e+4932L") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__DECIMAL_DIG__", 0, 0, NULL, 0, "21") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_RELAXED", 0, 0, NULL, 0, "0") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_CONSUME", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_ACQUIRE", 0, 0, NULL, 0, "2") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_RELEASE", 0, 0, NULL, 0, "3") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_ACQ_REL", 0, 0, NULL, 0, "4") != 0) {
        return -1;
    }
    if (macro_set(&st->macros, "__ATOMIC_SEQ_CST", 0, 0, NULL, 0, "5") != 0) {
        return -1;
    }
    if (st->std_is_c11 || st->std_is_c17 || st->std_is_c23) {
        if (macro_set(&st->macros, "__STDC_NO_THREADS__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__STDC_NO_COMPLEX__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__STDC_UTF_16__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__STDC_UTF_32__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
    }
    if (st->std_is_c23) {
        if (macro_set(&st->macros, "__STDC_EMBED_NOT_FOUND__", 0, 0, NULL, 0, "0") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__STDC_EMBED_FOUND__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__STDC_EMBED_EMPTY__", 0, 0, NULL, 0, "2") != 0) {
            return -1;
        }
    }
    if (st->target_bits == 32) {
        if (macro_set(&st->macros, "__i386__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (st->target_has_mmx) {
            if (macro_set(&st->macros, "__MMX__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
        if (st->target_has_sse) {
            if (macro_set(&st->macros, "__SSE__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
        if (st->target_has_sse2) {
            if (macro_set(&st->macros, "__SSE2__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
        if (st->target_has_avx) {
            if (macro_set(&st->macros, "__AVX__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
        if (st->target_has_avx2) {
            if (macro_set(&st->macros, "__AVX2__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
    } else {
        if (macro_set(&st->macros, "__x86_64__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__LP64__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "_LP64", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__MMX__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__SSE__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (macro_set(&st->macros, "__SSE2__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
        if (st->target_has_avx) {
            if (macro_set(&st->macros, "__AVX__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
        if (st->target_has_avx2) {
            if (macro_set(&st->macros, "__AVX2__", 0, 0, NULL, 0, "1") != 0) {
                return -1;
            }
        }
    }
    if (st->std_is_gnu) {
        if (macro_set(&st->macros, "__BLOCKS__", 0, 0, NULL, 0, "1") != 0) {
            return -1;
        }
    }
    return 0;
}

static void scan_target_flags(pp_state_t *st, const char *const *flags, size_t flag_count) {
    size_t i;
    int arch_explicit = 0;

    st->target_has_sse = 0;
    st->target_has_sse2 = 1;
    st->target_has_mmx = 1;
    st->target_has_avx = 0;
    st->target_has_avx2 = 0;
    st->target_supports_sse = 1;
    st->target_supports_sse2 = 1;
    st->target_supports_mmx = 1;
    st->target_supports_avx = 1;
    st->target_supports_avx2 = 1;

    if (st->target_bits == 32) {
        st->target_has_sse = 0;
        st->target_has_sse2 = 0;
        st->target_has_mmx = 1;
        st->target_has_avx = 0;
        st->target_has_avx2 = 0;
        st->target_supports_sse = 0;
        st->target_supports_sse2 = 0;
        st->target_supports_mmx = 1;
        st->target_supports_avx = 0;
        st->target_supports_avx2 = 0;
    }

    for (i = 0; i < flag_count; ++i) {
        const char *f = flags[i];
        if (strcmp(f, "-m32") == 0) {
            st->target_bits = 32;
            if (!arch_explicit) {
                st->target_has_sse = 0;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 1;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 0;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            }
        } else if (strcmp(f, "-m64") == 0) {
            st->target_bits = 64;
            st->target_has_sse = 1;
            st->target_has_sse2 = 1;
            st->target_has_mmx = 1;
            st->target_has_avx = 0;
            st->target_has_avx2 = 0;
            st->target_supports_sse = 1;
            st->target_supports_sse2 = 1;
            st->target_supports_mmx = 1;
            st->target_supports_avx = 1;
            st->target_supports_avx2 = 1;
        } else if ((strcmp(f, "-march") == 0 || strcmp(f, "-mcpu") == 0) && i + 1 < flag_count) {
            const char *name = flags[++i];
            arch_explicit = 1;
            if (strcmp(name, "i386") == 0 || strcmp(name, "i486") == 0) {
                st->target_has_sse = 0;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 0;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 0;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 0;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            } else if (strcmp(name, "i586") == 0 || strcmp(name, "pentium") == 0) {
                st->target_has_sse = 0;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 0;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 0;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            } else if (strcmp(name, "pentium-mmx") == 0) {
                st->target_has_sse = 0;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 1;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 0;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            } else if (strcmp(name, "i686") == 0 || strcmp(name, "pentiumpro") == 0 || strcmp(name, "pentium2") == 0) {
                st->target_has_sse = 0;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 1;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 0;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            } else if (strcmp(name, "pentium3") == 0) {
                st->target_has_sse = 1;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 1;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 1;
                st->target_supports_sse2 = 0;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
            } else if (strcmp(name, "pentium4") == 0 || strcmp(name, "prescott") == 0 || strcmp(name, "nocona") == 0 ||
                       strcmp(name, "core2") == 0 || strcmp(name, "x86-64") == 0 || strcmp(name, "x86-64-v1") == 0 ||
                       strcmp(name, "x86-64-v2") == 0 || strcmp(name, "x86-64-v3") == 0 || strcmp(name, "x86-64-v4") == 0) {
                st->target_has_sse = 1;
                st->target_has_sse2 = 0;
                st->target_has_mmx = 1;
                st->target_has_avx = 0;
                st->target_has_avx2 = 0;
                st->target_supports_sse = 1;
                st->target_supports_sse2 = 1;
                st->target_supports_mmx = 1;
                st->target_supports_avx = 0;
                st->target_supports_avx2 = 0;
                if (strcmp(name, "x86-64-v3") == 0 || strcmp(name, "x86-64-v4") == 0) {
                    st->target_has_avx = 1;
                    st->target_has_avx2 = 1;
                    st->target_supports_avx = 1;
                    st->target_supports_avx2 = 1;
                }
            }
        } else if (strncmp(f, "-march=", 7) == 0 || strncmp(f, "-mcpu=", 6) == 0) {
            const char *name = strchr(f, '=');
            if (name != NULL) {
                const char *tmpv[2];
                tmpv[0] = "-march";
                tmpv[1] = name + 1;
                scan_target_flags(st, tmpv, 2);
            }
        } else if (strcmp(f, "-msse") == 0) {
            if (st->target_supports_sse) {
                st->target_has_sse = 1;
            }
        } else if (strcmp(f, "-mno-sse") == 0) {
            st->target_has_sse = 0;
        } else if (strcmp(f, "-msse2") == 0) {
            if (st->target_supports_sse2) {
                st->target_has_sse2 = 1;
            }
        } else if (strcmp(f, "-mno-sse2") == 0) {
            st->target_has_sse2 = 0;
        } else if (strcmp(f, "-mmmx") == 0) {
            if (st->target_supports_mmx) {
                st->target_has_mmx = 1;
            }
        } else if (strcmp(f, "-mno-mmx") == 0) {
            st->target_has_mmx = 0;
        } else if (strcmp(f, "-mavx") == 0) {
            if (st->target_supports_avx) {
                st->target_has_avx = 1;
            }
        } else if (strcmp(f, "-mno-avx") == 0) {
            st->target_has_avx = 0;
            st->target_has_avx2 = 0;
        } else if (strcmp(f, "-mavx2") == 0) {
            if (st->target_supports_avx2) {
                st->target_has_avx = 1;
                st->target_has_avx2 = 1;
            }
        } else if (strcmp(f, "-mno-avx2") == 0) {
            st->target_has_avx2 = 0;
        }
    }
}

static FILE *safe_popen_read(const char *cmd, const char *arg1, pid_t *out_pid) {
    int fds[2];
    if (pipe(fds) < 0) {
        return NULL;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        char *const argv[] = {(char *)cmd, (char *)arg1, NULL};
        execvp(cmd, argv);
        exit(127);
    }
    close(fds[1]);
    *out_pid = pid;
    return fdopen(fds[0], "r");
}

static int add_default_include_paths(pp_state_t *st) {
    DIR *d;
    struct dirent *ent;
    const char *tool_dirs[] = {"include", "include-fixed"};
    const char *host_cc = NULL;
    size_t ti;
    if (st->no_default_includes) {
        return 0;
    }
    if (strvec_push_unique(&st->system_include_paths, "/usr/local/include") != 0) {
        return -1;
    }
    if (strvec_push_unique(&st->system_include_paths, "/usr/include") != 0) {
        return -1;
    }
    host_cc = getenv("CC_BOOTSTRAP");
    if (host_cc == NULL || host_cc[0] == '\0') {
        host_cc = getenv("HOSTCC");
    }
    if (host_cc == NULL || host_cc[0] == '\0') {
        host_cc = "/usr/bin/cc";
    }
    if (!path_exists(host_cc)) {
        host_cc = "cc";
    }
    for (ti = 0; ti < sizeof(tool_dirs) / sizeof(tool_dirs[0]); ++ti) {
        char arg1[PATH_MAX + 64];
        char buf[PATH_MAX];
        FILE *fp;
        size_t n;
        pid_t child_pid;
        if (strchr(host_cc, ' ') != NULL || strchr(host_cc, '\t') != NULL) {
            continue;
        }
        if (snprintf(arg1, sizeof(arg1), "-print-file-name=%s", tool_dirs[ti]) >= (int)sizeof(arg1)) {
            continue;
        }
        fp = safe_popen_read(host_cc, arg1, &child_pid);
        if (fp == NULL) {
            continue;
        }
        if (fgets(buf, sizeof(buf), fp) != NULL) {
            n = strlen(buf);
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
                buf[--n] = '\0';
            }
            if (buf[0] != '\0' && strcmp(buf, tool_dirs[ti]) != 0 && dir_exists(buf)) {
                if (strvec_push_unique(&st->system_include_paths, buf) != 0) {
                    fclose(fp);
                    waitpid(child_pid, NULL, 0);
                    return -1;
                }
            }
        }
        fclose(fp);
        waitpid(child_pid, NULL, 0);
    }
    d = opendir("/usr/include");
    if (d == NULL) {
        return 0;
    }
    while ((ent = readdir(d)) != NULL) {
        char base[PATH_MAX];
        char bits_dir[PATH_MAX];
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (snprintf(base, sizeof(base), "/usr/include/%s", ent->d_name) >= (int)sizeof(base)) {
            continue;
        }
        if (!dir_exists(base)) {
            continue;
        }
        if (snprintf(bits_dir, sizeof(bits_dir), "%s/bits", base) >= (int)sizeof(bits_dir)) {
            continue;
        }
        if (!dir_exists(bits_dir)) {
            continue;
        }
        if (strvec_push_unique(&st->system_include_paths, base) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

static void dump_include_paths(const pp_state_t *st) {
    size_t i;
    fprintf(stderr, "cpp include search paths:\n");
    if (st->quote_paths.count > 0) {
        fprintf(stderr, "  quote:\n");
        for (i = 0; i < st->quote_paths.count; ++i) {
            fprintf(stderr, "    %s\n", st->quote_paths.items[i]);
        }
    }
    if (st->user_include_paths.count > 0) {
        fprintf(stderr, "  user:\n");
        for (i = 0; i < st->user_include_paths.count; ++i) {
            fprintf(stderr, "    %s\n", st->user_include_paths.items[i]);
        }
    }
    if (st->system_include_paths.count > 0) {
        fprintf(stderr, "  system:\n");
        for (i = 0; i < st->system_include_paths.count; ++i) {
            fprintf(stderr, "    %s\n", st->system_include_paths.items[i]);
        }
    }
}

static int dep_add_path(pp_state_t *st, const char *path, int is_system, int is_main) {
    if (!st->dep_emit) {
        return 0;
    }
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (st->dep_user_only && is_system && !is_main) {
        return 0;
    }
    return strvec_push_unique(&st->dep_paths, path);
}

static int derive_default_dep_target(const char *in_path, sb_t *out) {
    const char *dot = strrchr(in_path, '.');
    size_t n = dot == NULL ? strlen(in_path) : (size_t)(dot - in_path);
    if (sb_append_n(out, in_path, n) != 0 || sb_append(out, ".o") != 0) {
        return -1;
    }
    return 0;
}

static int emit_dependency_file(pp_state_t *st, const char *in_path) {
    sb_t dep;
    FILE *fp = NULL;
    size_t i;
    memset(&dep, 0, sizeof(dep));

    if (!st->dep_emit) {
        return 0;
    }

    if (st->dep_target != NULL && st->dep_target[0] != '\0') {
        if (sb_append_escaped_make_target(&dep, st->dep_target, st->target_quote) != 0) {
            goto fail;
        }
    } else {
        if (derive_default_dep_target(in_path, &dep) != 0) {
            goto fail;
        }
    }
    if (sb_append(&dep, ":") != 0) {
        goto fail;
    }
    for (i = 0; i < st->dep_paths.count; ++i) {
        if (sb_append(&dep, " \\\n  ") != 0 || sb_append_escaped_make_target(&dep, st->dep_paths.items[i], 0) != 0) {
            goto fail;
        }
    }
    if (sb_append(&dep, "\n") != 0) {
        goto fail;
    }

    if (st->dep_stdout_only) {
        if (fputs(dep.buf != NULL ? dep.buf : "", stdout) < 0) {
            goto fail;
        }
    } else {
        char dep_default[PATH_MAX];
        const char *out_path = st->dep_file;
        if (out_path == NULL || out_path[0] == '\0') {
            const char *dot = strrchr(in_path, '.');
            size_t n = dot == NULL ? strlen(in_path) : (size_t)(dot - in_path);
            int res = snprintf(dep_default, sizeof(dep_default), "%.*s.d", (int)n, in_path);
            if (res < 0 || res >= (int)sizeof(dep_default)) {
                goto fail;
            }
            out_path = dep_default;
        }
        fp = fopen(out_path, "w");
        if (fp == NULL) {
            goto fail;
        }
        if (fputs(dep.buf != NULL ? dep.buf : "", fp) < 0) {
            fclose(fp);
            fp = NULL;
            goto fail;
        }
        fclose(fp);
        fp = NULL;
    }

    sb_free(&dep);
    return 0;

fail:
    if (fp != NULL) {
        fclose(fp);
    }
    sb_free(&dep);
    return -1;
}

static int macro_name_cmp(const void *ap, const void *bp) {
    const pp_macro_t *const *a = (const pp_macro_t *const *)ap;
    const pp_macro_t *const *b = (const pp_macro_t *const *)bp;
    return strcmp((*a)->name, (*b)->name);
}

static int emit_macro_dump(pp_state_t *st, FILE *out) {
    pp_macro_t **sorted = NULL;
    size_t i;
    if (!st->dump_macros) {
        return 0;
    }
    if (st->macros.count == 0) {
        return 0;
    }
    sorted = (pp_macro_t **)calloc(st->macros.count, sizeof(*sorted));
    if (sorted == NULL) {
        return -1;
    }
    for (i = 0; i < st->macros.count; ++i) {
        sorted[i] = &st->macros.items[i];
    }
    qsort(sorted, st->macros.count, sizeof(*sorted), macro_name_cmp);
    for (i = 0; i < st->macros.count; ++i) {
        pp_macro_t *m = sorted[i];
        if (m->is_function) {
            size_t p;
            if (fprintf(out, "#define %s(", m->name) < 0) {
                free(sorted);
                return -1;
            }
            for (p = 0; p < m->param_count; ++p) {
                if (p > 0 && fputs(", ", out) < 0) {
                    free(sorted);
                    return -1;
                }
                if (fputs(m->params[p], out) < 0) {
                    free(sorted);
                    return -1;
                }
            }
            if (m->is_variadic) {
                if (m->param_count > 0 && fputs(", ", out) < 0) {
                    free(sorted);
                    return -1;
                }
                if (fputs("...", out) < 0) {
                    free(sorted);
                    return -1;
                }
            }
            if (fprintf(out, ") %s\n", m->body != NULL ? m->body : "") < 0) {
                free(sorted);
                return -1;
            }
        } else {
            if (fprintf(out, "#define %s %s\n", m->name, m->body != NULL ? m->body : "") < 0) {
                free(sorted);
                return -1;
            }
        }
    }
    free(sorted);
    return 0;
}

static int parse_cmd_define(pp_state_t *st, const char *arg) {
    const char *eq = strchr(arg, '=');
    char *name = NULL;
    char *body = NULL;
    int rc = -1;
    if (eq == NULL) {
        name = xstrdup(arg);
        body = xstrdup("1");
    } else {
        name = xstrdup_n(arg, (size_t)(eq - arg));
        body = xstrdup(eq + 1);
    }
    if (name == NULL || body == NULL) {
        goto out;
    }
    rc = macro_set(&st->macros, name, 0, 0, NULL, 0, body);
out:
    free(name);
    free(body);
    return rc;
}

static int append_dep_target(pp_state_t *st, const char *value, int quote_mode) {
    sb_t sb;
    memset(&sb, 0, sizeof(sb));
    if (st->dep_target != NULL && st->dep_target[0] != '\0') {
        if (sb_append(&sb, st->dep_target) != 0 || sb_append_c(&sb, ' ') != 0) {
            sb_free(&sb);
            return -1;
        }
    }
    if (sb_append_escaped_make_target(&sb, value, quote_mode) != 0) {
        sb_free(&sb);
        return -1;
    }
    free(st->dep_target);
    st->dep_target = sb.buf != NULL ? sb.buf : xstrdup("");
    if (st->dep_target == NULL) {
        return -1;
    }
    if (sb.buf == NULL) {
        sb_free(&sb);
    }
    return 0;
}

static int apply_flags(pp_state_t *st, const char *const *flags, size_t flag_count) {
    size_t i;
    for (i = 0; i < flag_count; ++i) {
        const char *f = flags[i];
        if (strcmp(f, "-P") == 0) {
            st->emit_line_markers = 0;
            continue;
        }
        if (strcmp(f, "-dM") == 0) {
            st->dump_macros = 1;
            st->suppress_output = 1;
            continue;
        }
        if (strcmp(f, "-v") == 0) {
            st->show_include_paths = 1;
            continue;
        }
        if (strcmp(f, "-M") == 0 || strcmp(f, "-MM") == 0) {
            st->dep_emit = 1;
            st->dep_stdout_only = 1;
            st->suppress_output = 1;
            st->dep_user_only = strcmp(f, "-MM") == 0;
            continue;
        }
        if (strcmp(f, "-MD") == 0 || strcmp(f, "-MMD") == 0) {
            st->dep_emit = 1;
            st->dep_stdout_only = 0;
            st->dep_user_only = strcmp(f, "-MMD") == 0;
            continue;
        }
        if (f[0] != '-' && st->dep_emit && !st->dep_stdout_only && st->dep_file == NULL) {
            /*
             * GCC-compatible handling for -Wp,-MD,<file> and -Wp,-MMD,<file>.
             */
            free(st->dep_file);
            st->dep_file = xstrdup(f);
            if (st->dep_file == NULL) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-MF") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            free(st->dep_file);
            st->dep_file = xstrdup(flags[++i]);
            if (st->dep_file == NULL) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-MF", 3) == 0 && strlen(f) > 3) {
            free(st->dep_file);
            st->dep_file = xstrdup(f + 3);
            if (st->dep_file == NULL) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-MT") == 0 || strcmp(f, "-MQ") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            if (append_dep_target(st, flags[++i], strcmp(f, "-MQ") == 0) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-MT", 3) == 0 && strlen(f) > 3) {
            if (append_dep_target(st, f + 3, 0) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-MQ", 3) == 0 && strlen(f) > 3) {
            if (append_dep_target(st, f + 3, 1) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-include") == 0 || strcmp(f, "-imacros") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            if (strcmp(f, "-include") == 0) {
                if (strvec_push(&st->force_includes, flags[++i]) != 0) {
                    return -1;
                }
            } else {
                if (strvec_push(&st->force_imacros, flags[++i]) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (strncmp(f, "-include", 8) == 0 && strlen(f) > 8) {
            if (strvec_push(&st->force_includes, f + 8) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-imacros", 8) == 0 && strlen(f) > 8) {
            if (strvec_push(&st->force_imacros, f + 8) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-I") == 0 || strcmp(f, "-isystem") == 0 || strcmp(f, "-iquote") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            if (strcmp(f, "-I") == 0) {
                if (strvec_push(&st->user_include_paths, flags[++i]) != 0)
                    return -1;
            } else if (strcmp(f, "-isystem") == 0) {
                if (strvec_push(&st->system_include_paths, flags[++i]) != 0)
                    return -1;
            } else {
                if (strvec_push(&st->quote_paths, flags[++i]) != 0)
                    return -1;
            }
            continue;
        }
        if (strncmp(f, "-I", 2) == 0 && strlen(f) > 2) {
            if (strvec_push(&st->user_include_paths, f + 2) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-isystem", 8) == 0 && strlen(f) > 8) {
            if (strvec_push(&st->system_include_paths, f + 8) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-iquote", 7) == 0 && strlen(f) > 7) {
            if (strvec_push(&st->quote_paths, f + 7) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-nostdinc") == 0) {
            st->no_default_includes = 1;
            continue;
        }
        if (strcmp(f, "-fpp-max-include-depth") == 0 || strcmp(f, "-fpp-max-macro-depth") == 0 ||
            strcmp(f, "-fpp-max-expand-passes") == 0 || strcmp(f, "-fpp-max-expanded-bytes") == 0 ||
            strcmp(f, "-fpp-max-output-bytes") == 0 || strcmp(f, "-fpp-max-tokens") == 0) {
            size_t val = 0;
            if (i + 1 >= flag_count || parse_limit_value(flags[++i], &val) != 0) {
                return -1;
            }
            if (strcmp(f, "-fpp-max-include-depth") == 0) {
                st->max_include_depth = val;
            } else if (strcmp(f, "-fpp-max-macro-depth") == 0) {
                st->max_expand_depth = val;
            } else if (strcmp(f, "-fpp-max-expand-passes") == 0) {
                st->max_expand_passes = val;
            } else if (strcmp(f, "-fpp-max-expanded-bytes") == 0) {
                st->max_expanded_text = val;
            } else if (strcmp(f, "-fpp-max-output-bytes") == 0) {
                st->max_output_size = val;
            } else {
                st->max_expanded_tokens = val;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-include-depth=", 24) == 0) {
            if (parse_limit_value(f + 24, &st->max_include_depth) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-macro-depth=", 22) == 0) {
            if (parse_limit_value(f + 22, &st->max_expand_depth) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-expand-passes=", 24) == 0) {
            if (parse_limit_value(f + 24, &st->max_expand_passes) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-expanded-bytes=", 24) == 0) {
            if (parse_limit_value(f + 24, &st->max_expanded_text) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-output-bytes=", 22) == 0) {
            if (parse_limit_value(f + 22, &st->max_output_size) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-fpp-max-tokens=", 17) == 0) {
            if (parse_limit_value(f + 17, &st->max_expanded_tokens) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-D") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            if (parse_cmd_define(st, flags[++i]) != 0) {
                return -1;
            }
            continue;
        }
        if (strncmp(f, "-D", 2) == 0 && strlen(f) > 2) {
            if (parse_cmd_define(st, f + 2) != 0) {
                return -1;
            }
            continue;
        }
        if (strcmp(f, "-U") == 0) {
            if (i + 1 >= flag_count) {
                return -1;
            }
            macro_unset(&st->macros, flags[++i]);
            continue;
        }
        if (strncmp(f, "-U", 2) == 0 && strlen(f) > 2) {
            macro_unset(&st->macros, f + 2);
            continue;
        }
    }
    return 0;
}

static char *dirname_dup(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
    return xstrdup_n(path, (size_t)(slash - path));
}

static int path_exists(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static int dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static void path_make_key(const char *path, char out[PATH_MAX]) {
    char real_buf[PATH_MAX];
    size_t n;
    if (out == NULL) {
        return;
    }
    if (path == NULL || path[0] == '\0') {
        out[0] = '\0';
        return;
    }
    if (realpath(path, real_buf) != NULL) {
        snprintf(out, PATH_MAX, "%s", real_buf);
        return;
    }
    while (path[0] == '.' && path[1] == '/') {
        path += 2;
    }
    if (path[0] == '\0') {
        snprintf(out, PATH_MAX, ".");
        return;
    }
    snprintf(out, PATH_MAX, "%s", path);
    n = strlen(out);
    while (n > 1 && out[n - 1] == '/') {
        out[n - 1] = '\0';
        n--;
    }
}

static int path_keys_equal(const char *a, const char *b) {
    char ka[PATH_MAX];
    char kb[PATH_MAX];
    path_make_key(a, ka);
    path_make_key(b, kb);
    return strcmp(ka, kb) == 0;
}

static int resolve_include(pp_state_t *st, const char *cur_file, const char *spec, int quoted, char out[PATH_MAX],
                           int *out_is_system) {
    size_t i;
    char cand[PATH_MAX];
    if (spec[0] == '/' && path_exists(spec)) {
        path_make_key(spec, out);
        if (out_is_system != NULL) {
            *out_is_system = 0;
        }
        return 0;
    }
    if (quoted) {
        char *dir = dirname_dup(cur_file);
        if (dir == NULL) {
            return -1;
        }
        if (snprintf(cand, sizeof(cand), "%s/%s", dir, spec) < (int)sizeof(cand) && path_exists(cand)) {
            path_make_key(cand, out);
            if (out_is_system != NULL) {
                *out_is_system = 0;
            }
            free(dir);
            return 0;
        }
        free(dir);
    }
    if (quoted) {
        for (i = 0; i < st->quote_paths.count; ++i) {
            if (snprintf(cand, sizeof(cand), "%s/%s", st->quote_paths.items[i], spec) >= (int)sizeof(cand))
                continue;
            if (path_exists(cand)) {
                path_make_key(cand, out);
                if (out_is_system != NULL) {
                    *out_is_system = 0;
                }
                return 0;
            }
        }
    }
    for (i = 0; i < st->user_include_paths.count; ++i) {
        if (snprintf(cand, sizeof(cand), "%s/%s", st->user_include_paths.items[i], spec) >= (int)sizeof(cand)) {
            continue;
        }
        if (path_exists(cand)) {
            path_make_key(cand, out);
            if (out_is_system != NULL) {
                *out_is_system = 0;
            }
            return 0;
        }
    }
    for (i = 0; i < st->system_include_paths.count; ++i) {
        if (snprintf(cand, sizeof(cand), "%s/%s", st->system_include_paths.items[i], spec) >= (int)sizeof(cand))
            continue;
        if (path_exists(cand)) {
            path_make_key(cand, out);
            if (out_is_system != NULL) {
                *out_is_system = 1;
            }
            return 0;
        }
    }
    return -1;
}

static int resolve_include_next(pp_state_t *st, const char *cur_file, const char *spec, int quoted, char out[PATH_MAX],
                                int *out_is_system) {
    size_t i;
    char cand[PATH_MAX];
    char fallback[PATH_MAX];
    int fallback_is_system = 0;
    int have_fallback = 0;
    int found_current = 0;

    if (quoted) {
        for (i = 0; i < st->quote_paths.count; ++i) {
            if (snprintf(cand, sizeof(cand), "%s/%s", st->quote_paths.items[i], spec) >= (int)sizeof(cand)) {
                continue;
            }
            if (!path_exists(cand)) {
                continue;
            }
            if (cur_file != NULL && path_keys_equal(cand, cur_file)) {
                found_current = 1;
                continue;
            }
            if (found_current) {
                path_make_key(cand, out);
                if (out_is_system != NULL) {
                    *out_is_system = 0;
                }
                return 0;
            }
            if (!have_fallback) {
                path_make_key(cand, fallback);
                fallback_is_system = 0;
                have_fallback = 1;
            }
        }
    }

    for (i = 0; i < st->user_include_paths.count; ++i) {
        if (snprintf(cand, sizeof(cand), "%s/%s", st->user_include_paths.items[i], spec) >= (int)sizeof(cand)) {
            continue;
        }
        if (!path_exists(cand)) {
            continue;
        }
        if (cur_file != NULL && path_keys_equal(cand, cur_file)) {
            found_current = 1;
            continue;
        }
        if (found_current) {
            path_make_key(cand, out);
            if (out_is_system != NULL) {
                *out_is_system = 0;
            }
            return 0;
        }
        if (!have_fallback) {
            path_make_key(cand, fallback);
            fallback_is_system = 0;
            have_fallback = 1;
        }
    }

    for (i = 0; i < st->system_include_paths.count; ++i) {
        if (snprintf(cand, sizeof(cand), "%s/%s", st->system_include_paths.items[i], spec) >= (int)sizeof(cand)) {
            continue;
        }
        if (!path_exists(cand)) {
            continue;
        }
        if (cur_file != NULL && path_keys_equal(cand, cur_file)) {
            found_current = 1;
            continue;
        }
        if (found_current) {
            path_make_key(cand, out);
            if (out_is_system != NULL) {
                *out_is_system = 1;
            }
            return 0;
        }
        if (!have_fallback) {
            path_make_key(cand, fallback);
            fallback_is_system = 1;
            have_fallback = 1;
        }
    }

    if (have_fallback) {
        snprintf(out, PATH_MAX, "%s", fallback);
        if (out_is_system != NULL) {
            *out_is_system = fallback_is_system;
        }
        return 0;
    }
    return -1;
}

static int resolve_forced_include(pp_state_t *st, const char *main_file, const char *spec, char out[PATH_MAX],
                                  int *out_is_system) {
    if (path_exists(spec)) {
        path_make_key(spec, out);
        if (out_is_system != NULL) {
            *out_is_system = 0;
        }
        return 0;
    }
    return resolve_include(st, main_file, spec, 1, out, out_is_system);
}

static int once_contains(pp_state_t *st, const char *path) {
    size_t i;
    char key[PATH_MAX];
    path_make_key(path, key);
    for (i = 0; i < st->include_once.count; ++i) {
        if (strcmp(st->include_once.items[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

static int once_add(pp_state_t *st, const char *path) {
    char key[PATH_MAX];
    path_make_key(path, key);
    if (once_contains(st, key)) {
        return 0;
    }
    return strvec_push(&st->include_once, key);
}

static int read_logical_line(FILE *fp, sb_t *out, int *out_had_line, int *line_counter, int *out_line_no,
                             int enable_trigraphs) {
    int c;
    int got_any = 0;
    int continue_line = 0;
    int start_line;

    if (line_counter == NULL || out_line_no == NULL) {
        return -1;
    }
    start_line = *line_counter;

    out->len = 0;
    if (out->buf != NULL) {
        out->buf[0] = '\0';
    }

    do {
        continue_line = 0;
        while ((c = fgetc(fp)) != EOF) {
            got_any = 1;
            if (c == '\n') {
                (*line_counter)++;
                if (enable_trigraphs && out->buf != NULL) {
                    out->len = normalize_trigraphs(out->buf, out->len);
                    out->buf[out->len] = '\0';
                }
                if (out->len > 0 && out->buf[out->len - 1] == '\\') {
                    out->len--;
                    out->buf[out->len] = '\0';
                    continue_line = 1;
                }
                break;
            }
            if (sb_append_c(out, (char)c) != 0) {
                return -1;
            }
        }
        if (c == EOF) {
            break;
        }
    } while (continue_line);

    if (enable_trigraphs && out->buf != NULL && out->len > 0) {
        out->len = normalize_trigraphs(out->buf, out->len);
        out->buf[out->len] = '\0';
    }

    *out_had_line = got_any;
    *out_line_no = start_line;
    return 0;
}

static int strip_comments_line(const char *in, int *in_block_comment, sb_t *out) {
    size_t i = 0;
    out->len = 0;
    if (out->buf != NULL) {
        out->buf[0] = '\0';
    }
    while (in[i] != '\0') {
        if (*in_block_comment) {
            if (in[i] == '*' && in[i + 1] == '/') {
                *in_block_comment = 0;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (in[i] == '"' || in[i] == '\'') {
            char q = in[i];
            if (sb_append_c(out, in[i]) != 0) {
                return -1;
            }
            i++;
            while (in[i] != '\0') {
                if (sb_append_c(out, in[i]) != 0) {
                    return -1;
                }
                if (in[i] == '\\' && in[i + 1] != '\0') {
                    i++;
                    if (sb_append_c(out, in[i]) != 0) {
                        return -1;
                    }
                } else if (in[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (in[i] == '/' && in[i + 1] == '*') {
            *in_block_comment = 1;
            if (sb_append_c(out, ' ') != 0) {
                return -1;
            }
            i += 2;
            continue;
        }
        if (in[i] == '/' && in[i + 1] == '/') {
            break;
        }
        if (sb_append_c(out, in[i]) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

static int parse_int_literal(const char *s, long long *out) {
    const char *p;
    char *end;
    unsigned long long uv;
    int has_unsigned = 0;

    if (s == NULL || *s == '\0') {
        return -1;
    }

    p = s;
    if (*p == '+' || *p == '-') {
        p++;
    }
    while (*p != '\0' && (isalnum((unsigned char)*p) || *p == 'x' || *p == 'X')) {
        p++;
    }
    while (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L') {
        if (*p == 'u' || *p == 'U') {
            has_unsigned = 1;
        }
        p++;
    }
    if (*p != '\0') {
        return -1;
    }

    errno = 0;
    uv = strtoull(s, &end, 0);
    if (end == s || errno == ERANGE) {
        return -1;
    }
    while (*end == 'u' || *end == 'U' || *end == 'l' || *end == 'L') {
        if (*end == 'u' || *end == 'U') {
            has_unsigned = 1;
        }
        end++;
    }
    if (*end != '\0') {
        return -1;
    }

    if (has_unsigned && uv > (unsigned long long)LLONG_MAX) {
        *out = LLONG_MAX;
    } else {
        *out = (long long)uv;
    }
    return 0;
}

static int hex_digit_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int parse_pp_char_escape(const char *s, size_t *pos, long long *out) {
    int c;
    long long v = 0;
    int hv;
    size_t i = *pos;
    c = (unsigned char)s[i];
    if (c >= '0' && c <= '7') {
        int digits = 0;
        while (digits < 3 && s[i] >= '0' && s[i] <= '7') {
            v = (v << 3) + (s[i] - '0');
            i++;
            digits++;
        }
        *pos = i;
        *out = v;
        return 0;
    }
    if (c == 'x') {
        i++;
        while ((hv = hex_digit_value((unsigned char)s[i])) >= 0) {
            v = (v << 4) | hv;
            i++;
        }
        *pos = i;
        *out = v;
        return 0;
    }
    switch (c) {
    case 'a':
        v = '\a';
        i++;
        break;
    case 'b':
        v = '\b';
        i++;
        break;
    case 'f':
        v = '\f';
        i++;
        break;
    case 'n':
        v = '\n';
        i++;
        break;
    case 'r':
        v = '\r';
        i++;
        break;
    case 't':
        v = '\t';
        i++;
        break;
    case 'v':
        v = '\v';
        i++;
        break;
    case '\\':
        v = '\\';
        i++;
        break;
    case '\'':
        v = '\'';
        i++;
        break;
    case '\"':
        v = '\"';
        i++;
        break;
    case '\0':
        return -1;
    default:
        v = (unsigned char)c;
        i++;
        break;
    }
    *pos = i;
    *out = v;
    return 0;
}

static int parse_pp_char_literal(expr_parser_t *p, long long *out) {
    size_t i = p->pos;
    long long v = 0;
    int saw = 0;
    if (p->s[i] == 'L' || p->s[i] == 'u' || p->s[i] == 'U') {
        i++;
    }
    if (p->s[i] != '\'') {
        return 0;
    }
    i++;
    while (p->s[i] != '\0' && p->s[i] != '\'') {
        long long ch;
        if (p->s[i] == '\\') {
            i++;
            if (parse_pp_char_escape(p->s, &i, &ch) != 0) {
                return -1;
            }
        } else {
            ch = (unsigned char)p->s[i];
            i++;
        }
        v = (v << 8) | (ch & 0xff);
        saw = 1;
    }
    if (p->s[i] != '\'') {
        return -1;
    }
    if (!saw) {
        return -1;
    }
    i++;
    p->pos = i;
    *out = v;
    return 1;
}

static void expr_skip_ws(expr_parser_t *p) {
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' || p->s[p->pos] == '\r' || p->s[p->pos] == '\n') {
        p->pos++;
    }
}

static int expr_match(expr_parser_t *p, const char *tok) {
    size_t n = strlen(tok);
    expr_skip_ws(p);
    if (strncmp(p->s + p->pos, tok, n) == 0) {
        if (n == 1 && tok[0] == '&' && p->s[p->pos + 1] == '&') {
            return 0;
        }
        if (n == 1 && tok[0] == '|' && p->s[p->pos + 1] == '|') {
            return 0;
        }
        p->pos += n;
        return 1;
    }
    return 0;
}

static long long parse_expr_or(expr_parser_t *p, int *ok);
static long long parse_expr_cond(expr_parser_t *p, int *ok);

static long long parse_expr_primary(expr_parser_t *p, int *ok) {
    long long v;
    int chr;
    size_t start;
    char *tmp;
    expr_skip_ws(p);
    if (expr_match(p, "(")) {
        v = parse_expr_cond(p, ok);
        if (!expr_match(p, ")")) {
            *ok = 0;
            return 0;
        }
        return v;
    }
    chr = parse_pp_char_literal(p, &v);
    if (chr < 0) {
        *ok = 0;
        return 0;
    }
    if (chr > 0) {
        return v;
    }
    if (is_ident_start((unsigned char)p->s[p->pos])) {
        while (is_ident_char((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
        return 0;
    }
    start = p->pos;
    if (p->s[p->pos] == '+' || p->s[p->pos] == '-') {
        p->pos++;
    }
    if (p->s[p->pos] == '0' && (p->s[p->pos + 1] == 'x' || p->s[p->pos + 1] == 'X')) {
        p->pos += 2;
        while (isxdigit((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
    } else {
        while (isdigit((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
    }
    while (p->s[p->pos] == 'u' || p->s[p->pos] == 'U' || p->s[p->pos] == 'l' || p->s[p->pos] == 'L') {
        p->pos++;
    }
    if (p->pos == start) {
        *ok = 0;
        return 0;
    }
    tmp = xstrdup_n(p->s + start, p->pos - start);
    if (tmp == NULL) {
        *ok = 0;
        return 0;
    }
    if (parse_int_literal(tmp, &v) != 0) {
        free(tmp);
        *ok = 0;
        return 0;
    }
    free(tmp);
    return v;
}

static long long parse_expr_unary(expr_parser_t *p, int *ok) {
    expr_skip_ws(p);
    if (expr_match(p, "!")) {
        return !parse_expr_unary(p, ok);
    }
    if (expr_match(p, "~")) {
        return ~parse_expr_unary(p, ok);
    }
    if (expr_match(p, "-")) {
        return -parse_expr_unary(p, ok);
    }
    if (expr_match(p, "+")) {
        return parse_expr_unary(p, ok);
    }
    return parse_expr_primary(p, ok);
}

static long long parse_expr_mul(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_unary(p, ok);
    while (*ok) {
        if (expr_match(p, "*")) {
            lhs *= parse_expr_unary(p, ok);
        } else if (expr_match(p, "/")) {
            long long rhs = parse_expr_unary(p, ok);
            if (rhs == 0) {
                if (!p->relaxed_eval) {
                    *ok = 0;
                    return 0;
                }
                lhs = 0;
                continue;
            }
            lhs /= rhs;
        } else if (expr_match(p, "%")) {
            long long rhs = parse_expr_unary(p, ok);
            if (rhs == 0) {
                if (!p->relaxed_eval) {
                    *ok = 0;
                    return 0;
                }
                lhs = 0;
                continue;
            }
            lhs %= rhs;
        } else {
            break;
        }
    }
    return lhs;
}

static long long parse_expr_add(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_mul(p, ok);
    while (*ok) {
        if (expr_match(p, "+")) {
            lhs += parse_expr_mul(p, ok);
        } else if (expr_match(p, "-")) {
            lhs -= parse_expr_mul(p, ok);
        } else {
            break;
        }
    }
    return lhs;
}

static long long parse_expr_shift(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_add(p, ok);
    while (*ok) {
        if (expr_match(p, "<<")) {
            lhs <<= parse_expr_add(p, ok);
        } else if (expr_match(p, ">>")) {
            lhs >>= parse_expr_add(p, ok);
        } else {
            break;
        }
    }
    return lhs;
}

static long long parse_expr_rel(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_shift(p, ok);
    while (*ok) {
        if (expr_match(p, "<=")) {
            lhs = lhs <= parse_expr_shift(p, ok);
        } else if (expr_match(p, ">=")) {
            lhs = lhs >= parse_expr_shift(p, ok);
        } else if (expr_match(p, "<")) {
            lhs = lhs < parse_expr_shift(p, ok);
        } else if (expr_match(p, ">")) {
            lhs = lhs > parse_expr_shift(p, ok);
        } else {
            break;
        }
    }
    return lhs;
}

static long long parse_expr_eq(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_rel(p, ok);
    while (*ok) {
        if (expr_match(p, "==")) {
            lhs = lhs == parse_expr_rel(p, ok);
        } else if (expr_match(p, "!=")) {
            lhs = lhs != parse_expr_rel(p, ok);
        } else {
            break;
        }
    }
    return lhs;
}

static long long parse_expr_band(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_eq(p, ok);
    while (*ok && expr_match(p, "&")) {
        lhs &= parse_expr_eq(p, ok);
    }
    return lhs;
}

static long long parse_expr_bxor(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_band(p, ok);
    while (*ok && expr_match(p, "^")) {
        lhs ^= parse_expr_band(p, ok);
    }
    return lhs;
}

static long long parse_expr_bor(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_bxor(p, ok);
    while (*ok && expr_match(p, "|")) {
        lhs |= parse_expr_bxor(p, ok);
    }
    return lhs;
}

static long long parse_expr_land(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_bor(p, ok);
    while (*ok && expr_match(p, "&&")) {
        long long rhs;
        int prev_relaxed = p->relaxed_eval;
        if (lhs == 0) {
            p->relaxed_eval = 1;
        }
        rhs = parse_expr_bor(p, ok);
        p->relaxed_eval = prev_relaxed;
        lhs = (lhs != 0 && rhs != 0) ? 1 : 0;
    }
    return lhs;
}

static long long parse_expr_or(expr_parser_t *p, int *ok) {
    long long lhs = parse_expr_land(p, ok);
    while (*ok && expr_match(p, "||")) {
        long long rhs;
        int prev_relaxed = p->relaxed_eval;
        if (lhs != 0) {
            p->relaxed_eval = 1;
        }
        rhs = parse_expr_land(p, ok);
        p->relaxed_eval = prev_relaxed;
        lhs = (lhs != 0 || rhs != 0) ? 1 : 0;
    }
    return lhs;
}

static long long parse_expr_cond(expr_parser_t *p, int *ok) {
    long long cond = parse_expr_or(p, ok);
    if (!*ok) {
        return 0;
    }
    if (expr_match(p, "?")) {
        long long lhs;
        long long rhs;
        int prev_relaxed = p->relaxed_eval;
        if (cond == 0) {
            p->relaxed_eval = 1;
        }
        lhs = parse_expr_cond(p, ok);
        p->relaxed_eval = prev_relaxed;
        if (!*ok) {
            return 0;
        }
        if (!expr_match(p, ":")) {
            *ok = 0;
            return 0;
        }
        if (cond != 0) {
            p->relaxed_eval = 1;
        }
        rhs = parse_expr_cond(p, ok);
        p->relaxed_eval = prev_relaxed;
        if (!*ok) {
            return 0;
        }
        return cond != 0 ? lhs : rhs;
    }
    return cond;
}

static char *expand_text(pp_state_t *st, const char *src, const char *file, int line, int depth,
                         pp_strvec_t *disabled, cc_diag_t *diag);

static char *stringify_macro_arg(const char *arg) {
    sb_t out;
    size_t i = 0;
    memset(&out, 0, sizeof(out));
    if (sb_append_c(&out, '"') != 0) {
        return NULL;
    }
    while (arg != NULL && arg[i] != '\0') {
        unsigned char c = (unsigned char)arg[i];
        if (c == '\\' || c == '"') {
            if (sb_append_c(&out, '\\') != 0 || sb_append_c(&out, (char)c) != 0) {
                sb_free(&out);
                return NULL;
            }
        } else if (c == '\n') {
            if (sb_append(&out, "\\n") != 0) {
                sb_free(&out);
                return NULL;
            }
        } else if (c == '\r') {
            if (sb_append(&out, "\\r") != 0) {
                sb_free(&out);
                return NULL;
            }
        } else if (c == '\t') {
            if (sb_append(&out, "\\t") != 0) {
                sb_free(&out);
                return NULL;
            }
        } else {
            if (sb_append_c(&out, (char)c) != 0) {
                sb_free(&out);
                return NULL;
            }
        }
        i++;
    }
    if (sb_append_c(&out, '"') != 0) {
        sb_free(&out);
        return NULL;
    }
    if (out.buf == NULL) {
        return xstrdup("\"\"");
    }
    return out.buf;
}

static char *build_va_args_value(const pp_macro_t *m, char **args, size_t arg_count) {
    sb_t out;
    size_t i;
    memset(&out, 0, sizeof(out));
    if (!m->is_variadic || arg_count <= m->param_count) {
        return xstrdup("");
    }
    for (i = m->param_count; i < arg_count; ++i) {
        if (i > m->param_count) {
            if (sb_append(&out, ", ") != 0) {
                sb_free(&out);
                return NULL;
            }
        }
        if (sb_append(&out, args[i] != NULL ? args[i] : "") != 0) {
            sb_free(&out);
            return NULL;
        }
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static int parse_va_opt_body(const char *body, size_t start, size_t *end_out, char **text_out) {
    size_t i = start;
    int level = 1;
    sb_t out;
    memset(&out, 0, sizeof(out));
    while (body[i] != '\0') {
        if (body[i] == '"' || body[i] == '\'') {
            char q = body[i];
            if (sb_append_c(&out, body[i]) != 0) {
                sb_free(&out);
                return -1;
            }
            i++;
            while (body[i] != '\0') {
                if (sb_append_c(&out, body[i]) != 0) {
                    sb_free(&out);
                    return -1;
                }
                if (body[i] == '\\' && body[i + 1] != '\0') {
                    i++;
                    if (sb_append_c(&out, body[i]) != 0) {
                        sb_free(&out);
                        return -1;
                    }
                } else if (body[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (body[i] == '(') {
            level++;
            if (sb_append_c(&out, body[i]) != 0) {
                sb_free(&out);
                return -1;
            }
            i++;
            continue;
        }
        if (body[i] == ')') {
            level--;
            if (level == 0) {
                *end_out = i + 1;
                *text_out = out.buf != NULL ? out.buf : xstrdup("");
                if (*text_out == NULL) {
                    sb_free(&out);
                    return -1;
                }
                return 0;
            }
            if (sb_append_c(&out, body[i]) != 0) {
                sb_free(&out);
                return -1;
            }
            i++;
            continue;
        }
        if (sb_append_c(&out, body[i]) != 0) {
            sb_free(&out);
            return -1;
        }
        i++;
    }
    sb_free(&out);
    return -1;
}

static char *replace_va_args_in_text(const char *text, const char *va_args) {
    sb_t out;
    size_t i = 0;
    const size_t key_len = strlen("__VA_ARGS__");
    memset(&out, 0, sizeof(out));
    while (text[i] != '\0') {
        if (strncmp(text + i, "__VA_ARGS__", key_len) == 0 &&
            (i == 0 || !is_ident_char((unsigned char)text[i - 1])) &&
            !is_ident_char((unsigned char)text[i + key_len])) {
            if (sb_append(&out, va_args != NULL ? va_args : "") != 0) {
                sb_free(&out);
                return NULL;
            }
            i += key_len;
            continue;
        }
        if (sb_append_c(&out, text[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static int token_matches(const char *s, size_t n, const char *tok) {
    return strlen(tok) == n && strncmp(s, tok, n) == 0;
}

static int is_va_args_token(const char *s, size_t n) {
    return token_matches(s, n, "__VA_ARGS__");
}

static int macro_body_uses_hash_ops(const char *body) {
    size_t i = 0;
    if (body == NULL) {
        return 0;
    }
    while (body[i] != '\0') {
        if (body[i] == '"' || body[i] == '\'') {
            char q = body[i++];
            while (body[i] != '\0') {
                if (body[i] == '\\' && body[i + 1] != '\0') {
                    i += 2;
                    continue;
                }
                if (body[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (body[i] == '#') {
            return 1;
        }
        i++;
    }
    return 0;
}

static char *replace_ident_token(const char *text, const char *from, const char *to) {
    sb_t out;
    size_t i = 0;
    size_t from_len = strlen(from);
    memset(&out, 0, sizeof(out));
    while (text[i] != '\0') {
        if ((i == 0 || !is_ident_char((unsigned char)text[i - 1])) &&
            strncmp(text + i, from, from_len) == 0 && !is_ident_char((unsigned char)text[i + from_len])) {
            if (sb_append(&out, to) != 0) {
                sb_free(&out);
                return NULL;
            }
            i += from_len;
            continue;
        }
        if (sb_append_c(&out, text[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    return out.buf != NULL ? out.buf : xstrdup("");
}

static char *replace_params(const pp_macro_t *m, char **args, size_t arg_count, int allow_va_opt, int allow_gnu_ext) {
    sb_t out;
    sb_t pasted;
    size_t i = 0;
    char *va_args = NULL;
    int has_va_args = 0;
    memset(&out, 0, sizeof(out));
    memset(&pasted, 0, sizeof(pasted));
    va_args = build_va_args_value(m, args, arg_count);
    if (va_args == NULL) {
        return NULL;
    }
    has_va_args = va_args[0] != '\0';
    while (m->body[i] != '\0') {
        if (m->is_variadic && !has_va_args && allow_gnu_ext && m->body[i] == ',') {
            size_t k = i + 1;
            while (m->body[k] == ' ' || m->body[k] == '\t') {
                k++;
            }
            if (m->body[k] == '#' && m->body[k + 1] == '#') {
                size_t ident_a;
                size_t ident_b;
                k += 2;
                while (m->body[k] == ' ' || m->body[k] == '\t') {
                    k++;
                }
                ident_a = k;
                if (is_ident_start((unsigned char)m->body[k])) {
                    k++;
                    while (is_ident_char((unsigned char)m->body[k])) {
                        k++;
                    }
                    ident_b = k;
                    if (is_va_args_token(m->body + ident_a, ident_b - ident_a)) {
                        i = ident_b;
                        continue;
                    }
                }
            }
        }
        if (m->is_variadic && strncmp(m->body + i, "__VA_OPT__", 10) == 0) {
            if (!allow_va_opt) {
                free(va_args);
                sb_free(&out);
                sb_free(&pasted);
                return NULL;
            }
            size_t k = i + 10;
            size_t end_pos = 0;
            char *va_opt_text = NULL;
            while (m->body[k] == ' ' || m->body[k] == '\t') {
                k++;
            }
            if (m->body[k] != '(' || parse_va_opt_body(m->body, k + 1, &end_pos, &va_opt_text) != 0) {
                free(va_args);
                sb_free(&out);
                sb_free(&pasted);
                return NULL;
            }
            if (has_va_args) {
                char *va_opt_subst = replace_va_args_in_text(va_opt_text != NULL ? va_opt_text : "", va_args);
                if (va_opt_subst == NULL || sb_append(&out, va_opt_subst) != 0) {
                    free(va_opt_subst);
                    free(va_opt_text);
                    free(va_args);
                    sb_free(&out);
                    sb_free(&pasted);
                    return NULL;
                }
                free(va_opt_subst);
            }
            free(va_opt_text);
            i = end_pos;
            continue;
        }
        if (m->body[i] == '"' || m->body[i] == '\'') {
            char q = m->body[i];
            if (sb_append_c(&out, q) != 0) {
                free(va_args);
                sb_free(&out);
                return NULL;
            }
            i++;
            while (m->body[i] != '\0') {
                if (sb_append_c(&out, m->body[i]) != 0) {
                    free(va_args);
                    sb_free(&out);
                    return NULL;
                }
                if (m->body[i] == '\\' && m->body[i + 1] != '\0') {
                    i++;
                    if (sb_append_c(&out, m->body[i]) != 0) {
                        free(va_args);
                        sb_free(&out);
                        return NULL;
                    }
                } else if (m->body[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (m->body[i] == '#' && m->body[i + 1] != '#' && (i == 0 || m->body[i - 1] != '#')) {
            size_t ident_start = i + 1;
            size_t ident_end;
            size_t pidx;
            while (m->body[ident_start] == ' ' || m->body[ident_start] == '\t') {
                ident_start++;
            }
            if (is_ident_start((unsigned char)m->body[ident_start])) {
                for (ident_end = ident_start + 1; is_ident_char((unsigned char)m->body[ident_end]); ++ident_end) {
                }
                for (pidx = 0; pidx < m->param_count; ++pidx) {
                    if (strlen(m->params[pidx]) == (ident_end - ident_start) &&
                        strncmp(m->body + ident_start, m->params[pidx], ident_end - ident_start) == 0) {
                        char *quoted = stringify_macro_arg(pidx < arg_count && args[pidx] != NULL ? args[pidx] : "");
                        if (quoted == NULL || sb_append(&out, quoted) != 0) {
                            free(quoted);
                            free(va_args);
                            sb_free(&out);
                            return NULL;
                        }
                        free(quoted);
                        i = ident_end;
                        goto next_iter;
                    }
                }
                if (m->is_variadic && (ident_end - ident_start) == strlen("__VA_ARGS__") &&
                    is_va_args_token(m->body + ident_start, ident_end - ident_start)) {
                    char *quoted = stringify_macro_arg(va_args);
                    if (quoted == NULL || sb_append(&out, quoted) != 0) {
                        free(quoted);
                        free(va_args);
                        sb_free(&out);
                        return NULL;
                    }
                    free(quoted);
                    i = ident_end;
                    goto next_iter;
                }
            }
        }
        if (is_ident_start((unsigned char)m->body[i])) {
            size_t j = i + 1;
            size_t k;
            while (is_ident_char((unsigned char)m->body[j])) {
                j++;
            }
            for (k = 0; k < m->param_count; ++k) {
                if (strlen(m->params[k]) == (j - i) && strncmp(m->body + i, m->params[k], j - i) == 0) {
                    if (k < arg_count) {
                        const char *aval = args[k] != NULL ? args[k] : "";
                        if (aval[0] == '\0') {
                            if (sb_append_c(&out, PP_EMPTY_ARG_MARKER) != 0) {
                                free(va_args);
                                sb_free(&out);
                                return NULL;
                            }
                        } else if (sb_append(&out, aval) != 0) {
                            free(va_args);
                            sb_free(&out);
                            return NULL;
                        }
                    }
                    break;
                }
            }
            if (k == m->param_count && m->is_variadic && (j - i) == strlen("__VA_ARGS__") &&
                is_va_args_token(m->body + i, j - i)) {
                if (va_args[0] == '\0') {
                    if (sb_append_c(&out, PP_EMPTY_ARG_MARKER) != 0) {
                        free(va_args);
                        sb_free(&out);
                        return NULL;
                    }
                } else if (sb_append(&out, va_args) != 0) {
                    free(va_args);
                    sb_free(&out);
                    return NULL;
                }
            } else if (k == m->param_count) {
                if (sb_append_n(&out, m->body + i, j - i) != 0) {
                    free(va_args);
                    sb_free(&out);
                    return NULL;
                }
            }
            i = j;
            continue;
        }
        if (sb_append_c(&out, m->body[i]) != 0) {
            free(va_args);
            sb_free(&out);
            return NULL;
        }
        i++;
next_iter:
        ;
    }
    if (out.buf == NULL) {
        free(va_args);
        return xstrdup("");
    }

    i = 0;
    while (out.buf[i] != '\0') {
        if (out.buf[i] == PP_EMPTY_ARG_MARKER) {
            i++;
            continue;
        }
        if (out.buf[i] == '#' && out.buf[i + 1] == '#') {
            int left_empty = 0;
            int right_empty = 0;
            while (pasted.len > 0 && (pasted.buf[pasted.len - 1] == ' ' || pasted.buf[pasted.len - 1] == '\t')) {
                pasted.len--;
                pasted.buf[pasted.len] = '\0';
            }
            if (pasted.len > 0 && pasted.buf[pasted.len - 1] == PP_EMPTY_ARG_MARKER) {
                left_empty = 1;
                pasted.len--;
                pasted.buf[pasted.len] = '\0';
            }
            i += 2;
            while (out.buf[i] == ' ' || out.buf[i] == '\t') {
                i++;
            }
            if (out.buf[i] == PP_EMPTY_ARG_MARKER) {
                right_empty = 1;
                i++;
                while (out.buf[i] == ' ' || out.buf[i] == '\t') {
                    i++;
                }
            }
            if ((left_empty || right_empty) && pasted.len > 0 && out.buf[i] != '\0') {
                if (sb_append_c(&pasted, ' ') != 0) {
                    free(va_args);
                    sb_free(&out);
                    sb_free(&pasted);
                    return NULL;
                }
            }
            continue;
        }
        if (sb_append_c(&pasted, out.buf[i]) != 0) {
            free(va_args);
            sb_free(&out);
            sb_free(&pasted);
            return NULL;
        }
        i++;
    }
    free(va_args);
    sb_free(&out);
    if (pasted.buf == NULL) {
        return xstrdup("");
    }
    return pasted.buf;
}

static int parse_call_args(const char *src, size_t open_pos, size_t *end_pos, char ***out_args, size_t *out_count) {
    size_t i = open_pos + 1;
    int paren_level = 1;
    int brace_level = 0;
    int bracket_level = 0;
    sb_t cur;
    pp_strvec_t args;
    memset(&cur, 0, sizeof(cur));
    memset(&args, 0, sizeof(args));
    while (src[i] != '\0') {
        char c = src[i];
        if (c == '"' || c == '\'') {
            char q = c;
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            while (src[i] != '\0') {
                if (sb_append_c(&cur, src[i]) != 0) {
                    goto fail;
                }
                if (src[i] == '\\' && src[i + 1] != '\0') {
                    i++;
                    if (sb_append_c(&cur, src[i]) != 0) {
                        goto fail;
                    }
                } else if (src[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (c == '(') {
            paren_level++;
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == '{') {
            brace_level++;
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == '}') {
            if (brace_level > 0) {
                brace_level--;
            }
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == '[') {
            bracket_level++;
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == ']') {
            if (bracket_level > 0) {
                bracket_level--;
            }
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == ')') {
            paren_level--;
            if (paren_level < 0) {
                goto fail;
            }
            if (paren_level == 0 && brace_level == 0 && bracket_level == 0) {
                char *arg = trim_dup(cur.buf != NULL ? cur.buf : "");
                if (arg == NULL) {
                    goto fail;
                }
                if (strvec_push(&args, arg) != 0) {
                    free(arg);
                    goto fail;
                }
                free(arg);
                *end_pos = i + 1;
                *out_args = args.items;
                *out_count = args.count;
                sb_free(&cur);
                return 0;
            }
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == ',' && paren_level == 1 && brace_level == 0 && bracket_level == 0) {
            char *arg = trim_dup(cur.buf != NULL ? cur.buf : "");
            if (arg == NULL) {
                goto fail;
            }
            if (strvec_push(&args, arg) != 0) {
                free(arg);
                goto fail;
            }
            free(arg);
            cur.len = 0;
            if (cur.buf != NULL) {
                cur.buf[0] = '\0';
            }
            i++;
            continue;
        }
        if (sb_append_c(&cur, c) != 0) {
            goto fail;
        }
        i++;
    }
fail:
    sb_free(&cur);
    strvec_free(&args);
    return -1;
}

static void emit_macro_trace(const char *file, int line, const char *name) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    fprintf(stderr, "%s:%d:1: note: while expanding macro '%s'\n", file, line, name);
}

static char *expand_once(pp_state_t *st, const char *src, const char *file, int line, int depth,
                         pp_strvec_t *disabled, cc_diag_t *diag) {
    size_t i = 0;
    sb_t out;
    memset(&out, 0, sizeof(out));
    while (src[i] != '\0') {
        if (src[i] == '"' || src[i] == '\'') {
            char q = src[i];
            if (sb_append_c(&out, src[i]) != 0) {
                sb_free(&out);
                return NULL;
            }
            i++;
            while (src[i] != '\0') {
                if (sb_append_c(&out, src[i]) != 0) {
                    sb_free(&out);
                    return NULL;
                }
                if (src[i] == '\\' && src[i + 1] != '\0') {
                    i++;
                    if (sb_append_c(&out, src[i]) != 0) {
                        sb_free(&out);
                        return NULL;
                    }
                } else if (src[i] == q) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        if (is_ident_start((unsigned char)src[i])) {
            size_t j = i + 1;
            char *name;
            while (is_ident_char((unsigned char)src[j])) {
                j++;
            }
            name = xstrdup_n(src + i, j - i);
            if (name == NULL) {
                sb_free(&out);
                return NULL;
            }
            if (strcmp(name, "__LINE__") == 0) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%d", line);
                if (sb_append(&out, tmp) != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__FILE__") == 0) {
                sb_t lit;
                memset(&lit, 0, sizeof(lit));
                if (sb_append_c(&lit, '"') != 0 || sb_append(&lit, file) != 0 || sb_append_c(&lit, '"') != 0 ||
                    sb_append(&out, lit.buf != NULL ? lit.buf : "\"\"") != 0) {
                    sb_free(&lit);
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                sb_free(&lit);
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__FILE_NAME__") == 0) {
                sb_t lit;
                memset(&lit, 0, sizeof(lit));
                if (sb_append_c(&lit, '"') != 0 || sb_append(&lit, path_basename(file)) != 0 ||
                    sb_append_c(&lit, '"') != 0 || sb_append(&out, lit.buf != NULL ? lit.buf : "\"\"") != 0) {
                    sb_free(&lit);
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                sb_free(&lit);
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__BASE_FILE__") == 0) {
                sb_t lit;
                memset(&lit, 0, sizeof(lit));
                if (sb_append_c(&lit, '"') != 0 || sb_append(&lit, st->base_file != NULL ? st->base_file : file) != 0 ||
                    sb_append_c(&lit, '"') != 0 || sb_append(&out, lit.buf != NULL ? lit.buf : "\"\"") != 0) {
                    sb_free(&lit);
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                sb_free(&lit);
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__INCLUDE_LEVEL__") == 0) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%d", st->include_level);
                if (sb_append(&out, tmp) != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__COUNTER__") == 0) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%lu", st->counter_value++);
                if (sb_append(&out, tmp) != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__DATE__") == 0) {
                if (sb_append(&out, "\"Jan 01 1970\"") != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__TIME__") == 0) {
                if (sb_append(&out, "\"00:00:00\"") != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            if (strcmp(name, "__TIMESTAMP__") == 0) {
                if (sb_append(&out, "\"Thu Jan  1 00:00:00 1970\"") != 0) {
                    free(name);
                    sb_free(&out);
                    return NULL;
                }
                free(name);
                i = j;
                continue;
            }
            {
                pp_macro_t *m = macro_find(&st->macros, name);
                if (m == NULL) {
                    if (sb_append_n(&out, src + i, j - i) != 0) {
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    free(name);
                    i = j;
                    continue;
                }
                if (disabled != NULL && strvec_contains(disabled, name)) {
                    if (sb_append_n(&out, src + i, j - i) != 0) {
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    free(name);
                    i = j;
                    continue;
                }
                if (!m->is_function) {
                    char *exp_body;
                    char *masked_body;
                    size_t k = j;
                    if (disabled != NULL && strvec_push(disabled, name) != 0) {
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    exp_body = expand_text(st, m->body, file, line, depth + 1, disabled, diag);
                    if (disabled != NULL) {
                        strvec_pop_free(disabled);
                    }
                    if (exp_body == NULL) {
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    masked_body = mask_hidden_macro_name_tokens(exp_body, name);
                    free(exp_body);
                    if (masked_body == NULL) {
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    exp_body = masked_body;
                    while (src[k] == ' ' || src[k] == '\t' || src[k] == '\r' || src[k] == '\n') {
                        k++;
                    }
                    if (src[k] == '(') {
                        const char *alias_start = skip_ws(exp_body);
                        const char *alias_end = alias_start;
                        while (is_ident_char((unsigned char)*alias_end)) {
                            alias_end++;
                        }
                        if (alias_end > alias_start && skip_ws(alias_end)[0] == '\0') {
                            char *alias_name = xstrdup_n(alias_start, (size_t)(alias_end - alias_start));
                            if (alias_name == NULL) {
                                free(exp_body);
                                free(name);
                                sb_free(&out);
                                return NULL;
                            }
                            {
                                pp_macro_t *alias_m = macro_find(&st->macros, alias_name);
                                if (alias_m != NULL && alias_m->is_function) {
                                    size_t call_end = 0;
                                    char **alias_args = NULL;
                                    size_t alias_arg_count = 0;
                                    size_t ai;
                                    if (parse_call_args(src, k, &call_end, &alias_args, &alias_arg_count) != 0) {
                                        set_diag(diag, (size_t)line, k + 1, "malformed function-like macro invocation");
                                        emit_macro_trace(file, line, alias_name);
                                        free(alias_name);
                                        free(exp_body);
                                        free(name);
                                        sb_free(&out);
                                        return NULL;
                                    }
                                    for (ai = 0; ai < alias_arg_count; ++ai) {
                                        free(alias_args[ai]);
                                    }
                                    free(alias_args);
                                    if (sb_append(&out, exp_body) != 0 || sb_append_n(&out, src + j, call_end - j) != 0) {
                                        free(alias_name);
                                        free(exp_body);
                                        free(name);
                                        sb_free(&out);
                                        return NULL;
                                    }
                                    free(alias_name);
                                    free(exp_body);
                                    free(name);
                                    i = call_end;
                                    continue;
                                }
                            }
                            free(alias_name);
                        }
                    }
                    if (sb_append(&out, exp_body) != 0) {
                        free(exp_body);
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    free(exp_body);
                    free(name);
                    i = j;
                    continue;
                } else {
                    size_t k = j;
                    size_t call_end = 0;
                    char **args = NULL;
                    size_t arg_count = 0;
                    char **exp_args = NULL;
                    int disable_arg_prescan = 0;
                    size_t ai;
                    char *subst = NULL;
                    char *exp_subst = NULL;
                    char *masked_subst = NULL;
                    while (src[k] == ' ' || src[k] == '\t' || src[k] == '\r' || src[k] == '\n') {
                        k++;
                    }
                    if (src[k] != '(') {
                        if (sb_append_n(&out, src + i, j - i) != 0) {
                            free(name);
                            sb_free(&out);
                            return NULL;
                        }
                        free(name);
                        i = j;
                        continue;
                    }
                    if (parse_call_args(src, k, &call_end, &args, &arg_count) != 0) {
                        set_diag(diag, (size_t)line, k + 1, "malformed function-like macro invocation");
                        emit_macro_trace(file, line, name);
                        free(name);
                        sb_free(&out);
                        return NULL;
                    }
                    if (!m->is_variadic && m->param_count == 0 && arg_count == 1 && args != NULL && args[0] != NULL &&
                        args[0][0] == '\0') {
                        free(args[0]);
                        free(args);
                        args = NULL;
                        arg_count = 0;
                    }
                    if ((!m->is_variadic && arg_count != m->param_count) || (m->is_variadic && arg_count < m->param_count)) {
                        char msg[160];
                        if (m->is_variadic) {
                            snprintf(msg, sizeof(msg), "macro '%s' argument count mismatch (got %zu, need at least %zu)",
                                     name, arg_count, m->param_count);
                        } else {
                            snprintf(msg, sizeof(msg), "macro '%s' argument count mismatch (got %zu, expected %zu)", name,
                                     arg_count, m->param_count);
                        }
                        set_diag(diag, (size_t)line, k + 1, msg);
                        emit_macro_trace(file, line, name);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                        }
                        free(args);
                        sb_free(&out);
                        return NULL;
                    }
                    exp_args = (char **)calloc(arg_count > 0 ? arg_count : 1, sizeof(*exp_args));
                    if (exp_args == NULL) {
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                        }
                        free(args);
                        sb_free(&out);
                        return NULL;
                    }
                    disable_arg_prescan = macro_body_uses_hash_ops(m->body);
                    for (ai = 0; ai < arg_count; ++ai) {
                        if (disable_arg_prescan) {
                            exp_args[ai] = xstrdup(args[ai] != NULL ? args[ai] : "");
                        } else {
                            exp_args[ai] = expand_text(st, args[ai], file, line, depth + 1, disabled, diag);
                        }
                        if (exp_args[ai] == NULL) {
                            if (diag != NULL && diag->message[0] == '\0') {
                                char msg[160];
                                snprintf(msg, sizeof(msg), "failed to expand macro argument %zu for '%s'", ai + 1, name);
                                set_diag(diag, (size_t)line, k + 1, msg);
                            }
                            emit_macro_trace(file, line, name);
                            free(name);
                            for (; ai < arg_count; ++ai) {
                                free(args[ai]);
                            }
                            free(args);
                            for (ai = 0; ai < arg_count; ++ai) {
                                free(exp_args[ai]);
                            }
                            free(exp_args);
                            sb_free(&out);
                            return NULL;
                        }
                    }
                    if (!(st->std_is_c23 || st->std_is_gnu) && strstr(m->body, "__VA_OPT__") != NULL) {
                        set_diag(diag, (size_t)line, k + 1, "__VA_OPT__ requires c23/gnu mode");
                        emit_macro_trace(file, line, name);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    subst = replace_params(m, exp_args, arg_count, st->std_is_c23 || st->std_is_gnu, st->std_is_gnu);
                    if (subst == NULL) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            char msg[160];
                            snprintf(msg, sizeof(msg), "failed to substitute macro parameters for '%s'", name);
                            set_diag(diag, (size_t)line, k + 1, msg);
                        }
                        emit_macro_trace(file, line, name);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    if (disabled != NULL && strvec_push(disabled, name) != 0) {
                        emit_macro_trace(file, line, name);
                        free(subst);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    exp_subst = expand_text(st, subst, file, line, depth + 1, disabled, diag);
                    if (disabled != NULL) {
                        strvec_pop_free(disabled);
                    }
                    if (exp_subst == NULL) {
                        emit_macro_trace(file, line, name);
                        free(subst);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    masked_subst = mask_hidden_macro_name_tokens(exp_subst, name);
                    free(exp_subst);
                    if (masked_subst == NULL) {
                        free(subst);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    exp_subst = masked_subst;
                    if (sb_append(&out, exp_subst) != 0) {
                        free(exp_subst);
                        free(subst);
                        free(name);
                        for (ai = 0; ai < arg_count; ++ai) {
                            free(args[ai]);
                            free(exp_args[ai]);
                        }
                        free(args);
                        free(exp_args);
                        sb_free(&out);
                        return NULL;
                    }
                    free(exp_subst);
                    free(subst);
                    for (ai = 0; ai < arg_count; ++ai) {
                        free(args[ai]);
                        free(exp_args[ai]);
                    }
                    free(args);
                    free(exp_args);
                    free(name);
                    i = call_end;
                    continue;
                }
            }
        }
        if (sb_append_c(&out, src[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static int parse_pragma_operator(const char *src, size_t start, size_t *out_end) {
    size_t i = start;
    size_t j;
    int depth = 0;

    if (src == NULL || out_end == NULL) {
        return 0;
    }
    if (strncmp(src + i, "_Pragma", 7) != 0) {
        return 0;
    }
    if (i > 0 && is_ident_char((unsigned char)src[i - 1])) {
        return 0;
    }
    if (is_ident_char((unsigned char)src[i + 7])) {
        return 0;
    }
    j = i + 7;
    while (src[j] == ' ' || src[j] == '\t' || src[j] == '\r' || src[j] == '\n' || src[j] == '\f' || src[j] == '\v') {
        j++;
    }
    if (src[j] != '(') {
        return 0;
    }

    for (; src[j] != '\0'; ++j) {
        if (src[j] == '"' || src[j] == '\'') {
            char q = src[j];
            ++j;
            while (src[j] != '\0') {
                if (src[j] == '\\' && src[j + 1] != '\0') {
                    j += 2;
                    continue;
                }
                if (src[j] == q) {
                    break;
                }
                ++j;
            }
            if (src[j] == '\0') {
                return 0;
            }
            continue;
        }
        if (src[j] == '(') {
            depth++;
            continue;
        }
        if (src[j] == ')') {
            depth--;
            if (depth == 0) {
                *out_end = j + 1;
                return 1;
            }
        }
    }
    return 0;
}

static char *strip_pragma_operators(const char *src) {
    sb_t out;
    size_t i = 0;

    if (src == NULL) {
        return NULL;
    }
    memset(&out, 0, sizeof(out));
    while (src[i] != '\0') {
        size_t end = 0;
        if (parse_pragma_operator(src, i, &end)) {
            size_t next = end;
            int need_sep = 0;
            while (src[next] == ' ' || src[next] == '\t') {
                next++;
            }
            if (out.len > 0 && is_ident_char((unsigned char)out.buf[out.len - 1]) &&
                is_ident_char((unsigned char)src[next])) {
                need_sep = 1;
            }
            if (need_sep && sb_append_c(&out, ' ') != 0) {
                sb_free(&out);
                return NULL;
            }
            i = end;
            continue;
        }
        if (sb_append_c(&out, src[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static char *mask_hidden_macro_name_tokens(const char *src, const char *name) {
    sb_t out;
    size_t i = 0;
    size_t nlen;
    if (src == NULL || name == NULL || name[0] == '\0') {
        return xstrdup(src != NULL ? src : "");
    }
    nlen = strlen(name);
    memset(&out, 0, sizeof(out));
    while (src[i] != '\0') {
        if (is_ident_start((unsigned char)src[i])) {
            size_t j = i + 1;
            while (is_ident_char((unsigned char)src[j])) {
                j++;
            }
            if ((j - i) == nlen && strncmp(src + i, name, nlen) == 0) {
                size_t ni;
                if (sb_append_c(&out, (char)PP_HIDE_MACRO_BEGIN) != 0) {
                    sb_free(&out);
                    return NULL;
                }
                for (ni = 0; ni < nlen; ++ni) {
                    char oct[4];
                    unsigned v = (unsigned char)name[ni];
                    oct[0] = (char)('0' + ((v / 64) % 8));
                    oct[1] = (char)('0' + ((v / 8) % 8));
                    oct[2] = (char)('0' + (v % 8));
                    oct[3] = '\0';
                    if (sb_append_n(&out, oct, 3) != 0) {
                        sb_free(&out);
                        return NULL;
                    }
                }
                if (sb_append_c(&out, (char)PP_HIDE_MACRO_END) != 0) {
                    sb_free(&out);
                    return NULL;
                }
            } else if (sb_append_n(&out, src + i, j - i) != 0) {
                sb_free(&out);
                return NULL;
            }
            i = j;
            continue;
        }
        if (sb_append_c(&out, src[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static char *unmask_hidden_macro_tokens(const char *src) {
    sb_t out;
    size_t i = 0;
    if (src == NULL) {
        return NULL;
    }
    memset(&out, 0, sizeof(out));
    while (src[i] != '\0') {
        if ((unsigned char)src[i] == (unsigned char)PP_HIDE_MACRO_BEGIN) {
            i++;
            while (src[i] != '\0' && (unsigned char)src[i] != (unsigned char)PP_HIDE_MACRO_END) {
                unsigned d0, d1, d2, v;
                if (src[i] < '0' || src[i] > '7' || src[i + 1] < '0' || src[i + 1] > '7' || src[i + 2] < '0' ||
                    src[i + 2] > '7') {
                    i++;
                    continue;
                }
                d0 = (unsigned)(src[i] - '0');
                d1 = (unsigned)(src[i + 1] - '0');
                d2 = (unsigned)(src[i + 2] - '0');
                v = d0 * 64 + d1 * 8 + d2;
                if (sb_append_c(&out, (char)v) != 0) {
                    sb_free(&out);
                    return NULL;
                }
                i += 3;
            }
            if ((unsigned char)src[i] == (unsigned char)PP_HIDE_MACRO_END) {
                i++;
            }
            continue;
        }
        if ((unsigned char)src[i] == (unsigned char)PP_HIDE_MACRO_END) {
            i++;
            continue;
        }
        if (sb_append_c(&out, src[i]) != 0) {
            sb_free(&out);
            return NULL;
        }
        i++;
    }
    if (out.buf == NULL) {
        return xstrdup("");
    }
    return out.buf;
}

static char *expand_text(pp_state_t *st, const char *src, const char *file, int line, int depth,
                         pp_strvec_t *disabled, cc_diag_t *diag) {
    char *cur;
    int pass;
    pp_strvec_t local_disabled;
    int use_local_disabled = 0;

    if (depth > (int)st->max_expand_depth) {
        set_diag(diag, (size_t)line, 1, "macro expansion depth exceeded");
        return NULL;
    }
    if (disabled == NULL) {
        memset(&local_disabled, 0, sizeof(local_disabled));
        disabled = &local_disabled;
        use_local_disabled = 1;
    }
    cur = xstrdup(src);
    if (cur == NULL) {
        if (use_local_disabled) {
            strvec_free(disabled);
        }
        return NULL;
    }
    for (pass = 0; pass < (int)st->max_expand_passes; ++pass) {
        char *next = expand_once(st, cur, file, line, depth, disabled, diag);
        if (next == NULL) {
            free(cur);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return NULL;
        }
        if (strlen(next) > st->max_expanded_text) {
            set_diag(diag, (size_t)line, 1, "macro expansion output too large");
            free(cur);
            free(next);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return NULL;
        }
        if (count_pp_tokens(next) > st->max_expanded_tokens) {
            set_diag(diag, (size_t)line, 1, "macro expansion token growth limit exceeded");
            free(cur);
            free(next);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return NULL;
        }
        if (strcmp(next, cur) == 0) {
            char *normalized = strip_pragma_operators(next);
            free(cur);
            free(next);
            if (normalized == NULL) {
                set_diag(diag, (size_t)line, 1, "out of memory stripping _Pragma operators");
                return NULL;
            }
            if (use_local_disabled) {
                char *unmasked = unmask_hidden_macro_tokens(normalized);
                strvec_free(disabled);
                free(normalized);
                if (unmasked == NULL) {
                    set_diag(diag, (size_t)line, 1, "out of memory unmasking hidden macro tokens");
                    return NULL;
                }
                return unmasked;
            }
            return normalized;
        }
        free(cur);
        cur = next;
    }
    if (use_local_disabled) {
        strvec_free(disabled);
    }
    {
        char *normalized = strip_pragma_operators(cur);
        free(cur);
        if (normalized == NULL) {
            set_diag(diag, (size_t)line, 1, "out of memory stripping _Pragma operators");
            return NULL;
        }
        if (use_local_disabled) {
            char *unmasked = unmask_hidden_macro_tokens(normalized);
            free(normalized);
            if (unmasked == NULL) {
                set_diag(diag, (size_t)line, 1, "out of memory unmasking hidden macro tokens");
                return NULL;
            }
            return unmasked;
        }
        return normalized;
    }
}

static int eval_condition(pp_state_t *st, const char *expr, const char *file, int line, int *out_true, cc_diag_t *diag) {
    char *rewritten = NULL;
    char *expanded;
    expr_parser_t p;
    int ok = 1;
    long long v;
    {
        sb_t out;
        size_t i = 0;
        memset(&out, 0, sizeof(out));
        while (expr[i] != '\0') {
            if (is_ident_start((unsigned char)expr[i])) {
                size_t j = i + 1;
                while (is_ident_char((unsigned char)expr[j])) {
                    j++;
                }
                if ((j - i) == 7 && strncmp(expr + i, "defined", 7) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int is_def = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] == '(') {
                        k++;
                        while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                            k++;
                        }
                        if (!is_ident_start((unsigned char)expr[k])) {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "malformed defined() operand");
                            return -1;
                        }
                        name_a = expr + k;
                        k++;
                        while (is_ident_char((unsigned char)expr[k])) {
                            k++;
                        }
                        name_b = expr + k;
                        while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                            k++;
                        }
                        if (expr[k] != ')') {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "unterminated defined() expression");
                            return -1;
                        }
                        k++;
                    } else {
                        if (!is_ident_start((unsigned char)expr[k])) {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "malformed defined operand");
                            return -1;
                        }
                        name_a = expr + k;
                        k++;
                        while (is_ident_char((unsigned char)expr[k])) {
                            k++;
                        }
                        name_b = expr + k;
                    }
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    is_def = macro_find(&st->macros, name) != NULL;
                    free(name);
                    if (sb_append(&out, is_def ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if (((j - i) == strlen("__has_include") &&
                     strncmp(expr + i, "__has_include", strlen("__has_include")) == 0) ||
                    ((j - i) == strlen("__has_include_next") &&
                     strncmp(expr + i, "__has_include_next", strlen("__has_include_next")) == 0)) {
                    size_t k = j;
                    char inc_spec[PATH_MAX];
                    char inc_path[PATH_MAX];
                    size_t n = 0;
                    int quoted = 0;
                    int is_system = 0;
                    int enabled = st->std_is_c23 || st->std_is_gnu;
                    int use_next = ((j - i) == strlen("__has_include_next"));
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_include operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '"' && expr[k] != '<') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_include operand");
                        return -1;
                    }
                    quoted = expr[k] == '"';
                    {
                        char endc = quoted ? '"' : '>';
                        k++;
                        while (expr[k] != '\0' && expr[k] != endc && n + 1 < sizeof(inc_spec)) {
                            inc_spec[n++] = expr[k++];
                        }
                        inc_spec[n] = '\0';
                        if (expr[k] != endc) {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "unterminated __has_include operand");
                            return -1;
                        }
                        k++;
                    }
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_include expression");
                        return -1;
                    }
                    k++;
                    if (enabled &&
                        ((use_next && resolve_include_next(st, file, inc_spec, quoted, inc_path, &is_system) == 0) ||
                         (!use_next && resolve_include(st, file, inc_spec, quoted, inc_path, &is_system) == 0))) {
                        if (sb_append(&out, "1") != 0) {
                            sb_free(&out);
                            return -1;
                        }
                    } else {
                        if (sb_append(&out, "0") != 0) {
                            sb_free(&out);
                            return -1;
                        }
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_embed") &&
                    strncmp(expr + i, "__has_embed", strlen("__has_embed")) == 0) {
                    size_t k = j;
                    char inc_spec[PATH_MAX];
                    char inc_path[PATH_MAX];
                    size_t n = 0;
                    int quoted = 0;
                    int is_system = 0;
                    int found = 0;
                    int enabled = st->std_is_c23 || st->std_is_gnu;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_embed operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '"' && expr[k] != '<') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_embed operand");
                        return -1;
                    }
                    quoted = expr[k] == '"';
                    {
                        char endc = quoted ? '"' : '>';
                        k++;
                        while (expr[k] != '\0' && expr[k] != endc && n + 1 < sizeof(inc_spec)) {
                            inc_spec[n++] = expr[k++];
                        }
                        inc_spec[n] = '\0';
                        if (expr[k] != endc) {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "unterminated __has_embed operand");
                            return -1;
                        }
                        k++;
                    }
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_embed expression");
                        return -1;
                    }
                    k++;
                    if (enabled && resolve_include(st, file, inc_spec, quoted, inc_path, &is_system) == 0 &&
                        path_exists(inc_path)) {
                        found = 1;
                    }
                    if (sb_append(&out, found ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_c_attribute") &&
                    strncmp(expr + i, "__has_c_attribute", strlen("__has_c_attribute")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    char *base = NULL;
                    int has_attr = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_c_attribute operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_c_attribute operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k]) || expr[k] == ':') {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_c_attribute expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    base = strrchr(name, ':');
                    if (base != NULL && base[1] != '\0') {
                        base++;
                    } else {
                        base = name;
                    }
                    has_attr = has_c_attribute_name(base);
                    free(name);
                    if (sb_append(&out, has_attr ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_feature") && strncmp(expr + i, "__has_feature", strlen("__has_feature")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_feature operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_feature operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_feature expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    has_v = has_feature_name(name, st);
                    free(name);
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_extension") &&
                    strncmp(expr + i, "__has_extension", strlen("__has_extension")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_extension operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_extension operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_extension expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    has_v = has_feature_name(name, st) || (st->std_is_gnu && has_gnu_attribute_name(name));
                    free(name);
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_builtin") && strncmp(expr + i, "__has_builtin", strlen("__has_builtin")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_builtin operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_builtin operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_builtin expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    has_v = has_builtin_name(name);
                    free(name);
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_attribute") &&
                    strncmp(expr + i, "__has_attribute", strlen("__has_attribute")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_attribute operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_attribute operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_attribute expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    has_v = has_gnu_attribute_name(name);
                    free(name);
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_declspec_attribute") &&
                    strncmp(expr + i, "__has_declspec_attribute", strlen("__has_declspec_attribute")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_declspec_attribute operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_declspec_attribute operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_declspec_attribute expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    has_v = has_declspec_attribute_name(name);
                    free(name);
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__has_warning") && strncmp(expr + i, "__has_warning", strlen("__has_warning")) == 0) {
                    size_t k = j;
                    int has_v = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_warning operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] == '"') {
                        size_t b = ++k;
                        while (expr[k] != '\0' && expr[k] != '"') {
                            k++;
                        }
                        if (expr[k] != '"') {
                            sb_free(&out);
                            set_diag(diag, (size_t)line, k + 1, "unterminated __has_warning operand");
                            return -1;
                        }
                        has_v = (k > b);
                        k++;
                    } else if (is_ident_start((unsigned char)expr[k])) {
                        has_v = 1;
                        k++;
                        while (is_ident_char((unsigned char)expr[k])) {
                            k++;
                        }
                    } else {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __has_warning operand");
                        return -1;
                    }
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __has_warning expression");
                        return -1;
                    }
                    k++;
                    if (sb_append(&out, has_v ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
                if ((j - i) == strlen("__is_identifier") &&
                    strncmp(expr + i, "__is_identifier", strlen("__is_identifier")) == 0) {
                    size_t k = j;
                    const char *name_a = NULL;
                    const char *name_b = NULL;
                    char *name = NULL;
                    int is_ident = 0;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != '(') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __is_identifier operand");
                        return -1;
                    }
                    k++;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (!is_ident_start((unsigned char)expr[k])) {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "malformed __is_identifier operand");
                        return -1;
                    }
                    name_a = expr + k;
                    k++;
                    while (is_ident_char((unsigned char)expr[k])) {
                        k++;
                    }
                    name_b = expr + k;
                    while (expr[k] == ' ' || expr[k] == '\t' || expr[k] == '\r' || expr[k] == '\n') {
                        k++;
                    }
                    if (expr[k] != ')') {
                        sb_free(&out);
                        set_diag(diag, (size_t)line, k + 1, "unterminated __is_identifier expression");
                        return -1;
                    }
                    k++;
                    name = xstrdup_n(name_a, (size_t)(name_b - name_a));
                    if (name == NULL) {
                        sb_free(&out);
                        return -1;
                    }
                    is_ident = !is_reserved_identifier_name(name, st);
                    free(name);
                    if (sb_append(&out, is_ident ? "1" : "0") != 0) {
                        sb_free(&out);
                        return -1;
                    }
                    i = k;
                    continue;
                }
            }
            if (sb_append_c(&out, expr[i]) != 0) {
                sb_free(&out);
                return -1;
            }
            i++;
        }
        rewritten = out.buf != NULL ? out.buf : xstrdup("");
        if (rewritten == NULL) {
            sb_free(&out);
            return -1;
        }
    }
    expanded = expand_text(st, rewritten, file, line, 0, NULL, diag);
    free(rewritten);
    if (expanded == NULL) {
        return -1;
    }
    p.s = expanded;
    p.pos = 0;
    p.relaxed_eval = 0;
    v = parse_expr_cond(&p, &ok);
    expr_skip_ws(&p);
    if (!ok || p.s[p.pos] != '\0') {
        if (diag != NULL && diag->message[0] == '\0') {
            diag->line = (size_t)line;
            diag->col = p.pos + 1;
            snprintf(diag->message, sizeof(diag->message), "unsupported #if expression near: %.220s", expanded);
        }
        free(expanded);
        return -1;
    }
    *out_true = (v != 0);
    free(expanded);
    return 0;
}

static int parse_define_directive(pp_state_t *st, const char *rest) {
    const char *p = skip_ws(rest);
    const char *name_start;
    const char *name_end;
    int is_function = 0;
    int is_variadic = 0;
    char **params = NULL;
    size_t param_count = 0;
    char *name = NULL;
    char *body = NULL;
    char *vararg_name = NULL;
    size_t i;

    if (!is_ident_start((unsigned char)*p)) {
        return -1;
    }
    name_start = p;
    p++;
    while (is_ident_char((unsigned char)*p)) {
        p++;
    }
    name_end = p;
    if (*p == '(') {
        pp_strvec_t parsed_params;
        memset(&parsed_params, 0, sizeof(parsed_params));
        is_function = 1;
        p++;
        p = skip_ws(p);
        if (*p != ')') {
            for (;;) {
                const char *a = p;
                char *param = NULL;
                if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
                    is_variadic = 1;
                    p += 3;
                    p = skip_ws(p);
                    break;
                }
                if (!is_ident_start((unsigned char)*p)) {
                    strvec_free(&parsed_params);
                    return -1;
                }
                p++;
                while (is_ident_char((unsigned char)*p)) {
                    p++;
                }
                param = xstrdup_n(a, (size_t)(p - a));
                if (param == NULL) {
                    strvec_free(&parsed_params);
                    return -1;
                }
                if (strvec_push(&parsed_params, param) != 0) {
                    free(param);
                    strvec_free(&parsed_params);
                    return -1;
                }
                if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
                    is_variadic = 1;
                    vararg_name = xstrdup_n(a, (size_t)(p - a));
                    free(param);
                    if (vararg_name == NULL) {
                        strvec_free(&parsed_params);
                        return -1;
                    }
                    free(parsed_params.items[parsed_params.count - 1]);
                    parsed_params.count--;
                    p += 3;
                    p = skip_ws(p);
                    break;
                }
                free(param);
                p = skip_ws(p);
                if (*p == ')') {
                    break;
                }
                if (*p != ',') {
                    strvec_free(&parsed_params);
                    return -1;
                }
                p++;
                p = skip_ws(p);
            }
        }
        if (*p != ')') {
            strvec_free(&parsed_params);
            return -1;
        }
        p++;
        params = parsed_params.items;
        param_count = parsed_params.count;
    }
    p = skip_ws(p);
    name = xstrdup_n(name_start, (size_t)(name_end - name_start));
    body = xstrdup(p);
    if (name == NULL || body == NULL) {
        free(name);
        free(body);
        free(vararg_name);
        for (i = 0; i < param_count; ++i) {
            free(params[i]);
        }
        free(params);
        return -1;
    }
    if (is_variadic && vararg_name != NULL) {
        char *norm = replace_ident_token(body, vararg_name, "__VA_ARGS__");
        if (norm == NULL) {
            free(name);
            free(body);
            free(vararg_name);
            for (i = 0; i < param_count; ++i) {
                free(params[i]);
            }
            free(params);
            return -1;
        }
        free(body);
        body = norm;
    }
    if (macro_set(&st->macros, name, is_function, is_variadic, params, param_count, body) != 0) {
        free(name);
        free(body);
        free(vararg_name);
        for (i = 0; i < param_count; ++i) {
            free(params[i]);
        }
        free(params);
        return -1;
    }
    free(name);
    free(body);
    free(vararg_name);
    return 0;
}

static int current_active(pp_cond_frame_t *stack, size_t count) {
    if (count == 0) {
        return 1;
    }
    return stack[count - 1].this_active;
}

static int push_cond(pp_cond_frame_t **stack, size_t *count, size_t *cap, pp_cond_frame_t frame) {
    pp_cond_frame_t *next;
    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 16 : (*cap * 2);
        next = (pp_cond_frame_t *)realloc(*stack, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *stack = next;
        *cap = ncap;
    }
    (*stack)[(*count)++] = frame;
    return 0;
}

static void update_paren_depth_line(const char *s, int *depth) {
    size_t i = 0;
    char in_quote = '\0';
    int esc = 0;
    int d;

    if (s == NULL || depth == NULL) {
        return;
    }
    d = *depth;
    while (s[i] != '\0') {
        char c = s[i++];
        if (in_quote != '\0') {
            if (esc) {
                esc = 0;
                continue;
            }
            if (c == '\\') {
                esc = 1;
                continue;
            }
            if (c == in_quote) {
                in_quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quote = c;
            continue;
        }
        if (c == '(') {
            d++;
            continue;
        }
        if (c == ')' && d > 0) {
            d--;
        }
    }
    *depth = d;
}

static int pp_emit_text(pp_state_t *st, FILE *out, const char *s) {
    size_t n = strlen(s);
    if (st->suppress_output) {
        return 0;
    }
    if (st->output_bytes + n > st->max_output_size) {
        return -1;
    }
    if (fputs(s, out) < 0) {
        return -1;
    }
    st->output_bytes += n;
    return 0;
}

static int pp_emit_line_marker(pp_state_t *st, FILE *out, int line_no, const char *file_path) {
    char buf[PATH_MAX + 64];
    if (st->suppress_output || !st->emit_line_markers) {
        return 0;
    }
    if (snprintf(buf, sizeof(buf), "#line %d \"%s\"\n", line_no, file_path) >= (int)sizeof(buf)) {
        return -1;
    }
    return pp_emit_text(st, out, buf);
}

static int pp_emit_embed_file(pp_state_t *st, FILE *out, const char *path) {
    FILE *fp;
    int c;
    int first = 1;
    char tok[16];
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }
    while ((c = fgetc(fp)) != EOF) {
        if (!first && pp_emit_text(st, out, ", ") != 0) {
            fclose(fp);
            return -1;
        }
        snprintf(tok, sizeof(tok), "0x%02X", (unsigned)(c & 0xff));
        if (pp_emit_text(st, out, tok) != 0) {
            fclose(fp);
            return -1;
        }
        first = 0;
    }
    if (ferror(fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    if (first) {
        if (pp_emit_text(st, out, "0") != 0) {
            return -1;
        }
    }
    if (pp_emit_text(st, out, "\n") != 0) {
        return -1;
    }
    return 0;
}

static int preprocess_file(pp_state_t *st, const char *path, FILE *out, int depth, int macros_only, cc_diag_t *diag) {
    FILE *fp;
    sb_t line = {0};
    sb_t stripped = {0};
    int line_no = 1;
    int cur_line = 1;
    int had_line = 0;
    int in_block_comment = 0;
    int code_paren_depth = 0;
    int code_start_line = 0;
    pp_cond_frame_t *cond_stack = NULL;
    size_t cond_count = 0;
    size_t cond_cap = 0;
    int saw_pragma_once = 0;
    sb_t code_accum = {0};
    int old_include_level = st->include_level;
    const char *old_diag_file = g_pp_diag_file;

    g_pp_diag_file = path;

    if (depth > (int)st->max_include_depth) {
        set_diag(diag, 0, 0, "include depth exceeded");
        g_pp_diag_file = old_diag_file;
        return -1;
    }

    st->include_level = depth;

    if (once_contains(st, path)) {
        g_pp_diag_file = old_diag_file;
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        set_diag(diag, 0, 0, "failed to open include file");
        g_pp_diag_file = old_diag_file;
        return -1;
    }
    if (dep_add_path(st, path, 0, depth == 0) != 0) {
        goto fail;
    }
    if (pp_emit_line_marker(st, out, 1, path) != 0) {
        goto fail;
    }

    while (read_logical_line(fp, &line, &had_line, &line_no, &cur_line, st->enable_trigraphs) == 0 && had_line) {
        const char *raw = line.buf != NULL ? line.buf : "";
        const char *proc;
        const char *p = skip_ws(raw);
        int active = current_active(cond_stack, cond_count);
        if (strip_comments_line(raw, &in_block_comment, &stripped) != 0) {
            goto fail;
        }
        proc = stripped.buf != NULL ? stripped.buf : "";
        p = skip_ws(proc);
        if (*p == '#') {
            if (code_accum.len > 0 && code_paren_depth == 0) {
                char *expanded = expand_text(st, code_accum.buf != NULL ? code_accum.buf : "", path, code_start_line,
                                             0, NULL, diag);
                if (expanded == NULL) {
                    goto fail;
                }
                if (pp_emit_text(st, out, expanded) != 0) {
                    free(expanded);
                    goto fail;
                }
                free(expanded);
                code_accum.len = 0;
                if (code_accum.buf != NULL) {
                    code_accum.buf[0] = '\0';
                }
                code_paren_depth = 0;
                code_start_line = 0;
            }
            const char *kw;
            p++;
            p = skip_ws(p);
            kw = p;
            while (is_ident_char((unsigned char)*p)) {
                p++;
            }
            if ((strncmp(kw, "include", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 7) ||
                (strncmp(kw, "include_next", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 12)) {
                if (active) {
                    char inc_spec[PATH_MAX];
                    char inc_path[PATH_MAX];
                    int quoted = 0;
                    int is_system = 0;
                    int include_next = ((size_t)(p - kw) == 12);
                    size_t n = 0;
                    p = skip_ws(p);
                    if (*p == '"' || *p == '<') {
                        char endc = *p == '"' ? '"' : '>';
                        quoted = (*p == '"');
                        p++;
                        while (*p != '\0' && *p != endc && n + 1 < sizeof(inc_spec)) {
                            inc_spec[n++] = *p++;
                        }
                        inc_spec[n] = '\0';
                        if (*p != endc) {
                            set_diag(diag, (size_t)cur_line, 1, "malformed #include");
                            goto fail;
                        }
                        if (include_next) {
                            if (resolve_include_next(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                                set_diag(diag, (size_t)cur_line, 1, "include_next file not found");
                                goto fail;
                            }
                        } else if (resolve_include(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                            set_diag(diag, (size_t)cur_line, 1, "include file not found");
                            goto fail;
                        }
                        if (dep_add_path(st, inc_path, is_system, 0) != 0) {
                            goto fail;
                        }
                        if (preprocess_file(st, inc_path, out, depth + 1, macros_only, diag) != 0) {
                            fprintf(stderr, "cpp: note: in file included from %s:%d\n", path, cur_line);
                            goto fail;
                        }
                        if (pp_emit_line_marker(st, out, line_no, path) != 0) {
                            goto fail;
                        }
                    }
                }
                continue;
            }
            if (strncmp(kw, "embed", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (active) {
                    char inc_spec[PATH_MAX];
                    char inc_path[PATH_MAX];
                    int quoted = 0;
                    int is_system = 0;
                    size_t n = 0;
                    if (!(st->std_is_c23 || st->std_is_gnu)) {
                        set_diag(diag, (size_t)cur_line, 1, "#embed requires c23/gnu mode");
                        goto fail;
                    }
                    p = skip_ws(p);
                    if (*p != '"' && *p != '<') {
                        set_diag(diag, (size_t)cur_line, 1, "malformed #embed");
                        goto fail;
                    }
                    {
                        char endc = *p == '"' ? '"' : '>';
                        quoted = (*p == '"');
                        p++;
                        while (*p != '\0' && *p != endc && n + 1 < sizeof(inc_spec)) {
                            inc_spec[n++] = *p++;
                        }
                        inc_spec[n] = '\0';
                        if (*p != endc) {
                            set_diag(diag, (size_t)cur_line, 1, "malformed #embed");
                            goto fail;
                        }
                    }
                    if (resolve_include(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                        set_diag(diag, (size_t)cur_line, 1, "embed file not found");
                        goto fail;
                    }
                    if (dep_add_path(st, inc_path, is_system, 0) != 0) {
                        goto fail;
                    }
                    if (!macros_only && pp_emit_embed_file(st, out, inc_path) != 0) {
                        set_diag(diag, (size_t)cur_line, 1, "failed to emit #embed data");
                        goto fail;
                    }
                }
                continue;
            }
            if (strncmp(kw, "define", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 6) {
                if (active) {
                    if (parse_define_directive(st, p) != 0) {
                        set_diag(diag, (size_t)cur_line, 1, "malformed #define");
                        goto fail;
                    }
                }
                continue;
            }
            if (strncmp(kw, "undef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (active) {
                    p = skip_ws(p);
                    if (!is_ident_start((unsigned char)*p)) {
                        set_diag(diag, (size_t)cur_line, 1, "malformed #undef");
                        goto fail;
                    }
                    {
                        const char *a = p;
                        while (is_ident_char((unsigned char)*p)) {
                            p++;
                        }
                        {
                            char *name = xstrdup_n(a, (size_t)(p - a));
                            if (name == NULL) {
                                goto fail;
                            }
                            macro_unset(&st->macros, name);
                            free(name);
                        }
                    }
                }
                continue;
            }
            if (strncmp(kw, "ifdef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                pp_cond_frame_t fr;
                int cond = 0;
                p = skip_ws(p);
                if (is_ident_start((unsigned char)*p)) {
                    const char *a = p;
                    char *name;
                    while (is_ident_char((unsigned char)*p)) {
                        p++;
                    }
                    name = xstrdup_n(a, (size_t)(p - a));
                    if (name == NULL) {
                        goto fail;
                    }
                    cond = (macro_find(&st->macros, name) != NULL);
                    free(name);
                }
                fr.parent_active = active;
                fr.this_active = active && cond;
                fr.any_taken = fr.this_active;
                fr.saw_else = 0;
                if (push_cond(&cond_stack, &cond_count, &cond_cap, fr) != 0) {
                    goto fail;
                }
                continue;
            }
            if (strncmp(kw, "ifndef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 6) {
                pp_cond_frame_t fr;
                int cond = 1;
                p = skip_ws(p);
                if (is_ident_start((unsigned char)*p)) {
                    const char *a = p;
                    char *name;
                    while (is_ident_char((unsigned char)*p)) {
                        p++;
                    }
                    name = xstrdup_n(a, (size_t)(p - a));
                    if (name == NULL) {
                        goto fail;
                    }
                    cond = (macro_find(&st->macros, name) == NULL);
                    free(name);
                }
                fr.parent_active = active;
                fr.this_active = active && cond;
                fr.any_taken = fr.this_active;
                fr.saw_else = 0;
                if (push_cond(&cond_stack, &cond_count, &cond_cap, fr) != 0) {
                    goto fail;
                }
                continue;
            }
            if (strncmp(kw, "if", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 2) {
                pp_cond_frame_t fr;
                int cond = 0;
                if (active) {
                    if (eval_condition(st, p, path, cur_line, &cond, diag) != 0) {
                        goto fail;
                    }
                }
                fr.parent_active = active;
                fr.this_active = active && cond;
                fr.any_taken = fr.this_active;
                fr.saw_else = 0;
                if (push_cond(&cond_stack, &cond_count, &cond_cap, fr) != 0) {
                    goto fail;
                }
                continue;
            }
            if (strncmp(kw, "elifdef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 7) {
                int cond = 0;
                if (!(st->std_is_c23 || st->std_is_gnu)) {
                    set_diag(diag, (size_t)cur_line, 1, "#elifdef requires c23/gnu mode");
                    goto fail;
                }
                if (cond_count == 0) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elifdef");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elifdef after #else");
                    goto fail;
                }
                p = skip_ws(p);
                if (is_ident_start((unsigned char)*p)) {
                    const char *a = p;
                    char *name;
                    while (is_ident_char((unsigned char)*p)) {
                        p++;
                    }
                    name = xstrdup_n(a, (size_t)(p - a));
                    if (name == NULL) {
                        goto fail;
                    }
                    cond = (macro_find(&st->macros, name) != NULL);
                    free(name);
                }
                if (!cond_stack[cond_count - 1].parent_active || cond_stack[cond_count - 1].any_taken) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else {
                    cond_stack[cond_count - 1].this_active = cond;
                    if (cond) {
                        cond_stack[cond_count - 1].any_taken = 1;
                    }
                }
                continue;
            }
            if (strncmp(kw, "elifndef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 8) {
                int cond = 1;
                if (!(st->std_is_c23 || st->std_is_gnu)) {
                    set_diag(diag, (size_t)cur_line, 1, "#elifndef requires c23/gnu mode");
                    goto fail;
                }
                if (cond_count == 0) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elifndef");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elifndef after #else");
                    goto fail;
                }
                p = skip_ws(p);
                if (is_ident_start((unsigned char)*p)) {
                    const char *a = p;
                    char *name;
                    while (is_ident_char((unsigned char)*p)) {
                        p++;
                    }
                    name = xstrdup_n(a, (size_t)(p - a));
                    if (name == NULL) {
                        goto fail;
                    }
                    cond = (macro_find(&st->macros, name) == NULL);
                    free(name);
                }
                if (!cond_stack[cond_count - 1].parent_active || cond_stack[cond_count - 1].any_taken) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else {
                    cond_stack[cond_count - 1].this_active = cond;
                    if (cond) {
                        cond_stack[cond_count - 1].any_taken = 1;
                    }
                }
                continue;
            }
            if (strncmp(kw, "elif", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 4) {
                int cond = 0;
                if (cond_count == 0) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elif");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #elif after #else");
                    goto fail;
                }
                if (!cond_stack[cond_count - 1].parent_active) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else if (cond_stack[cond_count - 1].any_taken) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else {
                    if (eval_condition(st, p, path, cur_line, &cond, diag) != 0) {
                        goto fail;
                    }
                    cond_stack[cond_count - 1].this_active = cond;
                    if (cond) {
                        cond_stack[cond_count - 1].any_taken = 1;
                    }
                }
                continue;
            }
            if (strncmp(kw, "else", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 4) {
                if (cond_count == 0) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #else");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)cur_line, 1, "duplicate #else");
                    goto fail;
                }
                cond_stack[cond_count - 1].saw_else = 1;
                if (!cond_stack[cond_count - 1].parent_active || cond_stack[cond_count - 1].any_taken) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else {
                    cond_stack[cond_count - 1].this_active = 1;
                    cond_stack[cond_count - 1].any_taken = 1;
                }
                continue;
            }
            if (strncmp(kw, "endif", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (cond_count == 0) {
                    set_diag(diag, (size_t)cur_line, 1, "unexpected #endif");
                    goto fail;
                }
                cond_count--;
                continue;
            }
            if (strncmp(kw, "line", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 4) {
                if (active) {
                    long v = 0;
                    char *end = NULL;
                    char *line_expanded = NULL;
                    char line_file[PATH_MAX];
                    const char *line_target = path;
                    p = skip_ws(p);
                    line_expanded = expand_text(st, p, path, cur_line, 0, NULL, diag);
                    if (line_expanded == NULL) {
                        goto fail;
                    }
                    p = skip_ws(line_expanded);
                    if (*p == '\0') {
                        free(line_expanded);
                        set_diag(diag, (size_t)cur_line, 1, "malformed #line");
                        goto fail;
                    }
                    errno = 0;
                    v = strtol(p, &end, 10);
                    if (end == p || errno != 0 || v <= 0) {
                        free(line_expanded);
                        set_diag(diag, (size_t)cur_line, 1, "malformed #line");
                        goto fail;
                    }
                    p = skip_ws(end);
                    if (*p == '"') {
                        size_t n = 0;
                        p++;
                        while (*p != '\0' && *p != '"' && n + 1 < sizeof(line_file)) {
                            line_file[n++] = *p++;
                        }
                        line_file[n] = '\0';
                        if (*p != '"') {
                            free(line_expanded);
                            set_diag(diag, (size_t)cur_line, 1, "malformed #line filename");
                            goto fail;
                        }
                        line_target = line_file;
                    }
                    if (pp_emit_line_marker(st, out, (int)v, line_target) != 0) {
                        free(line_expanded);
                        goto fail;
                    }
                    line_no = (int)v;
                    free(line_expanded);
                }
                continue;
            }
            if (strncmp(kw, "pragma", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 6) {
                if (active) {
                    const char *r = skip_ws(p);
                    char macro_name[256];
                    int pm_rc;
                    int stdc_rc = validate_stdc_pragma(r, diag, (size_t)cur_line);
                    int clang_rc;
                    if (stdc_rc < 0) {
                        goto fail;
                    }
                    clang_rc = validate_clang_pragma(r, diag, (size_t)cur_line);
                    if (clang_rc < 0) {
                        goto fail;
                    }
                    pm_rc = parse_pragma_macro_name(r, "push_macro", macro_name, sizeof(macro_name));
                    if (pm_rc < 0) {
                        set_diag(diag, (size_t)cur_line, 1, "malformed #pragma push_macro");
                        goto fail;
                    }
                    if (pm_rc > 0) {
                        if (pragma_macro_push(st, macro_name) != 0) {
                            set_diag(diag, (size_t)cur_line, 1, "out of memory handling #pragma push_macro");
                            goto fail;
                        }
                        continue;
                    }
                    pm_rc = parse_pragma_macro_name(r, "pop_macro", macro_name, sizeof(macro_name));
                    if (pm_rc < 0) {
                        set_diag(diag, (size_t)cur_line, 1, "malformed #pragma pop_macro");
                        goto fail;
                    }
                    if (pm_rc > 0) {
                        if (pragma_macro_pop(st, macro_name) != 0) {
                            set_diag(diag, (size_t)cur_line, 1, "out of memory handling #pragma pop_macro");
                            goto fail;
                        }
                        continue;
                    }
                    if (strncmp(r, "once", 4) == 0) {
                        saw_pragma_once = 1;
                    }
                }
                continue;
            }
            if (strncmp(kw, "warning", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 7) {
                if (active) {
                    if (!(st->std_is_c23 || st->std_is_gnu)) {
                        set_diag(diag, (size_t)cur_line, 1, "#warning requires c23/gnu mode");
                        goto fail;
                    }
                    {
                        const char *msg = skip_ws(p);
                        fprintf(stderr, "%s:%d:1: warning: %s\n", path, cur_line, msg);
                    }
                }
                continue;
            }
            if (strncmp(kw, "error", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (active) {
                    set_diag(diag, (size_t)cur_line, 1, "preprocessor #error");
                    goto fail;
                }
                continue;
            }
            continue;
        }

        if (active) {
            if (macros_only) {
                continue;
            }
            if (code_accum.len == 0) {
                code_start_line = cur_line;
            }
            if (sb_append(&code_accum, proc) != 0 || sb_append_c(&code_accum, '\n') != 0) {
                goto fail;
            }
            update_paren_depth_line(proc, &code_paren_depth);
            if (code_paren_depth > 0) {
                continue;
            }
            {
                char *expanded = expand_text(st, code_accum.buf != NULL ? code_accum.buf : "", path, code_start_line,
                                             0, NULL, diag);
                if (expanded == NULL) {
                    goto fail;
                }
                if (pp_emit_text(st, out, expanded) != 0) {
                    free(expanded);
                    goto fail;
                }
                free(expanded);
                code_accum.len = 0;
                if (code_accum.buf != NULL) {
                    code_accum.buf[0] = '\0';
                }
                code_paren_depth = 0;
                code_start_line = 0;
            }
        }
    }

    if (code_accum.len > 0) {
        char *expanded;
        expanded = expand_text(st, code_accum.buf != NULL ? code_accum.buf : "", path, code_start_line, 0, NULL,
                               diag);
        if (expanded == NULL) {
            goto fail;
        }
        if (pp_emit_text(st, out, expanded) != 0) {
            free(expanded);
            goto fail;
        }
        free(expanded);
        code_accum.len = 0;
        if (code_accum.buf != NULL) {
            code_accum.buf[0] = '\0';
        }
    }

    if (cond_count != 0) {
        set_diag(diag, (size_t)line_no, 1, "unterminated conditional preprocessor block");
        goto fail;
    }

    if (saw_pragma_once && once_add(st, path) != 0) {
        goto fail;
    }

    st->include_level = old_include_level;
    g_pp_diag_file = old_diag_file;
    free(cond_stack);
    sb_free(&line);
    sb_free(&stripped);
    sb_free(&code_accum);
    fclose(fp);
    return 0;

fail:
    st->include_level = old_include_level;
    g_pp_diag_file = old_diag_file;
    free(cond_stack);
    sb_free(&line);
    sb_free(&stripped);
    sb_free(&code_accum);
    fclose(fp);
    return -1;
}

int cc_preprocess_file(const char *in_path, const char *out_path, const char *std_mode,
                       const char *const *flags, size_t flag_count, cc_diag_t *diag) {
    pp_state_t st;
    FILE *out = NULL;
    int rc = -1;
    size_t i;

    memset(&st, 0, sizeof(st));
    st.target_bits = 64;
    st.emit_line_markers = 1;
    st.base_file = in_path;
    st.include_level = 0;
    st.counter_value = 0;
    st.max_include_depth = PP_MAX_INCLUDE_DEPTH;
    st.max_expand_depth = PP_MAX_EXPAND_DEPTH;
    st.max_expand_passes = PP_MAX_EXPAND_PASSES;
    st.max_expanded_text = PP_MAX_EXPANDED_TEXT;
    st.max_output_size = PP_MAX_OUTPUT_SIZE;
    st.max_expanded_tokens = PP_MAX_EXPANDED_TOKENS;
    st.enable_trigraphs = std_mode_enable_trigraphs(std_mode);
    st.std_version = std_mode_version(std_mode, &st.std_is_c11, &st.std_is_c17, &st.std_is_c23, &st.std_is_gnu);
    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    scan_target_flags(&st, flags, flag_count);
    if (add_builtin_macros(&st) != 0) {
        set_diag(diag, 0, 0, "failed to initialize preprocessor builtins");
        goto out;
    }
    if (apply_flags(&st, flags, flag_count) != 0) {
        set_diag(diag, 0, 0, "invalid preprocessor options");
        goto out;
    }
    if (add_default_include_paths(&st) != 0) {
        set_diag(diag, 0, 0, "failed to initialize include paths");
        goto out;
    }
    if (st.show_include_paths) {
        dump_include_paths(&st);
    }
    out = fopen(out_path, "w");
    if (out == NULL) {
        set_diag(diag, 0, 0, "failed to open preprocess output");
        goto out;
    }
    for (i = 0; i < st.force_imacros.count; ++i) {
        char inc_path[PATH_MAX];
        int is_system = 0;
        if (resolve_forced_include(&st, in_path, st.force_imacros.items[i], inc_path, &is_system) != 0) {
            set_diag(diag, 0, 0, "forced -imacros file not found");
            goto out;
        }
        if (dep_add_path(&st, inc_path, is_system, 0) != 0) {
            goto out;
        }
        if (preprocess_file(&st, inc_path, out, 0, 1, diag) != 0) {
            goto out;
        }
    }
    for (i = 0; i < st.force_includes.count; ++i) {
        char inc_path[PATH_MAX];
        int is_system = 0;
        if (resolve_forced_include(&st, in_path, st.force_includes.items[i], inc_path, &is_system) != 0) {
            set_diag(diag, 0, 0, "forced -include file not found");
            goto out;
        }
        if (dep_add_path(&st, inc_path, is_system, 0) != 0) {
            goto out;
        }
        if (preprocess_file(&st, inc_path, out, 0, 0, diag) != 0) {
            goto out;
        }
    }
    if (preprocess_file(&st, in_path, out, 0, 0, diag) != 0) {
        goto out;
    }
    if (emit_macro_dump(&st, out) != 0) {
        set_diag(diag, 0, 0, "failed to emit macro dump");
        goto out;
    }
    if (emit_dependency_file(&st, in_path) != 0) {
        set_diag(diag, 0, 0, "failed to emit dependency file");
        goto out;
    }
    rc = 0;

out:
    if (out != NULL) {
        fclose(out);
    }
    macro_table_free(&st.macros);
    strvec_free(&st.quote_paths);
    strvec_free(&st.user_include_paths);
    strvec_free(&st.system_include_paths);
    strvec_free(&st.include_once);
    strvec_free(&st.force_includes);
    strvec_free(&st.force_imacros);
    strvec_free(&st.dep_paths);
    macro_stack_free(&st.pragma_macro_stack);
    free(st.dep_target);
    free(st.dep_file);
    return rc;
}

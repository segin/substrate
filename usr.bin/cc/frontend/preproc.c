#include "cc_frontend.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PP_MAX_INCLUDE_DEPTH 128
#define PP_MAX_EXPAND_DEPTH 32
#define PP_MAX_EXPAND_PASSES 16
#define PP_MAX_EXPANDED_TEXT (1024 * 1024)
#define PP_MAX_OUTPUT_SIZE (64 * 1024 * 1024)
#define PP_EMPTY_ARG_MARKER '\x1f'

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
} pp_macro_table_t;

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

static void set_diag(cc_diag_t *diag, size_t line, size_t col, const char *msg) {
    if (diag == NULL || diag->message[0] != '\0') {
        return;
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
    size_t i;
    for (i = 0; i < t->count; ++i) {
        if (strcmp(t->items[i].name, name) == 0) {
            return &t->items[i];
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
        strcmp(name, "__builtin_ctz") == 0 || strcmp(name, "__builtin_bswap16") == 0 ||
        strcmp(name, "__builtin_bswap32") == 0 || strcmp(name, "__builtin_bswap64") == 0 ||
        strcmp(name, "__builtin_add_overflow") == 0 || strcmp(name, "__builtin_sub_overflow") == 0 ||
        strcmp(name, "__builtin_mul_overflow") == 0 || strcmp(name, "__builtin_object_size") == 0 ||
        strcmp(name, "__builtin___memcpy_chk") == 0 || strcmp(name, "__builtin___memmove_chk") == 0 ||
        strcmp(name, "__builtin___memset_chk") == 0 || strcmp(name, "__builtin_va_start") == 0 ||
        strcmp(name, "__builtin_va_end") == 0 || strcmp(name, "__builtin_va_copy") == 0 ||
        strcmp(name, "__builtin_va_arg") == 0 || strcmp(name, "__builtin_trap") == 0 ||
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
    if (strcmp(name, "attribute") == 0 || strcmp(name, "loop") == 0 ||
        strcmp(name, "section") == 0 || strcmp(name, "fp") == 0) {
        return 1;
    }

    set_diag(diag, line_no, 1, "unsupported #pragma clang directive");
    return -1;
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
    char stdc_ver[32];
    if (macro_set(&st->macros, "__STDC__", 0, 0, NULL, 0, "1") != 0) {
        return -1;
    }
    snprintf(stdc_ver, sizeof(stdc_ver), "%dL", st->std_version);
    if (macro_set(&st->macros, "__STDC_VERSION__", 0, 0, NULL, 0, stdc_ver) != 0) {
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
    for (i = 0; i < flag_count; ++i) {
        const char *f = flags[i];
        if (strcmp(f, "-m32") == 0) {
            st->target_bits = 32;
        } else if (strcmp(f, "-m64") == 0) {
            st->target_bits = 64;
        }
    }
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
        char cmd[PATH_MAX + 64];
        char buf[PATH_MAX];
        FILE *fp;
        size_t n;
        if (strchr(host_cc, ' ') != NULL || strchr(host_cc, '\t') != NULL) {
            continue;
        }
        if (snprintf(cmd, sizeof(cmd), "%s -print-file-name=%s", host_cc, tool_dirs[ti]) >= (int)sizeof(cmd)) {
            continue;
        }
        fp = popen(cmd, "r");
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
                    pclose(fp);
                    return -1;
                }
            }
        }
        pclose(fp);
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
            if (n + 3 >= sizeof(dep_default)) {
                goto fail;
            }
            memcpy(dep_default, in_path, n);
            dep_default[n] = '\0';
            strcat(dep_default, ".d");
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

static int resolve_include(pp_state_t *st, const char *cur_file, const char *spec, int quoted, char out[PATH_MAX],
                           int *out_is_system) {
    size_t i;
    char cand[PATH_MAX];
    if (spec[0] == '/' && path_exists(spec)) {
        snprintf(out, PATH_MAX, "%s", spec);
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
            snprintf(out, PATH_MAX, "%s", cand);
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
                snprintf(out, PATH_MAX, "%s", cand);
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
            snprintf(out, PATH_MAX, "%s", cand);
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
            snprintf(out, PATH_MAX, "%s", cand);
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
    char *cur_dir = NULL;
    int have_cur = 0;

    if (cur_file != NULL) {
        cur_dir = dirname_dup(cur_file);
        if (cur_dir != NULL && cur_dir[0] != '\0') {
            have_cur = 1;
        }
    }

    if (quoted) {
        for (i = 0; i < st->quote_paths.count; ++i) {
            if (have_cur && strcmp(st->quote_paths.items[i], cur_dir) == 0) {
                continue;
            }
            if (snprintf(cand, sizeof(cand), "%s/%s", st->quote_paths.items[i], spec) >= (int)sizeof(cand)) {
                continue;
            }
            if (path_exists(cand)) {
                snprintf(out, PATH_MAX, "%s", cand);
                if (out_is_system != NULL) {
                    *out_is_system = 0;
                }
                free(cur_dir);
                return 0;
            }
        }
    }

    for (i = 0; i < st->user_include_paths.count; ++i) {
        if (have_cur && strcmp(st->user_include_paths.items[i], cur_dir) == 0) {
            continue;
        }
        if (snprintf(cand, sizeof(cand), "%s/%s", st->user_include_paths.items[i], spec) >= (int)sizeof(cand)) {
            continue;
        }
        if (path_exists(cand)) {
            snprintf(out, PATH_MAX, "%s", cand);
            if (out_is_system != NULL) {
                *out_is_system = 0;
            }
            free(cur_dir);
            return 0;
        }
    }

    for (i = 0; i < st->system_include_paths.count; ++i) {
        if (have_cur && strcmp(st->system_include_paths.items[i], cur_dir) == 0) {
            continue;
        }
        if (snprintf(cand, sizeof(cand), "%s/%s", st->system_include_paths.items[i], spec) >= (int)sizeof(cand)) {
            continue;
        }
        if (path_exists(cand)) {
            snprintf(out, PATH_MAX, "%s", cand);
            if (out_is_system != NULL) {
                *out_is_system = 1;
            }
            free(cur_dir);
            return 0;
        }
    }

    free(cur_dir);
    return -1;
}

static int resolve_forced_include(pp_state_t *st, const char *main_file, const char *spec, char out[PATH_MAX],
                                  int *out_is_system) {
    if (path_exists(spec)) {
        snprintf(out, PATH_MAX, "%s", spec);
        if (out_is_system != NULL) {
            *out_is_system = 0;
        }
        return 0;
    }
    return resolve_include(st, main_file, spec, 1, out, out_is_system);
}

static int once_contains(pp_state_t *st, const char *path) {
    size_t i;
    for (i = 0; i < st->include_once.count; ++i) {
        if (strcmp(st->include_once.items[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int once_add(pp_state_t *st, const char *path) {
    char real_buf[PATH_MAX];
    const char *norm = path;
    if (realpath(path, real_buf) != NULL) {
        norm = real_buf;
    }
    if (once_contains(st, norm)) {
        return 0;
    }
    return strvec_push(&st->include_once, norm);
}

static int read_logical_line(FILE *fp, sb_t *out, int *out_had_line, int *line_counter, int enable_trigraphs) {
    int c;
    int got_any = 0;
    int continue_line = 0;

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
    char *end;
    long long v;
    if (s == NULL || *s == '\0') {
        return -1;
    }
    errno = 0;
    v = strtoll(s, &end, 0);
    if (end == s || errno != 0) {
        return -1;
    }
    while (*end == 'u' || *end == 'U' || *end == 'l' || *end == 'L') {
        end++;
    }
    if (*end != '\0') {
        return -1;
    }
    *out = v;
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
    int level = 1;
    int saw_any_token = 0;
    int saw_comma = 0;
    sb_t cur;
    pp_strvec_t args;
    memset(&cur, 0, sizeof(cur));
    memset(&args, 0, sizeof(args));
    while (src[i] != '\0') {
        char c = src[i];
        if (c == '"' || c == '\'') {
            char q = c;
            saw_any_token = 1;
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
            level++;
            saw_any_token = 1;
            if (sb_append_c(&cur, c) != 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (c == ')') {
            level--;
            if (level == 0) {
                char *arg = trim_dup(cur.buf != NULL ? cur.buf : "");
                if (arg == NULL) {
                    goto fail;
                }
                if (!saw_any_token && !saw_comma && arg[0] == '\0') {
                    free(arg);
                    *end_pos = i + 1;
                    *out_args = args.items;
                    *out_count = args.count;
                    sb_free(&cur);
                    return 0;
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
        if (c == ',' && level == 1) {
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
            saw_comma = 1;
            saw_any_token = 0;
            i++;
            continue;
        }
        if (sb_append_c(&cur, c) != 0) {
            goto fail;
        }
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            saw_any_token = 1;
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
                    size_t ai;
                    char *subst = NULL;
                    char *exp_subst = NULL;
                    while (src[k] == ' ' || src[k] == '\t') {
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
                    if ((!m->is_variadic && arg_count != m->param_count) || (m->is_variadic && arg_count < m->param_count)) {
                        char msg[96];
                        snprintf(msg, sizeof(msg), "macro '%s' argument count mismatch", name);
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
                    for (ai = 0; ai < arg_count; ++ai) {
                        exp_args[ai] = expand_text(st, args[ai], file, line, depth + 1, disabled, diag);
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

static char *expand_text(pp_state_t *st, const char *src, const char *file, int line, int depth,
                         pp_strvec_t *disabled, cc_diag_t *diag) {
    char *cur;
    int pass;
    pp_strvec_t local_disabled;
    int use_local_disabled = 0;

    if (depth > PP_MAX_EXPAND_DEPTH) {
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
    for (pass = 0; pass < PP_MAX_EXPAND_PASSES; ++pass) {
        char *next = expand_once(st, cur, file, line, depth, disabled, diag);
        if (next == NULL) {
            free(cur);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return NULL;
        }
        if (strlen(next) > PP_MAX_EXPANDED_TEXT) {
            set_diag(diag, (size_t)line, 1, "macro expansion output too large");
            free(cur);
            free(next);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return NULL;
        }
        if (strcmp(next, cur) == 0) {
            free(cur);
            if (use_local_disabled) {
                strvec_free(disabled);
            }
            return next;
        }
        free(cur);
        cur = next;
    }
    if (use_local_disabled) {
        strvec_free(disabled);
    }
    return cur;
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
                if ((j - i) == strlen("__has_include") &&
                    strncmp(expr + i, "__has_include", strlen("__has_include")) == 0) {
                    size_t k = j;
                    char inc_spec[PATH_MAX];
                    char inc_path[PATH_MAX];
                    size_t n = 0;
                    int quoted = 0;
                    int is_system = 0;
                    int enabled = st->std_is_c23 || st->std_is_gnu;
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
                    if (enabled && resolve_include(st, file, inc_spec, quoted, inc_path, &is_system) == 0) {
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
                    if (!st->std_is_gnu) {
                        free(param);
                        strvec_free(&parsed_params);
                        return -1;
                    }
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
    if (st->output_bytes + n > PP_MAX_OUTPUT_SIZE) {
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
    sb_t line;
    sb_t stripped;
    int line_no = 1;
    int had_line = 0;
    int in_block_comment = 0;
    int code_paren_depth = 0;
    int code_start_line = 0;
    pp_cond_frame_t *cond_stack = NULL;
    size_t cond_count = 0;
    size_t cond_cap = 0;
    int saw_pragma_once = 0;
    sb_t code_accum;
    int old_include_level = st->include_level;

    if (depth > PP_MAX_INCLUDE_DEPTH) {
        set_diag(diag, 0, 0, "include depth exceeded");
        return -1;
    }

    st->include_level = depth;

    if (once_contains(st, path)) {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        set_diag(diag, 0, 0, "failed to open include file");
        return -1;
    }
    if (dep_add_path(st, path, 0, depth == 0) != 0) {
        goto fail;
    }
    if (pp_emit_line_marker(st, out, 1, path) != 0) {
        goto fail;
    }

    memset(&line, 0, sizeof(line));
    memset(&stripped, 0, sizeof(stripped));
    memset(&code_accum, 0, sizeof(code_accum));
    while (read_logical_line(fp, &line, &had_line, &line_no, st->enable_trigraphs) == 0 && had_line) {
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
            if (code_accum.len > 0) {
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
                            set_diag(diag, (size_t)line_no, 1, "malformed #include");
                            goto fail;
                        }
                        if (include_next) {
                            if (resolve_include_next(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                                set_diag(diag, (size_t)line_no, 1, "include_next file not found");
                                goto fail;
                            }
                        } else if (resolve_include(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                            set_diag(diag, (size_t)line_no, 1, "include file not found");
                            goto fail;
                        }
                        if (dep_add_path(st, inc_path, is_system, 0) != 0) {
                            goto fail;
                        }
                        if (preprocess_file(st, inc_path, out, depth + 1, macros_only, diag) != 0) {
                            fprintf(stderr, "cpp: note: in file included from %s:%d\n", path, line_no);
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
                        set_diag(diag, (size_t)line_no, 1, "#embed requires c23/gnu mode");
                        goto fail;
                    }
                    p = skip_ws(p);
                    if (*p != '"' && *p != '<') {
                        set_diag(diag, (size_t)line_no, 1, "malformed #embed");
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
                            set_diag(diag, (size_t)line_no, 1, "malformed #embed");
                            goto fail;
                        }
                    }
                    if (resolve_include(st, path, inc_spec, quoted, inc_path, &is_system) != 0) {
                        set_diag(diag, (size_t)line_no, 1, "embed file not found");
                        goto fail;
                    }
                    if (dep_add_path(st, inc_path, is_system, 0) != 0) {
                        goto fail;
                    }
                    if (!macros_only && pp_emit_embed_file(st, out, inc_path) != 0) {
                        set_diag(diag, (size_t)line_no, 1, "failed to emit #embed data");
                        goto fail;
                    }
                }
                continue;
            }
            if (strncmp(kw, "define", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 6) {
                if (active) {
                    if (parse_define_directive(st, p) != 0) {
                        set_diag(diag, (size_t)line_no, 1, "malformed #define");
                        goto fail;
                    }
                }
                continue;
            }
            if (strncmp(kw, "undef", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (active) {
                    p = skip_ws(p);
                    if (!is_ident_start((unsigned char)*p)) {
                        set_diag(diag, (size_t)line_no, 1, "malformed #undef");
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
                    if (eval_condition(st, p, path, line_no, &cond, diag) != 0) {
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
                    set_diag(diag, (size_t)line_no, 1, "#elifdef requires c23/gnu mode");
                    goto fail;
                }
                if (cond_count == 0) {
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elifdef");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elifdef after #else");
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
                    set_diag(diag, (size_t)line_no, 1, "#elifndef requires c23/gnu mode");
                    goto fail;
                }
                if (cond_count == 0) {
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elifndef");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elifndef after #else");
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
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elif");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)line_no, 1, "unexpected #elif after #else");
                    goto fail;
                }
                if (!cond_stack[cond_count - 1].parent_active) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else if (cond_stack[cond_count - 1].any_taken) {
                    cond_stack[cond_count - 1].this_active = 0;
                } else {
                    if (eval_condition(st, p, path, line_no, &cond, diag) != 0) {
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
                    set_diag(diag, (size_t)line_no, 1, "unexpected #else");
                    goto fail;
                }
                if (cond_stack[cond_count - 1].saw_else) {
                    set_diag(diag, (size_t)line_no, 1, "duplicate #else");
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
                    set_diag(diag, (size_t)line_no, 1, "unexpected #endif");
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
                    line_expanded = expand_text(st, p, path, line_no, 0, NULL, diag);
                    if (line_expanded == NULL) {
                        goto fail;
                    }
                    p = skip_ws(line_expanded);
                    if (*p == '\0') {
                        free(line_expanded);
                        set_diag(diag, (size_t)line_no, 1, "malformed #line");
                        goto fail;
                    }
                    errno = 0;
                    v = strtol(p, &end, 10);
                    if (end == p || errno != 0 || v <= 0) {
                        free(line_expanded);
                        set_diag(diag, (size_t)line_no, 1, "malformed #line");
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
                            set_diag(diag, (size_t)line_no, 1, "malformed #line filename");
                            goto fail;
                        }
                        line_target = line_file;
                    }
                    if (pp_emit_line_marker(st, out, (int)v, line_target) != 0) {
                        free(line_expanded);
                        goto fail;
                    }
                    line_no = (int)v - 1;
                    free(line_expanded);
                }
                continue;
            }
            if (strncmp(kw, "pragma", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 6) {
                if (active) {
                    const char *r = skip_ws(p);
                    int stdc_rc = validate_stdc_pragma(r, diag, (size_t)line_no);
                    int clang_rc;
                    if (stdc_rc < 0) {
                        goto fail;
                    }
                    clang_rc = validate_clang_pragma(r, diag, (size_t)line_no);
                    if (clang_rc < 0) {
                        goto fail;
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
                        set_diag(diag, (size_t)line_no, 1, "#warning requires c23/gnu mode");
                        goto fail;
                    }
                    {
                        const char *msg = skip_ws(p);
                        fprintf(stderr, "%s:%d:1: warning: %s\n", path, line_no, msg);
                    }
                }
                continue;
            }
            if (strncmp(kw, "error", (size_t)(p - kw)) == 0 && (size_t)(p - kw) == 5) {
                if (active) {
                    set_diag(diag, (size_t)line_no, 1, "preprocessor #error");
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
                code_start_line = line_no;
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
        if (code_paren_depth != 0) {
            set_diag(diag, (size_t)code_start_line, 1, "unterminated parenthesized expression");
            goto fail;
        }
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
    free(cond_stack);
    sb_free(&line);
    sb_free(&stripped);
    sb_free(&code_accum);
    fclose(fp);
    return 0;

fail:
    st->include_level = old_include_level;
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
    st.enable_trigraphs = std_mode_enable_trigraphs(std_mode);
    st.std_version = std_mode_version(std_mode, &st.std_is_c11, &st.std_is_c17, &st.std_is_c23, &st.std_is_gnu);
    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
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
    free(st.dep_target);
    free(st.dep_file);
    return rc;
}

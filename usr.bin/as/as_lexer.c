#include "as_lexer.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    const as_lexer_cfg_t *cfg;
    as_token_vec_t *out;
    char *errbuf;
    size_t errbuf_sz;
    char **include_stack;
    size_t include_stack_count;
    size_t include_stack_cap;
} lex_ctx_t;

static void set_err(lex_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

void as_token_vec_init(as_token_vec_t *v) {
    if (v == NULL) {
        return;
    }
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static void as_token_free(as_token_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->text);
    free(t->file);
    t->text = NULL;
    t->file = NULL;
}

void as_token_vec_free(as_token_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        as_token_free(&v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static int as_token_vec_push(as_token_vec_t *v, as_token_kind_t kind, const char *text,
                             const char *file, unsigned line, unsigned col) {
    as_token_t *next;

    if (v == NULL || text == NULL || file == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        next = (as_token_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].kind = kind;
    v->items[v->count].text = strdup(text);
    v->items[v->count].file = strdup(file);
    v->items[v->count].line = line;
    v->items[v->count].col = col;
    if (v->items[v->count].text == NULL || v->items[v->count].file == NULL) {
        as_token_free(&v->items[v->count]);
        return -1;
    }
    v->count++;
    return 0;
}

static int is_punct_delim(int ch) {
    return ch == ',' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static int is_operator_token(const char *tok) {
    static const char *const ops[] = {
        "+", "-", "*", "/", "%", "|", "&", "^", "~", "<<", ">>",
    };
    size_t i;

    if (tok == NULL || tok[0] == '\0') {
        return 0;
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(tok, ops[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_x86_register_name(const char *s) {
    static const char *const fixed[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
        "ax", "bx", "cx", "dx", "si", "di", "sp", "bp",
        "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh",
        "rip", "eip", "ip",
        "cs", "ds", "es", "fs", "gs", "ss",
        "st", "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
    };
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (strcmp(s, fixed[i]) == 0) {
            return 1;
        }
    }
    if (s[0] == 'r' && isdigit((unsigned char)s[1])) {
        long n = strtol(s + 1, NULL, 10);
        if (n >= 8 && n <= 31) {
            return 1;
        }
    }
    if ((strncmp(s, "xmm", 3) == 0 || strncmp(s, "ymm", 3) == 0 || strncmp(s, "zmm", 3) == 0) &&
        isdigit((unsigned char)s[3])) {
        return 1;
    }
    if (s[0] == 'k' && isdigit((unsigned char)s[1])) {
        return 1;
    }
    return 0;
}

static int is_arm_like_register_name(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (strcmp(s, "sp") == 0 || strcmp(s, "lr") == 0 || strcmp(s, "pc") == 0 || strcmp(s, "cpsr") == 0 ||
        strcmp(s, "spsr") == 0 || strcmp(s, "xzr") == 0 || strcmp(s, "wzr") == 0) {
        return 1;
    }
    if ((s[0] == 'r' || s[0] == 'w' || s[0] == 'x' || s[0] == 'q' || s[0] == 'd' || s[0] == 's' || s[0] == 'v') &&
        isdigit((unsigned char)s[1])) {
        return 1;
    }
    return 0;
}

static int looks_numeric(const char *s) {
    size_t i = 0;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[i] == '+' || s[i] == '-') {
        i++;
    }
    if (s[i] == '\0') {
        return 0;
    }
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
        for (; s[i] != '\0'; ++i) {
            if (!isxdigit((unsigned char)s[i])) {
                return 0;
            }
        }
        return 1;
    }
    if (s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) {
        i += 2;
        if (s[i] != '0' && s[i] != '1') {
            return 0;
        }
        for (; s[i] != '\0'; ++i) {
            if (s[i] != '0' && s[i] != '1') {
                return 0;
            }
        }
        return 1;
    }
    if (!isdigit((unsigned char)s[i])) {
        return 0;
    }
    for (; s[i] != '\0'; ++i) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static char unescape_single(int c) {
    switch (c) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        return (char)c;
    }
}

static char *unescape_string(const char *in) {
    size_t i;
    size_t j;
    size_t n;
    char *out;

    if (in == NULL) {
        return NULL;
    }
    n = strlen(in);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    j = 0;
    for (i = 0; i < n; ++i) {
        if (in[i] == '\\' && i + 1 < n) {
            int c = in[++i];
            if (c == 'x' && i + 1 < n && isxdigit((unsigned char)in[i + 1])) {
                int v = 0;
                int k = 0;
                while (k < 2 && i + 1 < n && isxdigit((unsigned char)in[i + 1])) {
                    int h = in[++i];
                    v <<= 4;
                    if (h >= '0' && h <= '9') {
                        v |= h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                        v |= 10 + (h - 'a');
                    } else {
                        v |= 10 + (h - 'A');
                    }
                    k++;
                }
                out[j++] = (char)v;
                continue;
            }
            out[j++] = unescape_single(c);
            continue;
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
    return out;
}

static char *strip_comments(const char *line, int *in_block_comment) {
    size_t i;
    size_t j;
    size_t n;
    int in_str = 0;
    int esc = 0;
    char quote = 0;
    char *out;

    if (line == NULL || in_block_comment == NULL) {
        return NULL;
    }
    n = strlen(line);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }

    j = 0;
    for (i = 0; i < n; ++i) {
        int c = (unsigned char)line[i];

        if (*in_block_comment) {
            if (c == '*' && i + 1 < n && line[i + 1] == '/') {
                *in_block_comment = 0;
                i++;
            }
            continue;
        }

        if (in_str) {
            out[j++] = (char)c;
            if (esc) {
                esc = 0;
            } else if (c == '\\') {
                esc = 1;
            } else if (c == quote) {
                in_str = 0;
                quote = 0;
            }
            continue;
        }

        if ((c == '"' || c == '\'') && !*in_block_comment) {
            in_str = 1;
            quote = (char)c;
            out[j++] = (char)c;
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '*') {
            *in_block_comment = 1;
            i++;
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') {
            break;
        }
        if (c == '#' || c == ';') {
            break;
        }
        out[j++] = (char)c;
    }

    out[j] = '\0';
    return out;
}

static void trim_trailing(char *s) {
    size_t n;

    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int append_path(char ***items, size_t *count, size_t *cap, const char *path) {
    char **next;

    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : (*cap * 2);
        next = (char **)realloc(*items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *items = next;
        *cap = ncap;
    }
    (*items)[*count] = strdup(path);
    if ((*items)[*count] == NULL) {
        return -1;
    }
    (*count)++;
    return 0;
}

static char *path_dirname_dup(const char *path) {
    const char *s;
    size_t n;
    char *out;

    if (path == NULL) {
        return strdup(".");
    }
    s = strrchr(path, '/');
    if (s == NULL) {
        return strdup(".");
    }
    n = (size_t)(s - path);
    if (n == 0) {
        return strdup("/");
    }
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *path_join2(const char *a, const char *b) {
    size_t alen;
    size_t blen;
    int slash;
    char *out;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    alen = strlen(a);
    blen = strlen(b);
    slash = (alen > 0 && a[alen - 1] != '/');
    out = (char *)malloc(alen + (size_t)slash + blen + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, alen);
    if (slash) {
        out[alen++] = '/';
    }
    memcpy(out + alen, b, blen);
    out[alen + blen] = '\0';
    return out;
}

static int file_readable(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

static char *resolve_include_path(const as_lexer_cfg_t *cfg, const char *curr_file, const char *include_name) {
    size_t i;
    char *curr_dir;
    char *cand;

    if (include_name == NULL) {
        return NULL;
    }
    if (include_name[0] == '/' && file_readable(include_name)) {
        return strdup(include_name);
    }

    curr_dir = path_dirname_dup(curr_file);
    if (curr_dir != NULL) {
        cand = path_join2(curr_dir, include_name);
        free(curr_dir);
        if (cand != NULL && file_readable(cand)) {
            return cand;
        }
        free(cand);
    }

    if (cfg != NULL) {
        for (i = 0; i < cfg->include_dir_count; ++i) {
            cand = path_join2(cfg->include_dirs[i], include_name);
            if (cand != NULL && file_readable(cand)) {
                return cand;
            }
            free(cand);
        }
    }
    return NULL;
}

static as_token_kind_t classify_token(const char *tok, int intel_syntax) {
    const char *s = tok;

    if (tok == NULL || tok[0] == '\0') {
        return AS_TOK_INVALID;
    }
    if (tok[0] == '.') {
        return AS_TOK_DIRECTIVE;
    }
    if (tok[0] == '$') {
        s = tok + 1;
    }
    if (tok[0] == '%' && is_x86_register_name(tok + 1)) {
        return AS_TOK_REGISTER;
    }
    if ((intel_syntax && is_x86_register_name(tok)) || is_arm_like_register_name(tok)) {
        return AS_TOK_REGISTER;
    }
    if (looks_numeric(s)) {
        return AS_TOK_IMMEDIATE;
    }
    if (is_operator_token(tok)) {
        return AS_TOK_OPERATOR;
    }
    return AS_TOK_IDENTIFIER;
}

static int tokenize_line(const char *file, unsigned line_no, const char *line, int intel_syntax,
                         as_token_vec_t *line_tokens, lex_ctx_t *ctx) {
    size_t i = 0;
    int saw_mnemonic = 0;

    while (line[i] != '\0') {
        size_t start;
        unsigned col;

        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        if (is_punct_delim((unsigned char)line[i])) {
            char punct[2];

            punct[0] = line[i];
            punct[1] = '\0';
            if (as_token_vec_push(line_tokens, AS_TOK_PUNCT, punct, file, line_no, (unsigned)(i + 1)) != 0) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            i++;
            continue;
        }

        start = i;
        col = (unsigned)(i + 1);

        if (line[i] == '"') {
            char *raw;
            char *decoded;
            size_t n;

            i++;
            while (line[i] != '\0') {
                if (line[i] == '\\' && line[i + 1] != '\0') {
                    i += 2;
                    continue;
                }
                if (line[i] == '"') {
                    break;
                }
                i++;
            }
            if (line[i] != '"') {
                set_err(ctx, "%s:%u: unterminated string literal", file, line_no);
                return -1;
            }
            n = i - (start + 1);
            raw = (char *)malloc(n + 1);
            if (raw == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            memcpy(raw, line + start + 1, n);
            raw[n] = '\0';
            decoded = unescape_string(raw);
            free(raw);
            if (decoded == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            if (as_token_vec_push(line_tokens, AS_TOK_STRING, decoded, file, line_no, col) != 0) {
                free(decoded);
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            free(decoded);
            i++;
            continue;
        }

        if (line[i] == '\'' && line[i + 1] != '\0') {
            char tmp[32];
            char c = 0;
            int done = 0;

            i++;
            if (line[i] == '\\' && line[i + 1] != '\0') {
                c = unescape_single((unsigned char)line[++i]);
                i++;
            } else {
                c = line[i++];
            }
            if (line[i] == '\'') {
                i++;
                done = 1;
            }
            if (!done) {
                set_err(ctx, "%s:%u: malformed character literal", file, line_no);
                return -1;
            }
            snprintf(tmp, sizeof(tmp), "%u", (unsigned char)c);
            if (as_token_vec_push(line_tokens, AS_TOK_IMMEDIATE, tmp, file, line_no, col) != 0) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            continue;
        }

        while (line[i] != '\0' && !isspace((unsigned char)line[i]) && !is_punct_delim((unsigned char)line[i])) {
            i++;
        }

        if (i > start) {
            size_t n = i - start;
            char *tok = (char *)malloc(n + 1);
            as_token_kind_t kind;

            if (tok == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            memcpy(tok, line + start, n);
            tok[n] = '\0';

            if (n > 0 && tok[n - 1] == ':') {
                tok[n - 1] = '\0';
                kind = AS_TOK_LABEL;
            } else {
                kind = classify_token(tok, intel_syntax);
                if (kind == AS_TOK_IDENTIFIER && !saw_mnemonic) {
                    kind = AS_TOK_MNEMONIC;
                    saw_mnemonic = 1;
                } else if (kind != AS_TOK_DIRECTIVE && kind != AS_TOK_LABEL) {
                    saw_mnemonic = 1;
                }
            }

            if (as_token_vec_push(line_tokens, kind, tok, file, line_no, col) != 0) {
                free(tok);
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            free(tok);
        }
    }
    return 0;
}

static int lex_file_internal(lex_ctx_t *ctx, const char *path, unsigned depth) {
    FILE *fp;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t nread;
    unsigned line_no = 0;
    int in_block_comment = 0;

    if (ctx == NULL || path == NULL) {
        return -1;
    }
    if (ctx->cfg != NULL && ctx->cfg->max_include_depth > 0 && depth > ctx->cfg->max_include_depth) {
        set_err(ctx, "%s: include depth exceeded (%u)", path, ctx->cfg->max_include_depth);
        return -1;
    }

    if (append_path(&ctx->include_stack, &ctx->include_stack_count, &ctx->include_stack_cap, path) != 0) {
        set_err(ctx, "%s: out of memory", path);
        return -1;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        set_err(ctx, "%s: %s", path, strerror(errno));
        return -1;
    }

    while ((nread = getline(&line, &line_cap, fp)) >= 0) {
        as_token_vec_t line_tokens;
        char *sanitized;
        size_t i;

        line_no++;
        (void)nread;

        sanitized = strip_comments(line, &in_block_comment);
        if (sanitized == NULL) {
            set_err(ctx, "%s:%u: out of memory", path, line_no);
            free(line);
            fclose(fp);
            return -1;
        }
        trim_trailing(sanitized);

        as_token_vec_init(&line_tokens);
        if (tokenize_line(path, line_no, sanitized, ctx->cfg != NULL ? ctx->cfg->intel_syntax : 0, &line_tokens, ctx) != 0) {
            free(sanitized);
            as_token_vec_free(&line_tokens);
            free(line);
            fclose(fp);
            return -1;
        }
        free(sanitized);

        for (i = 0; i < line_tokens.count; ++i) {
            as_token_t *t = &line_tokens.items[i];
            if (as_token_vec_push(ctx->out, t->kind, t->text, t->file, t->line, t->col) != 0) {
                set_err(ctx, "%s:%u: out of memory", path, line_no);
                as_token_vec_free(&line_tokens);
                free(line);
                fclose(fp);
                return -1;
            }
        }

        if (line_tokens.count >= 2 &&
            line_tokens.items[0].kind == AS_TOK_DIRECTIVE &&
            strcmp(line_tokens.items[0].text, ".include") == 0 &&
            line_tokens.items[1].kind == AS_TOK_STRING) {
            char *inc = resolve_include_path(ctx->cfg, path, line_tokens.items[1].text);
            if (inc == NULL) {
                set_err(ctx, "%s:%u: include file not found: %s", path, line_no, line_tokens.items[1].text);
                as_token_vec_free(&line_tokens);
                free(line);
                fclose(fp);
                return -1;
            }

            for (i = 0; i < ctx->include_stack_count; ++i) {
                if (strcmp(ctx->include_stack[i], inc) == 0) {
                    set_err(ctx, "%s:%u: include cycle detected: %s", path, line_no, inc);
                    free(inc);
                    as_token_vec_free(&line_tokens);
                    free(line);
                    fclose(fp);
                    return -1;
                }
            }

            if (lex_file_internal(ctx, inc, depth + 1) != 0) {
                free(inc);
                as_token_vec_free(&line_tokens);
                free(line);
                fclose(fp);
                return -1;
            }
            free(inc);
        }

        as_token_vec_free(&line_tokens);
    }

    free(line);
    fclose(fp);

    free(ctx->include_stack[ctx->include_stack_count - 1]);
    ctx->include_stack_count--;
    return 0;
}

int as_lex_file(const char *path, const as_lexer_cfg_t *cfg, as_token_vec_t *out,
                char *errbuf, size_t errbuf_sz) {
    lex_ctx_t ctx;
    int rc;

    if (path == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    rc = lex_file_internal(&ctx, path, 0);

    while (ctx.include_stack_count > 0) {
        free(ctx.include_stack[ctx.include_stack_count - 1]);
        ctx.include_stack_count--;
    }
    free(ctx.include_stack);
    return rc;
}

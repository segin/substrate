#include "as_data.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    as_data_program_t *out;
    char *errbuf;
    size_t errbuf_sz;
} data_ctx_t;

static void set_err(data_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static char *trim_copy(const char *s) {
    const char *b;
    const char *e;
    size_t n;
    char *out;

    if (s == NULL) {
        return NULL;
    }
    b = s;
    while (*b != '\0' && isspace((unsigned char)*b)) {
        b++;
    }
    e = b + strlen(b);
    while (e > b && isspace((unsigned char)e[-1])) {
        e--;
    }

    n = (size_t)(e - b);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, b, n);
    out[n] = '\0';
    return out;
}

static int parse_s64(const char *raw, long long *out) {
    char *tmp;
    char *end;
    long long v;
    long long a, b, c;
    int used = 0;

    tmp = trim_copy(raw);
    if (tmp == NULL || tmp[0] == '\0') {
        free(tmp);
        return -1;
    }

    if (tmp[0] == '0' && (tmp[1] == 'b' || tmp[1] == 'B')) {
        unsigned long long u = 0;
        const char *p = tmp + 2;
        if (*p == '\0') {
            free(tmp);
            return -1;
        }
        while (*p == '0' || *p == '1') {
            u = (u << 1) | (unsigned long long)(*p - '0');
            ++p;
        }
        if (*p == '\0') {
            *out = (long long)u;
            free(tmp);
            return 0;
        }
    }

    v = strtoll(tmp, &end, 0);
    if (end != tmp && *end == '\0') {
        *out = v;
        free(tmp);
        return 0;
    }

    if (sscanf(tmp, " ( %lld + %lld * %lld ) %n", &a, &b, &c, &used) == 3 && tmp[used] == '\0') {
        *out = a + (b * c);
        free(tmp);
        return 0;
    }
    if (sscanf(tmp, " %lld + %lld * %lld %n", &a, &b, &c, &used) == 3 && tmp[used] == '\0') {
        *out = a + (b * c);
        free(tmp);
        return 0;
    }

    free(tmp);
    return -1;
}

static int parse_u64(const char *raw, unsigned long long *out) {
    char *tmp;
    char *end;
    unsigned long long v;

    tmp = trim_copy(raw);
    if (tmp == NULL || tmp[0] == '\0') {
        free(tmp);
        return -1;
    }

    v = strtoull(tmp, &end, 0);
    if (end == tmp || *end != '\0') {
        free(tmp);
        return -1;
    }

    free(tmp);
    *out = v;
    return 0;
}

void as_data_program_init(as_data_program_t *p) {
    if (p == NULL) {
        return;
    }
    p->items = NULL;
    p->count = 0;
    p->cap = 0;
}

static void free_op(as_data_op_t *op) {
    if (op == NULL) {
        return;
    }
    free(op->file);
    free(op->directive);
    switch (op->kind) {
    case AS_DATA_INT:
        free(op->u.ints.values);
        break;
    case AS_DATA_FLOAT:
        free(op->u.floats.values);
        break;
    case AS_DATA_STRING:
        free(op->u.str.bytes);
        break;
    case AS_DATA_INCBIN:
        free(op->u.incbin.path);
        break;
    default:
        break;
    }
    memset(op, 0, sizeof(*op));
}

void as_data_program_free(as_data_program_t *p) {
    size_t i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < p->count; ++i) {
        free_op(&p->items[i]);
    }
    free(p->items);
    p->items = NULL;
    p->count = 0;
    p->cap = 0;
}

static int push_op(as_data_program_t *p, const as_data_op_t *src) {
    as_data_op_t *next;

    if (p->count == p->cap) {
        size_t ncap = p->cap == 0 ? 32 : p->cap * 2;
        next = (as_data_op_t *)realloc(p->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        p->items = next;
        p->cap = ncap;
    }

    p->items[p->count] = *src;
    p->count++;
    return 0;
}

static int init_op_from_stmt(as_data_op_t *op, const as_stmt_t *st) {
    memset(op, 0, sizeof(*op));
    op->file = xstrdup(st->file);
    op->line = st->line;
    op->directive = xstrdup(st->u.directive.name);
    if (op->file == NULL || op->directive == NULL) {
        free(op->file);
        free(op->directive);
        return -1;
    }
    return 0;
}

static int parse_int_directive(data_ctx_t *ctx, const as_stmt_t *st, unsigned width) {
    as_data_op_t op;
    const as_directive_t *d = &st->u.directive;
    size_t i;

    if (d->arg_count == 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_INT;
    op.u.ints.width = width;
    op.u.ints.values = (long long *)calloc(d->arg_count, sizeof(long long));
    op.u.ints.count = d->arg_count;
    if (op.u.ints.values == NULL) {
        free_op(&op);
        return -1;
    }

    for (i = 0; i < d->arg_count; ++i) {
        if (parse_s64(d->args[i], &op.u.ints.values[i]) != 0) {
            /*
             * Keep data parsing permissive for symbolic reloc expressions.
             * Relocation emission resolves symbols separately.
             */
            op.u.ints.values[i] = 0;
        }
    }

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_float_directive(data_ctx_t *ctx, const as_stmt_t *st, int is_double) {
    as_data_op_t op;
    const as_directive_t *d = &st->u.directive;
    size_t i;

    if (d->arg_count == 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_FLOAT;
    op.u.floats.is_double = is_double;
    op.u.floats.values = (double *)calloc(d->arg_count, sizeof(double));
    op.u.floats.count = d->arg_count;
    if (op.u.floats.values == NULL) {
        free_op(&op);
        return -1;
    }

    for (i = 0; i < d->arg_count; ++i) {
        char *tmp = trim_copy(d->args[i]);
        char *end;
        if (tmp == NULL || tmp[0] == '\0') {
            free(tmp);
            free_op(&op);
            return -1;
        }
        op.u.floats.values[i] = strtod(tmp, &end);
        if (end == tmp || *end != '\0') {
            free(tmp);
            free_op(&op);
            return -1;
        }
        free(tmp);
    }

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_string_directive(data_ctx_t *ctx, const as_stmt_t *st, int nul_terminated) {
    as_data_op_t op;
    const as_directive_t *d = &st->u.directive;
    size_t i;

    if (d->arg_count == 0) {
        return -1;
    }

    for (i = 0; i < d->arg_count; ++i) {
        char *bytes = trim_copy(d->args[i]);
        if (bytes == NULL) {
            return -1;
        }

        if (init_op_from_stmt(&op, st) != 0) {
            free(bytes);
            return -1;
        }

        op.kind = AS_DATA_STRING;
        op.u.str.bytes = bytes;
        op.u.str.len = strlen(bytes);
        op.u.str.nul_terminated = nul_terminated;

        if (push_op(ctx->out, &op) != 0) {
            free_op(&op);
            return -1;
        }
    }

    return 0;
}

static char *dirname_dup(const char *path) {
    const char *slash;
    size_t n;
    char *out;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }

    n = (size_t)(slash - path);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

static char *join_path2(const char *a, const char *b) {
    size_t an;
    size_t bn;
    char *out;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    an = strlen(a);
    bn = strlen(b);
    out = (char *)malloc(an + 1 + bn + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, an);
    out[an] = '/';
    memcpy(out + an + 1, b, bn);
    out[an + 1 + bn] = '\0';
    return out;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int parse_zero_like(data_ctx_t *ctx, const as_stmt_t *st) {
    as_data_op_t op;
    unsigned long long count;

    if (st->u.directive.arg_count < 1 || parse_u64(st->u.directive.args[0], &count) != 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_ZERO;
    op.u.zero.count = count;

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_align_directive(data_ctx_t *ctx, const as_stmt_t *st) {
    as_data_op_t op;
    long long v;
    unsigned long long align;

    if (st->u.directive.arg_count < 1 || parse_s64(st->u.directive.args[0], &v) != 0 || v < 0) {
        return -1;
    }
    if (strcmp(st->u.directive.name, ".p2align") == 0) {
        if (v >= 63) {
            return -1;
        }
        align = 1ULL << (unsigned)v;
    } else {
        align = (unsigned long long)v;
    }
    if (align == 0 || (align & (align - 1ULL)) != 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_ALIGN;
    op.u.align.value = align;
    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_fill(data_ctx_t *ctx, const as_stmt_t *st) {
    as_data_op_t op;
    unsigned long long repeat = 0;
    unsigned long long size = 1;
    unsigned long long value = 0;

    if (st->u.directive.arg_count < 1 || parse_u64(st->u.directive.args[0], &repeat) != 0) {
        return -1;
    }
    if (st->u.directive.arg_count >= 2 && parse_u64(st->u.directive.args[1], &size) != 0) {
        return -1;
    }
    if (st->u.directive.arg_count >= 3 && parse_u64(st->u.directive.args[2], &value) != 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_FILL;
    op.u.fill.repeat = repeat;
    op.u.fill.size = size;
    op.u.fill.value = value;

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_org(data_ctx_t *ctx, const as_stmt_t *st) {
    as_data_op_t op;
    unsigned long long off = 0;

    if (st->u.directive.arg_count < 1 || parse_u64(st->u.directive.args[0], &off) != 0) {
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        return -1;
    }
    op.kind = AS_DATA_ORG;
    op.u.org.offset = off;

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

static int parse_incbin(data_ctx_t *ctx, const as_stmt_t *st) {
    as_data_op_t op;
    char *path_arg;
    char *resolved = NULL;
    unsigned long long skip = 0;
    unsigned long long count = 0;

    if (st->u.directive.arg_count < 1) {
        return -1;
    }

    path_arg = trim_copy(st->u.directive.args[0]);
    if (path_arg == NULL) {
        return -1;
    }

    if (path_arg[0] == '/') {
        resolved = xstrdup(path_arg);
    } else {
        char *dir = dirname_dup(st->file);
        if (dir != NULL) {
            resolved = join_path2(dir, path_arg);
        }
        free(dir);
    }
    if (resolved == NULL) {
        free(path_arg);
        return -1;
    }
    if (!file_exists(resolved)) {
        free(path_arg);
        free(resolved);
        return -1;
    }

    if (st->u.directive.arg_count >= 2 && parse_u64(st->u.directive.args[1], &skip) != 0) {
        free(path_arg);
        free(resolved);
        return -1;
    }
    if (st->u.directive.arg_count >= 3 && parse_u64(st->u.directive.args[2], &count) != 0) {
        free(path_arg);
        free(resolved);
        return -1;
    }

    if (init_op_from_stmt(&op, st) != 0) {
        free(path_arg);
        free(resolved);
        return -1;
    }
    op.kind = AS_DATA_INCBIN;
    op.u.incbin.path = resolved;
    op.u.incbin.skip = skip;
    op.u.incbin.count = count;
    op.u.incbin.has_count = (st->u.directive.arg_count >= 3);

    free(path_arg);

    if (push_op(ctx->out, &op) != 0) {
        free_op(&op);
        return -1;
    }
    return 0;
}

int as_data_build(const as_parse_result_t *parsed, as_data_program_t *out,
                  char *errbuf, size_t errbuf_sz) {
    data_ctx_t ctx;
    size_t i;

    if (parsed == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        const char *dname;
        int rc = 0;

        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        dname = st->u.directive.name;

        if (strcmp(dname, ".byte") == 0) {
            rc = parse_int_directive(&ctx, st, 1);
        } else if (strcmp(dname, ".short") == 0 || strcmp(dname, ".hword") == 0) {
            rc = parse_int_directive(&ctx, st, 2);
        } else if (strcmp(dname, ".long") == 0 || strcmp(dname, ".int") == 0) {
            rc = parse_int_directive(&ctx, st, 4);
        } else if (strcmp(dname, ".quad") == 0 || strcmp(dname, ".8byte") == 0) {
            rc = parse_int_directive(&ctx, st, 8);
        } else if (strcmp(dname, ".float") == 0) {
            rc = parse_float_directive(&ctx, st, 0);
        } else if (strcmp(dname, ".double") == 0) {
            rc = parse_float_directive(&ctx, st, 1);
        } else if (strcmp(dname, ".ascii") == 0) {
            rc = parse_string_directive(&ctx, st, 0);
        } else if (strcmp(dname, ".asciz") == 0 || strcmp(dname, ".string") == 0) {
            rc = parse_string_directive(&ctx, st, 1);
        } else if (strcmp(dname, ".zero") == 0 || strcmp(dname, ".space") == 0 || strcmp(dname, ".skip") == 0) {
            rc = parse_zero_like(&ctx, st);
        } else if (strcmp(dname, ".align") == 0 || strcmp(dname, ".balign") == 0 || strcmp(dname, ".p2align") == 0) {
            rc = parse_align_directive(&ctx, st);
        } else if (strcmp(dname, ".fill") == 0) {
            rc = parse_fill(&ctx, st);
        } else if (strcmp(dname, ".org") == 0) {
            rc = parse_org(&ctx, st);
        } else if (strcmp(dname, ".incbin") == 0) {
            rc = parse_incbin(&ctx, st);
        } else {
            continue;
        }

        if (rc != 0) {
            set_err(&ctx, "%s:%u: malformed data directive %s", st->file, st->line, dname);
            return -1;
        }
    }

    return 0;
}

#include "as_symtab.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    as_symtab_t *tab;
    char *errbuf;
    size_t errbuf_sz;
} sym_ctx_t;

static size_t hash_name(const char *name) {
    size_t h = 1469598103934665603ull;

    while (name != NULL && *name != '\0') {
        h ^= (unsigned char)*name++;
        h *= 1099511628211ull;
    }
    return h;
}

static int symtab_index_rehash(as_symtab_t *tab, size_t cap) {
    size_t *slots;
    size_t i;

    if (tab == NULL || cap == 0) {
        return -1;
    }
    slots = (size_t *)calloc(cap, sizeof(*slots));
    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < tab->count; ++i) {
        size_t pos = hash_name(tab->items[i].name) & (cap - 1);

        while (slots[pos] != 0) {
            pos = (pos + 1) & (cap - 1);
        }
        slots[pos] = i + 1;
    }
    free(tab->name_index);
    tab->name_index = slots;
    tab->name_index_cap = cap;
    return 0;
}

static int symtab_index_ensure(as_symtab_t *tab) {
    size_t cap;

    if (tab == NULL) {
        return -1;
    }
    if (tab->name_index_cap != 0 && (tab->count + 1) * 2 < tab->name_index_cap) {
        return 0;
    }
    cap = 64;
    while (cap <= (tab->count + 1) * 2) {
        cap <<= 1;
    }
    return symtab_index_rehash(tab, cap);
}

static int ascii_eq_ci(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
    }
    return (*a == '\0' && *b == '\0') ? 1 : 0;
}

static void set_err(sym_ctx_t *ctx, const char *fmt, ...) {
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

static void strip_reloc_modifier(char *name) {
    char *at;

    if (name == NULL) {
        return;
    }
    at = strchr(name, '@');
    if (at == NULL || at[1] == '\0') {
        return;
    }
    if (ascii_eq_ci(at + 1, "PLT") ||
        ascii_eq_ci(at + 1, "GOTPCREL") ||
        ascii_eq_ci(at + 1, "GOTTPOFF")) {
        *at = '\0';
    }
}

void as_symtab_init(as_symtab_t *tab) {
    if (tab == NULL) {
        return;
    }
    tab->items = NULL;
    tab->count = 0;
    tab->cap = 0;
    tab->name_index = NULL;
    tab->name_index_cap = 0;
}

void as_symtab_free(as_symtab_t *tab) {
    size_t i;

    if (tab == NULL) {
        return;
    }
    for (i = 0; i < tab->count; ++i) {
        as_symbol_t *s = &tab->items[i];
        free(s->name);
        free(s->def_file);
        free(s->alias_target);
        free(s->version);
    }
    free(tab->items);
    free(tab->name_index);
    tab->items = NULL;
    tab->count = 0;
    tab->cap = 0;
    tab->name_index = NULL;
    tab->name_index_cap = 0;
}

static as_symbol_t *find_symbol(as_symtab_t *tab, const char *name) {
    size_t pos;

    if (tab == NULL || name == NULL || tab->name_index == NULL || tab->name_index_cap == 0) {
        return NULL;
    }
    pos = hash_name(name) & (tab->name_index_cap - 1);
    while (tab->name_index[pos] != 0) {
        size_t idx = tab->name_index[pos] - 1;
        if (strcmp(tab->items[idx].name, name) == 0) {
            return &tab->items[idx];
        }
        pos = (pos + 1) & (tab->name_index_cap - 1);
    }
    return NULL;
}

static as_symbol_t *get_or_create_symbol(sym_ctx_t *ctx, const char *name) {
    as_symbol_t *sym;
    as_symbol_t *next;

    sym = find_symbol(ctx->tab, name);
    if (sym != NULL) {
        return sym;
    }

    if (ctx->tab->count == ctx->tab->cap) {
        size_t ncap = ctx->tab->cap == 0 ? 32 : ctx->tab->cap * 2;
        next = (as_symbol_t *)realloc(ctx->tab->items, ncap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        ctx->tab->items = next;
        ctx->tab->cap = ncap;
    }
    if (symtab_index_ensure(ctx->tab) != 0) {
        return NULL;
    }

    sym = &ctx->tab->items[ctx->tab->count++];
    memset(sym, 0, sizeof(*sym));
    sym->name = xstrdup(name);
    sym->bind = AS_SYM_BIND_LOCAL;
    sym->type = AS_SYM_TYPE_NOTYPE;
    sym->visibility = AS_SYM_VIS_DEFAULT;
    if (sym->name == NULL) {
        return NULL;
    }
    {
        size_t pos = hash_name(sym->name) & (ctx->tab->name_index_cap - 1);
        while (ctx->tab->name_index[pos] != 0) {
            pos = (pos + 1) & (ctx->tab->name_index_cap - 1);
        }
        ctx->tab->name_index[pos] = ctx->tab->count;
    }
    return sym;
}

static int is_local_temp_symbol_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (name[0] == '.' && name[1] == 'L') {
        return 1;
    }
    return 0;
}

static int parse_u64_arg(const char *s, unsigned long long *out) {
    char *tmp;
    char *end;
    unsigned long long v;

    tmp = trim_copy(s);
    if (tmp == NULL) {
        return -1;
    }

    if (tmp[0] == '\0') {
        free(tmp);
        return -1;
    }

    end = NULL;
    v = strtoull(tmp, &end, 0);
    if (end == tmp || *end != '\0') {
        free(tmp);
        return -1;
    }

    free(tmp);
    *out = v;
    return 0;
}

static int parse_i64_arg(const char *s, long long *out) {
    char *tmp;
    char *end;
    long long v;

    tmp = trim_copy(s);
    if (tmp == NULL) {
        return -1;
    }
    if (tmp[0] == '\0') {
        free(tmp);
        return -1;
    }
    end = NULL;
    v = strtoll(tmp, &end, 0);
    if (end == tmp || *end != '\0') {
        free(tmp);
        return -1;
    }
    free(tmp);
    *out = v;
    return 0;
}

static int parse_set_rhs_symbol(const char *expr, char **out_name, long long *out_addend) {
    char *tmp;
    char *op = NULL;
    char sign = '+';
    size_t i;
    char *name;
    long long addend = 0;

    if (expr == NULL || out_name == NULL || out_addend == NULL) {
        return -1;
    }

    tmp = trim_copy(expr);
    if (tmp == NULL) {
        return -1;
    }
    if (tmp[0] == '\0') {
        free(tmp);
        return -1;
    }

    for (i = 1; tmp[i] != '\0'; ++i) {
        if (tmp[i] == '+' || tmp[i] == '-') {
            op = tmp + i;
            sign = tmp[i];
            break;
        }
    }

    if (op != NULL) {
        *op = '\0';
        if (parse_i64_arg(op + 1, &addend) != 0) {
            free(tmp);
            return -1;
        }
        if (sign == '-') {
            addend = -addend;
        }
    }

    name = trim_copy(tmp);
    free(tmp);
    if (name == NULL) {
        return -1;
    }
    strip_reloc_modifier(name);
    if (name[0] == '\0') {
        free(name);
        return -1;
    }

    *out_name = name;
    *out_addend = addend;
    return 0;
}

static int parse_set_rhs_dot_minus_symbol(const char *expr, char **out_name) {
    char *tmp;
    char *s;
    char *name;

    if (expr == NULL || out_name == NULL) {
        return -1;
    }
    tmp = trim_copy(expr);
    if (tmp == NULL) {
        return -1;
    }
    s = tmp;
    if (*s != '.') {
        free(tmp);
        return -1;
    }
    s++;
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '-') {
        free(tmp);
        return -1;
    }
    s++;
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        free(tmp);
        return -1;
    }
    name = trim_copy(s);
    free(tmp);
    if (name == NULL) {
        return -1;
    }
    strip_reloc_modifier(name);
    if (name[0] == '\0' || strcmp(name, ".") == 0) {
        free(name);
        return -1;
    }
    *out_name = name;
    return 0;
}

static int parse_size_arg(const char *expr, const char *sym_name, unsigned long long *out) {
    char *tmp;
    char *dash;

    if (expr == NULL || out == NULL) {
        return -1;
    }
    if (parse_u64_arg(expr, out) == 0) {
        return 0;
    }

    tmp = trim_copy(expr);
    if (tmp == NULL) {
        return -1;
    }

    /*
     * Accept common symbolic forms emitted by C compilers:
     *   .size foo, .-foo
     *   .size foo, .Lend-foo
     * We record size as 0 here when symbolic.
     */
    if (sym_name != NULL) {
        if (strncmp(tmp, ".-", 2) == 0 && strcmp(tmp + 2, sym_name) == 0) {
            free(tmp);
            *out = 0;
            return 0;
        }
        dash = strrchr(tmp, '-');
        if (dash != NULL) {
            char *rhs = trim_copy(dash + 1);
            if (rhs == NULL) {
                free(tmp);
                return -1;
            }
            if (strcmp(rhs, sym_name) == 0) {
                free(rhs);
                free(tmp);
                *out = 0;
                return 0;
            }
            free(rhs);
        }
    }

    free(tmp);
    return -1;
}

static int symbol_set_definition(as_symbol_t *sym, const char *file, unsigned line) {
    if (sym->defined) {
        if ((sym->def_file != NULL && file != NULL && strcmp(sym->def_file, file) == 0 && sym->def_line == line) ||
            (sym->def_file == NULL && file == NULL && sym->def_line == line)) {
            return 0;
        }
        return 1;
    }
    if (!sym->defined) {
        sym->def_file = xstrdup(file);
        if (sym->def_file == NULL) {
            return -1;
        }
        sym->def_line = line;
    }
    sym->defined = 1;
    return 0;
}

static int apply_visibility(sym_ctx_t *ctx, const as_directive_t *d, as_sym_visibility_t v) {
    size_t i;

    for (i = 0; i < d->arg_count; ++i) {
        char *name = trim_copy(d->args[i]);
        as_symbol_t *sym;
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        sym->visibility = v;
    }
    return 0;
}

static as_sym_type_t parse_type_name(const char *raw) {
    char *tmp;
    char *s;
    as_sym_type_t t = AS_SYM_TYPE_NOTYPE;

    tmp = trim_copy(raw);
    if (tmp == NULL) {
        return AS_SYM_TYPE_NOTYPE;
    }
    s = tmp;
    while (*s == '@' || *s == '%' || *s == '#') {
        s++;
    }

    if (strcmp(s, "function") == 0 || strcmp(s, "func") == 0) {
        t = AS_SYM_TYPE_FUNCTION;
    } else if (strcmp(s, "object") == 0) {
        t = AS_SYM_TYPE_OBJECT;
    } else if (strcmp(s, "tls_object") == 0) {
        t = AS_SYM_TYPE_TLS_OBJECT;
    } else if (strcmp(s, "common") == 0) {
        t = AS_SYM_TYPE_COMMON;
    } else if (strcmp(s, "notype") == 0) {
        t = AS_SYM_TYPE_NOTYPE;
    }

    free(tmp);
    return t;
}

static int handle_directive(sym_ctx_t *ctx, const as_stmt_t *st) {
    const as_directive_t *d = &st->u.directive;
    size_t i;

    if (strcmp(d->name, ".globl") == 0 || strcmp(d->name, ".global") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_GLOBAL;
        }
        return 0;
    }

    if (strcmp(d->name, ".local") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_LOCAL;
        }
        return 0;
    }

    if (strcmp(d->name, ".weak") == 0) {
        for (i = 0; i < d->arg_count; ++i) {
            char *name = trim_copy(d->args[i]);
            as_symbol_t *sym;
            if (name == NULL) {
                return -1;
            }
            sym = get_or_create_symbol(ctx, name);
            free(name);
            if (sym == NULL) {
                return -1;
            }
            sym->bind = AS_SYM_BIND_WEAK;
        }
        return 0;
    }

    if (strcmp(d->name, ".comm") == 0 || strcmp(d->name, ".lcomm") == 0) {
        char *name;
        as_symbol_t *sym;
        unsigned long long nsize = 0;
        unsigned long long nalign = 0;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        if (parse_u64_arg(d->args[1], &nsize) != 0) {
            return -1;
        }
        if (d->arg_count >= 3 && parse_u64_arg(d->args[2], &nalign) != 0) {
            return -1;
        }

        sym->is_common = 1;
        sym->defined = 1;
        sym->type = AS_SYM_TYPE_COMMON;
        sym->common_size = nsize;
        sym->common_align = nalign;
        sym->bind = (strcmp(d->name, ".lcomm") == 0) ? AS_SYM_BIND_LOCAL : AS_SYM_BIND_GLOBAL;
        return 0;
    }

    if (strcmp(d->name, ".type") == 0) {
        char *name;
        as_symbol_t *sym;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        sym->type = parse_type_name(d->args[1]);
        return 0;
    }

    if (strcmp(d->name, ".size") == 0) {
        char *name;
        as_symbol_t *sym;
        unsigned long long nsize = 0;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        if (sym == NULL) {
            free(name);
            return -1;
        }
        if (parse_size_arg(d->args[1], name, &nsize) != 0) {
            free(name);
            return -1;
        }
        free(name);
        sym->size = nsize;
        return 0;
    }

    if (strcmp(d->name, ".set") == 0 || strcmp(d->name, ".equ") == 0) {
        char *name;
        as_symbol_t *sym;
        unsigned long long abs_value = 0;
        char *target = NULL;
        long long addend = 0;
        int rc;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        if (name == NULL) {
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }

        free(sym->alias_target);
        sym->alias_target = NULL;
        sym->alias_from_dot = 0;
        sym->alias_addend = 0;
        sym->is_absolute = 0;
        sym->absolute_value = 0;
        sym->is_common = 0;
        rc = symbol_set_definition(sym, st->file, st->line);
        if (rc < 0) {
            return -1;
        }
        if (rc > 0) {
            free(sym->def_file);
            sym->def_file = xstrdup(st->file);
            if (sym->def_file == NULL) {
                return -1;
            }
            sym->def_line = st->line;
            sym->defined = 1;
        }

        if (parse_u64_arg(d->args[1], &abs_value) == 0) {
            sym->is_absolute = 1;
            sym->absolute_value = abs_value;
            return 0;
        }
        if (parse_set_rhs_dot_minus_symbol(d->args[1], &target) == 0) {
            sym->alias_target = target;
            sym->alias_from_dot = 1;
            sym->alias_addend = 0;
            return 0;
        }

        if (parse_set_rhs_symbol(d->args[1], &target, &addend) != 0) {
            return -1;
        }
        sym->alias_target = target;
        sym->alias_addend = addend;
        return 0;
    }

    if (strcmp(d->name, ".hidden") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_HIDDEN);
    }
    if (strcmp(d->name, ".protected") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_PROTECTED);
    }
    if (strcmp(d->name, ".internal") == 0) {
        return apply_visibility(ctx, d, AS_SYM_VIS_INTERNAL);
    }

    if (strcmp(d->name, ".symver") == 0) {
        char *name;
        char *ver;
        as_symbol_t *sym;

        if (d->arg_count < 2) {
            return -1;
        }
        name = trim_copy(d->args[0]);
        ver = trim_copy(d->args[1]);
        if (name == NULL || ver == NULL) {
            free(name);
            free(ver);
            return -1;
        }
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            free(ver);
            return -1;
        }
        free(sym->version);
        sym->version = ver;
        return 0;
    }

    return 0;
}

static int visit_expr_ref(sym_ctx_t *ctx, const as_expr_t *e, const char *file, unsigned line) {
    as_symbol_t *sym;

    if (e == NULL) {
        return 0;
    }

    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        char *name = trim_copy(e->symbol);
        if (name == NULL) {
            return -1;
        }
        strip_reloc_modifier(name);
        sym = get_or_create_symbol(ctx, name);
        free(name);
        if (sym == NULL) {
            return -1;
        }
        sym->reference_count++;
        if (!sym->defined || (sym->def_file != NULL && strcmp(sym->def_file, file) == 0 && sym->def_line > line)) {
            sym->forward_ref_count++;
        }
    }

    if (visit_expr_ref(ctx, e->lhs, file, line) != 0) {
        return -1;
    }
    if (visit_expr_ref(ctx, e->rhs, file, line) != 0) {
        return -1;
    }
    return 0;
}

static int collect_instruction_refs(sym_ctx_t *ctx, const as_stmt_t *st) {
    const as_instruction_t *in = &st->u.instr;
    size_t i;

    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];

        switch (op->kind) {
        case AS_OPERAND_IMMEDIATE:
        case AS_OPERAND_LABEL_REF:
            if (visit_expr_ref(ctx, op->u.expr, st->file, st->line) != 0) {
                return -1;
            }
            break;
        case AS_OPERAND_MEMORY:
            if (visit_expr_ref(ctx, op->u.mem.disp, st->file, st->line) != 0) {
                return -1;
            }
            break;
        case AS_OPERAND_SHIFTED_REGISTER:
            if (visit_expr_ref(ctx, op->u.shifted.amount_expr, st->file, st->line) != 0) {
                return -1;
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

static int is_numeric_local_label_name(const char *name) {
    return name != NULL && name[0] >= '0' && name[0] <= '9' && name[1] == '\0';
}

int as_symtab_build(const as_parse_result_t *parsed, as_symtab_t *tab,
                    char *errbuf, size_t errbuf_sz) {
    sym_ctx_t ctx;
    size_t i;

    if (parsed == NULL || tab == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.tab = tab;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    /* Pass 1: definitions and symbol directives. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        size_t j;

        for (j = 0; j < st->label_count; ++j) {
            const as_label_def_t *l = &st->labels[j];
            as_symbol_t *sym;
            int rc;
            if (is_numeric_local_label_name(l->name)) {
                continue;
            }
            sym = get_or_create_symbol(&ctx, l->name);
            if (sym == NULL) {
                set_err(&ctx, "%s:%u: out of memory", st->file, st->line);
                return -1;
            }
            rc = symbol_set_definition(sym, l->file, l->line);
            if (rc == 1) {
                set_err(&ctx, "%s:%u: redefinition of symbol %s", l->file, l->line, l->name);
                return -1;
            }
            if (rc != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file, st->line);
                return -1;
            }
        }

        if (st->kind == AS_STMT_DIRECTIVE) {
            if (handle_directive(&ctx, st) != 0) {
                set_err(&ctx, "%s:%u: malformed directive %s", st->file, st->line, st->u.directive.name);
                return -1;
            }
        }
    }

    /* Pass 2: references and forward-reference tracking. */
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind == AS_STMT_INSTRUCTION) {
            if (collect_instruction_refs(&ctx, st) != 0) {
                set_err(&ctx, "%s:%u: out of memory", st->file, st->line);
                return -1;
            }
        }
    }

    for (i = 0; i < tab->count; ++i) {
        as_symbol_t *sym = &tab->items[i];
        sym->unresolved = (!sym->defined && !sym->is_common && sym->reference_count > 0) ? 1 : 0;
        if (sym->unresolved && sym->bind == AS_SYM_BIND_LOCAL && !is_local_temp_symbol_name(sym->name)) {
            /* GAS-compatible default: unresolved non-temporary symbols are extern/global. */
            sym->bind = AS_SYM_BIND_GLOBAL;
        }
    }

    return 0;
}

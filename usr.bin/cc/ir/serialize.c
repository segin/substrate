#include "ir.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The names of one function's values in the order normalization numbers them. */
typedef struct {
    char **names;
    size_t count;
    size_t cap;
} name_map_t;

/** Whether @p c can be part of a value name. */
static int is_name_char(int c) {
    return isalnum(c) || c == '_' || c == '.';
}

/**
 * The number normalization gives value @p name: its place in the order
 * the function first mentions it, adding it when it is new.
 *
 * @return the number, or -1 when out of memory
 */
static long map_number(name_map_t *m, const char *name, size_t len) {
    size_t i;
    char *copy;

    for (i = 0; i < m->count; ++i) {
        if (strlen(m->names[i]) == len && strncmp(m->names[i], name, len) == 0) {
            return (long)i;
        }
    }
    if (m->count == m->cap) {
        size_t ncap = m->cap == 0 ? 64 : m->cap * 2;
        char **next = (char **)realloc(m->names, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        m->names = next;
        m->cap = ncap;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';
    m->names[m->count] = copy;
    return (long)m->count++;
}

/**
 * Write @p text with each value name replaced by its number, so that
 * two functions differing only in how their values are named print the
 * same.  A quoted string is copied as it is.
 *
 * @return 0, or -1 when out of memory
 */
static int put_renamed(FILE *out, const char *text, name_map_t *m) {
    const char *p = text;

    while (*p != '\0') {
        if (*p == '"') {
            const char *e = strchr(p + 1, '"');
            size_t n = e != NULL ? (size_t)(e - p) + 1 : strlen(p);

            fwrite(p, 1, n, out);
            p += n;
        } else if (*p == '%' && is_name_char((unsigned char)p[1])) {
            size_t len = 1;
            long num;

            while (is_name_char((unsigned char)p[1 + len])) {
                len++;
            }
            num = map_number(m, p + 1, len);
            if (num < 0) {
                return -1;
            }
            fprintf(out, "%%%ld", num);
            p += 1 + len;
        } else {
            fputc(*p++, out);
        }
    }
    return 0;
}

int ir_serialize_module(const ir_module_t *m, FILE *out, int normalize_ids) {
    size_t i;

    if (m == NULL || out == NULL) {
        return -1;
    }

    fprintf(out, "module \"%s\" {\n", m->name ? m->name : "");
    if (m->target != NULL) {
        fprintf(out, "  target %s;\n", m->target);
    }
    if (m->legal) {
        fputs("  stage legal;\n", out);
    }

    for (i = 0; i < m->func_count; ++i) {
        size_t b;
        size_t k;
        const ir_func_t *f = &m->funcs[i];
        name_map_t names;
        int rc = 0;

        memset(&names, 0, sizeof(names));
        /* the arguments are numbered first, in order */
        for (k = 0; normalize_ids && k < f->arg_count && rc == 0; ++k) {
            rc = map_number(&names, f->args[k], strlen(f->args[k])) < 0 ? -1 : 0;
        }
        fprintf(out, "\n  func @%s {\n", f->name ? f->name : "");
        for (b = 0; b < f->block_count && rc == 0; ++b) {
            size_t in;
            const ir_block_t *blk = &f->blocks[b];
            fprintf(out, "  %s:\n", blk->name ? blk->name : "");
            for (in = 0; in < blk->instr_count && rc == 0; ++in) {
                const char *text = blk->instrs[in].text ? blk->instrs[in].text : "";
                size_t len = strlen(text);

                /* the reader keeps a trailing ';' in the text; write one */
                while (len > 0 && (text[len - 1] == ';' || isspace((unsigned char)text[len - 1]))) {
                    len--;
                }
                fputs("    ", out);
                if (normalize_ids) {
                    char *copy = (char *)malloc(len + 1);

                    if (copy == NULL) {
                        rc = -1;
                        break;
                    }
                    memcpy(copy, text, len);
                    copy[len] = '\0';
                    rc = put_renamed(out, copy, &names);
                    free(copy);
                } else {
                    fwrite(text, 1, len, out);
                }
                fputs(";\n", out);
            }
        }
        fprintf(out, "  }\n");
        for (k = 0; k < names.count; ++k) {
            free(names.names[k]);
        }
        free(names.names);
        if (rc != 0) {
            return -1;
        }
    }

    fprintf(out, "}\n");
    return ferror(out) ? -1 : 0;
}

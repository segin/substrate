/*
 * ld_version.c -- version scripts: --version-script FILE and a linker
 * script's VERSION { }, as GNU ld reads them.
 *
 * A version script is a list of version nodes, each a name (or none, for
 * an anonymous one) and lists of patterns under `global:` and `local:`,
 * and after its closing brace the node it depends on:
 *
 *     LINUX_2.6 { global: clock_gettime; __vdso_*; local: *; };
 *
 * A defined global symbol the patterns match is given the node's version
 * (the default one, name@@NODE) when a global pattern matches, or is made
 * local, not exported, when a local one does.  An exact name is taken over
 * a pattern, and `*` last, as GNU ld takes them.
 */

#include "ld.h"

#include <fnmatch.h>

/* A word of a version script, and where the text goes on. */
static const char *vs_word(const char *p, char *buf, size_t cap, int *is_quoted) {
    size_t n = 0;

    *is_quoted = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            p = e != NULL ? e + 2 : p + strlen(p);
            continue;
        }
        if (*p == '#') {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue;
        }
        break;
    }
    if (*p == '\0') {
        buf[0] = '\0';
        return NULL;
    }
    if (*p == '{' || *p == '}' || *p == ';' || *p == ':') {
        buf[0] = *p;
        buf[1] = '\0';
        return p + 1;
    }
    if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"') {
            if (n + 1 < cap) {
                buf[n++] = *p;
            }
            p++;
        }
        if (*p == '"') {
            p++;
        }
        buf[n] = '\0';
        *is_quoted = 1;
        return p;
    }
    /* (a word ends at a ':' -- "local:*;" -- but for a C++ name's "::") */
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '{' && *p != '}' &&
           *p != ';' && !(*p == ':' && p[1] != ':' && (n == 0 || buf[n - 1] != ':'))) {
        if (n + 1 < cap) {
            buf[n++] = *p;
        }
        p++;
    }
    buf[n] = '\0';
    return p;
}

/* A node of @p vs, added; NULL out of memory. */
static ld_version_node_t *vs_add_node(ld_version_script_t *vs, const char *name) {
    ld_version_node_t *nodes;

    nodes = (ld_version_node_t *)realloc(vs->nodes, (vs->count + 1) * sizeof(*nodes));
    if (nodes == NULL) {
        return NULL;
    }
    vs->nodes = nodes;
    memset(&nodes[vs->count], 0, sizeof(nodes[0]));
    if (name != NULL && (nodes[vs->count].name = strdup(name)) == NULL) {
        return NULL;
    }
    return &nodes[vs->count++];
}

int ld_version_script_parse(ld_version_script_t *vs, const char *text, const char *where) {
    char w[512];
    const char *p = text;
    int q;

    for (;;) {
        ld_version_node_t *node;
        int local = 0;
        int in_extern = 0;

        p = vs_word(p, w, sizeof(w), &q);
        if (p == NULL) {
            return 0;
        }
        if (strcmp(w, "{") == 0) {
            node = vs_add_node(vs, NULL);
        } else {
            node = vs_add_node(vs, w);
            p = vs_word(p, w, sizeof(w), &q);
            if (p == NULL || strcmp(w, "{") != 0) {
                fprintf(stderr, "ld: %s: version node '%s' has no '{'\n", where, node != NULL ? node->name : "?");
                return -1;
            }
        }
        if (node == NULL) {
            return -1;
        }
        /* the patterns, to the node's '}' */
        for (;;) {
            p = vs_word(p, w, sizeof(w), &q);
            if (p == NULL) {
                fprintf(stderr, "ld: %s: version node is not closed\n", where);
                return -1;
            }
            if (!q && strcmp(w, "}") == 0) {
                if (in_extern) {
                    in_extern = 0;  /* (the end of an extern "C++" { }) */
                    continue;
                }
                break;
            }
            if (!q && (strcmp(w, ";") == 0 || strcmp(w, ":") == 0)) {
                continue;
            }
            if (!q && (strcmp(w, "global:") == 0 || strcmp(w, "global") == 0)) {
                local = 0;
                continue;
            }
            if (!q && (strcmp(w, "local:") == 0 || strcmp(w, "local") == 0)) {
                local = 1;
                continue;
            }
            if (!q && strcmp(w, "extern") == 0) {
                /* extern "C++" { demangled names }: names this linker cannot match; read over */
                p = vs_word(p, w, sizeof(w), &q);
                p = p != NULL ? vs_word(p, w, sizeof(w), &q) : NULL;
                if (p == NULL || strcmp(w, "{") != 0) {
                    fprintf(stderr, "ld: %s: extern without '{' in a version node\n", where);
                    return -1;
                }
                while ((p = vs_word(p, w, sizeof(w), &q)) != NULL && !(strcmp(w, "}") == 0 && !q)) {
                }
                if (p == NULL) {
                    return -1;
                }
                continue;
            }
            if (strvec_push(local ? &node->locals : &node->globals, w) != 0) {
                return -1;
            }
        }
        /* the node it depends on, if any, and ';' */
        p = vs_word(p, w, sizeof(w), &q);
        if (p != NULL && strcmp(w, ";") != 0) {
            if ((node->parent = strdup(w)) == NULL) {
                return -1;
            }
            p = vs_word(p, w, sizeof(w), &q);
        }
        if (p == NULL) {
            return 0;
        }
    }
}

int ld_version_script_read(ld_version_script_t *vs, const char *path) {
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    size_t len = 0, cap = 0;
    int c;
    int rc;

    if (f == NULL) {
        fprintf(stderr, "ld: cannot read version script %s: %s\n", path, strerror(errno));
        return -1;
    }
    while ((c = fgetc(f)) != EOF) {
        if (len + 2 > cap) {
            char *next = (char *)realloc(text, cap == 0 ? 4096 : cap * 2);
            if (next == NULL) {
                free(text);
                fclose(f);
                return -1;
            }
            text = next;
            cap = cap == 0 ? 4096 : cap * 2;
        }
        text[len++] = (char)c;
    }
    fclose(f);
    if (text == NULL) {
        return 0;
    }
    text[len] = '\0';
    rc = ld_version_script_parse(vs, text, path);
    free(text);
    return rc;
}

void ld_version_script_free(ld_version_script_t *vs) {
    size_t i;

    for (i = 0; i < vs->count; ++i) {
        free(vs->nodes[i].name);
        free(vs->nodes[i].parent);
        strvec_free(&vs->nodes[i].globals);
        strvec_free(&vs->nodes[i].locals);
    }
    free(vs->nodes);
    memset(vs, 0, sizeof(*vs));
}

/* Whether @p pat is a pattern rather than a name. */
static int is_wild(const char *pat) {
    return strpbrk(pat, "*?[") != NULL;
}

/*
 * Which node's list names @p name, as GNU ld picks: an exact name, then a
 * pattern other than `*`, then `*`; a global list over a local one at each.
 * 1 with *@p node and *@p local, or 0 if none does.
 */
static int vs_match(const ld_version_script_t *vs, const char *name, size_t *node, int *local) {
    int pass;

    for (pass = 0; pass < 3; ++pass) {
        int side;

        for (side = 0; side < 2; ++side) {
            size_t i, k;

            for (i = 0; i < vs->count; ++i) {
                const strvec_t *list = side == 0 ? &vs->nodes[i].globals : &vs->nodes[i].locals;

                for (k = 0; k < list->count; ++k) {
                    const char *pat = list->items[k];
                    int hit;

                    if (pass == 0) {
                        hit = !is_wild(pat) && strcmp(pat, name) == 0;
                    } else if (pass == 1) {
                        hit = is_wild(pat) && strcmp(pat, "*") != 0 && fnmatch(pat, name, 0) == 0;
                    } else {
                        hit = strcmp(pat, "*") == 0;
                    }
                    if (hit) {
                        *node = i;
                        *local = side;
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

int apply_version_script(const ld_ctx_t *ctx, elfobj_t *out) {
    const ld_version_script_t *vs = &ctx->version_script;
    size_t i;
    int rc = 0;

    if (vs->count == 0) {
        return 0;
    }
    for (i = 0; i < elf_symbol_count(out); ++i) {
        elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;
        uint8_t bind;
        size_t node;
        int local;

        if (name == NULL || name[0] == '\0' || strchr(name, '@') != NULL || elf_symbol_shndx(sym) == SHN_UNDEF) {
            continue;   /* (one an object versioned itself keeps its version) */
        }
        bind = elf_symbol_bind(sym);
        if ((bind != STB_GLOBAL && bind != STB_WEAK) || !vs_match(vs, name, &node, &local)) {
            continue;
        }
        if (local) {
            /* not exported: a local of the output, as GNU ld makes it */
            if (elf_symbol_set_binding(sym, STB_LOCAL) != ELF_OK) {
                rc = -1;
                break;
            }
        } else if (vs->nodes[node].name != NULL &&
                   elf_symbol_set_version_name(sym, vs->nodes[node].name, 1) != ELF_OK) {
            rc = -1;
            break;
        }
    }
    /* each version is a symbol of its own name too, absolute, as GNU ld defines it (LINUX_2.6), whether a
     * symbol is given it or not */
    for (i = 0; rc == 0 && i < vs->count; ++i) {
        elf_symbol_t *vsym;

        if (vs->nodes[i].name == NULL || elf_find_symbol(out, vs->nodes[i].name) != NULL) {
            continue;
        }
        vsym = elf_add_symbol(out, vs->nodes[i].name, 0, 0, STB_GLOBAL, STT_OBJECT);
        if (vsym == NULL || elf_symbol_set_shndx(vsym, SHN_ABS) != ELF_OK ||
            elf_symbol_set_version_name(vsym, vs->nodes[i].name, 1) != ELF_OK) {
            rc = -1;
        }
    }
    return rc;
}

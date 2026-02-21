#include "ir.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void set_err(ir_error_t *err, size_t line, const char *msg) {
    if (err == NULL || err->msg != NULL) {
        return;
    }
    err->line = line;
    err->msg = xstrdup(msg);
}

static char *trim(char *s) {
    char *e;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        e--;
    }
    *e = '\0';
    return s;
}

static int vec_push_str(char ***arr, size_t *count, size_t *cap, const char *s) {
    char **next;

    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : (*cap * 2);
        next = (char **)realloc(*arr, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *arr = next;
        *cap = ncap;
    }

    (*arr)[*count] = xstrdup(s);
    if ((*arr)[*count] == NULL) {
        return -1;
    }

    (*count)++;
    return 0;
}

static int module_push_func(ir_module_t *m, ir_func_t **out) {
    ir_func_t *next;

    if (m->func_count == m->func_cap) {
        size_t ncap = m->func_cap == 0 ? 4 : m->func_cap * 2;
        next = (ir_func_t *)realloc(m->funcs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        m->funcs = next;
        m->func_cap = ncap;
    }

    memset(&m->funcs[m->func_count], 0, sizeof(m->funcs[m->func_count]));
    *out = &m->funcs[m->func_count++];
    return 0;
}

static int func_push_block(ir_func_t *f, ir_block_t **out) {
    ir_block_t *next;

    if (f->block_count == f->block_cap) {
        size_t ncap = f->block_cap == 0 ? 8 : f->block_cap * 2;
        next = (ir_block_t *)realloc(f->blocks, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->blocks = next;
        f->block_cap = ncap;
    }

    memset(&f->blocks[f->block_count], 0, sizeof(f->blocks[f->block_count]));
    *out = &f->blocks[f->block_count++];
    return 0;
}

static int block_push_instr(ir_block_t *b, ir_instr_t **out) {
    ir_instr_t *next;

    if (b->instr_count == b->instr_cap) {
        size_t ncap = b->instr_cap == 0 ? 16 : b->instr_cap * 2;
        next = (ir_instr_t *)realloc(b->instrs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        b->instrs = next;
        b->instr_cap = ncap;
    }

    memset(&b->instrs[b->instr_count], 0, sizeof(b->instrs[b->instr_count]));
    *out = &b->instrs[b->instr_count++];
    return 0;
}

static int instr_push_use(ir_instr_t *in, const char *u) {
    ir_value_use_t *next;

    if (in->use_count == in->use_cap) {
        size_t ncap = in->use_cap == 0 ? 8 : in->use_cap * 2;
        next = (ir_value_use_t *)realloc(in->uses, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        in->uses = next;
        in->use_cap = ncap;
    }

    in->uses[in->use_count].name = xstrdup(u);
    if (in->uses[in->use_count].name == NULL) {
        return -1;
    }
    in->use_count++;
    return 0;
}

static int parse_target(ir_module_t *m, char *line) {
    char *p = line + 6;

    p = trim(p);
    if (m->target != NULL) {
        free(m->target);
    }
    m->target = xstrdup(p);
    return m->target == NULL ? -1 : 0;
}

static int parse_func_header(ir_module_t *m, char *line, ir_func_t **out) {
    char *at;
    char *name_end;
    ir_func_t *f;

    at = strchr(line, '@');
    if (at == NULL) {
        return -1;
    }

    name_end = at;
    while (*name_end != '\0' && !isspace((unsigned char)*name_end) && *name_end != ':') {
        name_end++;
    }
    *name_end = '\0';

    if (module_push_func(m, &f) != 0) {
        return -1;
    }

    f->name = xstrdup(at + 1);
    if (f->name == NULL) {
        return -1;
    }

    {
        char *args_start = strchr(name_end + 1, '(');
        char *args_end = args_start ? strchr(args_start, ')') : NULL;

        if (args_start != NULL && args_end != NULL) {
            char *p;
            *args_end = '\0';
            p = trim(args_start + 1);

            while (*p != '\0') {
                char *comma = strchr(p, ',');
                if (comma != NULL) {
                    *comma = '\0';
                }
                p = trim(p);
                {
                    char *pct = strchr(p, '%');
                    if (pct != NULL) {
                        char *e = pct + 1;
                        while (*e != '\0' && (isalnum((unsigned char)*e) || *e == '_' || *e == '.')) {
                            e++;
                        }
                        *e = '\0';
                        if (vec_push_str(&f->args, &f->arg_count, &f->arg_cap, pct + 1) != 0) {
                            return -1;
                        }
                    }
                }
                if (comma == NULL) {
                    break;
                }
                p = comma + 1;
            }
        }
    }

    *out = f;
    return 0;
}

static int parse_label(ir_func_t *f, char *line, ir_block_t **out, size_t line_no) {
    char *colon;
    ir_block_t *b;

    colon = strrchr(line, ':');
    if (colon == NULL) {
        return -1;
    }
    *colon = '\0';

    if (func_push_block(f, &b) != 0) {
        return -1;
    }

    b->name = xstrdup(trim(line));
    if (b->name == NULL) {
        return -1;
    }
    b->line = line_no;
    *out = b;
    return 0;
}

static int is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.';
}

static void clean_label(char *s) {
    char *p = trim(s);
    size_t n = strlen(p);

    while (n > 0 && (p[n - 1] == ';' || p[n - 1] == ',' || isspace((unsigned char)p[n - 1]))) {
        p[n - 1] = '\0';
        n--;
    }

    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static int parse_instr(ir_block_t *b, char *line, size_t line_no) {
    ir_instr_t *in;
    char *rhs;
    char *op;

    if (block_push_instr(b, &in) != 0) {
        return -1;
    }

    in->text = xstrdup(line);
    if (in->text == NULL) {
        return -1;
    }
    in->line = line_no;

    rhs = line;
    if (line[0] == '%') {
        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            in->def = xstrdup(trim(line) + 1);
            if (in->def == NULL) {
                return -1;
            }
            rhs = trim(eq + 1);
        }
    }

    op = rhs;
    while (*op != '\0' && isspace((unsigned char)*op)) {
        op++;
    }
    {
        char *e = op;
        while (*e != '\0' && !isspace((unsigned char)*e) && *e != ';') {
            e++;
        }
        {
            char c = *e;
            *e = '\0';
            in->opcode = xstrdup(op);
            *e = c;
            if (in->opcode == NULL) {
                return -1;
            }
        }
    }

    in->is_terminator = strcmp(in->opcode, "br") == 0 || strcmp(in->opcode, "br_cond") == 0 ||
                        strcmp(in->opcode, "ret") == 0 || strcmp(in->opcode, "switch") == 0 ||
                        strcmp(in->opcode, "unreachable") == 0;
    in->is_phi = strcmp(in->opcode, "phi") == 0;

    {
        char *p = rhs;
        while ((p = strchr(p, '%')) != NULL) {
            char tmp[256];
            size_t n = 0;

            p++;
            while (is_ident_char((unsigned char)p[n]) && n + 1 < sizeof(tmp)) {
                tmp[n] = p[n];
                n++;
            }
            tmp[n] = '\0';
            if (n > 0 && (in->def == NULL || strcmp(tmp, in->def) != 0)) {
                if (instr_push_use(in, tmp) != 0) {
                    return -1;
                }
            }
            p += n;
        }
    }

    if (in->is_phi) {
        char *p = rhs;
        while ((p = strchr(p, '[')) != NULL) {
            in->phi_incoming_count++;
            p++;
        }
    }

    if (in->is_terminator) {
        if (strcmp(in->opcode, "br") == 0) {
            char *p = rhs;
            p += 2;
            p = trim(p);
            if (*p != '\0') {
                char label[256];
                size_t i = 0;
                while (p[i] != '\0' && !isspace((unsigned char)p[i]) && i + 1 < sizeof(label)) {
                    label[i] = p[i];
                    i++;
                }
                label[i] = '\0';
                clean_label(label);
                if (label[0] != '\0' && vec_push_str(&b->succs, &b->succ_count, &b->succ_cap, label) != 0) {
                    return -1;
                }
            }
        } else if (strcmp(in->opcode, "br_cond") == 0) {
            char *c1 = strchr(rhs, ',');
            if (c1 != NULL) {
                char *t = trim(c1 + 1);
                char *c2 = strchr(t, ',');
                if (c2 != NULL) {
                    char label1[256];
                    char label2[256];
                    *c2 = '\0';
                    snprintf(label1, sizeof(label1), "%s", trim(t));
                    snprintf(label2, sizeof(label2), "%s", trim(c2 + 1));
                    clean_label(label1);
                    clean_label(label2);
                    if (label1[0] != '\0' && vec_push_str(&b->succs, &b->succ_count, &b->succ_cap, label1) != 0) {
                        return -1;
                    }
                    if (label2[0] != '\0' && vec_push_str(&b->succs, &b->succ_count, &b->succ_cap, label2) != 0) {
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

static ir_block_t *find_block(ir_func_t *f, const char *name) {
    size_t i;
    for (i = 0; i < f->block_count; ++i) {
        if (strcmp(f->blocks[i].name, name) == 0) {
            return &f->blocks[i];
        }
    }
    return NULL;
}

static int wire_preds(ir_func_t *f) {
    size_t i;
    size_t j;

    for (i = 0; i < f->block_count; ++i) {
        ir_block_t *b = &f->blocks[i];

        for (j = 0; j < b->succ_count; ++j) {
            ir_block_t *succ = find_block(f, b->succs[j]);
            if (succ != NULL) {
                if (vec_push_str(&succ->preds, &succ->pred_count, &succ->pred_cap, b->name) != 0) {
                    return -1;
                }
            }
        }
    }

    return 0;
}

int ir_parse_file(const char *path, ir_module_t *out, ir_error_t *err) {
    FILE *fp;
    char buf[4096];
    size_t line = 0;
    ir_func_t *curf = NULL;
    ir_block_t *curb = NULL;

    fp = fopen(path, "r");
    if (fp == NULL) {
        set_err(err, 0, "cannot open IR file");
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        char *p;
        char *comment;
        line++;

        comment = strchr(buf, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        p = trim(buf);
        if (*p == '\0') {
            continue;
        }

        if (strncmp(p, "module", 6) == 0) {
            char *q = strchr(p, '"');
            if (q != NULL) {
                char *e = strchr(q + 1, '"');
                if (e != NULL) {
                    *e = '\0';
                    free(out->name);
                    out->name = xstrdup(q + 1);
                }
            }
            continue;
        }

        if (strncmp(p, "target", 6) == 0) {
            if (parse_target(out, p) != 0) {
                set_err(err, line, "failed parsing target");
                fclose(fp);
                return -1;
            }
            continue;
        }

        if (strncmp(p, "func", 4) == 0) {
            if (parse_func_header(out, p, &curf) != 0) {
                set_err(err, line, "failed parsing function header");
                fclose(fp);
                return -1;
            }
            curb = NULL;
            continue;
        }

        if (strcmp(p, "}") == 0 || strcmp(p, "};") == 0) {
            curb = NULL;
            curf = NULL;
            continue;
        }

        if (curf != NULL && strchr(p, ':') != NULL && p[0] != '%') {
            if (parse_label(curf, p, &curb, line) != 0) {
                set_err(err, line, "failed parsing block label");
                fclose(fp);
                return -1;
            }
            continue;
        }

        if (curb != NULL) {
            if (parse_instr(curb, p, line) != 0) {
                set_err(err, line, "failed parsing instruction");
                fclose(fp);
                return -1;
            }
            continue;
        }
    }

    fclose(fp);

    {
        size_t i;
        for (i = 0; i < out->func_count; ++i) {
            if (wire_preds(&out->funcs[i]) != 0) {
                set_err(err, line, "failed wiring CFG predecessors");
                return -1;
            }
        }
    }

    return 0;
}

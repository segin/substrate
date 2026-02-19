/*
 * regex.c - Thompson NFA construction for lex
 * 
 * Implements ERE parsing and NFA generation using Thompson's algorithm.
 * The NFA is later converted to DFA for the generated scanner.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "regex.h"
#include "symtab.h"

static struct rule *rules = NULL;
static int next_rule_id = 1;

/* State allocation */
struct nfa_state *nfa_state_create(int c) {
    struct nfa_state *s = malloc(sizeof(struct nfa_state));
    if (!s) {
        perror("malloc");
        exit(1);
    }
    s->c = c;
    s->out1 = NULL;
    s->out2 = NULL;
    s->accept = 0;
    return s;
}



/* Parser state */
static const char *re_pos;
static int re_rule_id;

/* NFA copying for intervals */
struct nfa_copy_map {
    struct nfa_state *old_st;
    struct nfa_state *new_st;
    struct nfa_copy_map *next;
};

static struct nfa_state *nfa_state_copy(struct nfa_state *s, struct nfa_copy_map **map) {
    if (!s) return NULL;
    struct nfa_copy_map *m = *map;
    while (m) {
        if (m->old_st == s) return m->new_st;
        m = m->next;
    }
    struct nfa_state *ns = nfa_state_create(s->c);
    ns->accept = s->accept;
    m = malloc(sizeof(struct nfa_copy_map));
    m->old_st = s;
    m->new_st = ns;
    m->next = *map;
    *map = m;
    ns->out1 = nfa_state_copy(s->out1, map);
    ns->out2 = nfa_state_copy(s->out2, map);
    return ns;
}

static struct nfa_frag *copy_frag(struct nfa_frag *f) {
    struct nfa_copy_map *map = NULL;
    struct nfa_state *new_start = nfa_state_copy(f->start, &map);
    struct nfa_frag *nf = malloc(sizeof(struct nfa_frag));
    nf->start = new_start;
    nf->out_count = f->out_count;
    nf->out = malloc(nf->out_count * sizeof(struct nfa_state **));
    for (int i = 0; i < f->out_count; i++) {
        /* Find which state and which field (out1/out2) this points to */
        struct nfa_state **old_ptr = f->out[i];
        /* We need to find the old state that has this field */
        /* This is inefficient. Better to store in fragment? */
        /* Actually, in Thompson's, f->out only contains dangling pointers.
         * We can iterate over all new states in the map and find the one that corresponds
         * to the state owning the old_ptr.
         */
        struct nfa_copy_map *m = map;
        while (m) {
            if (&m->old_st->out1 == old_ptr) {
                nf->out[i] = &m->new_st->out1;
                break;
            }
            if (&m->old_st->out2 == old_ptr) {
                nf->out[i] = &m->new_st->out2;
                break;
            }
            m = m->next;
        }
    }
    /* Clean up map */
    while (map) {
        struct nfa_copy_map *next = map->next;
        free(map);
        map = next;
    }
    return nf;
}

static struct nfa_frag *parse_expr(void);
static struct nfa_frag *parse_term(void);
static struct nfa_frag *parse_factor(void);
static struct nfa_frag *parse_atom(void);

/* Create a fragment for a single character */
static struct nfa_frag *make_char_frag(int c) {
    struct nfa_frag *f = malloc(sizeof(struct nfa_frag));
    struct nfa_state *s = nfa_state_create(c);
    f->start = s;
    f->out = malloc(sizeof(struct nfa_state **));
    f->out[0] = &s->out1;
    f->out_count = 1;
    return f;
}

/* Concatenate two fragments */
static struct nfa_frag *concat_frag(struct nfa_frag *f1, struct nfa_frag *f2) {
    /* Patch f1's out to f2's start */
    for (int i = 0; i < f1->out_count; i++) {
        *(f1->out[i]) = f2->start;
    }
    free(f1->out);
    f1->out = f2->out;
    f1->out_count = f2->out_count;
    free(f2);
    return f1;
}

/* Alternation of two fragments */
static struct nfa_frag *alt_frag(struct nfa_frag *f1, struct nfa_frag *f2) {
    struct nfa_state *s = nfa_state_create(EPSILON);
    s->out1 = f1->start;
    s->out2 = f2->start;
    
    struct nfa_frag *f = malloc(sizeof(struct nfa_frag));
    f->start = s;
    /* Merge out lists */
    f->out_count = f1->out_count + f2->out_count;
    f->out = malloc(f->out_count * sizeof(struct nfa_state **));
    memcpy(f->out, f1->out, f1->out_count * sizeof(struct nfa_state **));
    memcpy(f->out + f1->out_count, f2->out, f2->out_count * sizeof(struct nfa_state **));
    
    free(f1->out);
    free(f2->out);
    free(f1);
    free(f2);
    return f;
}

/* Kleene star (zero or more) */
static struct nfa_frag *star_frag(struct nfa_frag *f) {
    struct nfa_state *s = nfa_state_create(EPSILON);
    s->out1 = f->start;
    
    /* Patch f's out back to s */
    for (int i = 0; i < f->out_count; i++) {
        *(f->out[i]) = s;
    }
    
    struct nfa_frag *nf = malloc(sizeof(struct nfa_frag));
    nf->start = s;
    nf->out = malloc(sizeof(struct nfa_state **));
    nf->out[0] = &s->out2;
    nf->out_count = 1;
    
    free(f->out);
    free(f);
    return nf;
}

/* Plus (one or more) */
static struct nfa_frag *plus_frag(struct nfa_frag *f) {
    struct nfa_state *s = nfa_state_create(EPSILON);
    s->out1 = f->start;
    
    for (int i = 0; i < f->out_count; i++) {
        *(f->out[i]) = s;
    }
    
    /* Start from original, not the split */
    struct nfa_frag *nf = malloc(sizeof(struct nfa_frag));
    nf->start = f->start;
    nf->out = malloc(sizeof(struct nfa_state **));
    nf->out[0] = &s->out2;
    nf->out_count = 1;
    
    free(f->out);
    free(f);
    return nf;
}

/* Question (zero or one) */
static struct nfa_frag *quest_frag(struct nfa_frag *f) {
    struct nfa_state *s = nfa_state_create(EPSILON);
    s->out1 = f->start;
    
    struct nfa_frag *nf = malloc(sizeof(struct nfa_frag));
    nf->start = s;
    nf->out_count = f->out_count + 1;
    nf->out = malloc(nf->out_count * sizeof(struct nfa_state **));
    memcpy(nf->out, f->out, f->out_count * sizeof(struct nfa_state **));
    nf->out[f->out_count] = &s->out2;
    
    free(f->out);
    free(f);
    return nf;
}

/* Parse trailing context: ere / context */
static struct nfa_frag *parse_trailing(void) {
    struct nfa_frag *f = parse_expr();
    if (!f) return NULL;
    
    if (*re_pos == '/') {
        re_pos++;
        struct nfa_frag *f2 = parse_expr();
        if (f2) {
            /* For now, just concatenate. We'll need to handle the split point in the DFA.
             * POSIX: r/s matches rs, but returns match for r.
             */
            f = concat_frag(f, f2);
        }
    }
    return f;
}

/* Parse escape sequence */
static int parse_escape(void) {
    if (*re_pos != '\\') return -1;
    re_pos++;
    if (*re_pos == '\0') return '\\';
    
    switch (*re_pos) {
        case 'n': re_pos++; return '\n';
        case 't': re_pos++; return '\t';
        case 'r': re_pos++; return '\r';
        case 'a': re_pos++; return '\a';
        case 'b': re_pos++; return '\b';
        case 'f': re_pos++; return '\f';
        case 'v': re_pos++; return '\v';
        case '\\': re_pos++; return '\\';
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            /* Octal */
            int val = 0;
            for (int i = 0; i < 3 && *re_pos >= '0' && *re_pos <= '7'; i++) {
                val = val * 8 + (*re_pos++ - '0');
            }
            return val;
        }
        case 'x': {
            /* Hex */
            re_pos++;
            int val = 0;
            while (isxdigit((unsigned char)*re_pos)) {
                int d = *re_pos++;
                if (d >= 'a') d = d - 'a' + 10;
                else if (d >= 'A') d = d - 'A' + 10;
                else d = d - '0';
                val = val * 16 + d;
            }
            return val;
        }
        case 'c': {
            /* \cX for control-X */
            re_pos++;
            if (*re_pos) {
                int c = *re_pos++;
                return c & 0x1F;
            }
            return 'c';
        }
        default:
            /* Literal escape \X -> X */
            return *re_pos++;
    }
}

/* Parse character class [abc] or [^abc] */
static struct nfa_frag *parse_class(void) {
    re_pos++; /* skip [ */
    bool negate = false;
    if (*re_pos == '^') {
        negate = true;
        re_pos++;
    }
    
    /* Build bitmap of characters in class */
    bool in_class[256] = {false};
    
    while (*re_pos && *re_pos != ']') {
        int c1;
        if (*re_pos == '\\') {
            c1 = parse_escape();
        } else {
            c1 = (unsigned char)*re_pos++;
        }
        
        if (*re_pos == '-' && re_pos[1] != ']') {
            re_pos++; /* skip - */
            int c2;
            if (*re_pos == '\\') {
                c2 = parse_escape();
            } else {
                c2 = (unsigned char)*re_pos++;
            }
            for (int c = c1; c <= c2; c++) {
                in_class[c] = true;
            }
        } else {
            in_class[c1] = true;
        }
    }
    
    if (*re_pos == ']') re_pos++;
    
    if (negate) {
        for (int i = 0; i < 256; i++) {
            in_class[i] = !in_class[i];
        }
        in_class[0] = false; /* Don't match NUL */
    }
    
    /* Build alternation of all matching characters */
    struct nfa_frag *result = NULL;
    for (int i = 1; i < 256; i++) {
        if (in_class[i]) {
            struct nfa_frag *cf = make_char_frag(i);
            if (result == NULL) {
                result = cf;
            } else {
                result = alt_frag(result, cf);
            }
        }
    }
    
    if (result == NULL) {
        /* Empty class - create a never-matching state? */
        result = make_char_frag(-999); /* Will never match */
    }
    
    return result;
}

/* Parse quoted string "..." */
static struct nfa_frag *parse_quoted(void) {
    re_pos++; /* skip " */
    struct nfa_frag *result = NULL;
    
    while (*re_pos && *re_pos != '"') {
        int c;
        if (*re_pos == '\\') {
            c = parse_escape();
        } else {
            c = (unsigned char)*re_pos++;
        }
        struct nfa_frag *cf = make_char_frag(c);
        if (result == NULL) {
            result = cf;
        } else {
            result = concat_frag(result, cf);
        }
    }
    
    if (*re_pos == '"') re_pos++;
    
    if (result == NULL) {
        /* Empty string - epsilon */
        struct nfa_state *s = nfa_state_create(EPSILON);
        result = malloc(sizeof(struct nfa_frag));
        result->start = s;
        result->out = malloc(sizeof(struct nfa_state **));
        result->out[0] = &s->out1;
        result->out_count = 1;
    }
    
    return result;
}

/* Parse {name} substitution */
static struct nfa_frag *parse_subst(void) {
    re_pos++; /* skip { */
    const char *name_start = re_pos;
    while (*re_pos && *re_pos != '}') re_pos++;
    
    int len = re_pos - name_start;
    char *name = malloc(len + 1);
    memcpy(name, name_start, len);
    name[len] = '\0';
    
    if (*re_pos == '}') re_pos++;
    
    struct definition *def = find_definition(name);
    
    if (!def) {
        fprintf(stderr, "Error: undefined name {%s}\n", name);
        free(name);
        exit(1);
    }
    
    free(name);
    
    /* Recursively compile the definition value */
    const char *saved = re_pos;
    re_pos = def->value;
    struct nfa_frag *f = parse_expr();
    re_pos = saved;
    
    return f;
}

/* Atom: single char, ., [], "", {name}, or (expr) */
static struct nfa_frag *parse_atom(void) {
    if (*re_pos == '(') {
        re_pos++;
        struct nfa_frag *f = parse_expr();
        if (*re_pos == ')') re_pos++;
        return f;
    }
    if (*re_pos == '[') {
        return parse_class();
    }
    if (*re_pos == '"') {
        return parse_quoted();
    }
    if (*re_pos == '{') {
        return parse_subst();
    }
    if (*re_pos == '.') {
        re_pos++;
        return make_char_frag(ANY_CHAR);
    }
    if (*re_pos == '\\') {
        int c = parse_escape();
        return make_char_frag(c);
    }
    /* Regular character */
    if (*re_pos && !strchr("*+?|()[]{}\"\\^$/", *re_pos)) {
        return make_char_frag((unsigned char)*re_pos++);
    }
    return NULL;
}

static struct nfa_frag *parse_interval(struct nfa_frag *f) {
    if (*re_pos != '{') return f;
    re_pos++;
    
    int m = 0;
    while (isdigit((unsigned char)*re_pos)) m = m * 10 + (*re_pos++ - '0');
    
    int n = -1; /* -1 = m exactly, -2 = m or more */
    if (*re_pos == ',') {
        re_pos++;
        if (isdigit((unsigned char)*re_pos)) {
            n = 0;
            while (isdigit((unsigned char)*re_pos)) n = n * 10 + (*re_pos++ - '0');
        } else {
            n = -2; /* m or more */
        }
    }
    
    if (*re_pos != '}') {
        fprintf(stderr, "Error: expected } in interval quantifier\n");
        exit(1);
    }
    re_pos++;
    
    struct nfa_frag *res = NULL;
    if (m == 0) {
        res = make_char_frag(EPSILON);
    } else {
        /* Use copies for all repetitions so f remains a clean template */
        res = copy_frag(f);
        for (int i = 1; i < m; i++) {
            res = concat_frag(res, copy_frag(f));
        }
    }
    
    if (n == -1) {
        /* exactly m - done */
    } else if (n == -2) {
        /* m or more */
        res = concat_frag(res, star_frag(copy_frag(f)));
    } else {
        /* between m and n */
        for (int i = m; i < n; i++) {
            res = concat_frag(res, quest_frag(copy_frag(f)));
        }
    }
    
    free(f->out);
    free(f);

    return res;
}

/* Factor: atom with quantifiers */
static struct nfa_frag *parse_factor(void) {
    struct nfa_frag *f = parse_atom();
    if (!f) return NULL;
    
    while (*re_pos == '*' || *re_pos == '+' || *re_pos == '?' || *re_pos == '{') {
        if (*re_pos == '{') {
            f = parse_interval(f);
        } else {
            switch (*re_pos++) {
                case '*': f = star_frag(f); break;
                case '+': f = plus_frag(f); break;
                case '?': f = quest_frag(f); break;
            }
        }
    }
    
    return f;
}

/* Term: concatenation of factors */
static struct nfa_frag *parse_term(void) {
    struct nfa_frag *f = parse_factor();
    if (!f) return NULL;
    
    while (*re_pos && !strchr("|)/", *re_pos)) {
        struct nfa_frag *f2 = parse_factor();
        if (!f2) break;
        f = concat_frag(f, f2);
    }
    
    return f;
}

/* Expr: alternation of terms */
static struct nfa_frag *parse_expr(void) {
    struct nfa_frag *f = parse_term();
    if (!f) return NULL;
    
    while (*re_pos == '|') {
        re_pos++;
        struct nfa_frag *f2 = parse_term();
        if (f2) {
            f = alt_frag(f, f2);
        }
    }
    
    return f;
}

/* Compile pattern to NFA */
struct nfa_frag *regex_compile(const char *pattern, int rule_id) {
    re_pos = pattern;
    re_rule_id = rule_id;
    
    struct nfa_frag *f = parse_trailing();
    if (!f) {
        fprintf(stderr, "Error: failed to compile pattern: %s\n", pattern);
        exit(1);
    }
    
    /* Add accepting state */
    struct nfa_state *accept = nfa_state_create(EPSILON);
    accept->accept = rule_id;
    
    for (int i = 0; i < f->out_count; i++) {
        *(f->out[i]) = accept;
    }
    
    return f;
}

/* Add a rule */
void add_rule(const char *pattern, const char *action, char **start_conds, int sc_count) {
    struct rule *r = malloc(sizeof(struct rule));
    r->id = next_rule_id++;
    r->pattern = strdup(pattern);
    r->action = action ? strdup(action) : NULL;
    r->start_conditions = malloc(sc_count * sizeof(char *));
    for (int i = 0; i < sc_count; i++) {
        r->start_conditions[i] = strdup(start_conds[i]);
    }
    r->sc_count = sc_count;
    
    /* Anchors */
    r->has_bol = (pattern[0] == '^');
    
    /* Handle $ and / context */
    const char *p = pattern;
    if (r->has_bol) p++;
    
    /* Simple $ handling: replace with /\n */
    char *p_modified = strdup(p);
    int p_len = strlen(p_modified);
    r->has_eol = false;
    if (p_len > 0 && p_modified[p_len-1] == '$') {
        /* Check if not escaped */
        if (p_len == 1 || p_modified[p_len-2] != '\\') {
            r->has_eol = true;
            char *new_p = malloc(p_len + 2);
            memcpy(new_p, p_modified, p_len - 1);
            new_p[p_len-1] = '/';
            new_p[p_len] = '\n';
            new_p[p_len+1] = '\0';
            free(p_modified);
            p_modified = new_p;
        }
    }
    
    struct nfa_frag *frag = regex_compile(p_modified, r->id);
    r->nfa = frag->start;
    free(frag->out);
    free(frag);
    free(p_modified);
    
    r->next = rules;
    rules = r;
}

struct rule *get_rules(void) {
    return rules;
}

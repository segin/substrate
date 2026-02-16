#include <stdlib.h>
#include <string.h>

#include "regex_internal.h"

typedef enum {
    NODE_EMPTY,
    NODE_LITERAL,
    NODE_DOT,
    NODE_CLASS,
    NODE_CONCAT,
    NODE_ALT,
    NODE_STAR,
    NODE_PLUS,
    NODE_QMARK,
    NODE_REPEAT,
    NODE_GROUP,
    NODE_BOL,
    NODE_EOL
} node_type_t;

typedef struct regex_node {
    node_type_t type;
    struct regex_node *left;
    struct regex_node *right;
    uint32_t literal;
    size_t group_id;
    size_t rep_min;
    size_t rep_max; /* SIZE_MAX for unbounded */
    struct regex_charclass *charclass;
} regex_node;

typedef struct regex_range {
    uint32_t lo;
    uint32_t hi;
} regex_range;

typedef struct regex_charclass {
    int negated;
    int byte_mode;
    size_t range_count;
    size_t range_cap;
    regex_range *ranges;
    uint8_t bitmap[32];
} regex_charclass;

typedef enum {
    NFA_EPSILON,
    NFA_CHAR,
    NFA_DOT,
    NFA_CLASS,
    NFA_SPLIT,
    NFA_MATCH,
    NFA_SAVE,
    NFA_BOL,
    NFA_EOL
} nfa_type_t;

typedef struct nfa_state {
    nfa_type_t type;
    int id;
    int last_list_id;
    uint32_t ch;
    regex_charclass *charclass;
    int save_slot;
    struct nfa_state *out;
    struct nfa_state *out1;
} nfa_state;

typedef struct ptrlist {
    nfa_state **outp;
    struct ptrlist *next;
} ptrlist;

typedef struct frag {
    nfa_state *start;
    ptrlist *out;
} frag;

typedef struct nfa_prog {
    nfa_state **states;
    size_t state_count;
    size_t state_cap;
    nfa_state *start;
    size_t capture_count;
    int uses_bol;
    int uses_eol;
} nfa_prog;

typedef struct dfa_trans {
    uint32_t cp;
    int target;
} dfa_trans;

typedef struct dfa_state {
    uint8_t *nfa_set;
    size_t nfa_set_len;
    int is_accept;
    dfa_trans *trans;
    size_t trans_count;
    size_t trans_cap;
} dfa_state;

typedef struct dfa_prog {
    dfa_state *states;
    size_t state_count;
    size_t state_cap;
    int start_state;
    size_t max_states;
} dfa_prog;

typedef struct safe_regex {
    nfa_prog *nfa;
    dfa_prog *dfa;
    size_t capture_count;
    unsigned flags;
} safe_regex;

typedef struct thread {
    nfa_state *s;
    size_t *caps;
} thread;

typedef struct thread_list {
    thread *threads;
    size_t count;
    size_t cap;
} thread_list;

typedef struct match_record {
    size_t start;
    size_t end;
    size_t cap_count;
    size_t *caps;
} match_record;

typedef struct match_queue {
    match_record *items;
    size_t head;
    size_t tail;
    size_t count;
    size_t cap;
} match_queue;

typedef struct regex_iter_safe {
    regex_iter_t base;
    const regex_t *re;
    char *buffer;
    size_t buf_len;
    size_t buf_cap;
    size_t scan_pos;
    size_t base_offset;
    unsigned options;
    regex_err_t last_err;
    match_queue queue;
    int finished;
    uint8_t *scratch_set;
    size_t scratch_cap;
} regex_iter_safe;

typedef struct parser {
    const char *pattern;
    size_t len;
    size_t pos;
    unsigned flags;
    int extended;
    int utf8;
    size_t capture_count;
    regex_err_t err;
} parser;

static regex_node *node_new(node_type_t type) {
    regex_node *n = (regex_node *)calloc(1, sizeof(*n));
    if (!n) {
        return NULL;
    }
    n->type = type;
    return n;
}

static void node_free(regex_node *n) {
    if (!n) {
        return;
    }
    node_free(n->left);
    node_free(n->right);
    if (n->charclass) {
        free(n->charclass->ranges);
        free(n->charclass);
    }
    free(n);
}

static int parser_at_end(parser *p) {
    return p->pos >= p->len;
}

static uint8_t parser_peek(parser *p) {
    if (parser_at_end(p)) {
        return 0;
    }
    return (uint8_t)p->pattern[p->pos];
}

static uint8_t parser_get(parser *p) {
    if (parser_at_end(p)) {
        return 0;
    }
    return (uint8_t)p->pattern[p->pos++];
}

static int parser_read_hex(parser *p, size_t digits, uint32_t *out) {
    size_t i;
    uint32_t v = 0;

    if (p->pos + digits > p->len) {
        return 0;
    }

    for (i = 0; i < digits; ++i) {
        uint8_t c = (uint8_t)p->pattern[p->pos + i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (uint32_t)(c - 'A' + 10);
        } else {
            return 0;
        }
    }

    p->pos += digits;
    *out = v;
    return 1;
}

static int parser_read_codepoint(parser *p, uint32_t *out_cp) {
    if (parser_at_end(p)) {
        return 0;
    }

    if (p->utf8) {
        size_t idx = p->pos;
        if (!regex_utf8_decode(p->pattern, p->len, &idx, out_cp)) {
            p->err = REGEX_ERR_SYNTAX;
            return 0;
        }
        p->pos = idx;
        return 1;
    }

    *out_cp = (uint8_t)p->pattern[p->pos++];
    return 1;
}

static regex_charclass *charclass_new(int byte_mode) {
    regex_charclass *cc = (regex_charclass *)calloc(1, sizeof(*cc));
    if (!cc) {
        return NULL;
    }
    cc->byte_mode = byte_mode;
    return cc;
}

static int charclass_add_range(regex_charclass *cc, uint32_t lo, uint32_t hi) {
    regex_range *ranges;

    if (!cc) {
        return 0;
    }

    if (cc->byte_mode) {
        uint32_t i;
        if (lo > 0xFF || hi > 0xFF) {
            return 0;
        }
        for (i = lo; i <= hi; ++i) {
            cc->bitmap[i >> 3] |= (uint8_t)(1u << (i & 7u));
        }
        return 1;
    }

    if (cc->range_count == cc->range_cap) {
        size_t new_cap = cc->range_cap ? cc->range_cap * 2 : 8;
        ranges = (regex_range *)realloc(cc->ranges, new_cap * sizeof(*ranges));
        if (!ranges) {
            return 0;
        }
        cc->ranges = ranges;
        cc->range_cap = new_cap;
    }

    cc->ranges[cc->range_count].lo = lo;
    cc->ranges[cc->range_count].hi = hi;
    cc->range_count++;
    return 1;
}

static int charclass_match_raw(const regex_charclass *cc, uint32_t cp) {
    size_t i;
    if (!cc) {
        return 0;
    }
    if (cc->byte_mode) {
        if (cp > 0xFF) {
            return 0;
        }
        return (cc->bitmap[cp >> 3] & (uint8_t)(1u << (cp & 7u))) != 0;
    }
    for (i = 0; i < cc->range_count; ++i) {
        if (cp >= cc->ranges[i].lo && cp <= cc->ranges[i].hi) {
            return 1;
        }
    }
    return 0;
}

static int charclass_match(const regex_charclass *cc, uint32_t cp, int icase, int utf8) {
    int matched;
    uint32_t alt;

    matched = charclass_match_raw(cc, cp);
    if (matched) {
        return cc->negated ? 0 : 1;
    }

    if (icase) {
        if (utf8) {
            alt = regex_unicode_tolower(cp);
            if (alt != cp && charclass_match_raw(cc, alt)) {
                return cc->negated ? 0 : 1;
            }
            alt = regex_unicode_toupper(cp);
            if (alt != cp && charclass_match_raw(cc, alt)) {
                return cc->negated ? 0 : 1;
            }
        }
        if (cp <= 0x7F) {
            alt = regex_ascii_tolower(cp);
            if (alt != cp && charclass_match_raw(cc, alt)) {
                return cc->negated ? 0 : 1;
            }
            alt = regex_ascii_toupper(cp);
            if (alt != cp && charclass_match_raw(cc, alt)) {
                return cc->negated ? 0 : 1;
            }
        }
    }

    return cc->negated ? 1 : 0;
}

static regex_node *parse_regex(parser *p);

static regex_node *parse_atom(parser *p) {
    regex_node *n = NULL;
    uint8_t c;
    uint32_t cp;

    if (parser_at_end(p)) {
        return NULL;
    }

    c = parser_peek(p);

    if (p->extended && c == '(') {
        parser_get(p);
        n = parse_regex(p);
        if (!n) {
            return NULL;
        }
        if (parser_at_end(p) || parser_get(p) != ')') {
            node_free(n);
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
        }
        {
            regex_node *g = node_new(NODE_GROUP);
            if (!g) {
                node_free(n);
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            g->left = n;
            g->group_id = ++p->capture_count;
            return g;
        }
    }

    if (!p->extended && c == '\\') {
        size_t save = p->pos;
        parser_get(p);
        if (!parser_at_end(p) && parser_peek(p) == '(') {
            parser_get(p);
            n = parse_regex(p);
            if (!n) {
                return NULL;
            }
            if (parser_at_end(p) || parser_get(p) != '\\' || parser_get(p) != ')') {
                node_free(n);
                p->err = REGEX_ERR_SYNTAX;
                return NULL;
            }
            {
                regex_node *g = node_new(NODE_GROUP);
                if (!g) {
                    node_free(n);
                    p->err = REGEX_ERR_NOMEM;
                    return NULL;
                }
                g->left = n;
                g->group_id = ++p->capture_count;
                return g;
            }
        }
        p->pos = save;
    }

    if (c == '[') {
        regex_charclass *cc;
        int negate = 0;
        uint32_t lo;
        uint32_t hi;
        uint8_t next;

        parser_get(p);
        if (parser_at_end(p)) {
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
        }

        if (parser_peek(p) == '^') {
            negate = 1;
            parser_get(p);
        }

        cc = charclass_new(!p->utf8);
        if (!cc) {
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        cc->negated = negate;

        while (!parser_at_end(p) && parser_peek(p) != ']') {
            if (parser_peek(p) == '\\') {
                parser_get(p);
                if (parser_at_end(p)) {
                    p->err = REGEX_ERR_SYNTAX;
                    break;
                }
                next = parser_get(p);
                switch (next) {
                case 'd':
                    if (!charclass_add_range(cc, '0', '9')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'w':
                    if (!charclass_add_range(cc, '0', '9') ||
                        !charclass_add_range(cc, 'A', 'Z') ||
                        !charclass_add_range(cc, 'a', 'z') ||
                        !charclass_add_range(cc, '_', '_')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 's':
                    if (!charclass_add_range(cc, ' ', ' ') ||
                        !charclass_add_range(cc, '\t', '\t') ||
                        !charclass_add_range(cc, '\n', '\n') ||
                        !charclass_add_range(cc, '\r', '\r') ||
                        !charclass_add_range(cc, '\f', '\f') ||
                        !charclass_add_range(cc, '\v', '\v')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 't':
                    if (!charclass_add_range(cc, '\t', '\t')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'n':
                    if (!charclass_add_range(cc, '\n', '\n')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'r':
                    if (!charclass_add_range(cc, '\r', '\r')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'f':
                    if (!charclass_add_range(cc, '\f', '\f')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'v':
                    if (!charclass_add_range(cc, '\v', '\v')) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'x':
                    if (!parser_read_hex(p, 2, &lo)) {
                        p->err = REGEX_ERR_SYNTAX;
                        break;
                    }
                    if (!charclass_add_range(cc, lo, lo)) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'u':
                    if (!parser_read_hex(p, 4, &lo)) {
                        p->err = REGEX_ERR_SYNTAX;
                        break;
                    }
                    if (!charclass_add_range(cc, lo, lo)) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                case 'U':
                    if (!parser_read_hex(p, 8, &lo)) {
                        p->err = REGEX_ERR_SYNTAX;
                        break;
                    }
                    if (!charclass_add_range(cc, lo, lo)) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                default:
                    if (!charclass_add_range(cc, next, next)) {
                        p->err = REGEX_ERR_NOMEM;
                    }
                    continue;
                }
            }

            if (!parser_read_codepoint(p, &lo)) {
                break;
            }
            if (!parser_at_end(p) && parser_peek(p) == '-') {
                size_t save = p->pos;
                parser_get(p);
                if (!parser_at_end(p) && parser_peek(p) != ']') {
                    if (!parser_read_codepoint(p, &hi)) {
                        break;
                    }
                    if (hi < lo) {
                        uint32_t tmp = lo;
                        lo = hi;
                        hi = tmp;
                    }
                    if (!charclass_add_range(cc, lo, hi)) {
                        p->err = REGEX_ERR_NOMEM;
                        break;
                    }
                    continue;
                }
                p->pos = save;
            }
            if (!charclass_add_range(cc, lo, lo)) {
                p->err = REGEX_ERR_NOMEM;
                break;
            }
        }

        if (p->err != REGEX_OK) {
            free(cc->ranges);
            free(cc);
            return NULL;
        }

        if (parser_at_end(p) || parser_get(p) != ']') {
            free(cc->ranges);
            free(cc);
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
        }

        n = node_new(NODE_CLASS);
        if (!n) {
            free(cc->ranges);
            free(cc);
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        n->charclass = cc;
        return n;
    }

    if (p->extended && c == '|') {
        return NULL;
    }

    if (c == '.') {
        parser_get(p);
        n = node_new(NODE_DOT);
        if (!n) {
            p->err = REGEX_ERR_NOMEM;
        }
        return n;
    }

    if (c == '^') {
        parser_get(p);
        n = node_new(NODE_BOL);
        if (!n) {
            p->err = REGEX_ERR_NOMEM;
        }
        return n;
    }

    if (c == '$') {
        parser_get(p);
        n = node_new(NODE_EOL);
        if (!n) {
            p->err = REGEX_ERR_NOMEM;
        }
        return n;
    }

    if (c == '\\') {
        parser_get(p);
        if (parser_at_end(p)) {
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
        }
        c = parser_get(p);
        switch (c) {
        case 'd':
        case 'w':
        case 's':
            n = node_new(NODE_CLASS);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->charclass = charclass_new(!p->utf8);
            if (!n->charclass) {
                p->err = REGEX_ERR_NOMEM;
                node_free(n);
                return NULL;
            }
            if (c == 'd') {
                if (!charclass_add_range(n->charclass, '0', '9')) {
                    p->err = REGEX_ERR_NOMEM;
                    node_free(n);
                    return NULL;
                }
            } else if (c == 'w') {
                if (!charclass_add_range(n->charclass, '0', '9') ||
                    !charclass_add_range(n->charclass, 'A', 'Z') ||
                    !charclass_add_range(n->charclass, 'a', 'z') ||
                    !charclass_add_range(n->charclass, '_', '_')) {
                    p->err = REGEX_ERR_NOMEM;
                    node_free(n);
                    return NULL;
                }
            } else {
                if (!charclass_add_range(n->charclass, ' ', ' ') ||
                    !charclass_add_range(n->charclass, '\t', '\t') ||
                    !charclass_add_range(n->charclass, '\n', '\n') ||
                    !charclass_add_range(n->charclass, '\r', '\r') ||
                    !charclass_add_range(n->charclass, '\f', '\f') ||
                    !charclass_add_range(n->charclass, '\v', '\v')) {
                    p->err = REGEX_ERR_NOMEM;
                    node_free(n);
                    return NULL;
                }
            }
            return n;
        case 't':
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = '\t';
            return n;
        case 'n':
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = '\n';
            return n;
        case 'r':
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = '\r';
            return n;
        case 'f':
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = '\f';
            return n;
        case 'v':
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = '\v';
            return n;
        case 'x':
            if (!parser_read_hex(p, 2, &cp)) {
                p->err = REGEX_ERR_SYNTAX;
                return NULL;
            }
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = cp;
            return n;
        case 'u':
            if (!parser_read_hex(p, 4, &cp)) {
                p->err = REGEX_ERR_SYNTAX;
                return NULL;
            }
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = cp;
            return n;
        case 'U':
            if (!parser_read_hex(p, 8, &cp)) {
                p->err = REGEX_ERR_SYNTAX;
                return NULL;
            }
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = cp;
            return n;
        default:
            n = node_new(NODE_LITERAL);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->literal = c;
            return n;
        }
    }

    if (parser_read_codepoint(p, &cp)) {
        n = node_new(NODE_LITERAL);
        if (!n) {
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        n->literal = cp;
        return n;
    }

    p->err = REGEX_ERR_SYNTAX;
    return NULL;
}

static regex_node *parse_repeat(parser *p) {
    regex_node *atom = parse_atom(p);
    regex_node *n;
    uint8_t c;
    size_t min = 0;
    size_t max = 0;
    int has_max = 0;
    int max_set = 0;

    if (!atom) {
        return NULL;
    }

    if (parser_at_end(p)) {
        return atom;
    }

    c = parser_peek(p);
    if (c == '*' || c == '+' || c == '?') {
        parser_get(p);
        if (c == '*') {
            n = node_new(NODE_STAR);
        } else if (c == '+') {
            n = node_new(NODE_PLUS);
        } else {
            n = node_new(NODE_QMARK);
        }
        if (!n) {
            node_free(atom);
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        n->left = atom;
        return n;
    }

    if (c == '{') {
        size_t save = p->pos;
        parser_get(p);
        if (parser_at_end(p) || parser_peek(p) < '0' || parser_peek(p) > '9') {
            p->pos = save;
            return atom;
        }
        while (!parser_at_end(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
            min = min * 10 + (parser_get(p) - '0');
        }
        max = min;
        if (!parser_at_end(p) && parser_peek(p) == ',') {
            parser_get(p);
            has_max = 1;
            max = 0;
            while (!parser_at_end(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
                max = max * 10 + (parser_get(p) - '0');
                max_set = 1;
            }
        }
        if (parser_at_end(p) || parser_get(p) != '}') {
            p->pos = save;
            return atom;
        }
        n = node_new(NODE_REPEAT);
        if (!n) {
            node_free(atom);
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        n->left = atom;
        n->rep_min = min;
        if (!has_max) {
            n->rep_max = min;
        } else {
            n->rep_max = max_set ? max : SIZE_MAX;
        }
        return n;
    }

    return atom;
}

static int can_start_atom(parser *p) {
    if (parser_at_end(p)) {
        return 0;
    }
    if (p->extended) {
        uint8_t c = parser_peek(p);
        if (c == ')' || c == '|') {
            return 0;
        }
    } else if (parser_peek(p) == '\\') {
        size_t save = p->pos;
        parser_get(p);
        if (!parser_at_end(p)) {
            uint8_t c = parser_peek(p);
            if (c == ')' || c == '|') {
                p->pos = save;
                return 0;
            }
        }
        p->pos = save;
    }
    return 1;
}

static regex_node *parse_concat(parser *p) {
    regex_node *left = NULL;
    regex_node *right = NULL;

    while (can_start_atom(p)) {
        right = parse_repeat(p);
        if (!right) {
            node_free(left);
            return NULL;
        }
        if (!left) {
            left = right;
        } else {
            regex_node *n = node_new(NODE_CONCAT);
            if (!n) {
                node_free(left);
                node_free(right);
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->left = left;
            n->right = right;
            left = n;
        }
    }

    if (!left) {
        left = node_new(NODE_EMPTY);
        if (!left) {
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
    }
    return left;
}

static regex_node *parse_regex(parser *p) {
    regex_node *left = parse_concat(p);
    regex_node *right;

    if (!left) {
        return NULL;
    }

    if (p->extended) {
        while (!parser_at_end(p) && parser_peek(p) == '|') {
            parser_get(p);
            right = parse_concat(p);
            if (!right) {
                node_free(left);
                return NULL;
            }
            {
                regex_node *n = node_new(NODE_ALT);
                if (!n) {
                    node_free(left);
                    node_free(right);
                    p->err = REGEX_ERR_NOMEM;
                    return NULL;
                }
                n->left = left;
                n->right = right;
                left = n;
            }
        }
    } else {
        while (!parser_at_end(p)) {
            size_t save = p->pos;
            if (parser_peek(p) == '\\') {
                parser_get(p);
                if (!parser_at_end(p) && parser_peek(p) == '|') {
                    parser_get(p);
                    right = parse_concat(p);
                    if (!right) {
                        node_free(left);
                        return NULL;
                    }
                    {
                        regex_node *n = node_new(NODE_ALT);
                        if (!n) {
                            node_free(left);
                            node_free(right);
                            p->err = REGEX_ERR_NOMEM;
                            return NULL;
                        }
                        n->left = left;
                        n->right = right;
                        left = n;
                    }
                } else {
                    p->pos = save;
                    break;
                }
            } else {
                break;
            }
        }
    }

    return left;
}

static ptrlist *list1(nfa_state **outp) {
    ptrlist *l = (ptrlist *)malloc(sizeof(*l));
    if (!l) {
        return NULL;
    }
    l->outp = outp;
    l->next = NULL;
    return l;
}

static ptrlist *append_list(ptrlist *l1, ptrlist *l2) {
    ptrlist *p;
    if (!l1) {
        return l2;
    }
    for (p = l1; p->next; p = p->next) {
    }
    p->next = l2;
    return l1;
}

static void patch(ptrlist *l, nfa_state *s) {
    ptrlist *next;
    while (l) {
        *(l->outp) = s;
        next = l->next;
        free(l);
        l = next;
    }
}

static nfa_state *nfa_state_new(nfa_prog *prog, nfa_type_t type) {
    nfa_state *s;
    if (prog->state_count == prog->state_cap) {
        size_t new_cap = prog->state_cap ? prog->state_cap * 2 : 64;
        nfa_state **new_states = (nfa_state **)realloc(prog->states, new_cap * sizeof(*new_states));
        if (!new_states) {
            return NULL;
        }
        prog->states = new_states;
        prog->state_cap = new_cap;
    }
    s = (nfa_state *)calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->type = type;
    s->id = (int)prog->state_count;
    prog->states[prog->state_count++] = s;
    return s;
}

static frag frag_literal(nfa_prog *prog, uint32_t cp) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_CHAR);
    s->ch = cp;
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_dot(nfa_prog *prog) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_DOT);
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_class(nfa_prog *prog, regex_charclass *cc) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_CLASS);
    s->charclass = cc;
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_anchor(nfa_prog *prog, nfa_type_t type) {
    frag f;
    nfa_state *s = nfa_state_new(prog, type);
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_concat(frag a, frag b) {
    patch(a.out, b.start);
    a.out = b.out;
    return a;
}

static frag frag_alt(nfa_prog *prog, frag a, frag b) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    s->out = a.start;
    s->out1 = b.start;
    f.start = s;
    f.out = append_list(a.out, b.out);
    return f;
}

static frag frag_star(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    s->out = a.start;
    patch(a.out, s);
    f.start = s;
    f.out = list1(&s->out1);
    return f;
}

static frag frag_plus(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    patch(a.out, s);
    s->out = a.start;
    f.start = a.start;
    f.out = list1(&s->out1);
    return f;
}

static frag frag_qmark(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    s->out = a.start;
    f.start = s;
    f.out = append_list(a.out, list1(&s->out1));
    return f;
}

static frag frag_group(nfa_prog *prog, frag inner, size_t group_id) {
    frag f;
    nfa_state *s1 = nfa_state_new(prog, NFA_SAVE);
    nfa_state *s2 = nfa_state_new(prog, NFA_SAVE);
    s1->save_slot = (int)(2 * group_id);
    s2->save_slot = (int)(2 * group_id + 1);
    s1->out = inner.start;
    patch(inner.out, s2);
    f.start = s1;
    f.out = list1(&s2->out);
    return f;
}

static frag compile_node(nfa_prog *prog, regex_node *n) {
    frag f;
    size_t i;
    size_t max;

    memset(&f, 0, sizeof(f));
    if (!n) {
        return f;
    }

    switch (n->type) {
    case NODE_EMPTY:
        {
            nfa_state *s = nfa_state_new(prog, NFA_EPSILON);
            f.start = s;
            f.out = list1(&s->out);
        }
        break;
    case NODE_LITERAL:
        f = frag_literal(prog, n->literal);
        break;
    case NODE_DOT:
        f = frag_dot(prog);
        break;
    case NODE_CLASS:
        f = frag_class(prog, n->charclass);
        n->charclass = NULL;
        break;
    case NODE_BOL:
        prog->uses_bol = 1;
        f = frag_anchor(prog, NFA_BOL);
        break;
    case NODE_EOL:
        prog->uses_eol = 1;
        f = frag_anchor(prog, NFA_EOL);
        break;
    case NODE_CONCAT:
        f = frag_concat(compile_node(prog, n->left), compile_node(prog, n->right));
        break;
    case NODE_ALT:
        f = frag_alt(prog, compile_node(prog, n->left), compile_node(prog, n->right));
        break;
    case NODE_STAR:
        f = frag_star(prog, compile_node(prog, n->left));
        break;
    case NODE_PLUS:
        f = frag_plus(prog, compile_node(prog, n->left));
        break;
    case NODE_QMARK:
        f = frag_qmark(prog, compile_node(prog, n->left));
        break;
    case NODE_REPEAT:
        max = n->rep_max;
        if (max == 0) {
            f = compile_node(prog, NULL);
            break;
        }
        f = compile_node(prog, n->left);
        for (i = 1; i < n->rep_min; ++i) {
            f = frag_concat(f, compile_node(prog, n->left));
        }
        if (max == SIZE_MAX) {
            f = frag_concat(f, frag_star(prog, compile_node(prog, n->left)));
            break;
        }
        if (max > n->rep_min) {
            size_t extra;
            for (extra = n->rep_min; extra < max; ++extra) {
                f = frag_concat(f, frag_qmark(prog, compile_node(prog, n->left)));
            }
        }
        break;
    case NODE_GROUP:
        f = frag_group(prog, compile_node(prog, n->left), n->group_id);
        break;
    default:
        break;
    }

    return f;
}

static void nfa_free(nfa_prog *prog) {
    size_t i;
    if (!prog) {
        return;
    }
    for (i = 0; i < prog->state_count; ++i) {
        free(prog->states[i]);
    }
    free(prog->states);
    free(prog);
}

static int bitset_test(uint8_t *set, size_t idx) {
    return (set[idx >> 3] & (uint8_t)(1u << (idx & 7u))) != 0;
}

static void bitset_set(uint8_t *set, size_t idx) {
    set[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
}

static size_t bitset_bytes(size_t bits) {
    return (bits + 7u) / 8u;
}

static void epsilon_closure(nfa_prog *prog, uint8_t *set, size_t start_id, size_t pos,
                            const char *text, size_t text_len, unsigned flags, int ignore_anchors) {
    int stack_buf[2048];
    int *heap_stack = NULL;
    int *stack = stack_buf;
    size_t stack_cap = 2048;
    size_t stack_len = 0;

#define PUSH_STACK(id_val) do { \
    if (stack_len == stack_cap) { \
        size_t new_cap = stack_cap * 2; \
        int *new_stack; \
        if (!heap_stack) { \
            new_stack = (int *)malloc(new_cap * sizeof(*new_stack)); \
            if (new_stack) { \
                memcpy(new_stack, stack_buf, stack_len * sizeof(*stack_buf)); \
            } \
        } else { \
            new_stack = (int *)realloc(heap_stack, new_cap * sizeof(*new_stack)); \
        } \
        if (!new_stack) { \
            if (heap_stack) free(heap_stack); \
            return; \
        } \
        heap_stack = new_stack; \
        stack = heap_stack; \
        stack_cap = new_cap; \
    } \
    stack[stack_len++] = (id_val); \
} while (0)

    PUSH_STACK((int)start_id);

    while (stack_len > 0) {
        nfa_state *s;
        int sid = stack[--stack_len];
        if (sid < 0 || (size_t)sid >= prog->state_count) {
            continue;
        }
        if (bitset_test(set, (size_t)sid)) {
            continue;
        }
        bitset_set(set, (size_t)sid);
        s = prog->states[sid];
        if (!s) {
            continue;
        }
        switch (s->type) {
        case NFA_EPSILON:
        case NFA_SAVE:
            if (s->out) {
                PUSH_STACK(s->out->id);
            }
            break;
        case NFA_SPLIT:
            if (s->out) {
                PUSH_STACK(s->out->id);
            }
            if (s->out1) {
                PUSH_STACK(s->out1->id);
            }
            break;
        case NFA_BOL:
            if (ignore_anchors ||
                pos == 0 || ((flags & REGEX_FLAG_MULTILINE) && pos > 0 && text && text[pos - 1] == '\n')) {
                if (s->out) {
                    PUSH_STACK(s->out->id);
                }
            }
            break;
        case NFA_EOL:
            if (ignore_anchors ||
                pos == text_len || ((flags & REGEX_FLAG_MULTILINE) && pos < text_len && text && text[pos] == '\n')) {
                if (s->out) {
                    PUSH_STACK(s->out->id);
                }
            }
            break;
        default:
            break;
        }
    }

#undef PUSH_STACK

    if (heap_stack) {
        free(heap_stack);
    }
}

static int dfa_state_equal(uint8_t *a, uint8_t *b, size_t bytes) {
    return memcmp(a, b, bytes) == 0;
}

static int dfa_find_state(dfa_prog *dfa, uint8_t *set, size_t bytes) {
    size_t i;
    for (i = 0; i < dfa->state_count; ++i) {
        if (dfa_state_equal(dfa->states[i].nfa_set, set, bytes)) {
            return (int)i;
        }
    }
    return -1;
}

static int dfa_add_state(dfa_prog *dfa, uint8_t *set, size_t bytes, int is_accept) {
    if (dfa->max_states && dfa->state_count >= dfa->max_states) {
        return -1;
    }
    if (dfa->state_count == dfa->state_cap) {
        size_t new_cap = dfa->state_cap ? dfa->state_cap * 2 : 64;
        dfa_state *new_states = (dfa_state *)realloc(dfa->states, new_cap * sizeof(*new_states));
        if (!new_states) {
            return -1;
        }
        dfa->states = new_states;
        dfa->state_cap = new_cap;
    }
    dfa->states[dfa->state_count].nfa_set = set;
    dfa->states[dfa->state_count].nfa_set_len = bytes;
    dfa->states[dfa->state_count].is_accept = is_accept;
    dfa->states[dfa->state_count].trans = NULL;
    dfa->states[dfa->state_count].trans_count = 0;
    dfa->states[dfa->state_count].trans_cap = 0;
    return (int)dfa->state_count++;
}

static int dfa_add_transition(dfa_state *state, uint32_t cp, int target) {
    size_t i;
    for (i = 0; i < state->trans_count; ++i) {
        if (state->trans[i].cp == cp) {
            state->trans[i].target = target;
            return 1;
        }
    }
    if (state->trans_count == state->trans_cap) {
        size_t new_cap = state->trans_cap ? state->trans_cap * 2 : 8;
        dfa_trans *new_trans = (dfa_trans *)realloc(state->trans, new_cap * sizeof(*new_trans));
        if (!new_trans) {
            return 0;
        }
        state->trans = new_trans;
        state->trans_cap = new_cap;
    }
    state->trans[state->trans_count].cp = cp;
    state->trans[state->trans_count].target = target;
    state->trans_count++;
    return 1;
}

static int dfa_find_transition(const dfa_state *state, uint32_t cp) {
    size_t i;
    for (i = 0; i < state->trans_count; ++i) {
        if (state->trans[i].cp == cp) {
            return state->trans[i].target;
        }
    }
    return -1;
}

static int dfa_state_accepts(nfa_prog *prog, uint8_t *set) {
    size_t i;
    for (i = 0; i < prog->state_count; ++i) {
        if (bitset_test(set, i)) {
            if (prog->states[i]->type == NFA_MATCH) {
                return 1;
            }
        }
    }
    return 0;
}

static void dfa_move(nfa_prog *prog, uint8_t *set, size_t pos, const char *text, size_t text_len,
                     uint32_t cp, unsigned flags, uint8_t *next, size_t bytes) {
    size_t i;

    memset(next, 0, bytes);

    for (i = 0; i < prog->state_count; ++i) {
        if (!bitset_test(set, i)) {
            continue;
        }
        {
            nfa_state *s = prog->states[i];
            if (!s) {
                continue;
            }
            switch (s->type) {
            case NFA_CHAR:
                if (flags & REGEX_FLAG_ICASE) {
                    if (s->ch <= 0x7F && cp <= 0x7F) {
                        if (regex_ascii_tolower(s->ch) == regex_ascii_tolower(cp)) {
                            if (s->out) {
                                epsilon_closure(prog, next, (size_t)s->out->id, pos, text, text_len, flags, 1);
                            }
                        }
                        break;
                    }
                }
                if (s->ch == cp) {
                    if (s->out) {
                        epsilon_closure(prog, next, (size_t)s->out->id, pos, text, text_len, flags, 1);
                    }
                }
                break;
            case NFA_DOT:
                if ((flags & REGEX_FLAG_DOTALL) || !regex_is_newline(cp)) {
                    if (s->out) {
                        epsilon_closure(prog, next, (size_t)s->out->id, pos, text, text_len, flags, 1);
                    }
                }
                break;
            case NFA_CLASS:
                if (charclass_match(s->charclass, cp, (flags & REGEX_FLAG_ICASE) != 0, (flags & REGEX_FLAG_UTF8) != 0)) {
                    if (s->out) {
                        epsilon_closure(prog, next, (size_t)s->out->id, pos, text, text_len, flags, 1);
                    }
                }
                break;
            default:
                break;
            }
        }
    }

}

static dfa_prog *dfa_build(nfa_prog *prog, unsigned flags, size_t max_states) {
    dfa_prog *dfa = (dfa_prog *)calloc(1, sizeof(*dfa));
    size_t bytes = bitset_bytes(prog->state_count);
    uint8_t *start_set;

    if (!dfa) {
        return NULL;
    }

    start_set = (uint8_t *)calloc(bytes, 1);
    if (!start_set) {
        free(dfa);
        return NULL;
    }

    dfa->max_states = max_states;
    epsilon_closure(prog, start_set, (size_t)prog->start->id, 0, NULL, 0, flags, 1);
    dfa->start_state = dfa_add_state(dfa, start_set, bytes, dfa_state_accepts(prog, start_set));
    if (dfa->start_state < 0) {
        free(start_set);
        free(dfa);
        return NULL;
    }

    return dfa;
}

static void dfa_free(dfa_prog *dfa) {
    size_t i;
    if (!dfa) {
        return;
    }
    for (i = 0; i < dfa->state_count; ++i) {
        free(dfa->states[i].nfa_set);
        free(dfa->states[i].trans);
    }
    free(dfa->states);
    free(dfa);
}

static int dfa_step(nfa_prog *prog, dfa_prog *dfa, int state_id, size_t pos,
                    const char *text, size_t text_len, uint32_t cp, unsigned flags,
                    uint8_t *scratch_set, size_t scratch_cap) {
    dfa_state *state = &dfa->states[state_id];
    int existing = dfa_find_transition(state, cp);
    if (existing >= 0) {
        return existing;
    }

    {
        size_t bytes = scratch_cap;
        uint8_t *next = scratch_set;
        int target;

        dfa_move(prog, state->nfa_set, pos, text, text_len, cp, flags, next, bytes);

        target = dfa_find_state(dfa, next, bytes);
        if (target < 0) {
            uint8_t *new_set;
            int accept;

            new_set = (uint8_t *)malloc(bytes);
            if (!new_set) {
                return -1;
            }
            memcpy(new_set, next, bytes);

            accept = dfa_state_accepts(prog, new_set);
            target = dfa_add_state(dfa, new_set, bytes, accept);
            if (target < 0) {
                free(new_set);
                return -1;
            }
            // State array may have been reallocated
            state = &dfa->states[state_id];
        }
        if (!dfa_add_transition(state, cp, target)) {
            return -1;
        }
        return target;
    }
}

static int thread_list_init(thread_list *list, size_t cap) {
    list->threads = (thread *)calloc(cap, sizeof(*list->threads));
    if (!list->threads) {
        return 0;
    }
    list->cap = cap;
    list->count = 0;
    return 1;
}

static void thread_list_clear(thread_list *list) {
    size_t i;
    if (!list) {
        return;
    }
    for (i = 0; i < list->count; ++i) {
        free(list->threads[i].caps);
        list->threads[i].caps = NULL;
        list->threads[i].s = NULL;
    }
    list->count = 0;
}

static void thread_list_free(thread_list *list) {
    if (!list) {
        return;
    }
    thread_list_clear(list);
    free(list->threads);
    list->threads = NULL;
    list->cap = 0;
}

static size_t *caps_clone(const size_t *src, size_t count) {
    size_t *out = (size_t *)malloc(count * sizeof(*out));
    if (!out) {
        return NULL;
    }
    memcpy(out, src, count * sizeof(*out));
    return out;
}

static int add_thread(thread_list *list, nfa_state *s, size_t *caps) {
    if (list->count == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 64;
        thread *new_threads = (thread *)realloc(list->threads, new_cap * sizeof(*new_threads));
        if (!new_threads) {
            return 0;
        }
        list->threads = new_threads;
        list->cap = new_cap;
    }
    list->threads[list->count].s = s;
    list->threads[list->count].caps = caps;
    list->count++;
    return 1;
}

static int add_state(thread_list *list, nfa_state *s, size_t *caps, size_t cap_count,
                     int list_id, size_t pos, const char *text, size_t text_len,
                     unsigned flags, regex_err_t *out_err) {
    size_t *clone_caps;

    if (!s) {
        free(caps);
        return 1;
    }

    if (s->last_list_id == list_id) {
        free(caps);
        return 1;
    }
    s->last_list_id = list_id;

    switch (s->type) {
    case NFA_EPSILON:
        return add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err);
    case NFA_SAVE:
        if ((size_t)s->save_slot < cap_count) {
            caps[s->save_slot] = pos;
        }
        return add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err);
    case NFA_SPLIT:
        clone_caps = caps_clone(caps, cap_count);
        if (!clone_caps) {
            free(caps);
            if (out_err) {
                *out_err = REGEX_ERR_NOMEM;
            }
            return 0;
        }
        if (!add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err)) {
            free(clone_caps);
            return 0;
        }
        return add_state(list, s->out1, clone_caps, cap_count, list_id, pos, text, text_len, flags, out_err);
    case NFA_BOL:
        if (pos == 0 || ((flags & REGEX_FLAG_MULTILINE) && pos > 0 && text[pos - 1] == '\n')) {
            return add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err);
        }
        free(caps);
        return 1;
    case NFA_EOL:
        if (pos == text_len || ((flags & REGEX_FLAG_MULTILINE) && pos < text_len && text[pos] == '\n')) {
            return add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err);
        }
        free(caps);
        return 1;
    default:
        return add_thread(list, s, caps);
    }
}

static int match_class_char(regex_charclass *cc, uint32_t cp, unsigned flags) {
    return charclass_match(cc, cp, (flags & REGEX_FLAG_ICASE) != 0, (flags & REGEX_FLAG_UTF8) != 0);
}

static int match_char(uint32_t pat, uint32_t cp, unsigned flags) {
    if (flags & REGEX_FLAG_ICASE) {
        if (flags & REGEX_FLAG_UTF8) {
            return regex_unicode_tolower(pat) == regex_unicode_tolower(cp);
        }
        if (pat <= 0x7F && cp <= 0x7F) {
            return regex_ascii_tolower(pat) == regex_ascii_tolower(cp);
        }
    }
    return pat == cp;
}

static ssize_t nfa_capture_match(nfa_prog *prog, const regex_t *re, const char *text, size_t text_len,
                                 size_t start_pos, size_t *capture_offsets, size_t max_captures,
                                 regex_err_t *out_err) {
    size_t cap_count = prog->capture_count * 2;
    size_t *caps_init;
    size_t i;
    size_t local_pos = start_pos;
    size_t steps = 0;
    int list_id = 1;
    thread_list clist;
    thread_list nlist;

    if (!thread_list_init(&clist, 64) || !thread_list_init(&nlist, 64)) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        thread_list_free(&clist);
        thread_list_free(&nlist);
        return -REGEX_ERR_NOMEM;
    }

    caps_init = (size_t *)malloc(cap_count * sizeof(*caps_init));
    if (!caps_init) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        thread_list_free(&clist);
        thread_list_free(&nlist);
        return -REGEX_ERR_NOMEM;
    }

    for (i = 0; i < cap_count; ++i) {
        caps_init[i] = (size_t)-1;
    }
    caps_init[0] = start_pos;
    thread_list_clear(&clist);
    thread_list_clear(&nlist);
    list_id++;
    if (!add_state(&clist, prog->start, caps_init, cap_count, list_id, start_pos, text, text_len,
                   re->flags, out_err)) {
        thread_list_free(&clist);
        thread_list_free(&nlist);
        return -REGEX_ERR_NOMEM;
    }

    while (local_pos <= text_len) {
        size_t idx;
        for (idx = 0; idx < clist.count; ++idx) {
            nfa_state *s = clist.threads[idx].s;
            if (s->type == NFA_MATCH) {
                if (capture_offsets && max_captures >= cap_count) {
                    memcpy(capture_offsets, clist.threads[idx].caps, cap_count * sizeof(*capture_offsets));
                    capture_offsets[1] = local_pos;
                }
                if (out_err) {
                    *out_err = REGEX_OK;
                }
                thread_list_free(&clist);
                thread_list_free(&nlist);
                return (ssize_t)prog->capture_count;
            }
        }

        if (local_pos >= text_len) {
            break;
        }

        if (steps++ > re->limits.match_steps) {
            if (out_err) {
                *out_err = REGEX_ERR_MATCH_TIMEOUT;
            }
            thread_list_free(&clist);
            thread_list_free(&nlist);
            return -REGEX_ERR_MATCH_TIMEOUT;
        }

        {
            uint32_t cp;
            size_t advance = 1;
            if (re->flags & REGEX_FLAG_UTF8) {
                size_t idx2 = local_pos;
                if (!regex_utf8_decode(text, text_len, &idx2, &cp)) {
                    if (out_err) {
                        *out_err = REGEX_ERR_SYNTAX;
                    }
                    thread_list_free(&clist);
                    thread_list_free(&nlist);
                    return -REGEX_ERR_SYNTAX;
                }
                advance = idx2 - local_pos;
            } else {
                cp = (uint8_t)text[local_pos];
            }

            thread_list_clear(&nlist);
            list_id++;
            for (i = 0; i < clist.count; ++i) {
                nfa_state *s = clist.threads[i].s;
                size_t *caps = clist.threads[i].caps;
                clist.threads[i].caps = NULL;

                switch (s->type) {
                case NFA_CHAR:
                    if (match_char(s->ch, cp, re->flags)) {
                        if (!add_state(&nlist, s->out, caps, cap_count, list_id, local_pos + advance,
                                       text, text_len, re->flags, out_err)) {
                            thread_list_free(&clist);
                            thread_list_free(&nlist);
                            return -REGEX_ERR_NOMEM;
                        }
                    } else {
                        free(caps);
                    }
                    break;
                case NFA_DOT:
                    if ((re->flags & REGEX_FLAG_DOTALL) || !regex_is_newline(cp)) {
                        if (!add_state(&nlist, s->out, caps, cap_count, list_id, local_pos + advance,
                                       text, text_len, re->flags, out_err)) {
                            thread_list_free(&clist);
                            thread_list_free(&nlist);
                            return -REGEX_ERR_NOMEM;
                        }
                    } else {
                        free(caps);
                    }
                    break;
                case NFA_CLASS:
                    if (match_class_char(s->charclass, cp, re->flags)) {
                        if (!add_state(&nlist, s->out, caps, cap_count, list_id, local_pos + advance,
                                       text, text_len, re->flags, out_err)) {
                            thread_list_free(&clist);
                            thread_list_free(&nlist);
                            return -REGEX_ERR_NOMEM;
                        }
                    } else {
                        free(caps);
                    }
                    break;
                default:
                    free(caps);
                    break;
                }
            }

            {
                thread_list tmp = clist;
                clist = nlist;
                nlist = tmp;
            }
            local_pos += advance;
        }
    }

    if (out_err) {
        *out_err = REGEX_OK;
    }
    thread_list_free(&clist);
    thread_list_free(&nlist);
    return -1;
}

static int dfa_match_span(const regex_t *re, safe_regex *sre, const char *text, size_t text_len,
                          size_t start_pos, size_t *out_end, regex_err_t *out_err,
                          uint8_t *scratch_set, size_t scratch_cap) {
    dfa_prog *dfa = sre->dfa;
    size_t pos = start_pos;
    int state = dfa->start_state;
    int matched = 0;
    size_t last_accept = start_pos;
    size_t steps = 0;

    while (pos <= text_len) {
        if (dfa->states[state].is_accept) {
            matched = 1;
            last_accept = pos;
        }
        if (pos == text_len) {
            break;
        }
        if (steps++ > re->limits.match_steps) {
            if (out_err) {
                *out_err = REGEX_ERR_MATCH_TIMEOUT;
            }
            return 0;
        }
        if (re->flags & REGEX_FLAG_UTF8) {
            size_t idx = pos;
            uint32_t cp;
            if (!regex_utf8_decode(text, text_len, &idx, &cp)) {
                if (out_err) {
                    *out_err = REGEX_ERR_SYNTAX;
                }
                return 0;
            }
            state = dfa_step(sre->nfa, dfa, state, pos, text, text_len, cp, re->flags, scratch_set, scratch_cap);
            if (state < 0) {
                if (out_err) {
                    *out_err = REGEX_ERR_COMPILE_LIMIT;
                }
                break;
            }
            pos = idx;
        } else {
            uint32_t cp = (uint8_t)text[pos];
            state = dfa_step(sre->nfa, dfa, state, pos, text, text_len, cp, re->flags, scratch_set, scratch_cap);
            if (state < 0) {
                if (out_err) {
                    *out_err = REGEX_ERR_COMPILE_LIMIT;
                }
                break;
            }
            pos++;
        }
    }

    if (matched) {
        *out_end = last_accept;
        return 1;
    }
    return 0;
}

static ssize_t safe_regex_match_internal(const regex_t *re, const char *text, size_t text_len,
                                         size_t *capture_offsets, size_t max_captures,
                                         regex_err_t *out_err,
                                         uint8_t *scratch_set, size_t scratch_cap) {
    safe_regex *sre = (safe_regex *)re->impl;
    size_t cap_count = sre->capture_count * 2;
    size_t start_pos = 0;
    int anchored = (re->flags & REGEX_FLAG_ANCHORED) != 0;

    if (!sre || !sre->dfa || !sre->nfa) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }

    if (capture_offsets && max_captures >= cap_count) {
        size_t i;
        for (i = 0; i < cap_count; ++i) {
            capture_offsets[i] = (size_t)-1;
        }
    }

    while (start_pos <= text_len) {
        size_t end_pos = 0;
        int ok = dfa_match_span(re, sre, text, text_len, start_pos, &end_pos, out_err, scratch_set, scratch_cap);
        if (ok) {
            ssize_t cap_res = nfa_capture_match(sre->nfa, re, text, text_len, start_pos,
                                                capture_offsets, max_captures, out_err);
            if (cap_res >= 0) {
                (void)end_pos;
                return cap_res;
            }
            return cap_res;
        }
        if (anchored || sre->nfa->uses_bol) {
            break;
        }
        if (re->flags & REGEX_FLAG_UTF8) {
            size_t idx = start_pos;
            uint32_t cp;
            if (!regex_utf8_decode(text, text_len, &idx, &cp)) {
                break;
            }
            if (idx == start_pos) {
                start_pos++;
            } else {
                start_pos = idx;
            }
        } else {
            start_pos++;
        }
    }

    if (out_err) {
        *out_err = REGEX_OK;
    }
    return -1;
}

static ssize_t safe_regex_match(const regex_t *re, const char *text, size_t text_len,
                                size_t *capture_offsets, size_t max_captures,
                                regex_err_t *out_err) {
    safe_regex *sre = (safe_regex *)re->impl;
    size_t scratch_cap;
    uint8_t *scratch_set;
    ssize_t res;

    if (!sre || !sre->nfa) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }

    scratch_cap = bitset_bytes(sre->nfa->state_count);
    scratch_set = (uint8_t *)malloc(scratch_cap);
    if (!scratch_set) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return -REGEX_ERR_NOMEM;
    }

    res = safe_regex_match_internal(re, text, text_len, capture_offsets, max_captures, out_err, scratch_set, scratch_cap);
    free(scratch_set);
    return res;
}

static regex_err_t safe_regex_find_all(const regex_t *re, const char *text, size_t text_len,
                                       regex_match_cb cb, void *user, size_t max_matches) {
    size_t pos = 0;
    size_t matches = 0;
    regex_err_t err = REGEX_OK;
    size_t cap_count;
    size_t *caps;
    ssize_t res;
    size_t limit = max_matches ? max_matches : re->limits.max_matches;
    safe_regex *sre = (safe_regex *)re->impl;
    size_t scratch_cap;
    uint8_t *scratch_set;

    if (!re || !text || !cb) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (!sre || !sre->nfa) {
        return REGEX_ERR_INTERNAL;
    }

    cap_count = regex_capture_count(re) * 2;
    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        return REGEX_ERR_NOMEM;
    }

    scratch_cap = bitset_bytes(sre->nfa->state_count);
    scratch_set = (uint8_t *)malloc(scratch_cap);
    if (!scratch_set) {
        free(caps);
        return REGEX_ERR_NOMEM;
    }

    while (pos <= text_len) {
        res = safe_regex_match_internal(re, text + pos, text_len - pos, caps, cap_count, &err, scratch_set, scratch_cap);
        if (res < 0 && res != -1) {
            free(caps);
            free(scratch_set);
            return err;
        }
        if (res == -1) {
            break;
        }

        if (limit && matches >= limit) {
            free(caps);
            free(scratch_set);
            return REGEX_ERR_MATCH_TIMEOUT;
        }

        {
            size_t start = caps[0] + pos;
            size_t end = caps[1] + pos;
            size_t i;
            for (i = 0; i < cap_count; ++i) {
                if (caps[i] != (size_t)-1) {
                    caps[i] += pos;
                }
            }
            if (!cb(user, start, end, caps, cap_count / 2)) {
                break;
            }
            matches++;
            if (end == start) {
                pos = end + 1;
            } else {
                pos = end;
            }
        }
    }

    free(scratch_set);
    free(caps);
    return REGEX_OK;
}

static int ensure_capacity(char **buf, size_t *cap, size_t needed) {
    if (needed <= *cap) {
        return 1;
    }
    {
        size_t new_cap = *cap ? *cap * 2 : 256;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        {
            char *new_buf = (char *)realloc(*buf, new_cap);
            if (!new_buf) {
                return 0;
            }
            *buf = new_buf;
            *cap = new_cap;
        }
    }
    return 1;
}

static regex_err_t replace_append_capture(char **out, size_t *out_len, size_t *out_cap,
                                         const char *text, size_t text_len,
                                         const size_t *caps, size_t cap_count,
                                         size_t idx) {
    size_t start;
    size_t end;
    if (idx >= cap_count) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    start = caps[2 * idx];
    end = caps[2 * idx + 1];
    if (start == (size_t)-1 || end == (size_t)-1 || end < start || end > text_len) {
        return REGEX_OK;
    }
    if (!ensure_capacity(out, out_cap, *out_len + (end - start) + 1)) {
        return REGEX_ERR_NOMEM;
    }
    memcpy(*out + *out_len, text + start, end - start);
    *out_len += end - start;
    return REGEX_OK;
}

static regex_err_t safe_regex_replace(const regex_t *re, const char *text, size_t text_len,
                                      const char *replacement, int global,
                                      char **out_buf, size_t *out_len) {
    size_t cap_count = regex_capture_count(re) * 2;
    size_t *caps = NULL;
    size_t pos = 0;
    size_t out_cap = 0;
    char *out = NULL;
    regex_err_t err = REGEX_OK;
    ssize_t res;
    size_t out_len_local = 0;
    size_t *out_len_ptr = out_len ? out_len : &out_len_local;
    safe_regex *sre = (safe_regex *)re->impl;
    size_t scratch_cap;
    uint8_t *scratch_set;

    if (!out_buf) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (!sre || !sre->nfa) {
        return REGEX_ERR_INTERNAL;
    }

    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        return REGEX_ERR_NOMEM;
    }

    scratch_cap = bitset_bytes(sre->nfa->state_count);
    scratch_set = (uint8_t *)malloc(scratch_cap);
    if (!scratch_set) {
        free(caps);
        return REGEX_ERR_NOMEM;
    }

    while (pos <= text_len) {
        res = safe_regex_match_internal(re, text + pos, text_len - pos, caps, cap_count, &err, scratch_set, scratch_cap);
        if (res < 0 && res != -1) {
            free(caps);
            free(out);
            free(scratch_set);
            return err;
        }
        if (res == -1) {
            break;
        }

        {
            size_t start = caps[0] + pos;
            size_t end = caps[1] + pos;
            size_t rpos = 0;
            if (!ensure_capacity(&out, &out_cap, *out_len_ptr + (start - pos) + 1)) {
                free(caps);
                free(out);
                free(scratch_set);
                return REGEX_ERR_NOMEM;
            }
            memcpy(out + *out_len_ptr, text + pos, start - pos);
            *out_len_ptr += start - pos;

            while (replacement[rpos] != '\0') {
                if (replacement[rpos] == '$' && replacement[rpos + 1] >= '0' && replacement[rpos + 1] <= '9') {
                    size_t idx = (size_t)(replacement[rpos + 1] - '0');
                    err = replace_append_capture(&out, out_len_ptr, &out_cap, text, text_len, caps, cap_count / 2, idx);
                    if (err != REGEX_OK) {
                        free(caps);
                        free(out);
                        free(scratch_set);
                        return err;
                    }
                    rpos += 2;
                    continue;
                }
                if (replacement[rpos] == '\\' && replacement[rpos + 1] != '\0') {
                    rpos++;
                }
                if (!ensure_capacity(&out, &out_cap, *out_len_ptr + 2)) {
                    free(caps);
                    free(out);
                    free(scratch_set);
                    return REGEX_ERR_NOMEM;
                }
                out[(*out_len_ptr)++] = replacement[rpos++];
            }

            pos = end;
            if (!global) {
                break;
            }
            if (end == start) {
                pos++;
            }
        }
    }

    if (!ensure_capacity(&out, &out_cap, *out_len_ptr + (text_len - pos) + 1)) {
        free(caps);
        free(out);
        free(scratch_set);
        return REGEX_ERR_NOMEM;
    }
    memcpy(out + *out_len_ptr, text + pos, text_len - pos);
    *out_len_ptr += text_len - pos;
    out[*out_len_ptr] = '\0';

    *out_buf = out;
    free(caps);
    free(scratch_set);
    return REGEX_OK;
}

static regex_err_t safe_regex_split(const regex_t *re, const char *text, size_t text_len,
                                    regex_split_result_t *out, size_t max_splits) {
    size_t cap_count = regex_capture_count(re) * 2;
    size_t *caps;
    size_t pos = 0;
    size_t count = 0;
    size_t cap = 0;
    char **items = NULL;
    regex_err_t err = REGEX_OK;
    ssize_t res;
    safe_regex *sre = (safe_regex *)re->impl;
    size_t scratch_cap;
    uint8_t *scratch_set;

    if (!out) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (!sre || !sre->nfa) {
        return REGEX_ERR_INTERNAL;
    }

    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        return REGEX_ERR_NOMEM;
    }

    scratch_cap = bitset_bytes(sre->nfa->state_count);
    scratch_set = (uint8_t *)malloc(scratch_cap);
    if (!scratch_set) {
        free(caps);
        return REGEX_ERR_NOMEM;
    }

    while (pos <= text_len) {
        res = safe_regex_match_internal(re, text + pos, text_len - pos, caps, cap_count, &err, scratch_set, scratch_cap);
        if (res < 0 && res != -1) {
            free(caps);
            free(scratch_set);
            return err;
        }
        if (res == -1 || (max_splits && count >= max_splits)) {
            break;
        }
        {
            size_t start = caps[0] + pos;
            size_t end = caps[1] + pos;
            size_t seg_len = start - pos;
            if (count == cap) {
                size_t new_cap = cap ? cap * 2 : 8;
                char **new_items = (char **)realloc(items, new_cap * sizeof(*new_items));
                if (!new_items) {
                    free(caps);
                    free(scratch_set);
                    return REGEX_ERR_NOMEM;
                }
                items = new_items;
                cap = new_cap;
            }
            items[count] = (char *)malloc(seg_len + 1);
            if (!items[count]) {
                free(caps);
                free(scratch_set);
                return REGEX_ERR_NOMEM;
            }
            memcpy(items[count], text + pos, seg_len);
            items[count][seg_len] = '\0';
            count++;
            pos = end;
            if (end == start) {
                pos++;
            }
        }
    }

    if (pos <= text_len) {
        size_t seg_len = text_len - pos;
        if (count == cap) {
            size_t new_cap = cap ? cap * 2 : 8;
            char **new_items = (char **)realloc(items, new_cap * sizeof(*new_items));
            if (!new_items) {
                free(caps);
                free(scratch_set);
                return REGEX_ERR_NOMEM;
            }
            items = new_items;
            cap = new_cap;
        }
        items[count] = (char *)malloc(seg_len + 1);
        if (!items[count]) {
            free(caps);
            free(scratch_set);
            return REGEX_ERR_NOMEM;
        }
        memcpy(items[count], text + pos, seg_len);
        items[count][seg_len] = '\0';
        count++;
    }

    out->items = items;
    out->count = count;
    free(caps);
    free(scratch_set);
    return REGEX_OK;
}

static int match_queue_push(match_queue *q, match_record *rec) {
    if (q->count == q->cap) {
        size_t new_cap = q->cap ? q->cap * 2 : 16;
        match_record *new_items = (match_record *)realloc(q->items, new_cap * sizeof(*new_items));
        if (!new_items) {
            return 0;
        }
        q->items = new_items;
        q->cap = new_cap;
    }
    q->items[q->tail] = *rec;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    return 1;
}

static int match_queue_pop(match_queue *q, match_record *rec) {
    if (!q->count) {
        return 0;
    }
    *rec = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return 1;
}

static regex_iter_t *safe_regex_iter_create(const regex_t *re, unsigned options, regex_err_t *out_err) {
    regex_iter_safe *it = (regex_iter_safe *)calloc(1, sizeof(*it));
    safe_regex *sre = (safe_regex *)re->impl;

    if (!it) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return NULL;
    }
    if (!sre || !sre->nfa) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        free(it);
        return NULL;
    }

    it->scratch_cap = bitset_bytes(sre->nfa->state_count);
    it->scratch_set = (uint8_t *)malloc(it->scratch_cap);
    if (!it->scratch_set) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        free(it);
        return NULL;
    }

    it->base.engine = regex_engine_safe_vtable();
    it->re = re;
    it->options = options;
    it->last_err = REGEX_OK;
    if (out_err) {
        *out_err = REGEX_OK;
    }
    return (regex_iter_t *)it;
}

static regex_err_t safe_regex_iter_feed(regex_iter_t *it_base, const char *chunk, size_t len) {
    regex_iter_safe *it = (regex_iter_safe *)it_base;
    regex_err_t err;
    size_t cap_count;
    size_t *caps;
    ssize_t res;

    if (!it || !chunk) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    if (it->finished) {
        it->last_err = REGEX_ERR_INVALID_ARGUMENT;
        return it->last_err;
    }

    if (it->buf_len + len > it->re->limits.max_stream_buffer) {
        it->last_err = REGEX_ERR_MATCH_TIMEOUT;
        return it->last_err;
    }

    if (it->buf_len + len + 1 > it->buf_cap) {
        size_t new_cap = it->buf_cap ? it->buf_cap * 2 : 1024;
        while (new_cap < it->buf_len + len + 1) {
            new_cap *= 2;
        }
        {
            char *new_buf = (char *)realloc(it->buffer, new_cap);
            if (!new_buf) {
                it->last_err = REGEX_ERR_NOMEM;
                return it->last_err;
            }
            it->buffer = new_buf;
            it->buf_cap = new_cap;
        }
    }

    memcpy(it->buffer + it->buf_len, chunk, len);
    it->buf_len += len;
    it->buffer[it->buf_len] = '\0';

    cap_count = regex_capture_count(it->re) * 2;
    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        it->last_err = REGEX_ERR_NOMEM;
        return it->last_err;
    }

    while (it->scan_pos <= it->buf_len) {
        res = safe_regex_match_internal(it->re, it->buffer + it->scan_pos, it->buf_len - it->scan_pos,
                                        caps, cap_count, &err, it->scratch_set, it->scratch_cap);
        if (res == -1) {
            break;
        }
        if (res < 0) {
            free(caps);
            it->last_err = err;
            return it->last_err;
        }
        {
            match_record rec;
            size_t start = caps[0] + it->scan_pos;
            size_t end = caps[1] + it->scan_pos;
            rec.start = it->base_offset + start;
            rec.end = it->base_offset + end;
            rec.cap_count = cap_count;
            rec.caps = (size_t *)malloc(cap_count * sizeof(*rec.caps));
            if (!rec.caps) {
                free(caps);
                it->last_err = REGEX_ERR_NOMEM;
                return it->last_err;
            }
            memcpy(rec.caps, caps, cap_count * sizeof(*rec.caps));
            if (!match_queue_push(&it->queue, &rec)) {
                free(rec.caps);
                free(caps);
                it->last_err = REGEX_ERR_NOMEM;
                return it->last_err;
            }
            if (end == start) {
                it->scan_pos = end + 1;
            } else {
                it->scan_pos = end;
            }
            if (it->re->limits.max_matches && it->queue.count > it->re->limits.max_matches) {
                it->last_err = REGEX_ERR_MATCH_TIMEOUT;
                free(caps);
                return it->last_err;
            }
        }
    }

    free(caps);
    return REGEX_OK;
}

static regex_err_t safe_regex_iter_finish(regex_iter_t *it_base) {
    regex_iter_safe *it = (regex_iter_safe *)it_base;
    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    it->finished = 1;
    return REGEX_OK;
}

static ssize_t safe_regex_iter_next(regex_iter_t *it_base, size_t *start, size_t *end,
                                    size_t *capture_offsets, size_t max_captures,
                                    size_t *out_cap_count) {
    regex_iter_safe *it = (regex_iter_safe *)it_base;
    match_record rec;
    if (!it) {
        return -REGEX_ERR_INVALID_ARGUMENT;
    }
    if (!match_queue_pop(&it->queue, &rec)) {
        return 0;
    }
    if (start) {
        *start = rec.start;
    }
    if (end) {
        *end = rec.end;
    }
    if (capture_offsets && max_captures >= rec.cap_count) {
        memcpy(capture_offsets, rec.caps, rec.cap_count * sizeof(*capture_offsets));
    }
    if (out_cap_count) {
        *out_cap_count = rec.cap_count / 2;
    }
    free(rec.caps);
    return 1;
}

static regex_err_t safe_regex_iter_last_error(const regex_iter_t *it_base) {
    const regex_iter_safe *it = (const regex_iter_safe *)it_base;
    if (!it) {
        return REGEX_ERR_INVALID_ARGUMENT;
    }
    return it->last_err;
}

static void safe_regex_iter_destroy(regex_iter_t *it_base) {
    regex_iter_safe *it = (regex_iter_safe *)it_base;
    match_record rec;
    if (!it) {
        return;
    }
    while (match_queue_pop(&it->queue, &rec)) {
        free(rec.caps);
    }
    free(it->queue.items);
    free(it->buffer);
    free(it->scratch_set);
    free(it);
}

static void safe_regex_destroy(regex_t *re) {
    safe_regex *sre = (safe_regex *)re->impl;
    if (!sre) {
        return;
    }
    dfa_free(sre->dfa);
    nfa_free(sre->nfa);
    free(sre);
    re->impl = NULL;
}

static regex_err_t safe_regex_compile(regex_t *re, const char *pattern, unsigned flags) {
    parser p;
    regex_node *ast = NULL;
    nfa_prog *prog = NULL;
    dfa_prog *dfa = NULL;
    safe_regex *sre = NULL;
    frag compiled;
    nfa_state *match_state;

    memset(&p, 0, sizeof(p));
    p.pattern = pattern;
    p.len = strlen(pattern);
    p.flags = flags;
    p.extended = (flags & REGEX_FLAG_EXTENDED) != 0;
    p.utf8 = (flags & REGEX_FLAG_UTF8) != 0;
    p.err = REGEX_OK;

    if (flags & REGEX_FLAG_LITERAL) {
        size_t i = 0;
        regex_node *left = NULL;
        while (i < p.len) {
            regex_node *lit;
            uint32_t cp;
            size_t save = p.pos;
            p.pos = i;
            if (!parser_read_codepoint(&p, &cp)) {
                p.pos = save;
                p.err = REGEX_ERR_SYNTAX;
                break;
            }
            i = p.pos;
            lit = node_new(NODE_LITERAL);
            if (!lit) {
                p.err = REGEX_ERR_NOMEM;
                break;
            }
            lit->literal = cp;
            if (!left) {
                left = lit;
            } else {
                regex_node *n = node_new(NODE_CONCAT);
                if (!n) {
                    node_free(lit);
                    p.err = REGEX_ERR_NOMEM;
                    break;
                }
                n->left = left;
                n->right = lit;
                left = n;
            }
        }
        ast = left;
    } else {
        ast = parse_regex(&p);
    }

    if (p.err != REGEX_OK || !ast) {
        node_free(ast);
        return p.err != REGEX_OK ? p.err : REGEX_ERR_SYNTAX;
    }

    if (p.capture_count > re->limits.max_captures) {
        node_free(ast);
        return REGEX_ERR_COMPILE_LIMIT;
    }

    prog = (nfa_prog *)calloc(1, sizeof(*prog));
    if (!prog) {
        node_free(ast);
        return REGEX_ERR_NOMEM;
    }

    compiled = compile_node(prog, ast);
    match_state = nfa_state_new(prog, NFA_MATCH);
    if (!match_state) {
        node_free(ast);
        nfa_free(prog);
        return REGEX_ERR_NOMEM;
    }
    patch(compiled.out, match_state);
    prog->start = compiled.start;
    prog->capture_count = p.capture_count + 1;
    re->capture_count = prog->capture_count;

    node_free(ast);

    if (prog->state_count > re->limits.max_states) {
        nfa_free(prog);
        return REGEX_ERR_COMPILE_LIMIT;
    }

    dfa = dfa_build(prog, flags, re->limits.max_states);
    if (!dfa) {
        nfa_free(prog);
        return REGEX_ERR_NOMEM;
    }

    sre = (safe_regex *)calloc(1, sizeof(*sre));
    if (!sre) {
        dfa_free(dfa);
        nfa_free(prog);
        return REGEX_ERR_NOMEM;
    }

    sre->nfa = prog;
    sre->dfa = dfa;
    sre->capture_count = prog->capture_count;
    sre->flags = flags;
    re->impl = sre;
    return REGEX_OK;
}

static const regex_engine_vtable safe_vtable = {
    .name = "safe",
    .compile = safe_regex_compile,
    .destroy = safe_regex_destroy,
    .match = safe_regex_match,
    .find_all = safe_regex_find_all,
    .replace = safe_regex_replace,
    .split = safe_regex_split,
    .iter_create = safe_regex_iter_create,
    .iter_feed = safe_regex_iter_feed,
    .iter_finish = safe_regex_iter_finish,
    .iter_next = safe_regex_iter_next,
    .iter_last_error = safe_regex_iter_last_error,
    .iter_destroy = safe_regex_iter_destroy
};

const regex_engine_vtable *regex_engine_safe_vtable(void) {
    return &safe_vtable;
}

regex_err_t regex_engine_safe_init(regex_t *re, const char *pattern, unsigned flags) {
    (void)re;
    (void)pattern;
    (void)flags;
    return REGEX_OK;
}

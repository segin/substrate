#include <stdlib.h>
#include <string.h>

#include "regex_internal.h"

/* Upper bound on a single {m,n} repetition count.  POSIX only requires
 * RE_DUP_MAX (>= 255) to be supported; we allow far more but reject the
 * absurd values a{100000000} that would otherwise spin the NFA expansion
 * loop.  The compile-time state budget (regex_limits.max_states) is the
 * real backstop; this just fails such patterns cleanly at parse time. */
#define REGEX_MAX_REPEAT 32767

/* Cap on parser group-nesting and compile-node recursion depth.  A pattern
 * like ((((...)))) recurses parse_regex -> ... -> parse_group -> parse_regex
 * (and later compile_node) once per nesting level; without a cap a deeply
 * nested pattern from a file or config overflows the C stack.  1000 is far
 * beyond any real regex yet nowhere near the userland stack limit. */
#define REGEX_MAX_DEPTH 1000

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
    NODE_BACKREF,
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
    NFA_BACKREF,
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
    size_t max_states;  /* compile-time state budget (0 = unlimited) */
    int failed;         /* set when nfa_state_new hit the budget or OOM */
    int depth;          /* current compile_node recursion depth */
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
    dfa_prog *dfa;          /* NULL when has_backref (DFA can't model backrefs) */
    size_t capture_count;
    unsigned flags;
    int has_backref;        /* pattern uses \1..\9; use the backtracking matcher */
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
    int has_backref;
    int depth;          /* current group-nesting recursion depth */
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

/* Deep-copy a character class.  Needed because a NODE_CLASS subtree can be
 * compiled more than once -- e.g. a counted repeat like [0-9]{4} re-compiles
 * the same node for each repetition -- so the NFA_CLASS state must take an
 * independent copy rather than steal the node's single charclass. */
static regex_charclass *charclass_clone(const regex_charclass *src) {
    regex_charclass *cc;

    if (!src) {
        return NULL;
    }
    cc = charclass_new(src->byte_mode);
    if (!cc) {
        return NULL;
    }
    cc->negated = src->negated;
    memcpy(cc->bitmap, src->bitmap, sizeof(cc->bitmap));
    if (src->range_count > 0) {
        cc->ranges = (regex_range *)malloc(src->range_count * sizeof(*cc->ranges));
        if (!cc->ranges) {
            free(cc);
            return NULL;
        }
        memcpy(cc->ranges, src->ranges, src->range_count * sizeof(*cc->ranges));
        cc->range_count = src->range_count;
        cc->range_cap = src->range_count;
    }
    return cc;
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

    if (!cc) {
        return 0;
    }
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

static regex_node *create_group_node(parser *p, regex_node *n, size_t group_id) {
    regex_node *g = node_new(NODE_GROUP);
    if (!g) {
        node_free(n);
        p->err = REGEX_ERR_NOMEM;
        return NULL;
    }
    g->left = n;
    g->group_id = group_id;
    return g;
}

static regex_node *parse_group_extended(parser *p) {
    regex_node *n;
    size_t group_id;
    parser_get(p);
    /* POSIX: a group is numbered by its opening parenthesis, so the id must be
     * assigned before its contents (which may contain nested groups). */
    group_id = ++p->capture_count;
    n = parse_regex(p);
    if (!n) {
        return NULL;
    }
    if (parser_at_end(p) || parser_get(p) != ')') {
        node_free(n);
        p->err = REGEX_ERR_SYNTAX;
        return NULL;
    }
    return create_group_node(p, n, group_id);
}

static regex_node *parse_group_basic(parser *p) {
    regex_node *n;
    size_t group_id;
    parser_get(p);
    parser_get(p);
    group_id = ++p->capture_count;
    n = parse_regex(p);
    if (!n) {
        return NULL;
    }
    if (parser_at_end(p) || parser_get(p) != '\\' || parser_get(p) != ')') {
        node_free(n);
        p->err = REGEX_ERR_SYNTAX;
        return NULL;
    }
    return create_group_node(p, n, group_id);
}

/* Add the members of a POSIX character class (the NAME in [[:NAME:]]) to
 * cc.  Returns 0 on success, 1 if NAME is not a known class, 2 on an
 * allocation failure. */
static int charclass_add_posix(regex_charclass *cc, const char *name,
                               size_t namelen) {
    struct { const char *name; uint32_t lo[4]; uint32_t hi[4]; int n; } tbl[] = {
        { "alpha",  { 'A', 'a' },       { 'Z', 'z' },       2 },
        { "digit",  { '0' },            { '9' },            1 },
        { "alnum",  { '0', 'A', 'a' },  { '9', 'Z', 'z' },  3 },
        { "upper",  { 'A' },            { 'Z' },            1 },
        { "lower",  { 'a' },            { 'z' },            1 },
        { "space",  { '\t', ' ' },      { '\r', ' ' },      2 },
        { "blank",  { '\t', ' ' },      { '\t', ' ' },      2 },
        { "punct",  { '!', ':', '[', '{' }, { '/', '@', '`', '~' }, 4 },
        { "cntrl",  { 0x00, 0x7f },     { 0x1f, 0x7f },     2 },
        { "xdigit", { '0', 'A', 'a' },  { '9', 'F', 'f' },  3 },
        { "print",  { 0x20 },           { 0x7e },           1 },
        { "graph",  { 0x21 },           { 0x7e },           1 },
    };
    size_t i;
    int j;
    for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); ++i) {
        if (strlen(tbl[i].name) == namelen &&
            memcmp(tbl[i].name, name, namelen) == 0) {
            for (j = 0; j < tbl[i].n; ++j) {
                if (!charclass_add_range(cc, tbl[i].lo[j], tbl[i].hi[j])) {
                    return 2;
                }
            }
            return 0;
        }
    }
    return 1;
}

/* If p is positioned at a POSIX class expression "[:name:]", consume it and
 * add its members to cc.  Returns 1 if one was consumed (p->err is set on a
 * bad/unknown class), 0 if p is not at "[:", leaving p unchanged. */
static int parse_posix_class(parser *p, regex_charclass *cc) {
    size_t start;
    size_t end;
    int rc;

    if (parser_peek(p) != '[' || p->pos + 1 >= p->len ||
        p->pattern[p->pos + 1] != ':') {
        return 0;
    }
    start = p->pos + 2;
    end = start;
    while (end + 1 < p->len &&
           !(p->pattern[end] == ':' && p->pattern[end + 1] == ']')) {
        end++;
    }
    if (end + 1 >= p->len ||
        !(p->pattern[end] == ':' && p->pattern[end + 1] == ']')) {
        /* No closing ":]" - not a POSIX class; treat "[" as a literal. */
        return 0;
    }
    rc = charclass_add_posix(cc, p->pattern + start, end - start);
    if (rc != 0) {
        p->err = rc == 2 ? REGEX_ERR_NOMEM : REGEX_ERR_SYNTAX;
    }
    p->pos = end + 2;
    return 1;
}

static regex_node *parse_charclass(parser *p) {
    regex_node *n;
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
        if (parse_posix_class(p, cc)) {
            if (p->err != REGEX_OK) {
                break;
            }
            continue;
        }
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
                if (lo > REGEX_MAX_CODEPOINT) {
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

static regex_node *parse_escape(parser *p) {
    regex_node *n;
    uint32_t cp;
    uint8_t c;

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
        if (cp > REGEX_MAX_CODEPOINT) {
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
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
        /* Back-reference (POSIX BRE).  ERE has no back-references in the
         * BSD/POSIX dialect, so there \N stays a literal digit. */
        if (!p->extended) {
            size_t group = (size_t)(c - '0');
            if (group > p->capture_count) {
                /* reference to a group that has not been opened */
                p->err = REGEX_ERR_SYNTAX;
                return NULL;
            }
            n = node_new(NODE_BACKREF);
            if (!n) {
                p->err = REGEX_ERR_NOMEM;
                return NULL;
            }
            n->group_id = group;
            p->has_backref = 1;
            return n;
        }
        /* FALLTHROUGH: ERE treats \N as a literal */
        n = node_new(NODE_LITERAL);
        if (!n) {
            p->err = REGEX_ERR_NOMEM;
            return NULL;
        }
        n->literal = c;
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

static regex_node *parse_simple_node(parser *p, node_type_t type) {
    regex_node *n;
    parser_get(p);
    n = node_new(type);
    if (!n) {
        p->err = REGEX_ERR_NOMEM;
    }
    return n;
}

static regex_node *parse_atom(parser *p) {
    regex_node *n = NULL;
    uint8_t c;
    uint32_t cp;

    if (parser_at_end(p)) {
        return NULL;
    }

    c = parser_peek(p);

    if (p->extended && c == '(') {
        return parse_group_extended(p);
    }

    if (!p->extended && c == '\\') {
        size_t save = p->pos;
        parser_get(p);
        if (!parser_at_end(p) && parser_peek(p) == '(') {
            p->pos = save;
            return parse_group_basic(p);
        }
        p->pos = save;
    }

    if (c == '[') {
        return parse_charclass(p);
    }

    if (p->extended && c == '|') {
        return NULL;
    }

    if (c == '.') {
        return parse_simple_node(p, NODE_DOT);
    }

    if (c == '^') {
        return parse_simple_node(p, NODE_BOL);
    }

    if (c == '$') {
        return parse_simple_node(p, NODE_EOL);
    }

    if (c == '\\') {
        return parse_escape(p);
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
            /* Clamp accumulation so an absurd count string cannot overflow
             * size_t; anything past the cap stays just above it and is
             * rejected below. */
            if (min <= REGEX_MAX_REPEAT) {
                min = min * 10 + (parser_get(p) - '0');
            } else {
                parser_get(p);
            }
        }
        max = min;
        if (!parser_at_end(p) && parser_peek(p) == ',') {
            parser_get(p);
            has_max = 1;
            max = 0;
            while (!parser_at_end(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9') {
                if (max <= REGEX_MAX_REPEAT) {
                    max = max * 10 + (parser_get(p) - '0');
                } else {
                    parser_get(p);
                }
                max_set = 1;
            }
        }
        if (parser_at_end(p) || parser_get(p) != '}') {
            p->pos = save;
            return atom;
        }
        if (min > REGEX_MAX_REPEAT || (max_set && max > REGEX_MAX_REPEAT)) {
            node_free(atom);
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
        }
        /* {m,n} with an explicit finite n < m is a malformed bound. */
        if (has_max && max_set && max < min) {
            node_free(atom);
            p->err = REGEX_ERR_SYNTAX;
            return NULL;
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

static regex_node *parse_regex_body(parser *p) {
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

static regex_node *parse_regex(parser *p) {
    regex_node *n;

    if (p->depth >= REGEX_MAX_DEPTH) {
        p->err = REGEX_ERR_COMPILE_LIMIT;
        return NULL;
    }
    p->depth++;
    n = parse_regex_body(p);
    p->depth--;
    return n;
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
    /* Enforce the state budget DURING construction so a pattern whose
     * expansion is huge (nested {m,n}, deep nesting) fails fast instead
     * of building the whole NFA before the post-hoc check.  The `failed`
     * flag propagates out through the frag builders (which return an
     * empty frag) so compile aborts cleanly. */
    if (prog->max_states && prog->state_count >= prog->max_states) {
        prog->failed = 1;
        return NULL;
    }
    if (prog->state_count == prog->state_cap) {
        size_t new_cap = prog->state_cap ? prog->state_cap * 2 : 64;
        nfa_state **new_states = (nfa_state **)realloc(prog->states, new_cap * sizeof(*new_states));
        if (!new_states) {
            prog->failed = 1;
            return NULL;
        }
        prog->states = new_states;
        prog->state_cap = new_cap;
    }
    s = (nfa_state *)calloc(1, sizeof(*s));
    if (!s) {
        prog->failed = 1;
        return NULL;
    }
    s->type = type;
    s->id = (int)prog->state_count;
    prog->states[prog->state_count++] = s;
    return s;
}

/* Empty frag returned when a state allocation fails (prog->failed is set
 * by nfa_state_new).  Every builder short-circuits to this so a NULL
 * state is never dereferenced (list1(&NULL->out) would form a bogus
 * pointer that patch() later writes through). */
static frag frag_none(void) {
    frag f;
    memset(&f, 0, sizeof(f));
    return f;
}

static frag frag_literal(nfa_prog *prog, uint32_t cp) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_CHAR);
    if (!s) {
        return frag_none();
    }
    s->ch = cp;
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_dot(nfa_prog *prog) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_DOT);
    if (!s) {
        return frag_none();
    }
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_class(nfa_prog *prog, regex_charclass *cc) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_CLASS);
    if (!s) {
        if (cc) {
            free(cc->ranges);
            free(cc);
        }
        return frag_none();
    }
    s->charclass = cc;
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_backref(nfa_prog *prog, size_t group_id) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_BACKREF);
    if (!s) {
        return frag_none();
    }
    /* The group this back-reference reproduces; reuses save_slot storage. */
    s->save_slot = (int)group_id;
    f.start = s;
    f.out = list1(&s->out);
    return f;
}

static frag frag_anchor(nfa_prog *prog, nfa_type_t type) {
    frag f;
    nfa_state *s = nfa_state_new(prog, type);
    if (!s) {
        return frag_none();
    }
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
    if (!s) {
        return frag_none();
    }
    s->out = a.start;
    s->out1 = b.start;
    f.start = s;
    f.out = append_list(a.out, b.out);
    return f;
}

static frag frag_star(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    if (!s) {
        return frag_none();
    }
    s->out = a.start;
    patch(a.out, s);
    f.start = s;
    f.out = list1(&s->out1);
    return f;
}

static frag frag_plus(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    if (!s) {
        return frag_none();
    }
    patch(a.out, s);
    s->out = a.start;
    f.start = a.start;
    f.out = list1(&s->out1);
    return f;
}

static frag frag_qmark(nfa_prog *prog, frag a) {
    frag f;
    nfa_state *s = nfa_state_new(prog, NFA_SPLIT);
    if (!s) {
        return frag_none();
    }
    s->out = a.start;
    f.start = s;
    f.out = append_list(a.out, list1(&s->out1));
    return f;
}

static frag frag_group(nfa_prog *prog, frag inner, size_t group_id) {
    frag f;
    nfa_state *s1 = nfa_state_new(prog, NFA_SAVE);
    nfa_state *s2 = nfa_state_new(prog, NFA_SAVE);
    if (!s1 || !s2) {
        return frag_none();
    }
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
    /* Once the state budget is exhausted, stop building: recursive callers
     * and the counted-repeat loops below check prog->failed and unwind
     * instead of spinning out the full (rejected) expansion. */
    if (prog->failed) {
        return frag_none();
    }
    /* Bound recursion so a deeply nested AST cannot overflow the C stack. */
    if (prog->depth >= REGEX_MAX_DEPTH) {
        prog->failed = 1;
        return frag_none();
    }
    prog->depth++;

    switch (n->type) {
    case NODE_EMPTY:
        {
            nfa_state *s = nfa_state_new(prog, NFA_EPSILON);
            if (!s) {
                f = frag_none();
                break;
            }
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
        /* Clone, don't steal: a NODE_CLASS under a counted repeat is compiled
         * once per repetition, so the node must keep its master charclass for
         * the next pass (it is freed with the node at node_free time). */
        f = frag_class(prog, charclass_clone(n->charclass));
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
            if (prog->failed) {
                break;
            }
            f = frag_concat(f, compile_node(prog, n->left));
        }
        if (max == SIZE_MAX) {
            f = frag_concat(f, frag_star(prog, compile_node(prog, n->left)));
            break;
        }
        if (max > n->rep_min) {
            size_t extra;
            for (extra = n->rep_min; extra < max; ++extra) {
                if (prog->failed) {
                    break;
                }
                f = frag_concat(f, frag_qmark(prog, compile_node(prog, n->left)));
            }
        }
        break;
    case NODE_GROUP:
        f = frag_group(prog, compile_node(prog, n->left), n->group_id);
        break;
    case NODE_BACKREF:
        f = frag_backref(prog, n->group_id);
        break;
    default:
        break;
    }

    prog->depth--;
    return f;
}

static void nfa_free(nfa_prog *prog) {
    size_t i;
    if (!prog) {
        return;
    }
    for (i = 0; i < prog->state_count; ++i) {
        nfa_state *s = prog->states[i];
        if (s) {
            /* A NFA_CLASS state owns the regex_charclass transferred from the
             * AST (see ast->nfa conversion, which nulls the AST pointer), so
             * it must release it here -- node_free() no longer can. */
            if (s->charclass) {
                free(s->charclass->ranges);
                free(s->charclass);
            }
            free(s);
        }
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

/* Does a `^` anchor match at pos?  Start of text counts unless REG_NOTBOL
 * (REGEX_FLAG_NOTBOL) says the buffer start is mid-line; an interior line
 * start counts only in multiline mode. */
static int anchor_at_bol(unsigned flags, size_t pos, const char *text) {
    if (pos == 0) {
        return !(flags & REGEX_FLAG_NOTBOL);
    }
    return (flags & REGEX_FLAG_MULTILINE) && text && text[pos - 1] == '\n';
}

/* Does a `$` anchor match at pos?  End of text counts unless REG_NOTEOL
 * (REGEX_FLAG_NOTEOL); an interior line end counts only in multiline mode. */
static int anchor_at_eol(unsigned flags, size_t pos, size_t text_len, const char *text) {
    if (pos == text_len) {
        return !(flags & REGEX_FLAG_NOTEOL);
    }
    return (flags & REGEX_FLAG_MULTILINE) && text && text[pos] == '\n';
}

static void epsilon_closure(nfa_prog *prog, uint8_t *set, size_t start_id, size_t pos,
                            const char *text, size_t text_len, unsigned flags, int ignore_anchors) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
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
            if (ignore_anchors || anchor_at_bol(flags, pos, text)) {
                if (s->out) {
                    PUSH_STACK(s->out->id);
                }
            }
            break;
        case NFA_EOL:
            if (ignore_anchors || anchor_at_eol(flags, pos, text_len, text)) {
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
#pragma GCC diagnostic pop
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
        if (anchor_at_bol(flags, pos, text)) {
            return add_state(list, s->out, caps, cap_count, list_id, pos, text, text_len, flags, out_err);
        }
        free(caps);
        return 1;
    case NFA_EOL:
        if (anchor_at_eol(flags, pos, text_len, text)) {
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

/* Copy as many capture offsets as fit in the caller's buffer. A buffer
 * smaller than the group count receives the leading offsets it can hold
 * (POSIX regexec style) rather than being left uninitialized while the
 * caller is told the full count was produced. */
static void copy_capture_offsets(size_t *dst, size_t dst_cap,
                                 const size_t *src, size_t src_count) {
    size_t n = dst_cap < src_count ? dst_cap : src_count;
    if (dst && src && n) {
        memcpy(dst, src, n * sizeof(*dst));
    }
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
    /* Greedy tracking: record the last (longest) match found */
    size_t best_match_pos = (size_t)-1;
    size_t *best_match_caps = NULL;

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
    /* The per-thread visited marker (nfa_state.last_list_id) lives on the
     * shared NFA program, but list_id is a per-call counter that restarts at
     * 1 every match.  Without clearing the stamps left by a previous match on
     * this same compiled regex, the new call's low list_id values collide with
     * those stale stamps and add_state() drops the threads -- so a regex would
     * match the first input string and then never again.  Reset the stamps so
     * each match starts clean. */
    for (size_t gi = 0; gi < prog->state_count; ++gi) {
        prog->states[gi]->last_list_id = 0;
    }
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
                /* Greedy: record this match but continue to find a longer one */
                size_t *clone = caps_clone(clist.threads[idx].caps, cap_count);
                if (clone) {
                    free(best_match_caps);
                    best_match_caps = clone;
                    best_match_pos  = local_pos;
                }
                /* Fall through: let char-processing phase free this thread's caps */
            }
        }

        if (local_pos >= text_len) {
            break;
        }

        if (steps++ > re->limits.match_steps) {
            if (out_err) {
                *out_err = REGEX_ERR_MATCH_TIMEOUT;
            }
            free(best_match_caps);
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
                    free(best_match_caps);
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
                            free(best_match_caps);
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
                            free(best_match_caps);
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
                            free(best_match_caps);
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

    thread_list_free(&clist);
    thread_list_free(&nlist);

    if (best_match_pos != (size_t)-1) {
        if (capture_offsets && best_match_caps) {
            copy_capture_offsets(capture_offsets, max_captures, best_match_caps, cap_count);
            if (max_captures >= 2 && cap_count >= 2) {
                capture_offsets[1] = best_match_pos;
            }
        }
        free(best_match_caps);
        if (out_err) {
            *out_err = REGEX_OK;
        }
        return (ssize_t)prog->capture_count;
    }

    free(best_match_caps);
    if (out_err) {
        *out_err = REGEX_OK;
    }
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

static ssize_t safe_regex_backtrack(const regex_t *re, const char *text, size_t text_len,
                                    size_t *capture_offsets, size_t max_captures,
                                    regex_err_t *out_err);

static ssize_t safe_regex_match_internal(const regex_t *re, const char *text, size_t text_len,
                                         size_t *capture_offsets, size_t max_captures,
                                         regex_err_t *out_err,
                                         uint8_t *scratch_set, size_t scratch_cap) {
    safe_regex *sre = (safe_regex *)re->impl;
    size_t cap_count;
    size_t start_pos = 0;
    int anchored = (re->flags & REGEX_FLAG_ANCHORED) != 0;

    if (!sre || !sre->nfa) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }
    /* Back-reference patterns have no DFA; route them to the backtracker so
     * find_all() and the iterator (which call this directly) also work. */
    if (sre->has_backref) {
        return safe_regex_backtrack(re, text, text_len, capture_offsets,
                                    max_captures, out_err);
    }
    if (!sre->dfa) {
        if (out_err) {
            *out_err = REGEX_ERR_INTERNAL;
        }
        return -REGEX_ERR_INTERNAL;
    }
    cap_count = sre->capture_count * 2;

    if (capture_offsets) {
        size_t i;
        size_t n = max_captures < cap_count ? max_captures : cap_count;
        for (i = 0; i < n; ++i) {
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

/* -------------------------------------------------------------------------
 * Backtracking matcher.
 *
 * Back-references (\1..\9) make the language non-regular, so the DFA/Pike-VM
 * path cannot handle them.  Patterns that use a back-reference are matched by
 * this recursive backtracker over the same compiled NFA (which carries an
 * NFA_BACKREF state).  A global step budget (re->limits.match_steps) bounds
 * catastrophic backtracking, and a recursion-depth cap keeps the user stack
 * bounded on greedy loops; exceeding either yields a clean MATCH_TIMEOUT
 * rather than a hang or crash.
 * ---------------------------------------------------------------------- */
#define BT_MAX_DEPTH 40000u

typedef struct bt_ctx {
    const char *text;
    size_t len;
    unsigned flags;
    size_t *caps;
    size_t ncaps;
    size_t steps;
    size_t max_steps;
    int timed_out;
} bt_ctx;

static int bt_decode(bt_ctx *c, size_t pos, uint32_t *cp, size_t *adv) {
    if (c->flags & REGEX_FLAG_UTF8) {
        size_t i = pos;
        if (!regex_utf8_decode(c->text, c->len, &i, cp)) {
            return 0;
        }
        *adv = (i > pos) ? (i - pos) : 1;
        return 1;
    }
    *cp = (uint8_t)c->text[pos];
    *adv = 1;
    return 1;
}

static int nfa_bt(bt_ctx *c, nfa_state *s, size_t pos, size_t depth) {
    if (depth > BT_MAX_DEPTH) {
        c->timed_out = 1;
        return 0;
    }
    for (;;) {
        if (c->steps++ > c->max_steps) {
            c->timed_out = 1;
            return 0;
        }
        switch (s->type) {
        case NFA_MATCH:
            c->caps[1] = pos;           /* end of group 0 (whole match) */
            return 1;
        case NFA_EPSILON:
            s = s->out;
            continue;
        case NFA_CHAR: {
            uint32_t cp;
            size_t adv;
            if (pos >= c->len || !bt_decode(c, pos, &cp, &adv)) {
                return 0;
            }
            if (!match_char(s->ch, cp, c->flags)) {
                return 0;
            }
            pos += adv;
            s = s->out;
            continue;
        }
        case NFA_DOT: {
            uint32_t cp;
            size_t adv;
            if (pos >= c->len || !bt_decode(c, pos, &cp, &adv)) {
                return 0;
            }
            if (!(c->flags & REGEX_FLAG_DOTALL) && regex_is_newline(cp)) {
                return 0;
            }
            pos += adv;
            s = s->out;
            continue;
        }
        case NFA_CLASS: {
            uint32_t cp;
            size_t adv;
            if (pos >= c->len || !bt_decode(c, pos, &cp, &adv)) {
                return 0;
            }
            if (!match_class_char(s->charclass, cp, c->flags)) {
                return 0;
            }
            pos += adv;
            s = s->out;
            continue;
        }
        case NFA_BOL:
            if (anchor_at_bol(c->flags, pos, c->text)) {
                s = s->out;
                continue;
            }
            return 0;
        case NFA_EOL:
            if (anchor_at_eol(c->flags, pos, c->len, c->text)) {
                s = s->out;
                continue;
            }
            return 0;
        case NFA_SPLIT:
            if (nfa_bt(c, s->out, pos, depth + 1)) {
                return 1;
            }
            if (c->timed_out) {
                return 0;
            }
            s = s->out1;
            continue;
        case NFA_SAVE: {
            int slot = s->save_slot;
            size_t saved = 0;
            int in_range = slot >= 0 && (size_t)slot < c->ncaps;
            if (in_range) {
                saved = c->caps[slot];
                c->caps[slot] = pos;
            }
            if (nfa_bt(c, s->out, pos, depth + 1)) {
                return 1;
            }
            if (in_range) {
                c->caps[slot] = saved;      /* restore on backtrack */
            }
            return 0;
        }
        case NFA_BACKREF: {
            size_t g = (size_t)s->save_slot;
            size_t so, eo, glen, k;
            if (2 * g + 1 >= c->ncaps) {
                return 0;
            }
            so = c->caps[2 * g];
            eo = c->caps[2 * g + 1];
            if (so == (size_t)-1 || eo == (size_t)-1 || eo < so) {
                /* group did not participate -> matches the empty string */
                s = s->out;
                continue;
            }
            glen = eo - so;
            if (pos + glen > c->len) {
                return 0;
            }
            for (k = 0; k < glen; ++k) {
                uint32_t a = (uint8_t)c->text[so + k];
                uint32_t b = (uint8_t)c->text[pos + k];
                if (c->flags & REGEX_FLAG_ICASE) {
                    a = regex_ascii_tolower(a);
                    b = regex_ascii_tolower(b);
                }
                if (a != b) {
                    return 0;
                }
            }
            pos += glen;
            s = s->out;
            continue;
        }
        default:
            return 0;
        }
    }
}

static ssize_t safe_regex_backtrack(const regex_t *re, const char *text, size_t text_len,
                                    size_t *capture_offsets, size_t max_captures,
                                    regex_err_t *out_err) {
    safe_regex *sre = (safe_regex *)re->impl;
    size_t cap_count = sre->capture_count * 2;
    size_t *caps;
    bt_ctx c;
    size_t start;
    int anchored = (re->flags & REGEX_FLAG_ANCHORED) != 0;

    caps = (size_t *)malloc(cap_count * sizeof(*caps));
    if (!caps) {
        if (out_err) {
            *out_err = REGEX_ERR_NOMEM;
        }
        return -REGEX_ERR_NOMEM;
    }
    c.text = text;
    c.len = text_len;
    c.flags = re->flags;
    c.caps = caps;
    c.ncaps = cap_count;
    c.steps = 0;
    c.max_steps = re->limits.match_steps;
    c.timed_out = 0;

    for (start = 0; start <= text_len;) {
        size_t i;
        for (i = 0; i < cap_count; ++i) {
            caps[i] = (size_t)-1;
        }
        caps[0] = start;
        if (nfa_bt(&c, sre->nfa->start, start, 0)) {
            if (capture_offsets) {
                copy_capture_offsets(capture_offsets, max_captures, caps, cap_count);
            }
            free(caps);
            if (out_err) {
                *out_err = REGEX_OK;
            }
            return (ssize_t)sre->capture_count;
        }
        if (c.timed_out) {
            free(caps);
            if (out_err) {
                *out_err = REGEX_ERR_MATCH_TIMEOUT;
            }
            return -REGEX_ERR_MATCH_TIMEOUT;
        }
        if (anchored || sre->nfa->uses_bol) {
            break;
        }
        if (re->flags & REGEX_FLAG_UTF8) {
            size_t idx = start;
            uint32_t cp;
            if (!regex_utf8_decode(text, text_len, &idx, &cp) || idx == start) {
                start++;
            } else {
                start = idx;
            }
        } else {
            start++;
        }
    }
    free(caps);
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

    if (sre->has_backref) {
        return safe_regex_backtrack(re, text, text_len, capture_offsets,
                                    max_captures, out_err);
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
                                         size_t idx, size_t base) {
    size_t start;
    size_t end;
    if (idx >= cap_count) {
        /* A $N referencing a group the pattern does not have expands to
         * nothing, as POSIX/ed/sed do - it must not abort the replace. */
        return REGEX_OK;
    }
    start = caps[2 * idx];
    end = caps[2 * idx + 1];
    /* caps are relative to the current match start (text + base); an unset
     * group is the SIZE_MAX sentinel and must be tested before shifting to
     * absolute offsets into text. */
    if (start == (size_t)-1 || end == (size_t)-1 || end < start) {
        return REGEX_OK;
    }
    start += base;
    end += base;
    if (end > text_len) {
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

    /* End offset of the previous SUBSTITUTED match; SIZE_MAX = none yet.
     * Used to suppress a degenerate empty match sitting exactly at the
     * end of the previous match (sed/perl semantics) - otherwise an
     * empty-matchable pattern double-substitutes and/or drops the text
     * between matches. */
    size_t prev_end = (size_t)-1;

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

            /* Degenerate empty match adjacent to the previous match's
             * end: don't substitute again; emit one char and advance so
             * the loop makes progress without losing text. */
            if (start == end && start == prev_end) {
                if (pos < text_len) {
                    if (!ensure_capacity(&out, &out_cap, *out_len_ptr + 2)) {
                        free(caps);
                        free(out);
                        free(scratch_set);
                        return REGEX_ERR_NOMEM;
                    }
                    out[(*out_len_ptr)++] = text[pos];
                }
                pos++;
                continue;
            }

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
                    err = replace_append_capture(&out, out_len_ptr, &out_cap, text, text_len, caps, cap_count / 2, idx, pos);
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

            prev_end = end;
            pos = end;
            if (!global) {
                break;
            }
            if (end == start) {
                /* Zero-width match: emit the char we skip over so it is
                 * not dropped from the output. */
                if (pos < text_len) {
                    if (!ensure_capacity(&out, &out_cap, *out_len_ptr + 2)) {
                        free(caps);
                        free(out);
                        free(scratch_set);
                        return REGEX_ERR_NOMEM;
                    }
                    out[(*out_len_ptr)++] = text[pos];
                }
                pos++;
            }
        }
    }

    {
        /* A zero-width match at end-of-text advances pos to text_len+1
         * (2589-2590), so guard the subtraction: an unguarded
         * text_len - pos underflows to SIZE_MAX and memcpy overflows. */
        size_t rest = pos < text_len ? text_len - pos : 0;
        if (!ensure_capacity(&out, &out_cap, *out_len_ptr + rest + 1)) {
            free(caps);
            free(out);
            free(scratch_set);
            return REGEX_ERR_NOMEM;
        }
        memcpy(out + *out_len_ptr, text + pos, rest);
        *out_len_ptr += rest;
        out[*out_len_ptr] = '\0';
    }

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
        size_t old_cap = q->cap;
        size_t new_cap = old_cap ? old_cap * 2 : 16;
        match_record *new_items = (match_record *)realloc(q->items, new_cap * sizeof(*new_items));
        if (!new_items) {
            return 0;
        }
        q->items = new_items;
        q->cap = new_cap;
        /* The ring was full, so head == tail and the logical order wraps:
         * items[head..old_cap) then items[0..head). Relocate the wrapped
         * prefix [0..head) into the freed upper half so the queue is
         * contiguous; otherwise the push below (and later pops) overwrite
         * and reorder live records once the queue grows past its first cap. */
        if (old_cap > 0 && q->head > 0) {
            memcpy(q->items + old_cap, q->items, q->head * sizeof(*q->items));
            q->tail = q->head + old_cap;
        } else {
            q->tail = old_cap;
        }
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

    /* Drop the consumed prefix (everything below scan_pos) so the buffer
     * and the max_stream_buffer budget track only the unconsumed tail, not
     * the whole stream. base_offset carries the trimmed byte count so the
     * absolute offsets already queued and future ones stay correct. Without
     * this, buf_len grows to the cap and every stream longer than
     * max_stream_buffer eventually fails even after emitting matches. */
    {
        size_t trim = it->scan_pos;
        if (trim > it->buf_len) {
            trim = it->buf_len;
        }
        if (trim > 0) {
            size_t remaining = it->buf_len - trim;
            if (remaining > 0) {
                memmove(it->buffer, it->buffer + trim, remaining);
            }
            it->base_offset += trim;
            it->buf_len = remaining;
            it->scan_pos -= trim;
            if (it->buffer) {
                it->buffer[it->buf_len] = '\0';
            }
        }
    }
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
    if (capture_offsets) {
        copy_capture_offsets(capture_offsets, max_captures, rec.caps, rec.cap_count);
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
    /* Enforce the state budget during construction (nfa_state_new), so a
     * pattern whose NFA expansion is enormous - nested {m,n}, deeply
     * nested groups - fails fast instead of allocating it all first. */
    prog->max_states = re->limits.max_states;

    compiled = compile_node(prog, ast);
    if (prog->failed) {
        node_free(ast);
        nfa_free(prog);
        return REGEX_ERR_COMPILE_LIMIT;
    }
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

    /* A back-reference makes the language non-regular, so the DFA span
     * pre-filter cannot represent it; such patterns are matched purely by the
     * backtracking matcher (safe_regex_backtrack) and need no DFA. */
    if (!p.has_backref) {
        dfa = dfa_build(prog, flags, re->limits.max_states);
        if (!dfa) {
            nfa_free(prog);
            return REGEX_ERR_NOMEM;
        }
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
    sre->has_backref = p.has_backref;
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

#ifndef SLEX_REGEX_H
#define SLEX_REGEX_H

/* NFA transition labels besides plain bytes. */
#define EPSILON -1
#define ANY_CHAR -2

/* NFA State */
struct nfa_state {
    int c;                      /* Character to match, EPSILON, or ANY_CHAR */
    struct nfa_state *out1;     /* First transition */
    struct nfa_state *out2;     /* Second transition (for split states) */
    int accept;                 /* Non-zero if accepting state, value = rule number */
};

/* NFA Fragment (used during construction) */
struct nfa_frag {
    struct nfa_state *start;
    struct nfa_state ***out;    /* Dangling arrows to patch (pointers to out1/out2 fields) */
    int out_count;
};

struct nfa_state *nfa_state_create(int c);

/*
 * Compile an ERE (with lex's "string", {name} and r/s extensions) to an
 * NFA whose accepting state carries rule_id.  Exits on a malformed
 * pattern.
 */
struct nfa_frag *regex_compile(const char *pattern, int rule_id);

#endif

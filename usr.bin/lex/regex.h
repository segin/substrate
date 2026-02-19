#ifndef LEX_REGEX_H
#define LEX_REGEX_H

#include <stdbool.h>

/* NFA State */
#define EPSILON -1
#define ANY_CHAR -2

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

/* Rule structure */
struct rule {
    int id;
    char *pattern;              /* Original pattern string */
    char *action;               /* C action code */
    struct nfa_state *nfa;      /* Compiled NFA start state */
    char **start_conditions;    /* Array of start condition names, NULL = all */
    int sc_count;
    bool has_bol;               /* ^ anchor */
    bool has_eol;               /* $ anchor */
    struct rule *next;
};

/* Regex API */
struct nfa_state *nfa_state_create(int c);
struct nfa_frag *regex_compile(const char *pattern, int rule_id);

/* Rule management */
void add_rule(const char *pattern, const char *action, char **start_conds, int sc_count);
struct rule *get_rules(void);

#endif

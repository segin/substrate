#ifndef SLEX_RULES_H
#define SLEX_RULES_H

#include <stdbool.h>

#include "regex.h"

/* One rule from the rules section. */
struct rule {
    int id;
    char *pattern;              /* Original pattern string */
    char *action;               /* C action code, NULL for the default */
    struct nfa_state *nfa;      /* Compiled NFA start state */
    char **start_conditions;    /* Start condition names; sc_count 0 = all */
    int sc_count;
    bool has_bol;               /* ^ anchor */
    bool has_eol;               /* $ anchor */
    struct rule *next;
};

/* Compile a rule's pattern and add it; rules get ids 1, 2, ... in order. */
void add_rule(const char *pattern, const char *action, char **start_conds,
              int sc_count);

/* All rules, most recently added first. */
struct rule *get_rules(void);

#endif

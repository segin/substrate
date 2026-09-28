#ifndef LEX_DFA_H
#define LEX_DFA_H

#include "regex.h"

/* DFA State */
struct dfa_state {
    int id;
    int transitions[256];       /* Next state for each input byte */
    int accept;                 /* Primary rule ID (highest priority) */
    int *accept_rules;          /* All rule IDs that accept in this state */
    int accept_count;
    struct dfa_state *next;     /* Linked list for all states */
};

/* DFA */
struct dfa {
    int *start_states;          /* DFA state IDs for each start condition */
    int num_start_states;       /* Number of start conditions (including INITIAL) */
    struct dfa_state *states;   /* All states */
    int num_states;
};

/* Build DFA from all rules */
struct dfa *nfa_to_dfa(void);

/* Free DFA */
void dfa_free(struct dfa *d);

#endif

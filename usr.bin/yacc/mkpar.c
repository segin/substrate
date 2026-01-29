/*
 * mkpar.c - Parser Table Construction
 *
 * Builds ACTION and GOTO tables from LALR(1) states.
 * Handles conflict detection and resolution using precedence/associativity.
 */

#include "defs.h"

/* Parser table storage */
action **parser;            /* ACTION table: parser[state] -> list of actions */
int SRtotal;                /* Total shift/reduce conflicts */
int RRtotal;                /* Total reduce/reduce conflicts */
int SRexpect;               /* Expected S/R conflicts from %expect */
short *SRconflicts;         /* Per-state S/R conflict count */
short *RRconflicts;         /* Per-state R/R conflict count */
short final_state;          /* Accept state */

static action *parse_actions(int stateno);
static action *add_reduce(action *alist, int ruleno, short *la);
static action *add_shift(action *alist, int symbol, int state);
static void find_final_state(void);
static void remove_conflicts(void);
static void resolve_conflict(int state, action *a, action *b);
static void total_conflicts(void);
static void defreds(void);
static void free_action_row(action *a);

void make_parser(void) {
    int i;
    
    parser = (action **)malloc(nstates * sizeof(action *));
    SRconflicts = (short *)calloc(nstates, sizeof(short));
    RRconflicts = (short *)calloc(nstates, sizeof(short));
    if (parser == NULL || SRconflicts == NULL || RRconflicts == NULL)
        no_space();
    
    /* Build action lists for each state */
    for (i = 0; i < nstates; i++) {
        parser[i] = parse_actions(i);
    }
    
    find_final_state();
    remove_conflicts();
    total_conflicts();
    defreds();
}

static action *parse_actions(int stateno) {
    action *alist = NULL;
    
    /* Add shift actions from shifts for this state */
    /* Add reduce actions from reductions for this state with lookaheads */
    
    return alist;
}

static action *add_shift(action *alist, int symbol, int state) {
    action *a;
    
    a = (action *)malloc(sizeof(action));
    if (a == NULL) no_space();
    
    a->next = alist;
    a->symbol = symbol;
    a->number = state;
    a->prec = 0;        /* Shifts don't have precedence */
    a->action_code = SHIFT;
    a->assoc = NO_ASSOC;
    a->suppressed = 0;
    
    return a;
}

static action *add_reduce(action *alist, int ruleno, short *la) {
    action *a;
    int i;
    
    /* Add reduce action for each lookahead in la */
    for (i = 0; i < ntokens; i++) {
        /* Check if token i is in lookahead set */
        /* if (TESTBIT(la, i)) { ... } */
        
        a = (action *)malloc(sizeof(action));
        if (a == NULL) no_space();
        
        a->next = alist;
        a->symbol = i;
        a->number = ruleno;
        a->prec = 0;  /* Get from rule's precedence */
        a->action_code = REDUCE;
        a->assoc = NO_ASSOC;
        a->suppressed = 0;
        
        alist = a;
    }
    
    return alist;
}

static void find_final_state(void) {
    /* Find the accept state: state reached after shifting start symbol */
    /* The accept state reduces by rule 0: $accept -> start $end */
    final_state = -1;
    
    /* Search for state where we've recognized the goal */
}

static void remove_conflicts(void) {
    int i;
    action *a, *b;
    
    SRtotal = 0;
    RRtotal = 0;
    
    for (i = 0; i < nstates; i++) {
        a = parser[i];
        while (a != NULL) {
            b = a->next;
            while (b != NULL) {
                if (a->symbol == b->symbol) {
                    /* Conflict on same symbol */
                    resolve_conflict(i, a, b);
                }
                b = b->next;
            }
            a = a->next;
        }
    }
}

static void resolve_conflict(int state, action *a, action *b) {
    /* Apply precedence and associativity rules */
    
    if (a->action_code == SHIFT && b->action_code == REDUCE) {
        /* Shift/reduce conflict */
        if (a->prec > 0 && b->prec > 0) {
            if (a->prec > b->prec) {
                b->suppressed = 1;  /* Suppress reduce */
            } else if (a->prec < b->prec) {
                a->suppressed = 1;  /* Suppress shift */
            } else {
                /* Equal precedence: use associativity */
                switch (a->assoc) {
                case LEFT_ASSOC:
                    a->suppressed = 1;  /* Reduce */
                    break;
                case RIGHT_ASSOC:
                    b->suppressed = 1;  /* Shift */
                    break;
                case NON_ASSOC:
                    /* Both suppressed -> error */
                    a->suppressed = 1;
                    b->suppressed = 1;
                    break;
                default:
                    /* No associativity: default to shift */
                    b->suppressed = 2;  /* Suppressed with warning */
                    SRconflicts[state]++;
                    SRtotal++;
                    break;
                }
            }
        } else {
            /* No precedence declared: default shift */
            b->suppressed = 2;
            SRconflicts[state]++;
            SRtotal++;
        }
    } else if (a->action_code == REDUCE && b->action_code == REDUCE) {
        /* Reduce/reduce conflict: first rule wins */
        if (a->number < b->number) {
            b->suppressed = 1;
        } else {
            a->suppressed = 1;
        }
        RRconflicts[state]++;
        RRtotal++;
    }
}

static void total_conflicts(void) {
    /* Report conflicts to stderr */
    if (SRtotal > SRexpect || RRtotal > 0) {
        fprintf(stderr, "conflicts:");
        if (SRtotal > 0)
            fprintf(stderr, " %d shift/reduce", SRtotal);
        if (RRtotal > 0)
            fprintf(stderr, " %d reduce/reduce", RRtotal);
        fprintf(stderr, "\n");
    }
}

static void defreds(void) {
    /* Compute default reductions for each state */
    /* If a state has only one reduce action, it can be the default */
}

static void free_action_row(action *a) {
    action *next;
    while (a != NULL) {
        next = a->next;
        free(a);
        a = next;
    }
}

void free_parser(void) {
    int i;
    for (i = 0; i < nstates; i++) {
        free_action_row(parser[i]);
    }
    free(parser);
    free(SRconflicts);
    free(RRconflicts);
}

/*
 * mkpar.c - Parser Table Construction
 *
 * Builds ACTION and GOTO tables from LALR(1) states.
 * Handles conflict detection and resolution using precedence/associativity.
 */

#include "defs.h"

#define BITS_PER_WORD 32
#define WORDSIZE(n) (((n) + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define TESTBIT(a, i) ((a)[(i) / BITS_PER_WORD] & (1U << ((i) % BITS_PER_WORD)))

/* Parser table storage */
action **parser;            /* ACTION table: parser[state] -> list of actions */
int SRtotal;                /* Total shift/reduce conflicts */
int RRtotal;                /* Total reduce/reduce conflicts */
int SRexpect;               /* Expected S/R conflicts from %expect */
short *SRconflicts;         /* Per-state S/R conflict count */
short *RRconflicts;         /* Per-state R/R conflict count */
short final_state;          /* Accept state */
static short *symbol_prec;  /* Precedence by symbol index */
static char *symbol_assoc;  /* Associativity by symbol index */
static short *rule_prec;    /* Precedence by rule number */
static char *rule_assoc;    /* Associativity by rule number */

static action *parse_actions(int stateno);
static action *add_reduce(action *alist, int ruleno, unsigned *la);
static action *add_shift(action *alist, int symbol, int state);
static void find_final_state(void);
static void remove_conflicts(void);
static void resolve_conflict(int state, action *a, action *b);
static void total_conflicts(void);
static void defreds(void);
static void build_goto_table(void);
static void build_yytable(void);
static void free_action_row(action *a);
static void build_precedence_tables(void);

void make_parser(void) {
    int i;
    
    parser = (action **)malloc(nstates * sizeof(action *));
    SRconflicts = (short *)calloc(nstates, sizeof(short));
    RRconflicts = (short *)calloc(nstates, sizeof(short));
    if (parser == NULL || SRconflicts == NULL || RRconflicts == NULL)
        no_space();

    build_precedence_tables();
    
    /* Build action lists for each state */
    for (i = 0; i < nstates; i++) {
        parser[i] = parse_actions(i);
    }
    
    find_final_state();
    remove_conflicts();
    total_conflicts();
    defreds();
    build_goto_table();
    build_yytable();
}

static void build_precedence_tables(void) {
    int i, j;
    bucket *bp;

    symbol_prec = (short *)calloc(nsyms, sizeof(short));
    symbol_assoc = (char *)calloc(nsyms, sizeof(char));
    rule_prec = (short *)calloc(nrules, sizeof(short));
    rule_assoc = (char *)calloc(nrules, sizeof(char));
    if (symbol_prec == NULL || symbol_assoc == NULL ||
        rule_prec == NULL || rule_assoc == NULL)
        no_space();

    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->index >= 0 && bp->index < nsyms) {
            symbol_prec[bp->index] = bp->prec;
            symbol_assoc[bp->index] = bp->assoc;
        }
    }

    for (i = 1; i < nrules; i++) {
        for (j = rrhs[i]; ritem[j] >= 0; j++) {
            int sym = ritem[j];
            if (sym >= 0 && sym < ntokens && symbol_prec[sym] > 0) {
                rule_prec[i] = symbol_prec[sym];
                rule_assoc[i] = symbol_assoc[sym];
            }
        }
    }
}


static action *parse_actions(int stateno) {
    action *alist = NULL;
    shifts *sp;
    reductions *rp;
    int i;
    int red_idx;
    int nwords;

    nwords = WORDSIZE(ntokens);
    
    /* Find shifts for this state */
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number == stateno) {
            /* Add a shift action for each transition */
            for (i = 0; i < sp->nshifts; i++) {
                int dest = sp->shift[i];
                int symbol = accessing_symbol[dest];
                if (symbol < ntokens) {  /* Terminal = shift */
                    alist = add_shift(alist, symbol, dest);
                }
            }
            break;
        }
    }
    
    /* Find reductions for this state */
    red_idx = 0;
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        for (i = 0; i < rp->nreds; i++) {
            if (rp->number == stateno && red_idx < lalr_nreductions) {
                unsigned *la = lalr_LA + red_idx * nwords;
                alist = add_reduce(alist, rp->rules[i], la);
            }
            red_idx++;
        }
    }
    
    return alist;
}


static action *add_shift(action *alist, int symbol, int state) {
    action *a;
    
    a = (action *)malloc(sizeof(action));
    if (a == NULL) no_space();
    
    a->next = alist;
    a->symbol = symbol;
    a->number = state;
    a->prec = (symbol >= 0 && symbol < nsyms) ? symbol_prec[symbol] : 0;
    a->action_code = SHIFT;
    a->assoc = (symbol >= 0 && symbol < nsyms) ? symbol_assoc[symbol] : NO_ASSOC;
    a->suppressed = 0;
    
    return a;
}

static action *add_reduce(action *alist, int ruleno, unsigned *la) {
    action *a;
    int i;
    
    /* Add reduce action for each lookahead in la */
    for (i = 0; i < ntokens; i++) {
        if (TESTBIT(la, i)) {
            a = (action *)malloc(sizeof(action));
            if (a == NULL) no_space();
            
            a->next = alist;
            a->symbol = i;
            a->number = ruleno;
            a->prec = (ruleno >= 0 && ruleno < nrules) ? rule_prec[ruleno] : 0;
            a->action_code = REDUCE;
            a->assoc = (ruleno >= 0 && ruleno < nrules) ? rule_assoc[ruleno] : NO_ASSOC;
            a->suppressed = 0;
            
            alist = a;
        }
    }
    
    return alist;
}

short *yydgoto;
short *yygindex;
static short *goto_base;

static void find_final_state(void) {
    int i;
    shifts *sp;
    
    /* State 0 shifts on goal_symbol to final_state */
    final_state = -1;
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number == 0) {
            for (i = 0; i < sp->nshifts; i++) {
                int dest = sp->shift[i];
                if (accessing_symbol[dest] == goal_symbol->index) {
                    final_state = dest;
                    return;
                }
            }
        }
    }
}

static void build_goto_table(void) {
    int i, j;
    shifts *sp;
    
    yydgoto = (short *)calloc(nvars, sizeof(short));
    yygindex = (short *)calloc(nvars, sizeof(short));
    if (yydgoto == NULL || yygindex == NULL) no_space();
    
    /* For each nonterminal choose the most common destination as default goto. */
    for (i = 0; i < nvars; i++) {
        int sym = ntokens + i;
        int best_dest = -1;
        int best_count = 0;
        int *dest_freq = (int *)calloc(nstates, sizeof(int));
        if (dest_freq == NULL) no_space();

        for (sp = first_shift; sp != NULL; sp = sp->next) {
            for (j = 0; j < sp->nshifts; j++) {
                int dest = sp->shift[j];
                if (accessing_symbol[dest] == sym) {
                    dest_freq[dest]++;
                    if (dest_freq[dest] > best_count ||
                        (dest_freq[dest] == best_count &&
                         (best_dest < 0 || dest < best_dest))) {
                        best_count = dest_freq[dest];
                        best_dest = dest;
                    }
                }
            }
        }

        if (best_dest < 0) {
            best_dest = 0;
        }
        yydgoto[i] = best_dest;
        free(dest_freq);
    }
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
    action *shift_act = NULL;
    action *reduce_act = NULL;

    /* Apply precedence and associativity rules */

    if (a->action_code == SHIFT && b->action_code == REDUCE) {
        shift_act = a;
        reduce_act = b;
    } else if (a->action_code == REDUCE && b->action_code == SHIFT) {
        shift_act = b;
        reduce_act = a;
    }

    if (shift_act && reduce_act) {
        /* Shift/reduce conflict */
        if (shift_act->prec > 0 && reduce_act->prec > 0) {
            if (shift_act->prec > reduce_act->prec) {
                reduce_act->suppressed = 1;  /* Suppress reduce */
            } else if (shift_act->prec < reduce_act->prec) {
                shift_act->suppressed = 1;  /* Suppress shift */
            } else {
                /* Equal precedence: use associativity */
                switch (shift_act->assoc) {
                case LEFT_ASSOC:
                    shift_act->suppressed = 1;  /* Reduce */
                    break;
                case RIGHT_ASSOC:
                    reduce_act->suppressed = 1;  /* Shift */
                    break;
                case NON_ASSOC:
                    /* Both suppressed -> error */
                    shift_act->suppressed = 1;
                    reduce_act->suppressed = 1;
                    break;
                default:
                    /* No associativity: default to shift */
                    reduce_act->suppressed = 2;  /* Suppressed with warning */
                    SRconflicts[state]++;
                    SRtotal++;
                    break;
                }
            }
        } else {
            /* No precedence declared: default shift */
            reduce_act->suppressed = 2;
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

short *yydefred;
short *yysindex;
short *yyrindex;
short *yytable;
short *yycheck;
int yytable_size;

static void build_yytable(void) {
    int i, j;
    action *a;
    
    yytable_size = nstates * 3 + ntokens + 100; /* Conservative guess */
    yytable = (short *)calloc(yytable_size, sizeof(short));
    yycheck = (short *)calloc(yytable_size, sizeof(short));
    yysindex = (short *)calloc(nstates, sizeof(short));
    yyrindex = (short *)calloc(nstates, sizeof(short));
    
    if (yytable == NULL || yycheck == NULL || yysindex == NULL || yyrindex == NULL)
        no_space();
    
    for (i = 0; i < yytable_size; i++) yycheck[i] = -1;
    
    /* For each state's shift actions */
    for (i = 0; i < nstates; i++) {
        int base = -1;
        int found = 0;
        
        /* Find SHIFTs */
        int has_shifts = 0;
        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->action_code == SHIFT && !a->suppressed) {
                has_shifts = 1;
                break;
            }
        }
        
        if (!has_shifts) {
            yysindex[i] = 0;
            continue;
        }
        
        /* Find fit for shifts */
        for (base = 0; base < yytable_size - ntokens; base++) {
            int ok = 1;
            for (a = parser[i]; a != NULL; a = a->next) {
                if (a->action_code == SHIFT && !a->suppressed) {
                    if (yycheck[base + a->symbol] != -1) {
                        ok = 0;
                        break;
                    }
                }
            }
            if (ok) {
                found = 1;
                break;
            }
        }
        
        if (found) {
            yysindex[i] = base;
            for (a = parser[i]; a != NULL; a = a->next) {
                if (a->action_code == SHIFT && !a->suppressed) {
                    yytable[base + a->symbol] = a->number;
                    yycheck[base + a->symbol] = a->symbol;
                }
            }
        }
    }
    
    /* For each state's reduce actions */
    for (i = 0; i < nstates; i++) {
        int base = -1;
        int found = 0;
        int defred = yydefred[i];
        
        int has_reduces = 0;
        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
                has_reduces = 1;
                break;
            }
        }
        
        if (!has_reduces) {
            yyrindex[i] = 0;
            continue;
        }
        
        for (base = 0; base < yytable_size - ntokens; base++) {
            int ok = 1;
            for (a = parser[i]; a != NULL; a = a->next) {
                if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
                    if (yycheck[base + a->symbol] != -1) {
                        ok = 0;
                        break;
                    }
                }
            }
            if (ok) {
                found = 1;
                break;
            }
        }
        
        if (found) {
            yyrindex[i] = base;
            for (a = parser[i]; a != NULL; a = a->next) {
                if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
                    yytable[base + a->symbol] = -a->number;
                    yycheck[base + a->symbol] = a->symbol;
                }
            }
        }
    }

    /* For each non-terminal's goto transitions */
    for (i = 0; i < nvars; i++) {
        int base = -1;
        int found = 0;
        int sym = ntokens + i;
        
        /* Check if this variable has any non-default transitions */
        /* (Simplified - just fit all transitions for now) */
        for (base = 0; base < yytable_size - nstates; base++) {
            int ok = 1;
            shifts *sp;
            for (sp = first_shift; sp != NULL; sp = sp->next) {
                for (j = 0; j < sp->nshifts; j++) {
                    int dest = sp->shift[j];
                    if (accessing_symbol[dest] == sym) {
                        if (yycheck[base + sp->number] != -1) {
                            ok = 0;
                            break;
                        }
                    }
                }
                if (!ok) break;
            }
            if (ok) {
                found = 1;
                break;
            }
        }

        if (found) {
            yygindex[i] = base;
            shifts *sp;
            for (sp = first_shift; sp != NULL; sp = sp->next) {
                for (j = 0; j < sp->nshifts; j++) {
                    int dest = sp->shift[j];
                    if (accessing_symbol[dest] == sym) {
                        if (dest != yydgoto[i]) {
                            yytable[base + sp->number] = dest;
                            yycheck[base + sp->number] = sp->number;
                        }
                    }
                }
            }
        }
    }
}


static void defreds(void) {
    int i, j;
    action *a;
    int has_shift;
    int best_rule;
    int best_count;
    int *rule_count;
    
    /* Allocate yydefred array */
    yydefred = (short *)malloc(nstates * sizeof(short));
    if (yydefred == NULL) no_space();
    rule_count = (int *)calloc(nrules, sizeof(int));
    if (rule_count == NULL) no_space();
    
    /* Compute default reductions for each state */
    for (i = 0; i < nstates; i++) {
        has_shift = 0;
        best_rule = 0;
        best_count = 0;
        for (j = 0; j < nrules; j++) {
            rule_count[j] = 0;
        }

        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->suppressed) {
                continue;
            }
            if (a->action_code == SHIFT) {
                has_shift = 1;
            } else if (a->action_code == REDUCE &&
                       a->number >= 0 && a->number < nrules) {
                rule_count[a->number]++;
            }
        }

        if (!has_shift) {
            for (j = 0; j < nrules; j++) {
                if (rule_count[j] > best_count) {
                    best_count = rule_count[j];
                    best_rule = j;
                } else if (rule_count[j] == best_count &&
                           best_count > 0 &&
                           j < best_rule) {
                    best_rule = j;
                }
            }
        }

        yydefred[i] = (best_count > 0) ? best_rule : 0;
    }

    free(rule_count);
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

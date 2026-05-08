/*
 * mkpar.c - Parser Table Construction
 *
 * Builds the ACTION and GOTO tables from LALR(1) state information and
 * compresses them for emission.
 *
 * Compression layout:
 *   yysindex[s] - if non-zero, base such that yytable[base+token] is the
 *                 destination state of a shift on `token` from state s,
 *                 verified by yycheck[base+token] == token.
 *   yyrindex[s] - same, but yytable[base+token] is the (positive) rule
 *                 number to reduce by.
 *   yygindex[N] - non-terminal goto index; yytable[base+from] is the new
 *                 state when reducing into nonterminal N from state `from`.
 *   yydefred[s] - default reduction (rule >0) or 0 (no default).
 *   yydgoto[N]  - default new state on reducing into nonterminal N.
 *
 * A value of zero in yysindex/yyrindex/yygindex unambiguously means "no
 * entries"; placement starts at base 1, so no real entry ever lands at
 * yytable[base+token] with base==0.
 */

#include "defs.h"

#define BITS_PER_WORD 32
#define WORDSIZE(n)  (((n) + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define TESTBIT(a, i) ((a)[(i) / BITS_PER_WORD] & (1U << ((i) % BITS_PER_WORD)))

action **parser;
int SRtotal;
int RRtotal;
int SRexpect;
short *SRconflicts;
short *RRconflicts;
short final_state;

short *yydefred;
short *yydgoto;
short *yysindex;
short *yyrindex;
short *yygindex;
short *yytable;
short *yycheck;
int yytable_size;

static short *symbol_prec;
static char  *symbol_assoc;
static short *rule_prec;
static char  *rule_assoc;
static char  *yytable_used;     /* 1 if the slot has been claimed */

static action *parse_actions(int stateno);
static action *add_reduce(action *alist, int ruleno, unsigned *la);
static action *add_shift(action *alist, int symbol, int state);
static void   find_final_state(void);
static void   remove_conflicts(void);
static void   resolve_conflict(int state, action *a, action *b);
static void   total_conflicts(void);
static void   defreds(void);
static void   build_goto_defaults(void);
static void   build_yytable(void);
static void   free_action_row(action *a);
static void   build_precedence_tables(void);
static int    find_base_shift(int stateno);
static int    find_base_reduce(int stateno, int defred);
static int    find_base_goto(int sym);
static int    grow_table(int needed);

void make_parser(void) {
    int i;

    parser      = (action **)calloc((size_t)nstates, sizeof(action *));
    SRconflicts = (short   *)calloc((size_t)nstates, sizeof(short));
    RRconflicts = (short   *)calloc((size_t)nstates, sizeof(short));
    if (!parser || !SRconflicts || !RRconflicts) no_space();

    build_precedence_tables();

    for (i = 0; i < nstates; i++)
        parser[i] = parse_actions(i);

    find_final_state();
    remove_conflicts();
    total_conflicts();
    defreds();
    build_goto_defaults();
    build_yytable();
}

static void build_precedence_tables(void) {
    int i, j;
    bucket *bp;

    symbol_prec  = (short *)calloc((size_t)nsyms,  sizeof(short));
    symbol_assoc = (char  *)calloc((size_t)nsyms,  sizeof(char));
    rule_prec    = (short *)calloc((size_t)nrules, sizeof(short));
    rule_assoc   = (char  *)calloc((size_t)nrules, sizeof(char));
    if (!symbol_prec || !symbol_assoc || !rule_prec || !rule_assoc) no_space();

    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->index >= 0 && bp->index < nsyms) {
            symbol_prec[bp->index]  = bp->prec;
            symbol_assoc[bp->index] = bp->assoc;
        }
    }
    for (i = 1; i < nrules; i++) {
        for (j = rrhs[i]; ritem[j] >= 0; j++) {
            int sym = ritem[j];
            if (sym >= 0 && sym < ntokens && symbol_prec[sym] > 0) {
                rule_prec[i]  = symbol_prec[sym];
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
    int nwords = WORDSIZE(ntokens);

    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number != stateno) continue;
        for (i = 0; i < sp->nshifts; i++) {
            int dest   = sp->shift[i];
            int symbol = accessing_symbol[dest];
            if (symbol < ntokens) alist = add_shift(alist, symbol, dest);
        }
        break;
    }

    red_idx = 0;
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        for (i = 0; i < rp->nreds; i++) {
            if (rp->number == stateno && red_idx < lalr_nreductions) {
                unsigned *la = lalr_LA + (size_t)red_idx * (size_t)nwords;
                alist = add_reduce(alist, rp->rules[i], la);
            }
            red_idx++;
        }
    }

    return alist;
}

static action *add_shift(action *alist, int symbol, int state) {
    action *a = (action *)malloc(sizeof(action));
    if (!a) no_space();
    a->next        = alist;
    a->symbol      = (short)symbol;
    a->number      = (short)state;
    a->prec        = (symbol >= 0 && symbol < nsyms) ? symbol_prec[symbol]  : 0;
    a->action_code = SHIFT;
    a->assoc       = (symbol >= 0 && symbol < nsyms) ? symbol_assoc[symbol] : NO_ASSOC;
    a->suppressed  = 0;
    return a;
}

static action *add_reduce(action *alist, int ruleno, unsigned *la) {
    int i;
    for (i = 0; i < ntokens; i++) {
        if (TESTBIT(la, i)) {
            action *a = (action *)malloc(sizeof(action));
            if (!a) no_space();
            a->next        = alist;
            a->symbol      = (short)i;
            a->number      = (short)ruleno;
            a->prec        = (ruleno >= 0 && ruleno < nrules) ? rule_prec[ruleno]  : 0;
            a->action_code = REDUCE;
            a->assoc       = (ruleno >= 0 && ruleno < nrules) ? rule_assoc[ruleno] : NO_ASSOC;
            a->suppressed  = 0;
            alist = a;
        }
    }
    return alist;
}

static void find_final_state(void) {
    shifts *sp;
    int i;
    final_state = 0;
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number != 0) continue;
        for (i = 0; i < sp->nshifts; i++) {
            int dest = sp->shift[i];
            if (accessing_symbol[dest] == goal_symbol->index) {
                final_state = (short)dest;
                return;
            }
        }
    }
}

static void remove_conflicts(void) {
    int i;
    action *a, *b;
    SRtotal = 0;
    RRtotal = 0;
    for (i = 0; i < nstates; i++) {
        for (a = parser[i]; a != NULL; a = a->next) {
            for (b = a->next; b != NULL; b = b->next) {
                if (a->symbol == b->symbol)
                    resolve_conflict(i, a, b);
            }
        }
    }
}

static void resolve_conflict(int state, action *a, action *b) {
    action *shift_act = NULL, *reduce_act = NULL;

    if (a->suppressed || b->suppressed) return;

    if (a->action_code == SHIFT && b->action_code == REDUCE) {
        shift_act = a; reduce_act = b;
    } else if (a->action_code == REDUCE && b->action_code == SHIFT) {
        shift_act = b; reduce_act = a;
    } else if (a->action_code == REDUCE && b->action_code == REDUCE) {
        if (a->number < b->number) b->suppressed = 1;
        else                       a->suppressed = 1;
        RRconflicts[state]++;
        RRtotal++;
        return;
    } else {
        return;
    }

    if (shift_act->prec > 0 && reduce_act->prec > 0) {
        if (shift_act->prec > reduce_act->prec) {
            reduce_act->suppressed = 1;
        } else if (shift_act->prec < reduce_act->prec) {
            shift_act->suppressed = 1;
        } else {
            switch (shift_act->assoc) {
            case LEFT_ASSOC:  shift_act->suppressed  = 1; break;
            case RIGHT_ASSOC: reduce_act->suppressed = 1; break;
            case NON_ASSOC:
                shift_act->suppressed  = 1;
                reduce_act->suppressed = 1;
                break;
            default:
                reduce_act->suppressed = 2;
                SRconflicts[state]++;
                SRtotal++;
                break;
            }
        }
    } else {
        reduce_act->suppressed = 2;
        SRconflicts[state]++;
        SRtotal++;
    }
}

static void total_conflicts(void) {
    if (SRtotal > SRexpect || RRtotal > 0) {
        fprintf(stderr, "conflicts:");
        if (SRtotal > 0) fprintf(stderr, " %d shift/reduce", SRtotal);
        if (RRtotal > 0) fprintf(stderr, " %d reduce/reduce", RRtotal);
        fprintf(stderr, "\n");
    }
}

static void defreds(void) {
    int i, j;
    int *rule_count;
    action *a;

    yydefred = (short *)calloc((size_t)nstates, sizeof(short));
    rule_count = (int *)calloc((size_t)nrules, sizeof(int));
    if (!yydefred || !rule_count) no_space();

    for (i = 0; i < nstates; i++) {
        int has_shift = 0;
        int best_rule = 0;
        int best_count = 0;

        for (j = 0; j < nrules; j++) rule_count[j] = 0;

        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->suppressed) continue;
            if (a->action_code == SHIFT) {
                has_shift = 1;
            } else if (a->action_code == REDUCE) {
                if (a->number >= 0 && a->number < nrules)
                    rule_count[a->number]++;
            }
        }

        if (!has_shift) {
            for (j = 1; j < nrules; j++) {
                if (rule_count[j] > best_count ||
                    (rule_count[j] == best_count && best_count > 0 && j < best_rule)) {
                    best_count = rule_count[j];
                    best_rule  = j;
                }
            }
        }
        yydefred[i] = (best_count > 0) ? (short)best_rule : 0;
    }
    free(rule_count);
}

static void build_goto_defaults(void) {
    int i, j;
    shifts *sp;
    int *dest_freq;

    yydgoto = (short *)calloc((size_t)nvars, sizeof(short));
    if (!yydgoto) no_space();

    dest_freq = (int *)calloc((size_t)nstates, sizeof(int));
    if (!dest_freq) no_space();

    for (i = 0; i < nvars; i++) {
        int sym = ntokens + i;
        int best_dest = 0;
        int best_count = 0;

        memset(dest_freq, 0, (size_t)nstates * sizeof(int));

        for (sp = first_shift; sp != NULL; sp = sp->next) {
            for (j = 0; j < sp->nshifts; j++) {
                int dest = sp->shift[j];
                if (accessing_symbol[dest] == sym) {
                    if (++dest_freq[dest] > best_count) {
                        best_count = dest_freq[dest];
                        best_dest  = dest;
                    }
                }
            }
        }
        yydgoto[i] = (short)best_dest;
    }
    free(dest_freq);
}

static int grow_table(int needed) {
    int new_size = yytable_size;
    while (new_size < needed) new_size = new_size * 2 + 32;
    if (new_size == yytable_size) return 0;
    yytable      = (short *)realloc(yytable,      (size_t)new_size * sizeof(short));
    yycheck      = (short *)realloc(yycheck,      (size_t)new_size * sizeof(short));
    yytable_used = (char  *)realloc(yytable_used, (size_t)new_size);
    if (!yytable || !yycheck || !yytable_used) no_space();
    {
        int i;
        for (i = yytable_size; i < new_size; i++) {
            yytable[i]      = 0;
            yycheck[i]      = -1;
            yytable_used[i] = 0;
        }
    }
    yytable_size = new_size;
    return 1;
}

static int find_base_shift(int stateno) {
    int base, max_sym = 0, min_sym = ntokens, found_any = 0;
    action *a;

    for (a = parser[stateno]; a != NULL; a = a->next) {
        if (a->action_code == SHIFT && !a->suppressed) {
            if (!found_any) { min_sym = max_sym = a->symbol; found_any = 1; }
            if (a->symbol < min_sym) min_sym = a->symbol;
            if (a->symbol > max_sym) max_sym = a->symbol;
        }
    }
    if (!found_any) return 0;

    for (base = 1; ; base++) {
        int ok = 1;
        if (base + max_sym >= yytable_size) grow_table(base + max_sym + 1);
        for (a = parser[stateno]; a != NULL; a = a->next) {
            if (a->action_code == SHIFT && !a->suppressed) {
                if (yytable_used[base + a->symbol]) { ok = 0; break; }
            }
        }
        if (ok) return base;
    }
}

static int find_base_reduce(int stateno, int defred) {
    int base, max_sym = 0, min_sym = ntokens, found_any = 0;
    action *a;

    for (a = parser[stateno]; a != NULL; a = a->next) {
        if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
            if (!found_any) { min_sym = max_sym = a->symbol; found_any = 1; }
            if (a->symbol < min_sym) min_sym = a->symbol;
            if (a->symbol > max_sym) max_sym = a->symbol;
        }
    }
    if (!found_any) return 0;

    for (base = 1; ; base++) {
        int ok = 1;
        if (base + max_sym >= yytable_size) grow_table(base + max_sym + 1);
        for (a = parser[stateno]; a != NULL; a = a->next) {
            if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
                if (yytable_used[base + a->symbol]) { ok = 0; break; }
            }
        }
        if (ok) return base;
    }
}

static int find_base_goto(int sym) {
    int base, j;
    int max_state = 0, found_any = 0;
    shifts *sp;

    for (sp = first_shift; sp != NULL; sp = sp->next) {
        for (j = 0; j < sp->nshifts; j++) {
            int dest = sp->shift[j];
            if (accessing_symbol[dest] == sym) {
                if (dest != yydgoto[sym - ntokens]) {
                    if (!found_any) { max_state = sp->number; found_any = 1; }
                    if (sp->number > max_state) max_state = sp->number;
                }
            }
        }
    }
    if (!found_any) return 0;

    for (base = 1; ; base++) {
        int ok = 1;
        if (base + max_state >= yytable_size) grow_table(base + max_state + 1);
        for (sp = first_shift; sp != NULL; sp = sp->next) {
            for (j = 0; j < sp->nshifts; j++) {
                int dest = sp->shift[j];
                if (accessing_symbol[dest] == sym && dest != yydgoto[sym - ntokens]) {
                    if (yytable_used[base + sp->number]) { ok = 0; goto next; }
                }
            }
        }
        if (ok) return base;
next:   ;
    }
}

static void build_yytable(void) {
    int i, j;
    action *a;
    shifts *sp;

    yytable_size = nstates * 4 + ntokens + 64;
    yytable      = (short *)malloc((size_t)yytable_size * sizeof(short));
    yycheck      = (short *)malloc((size_t)yytable_size * sizeof(short));
    yytable_used = (char  *)malloc((size_t)yytable_size);
    yysindex     = (short *)calloc((size_t)nstates, sizeof(short));
    yyrindex     = (short *)calloc((size_t)nstates, sizeof(short));
    yygindex     = (short *)calloc((size_t)nvars,   sizeof(short));
    if (!yytable || !yycheck || !yytable_used || !yysindex || !yyrindex || !yygindex)
        no_space();

    for (i = 0; i < yytable_size; i++) {
        yytable[i]      = 0;
        yycheck[i]      = -1;
        yytable_used[i] = 0;
    }

    /* Place shifts. */
    for (i = 0; i < nstates; i++) {
        int base = find_base_shift(i);
        yysindex[i] = (short)base;
        if (base == 0) continue;
        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->action_code == SHIFT && !a->suppressed) {
                int idx = base + a->symbol;
                yytable[idx]      = a->number;
                yycheck[idx]      = a->symbol;
                yytable_used[idx] = 1;
            }
        }
    }

    /* Place non-default reduces. */
    for (i = 0; i < nstates; i++) {
        int defred = yydefred[i];
        int base   = find_base_reduce(i, defred);
        yyrindex[i] = (short)base;
        if (base == 0) continue;
        for (a = parser[i]; a != NULL; a = a->next) {
            if (a->action_code == REDUCE && !a->suppressed && a->number != defred) {
                int idx = base + a->symbol;
                yytable[idx]      = a->number;
                yycheck[idx]      = a->symbol;
                yytable_used[idx] = 1;
            }
        }
    }

    /* Place GOTO transitions. */
    for (i = 0; i < nvars; i++) {
        int sym  = ntokens + i;
        int base = find_base_goto(sym);
        yygindex[i] = (short)base;
        if (base == 0) continue;
        for (sp = first_shift; sp != NULL; sp = sp->next) {
            for (j = 0; j < sp->nshifts; j++) {
                int dest = sp->shift[j];
                if (accessing_symbol[dest] == sym && dest != yydgoto[i]) {
                    int idx = base + sp->number;
                    yytable[idx]      = (short)dest;
                    yycheck[idx]      = (short)sp->number;
                    yytable_used[idx] = 1;
                }
            }
        }
    }

    free(yytable_used);
    yytable_used = NULL;
}

static void free_action_row(action *a) {
    while (a != NULL) {
        action *next = a->next;
        free(a);
        a = next;
    }
}

void free_parser(void) {
    int i;
    for (i = 0; i < nstates; i++) free_action_row(parser[i]);
    free(parser);
    free(SRconflicts);
    free(RRconflicts);
}

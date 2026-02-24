/*
 * verbose.c - Verbose Output (-v option)
 *
 * Generates y.output file with human-readable state machine description.
 */

#include "defs.h"

extern int SRtotal, RRtotal;

static void log_unused_symbols(void);
static void log_conflicts(void);
static void print_core(core *state);
static void print_actions(int stateno);

static bucket **symbol_by_index = NULL;

void verbose(void) {
    int i;
    bucket *bp;
    core *sp;
    
    if (!vflag || verbose_file == NULL) return;
    
    /* Build symbol index mapping */
    symbol_by_index = (bucket **)malloc(nsyms * sizeof(bucket *));
    if (symbol_by_index == NULL) no_space();
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        symbol_by_index[bp->index] = bp;
    }

    fprintf(verbose_file, "Yacc Grammar Analysis\n");
    fprintf(verbose_file, "=====================\n\n");
    
    /* Grammar statistics */
    fprintf(verbose_file, "Grammar:\n");
    fprintf(verbose_file, "  %d terminals\n", ntokens);
    fprintf(verbose_file, "  %d nonterminals\n", nvars);
    fprintf(verbose_file, "  %d rules\n", nrules);
    fprintf(verbose_file, "  %d states\n", nstates);
    fprintf(verbose_file, "\n");
    
    /* Conflict summary */
    if (SRtotal > 0 || RRtotal > 0) {
        fprintf(verbose_file, "Conflicts:\n");
        if (SRtotal > 0)
            fprintf(verbose_file, "  %d shift/reduce\n", SRtotal);
        if (RRtotal > 0)
            fprintf(verbose_file, "  %d reduce/reduce\n", RRtotal);
        fprintf(verbose_file, "\n");
    }
    
    /* Rules listing */
    fprintf(verbose_file, "Rules:\n");
    for (i = 2; i < nrules; i++) {
        int k;
        fprintf(verbose_file, "  %3d  %s :", i, symbol_by_index[plhs[i]]->name);
        for (k = rrhs[i]; ritem[k] >= 0; k++) {
            fprintf(verbose_file, " %s", symbol_by_index[ritem[k]]->name);
        }
        fprintf(verbose_file, "\n");
    }
    fprintf(verbose_file, "\n");
    
    /* State descriptions */
    fprintf(verbose_file, "State Descriptions:\n");
    fprintf(verbose_file, "-------------------\n");
    for (sp = first_state; sp != NULL; sp = sp->next) {
        fprintf(verbose_file, "\nState %d:\n", sp->number);
        print_core(sp);
        print_actions(sp->number);
    }
    
    log_unused_symbols();
    log_conflicts();

    if (symbol_by_index) {
        free(symbol_by_index);
        symbol_by_index = NULL;
    }
}

static void print_core(core *state) {
    int i, k, rule;

    if (state->nitems == 0) return;

    for (i = 0; i < state->nitems; i++) {
        int item_index = state->items[i];

        /* Find rule number by scanning forward */
        k = item_index;
        while (ritem[k] >= 0) k++;
        rule = -ritem[k];

        fprintf(verbose_file, "    %s :", symbol_by_index[plhs[rule]]->name);

        /* Print RHS with dot */
        for (k = rrhs[rule]; ritem[k] >= 0; k++) {
            if (k == item_index) fprintf(verbose_file, " .");
            fprintf(verbose_file, " %s", symbol_by_index[ritem[k]]->name);
        }
        if (k == item_index) fprintf(verbose_file, " .");
        fprintf(verbose_file, "\n");
    }
    fprintf(verbose_file, "\n");
}

static void print_actions(int stateno) {
    action *p;

    for (p = parser[stateno]; p != NULL; p = p->next) {
        if (p->symbol == -1)
            fprintf(verbose_file, "    .  ");
        else
            fprintf(verbose_file, "    %s  ", symbol_by_index[p->symbol]->name);

        switch (p->action_code) {
        case SHIFT:
            fprintf(verbose_file, "shift, and go to state %d", p->number);
            break;
        case REDUCE:
            fprintf(verbose_file, "reduce using rule %d (%s)", p->number, symbol_by_index[plhs[p->number]]->name);
            break;
        case ACCEPT:
            fprintf(verbose_file, "accept");
            break;
        case ERROR:
            fprintf(verbose_file, "error");
            break;
        }

        if (p->suppressed) {
            fprintf(verbose_file, "  (suppressed)");
        }

        fprintf(verbose_file, "\n");
    }
}

static void log_unused_symbols(void) {
    /* Report symbols that appear in declarations but are never used */
    bucket *bp;
    int count = 0;
    char *used;
    int i, k;

    used = (char *)calloc(ntokens, sizeof(char));
    if (used == NULL) return;

    /* Mark used terminals */
    for (i = 2; i < nrules; i++) {
        for (k = rrhs[i]; ritem[k] >= 0; k++) {
            if (ritem[k] < ntokens) {
                used[ritem[k]] = 1;
            }
        }
    }
    
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM && bp->value > 256) {
            if (bp->index < ntokens && !used[bp->index]) {
                fprintf(verbose_file, "Warning: token %s is unused\n", bp->name);
                count++;
            }
        }
    }
    
    if (count > 0 && verbose_file) {
        fprintf(verbose_file, "\n%d unused tokens\n", count);
    }

    free(used);
}

static void log_conflicts(void) {
    /* Detailed conflict reporting for -v output */
    int i;
    if (SRtotal > 0 || RRtotal > 0) {
        fprintf(verbose_file, "\nConflict Details:\n");
        for (i = 0; i < nstates; i++) {
            if (SRconflicts[i] > 0 || RRconflicts[i] > 0) {
                fprintf(verbose_file, "  State %d: ", i);
                if (SRconflicts[i] > 0) fprintf(verbose_file, "%d shift/reduce ", SRconflicts[i]);
                if (RRconflicts[i] > 0) fprintf(verbose_file, "%d reduce/reduce ", RRconflicts[i]);
                fprintf(verbose_file, "\n");
            }
        }
    }
}

void create_output_file(void) {
    char *name;
    int len;
    
    /* Create y.tab.c or file_prefix.tab.c */
    len = strlen(file_prefix) + 10;
    name = (char *)malloc(len);
    if (name == NULL) no_space();
    
    snprintf(name, len, "%s.tab.c", file_prefix);
    output_file = fopen(name, "w");
    if (output_file == NULL) {
        perror(name);
        done(1);
    }
    
    if (dflag) {
        snprintf(name, len, "%s.tab.h", file_prefix);
        defines_file = fopen(name, "w");
        if (defines_file == NULL) {
            perror(name);
            done(1);
        }
    }
    
    if (vflag) {
        snprintf(name, len, "%s.output", file_prefix);
        verbose_file = fopen(name, "w");
        if (verbose_file == NULL) {
            perror(name);
            done(1);
        }
    }
    
    free(name);
}

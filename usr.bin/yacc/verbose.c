/*
 * verbose.c - Verbose Output (-v option)
 *
 * Generates y.output file with human-readable state machine description.
 */

#include "defs.h"

extern int SRtotal, RRtotal;

static void log_unused_symbols(void);
static void log_conflicts(void);
static void print_core(int state);
static void print_actions(int stateno);

void verbose(void) {
    int i;
    
    if (!vflag || verbose_file == NULL) return;
    
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
        fprintf(verbose_file, "  %3d  ", i);
        /* Print LHS */
        /* Print -> */
        /* Print RHS symbols */
        fprintf(verbose_file, "\n");
    }
    fprintf(verbose_file, "\n");
    
    /* State descriptions */
    fprintf(verbose_file, "State Descriptions:\n");
    fprintf(verbose_file, "-------------------\n");
    for (i = 0; i < nstates; i++) {
        fprintf(verbose_file, "\nState %d:\n", i);
        print_core(i);
        print_actions(i);
    }
    
    log_unused_symbols();
    log_conflicts();
}

static void print_core(int state) {
    /* Print kernel items for this state */
    fprintf(verbose_file, "  (kernel items)\n");
}

static void print_actions(int stateno) {
    /* Print shift, reduce, and goto actions for state */
    fprintf(verbose_file, "  (actions)\n");
}

static void log_unused_symbols(void) {
    /* Report symbols that appear in declarations but are never used */
    bucket *bp;
    int count = 0;
    
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM && bp->value > 256) {
            /* Check if token is actually used in any production */
            /* For now, just skip this check */
        }
    }
    
    if (count > 0 && verbose_file) {
        fprintf(verbose_file, "\n%d unused tokens\n", count);
    }
}

static void log_conflicts(void) {
    /* Detailed conflict reporting for -v output */
    if (SRtotal > 0 || RRtotal > 0) {
        fprintf(verbose_file, "\nConflict Details:\n");
        /* For each state with conflicts, describe them */
    }
}

void create_output_file(void) {
    char *name;
    int len;
    
    /* Create y.tab.c or file_prefix.tab.c */
    len = strlen(file_prefix) + 10;
    name = (char *)malloc(len);
    if (name == NULL) no_space();
    
    sprintf(name, "%s.tab.c", file_prefix);
    output_file = fopen(name, "w");
    if (output_file == NULL) {
        perror(name);
        done(1);
    }
    
    if (dflag) {
        sprintf(name, "%s.tab.h", file_prefix);
        defines_file = fopen(name, "w");
        if (defines_file == NULL) {
            perror(name);
            done(1);
        }
    }
    
    if (vflag) {
        sprintf(name, "%s.output", file_prefix);
        verbose_file = fopen(name, "w");
        if (verbose_file == NULL) {
            perror(name);
            done(1);
        }
    }
    
    free(name);
}

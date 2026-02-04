/*
 * closure.c - Closure algorithm for LR(0) item sets
 */

#include "defs.h"

/* Global storage defined here */
short *first_derives;
short *eff;

/* Linked list of rules for each non-terminal */
static short *derives;      /* derives[i] = first rule deriving symbol i */
static short *next_rule;    /* next_rule[r] = next rule with same LHS as r, or -1 */

static void set_eff(void);

void set_first_derives(void) {
    int i;
    
    derives = (short *)malloc(nsyms * sizeof(short));
    next_rule = (short *)malloc(nrules * sizeof(short));
    first_derives = (short *)malloc(nsyms * sizeof(short));
    if (derives == NULL || next_rule == NULL || first_derives == NULL)
        no_space();
    
    /* Initialize to -1 (no rule) */
    for (i = 0; i < nsyms; i++) {
        derives[i] = -1;
        first_derives[i] = -1;
    }
    for (i = 0; i < nrules; i++) {
        next_rule[i] = -1;
    }
    
    /* Build derives chain: scan rules in reverse to build forward linked list */
    /* For each rule, prepend to chain for its LHS */
    for (i = nrules - 1; i >= 2; i--) {
        short lhs_sym = plhs[i];
        if (lhs_sym >= ntokens) {  /* Non-terminal */
            next_rule[i] = derives[lhs_sym];
            derives[lhs_sym] = i;
            if (first_derives[lhs_sym] == -1) {
                first_derives[lhs_sym] = i;
            }
        }
    }
}

void closure(short *nucleus, int n) {
    int i;
    short *isp;
    int item_count;
    
    /* Initialize item_set with nucleus */
    for (i = 0; i < n; i++) {
        item_set[i] = nucleus[i];
    }
    item_count = n;
    item_set_end = item_set + n;
    
    /* Clear rules_used bitmap */
    int nwords = (nrules / 32) + 1;
    for (i = 0; i < nwords; i++) {
        rules_used[i] = 0;
    }
    
    /* Compute closure: for each item A -> α·Bβ where B is non-terminal,
       add all rules B -> ·γ */
    /* Process all items, including newly added ones */
    for (i = 0; i < item_count; i++) {
        short item = item_set[i];
        short symbol = ritem[item];
        
        /* If symbol after dot is positive (a symbol, not end marker)
           and is a non-terminal */
        if (symbol > 0 && symbol >= ntokens) {
            /* Add all productions for this non-terminal */
            short rule = derives[symbol];
            while (rule >= 0) {
                /* Check if rule already in set using bitmap */
                int word = rule / 32;
                unsigned int bit = 1U << (rule % 32);
                
                if ((rules_used[word] & bit) == 0) {
                    /* Mark rule as used */
                    rules_used[word] |= bit;
                    
                    /* Add item for this rule (at position 0, start of RHS) */
                    if (item_count >= nitems + nrules) {  /* More generous bound */
                        fprintf(stderr, "Error: item set overflow (count=%d, limit=%d)\n",
                                item_count, nitems + nrules);
                        done(1);
                    }
                    item_set[item_count++] = rrhs[rule];
                    item_set_end = item_set + item_count;
                }
                
                /* Move to next rule with same LHS */
                rule = next_rule[rule];
            }
        }
    }
}

static void set_eff(void) {
    /* Compute EFF array for LALR lookahead computation */
    /* This is called later in the LALR phase, not needed for LR(0) */
}

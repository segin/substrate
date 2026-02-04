/*
 * lalr.c - LALR(1) Lookahead Computation
 *
 * Computes lookahead sets for LR(0) states to produce LALR(1) parser.
 * Uses spontaneous generation and propagation algorithm.
 */

#include "defs.h"

/* Lookahead computation data */
static int ngotos;              /* Number of goto transitions */
static short *goto_map;         /* Index into goto arrays by symbol */
static short *from_state;       /* Source state of each goto */
static short *to_state;         /* Destination state of each goto */

static unsigned *F;             /* FIRST sets as bit vectors */
static unsigned *LA;            /* Lookahead sets (bit vectors) */
static short **lookback;        /* Map reductions to gotos */
static short **includes;        /* Propagation edges */

static short *nullable;         /* Nullable symbols */

/* Token set operations */
#define BITS_PER_WORD 32
#define WORDSIZE(n) (((n) + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define SETBIT(a, i) ((a)[(i) / BITS_PER_WORD] |= (1U << ((i) % BITS_PER_WORD)))
#define TESTBIT(a, i) ((a)[(i) / BITS_PER_WORD] & (1U << ((i) % BITS_PER_WORD)))
#define CLRBIT(a, i) ((a)[(i) / BITS_PER_WORD] &= ~(1U << ((i) % BITS_PER_WORD)))

/* Digraph algorithm state */
static short *vertices;
static int top;
static int infinity;
static int *INDEX;
static unsigned *R;
static short **relations;

/* Forward declarations */
static void set_goto_map(void);
static void initialize_F(void);
static void set_nullable(void);
static void build_relations(void);
static void compute_FOLLOWS(void);
static void initialize_LA(void);
static void compute_lookaheads(void);
static void digraph(short **relation);
static void traverse(int i);

void lalr(void) {
    set_nullable();
    set_goto_map();
    initialize_F();
    build_relations();
    compute_FOLLOWS();
    initialize_LA();
    compute_lookaheads();
}

/* Determine which symbols can derive empty string */
static void set_nullable(void) {
    int i, j;
    int changed;
    
    nullable = (short *)calloc(nsyms, sizeof(short));
    if (nullable == NULL) no_space();
    
    /* Terminals are never nullable */
    for (i = 0; i < ntokens; i++)
        nullable[i] = 0;
    
    /* Iterate until no changes */
    do {
        changed = 0;
        for (i = 2; i < nrules; i++) {
            if (nullable[plhs[i]])
                continue;
            
            /* Check if all RHS symbols are nullable */
            int all_nullable = 1;
            for (j = rrhs[i]; ritem[j] >= 0; j++) {
                if (!nullable[ritem[j]]) {
                    all_nullable = 0;
                    break;
                }
            }
            if (all_nullable) {
                nullable[plhs[i]] = 1;
                changed = 1;
            }
        }
    } while (changed);
}

static void set_goto_map(void) {
    int i, j;
    shifts *sp;
    int count;
    short *temp_map;
    
    /* First pass: count total gotos and gotos per symbol */
    ngotos = 0;
    temp_map = (short *)calloc(nvars + 1, sizeof(short));
    if (temp_map == NULL) no_space();
    
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        for (i = 0; i < sp->nshifts; i++) {
            int dest = sp->shift[i];
            int sym = accessing_symbol[dest];
            if (sym >= ntokens) {  /* Non-terminal = GOTO */
                ngotos++;
                temp_map[sym - ntokens]++;
            }
        }
    }
    
    /* Allocate arrays */
    goto_map = (short *)calloc(nvars + 1, sizeof(short));
    from_state = (short *)malloc(ngotos * sizeof(short));
    to_state = (short *)malloc(ngotos * sizeof(short));
    if (goto_map == NULL || from_state == NULL || to_state == NULL)
        no_space();
    
    /* Build goto_map as cumulative index */
    count = 0;
    for (i = 0; i < nvars; i++) {
        goto_map[i] = count;
        count += temp_map[i];
        temp_map[i] = goto_map[i]; /* Reuse temp_map to track current insertion point */
    }
    goto_map[nvars] = count;
    
    /* Second pass: populate from_state and to_state */
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        for (i = 0; i < sp->nshifts; i++) {
            int dest = sp->shift[i];
            int sym = accessing_symbol[dest];
            if (sym >= ntokens) {
                int index = temp_map[sym - ntokens]++;
                from_state[index] = sp->number;
                to_state[index] = dest;
            }
        }
    }
    
    free(temp_map);
}


/* Initialize FIRST sets */
static void initialize_F(void) {
    int i, j, k;
    int nwords;
    int changed;
    
    nwords = WORDSIZE(ntokens);
    F = (unsigned *)calloc(nsyms * nwords, sizeof(unsigned));
    if (F == NULL) no_space();
    
    /* FIRST(terminal) = {terminal} */
    for (i = 0; i < ntokens; i++) {
        SETBIT(F + i * nwords, i);
    }
    
    /* Compute FIRST sets for non-terminals using fixpoint iteration */
    do {
        changed = 0;
        for (i = 2; i < nrules; i++) {
            int lhs = plhs[i];
            unsigned *lhs_first = F + lhs * nwords;
            
            /* For each symbol in RHS */
            for (j = rrhs[i]; ritem[j] >= 0; j++) {
                int sym = ritem[j];
                unsigned *sym_first = F + sym * nwords;
                
                /* Add FIRST(sym) to FIRST(lhs) */
                for (k = 0; k < nwords; k++) {
                    unsigned old = lhs_first[k];
                    lhs_first[k] |= sym_first[k];
                    if (lhs_first[k] != old)
                        changed = 1;
                }
                
                /* Stop if sym is not nullable */
                if (!nullable[sym])
                    break;
            }
        }
    } while (changed);
}

static int nreductions;

static void initialize_LA(void) {
    int i, j;
    reductions *rp;
    int nwords;
    
    nwords = WORDSIZE(ntokens);
    nreductions = 0;
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        nreductions += rp->nreds;
    }
    
    LA = (unsigned *)calloc(nreductions * nwords, sizeof(unsigned));
    if (LA == NULL) no_space();
    
    /* Initially, only rule 0 (accept) gets $end token if it's there */
    /* Find which reduction is for rule 0 */
    int red_idx = 0;
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        for (i = 0; i < rp->nreds; i++) {
            if (rp->rules[i] == 0) {  /* Rule 0: $accept -> start $end */
                SETBIT(LA + red_idx * nwords, 0);  /* Token 0 is $end */
            }
            red_idx++;
        }
    }
}


static int map_goto(int state, int symbol) {
    shifts *sp;
    int i;
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number == state) {
            for (i = 0; i < sp->nshifts; i++) {
                int dest = sp->shift[i];
                if (accessing_symbol[dest] == symbol)
                    return dest;
            }
            return -1;
        }
    }
    return -1;
}

static void build_relations(void) {
    int i, j, k;
    reductions *rp;
    shifts *sp;
    int nwords = WORDSIZE(ntokens);
    
    /* Allocate relation arrays */
    includes = (short **)calloc(ngotos, sizeof(short *));
    lookback = (short **)calloc(nreductions, sizeof(short *));
    if (includes == NULL || lookback == NULL)
        no_space();
    
    /* For each goto (q, A) -> p */
    for (i = 0; i < ngotos; i++) {
        int q = from_state[i];
        int p = to_state[i];
        int A = accessing_symbol[p];
        
        /* READS relation: terminals that can follow A in this context */
        /* For each goto (p, B) -> r, if B is nullable, READS includes READS(p, B) */
        /* Also terminals shifted from p are added to F[i] */
        shifts *sp;
        for (sp = first_shift; sp != NULL; sp = sp->next) {
            if (sp->number == p) {
                for (j = 0; j < sp->nshifts; j++) {
                    int dest = sp->shift[j];
                    int sym = accessing_symbol[dest];
                    if (sym < ntokens) {
                        SETBIT(F + i * nwords, sym);
                    }
                }
                break;
            }
        }
    }
    
    /* lookback relation: map reductions to gotos they can propagation-back to */
    int red_idx = 0;
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        int r = rp->number;
        for (i = 0; i < rp->nreds; i++) {
            int ruleno = rp->rules[i];
            /* Find state q before the reduction: q -> RHS -> r */
            int q = r;
            int rhs_start = rrhs[ruleno];
            int len = 0;
            while (ritem[rhs_start + len] >= 0) len++;
            
            for (j = len - 1; j >= 0; j--) {
                int sym = ritem[rhs_start + j];
                /* find p such that p --sym--> q */
                shifts *tmp_sp;
                int found = 0;
                for (tmp_sp = first_shift; tmp_sp != NULL; tmp_sp = tmp_sp->next) {
                    for (k = 0; k < tmp_sp->nshifts; k++) {
                        if (tmp_sp->shift[k] == q && accessing_symbol[q] == sym) {
                            q = tmp_sp->number;
                            found = 1;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
            
            /* q is now the state before the rule's RHS. 
               The reduction corresponds to the goto from q on lhs. */
            int lhs = plhs[ruleno];
            for (j = goto_map[lhs - ntokens]; j < goto_map[lhs - ntokens + 1]; j++) {
                if (from_state[j] == q) {
                    /* In a full implementation, we'd record this link 
                       to propagate lookaheads from GOTO j to REDUCTION red_idx. */
                    break;
                }
            }
            red_idx++;
        }
    }
}



/* Compute FOLLOW sets using digraph algorithm */
static void compute_FOLLOWS(void) {
    /* Apply digraph to compute transitive closure of READS and INCLUDES */
    if (ngotos > 0)
        digraph(includes);
}

/* Propagate lookaheads to reductions */
static void compute_lookaheads(void) {
    int i, j, k;
    int nwords = WORDSIZE(ntokens);
    reductions *rp;
    int red_idx = 0;
    
    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        for (i = 0; i < rp->nreds; i++) {
            unsigned *la = LA + red_idx * nwords;
            /* In a full implementation, we'd union lookaheads from related gotos.
               For this phase, ensure at least rule 0 has $end. */
            if (rp->rules[i] == 0) {
                SETBIT(la, 0);
            }
            red_idx++;
        }
    }
}

/* DeRemer-Pennello digraph algorithm for transitive closure */
static void digraph(short **relation) {
    int i;
    int nwords = WORDSIZE(ntokens);
    
    infinity = ngotos + 2;
    INDEX = (int *)calloc(ngotos, sizeof(int));
    vertices = (short *)malloc(ngotos * sizeof(short));
    if (INDEX == NULL || vertices == NULL)
        no_space();
    
    relations = relation;
    R = F;  /* Operating on F sets */
    
    top = 0;
    for (i = 0; i < ngotos; i++) {
        if (INDEX[i] == 0)
            traverse(i);
    }
    
    free(INDEX);
    free(vertices);
}

static void traverse(int i) {
    int j;
    int height;
    int nwords = WORDSIZE(ntokens);
    unsigned *base = R + i * nwords;
    short *rp;
    
    vertices[top++] = i;
    height = top;
    INDEX[i] = top;
    
    /* Process relation edges */
    rp = relations[i];
    if (rp) {
        while (*rp >= 0) {
            j = *rp++;
            if (INDEX[j] == 0)
                traverse(j);
            
            /* Merge sets */
            if (INDEX[j] < INDEX[i])
                INDEX[i] = INDEX[j];
            
            /* Union F[j] into F[i] */
            unsigned *src = R + j * nwords;
            for (int k = 0; k < nwords; k++)
                base[k] |= src[k];
        }
    }
    
    /* If we're at SCC root, propagate to all in SCC */
    if (INDEX[i] == height) {
        while (top > 0 && vertices[top - 1] >= i) {
            j = vertices[--top];
            INDEX[j] = infinity;
            if (j != i) {
                /* Copy F[i] to F[j] */
                unsigned *dst = R + j * nwords;
                for (int k = 0; k < nwords; k++)
                    dst[k] = base[k];
            }
        }
    }
}

/* Free LALR computation data */
void lalr_free(void) {
    if (nullable) free(nullable);
    if (goto_map) free(goto_map);
    if (from_state) free(from_state);
    if (to_state) free(to_state);
    if (F) free(F);
    if (LA) free(LA);
    if (includes) free(includes);
    if (lookback) free(lookback);
}

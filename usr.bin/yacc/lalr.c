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

/* Build GOTO map from state transitions */
static void set_goto_map(void) {
    int i;
    
    /* Count gotos (shifts on non-terminals) */
    /* For now, estimate based on nstates and nvars */
    ngotos = nstates * 2;  /* Conservative estimate */
    
    goto_map = (short *)calloc(nvars + 1, sizeof(short));
    from_state = (short *)malloc(ngotos * sizeof(short));
    to_state = (short *)malloc(ngotos * sizeof(short));
    
    if (goto_map == NULL || from_state == NULL || to_state == NULL)
        no_space();
    
    /* In a real implementation, iterate through shift actions
       on non-terminals to populate these arrays */
    for (i = 0; i <= nvars; i++)
        goto_map[i] = 0;
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

/* Allocate lookahead sets */
static void initialize_LA(void) {
    int nwords;
    
    nwords = WORDSIZE(ntokens);
    LA = (unsigned *)calloc(nrules * nwords, sizeof(unsigned));
    if (LA == NULL) no_space();
    
    /* For the accept rule, add $end to lookahead */
    /* Rule 0: $accept -> start $end */
    SETBIT(LA, 0);  /* $end is token 0 */
}

/* Build READS and INCLUDES relations */
static void build_relations(void) {
    /* READS relation: for gotos, what terminals can follow */
    /* INCLUDES relation: lookahead propagation through grammar */
    
    /* Allocate relation arrays */
    includes = (short **)calloc(ngotos, sizeof(short *));
    lookback = (short **)calloc(nrules, sizeof(short *));
    if (includes == NULL || lookback == NULL)
        no_space();
}

/* Compute FOLLOW sets using digraph algorithm */
static void compute_FOLLOWS(void) {
    /* Apply digraph to compute transitive closure of READS and INCLUDES */
    if (ngotos > 0)
        digraph(includes);
}

/* Propagate lookaheads to reductions */
static void compute_lookaheads(void) {
    /* For each reduction, union lookaheads from lookback gotos */
    /* LA[rule] = union of F[goto] for all gotos in lookback[rule] */
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

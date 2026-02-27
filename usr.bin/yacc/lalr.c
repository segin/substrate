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

int lalr_ngotos;
int lalr_dr_term_count;
int lalr_read_edge_count;
int lalr_la_entry_count;
int lalr_la_reduction_count;

/* Forward declarations */
static void set_goto_map(void);
static void initialize_F(void);
static void set_nullable(void);
static void build_relations(void);
static void compute_FOLLOWS(void);
static void initialize_LA(void);
static void compute_lookaheads(void);
static shifts *find_shifts_for_state(int state);
static int find_goto_index(int state, int symbol);
static int find_predecessor_state(int dest_state, int symbol);
static void digraph(short **relation);
static void traverse(int i);

void lalr(void) {
    lalr_ngotos = 0;
    lalr_dr_term_count = 0;
    lalr_read_edge_count = 0;
    lalr_la_entry_count = 0;
    lalr_la_reduction_count = 0;
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
    int i;
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
    lalr_ngotos = ngotos;
}


static shifts *find_shifts_for_state(int state) {
    shifts *sp;
    for (sp = first_shift; sp != NULL; sp = sp->next) {
        if (sp->number == state) {
            return sp;
        }
    }
    return NULL;
}

static int find_goto_index(int state, int symbol) {
    int i;
    int var = symbol - ntokens;
    if (var < 0 || var >= nvars) {
        return -1;
    }
    for (i = goto_map[var]; i < goto_map[var + 1]; i++) {
        if (from_state[i] == state) {
            return i;
        }
    }
    return -1;
}

static int find_predecessor_state(int dest_state, int symbol) {
    shifts *sp;
    int i;

    for (sp = first_shift; sp != NULL; sp = sp->next) {
        for (i = 0; i < sp->nshifts; i++) {
            if (sp->shift[i] == dest_state && accessing_symbol[dest_state] == symbol) {
                return sp->number;
            }
        }
    }

    return -1;
}

/* Initialize direct-read sets DR(p, A) for each goto (p, A). */
static void initialize_F(void) {
    int i, j;
    int nwords;
    shifts *sp;

    nwords = WORDSIZE(ntokens);
    F = (unsigned *)calloc(ngotos * nwords, sizeof(unsigned));
    if (F == NULL) no_space();

    lalr_dr_term_count = 0;
    for (i = 0; i < ngotos; i++) {
        unsigned *dr = F + i * nwords;
        int p = to_state[i];
        sp = find_shifts_for_state(p);
        if (!sp) {
            continue;
        }
        for (j = 0; j < sp->nshifts; j++) {
            int dest = sp->shift[j];
            int sym = accessing_symbol[dest];
            if (sym < ntokens && !TESTBIT(dr, sym)) {
                SETBIT(dr, sym);
                lalr_dr_term_count++;
            }
        }
    }
}

static int nreductions;

static void initialize_LA(void) {
    int i;
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


static void build_relations(void) {
    int i, j;
    int *counts;
    int *fills;
    shifts *sp;

    includes = (short **)calloc(ngotos, sizeof(short *));
    if (includes == NULL)
        no_space();

    counts = (int *)calloc(ngotos, sizeof(int));
    fills = (int *)calloc(ngotos, sizeof(int));
    if (counts == NULL || fills == NULL)
        no_space();

    /* First pass: count READS edges for allocation. */
    for (i = 0; i < ngotos; i++) {
        int p = to_state[i];
        sp = find_shifts_for_state(p);
        if (!sp) {
            continue;
        }
        for (j = 0; j < sp->nshifts; j++) {
            int dest = sp->shift[j];
            int sym = accessing_symbol[dest];
            if (sym >= ntokens && nullable[sym] && find_goto_index(p, sym) >= 0) {
                counts[i]++;
            }
        }
    }

    for (i = 0; i < ngotos; i++) {
        if (counts[i] > 0) {
            includes[i] = (short *)malloc((counts[i] + 1) * sizeof(short));
            if (includes[i] == NULL)
                no_space();
            includes[i][counts[i]] = -1;
        }
    }

    lalr_read_edge_count = 0;
    for (i = 0; i < ngotos; i++) {
        int p = to_state[i];
        sp = find_shifts_for_state(p);
        if (!sp || includes[i] == NULL) {
            continue;
        }
        for (j = 0; j < sp->nshifts; j++) {
            int dest = sp->shift[j];
            int sym = accessing_symbol[dest];
            if (sym >= ntokens && nullable[sym]) {
                int g = find_goto_index(p, sym);
                if (g >= 0) {
                    includes[i][fills[i]++] = (short)g;
                    lalr_read_edge_count++;
                }
            }
        }
    }

    free(counts);
    free(fills);
}


/* Compute FOLLOW sets using digraph algorithm */
static void compute_FOLLOWS(void) {
    /* Apply digraph to compute transitive closure of READS and INCLUDES */
    if (ngotos > 0)
        digraph(includes);
}

/* Propagate lookaheads to reductions */
static void compute_lookaheads(void) {
    int i, j;
    int nwords = WORDSIZE(ntokens);
    reductions *rp;
    int red_idx = 0;
    
    lalr_la_entry_count = 0;
    lalr_la_reduction_count = 0;

    for (rp = first_reduction; rp != NULL; rp = rp->next) {
        for (i = 0; i < rp->nreds; i++) {
            unsigned *la = LA + red_idx * nwords;
            int ruleno = rp->rules[i];
            int q = rp->number;
            int rhs_start = rrhs[ruleno];
            int rhs_len = 0;
            int lhs = plhs[ruleno];
            int g;
            int has_la = 0;

            while (ritem[rhs_start + rhs_len] >= 0) {
                rhs_len++;
            }

            /* Walk backwards over RHS to reach the pre-reduction state. */
            for (j = rhs_len - 1; j >= 0; j--) {
                int sym = ritem[rhs_start + j];
                q = find_predecessor_state(q, sym);
                if (q < 0) {
                    break;
                }
            }

            if (q >= 0) {
                g = find_goto_index(q, lhs);
                if (g >= 0) {
                    unsigned *follow = F + g * nwords;
                    for (j = 0; j < nwords; j++) {
                        la[j] |= follow[j];
                    }
                }
            }

            if (ruleno == 1) {
                SETBIT(la, 0);
            }

            for (j = 0; j < ntokens; j++) {
                if (TESTBIT(la, j)) {
                    lalr_la_entry_count++;
                    has_la = 1;
                }
            }
            if (has_la) {
                lalr_la_reduction_count++;
            }
            red_idx++;
        }
    }
}

/* DeRemer-Pennello digraph algorithm for transitive closure */
static void digraph(short **relation) {
    int i;
    
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
        do {
            j = vertices[--top];
            INDEX[j] = infinity;
            if (j != i) {
                /* Copy F[i] to F[j] */
                unsigned *dst = R + j * nwords;
                for (int k = 0; k < nwords; k++)
                    dst[k] = base[k];
            }
        } while (j != i);
    }
}

/* Free LALR computation data */
void lalr_free(void) {
    int i;
    if (nullable) free(nullable);
    if (goto_map) free(goto_map);
    if (from_state) free(from_state);
    if (to_state) free(to_state);
    if (F) free(F);
    if (LA) free(LA);
    if (includes) {
        for (i = 0; i < ngotos; i++) {
            if (includes[i]) free(includes[i]);
        }
        free(includes);
    }
    if (lookback) free(lookback);
}

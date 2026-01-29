/*
 * lr0.c - LR(0) State Machine Generator
 *
 * Computes LR(0) states for the grammar using closure and GOTO algorithms.
 * Based on classic yacc implementation.
 */

#include "defs.h"

/* Item set working storage */
short *item_set;
short *item_set_end;
unsigned *rules_used;

/* State storage */
int nstates;
static core **state_set;        /* Hash table for states */
static core *first_state;       /* Linked list of all states */
static core *last_state;
static int nshifts;             /* Number of shifts */
static short *shift_symbol;     /* Symbols we shift on */
static short *redset;           /* Reduction set */
static short *shiftset;         /* Shift set */
static short *kernel_base;      /* Base of kernel items per state */
static short *kernel_end;       /* End of kernel items per state */
static short *kernel_items;     /* Kernel item storage */

#define STATE_TABLE_SIZE 1024

/* Forward declarations */
static void allocate_item_sets(void);
static void allocate_storage(void);
static void set_state_table(void);
static void initialize_states(void);
static void generate_states(void);
static int get_state(int symbol);
static void new_item_sets(void);
static void append_states(void);
static void save_shifts(void);
static void save_reductions(void);
static core *new_state(int symbol);

void lr0(void) {
    allocate_storage();
    set_first_derives();
    initialize_states();
    generate_states();
}

static void allocate_storage(void) {
    allocate_item_sets();
    set_state_table();
}

static void allocate_item_sets(void) {
    int i;
    
    /* Allocate item set storage - needs to hold all items */
    item_set = (short *)malloc((nitems + 1) * sizeof(short));
    if (item_set == NULL) no_space();
    
    /* Bit vector for rules used in closure */
    rules_used = (unsigned *)malloc(((nrules / 32) + 1) * sizeof(unsigned));
    if (rules_used == NULL) no_space();
    
    /* Allocate shift symbol and other arrays */
    shift_symbol = (short *)malloc(nsyms * sizeof(short));
    if (shift_symbol == NULL) no_space();
    
    redset = (short *)malloc((nrules + 1) * sizeof(short));
    if (redset == NULL) no_space();
    
    shiftset = (short *)malloc(nsyms * sizeof(short));
    if (shiftset == NULL) no_space();
    
    /* Kernel item storage */
    kernel_base = (short *)malloc(nsyms * sizeof(short));
    kernel_end = (short *)malloc(nsyms * sizeof(short));
    kernel_items = (short *)malloc(nitems * sizeof(short));
    if (kernel_base == NULL || kernel_end == NULL || kernel_items == NULL)
        no_space();
    
    /* Initialize kernel base indices */
    for (i = 0; i < nsyms; i++) {
        kernel_base[i] = -1;
        kernel_end[i] = -1;
    }
}

static void set_state_table(void) {
    int i;
    
    /* Hash table for state lookup */
    state_set = (core **)malloc(STATE_TABLE_SIZE * sizeof(core *));
    if (state_set == NULL) no_space();
    
    for (i = 0; i < STATE_TABLE_SIZE; i++)
        state_set[i] = NULL;
    
    first_state = NULL;
    last_state = NULL;
    nstates = 0;
}

static void initialize_states(void) {
    int i;
    core *sp;
    
    /* Create initial state with augmented start rule: $accept : . start $end */
    /* The item for this is at the start of ritem for rule 0 (or rule 1?) */
    /* We need to find where the start production begins */
    
    /* For now, create state 0 with the kernel item for the goal production */
    /* Standard yacc: Rule 0 is $accept : goal $end */
    /* We stored rules starting at index 2, so rule 2 is first user rule */
    /* Actually, we need to create augmented grammar first */
    
    /* Initialize item_set with the initial kernel */
    item_set[0] = rrhs[2];  /* First item of first rule */
    item_set_end = item_set + 1;
    
    /* Create state 0 */
    sp = (core *)malloc(sizeof(core));
    if (sp == NULL) no_space();
    
    sp->next = NULL;
    sp->link = NULL;
    sp->number = 0;
    sp->accessing_symbol = 0;
    sp->nitems = 1;
    sp->items = (short *)malloc(sizeof(short));
    if (sp->items == NULL) no_space();
    sp->items[0] = rrhs[2];
    
    first_state = sp;
    last_state = sp;
    nstates = 1;
    
    /* Hash the state (state 0 is special, just store it) */
    state_set[0] = sp;
}

/* Compute GOTO for all symbols from current state's closure */
static void new_item_sets(void) {
    int i;
    short *isp;
    int symbol;
    int count;
    
    /* Clear kernel arrays */
    for (i = 0; i < nsyms; i++) {
        kernel_base[i] = -1;
        kernel_end[i] = -1;
    }
    nshifts = 0;
    
    /* Scan closure items - for each item A -> α.Xβ, add A -> αX.β to kernel(X) */
    for (isp = item_set; isp < item_set_end; isp++) {
        short item = *isp;
        symbol = ritem[item];
        
        if (symbol > 0) {  /* Not end of rule (negative = end marker) */
            /* Add item+1 to kernel for symbol */
            if (kernel_base[symbol] < 0) {
                /* First item for this symbol */
                shift_symbol[nshifts++] = symbol;
                kernel_base[symbol] = 0;  /* Will be set properly below */
            }
        }
    }
    
    /* Now do a second pass to actually store items */
    /* Reset and accumulate counts first */
    count = 0;
    for (i = 0; i < nshifts; i++) {
        symbol = shift_symbol[i];
        kernel_base[symbol] = count;
        kernel_end[symbol] = count;
        
        /* Count items for this symbol */
        for (isp = item_set; isp < item_set_end; isp++) {
            if (ritem[*isp] == symbol) {
                kernel_items[kernel_end[symbol]++] = *isp + 1;  /* Advance dot */
                count++;
            }
        }
    }
}

/* Get or create state for symbol transition */
static int get_state(int symbol) {
    int key;
    int n;
    short *isp1;
    short *isp2;
    short *iend;
    core *sp;
    int found;
    
    n = kernel_end[symbol] - kernel_base[symbol];
    if (n == 0) return -1;
    
    /* Compute hash key */
    key = 0;
    isp1 = kernel_items + kernel_base[symbol];
    iend = kernel_items + kernel_end[symbol];
    while (isp1 < iend)
        key += *isp1++;
    key = key % STATE_TABLE_SIZE;
    
    /* Search for existing state */
    sp = state_set[key];
    while (sp != NULL) {
        if (sp->nitems == n) {
            found = 1;
            isp1 = kernel_items + kernel_base[symbol];
            isp2 = sp->items;
            while (found && isp1 < iend) {
                if (*isp1++ != *isp2++) found = 0;
            }
            if (found) return sp->number;
        }
        sp = sp->link;
    }
    
    /* Create new state */
    sp = new_state(symbol);
    
    /* Add to hash table */
    sp->link = state_set[key];
    state_set[key] = sp;
    
    return sp->number;
}

static core *new_state(int symbol) {
    int n;
    core *sp;
    short *isp1;
    short *isp2;
    short *iend;
    
    n = kernel_end[symbol] - kernel_base[symbol];
    
    sp = (core *)malloc(sizeof(core));
    if (sp == NULL) no_space();
    
    sp->next = NULL;
    sp->link = NULL;
    sp->number = nstates++;
    sp->accessing_symbol = symbol;
    sp->nitems = n;
    sp->items = (short *)malloc(n * sizeof(short));
    if (sp->items == NULL) no_space();
    
    /* Copy kernel items */
    isp1 = kernel_items + kernel_base[symbol];
    isp2 = sp->items;
    iend = kernel_items + kernel_end[symbol];
    while (isp1 < iend)
        *isp2++ = *isp1++;
    
    /* Add to state list */
    if (last_state)
        last_state->next = sp;
    last_state = sp;
    
    return sp;
}

static void append_states(void) {
    int i;
    int symbol;
    
    /* For each shift symbol, get or create destination state */
    for (i = 0; i < nshifts; i++) {
        symbol = shift_symbol[i];
        shiftset[i] = get_state(symbol);
    }
}

static void save_shifts(void) {
    shifts *sp;
    int i;
    
    if (nshifts == 0) return;
    
    sp = (shifts *)malloc(sizeof(shifts) + (nshifts - 1) * sizeof(short));
    if (sp == NULL) no_space();
    
    sp->number = nstates - 1;  /* Current state being processed */
    sp->nshifts = nshifts;
    for (i = 0; i < nshifts; i++)
        sp->shift[i] = shiftset[i];
    
    /* Link into shifts list (to be done - store in global) */
}

static void save_reductions(void) {
    int count;
    int i;
    short *isp;
    reductions *rp;
    
    /* Count reductions (items at end of rule) */
    count = 0;
    for (isp = item_set; isp < item_set_end; isp++) {
        if (ritem[*isp] < 0) {  /* End of rule */
            redset[count++] = -ritem[*isp];  /* Rule number */
        }
    }
    
    if (count == 0) return;
    
    rp = (reductions *)malloc(sizeof(reductions) + (count - 1) * sizeof(short));
    if (rp == NULL) no_space();
    
    rp->number = nstates - 1;
    rp->nreds = count;
    for (i = 0; i < count; i++)
        rp->rules[i] = redset[i];
    
    /* Link into reductions list (to be done) */
}

static void generate_states(void) {
    core *sp;
    
    /* Process all states (new states are appended as we go) */
    sp = first_state;
    while (sp != NULL) {
        /* Compute closure of this state's kernel */
        closure(sp->items, sp->nitems);
        
        /* Compute GOTO for all symbols */
        new_item_sets();
        
        /* Create or find successor states */
        append_states();
        
        /* Save shift and reduction information */
        save_shifts();
        save_reductions();
        
        sp = sp->next;
    }
    
    /* Report number of states */
    if (verbose_file) {
        fprintf(verbose_file, "\n%d states\n", nstates);
    }
}

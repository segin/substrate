/*
 * symtab.c - Symbol Table management
 */

#include "defs.h"

#define TABLE_SIZE 1024

bucket *symbol_table[TABLE_SIZE];
bucket *first_symbol;
bucket *last_symbol;

bucket *goal_symbol = NULL;

void init_symtab(void) {
    int i;
    for (i = 0; i < TABLE_SIZE; i++)
        symbol_table[i] = NULL;
    
    first_symbol = NULL;
    last_symbol = NULL;
}

unsigned int hash(char *name) {
    unsigned int h = 0;
    while (*name)
        h = (h << 5) + *name++;
    return h % TABLE_SIZE;
}

bucket *make_bucket(char *name) {
    bucket *bp;

    assert(name != NULL);
    bp = (bucket *)malloc(sizeof(bucket));
    if (bp == NULL) no_space();
    
    bp->link = NULL;
    bp->next = NULL;
    bp->name = strdup(name);
    if (bp->name == NULL) no_space();
    
    bp->tag = NULL;
    bp->value = 0;
    bp->index = 0;
    bp->prec = 0;
    bp->class = UNKNOWN;
    bp->assoc = NO_ASSOC;
    
    return bp;
}

bucket *lookup(char *name) {
    bucket *bp, **bpp;
    unsigned int h;

    h = hash(name);
    for (bpp = &symbol_table[h]; (bp = *bpp) != NULL; bpp = &bp->link) {
        if (strcmp(name, bp->name) == 0) return bp;
    }

    bp = make_bucket(name);
    *bpp = bp;
    
    /* Add to linear list */
    if (last_symbol)
        last_symbol->next = bp;
    else
        first_symbol = bp;
    last_symbol = bp;

    return bp;
}

void free_symtab(void) {
    bucket *bp, *next;
    bp = first_symbol;
    while (bp) {
        next = bp->next;
        free(bp->name);
        if (bp->tag) free(bp->tag);
        free(bp);
        bp = next;
    }
}

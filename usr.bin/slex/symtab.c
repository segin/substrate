/*
 * symtab.c - definitions-section names: substitutions and start conditions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symtab.h"
#include "util.h"

static struct definition *definitions = NULL;
static struct start_condition *start_conditions = NULL;

void init_symtab(void) {
    definitions = NULL;
    start_conditions = NULL;
}

void add_definition(const char *name, const char *value) {
    struct definition *def = xmalloc(sizeof(struct definition));

    def->name = xstrdup(name);
    def->value = xstrdup(value);
    def->next = definitions;
    definitions = def;
}

struct definition *find_definition(const char *name) {
    for (struct definition *curr = definitions; curr; curr = curr->next)
        if (strcmp(curr->name, name) == 0)
            return curr;
    return NULL;
}

void add_start_condition(const char *name, bool exclusive) {
    struct start_condition *sc = xmalloc(sizeof(struct start_condition));

    sc->name = xstrdup(name);
    sc->exclusive = exclusive;
    sc->next = start_conditions;
    start_conditions = sc;
}

struct start_condition *find_start_condition(const char *name) {
    for (struct start_condition *curr = start_conditions; curr; curr = curr->next)
        if (strcmp(curr->name, name) == 0)
            return curr;
    return NULL;
}

int get_num_start_conditions(void) {
    int count = 1; /* INITIAL */

    for (struct start_condition *sc = start_conditions; sc; sc = sc->next)
        count++;
    return count;
}

struct start_condition *get_start_conditions(void) {
    return start_conditions;
}

void print_symtab(void) {
    for (struct definition *d = definitions; d; d = d->next)
        printf("DEF: %s = %s\n", d->name, d->value);
    for (struct start_condition *s = start_conditions; s; s = s->next)
        printf("START: %s (%s)\n", s->name,
               s->exclusive ? "exclusive" : "inclusive");
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"

static struct definition *definitions = NULL;
static struct start_condition *start_conditions = NULL;

void init_symtab(void) {
    definitions = NULL;
    start_conditions = NULL;
}

void add_definition(const char *name, const char *value) {
    struct definition *def = malloc(sizeof(struct definition));
    if (!def) {
        perror("malloc");
        exit(1);
    }
    def->name = strdup(name);
    def->value = strdup(value); // Value might need copy or we take ownership
    def->next = definitions;
    definitions = def;
}

struct definition *find_definition(const char *name) {
    struct definition *curr = definitions;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void add_start_condition(const char *name, bool exclusive) {
    struct start_condition *sc = malloc(sizeof(struct start_condition));
    if (!sc) {
        perror("malloc");
        exit(1);
    }
    sc->name = strdup(name);
    sc->exclusive = exclusive;
    sc->next = start_conditions;
    start_conditions = sc;
}

struct start_condition *find_start_condition(const char *name) {
    struct start_condition *curr = start_conditions;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int get_num_start_conditions(void) {
    int count = 1; /* INITIAL */
    struct start_condition *sc = start_conditions;
    while (sc) {
        count++;
        sc = sc->next;
    }
    return count;
}

struct start_condition *get_start_conditions(void) {
    return start_conditions;
}

void print_symtab(void) {
    struct definition *d = definitions;
    while (d) {
        printf("DEF: %s = %s\n", d->name, d->value);
        d = d->next;
    }
    struct start_condition *s = start_conditions;
    while (s) {
        printf("START: %s (%s)\n", s->name, s->exclusive ? "exclusive" : "inclusive");
        s = s->next;
    }
}

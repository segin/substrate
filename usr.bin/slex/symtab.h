#ifndef LEX_SYMTAB_H
#define LEX_SYMTAB_H

#include <stdbool.h>

/* Definition (Substitution String) */
struct definition {
    char *name;
    char *value;
    struct definition *next;
};

/* Start Condition */
struct start_condition {
    char *name;
    bool exclusive; // true = %x, false = %s
    struct start_condition *next;
};

/* Symbol Table API */
void init_symtab(void);
void add_definition(const char *name, const char *value);
struct definition *find_definition(const char *name);

void add_start_condition(const char *name, bool exclusive);
struct start_condition *find_start_condition(const char *name);
int get_num_start_conditions(void);
struct start_condition *get_start_conditions(void);

void print_symtab(void);

#endif

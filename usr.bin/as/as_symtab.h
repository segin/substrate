#ifndef SUBSTRATE_AS_SYMTAB_H
#define SUBSTRATE_AS_SYMTAB_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_SYM_BIND_LOCAL = 0,
    AS_SYM_BIND_GLOBAL,
    AS_SYM_BIND_WEAK,
} as_sym_bind_t;

typedef enum {
    AS_SYM_TYPE_NOTYPE = 0,
    AS_SYM_TYPE_FUNCTION,
    AS_SYM_TYPE_OBJECT,
    AS_SYM_TYPE_TLS_OBJECT,
    AS_SYM_TYPE_COMMON,
} as_sym_type_t;

typedef enum {
    AS_SYM_VIS_DEFAULT = 0,
    AS_SYM_VIS_HIDDEN,
    AS_SYM_VIS_PROTECTED,
    AS_SYM_VIS_INTERNAL,
} as_sym_visibility_t;

typedef struct {
    char *name;
    as_sym_bind_t bind;
    as_sym_type_t type;
    as_sym_visibility_t visibility;
    int defined;
    unsigned def_line;
    char *def_file;
    int is_common;
    unsigned long long common_size;
    unsigned long long common_align;
    int is_absolute;
    unsigned long long absolute_value;
    char *alias_target;
    int alias_from_dot;
    long long alias_addend;
    unsigned long long size;
    char *version;
    size_t reference_count;
    size_t forward_ref_count;
    int unresolved;
} as_symbol_t;

typedef struct {
    as_symbol_t *items;
    size_t count;
    size_t cap;
} as_symtab_t;

void as_symtab_init(as_symtab_t *tab);
void as_symtab_free(as_symtab_t *tab);

int as_symtab_build(const as_parse_result_t *parsed, as_symtab_t *tab,
                    char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif

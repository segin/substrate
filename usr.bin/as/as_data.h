#ifndef SUBSTRATE_AS_DATA_H
#define SUBSTRATE_AS_DATA_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_DATA_NONE = 0,
    AS_DATA_INT,
    AS_DATA_FLOAT,
    AS_DATA_STRING,
    AS_DATA_ZERO,
    AS_DATA_FILL,
    AS_DATA_ORG,
    AS_DATA_INCBIN,
} as_data_kind_t;

typedef struct {
    as_data_kind_t kind;
    char *file;
    unsigned line;
    char *directive;
    union {
        struct {
            unsigned width;
            long long *values;
            size_t count;
        } ints;
        struct {
            int is_double;
            double *values;
            size_t count;
        } floats;
        struct {
            char *bytes;
            size_t len;
            int nul_terminated;
        } str;
        struct {
            unsigned long long count;
        } zero;
        struct {
            unsigned long long repeat;
            unsigned long long size;
            unsigned long long value;
        } fill;
        struct {
            unsigned long long offset;
        } org;
        struct {
            char *path;
            unsigned long long skip;
            unsigned long long count;
            int has_count;
        } incbin;
    } u;
} as_data_op_t;

typedef struct {
    as_data_op_t *items;
    size_t count;
    size_t cap;
} as_data_program_t;

void as_data_program_init(as_data_program_t *p);
void as_data_program_free(as_data_program_t *p);

int as_data_build(const as_parse_result_t *parsed, as_data_program_t *out,
                  char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif

#ifndef LIBJOIN_H
#define LIBJOIN_H

#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int join_field_1;       /* 1-based index, default 1 */
    int join_field_2;       /* 1-based index, default 1 */
    bool unpair_1;          /* -a 1 */
    bool unpair_2;          /* -a 2 */
    bool unpair_only_1;     /* -v 1 */
    bool unpair_only_2;     /* -v 2 */
    bool ignore_case;       /* -i or --ignore-case */
    int check_order;        /* 1 = --check-order, 0 = default, -1 = --nocheck-order */
    bool header;            /* --header */
    bool zero_terminated;   /* -z */
    bool auto_format;       /* -o auto */
    const char *empty_str;  /* -e string */
    char delim;             /* -t char. '\0' means default blank collapsing. */
    bool empty_delim;       /* -t '' Treat whole line as key */
    
    /* custom output format list: array of field specs. 
     * 0 = join field
     * 1 = file1 field
     * 2 = file2 field
     * A record of file and field index.
     */
    struct {
        int file_idx; /* 0 for join field, 1 for file 1, 2 for file 2 */
        int field_idx; /* 1-based, unless file_idx is 0 */
    } *out_list;
    int out_list_len;
} join_options_t;

int join_files(FILE *f1, const char *name1, FILE *f2, const char *name2, const join_options_t *opts, FILE *out);

#ifdef __cplusplus
}
#endif

#endif

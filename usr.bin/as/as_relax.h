#ifndef SUBSTRATE_AS_RELAX_H
#define SUBSTRATE_AS_RELAX_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_BRANCH_KIND_NONE = 0,
    AS_BRANCH_KIND_SHORT,
    AS_BRANCH_KIND_NEAR,
    AS_BRANCH_KIND_FAR,
} as_branch_kind_t;

typedef struct {
    size_t stmt_index;
    char *mnemonic;
    char *target_name;
    unsigned target_line;
    as_branch_kind_t kind;
    long displacement;
    int out_of_range;
    int veneer_needed;
} as_relax_branch_t;

typedef struct {
    as_parser_arch_t arch;
    long x86_short_min;
    long x86_short_max;
    long x86_near_min;
    long x86_near_max;
    long arm_branch_abs_range;
    unsigned max_passes;
} as_relax_cfg_t;

typedef struct {
    as_relax_branch_t *branches;
    size_t branch_count;
    size_t branch_cap;
    unsigned passes;
    int stabilized;
} as_relax_result_t;

void as_relax_result_init(as_relax_result_t *r);
void as_relax_result_free(as_relax_result_t *r);

int as_relax_branches(const as_parse_result_t *parsed, const as_relax_cfg_t *cfg,
                      as_relax_result_t *out, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif

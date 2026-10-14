#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cc_frontend.h"
#include "cc_ssa.h"

int main(void) {
    cc_translation_unit_t tu;
    cc_ssa_module_t ssa;
    cc_diag_t diag;

    memset(&tu, 0, sizeof(tu));
    memset(&ssa, 0, sizeof(ssa));
    memset(&diag, 0, sizeof(diag));

    // Test 1: NULL translation unit
    int result1 = cc_ast_to_ssa(NULL, &ssa, &diag);
    assert(result1 == -1);

    // Test 2: NULL output module
    int result2 = cc_ast_to_ssa(&tu, NULL, &diag);
    assert(result2 == -1);

    // Test 3: Valid but empty translation unit
    int result3 = cc_ast_to_ssa(&tu, &ssa, &diag);
    assert(result3 == 0);

    // Clean up
    cc_ssa_module_free(&ssa);

    return 0;
}

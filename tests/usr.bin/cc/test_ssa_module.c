#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../../../usr.bin/cc/include/cc_ssa.h"

#ifndef HOST_TEST
#define HOST_TEST 1
#endif

void test_cc_ssa_module_init() {
    cc_ssa_module_t m;

    // Fill with garbage to ensure init actually clears it
    memset(&m, 0xFF, sizeof(m));

    cc_ssa_module_init(&m);

    // Check fields are zeroed/NULL
    assert(m.globals == NULL);
    assert(m.global_count == 0);
    assert(m.funcs == NULL);
    assert(m.func_count == 0);
}

int main() {
    test_cc_ssa_module_init();
    printf("PASS\n");
    return 0;
}

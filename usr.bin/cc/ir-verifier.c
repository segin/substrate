#include "ir.h"

#include <stdio.h>

int main(int argc, char **argv) {
    ir_module_t m;
    ir_error_t err;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <input.ir>\n", argv[0]);
        return 2;
    }

    ir_module_init(&m);
    err.msg = NULL;
    err.line = 0;

    if (ir_parse_file(argv[1], &m, &err) != 0) {
        fprintf(stderr, "%s:%zu: parse error: %s\n", argv[1], err.line, err.msg ? err.msg : "unknown");
        ir_error_free(&err);
        ir_module_free(&m);
        return 1;
    }

    if (ir_verify_module(&m, &err) != 0) {
        fprintf(stderr, "%s:%zu: verify error: %s\n", argv[1], err.line, err.msg ? err.msg : "unknown");
        ir_error_free(&err);
        ir_module_free(&m);
        return 1;
    }

    ir_module_free(&m);
    return 0;
}
